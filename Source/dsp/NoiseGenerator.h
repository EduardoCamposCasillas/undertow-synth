#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace undertow::dsp
{

// Ruido con "color". Sin dependencias de JUCE.
//
// Ruido blanco = cada muestra es un número al azar, sin relación con la anterior. Su espectro es plano: la
// misma energía en cada Hz (por eso suena tan agudo: hay muchos más Hz en los agudos que en los graves).
// El color lo inclina con un filtro de 1 polo (6 dB/octava):
//   0 %  → low-pass en 100 Hz: retumbo grave, como viento o mar de fondo.
//   50 % → blanco (sin filtro que se oiga).
//   100 % → high-pass en 10 kHz: solo el "tss" de arriba, como un hi-hat o el aire de una voz.
class NoiseGenerator
{
public:
    void setSampleRate (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        smoothingCoef = static_cast<float> (1.0 - std::exp (-1.0 / (smoothingSeconds * sampleRate)));
        color = targetColor;
        updateCoefficient();
    }

    void setColor (float newColor) noexcept { targetColor = std::clamp (newColor, 0.0f, 1.0f); }

    // Nota nueva desde silencio: cada nota con su propia secuencia (dos notas iguales no suenan idénticas,
    // como dos golpes de un platillo real) y el filtro vacío.
    void reset (std::uint32_t seed) noexcept
    {
        state = seed * 2654435761u + 0x6A09E667u;
        if (state == 0)
            state = 1; // xorshift se queda en 0 para siempre si empieza en 0
        filterState = 0.0f;
        color = targetColor;
        updateCoefficient();
    }

    [[nodiscard]] float processSample() noexcept
    {
        if (color != targetColor)
        {
            color += (targetColor - color) * smoothingCoef;
            if (std::abs (targetColor - color) < 1.0e-5f)
                color = targetColor;
            updateCoefficient();
        }

        // xorshift32: 3 operaciones de bits por muestra, sin reservar memoria ni llamar al sistema.
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        const float white = static_cast<float> (state >> 8) * (2.0f / 16777216.0f) - 1.0f; // uniforme en [-1, 1)

        // Filtro de 1 polo TPT (la misma idea que el SVF de la Fase 4, con un solo integrador).
        const float v = (white - filterState) * gain;
        const float lowPass = v + filterState;
        filterState = lowPass + v;
        return highPassMode ? white - lowPass : lowPass;
    }

private:
    void updateCoefficient() noexcept
    {
        // Cutoff en escala logarítmica: cada tramo de la perilla mueve el mismo número de octavas.
        double cutoff = 0.0;
        highPassMode = color > 0.5f;
        if (highPassMode)
            cutoff = minHighPassHz * std::pow (maxHighPassHz / minHighPassHz, (color - 0.5) * 2.0);
        else
            cutoff = minLowPassHz * std::pow (maxLowPassHz / minLowPassHz, color * 2.0);

        cutoff = std::min (cutoff, 0.45 * sampleRate);
        const double g = std::tan (std::numbers::pi * cutoff / sampleRate);
        gain = static_cast<float> (g / (1.0 + g));
    }

    static constexpr double smoothingSeconds = 0.005;
    static constexpr double minLowPassHz = 100.0;
    static constexpr double maxLowPassHz = 20000.0; // al 50 %: prácticamente blanco
    static constexpr double minHighPassHz = 20.0;   // al 50 %: prácticamente blanco
    static constexpr double maxHighPassHz = 10000.0;

    double sampleRate = 44100.0;
    std::uint32_t state = 1;
    float filterState = 0.0f;
    float gain = 0.5f;
    float color = 0.5f;
    float targetColor = 0.5f;
    float smoothingCoef = 1.0f;
    bool highPassMode = false;
};

} // namespace undertow::dsp
