#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "dsp/DelayLine.h"

namespace undertow::dsp
{

// --- Reverb: red de retardos realimentados (FDN) ------------------------------------------------------------
//
// Una sala real devuelve miles de reflexiones que llegan cada vez más juntas y más débiles. Simularlas una a una
// es imposible; la FDN (Feedback Delay Network, Jot 1991) las fabrica con pocas piezas:
//  - 8 líneas de retardo de 30–75 ms con longitudes "sin relación" entre sí (así los ecos nunca coinciden y no
//    suena a un tubo metálico).
//  - Lo que sale de las 8 líneas se MEZCLA con una matriz (Householder: cada salida = ella misma − 1/4 de la suma
//    de todas) y vuelve a entrar. Cada eco se reparte entre todas las líneas: el número de reflexiones se
//    multiplica en cada vuelta, como en una sala. La matriz es "ortogonal": no suma ni quita energía.
//  - La energía la quita una ganancia < 1 en cada línea, calculada para que el sonido caiga 60 dB justo en el
//    tiempo de Decay (el "RT60" de la acústica): g = 10^(−3·longitud / (RT60·sr)). Una línea más larga da menos
//    vueltas por segundo, así que pierde más por vuelta y todas caen al mismo ritmo.
//  - Damping: un low-pass de 1 polo en cada línea. Los agudos se apagan antes que los graves, como en una sala
//    real (el aire y las paredes absorben más los agudos).
//
// Antes de la red: pre-delay (el hueco entre el sonido directo y la primera reflexión, que el oído usa para
// "medir" la sala) y 4 filtros all-pass en serie por canal (difusión). Un all-pass no cambia el volumen de
// ninguna frecuencia, solo desordena las fases: convierte el golpe de entrada en una nube de ecos densa desde el
// principio. Sin difusión, las primeras vueltas de la FDN se oirían como ecos sueltos ("flutter").
//
// Para que la cola no suene metálica, la longitud de cada línea oscila un poco (±0.3 ms, LFOs lentos distintos):
// las resonancias de la red se mueven y no "pitan". La salida L y R toma las 8 líneas con signos distintos
// (dos filas de una matriz de Hadamard): dos mezclas independientes → una cola ancha y envolvente.

struct ReverbParameters
{
    float size = 0.5f;            // 0..1 → escala las longitudes ×0.4..×1.6 (sala pequeña → grande)
    float decaySeconds = 2.5f;    // RT60: tiempo en caer 60 dB
    float damping = 0.4f;         // 0..1 → low-pass de 20 kHz (sin damping) a 1 kHz
    float preDelaySeconds = 0.02f;
    float mix = 0.3f;             // 0..1

    bool operator== (const ReverbParameters&) const = default;
};

class Reverb
{
public:
    static constexpr int numLines = 8;
    static constexpr double maxPreDelaySeconds = 0.25;
    static constexpr float minDecaySeconds = 0.2f;
    static constexpr float maxDecaySeconds = 30.0f;

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        const auto msToSamples = [this] (double ms) { return static_cast<int> (std::ceil (ms * 0.001 * sampleRate)); };

        for (size_t i = 0; i < numLines; ++i)
            lines[i].prepare (msToSamples (lineLengthsMs[i] * maxScale + modulationDepthMs) + 4);
        for (size_t c = 0; c < 2; ++c)
        {
            preDelays[c].prepare (msToSamples (maxPreDelaySeconds * 1000.0) + 4);
            for (size_t a = 0; a < numDiffusers; ++a)
            {
                diffusers[c][a].length = std::max (1, msToSamples (diffuserLengthsMs[c][a]));
                diffusers[c][a].line.prepare (diffusers[c][a].length + 1);
            }
        }
        for (size_t i = 0; i < numLines; ++i)
            modulationIncrements[i] = modulationRatesHz[i] / sampleRate;

