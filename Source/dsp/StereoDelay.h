#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "dsp/DelayLine.h"

namespace undertow::dsp
{

// --- Delay (eco) ----------------------------------------------------------------------------------------
//
// Una línea de retardo larga (hasta 4 s) con realimentación: lo que sale de la línea vuelve a entrar
// multiplicado por el feedback (< 1). Cada vuelta es un eco más débil: con feedback 50 %, cada eco está 6 dB
// por debajo del anterior. Dentro del lazo hay un low-pass de 1 polo ("Tone"): cada repetición pasa otra vez
// por él y sale más oscura, como en un delay de cinta analógico.
//
// Ping-pong: la entrada (sumada a mono) entra solo en la línea izquierda y cada canal realimenta al OTRO: los
// ecos saltan de izquierda a derecha.
//
// Cambiar el tiempo: saltar de golpe a otra posición de lectura sería un clic. Hay dos soluciones clásicas:
//  - "Cinta": deslizar el tiempo poco a poco. Suena musical pero el tono de los ecos sube o baja mientras cambia.
//  - "Digital": leer a la vez en la posición vieja y en la nueva y hacer un fundido de 50 ms entre ambas.
// Se usa la digital: con tempo sync los ecos no deben desafinarse cuando FL cambia de tempo o se cambia la
// división. El tiempo se redondea a muestras enteras (±10 µs, inaudible): así los ecos no pierden agudos por la
// interpolación, que se acumularía en cada vuelta.

struct DelayParameters
{
    float timeSeconds = 0.5f; // ya resuelto (libre o sincronizado al tempo)
    float feedback = 0.4f;    // 0..1 → 0..95 %
    float tone = 0.7f;        // 0..1 → low-pass de 1 polo en el lazo, de 500 Hz a 20 kHz (100 % = sin filtro)
    float mix = 0.3f;         // 0..1
    bool pingPong = false;

    bool operator== (const DelayParameters&) const = default;
};

class StereoDelay
{
public:
    static constexpr double maxDelaySeconds = 4.0;
    static constexpr float maxFeedback = 0.95f;

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        for (auto& line : lines)
            line.prepare (static_cast<int> (std::ceil (maxDelaySeconds * sampleRate)) + 1);
        feedback.setTime (0.02, sampleRate);
        tone.setTime (0.02, sampleRate);
        mix.setTime (0.02, sampleRate);
        pingPong.setTime (0.02, sampleRate);
        send.setTime (0.01, sampleRate);
        fadeStep = static_cast<float> (1.0 / (timeFadeSeconds * sampleRate));
        reset();
    }

    void setParameters (const DelayParameters& newParameters) noexcept { target = newParameters; }

    // Vacía las líneas y salta a los valores de las perillas. Con 'fadeInput' (al volver a encender el efecto) lo
    // que entra a las líneas sube en 10 ms: si no, la señal "empezaría de golpe" dentro de la línea y ese corte se
    // oiría como un clic cuando saliera, un retardo más tarde.
    void reset (bool fadeInput = false) noexcept
    {
        send.snap (! fadeInput);
        pingPong.value = target.pingPong ? 1.0f : 0.0f;
        for (auto& line : lines)
            line.reset();
        lowPass = { 0.0f, 0.0f };
        currentDelay = nextDelay = targetDelaySamples();
        fade = 1.0f;
        feedback.value = target.feedback;
        tone.value = target.tone;
        mix.value = target.mix;
        updateToneCoefficient();
    }

    void process (float* left, float* right, int numSamples) noexcept
    {
        const int wanted = targetDelaySamples();

        for (int i = 0; i < numSamples; ++i)
        {
            feedback.approach (target.feedback);
            if (tone.approach (target.tone))
                updateToneCoefficient();
            mix.approach (target.mix);
            pingPong.approach (target.pingPong ? 1.0f : 0.0f);
            const float sendGain = send.next (true);

            // Un fundido nuevo solo empieza cuando termina el anterior.
            if (fade >= 1.0f && wanted != currentDelay)
            {
                nextDelay = wanted;
                fade = 0.0f;
            }

            // Se lee ANTES de escribir la muestra actual: read (0) es la de hace 1 muestra, por eso el − 1.
            std::array<float, 2> echo {};
            for (size_t c = 0; c < 2; ++c)
            {
                float read = lines[c].read (currentDelay - 1);
                if (fade < 1.0f)
                    read += (lines[c].read (nextDelay - 1) - read) * fade;
                echo[c] = applyTone (lowPass[c], read);
            }
            if (fade < 1.0f)
            {
                fade = std::min (1.0f, fade + fadeStep);
                if (fade >= 1.0f)
                    currentDelay = nextDelay;
            }

            const float inL = left[i];
            const float inR = right != nullptr ? right[i] : inL;
            const float fb = feedback.value * maxFeedback;

            // Estéreo y ping-pong a la vez, mezclados: cambiar de uno a otro de golpe escribiría un salto dentro
            // de las líneas (que se oiría como clic en el eco siguiente).
            const float p = pingPong.value;
            const float sendL = sendGain * inL, sendR = sendGain * inR;
            const float stereoL = sendL + fb * echo[0];
            const float stereoR = sendR + fb * echo[1];
            const float pingL = 0.5f * (sendL + sendR) + fb * echo[1];
            const float pingR = fb * echo[0];
            lines[0].push (stereoL + p * (pingL - stereoL));
            lines[1].push (stereoR + p * (pingR - stereoR));

            left[i] = inL + mix.value * (echo[0] - inL);
            if (right != nullptr)
                right[i] = inR + mix.value * (echo[1] - inR);
        }
    }

    [[nodiscard]] static float toneCutoffHz (float tone) noexcept
    {
        return minToneHz * std::pow (maxToneHz / minToneHz, std::clamp (tone, 0.0f, 1.0f));
    }

private:
    [[nodiscard]] int targetDelaySamples() const noexcept
    {
        const double samples = std::round (static_cast<double> (target.timeSeconds) * sampleRate);
        return static_cast<int> (std::clamp (samples, 1.0, static_cast<double> (lines[0].getMaxDelay() - 1)));
    }

    // Low-pass de 1 polo TPT (el mismo del ruido de la Fase 6). Con Tone al 100 % no filtra nada.
    [[nodiscard]] float applyTone (float& state, float x) const noexcept
    {
        if (toneBypass)
        {
            state = x; // el estado sigue a la señal: si el filtro vuelve a entrar, empieza sin salto
            return x;
        }
        const float v = (x - state) * toneGain;
        const float y = v + state;
        state = y + v;
        return y;
    }

    void updateToneCoefficient() noexcept
    {
        toneBypass = tone.value >= 1.0f;
        const double hz = std::min (static_cast<double> (toneCutoffHz (tone.value)), 0.45 * sampleRate);
        const double g = std::tan (std::numbers::pi * hz / sampleRate);
        toneGain = static_cast<float> (g / (1.0 + g));
    }

    static constexpr double timeFadeSeconds = 0.05;
    static constexpr float minToneHz = 500.0f;
    static constexpr float maxToneHz = 20000.0f;

    std::array<DelayLine, 2> lines;
    DelayParameters target;
    double sampleRate = 44100.0;
    SmoothedParameter feedback, tone, mix, pingPong;
    EffectSwitch send;
    std::array<float, 2> lowPass {};
    float toneGain = 1.0f;
    bool toneBypass = true;
    int currentDelay = 1, nextDelay = 1;
    float fade = 1.0f, fadeStep = 1.0f;
};

} // namespace undertow::dsp
