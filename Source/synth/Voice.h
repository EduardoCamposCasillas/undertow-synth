#pragma once

#include <cmath>
#include <cstdint>

#include "dsp/AdsrEnvelope.h"
#include "dsp/Pitch.h"
#include "dsp/WavetableOscillator.h"

namespace undertow::synth
{

// Una voz = un oscilador wavetable + una envolvente. El VoiceManager decide qué nota toca cada una.
class Voice
{
public:
    void prepare (double sampleRate) noexcept
    {
        oscillator.setSampleRate (sampleRate);
        envelope.setSampleRate (sampleRate);
        envelope.reset();
        gainSmoothingCoef = static_cast<float> (1.0 - std::exp (-1.0 / (gainSmoothingSeconds * sampleRate)));
        keyHeld = false;
        sustainedByPedal = false;
    }

    void setEnvelopeParameters (const dsp::AdsrParameters& parameters) noexcept { envelope.setParameters (parameters); }
    void setWavetable (const dsp::Wavetable* table) noexcept { oscillator.setWavetable (table); }
    void setWavetablePosition (float position) noexcept { oscillator.setPosition (position); }

    void start (int midiNote, float gain, std::uint64_t order) noexcept
    {
        if (! envelope.isActive())
        {
            // Voz en silencio: empezar en fase 0 hace que cada nota arranque igual.
            oscillator.reset();
            currentGain = gain;
        }
        // Si la voz ya sonaba (redisparo de la misma nota) NO se reinicia la fase:
        // saltar de fase a mitad de ciclo sería una discontinuidad (clic).

        targetGain = gain;
        noteNumber = midiNote;
        noteOrder = order;
        keyHeld = true;
        sustainedByPedal = false;
        oscillator.setFrequency (dsp::midiNoteToHz (midiNote));
        envelope.noteOn();
    }

    void release() noexcept
    {
        keyHeld = false;
        sustainedByPedal = false;
        envelope.noteOff();
    }

    // Tecla soltada con el pedal pisado: la nota sigue en sustain hasta levantar el pedal.
    void holdWithPedal() noexcept
    {
        keyHeld = false;
        sustainedByPedal = true;
    }

    void steal() noexcept
    {
        keyHeld = false;
        sustainedByPedal = false;
        envelope.quickRelease();
    }

    // Corte seco. Solo como último recurso, si todas las ranuras del pool están ocupadas.
    void hardStop() noexcept { envelope.reset(); }

    // Suma (no sobrescribe) la salida de la voz en 'output': así se mezclan todas las voces.
    void render (float* output, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples && envelope.isActive(); ++i)
        {
            // La ganancia por velocity también se suaviza: al redisparar con otra velocity
            // un salto de volumen instantáneo sonaría como un clic.
            currentGain += (targetGain - currentGain) * gainSmoothingCoef;
            output[i] += oscillator.processSample() * envelope.processSample() * currentGain;
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

private:
    static constexpr double gainSmoothingSeconds = 0.005;

    dsp::WavetableOscillator oscillator;
    dsp::AdsrEnvelope envelope;

    int noteNumber = -1;
    std::uint64_t noteOrder = 0; // cuanto menor, más antigua
    bool keyHeld = false;
    bool sustainedByPedal = false;

    float currentGain = 0.0f;
    float targetGain = 0.0f;
    float gainSmoothingCoef = 1.0f;
};

} // namespace undertow::synth
