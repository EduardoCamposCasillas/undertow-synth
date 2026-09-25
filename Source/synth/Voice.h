#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "dsp/AdsrEnvelope.h"
#include "dsp/Filter.h"
#include "dsp/Lfo.h"
#include "dsp/Pitch.h"
#include "dsp/WavetableOscillator.h"
#include "synth/Modulation.h"

namespace undertow::synth
{

// Ajustes del filtro comunes a todas las voces. Cada voz calcula su propio cutoff a partir de su nota.
struct FilterSettings
{
    bool enabled = false;
    dsp::FilterParameters parameters;
    float keyTrack = 0.0f; // 0..1

    bool operator== (const FilterSettings&) const = default;
};

// Lo que el VoiceManager comparte con todas las voces en cada llamada a render.
struct ModulationContext
{
    const ModulationSettings& settings;
    std::array<double, numLfos> lfoIncrements {};       // ciclos por muestra
    std::array<dsp::LfoPhase, numLfos> freeClocks {};   // fase del reloj común (modo Free)
    std::array<bool, numModSources> sourceUsed {};      // fuentes conectadas a alguna ruta
    bool anyRouted = false;
    float modWheel = 0.0f;
    float aftertouch = 0.0f;
};

// Una voz = oscilador wavetable → filtro → amplificador (envolvente), más sus fuentes de modulación
// propias (Env 2, Env 3, LFOs). El VoiceManager decide qué nota toca cada una.
class Voice
{
public:
    void prepare (double sampleRate) noexcept
    {
        oscillator.setSampleRate (sampleRate);
        envelope.setSampleRate (sampleRate);
        envelope.reset();
        envelope2.setSampleRate (sampleRate);
        envelope2.reset();
        envelope3.setSampleRate (sampleRate);
        envelope3.reset();
        filter.setSampleRate (sampleRate);
        filterMixCoef = smoothingCoefficient (gainSmoothingSeconds, sampleRate);
        filterMix = filterSettings.enabled ? 1.0f : 0.0f;
        gainSmoothingCoef = smoothingCoefficient (gainSmoothingSeconds, sampleRate);
        amountSmoothingCoef = smoothingCoefficient (amountSmoothingSeconds, sampleRate);
        controllerSmoothingCoef = smoothingCoefficient (controllerSmoothingSeconds, sampleRate);
        lfoSmoothingCoef = smoothingCoefficient (lfoSmoothingSeconds, sampleRate);
        keyHeld = false;
        sustainedByPedal = false;
        snapModulation = true;
    }

    void setEnvelopeParameters (const dsp::AdsrParameters& parameters) noexcept { envelope.setParameters (parameters); }

    void setModulationEnvelopes (const dsp::AdsrParameters& env2, const dsp::AdsrParameters& env3) noexcept
    {
        envelope2.setParameters (env2);
        envelope3.setParameters (env3);
    }

    void setWavetable (const dsp::Wavetable* table) noexcept { oscillator.setWavetable (table); }
    void setWavetablePosition (float position) noexcept { oscillator.setPosition (position); }

    void setFilterSettings (const FilterSettings& settings) noexcept
    {
        const bool switchedOnFromSilence = settings.enabled && ! filterSettings.enabled && filterMix == 0.0f;

        filterSettings = settings;
        updateFilterCutoff();

        // Encender el filtro con una nota sonando: arranca desde estado limpio y entra con un fundido.
        if (switchedOnFromSilence)
            filter.reset();
    }

