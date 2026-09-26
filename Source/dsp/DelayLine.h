#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace undertow::dsp
{

// Línea de retardo: un buffer circular donde se escribe la señal y se lee "desde el pasado". Es la pieza de la
// que salen el chorus, el delay y la reverb. Sin dependencias de JUCE.
//
// El tamaño se reserva UNA vez en prepare (fuera del hilo de audio); escribir y leer no reservan nada.
// El tamaño es potencia de 2: "dar la vuelta" al buffer es un AND de bits en vez de una división.
class DelayLine
{
public:
    // Se llama fuera del hilo de audio. Reserva sitio para 'maxDelaySamples' más el margen de la interpolación.
    void prepare (int maxDelaySamples)
    {
        size_t size = 1;
        while (size < static_cast<size_t> (maxDelaySamples) + 4)
            size <<= 1;
        buffer.assign (size, 0.0f);
        mask = size - 1;
        writeIndex = 0;
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
    }

    void push (float sample) noexcept
    {
        writeIndex = (writeIndex + 1) & mask;
        buffer[writeIndex] = sample;
    }

    // Muestra de hace exactamente 'delay' muestras (0 = la última escrita).
    [[nodiscard]] float read (int delay) const noexcept
    {
        return buffer[(writeIndex - static_cast<size_t> (delay)) & mask];
    }

    // Retardo fraccionario con interpolación lineal: barata y nunca amplifica (segura dentro de una realimentación).
    // Atenúa un poco los agudos cuando la fracción está cerca de 0.5.
    [[nodiscard]] float readLinear (float delay) const noexcept
    {
        const auto whole = static_cast<int> (delay);
        const float fraction = delay - static_cast<float> (whole);
        const float a = read (whole);
        const float b = read (whole + 1);
        return a + (b - a) * fraction;
    }

    // Retardo fraccionario con interpolación cúbica (Catmull-Rom, la misma idea que en el oscilador de la Fase 3):
    // conserva los agudos. Necesita 'delay' ≥ 1.
    [[nodiscard]] float readCubic (float delay) const noexcept
    {
        const auto whole = static_cast<int> (delay);
        const float t = delay - static_cast<float> (whole);
        const float ym1 = read (whole - 1);
        const float y0 = read (whole);
        const float y1 = read (whole + 1);
        const float y2 = read (whole + 2);
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * t + c2) * t + c1) * t + y0;
    }

    [[nodiscard]] int getMaxDelay() const noexcept { return static_cast<int> (buffer.size()) - 4; }

private:
    std::vector<float> buffer = std::vector<float> (4, 0.0f);
    size_t mask = 3;
    size_t writeIndex = 0;
};

// Suavizado de un parámetro con un filtro de 1 polo: se acerca a su objetivo un porcentaje fijo por muestra.
struct SmoothedParameter
{
    float value = 0.0f;
    float coef = 1.0f;

    void setTime (double seconds, double sampleRate) noexcept
    {
        coef = static_cast<float> (1.0 - std::exp (-1.0 / (seconds * sampleRate)));
    }

    // Devuelve true si el valor cambió (para recalcular coeficientes solo cuando hace falta).
    bool approach (float goal) noexcept
    {
        if (value == goal)
            return false;
        value += (goal - value) * coef;
        if (std::abs (goal - value) < 1.0e-6f * std::max (1.0f, std::abs (goal)))
            value = goal;
        return true;
    }
};

// Fundido lineal de encendido/apagado de un efecto. Apagado y con el fundido terminado = no se procesa.
struct EffectSwitch
{
    float gain = 0.0f;
    float step = 1.0f;

    void setTime (double seconds, double sampleRate) noexcept { step = static_cast<float> (1.0 / (seconds * sampleRate)); }
    void snap (bool on) noexcept { gain = on ? 1.0f : 0.0f; }
    [[nodiscard]] bool isSilent (bool on) const noexcept { return ! on && gain == 0.0f; }
    [[nodiscard]] bool isSteady (bool on) const noexcept { return gain == (on ? 1.0f : 0.0f); }

    float next (bool on) noexcept
    {
        gain = on ? std::min (1.0f, gain + step) : std::max (0.0f, gain - step);
        return gain;
    }
};

} // namespace undertow::dsp
