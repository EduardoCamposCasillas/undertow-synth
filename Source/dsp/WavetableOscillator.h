#pragma once

#include <algorithm>
#include <cmath>

#include "dsp/Wavetable.h"

namespace undertow::dsp
{

// Oscilador wavetable sin aliasing audible. Sin dependencias de JUCE para poder probarlo aislado.
//
// Tres ideas:
//  1) Acumulador de fase (como en la Fase 1): la fase en [0, 1) multiplicada por el tamaño del frame
//     es la posición de lectura dentro del ciclo.
//  2) Mipmaps: según la frecuencia de la nota se lee la versión del ciclo con los armónicos justos
//     para que ninguno se refleje (alias) dentro de la banda audible.
//  3) Morphing: la posición (0..1) cae entre dos frames y se mezclan los dos.
class WavetableOscillator
{
public:
    // Anchura de la zona de fundido entre mipmaps: 1/6 de octava (2 semitonos) antes de cada límite.
    static constexpr double mipBlendOctaves = 1.0 / 6.0;

    void setSampleRate (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;

        // Un armónico por encima de Nyquist (sr/2) se refleja: si está en f, aparece en sr - f.
        // Si f < sr - 20 kHz, su reflejo cae POR ENCIMA de 20 kHz y no se oye. A 44.1/48 kHz eso deja
        // subir los armónicos hasta 24-28 kHz en lugar de 22-24 kHz: las notas agudas conservan más brillo.
        // A 88.2/96 kHz no hace falta: una octava por debajo de Nyquist ya cubre toda la banda audible,
        // así que ahí no se permite ningún reflejo.
        const double nyquist = 0.5 * sampleRate;
        maxHarmonicFrequency = nyquist >= 2.0 * audibleLimitHz ? nyquist
                                                                : std::max (nyquist, sampleRate - audibleLimitHz);

        positionSmoothingCoef = static_cast<float> (1.0 - std::exp (-1.0 / (positionSmoothingSeconds * sampleRate)));
        crossfadeLength = std::max (1, static_cast<int> (tableCrossfadeSeconds * sampleRate));
        updateIncrement();
    }

    void setFrequency (double newFrequencyHz) noexcept
    {
        baseFrequencyHz = newFrequencyHz;
        updateIncrement();
    }

    // Modulación de tono en semitonos (vibrato, caídas de pitch). La voz la llama en cada muestra:
    // si el valor no cambió no se recalcula nada.
    void setPitchModulation (float semitones) noexcept
    {
        if (semitones == pitchModulation)
            return;
        pitchModulation = semitones;
        updateIncrement();
    }

    // Modulación de Position (fracción del recorrido, -1..1). Se suma DESPUÉS del suavizado de la perilla:
    // la perilla se mueve con inercia (sin zipper) y la modulación llega al instante (un LFO rápido o un
    // attack corto no se "emborronan").
    void setPositionModulation (float amount) noexcept { positionModulation = amount; }

    // Cambiar de tabla con la nota sonando haría un salto en la forma de onda (clic):
    // durante unos milisegundos se leen las dos y se hace un fundido cruzado.
    void setWavetable (const Wavetable* newTable) noexcept
    {
        if (newTable == table)
            return;

        if (table != nullptr && newTable != nullptr)
        {
            previousTable = table;
            crossfadeRemaining = crossfadeLength;
        }
        table = newTable;
    }

    void setPosition (float newPosition) noexcept { targetPosition = std::clamp (newPosition, 0.0f, 1.0f); }

    // Para una nota que empieza desde silencio: fase 0 (cada nota arranca igual) y sin arrastrar
    // transiciones (morph o fundido de tabla) que se quedaron a medias en la nota anterior.
    void reset() noexcept
    {
        phase = 0.0;
        position = targetPosition;
        previousTable = nullptr;
        crossfadeRemaining = 0;
    }

    [[nodiscard]] float processSample() noexcept
    {
        if (table == nullptr)
            return 0.0f;

        // Position se suaviza por muestra: girar la perilla recorre los frames intermedios en vez de saltar.
        position += (targetPosition - position) * positionSmoothingCoef;
        const float framePosition = std::clamp (position + positionModulation, 0.0f, 1.0f);

        float output = readBlended (*table, framePosition);

        if (crossfadeRemaining > 0)
        {
            const float oldWeight = static_cast<float> (crossfadeRemaining) / static_cast<float> (crossfadeLength);
            output += (readBlended (*previousTable, framePosition) - output) * oldWeight;
            if (--crossfadeRemaining == 0)
                previousTable = nullptr;
        }

        phase += phaseIncrement;
        if (phase >= 1.0)
            phase -= 1.0;

        return output;
    }

    [[nodiscard]] int getMipLevel() const noexcept { return mipLevel; }
    [[nodiscard]] float getMipBlend() const noexcept { return mipBlend; }
    [[nodiscard]] double getFrequency() const noexcept { return frequencyHz; }

private:
    // Cerca del límite entre dos mipmaps se mezclan los dos niveles (ver updateIncrement).
    [[nodiscard]] float readBlended (const Wavetable& wavetable, float framePosition) const noexcept
    {
        const float rich = read (wavetable, mipLevel, framePosition);
        if (mipBlend <= 0.0f)
            return rich;
        return rich + (read (wavetable, mipLevel + 1, framePosition) - rich) * mipBlend;
    }