    // 'lfoSeed' hace que el Sample & Hold de cada nota (en modo Retrigger) siga su propia secuencia.
    void start (int midiNote, float velocity, float gain, std::uint64_t order, std::uint32_t lfoSeed) noexcept
    {
        targetGain = gain;
        noteNumber = midiNote;
        noteVelocity = velocity;
        noteOrder = order;
        keyHeld = true;
        sustainedByPedal = false;
        oscillator.setFrequency (dsp::midiNoteToHz (midiNote));
        updateFilterCutoff();

        if (! envelope.isActive())
        {
            // Voz en silencio: empezar en fase 0 hace que cada nota arranque igual, y el filtro
            // empieza vacío y con el cutoff de ESTA nota (sin barrer desde el de la nota anterior).
            oscillator.reset();
            filter.reset();
            filterMix = filterSettings.enabled ? 1.0f : 0.0f;
            currentGain = gain;

            // Lo mismo con la modulación: las envolventes 2 y 3 salen de 0 y la primera muestra usa
            // directamente los valores de esta nota (sin deslizarse desde los de la anterior).
            envelope2.reset();
            envelope3.reset();
            snapModulation = true;
        }
        // Si la voz ya sonaba (redisparo de la misma nota) NO se reinicia la fase ni el filtro:
        // saltar a mitad de ciclo sería una discontinuidad (clic). Las envolventes siguen desde su nivel.

        // Retrigger / One Shot: cada nota reinicia el LFO. (En modo Free, render lo vuelve a alinear con el reloj común.)
        for (size_t l = 0; l < lfos.size(); ++l)
            lfos[l].reset (lfoSeed + static_cast<std::uint32_t> (l));

        envelope.noteOn();
        envelope2.noteOn();
        envelope3.noteOn();
    }

    void release() noexcept
    {
        keyHeld = false;
        sustainedByPedal = false;
        envelope.noteOff();
        envelope2.noteOff();
        envelope3.noteOff();
    }

    // Tecla soltada con el pedal pisado: la nota sigue en sustain hasta levantar el pedal.
    void holdWithPedal() noexcept
    {
        keyHeld = false;
        sustainedByPedal = true;
    }

    // Las envolventes de modulación NO se sueltan al robar: durante el fade de 5 ms el timbre no cambia.
    void steal() noexcept
    {
        keyHeld = false;
        sustainedByPedal = false;
        envelope.quickRelease();
    }

    // Corte seco. Solo como último recurso, si todas las ranuras del pool están ocupadas.
    void hardStop() noexcept
    {
        envelope.reset();
        envelope2.reset();
        envelope3.reset();
    }