        scale.setTime (0.3, sampleRate); // cambiar el tamaño mueve las longitudes: despacio, sin saltos
        decay.setTime (0.05, sampleRate);
        damping.setTime (0.05, sampleRate);
        preDelay.setTime (0.1, sampleRate);
        mix.setTime (0.02, sampleRate);
        send.setTime (0.01, sampleRate);
        reset();
    }

    void setParameters (const ReverbParameters& newParameters) noexcept { target = newParameters; }

    // Vacía las líneas y salta a los valores de las perillas. Con 'fadeInput' (al volver a encender el efecto) lo
    // que entra a las líneas sube en 10 ms: si no, la señal "empezaría de golpe" dentro de la línea y ese corte se
    // oiría como un clic cuando saliera, un retardo más tarde.
    void reset (bool fadeInput = false) noexcept
    {
        send.snap (! fadeInput);
        for (auto& line : lines)
            line.reset();
        for (size_t c = 0; c < 2; ++c)
        {
            preDelays[c].reset();
            for (auto& diffuser : diffusers[c])
                diffuser.line.reset();
        }
        dampState.fill (0.0f);
        for (size_t i = 0; i < numLines; ++i)
            modulationPhases[i] = 0.125 * static_cast<double> (i);
        scale.value = scaleFor (target.size);
        decay.value = clampedDecay();
        damping.value = target.damping;
        preDelay.value = preDelaySamples();
        mix.value = target.mix;
        updateLoop();
    }

    void process (float* left, float* right, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            bool loopChanged = scale.approach (scaleFor (target.size));
            loopChanged = decay.approach (clampedDecay()) || loopChanged;
            loopChanged = damping.approach (target.damping) || loopChanged;
            if (loopChanged)
                updateLoop();
            preDelay.approach (preDelaySamples());
            mix.approach (target.mix);
            const float sendGain = send.next (true);

            const float inL = left[i];
            const float inR = right != nullptr ? right[i] : inL;

            // Pre-delay y difusión, por canal.
            std::array<float, 2> diffused {};
            for (size_t c = 0; c < 2; ++c)
            {
                preDelays[c].push (sendGain * (c == 0 ? inL : inR));
                float x = preDelays[c].readLinear (preDelay.value);
                for (auto& diffuser : diffusers[c])
                    x = diffuser.process (x);
                diffused[c] = x;
            }

            // Salida de cada línea (con su oscilación lenta) → damping → ganancia de caída.
            std::array<float, numLines> outputs {};
            float sum = 0.0f;
            for (size_t l = 0; l < numLines; ++l)
            {
                const float wobble = modulationDepth * static_cast<float> (std::sin (2.0 * std::numbers::pi * modulationPhases[l]));
                modulationPhases[l] += modulationIncrements[l];
                modulationPhases[l] -= std::floor (modulationPhases[l]);

                const float delayed = lines[l].readLinear (lengths[l] + wobble);
                float& state = dampState[l];
                const float v = (delayed - state) * dampGain;
                const float damped = v + state;
                state = damped + v;

                outputs[l] = delayed;
                const float decayed = damped * gains[l];
                sum += decayed;
                feedback[l] = decayed;
            }

            // Householder: cada línea recibe su propia salida menos 2/N de la suma de todas, más la entrada
            // (canal izquierdo en las líneas pares, derecho en las impares).
            const float householder = sum * (2.0f / numLines);
            for (size_t l = 0; l < numLines; ++l)
                lines[l].push (feedback[l] - householder + inputGain * diffused[l % 2]);

            // Dos filas de Hadamard: + − + − + − + −  y  + + − − + + − −.
            float wetL = 0.0f, wetR = 0.0f;
            for (size_t l = 0; l < numLines; ++l)
            {
                wetL += (l % 2 == 0) ? outputs[l] : -outputs[l];
                wetR += ((l / 2) % 2 == 0) ? outputs[l] : -outputs[l];
            }
            wetL *= outputGain;
            wetR *= outputGain;

            left[i] = inL + mix.value * (wetL - inL);
            if (right != nullptr)
                right[i] = inR + mix.value * (wetR - inR);
        }
    }

    [[nodiscard]] static float scaleFor (float size) noexcept
    {
        return minScale * std::pow (maxScale / minScale, std::clamp (size, 0.0f, 1.0f));
    }

    [[nodiscard]] static float dampingCutoffHz (float damping) noexcept
    {
        return 20000.0f * std::pow (1000.0f / 20000.0f, std::clamp (damping, 0.0f, 1.0f));
    }

