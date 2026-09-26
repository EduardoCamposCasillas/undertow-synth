#pragma once

#include <algorithm>
#include <array>

#include "dsp/Chorus.h"
#include "dsp/DelayLine.h"
#include "dsp/Distortion.h"
#include "dsp/Reverb.h"
#include "dsp/StereoDelay.h"

namespace undertow::synth
{

// --- Efectos (Fase 8) -----------------------------------------------------------------------------------
//
// Los efectos son GLOBALES: procesan la suma de todas las voces, una sola vez (no por nota). Orden fijo, el
// clásico de una cadena de guitarra o de un sinte: distorsión → chorus → delay → reverb.
//  - La distorsión va primero: distorsionar un eco o una reverb crea una "papilla" sin definición.
//  - El chorus antes del delay: cada eco repite el sonido ya ensanchado.
//  - La reverb al final: es "la sala" donde suena todo lo anterior.

// Divisiones del delay sincronizado. 'beats' = duración en negras. El orden se guarda: solo añadir al final.
struct DelayDivision
{
    const char* name;
    double beats;
};

inline constexpr std::array<DelayDivision, 14> delayDivisions { {
    { "1 bar", 4.0 },
    { "1/2", 2.0 },
    { "1/4", 1.0 },
    { "1/8", 0.5 },
    { "1/16", 0.25 },
    { "1/32", 0.125 },
    { "1/2 T", 4.0 / 3.0 },
    { "1/4 T", 2.0 / 3.0 },
    { "1/8 T", 1.0 / 3.0 },
    { "1/16 T", 1.0 / 6.0 },
    { "1/2 D", 3.0 },
    { "1/4 D", 1.5 },
    { "1/8 D", 0.75 },
    { "1/16 D", 0.375 },
} };

inline constexpr int defaultDelayDivision = 12; // 1/8 D: el eco "galopante" clásico

struct DistortionSettings
{
    bool enabled = false;
    dsp::DistortionParameters parameters;
    bool operator== (const DistortionSettings&) const = default;
};

struct ChorusSettings
{
    bool enabled = false;
    dsp::ChorusParameters parameters;
    bool operator== (const ChorusSettings&) const = default;
};

struct DelaySettings
{
    bool enabled = false;
    bool tempoSync = true;
    float timeSeconds = 0.3f; // sin sync
    int division = defaultDelayDivision;
    dsp::DelayParameters parameters; // su timeSeconds lo calcula la cadena con el tempo

    // Tiempo real del eco: libre o según la división y el tempo del host.
    [[nodiscard]] float resolvedSeconds (double bpm) const noexcept
    {
        if (! tempoSync)
            return timeSeconds;
        const auto& d = delayDivisions[static_cast<size_t> (std::clamp (division, 0, static_cast<int> (delayDivisions.size()) - 1))];
        return static_cast<float> (d.beats * 60.0 / std::max (bpm, 1.0));
    }

    bool operator== (const DelaySettings&) const = default;
};

struct ReverbSettings
{
    bool enabled = false;
    dsp::ReverbParameters parameters;
    bool operator== (const ReverbSettings&) const = default;
};

struct EffectsSettings
{
    DistortionSettings distortion;
    ChorusSettings chorus;
    DelaySettings delay;
    ReverbSettings reverb;
    bool operator== (const EffectsSettings&) const = default;
};

class EffectsChain
{
public:
    // Se llama fuera del hilo de audio: reserva las líneas de retardo (unos 3.5 MB a 96 kHz).
    void prepare (double sampleRate)
    {
        distortion.prepare (sampleRate);
        chorus.prepare (sampleRate);
        delay.prepare (sampleRate);
        reverb.prepare (sampleRate);
        for (auto& s : switches)
        {
            s.setTime (switchSeconds, sampleRate);
            s.snap (false);
        }
        snapSwitches();
    }