    // Suma (no sobrescribe) la salida de la voz en 'output': así se mezclan todas las voces.
    void render (float* output, int numSamples, const ModulationContext& context) noexcept
    {
        std::array<float, numModSources> sources {};
        sources[index (ModSource::key)] = static_cast<float> (noteNumber - 60) / keySemitonesPerUnit;

        // Un LFO se calcula si lo usa una ruta de los ajustes o una ruta que todavía se está apagando en esta voz.
        std::array<bool, numLfos> lfoNeeded {};
        for (size_t l = 0; l < lfos.size(); ++l)
        {
            const auto source = static_cast<ModSource> (index (ModSource::lfo1) + l);
            lfoNeeded[l] = context.sourceUsed[index (source)];
            for (const auto& route : activeRoutes)
                lfoNeeded[l] = lfoNeeded[l] || (route.isRouted() && route.source == source);

            const auto& lfoSettings = context.settings.lfos[l];
            lfos[l].setShape (lfoSettings.shape);
            lfos[l].setOneShot (lfoSettings.mode == LfoMode::oneShot);
            if (lfoSettings.mode == LfoMode::free)
                lfos[l].syncTo (context.freeClocks[l], freeRunningSeed + static_cast<std::uint32_t> (l));
        }

        for (int i = 0; i < numSamples && envelope.isActive(); ++i)
        {
            // La ganancia por velocity también se suaviza: al redisparar con otra velocity
            // un salto de volumen instantáneo sonaría como un clic.
            currentGain += (targetGain - currentGain) * gainSmoothingCoef;

            // --- Fuentes de modulación de esta muestra ---
            // Las envolventes son continuas por construcción: se usan tal cual (un attack de 1 ms llega en 1 ms).
            const float amplitude = envelope.processSample();
            sources[index (ModSource::env1)] = amplitude;
            sources[index (ModSource::env2)] = envelope2.processSample();
            sources[index (ModSource::env3)] = envelope3.processSample();

            // Las fuentes con saltos sí se suavizan: la cuadrada, las sierras y el S&H de los LFO (1 ms), y la
            // velocity al redisparar una nota y los controladores MIDI, que llegan en escalones de 1/127 (10 ms).
            for (size_t l = 0; l < lfos.size(); ++l)
            {
                // El seno cuesta un sin() por muestra: si ninguna ruta usa el LFO, solo avanza su fase.
                if (lfoNeeded[l])
                {
                    const float raw = lfos[l].getValue();
                    lfoOutputs[l] = snapModulation ? raw : lfoOutputs[l] + (raw - lfoOutputs[l]) * lfoSmoothingCoef;
                    sources[index (ModSource::lfo1) + l] = lfoOutputs[l];
                }
                lfos[l].advance (context.lfoIncrements[l]);
            }

            if (snapModulation)
            {
                smoothedVelocity = noteVelocity;
                modWheel = context.modWheel;
                aftertouch = context.aftertouch;
            }
            smoothedVelocity += (noteVelocity - smoothedVelocity) * controllerSmoothingCoef;
            modWheel += (context.modWheel - modWheel) * controllerSmoothingCoef;
            aftertouch += (context.aftertouch - aftertouch) * controllerSmoothingCoef;
            sources[index (ModSource::velocity)] = smoothedVelocity;
            sources[index (ModSource::modWheel)] = modWheel;
            sources[index (ModSource::aftertouch)] = aftertouch;

            // Sin rutas (ni rutas apagándose) no hay nada que calcular: el sonido es el de la Fase 4
            // y la matriz no cuesta CPU.
            float volume = 1.0f;
            if (context.anyRouted || hasActiveRoutes)
            {
                computeModulation (sources, context.settings.slots);
                const auto modulationOf = [this] (ModDestination d) { return destinationValues[index (d)]; };

                // --- Destinos ---
                oscillator.setPositionModulation (modulationOf (ModDestination::oscAPosition));
                oscillator.setPitchModulation (modulationOf (ModDestination::oscAPitch) * pitchModulationSemitones);
                filter.setModulation (modulationOf (ModDestination::filterCutoff) * cutoffModulationOctaves,
                                      modulationOf (ModDestination::filterResonance),
                                      modulationOf (ModDestination::filterDrive));
                // Volume: 0 % = sin cambio; -100 % = silencio; +100 % = el doble (+6 dB).
                volume = std::clamp (1.0f + modulationOf (ModDestination::volume), 0.0f, 2.0f);
            }
            snapModulation = false; // solo la primera muestra de una nota salta directo a sus valores

            float sample = oscillator.processSample();

            // Encender/apagar el filtro es un fundido de 5 ms entre la señal limpia y la filtrada.
            // Apagado del todo no se procesa: con el filtro en off el sonido es el de la Fase 3, sin gasto de CPU.
            const float mixTarget = filterSettings.enabled ? 1.0f : 0.0f;
            filterMix += (mixTarget - filterMix) * filterMixCoef;
            if (std::abs (mixTarget - filterMix) < 1.0e-5f)
                filterMix = mixTarget;
            if (filterMix > 0.0f)
                sample += filterMix * (filter.processSample (sample) - sample);

            output[i] += sample * amplitude * currentGain * volume;
        }
    }

    [[nodiscard]] bool isActive() const noexcept { return envelope.isActive(); }
    [[nodiscard]] bool isBeingStolen() const noexcept { return envelope.getStage() == dsp::AdsrEnvelope::Stage::quickRelease; }
    // "Tocando" = suena y no está siendo robada. Es lo que cuenta para el límite de polifonía.
    [[nodiscard]] bool isPlaying() const noexcept { return isActive() && ! isBeingStolen(); }
    [[nodiscard]] bool isKeyHeld() const noexcept { return keyHeld; }
    [[nodiscard]] bool isSustainedByPedal() const noexcept { return sustainedByPedal; }
    [[nodiscard]] int getNote() const noexcept { return noteNumber; }
    [[nodiscard]] std::uint64_t getNoteOrder() const noexcept { return noteOrder; }
    [[nodiscard]] float getEnvelopeLevel() const noexcept { return envelope.getLevel(); }
    [[nodiscard]] dsp::AdsrEnvelope::Stage getEnvelopeStage() const noexcept { return envelope.getStage(); }
    [[nodiscard]] double getLfoPhase (int lfo) const noexcept { return lfos[static_cast<size_t> (lfo)].getPhase(); }
    [[nodiscard]] float getModulation (ModDestination d) const noexcept { return destinationValues[index (d)]; }
    [[nodiscard]] double getOscillatorFrequency() const noexcept { return oscillator.getFrequency(); }
    [[nodiscard]] float getFilterCutoffHz() const noexcept { return filter.getEffectiveCutoffHz(); }

