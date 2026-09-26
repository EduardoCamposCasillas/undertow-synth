#pragma once

#include <array>

#include "dsp/Warp.h"
#include "dsp/Wavetable.h"

namespace undertow::synth
{

// Fase 6: las fuentes de sonido de cada voz. Dos osciladores wavetable (A y B), un sub-oscilador y ruido.
// Todas se suman y pasan juntas por el filtro.

// Fase 7: FM y ring mod de un oscilador. La señal moduladora es el OTRO oscilador, el sub o el ruido, tal como
// sale de su oscilador (antes de su Level y aunque esté apagado): así se puede usar un oscilador solo como
// modulador, sin oírlo, que es como se hace la FM clásica.
// El orden es parte del estado guardado: solo añadir al final.
enum class FmMode { off, fmOther, fmSub, fmNoise, rmOther, rmSub, rmNoise };
enum class FmSource { none, otherOscillator, sub, noise };

inline constexpr std::array<const char*, 7> fmModeNamesA { "Off", "FM: Osc B", "FM: Sub", "FM: Noise",
                                                          "RM: Osc B", "RM: Sub", "RM: Noise" };
inline constexpr std::array<const char*, 7> fmModeNamesB { "Off", "FM: Osc A", "FM: Sub", "FM: Noise",
                                                          "RM: Osc A", "RM: Sub", "RM: Noise" };

[[nodiscard]] inline bool isFrequencyModulation (FmMode mode) noexcept
{
    return mode == FmMode::fmOther || mode == FmMode::fmSub || mode == FmMode::fmNoise;
}

[[nodiscard]] inline bool isRingModulation (FmMode mode) noexcept
{
    return mode == FmMode::rmOther || mode == FmMode::rmSub || mode == FmMode::rmNoise;
}

[[nodiscard]] inline FmSource fmSourceOf (FmMode mode) noexcept
{
    switch (mode)
    {
        case FmMode::fmOther:
        case FmMode::rmOther: return FmSource::otherOscillator;
        case FmMode::fmSub:
        case FmMode::rmSub: return FmSource::sub;
        case FmMode::fmNoise:
        case FmMode::rmNoise: return FmSource::noise;
        case FmMode::off: break;
    }
    return FmSource::none;
}

// Profundidad de la FM al 100 %: 2 ciclos de desplazamiento de fase (índice de modulación 4π ≈ 12.6, del orden
// del máximo de un DX7). Curva cuadrática: la zona fina (brillo sutil, índice < 1) ocupa la mitad baja de la perilla.
inline constexpr float maxFmCycles = 2.0f;

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
    // Fase 7. Por defecto sin warp ni FM: el oscilador de la Fase 6.
    dsp::WarpMode warpMode = dsp::WarpMode::none;
    float warpAmount = 0.0f; // 0..1
    FmMode fmMode = FmMode::off;
    float fmAmount = 0.0f;   // 0..1

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