    [[nodiscard]] float read (const Wavetable& wavetable, int level, float normalisedPosition) const noexcept
    {
        const int numFrames = wavetable.getNumFrames();

        const double readPosition = phase * Wavetable::levelSize (level);
        const int index = static_cast<int> (readPosition);
        const auto fraction = static_cast<float> (readPosition - index);

        const float* frameA = wavetable.getFrame (level, 0);
        if (numFrames == 1)
            return interpolate (frameA + index, fraction);

        const float framePosition = normalisedPosition * static_cast<float> (numFrames - 1);
        const int firstFrame = std::min (static_cast<int> (framePosition), numFrames - 2);
        const float frameFraction = framePosition - static_cast<float> (firstFrame);

        frameA = wavetable.getFrame (level, firstFrame);
        const float* frameB = wavetable.getFrame (level, firstFrame + 1);

        const float a = interpolate (frameA + index, fraction);
        const float b = interpolate (frameB + index, fraction);
        return a + (b - a) * frameFraction;
    }

    // Interpolación cúbica de Catmull-Rom con 4 muestras (p[-1], p[0], p[1], p[2]).
    // La lineal (2 muestras) es más barata pero apaga los armónicos altos y deja "imágenes" espurias;
    // la cúbica pasa por las muestras y además respeta la pendiente de la onda en cada punto.
    [[nodiscard]] static float interpolate (const float* p, float t) noexcept
    {
        const float c1 = 0.5f * (p[1] - p[-1]);
        const float c2 = p[-1] - 2.5f * p[0] + 2.0f * p[1] - 0.5f * p[2];
        const float c3 = 0.5f * (p[2] - p[-1]) + 1.5f * (p[0] - p[1]);
        return ((c3 * t + c2) * t + c1) * t + p[0];
    }

    void updateIncrement() noexcept
    {
        frequencyHz = pitchModulation == 0.0f ? baseFrequencyHz
                                              : baseFrequencyHz * std::exp2 (static_cast<double> (pitchModulation) / 12.0);
        phaseIncrement = sampleRate > 0.0 ? frequencyHz / sampleRate : 0.0;

        // Nivel de mipmap: el primero (el más rico) cuyo armónico más alto no supera el límite.
        // Ejemplo a 48 kHz (límite 28 kHz): A5 = 440 Hz admite 63 armónicos -> nivel 5 (32 armónicos).
        // Solo se recalcula cuando cambia el tono (nota o pitch modulado), no en cada muestra.
        const double allowedHarmonics = maxHarmonicFrequency / std::max (frequencyHz, 1.0e-3);
        mipLevel = 0;
        while (mipLevel < Wavetable::numLevels - 1 && Wavetable::maxHarmonicsAtLevel (mipLevel) > allowedHarmonics)
            ++mipLevel;

        // Fundido entre mipmaps. Al subir el tono y cruzar un límite, el nivel siguiente tiene la mitad de
        // armónicos: con un cambio seco, la octava más aguda del sonido desaparecería de golpe (un salto de
        // brillo audible con vibrato o pitch bend). Por eso, en la franja de mipBlendOctaves antes del límite
        // se mezcla con el nivel siguiente de 0 % a 100 %: al llegar al límite ya suena solo el nivel siguiente
        // y el cambio es continuo. Se mezcla hacia el nivel MÁS POBRE (nunca hacia uno que se reflejaría),
        // así que no añade aliasing: el precio es un poco menos de brillo dentro de esa franja.
        mipBlend = 0.0f;
        if (mipLevel < Wavetable::numLevels - 1)
        {
            const double headroomOctaves = std::log2 (allowedHarmonics / Wavetable::maxHarmonicsAtLevel (mipLevel));
            mipBlend = static_cast<float> (std::clamp (1.0 - headroomOctaves / mipBlendOctaves, 0.0, 1.0));
        }
    }

    static constexpr double audibleLimitHz = 20000.0;
    static constexpr double positionSmoothingSeconds = 0.01;
    static constexpr double tableCrossfadeSeconds = 0.005;

    const Wavetable* table = nullptr;
    const Wavetable* previousTable = nullptr;
    int crossfadeRemaining = 0;
    int crossfadeLength = 1;

    double sampleRate = 44100.0;
    double baseFrequencyHz = 440.0; // la de la nota
    double frequencyHz = 440.0;     // la que suena: nota + modulación de pitch
    double maxHarmonicFrequency = 22050.0;
    double phase = 0.0; // double: con float la fase acumula error audible en notas graves
    double phaseIncrement = 0.0;
    float pitchModulation = 0.0f; // semitonos
    int mipLevel = 0;
    float mipBlend = 0.0f;

    float targetPosition = 0.0f;
    float position = 0.0f;
    float positionModulation = 0.0f;
    float positionSmoothingCoef = 1.0f;
};

} // namespace undertow::dsp
