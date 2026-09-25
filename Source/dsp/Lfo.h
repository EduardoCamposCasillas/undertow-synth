#pragma once

#include <cmath>
#include <cstdint>
#include <numbers>

namespace undertow::dsp
{

// El orden es parte del estado guardado (el parámetro guarda el índice): solo añadir al final.
enum class LfoShape { sine, triangle, sawUp, sawDown, square, sampleAndHold };

// Fase de un LFO: posición dentro del ciclo [0, 1) y cuántos ciclos completos lleva.
// El contador de ciclos sirve para que el Sample & Hold elija el MISMO valor al azar en todas las voces
// que comparten reloj (modo libre): el valor sale de (semilla, ciclo), no de un generador con estado.
struct LfoPhase
{
    double phase = 0.0;
    std::uint32_t cycle = 0;

    void advance (double increment, int numSamples) noexcept
    {
        phase += increment * numSamples;
        if (phase >= 1.0)
        {
            const double whole = std::floor (phase);
            cycle += static_cast<std::uint32_t> (whole);
            phase -= whole;
        }
    }
};

// LFO (Low Frequency Oscillator): un oscilador demasiado lento para oírse como tono (normalmente
// 0.02–40 Hz) que no suena, sino que MUEVE otro parámetro. Sin dependencias de JUCE.
//
// Salida bipolar en [-1, 1]. La frecuencia no se guarda aquí: cada muestra recibe el incremento de fase
// (ciclos por muestra), que la voz calcula desde Hz o desde el tempo del host.
// No hace falta limitar armónicos (a 40 Hz una sierra no llega a reflejarse de forma audible), pero sí
// evitar clics: los saltos de la sierra, la cuadrada y el S&H se suavizan en la voz, en el destino.
class Lfo
{
public:
    void setShape (LfoShape newShape) noexcept { shape = newShape; }

    // One shot: recorre un solo ciclo y se queda quieto en el valor final (una envolvente dibujada).
    void setOneShot (bool shouldBeOneShot) noexcept { oneShot = shouldBeOneShot; }

    // Nota nueva (modos Retrigger y One Shot): empieza en fase 0 con su propia semilla para el S&H.
    void reset (std::uint32_t newSeed) noexcept
    {
        position = {};
        seed = newSeed;
        finished = false;
        heldValue = randomValue (seed, 0);
    }

    // Modo libre: todas las voces copian la fase del reloj común, así suenan en fase entre sí.
    void syncTo (const LfoPhase& shared, std::uint32_t sharedSeed) noexcept
    {
        if (shared.cycle != position.cycle || sharedSeed != seed)
            heldValue = randomValue (sharedSeed, shared.cycle);
        position = shared;
        seed = sharedSeed;
        finished = false;
    }

    [[nodiscard]] float getValue() const noexcept { return shapeValue (shape, finished ? 1.0 : position.phase, heldValue); }

    void advance (double increment) noexcept
    {
        if (finished)
            return;

        position.phase += increment;
        if (position.phase < 1.0)
            return;

        if (oneShot)
        {
            finished = true;
            return;
        }

        const double whole = std::floor (position.phase);
        position.phase -= whole;
        position.cycle += static_cast<std::uint32_t> (whole);
        heldValue = randomValue (seed, position.cycle);
    }

    [[nodiscard]] double getPhase() const noexcept { return finished ? 1.0 : position.phase; }

    // Valor de cada forma en una fase dada. Todas empiezan en la fase 0 de forma "natural": el seno y el
    // triángulo en el centro subiendo, la sierra ascendente abajo, la cuadrada arriba.
    [[nodiscard]] static float shapeValue (LfoShape shape, double phase, float heldValue) noexcept
    {
        const auto p = static_cast<float> (phase);
        switch (shape)
        {
            case LfoShape::sine: return static_cast<float> (std::sin (2.0 * std::numbers::pi * phase));
            case LfoShape::triangle: return p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f);
            case LfoShape::sawUp: return 2.0f * p - 1.0f;
            case LfoShape::sawDown: return 1.0f - 2.0f * p;
            case LfoShape::square: return p < 0.5f ? 1.0f : -1.0f;
            case LfoShape::sampleAndHold:
            default: return heldValue;
        }
    }

    // Número "al azar" en [-1, 1] determinado por (semilla, ciclo): un hash entero (sin estado, sin
    // reservas de memoria). La misma pareja siempre da el mismo valor.
    [[nodiscard]] static float randomValue (std::uint32_t seed, std::uint32_t cycle) noexcept
    {
        std::uint32_t x = seed ^ (cycle * 0x9E3779B9u);
        x ^= x >> 16;
        x *= 0x7FEB352Du;
        x ^= x >> 15;
        x *= 0x846CA68Bu;
        x ^= x >> 16;
        return static_cast<float> (x) * (2.0f / 4294967295.0f) - 1.0f;
    }

private:
    LfoShape shape = LfoShape::sine;
    bool oneShot = false;
    bool finished = false;
    LfoPhase position;
    std::uint32_t seed = 0;
    float heldValue = 0.0f;
};

} // namespace undertow::dsp