    // Una vez por bloque. 'bpm' = tempo del host (para el delay sincronizado).
    void setSettings (const EffectsSettings& newSettings, double bpm) noexcept
    {
        settings = newSettings;
        distortion.setParameters (settings.distortion.parameters);
        chorus.setParameters (settings.chorus.parameters);
        auto delayParameters = settings.delay.parameters;
        delayParameters.timeSeconds = settings.delay.resolvedSeconds (bpm);
        delay.setParameters (delayParameters);
        reverb.setParameters (settings.reverb.parameters);
    }

    // Tras prepare y el primer setSettings: los efectos encendidos empiezan encendidos, sin fundido.
    void snapSwitches() noexcept
    {
        const auto on = enabledFlags();
        for (size_t e = 0; e < numEffects; ++e)
            switches[e].snap (on[e]);
    }

    // Procesa en el sitio. Con 'right' nulo, la salida es mono.
    void process (float* left, float* right, int numSamples) noexcept
    {
        const auto on = enabledFlags();
        runEffect (0, on[0], left, right, numSamples, [this] (float* l, float* r, int n) { distortion.process (l, r, n); },
                   [this] { distortion.reset(); });
        runEffect (1, on[1], left, right, numSamples, [this] (float* l, float* r, int n) { chorus.process (l, r, n); },
                   [this] { chorus.reset (true); });
        runEffect (2, on[2], left, right, numSamples, [this] (float* l, float* r, int n) { delay.process (l, r, n); },
                   [this] { delay.reset (true); });
        runEffect (3, on[3], left, right, numSamples, [this] (float* l, float* r, int n) { reverb.process (l, r, n); },
                   [this] { reverb.reset (true); });
    }

    [[nodiscard]] bool isAnyEffectActive() const noexcept
    {
        const auto on = enabledFlags();
        for (size_t e = 0; e < numEffects; ++e)
            if (! switches[e].isSilent (on[e]))
                return true;
        return false;
    }

    [[nodiscard]] const dsp::Distortion& getDistortion() const noexcept { return distortion; }

private:
    static constexpr size_t numEffects = 4;
    static constexpr double switchSeconds = 0.01;
    static constexpr int chunkSize = 256;

    [[nodiscard]] std::array<bool, numEffects> enabledFlags() const noexcept
    {
        return { settings.distortion.enabled, settings.chorus.enabled, settings.delay.enabled, settings.reverb.enabled };
    }

    // Encendido estable: se procesa en el sitio. Apagado: no se toca la señal (ni se gasta CPU). Durante los
    // 10 ms del fundido se guarda la señal limpia y se mezcla con la procesada.
    template <typename Process, typename Reset>
    void runEffect (size_t e, bool on, float* left, float* right, int numSamples, Process&& processEffect,
                    Reset&& resetEffect) noexcept
    {
        auto& s = switches[e];
        if (s.isSilent (on))
            return;
        if (on && s.gain == 0.0f)
            resetEffect(); // vuelve a encenderse: sin restos de la última vez (una cola vieja de delay, etc.)

        if (s.isSteady (on))
        {
            processEffect (left, right, numSamples);
            return;
        }

        for (int start = 0; start < numSamples; start += chunkSize)
        {
            const int count = std::min (chunkSize, numSamples - start);
            float* l = left + start;
            float* r = right != nullptr ? right + start : nullptr;
            std::copy (l, l + count, dryLeft.begin());
            if (r != nullptr)
                std::copy (r, r + count, dryRight.begin());

            processEffect (l, r, count);

            for (int i = 0; i < count; ++i)
            {
                const float g = s.next (on);
                const auto k = static_cast<size_t> (i);
                l[i] = dryLeft[k] + g * (l[i] - dryLeft[k]);
                if (r != nullptr)
                    r[i] = dryRight[k] + g * (r[i] - dryRight[k]);
            }
        }
    }

    EffectsSettings settings;
    dsp::Distortion distortion;
    dsp::Chorus chorus;
    dsp::StereoDelay delay;
    dsp::Reverb reverb;
    std::array<dsp::EffectSwitch, numEffects> switches;
    std::array<float, chunkSize> dryLeft {}, dryRight {};
};

} // namespace undertow::synth
