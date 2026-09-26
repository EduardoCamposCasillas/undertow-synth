#pragma once

#include <array>
#include <cmath>
#include <numbers>

#include "dsp/DelayLine.h"

namespace undertow::dsp
{

// --- Chorus ---------------------------------------------------------------------------------------------
//
// Idea: sumar a la señal copias de sí misma con un retardo corto (unos 10 ms) que un LFO lento hace variar.
// Cuando el retardo crece, la copia se lee "cada vez más atrás" y suena un poco más grave; cuando decrece, un
// poco más aguda (efecto Doppler). Esas copias levemente desafinadas y que se mueven son lo que oímos como
// "varios instrumentos a la vez": el mismo principio que el detune del unison, pero aplicado a TODO el sonido.
//
// Aquí: una copia por canal, con el LFO del derecho invertido (180°) respecto al del izquierdo, como en el
// chorus clásico del Roland Juno: cuando la copia de la izquierda sube de tono, la de la derecha baja. L y R nunca
// son iguales: el chorus también ensancha la imagen estéreo. (Dos copias en el MISMO canal se cancelarían entre
// sí a ratos y bajaría el volumen.)
// Con realimentación (feedback) la salida vuelve a entrar en la línea: las copias se repiten y se acerca a un
// flanger (el "silbido" metálico de un avión).
//
// La lectura usa interpolación cúbica: el retardo cambia en cada muestra y una interpolación lineal apagaría
// los agudos de forma desigual (más o menos según la fracción, que cambia continuamente).

struct ChorusParameters
{
    float rateHz = 0.8f;   // velocidad del LFO
    float depth = 0.5f;    // 0..1 → ±0..7 ms alrededor del retardo central
    float feedback = 0.0f; // 0..1 → 0..90 %
    float mix = 0.5f;      // 0..1

    bool operator== (const ChorusParameters&) const = default;
};

class Chorus
{
public:
    static constexpr float centreDelayMs = 10.0f;
    static constexpr float maxDepthMs = 7.0f;
    static constexpr float maxFeedback = 0.9f;

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        for (auto& line : lines)
            line.prepare (static_cast<int> (std::ceil ((centreDelayMs + maxDepthMs) * 0.001 * sampleRate)) + 4);
        depth.setTime (0.05, sampleRate); // cambiar la profundidad de golpe sería un salto de tono
        feedback.setTime (0.02, sampleRate);
        mix.setTime (0.02, sampleRate);
        send.setTime (0.01, sampleRate);
        reset();
    }

    void setParameters (const ChorusParameters& newParameters) noexcept { target = newParameters; }

    // Vacía las líneas y salta a los valores de las perillas. Con 'fadeInput' (al volver a encender el efecto) lo
    // que entra a las líneas sube en 10 ms: si no, la señal "empezaría de golpe" dentro de la línea y ese corte se
    // oiría como un clic cuando saliera, un retardo más tarde.
    void reset (bool fadeInput = false) noexcept
    {
        send.snap (! fadeInput);
        for (auto& line : lines)
            line.reset();
        lastWet = { 0.0f, 0.0f };
        phase = 0.0;
        depth.value = target.depth;
        feedback.value = target.feedback;
        mix.value = target.mix;
    }

    void process (float* left, float* right, int numSamples) noexcept
    {
        const double increment = static_cast<double> (target.rateHz) / sampleRate;
        const float samplesPerMs = static_cast<float> (0.001 * sampleRate);
        const float centre = centreDelayMs * samplesPerMs;

        for (int i = 0; i < numSamples; ++i)
        {
            depth.approach (target.depth);
            feedback.approach (target.feedback);
            mix.approach (target.mix);
            const float sendGain = send.next (true);

            const float swing = depth.value * maxDepthMs * samplesPerMs;
            const auto lfo = static_cast<float> (std::sin (2.0 * std::numbers::pi * phase));
            const float fb = feedback.value * maxFeedback;

            for (size_t c = 0; c < 2; ++c)
            {
                float* channel = c == 0 ? left : right;
                if (channel == nullptr)
                    continue;

                const float input = channel[i];
                lines[c].push (sendGain * input + fb * lastWet[c]);

                const float wet = lines[c].readCubic (centre + (c == 0 ? swing : -swing) * lfo);
                lastWet[c] = wet;
                channel[i] = input + mix.value * (wet - input);
            }

            phase += increment;
            phase -= std::floor (phase);
        }
    }

    [[nodiscard]] double getPhase() const noexcept { return phase; }

private:
    std::array<DelayLine, 2> lines;
    ChorusParameters target;
    double sampleRate = 44100.0;
    double phase = 0.0;
    SmoothedParameter depth, feedback, mix;
    EffectSwitch send;
    std::array<float, 2> lastWet {};
};

} // namespace undertow::dsp