    // Semilla común del S&H en modo Free: todas las voces sacan los mismos valores al azar.
    static constexpr std::uint32_t freeRunningSeed = 0x5EED0000u;

private:
    template <typename Enum>
    [[nodiscard]] static constexpr size_t index (Enum value) noexcept { return static_cast<size_t> (value); }

    [[nodiscard]] static float smoothingCoefficient (double seconds, double sampleRate) noexcept
    {
        return static_cast<float> (1.0 - std::exp (-1.0 / (seconds * sampleRate)));
    }

    static void approach (float& value, float goal, float coef) noexcept
    {
        value += (goal - value) * coef;
        if (std::abs (goal - value) < 1.0e-6f)
            value = goal; // sin esto el valor "casi llega" para siempre y los destinos se recalcularían sin parar
    }

    // La matriz: suma fuente × amount de cada ruta en su destino.
    //
    // Cada voz guarda la ruta que está sonando (activeRoutes) aparte de la que piden los ajustes. El amount se
    // suaviza 5 ms, como cualquier perilla. Si el usuario cambia la fuente o el destino de una ruta con el
    // amount subido, pasar de golpe de una ruta a otra sería un salto (un clic en Volume, un "chasquido" de
    // tono en Pitch): la ruta vieja se apaga primero en 5 ms y la nueva entra después desde 0.
    void computeModulation (const std::array<float, numModSources>& sources,
                            const std::array<ModSlot, numModSlots>& slots) noexcept
    {
        std::array<float, numModDestinations> sums {};
        hasActiveRoutes = false;

        for (size_t s = 0; s < slots.size(); ++s)
        {
            const auto& wanted = slots[s];
            auto& route = activeRoutes[s];

            if (snapModulation)
                route = wanted.isRouted() ? wanted : ModSlot {};

            const bool sameRoute = route.source == wanted.source && route.destination == wanted.destination;
            approach (route.amount, sameRoute && wanted.isRouted() ? wanted.amount : 0.0f, amountSmoothingCoef);

            if (! sameRoute && route.amount == 0.0f)
                route = { wanted.source, wanted.destination, 0.0f }; // la vieja ya se apagó: entra la nueva

            if (route.isRouted())
            {
                sums[index (route.destination)] += sources[index (route.source)] * route.amount;
                hasActiveRoutes = hasActiveRoutes || route.amount != 0.0f || wanted.isRouted();
            }
        }

        destinationValues = sums;
    }

    void updateFilterCutoff() noexcept
    {
        auto parameters = filterSettings.parameters;
        if (noteNumber >= 0)
            parameters.cutoffHz = dsp::keyTrackedCutoff (parameters.cutoffHz, noteNumber, filterSettings.keyTrack);
        filter.setParameters (parameters);
    }

    static constexpr double gainSmoothingSeconds = 0.005;
    static constexpr double amountSmoothingSeconds = 0.005;
    static constexpr double controllerSmoothingSeconds = 0.01;
    static constexpr double lfoSmoothingSeconds = 0.001;

    dsp::WavetableOscillator oscillator;
    dsp::Filter filter;
    dsp::AdsrEnvelope envelope; // Env 1: amplitud
    dsp::AdsrEnvelope envelope2;
    dsp::AdsrEnvelope envelope3;
    std::array<dsp::Lfo, numLfos> lfos;

    FilterSettings filterSettings;
    float filterMix = 0.0f;
    float filterMixCoef = 1.0f;

    std::array<ModSlot, numModSlots> activeRoutes {}; // las rutas que suenan en esta voz (ver computeModulation)
    std::array<float, numLfos> lfoOutputs {};         // salida suavizada de cada LFO
    std::array<float, numModDestinations> destinationValues {};
    float smoothedVelocity = 0.0f;
    float modWheel = 0.0f;
    float aftertouch = 0.0f;
    float amountSmoothingCoef = 1.0f;
    float controllerSmoothingCoef = 1.0f;
    float lfoSmoothingCoef = 1.0f;
    bool snapModulation = true;
    bool hasActiveRoutes = false; // alguna ruta suena o se está apagando en esta voz

    int noteNumber = -1;
    float noteVelocity = 0.0f;
    std::uint64_t noteOrder = 0; // cuanto menor, más antigua
    bool keyHeld = false;
    bool sustainedByPedal = false;

    float currentGain = 0.0f;
    float targetGain = 0.0f;
    float gainSmoothingCoef = 1.0f;
};

} // namespace undertow::synth
