#pragma once

#include <array>

#include "dsp/Wavetable.h"

namespace undertow::synth
{

// Fase 6: las fuentes de sonido de cada voz. Dos osciladores wavetable (A y B), un sub-oscilador y ruido.
// Todas se suman y pasan juntas por el filtro.

struct OscillatorSettings
{
    bool enabled = true;
    const dsp::Wavetable* table = nullptr;
    float position = 0.0f; // 0..1
    int octave = 0;        // -4..+4
    int semitones = 0;     // -12..+12
    float fineCents = 0.0f; // -100..+100
    float level = 1.0f;    // 0..1
    float pan = 0.0f;      // -1 (izquierda) .. +1 (derecha)
    int unison = 1;        // 1..16 copias
    float detune = 0.25f;  // 0..1
    float width = 0.8f;    // 0..1, apertura estéreo de las copias

    [[nodiscard]] float tuningSemitones() const noexcept
    {
        return static_cast<float> (12 * octave + semitones) + fineCents / 100.0f;
    }

    bool operator== (const OscillatorSettings&) const = default;
};

// El orden es parte del estado guardado: solo añadir al final. Cada forma es un frame de "Basic Shapes".
enum class SubShape { sine, triangle, saw, square };
inline constexpr std::array<const char*, 4> subShapeNames { "Sine", "Triangle", "Saw", "Square" };

struct SubSettings
{
    bool enabled = false;
    SubShape shape = SubShape::sine;
    int octave = -1; // respecto a la nota: -1 = una octava por debajo
    float level = 0.75f;

    bool operator== (const SubSettings&) const = default;
};

struct NoiseSettings
{
    bool enabled = false;
    float level = 0.5f;
    float color = 0.5f; // 0 = oscuro, 0.5 = blanco, 1 = brillante

    bool operator== (const NoiseSettings&) const = default;
};

struct SourceSettings
{
    // Osc A encendido y Osc B apagado: el sonido por defecto es el de la Fase 5.
    std::array<OscillatorSettings, 2> oscillators { OscillatorSettings {}, OscillatorSettings { .enabled = false } };
    SubSettings sub;
    NoiseSettings noise;

    // Tabla del sub: debe ser "Basic Shapes" (4 frames: seno, triángulo, sierra, cuadrada). Así el sub es
    // tan libre de aliasing como los osciladores principales sin escribir otro oscilador.
    const dsp::Wavetable* subTable = nullptr;

    bool operator== (const SourceSettings&) const = default;
};

inline constexpr int numOscillators = 2;

} // namespace undertow::synth