private:
    // All-pass de Schroeder: w = x + g·w[n−M];  y = w[n−M] − g·w.  Ganancia 1 en todas las frecuencias.
    struct Diffuser
    {
        DelayLine line;
        int length = 1;

        [[nodiscard]] float process (float x) noexcept
        {
            const float delayed = line.read (length - 1);
            const float w = x + diffusion * delayed;
            line.push (w);
            return delayed - diffusion * w;
        }
    };

    [[nodiscard]] float clampedDecay() const noexcept { return std::clamp (target.decaySeconds, minDecaySeconds, maxDecaySeconds); }

    [[nodiscard]] float preDelaySamples() const noexcept
    {
        const double seconds = std::clamp (static_cast<double> (target.preDelaySeconds), 0.0, maxPreDelaySeconds);
        return static_cast<float> (seconds * sampleRate);
    }

    // Longitudes, ganancias de caída y damping. Solo se recalcula mientras Size, Decay o Damping se mueven.
    void updateLoop() noexcept
    {
        const float samplesPerMs = static_cast<float> (0.001 * sampleRate);
        for (size_t l = 0; l < numLines; ++l)
        {
            lengths[l] = static_cast<float> (lineLengthsMs[l]) * scale.value * samplesPerMs;
            gains[l] = std::pow (10.0f, -3.0f * lengths[l] / (decay.value * static_cast<float> (sampleRate)));
        }
        modulationDepth = static_cast<float> (modulationDepthMs) * samplesPerMs;

        const double hz = std::min (static_cast<double> (dampingCutoffHz (damping.value)), 0.45 * sampleRate);
        const double g = std::tan (std::numbers::pi * hz / sampleRate);
        dampGain = static_cast<float> (g / (1.0 + g));
    }

    static constexpr size_t numDiffusers = 4;
    static constexpr float diffusion = 0.65f;
    static constexpr float minScale = 0.4f;
    static constexpr float maxScale = 1.6f;
    static constexpr double modulationDepthMs = 0.3;
    static constexpr float inputGain = 0.5f;
    static constexpr float outputGain = 0.4f;

    // Longitudes en ms con tamaño ×1. Ningún par es un múltiplo simple de otro: así los ecos de las distintas
    // líneas casi nunca coinciden (si coincidieran, se reforzarían y la cola "pitaría" en esa frecuencia).
    static constexpr std::array<double, numLines> lineLengthsMs { 29.7, 37.1, 41.1, 43.7, 53.9, 59.3, 67.1, 73.3 };
    static constexpr std::array<std::array<double, numDiffusers>, 2> diffuserLengthsMs { {
        { 4.71, 3.59, 12.73, 9.29 },
        { 5.13, 3.31, 11.87, 8.93 },
    } };
    static constexpr std::array<double, numLines> modulationRatesHz { 0.31, 0.43, 0.57, 0.71, 0.37, 0.53, 0.61, 0.79 };

    std::array<DelayLine, numLines> lines;
    std::array<DelayLine, 2> preDelays;
    std::array<std::array<Diffuser, numDiffusers>, 2> diffusers;
    ReverbParameters target;
    double sampleRate = 44100.0;

    SmoothedParameter scale, decay, damping, preDelay, mix;
    EffectSwitch send;
    std::array<float, numLines> lengths {};
    std::array<float, numLines> gains {};
    std::array<float, numLines> dampState {};
    std::array<float, numLines> feedback {};
    std::array<double, numLines> modulationPhases {};
    std::array<double, numLines> modulationIncrements {};
    float modulationDepth = 0.0f;
    float dampGain = 1.0f;
};

} // namespace undertow::dsp
