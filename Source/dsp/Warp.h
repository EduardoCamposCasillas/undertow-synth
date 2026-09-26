#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace undertow::dsp
{

// --- Warp: deformar la fase antes de leer la tabla -------------------------------------------------
//
// El oscilador lee la tabla en la posición "fase" (0..1 a lo largo del ciclo). Un warp cambia QUÉ parte del
// ciclo se lee en cada instante: acelera unas zonas, frena otras, repite el ciclo o lo convierte en escalones.
// La tabla no cambia; cambia el recorrido. Por eso cualquier warp funciona con cualquier wavetable.
//
// Todos los modos cumplen dos reglas:
//  - amount 0 = la onda original exacta (modular el amount desde 0 no produce saltos);
//  - 'maxSpeed' dice cuántas veces más rápido que la nota se recorre la tabla en la zona más rápida. El
//    oscilador elige el mipmap para esa velocidad: si una zona se lee 8 veces más rápido, sus armónicos suben
//    8 veces, y el mipmap tiene que ser el de una nota 3 octavas más aguda para que no se reflejen.
//
// El orden es parte del estado guardado (el parámetro guarda el índice): solo añadir al final.
enum class WarpMode { none, sync, bendPlus, bendMinus, pwm, mirror, quantize, bitcrush };

inline constexpr std::array<const char*, 8> warpModeNames { "None",   "Sync",   "Bend +",   "Bend -",
                                                            "PWM",    "Mirror", "Quantize", "Bitcrush" };

// Fase en [0, 1). Protege la lectura de la tabla: un 1.0 exacto se saldría del ciclo.
[[nodiscard]] inline double wrapPhase (double phase) noexcept
{
    const double wrapped = phase - std::floor (phase);
    return wrapped >= 1.0 ? 0.0 : wrapped;
}

// Curva de "bend": va de 0 a 1 empezando con pendiente 'slope' y terminando con pendiente 1/slope.
// Es una hipérbola (x·s / ((s − 1)·x + 1)). A diferencia de una potencia x^k, su pendiente nunca es infinita,
// así que la velocidad máxima de lectura está acotada (y el mipmap puede evitar el aliasing).
[[nodiscard]] inline double bendCurve (double x, double slope) noexcept
{
    return slope * x / ((slope - 1.0) * x + 1.0);
}

// Un warp listo para usar: el modo, el amount y lo que se deriva de él (se recalcula solo si el amount cambia).
struct Warp
{
    WarpMode mode = WarpMode::none;
    float amount = 0.0f;

    double syncRatio = 1.0;   // Sync: ciclos del esclavo por ciclo del maestro (1..16)
    double bendSlope = 1.0;   // Bend: la zona rápida va 'bendSlope' veces más rápido (1..8)
    double pulseWidth = 1.0;  // PWM: fracción del ciclo que ocupa la onda comprimida (1..1/16)
    double steps = 256.0;     // Quantize: escalones por ciclo (256..2)
    float crushScale = 128.0f; // Bitcrush: escalones de amplitud por unidad (128..1, de 8 bits a 1 bit)
    float wet = 0.0f;         // Quantize/Bitcrush: el efecto entra en el primer 10 % de la perilla
    double maxSpeed = 1.0;    // velocidad de lectura de la zona más rápida → mipmap de la lectura principal

    [[nodiscard]] static Warp make (WarpMode mode, float amount) noexcept
    {
        Warp w;
        w.mode = mode;
        w.amount = std::clamp (amount, 0.0f, 1.0f);
        const double a = w.amount;

        switch (mode)
        {
            case WarpMode::sync:
                // Exponencial: cada 25 % de la perilla, el esclavo sube una octava (×2, ×4, ×8, ×16).
                w.syncRatio = std::exp2 (4.0 * a);
                w.maxSpeed = w.syncRatio;
                break;
            case WarpMode::bendPlus:
            case WarpMode::bendMinus:
                w.bendSlope = std::exp2 (3.0 * a);
                w.maxSpeed = w.bendSlope;
                break;
            case WarpMode::pwm:
                w.pulseWidth = 1.0 - a * (1.0 - minPulseWidth);
                w.maxSpeed = 1.0 / w.pulseWidth;
                break;
            case WarpMode::quantize:
                w.steps = std::exp2 (1.0 + 7.0 * (1.0 - a));
                w.wet = std::min (1.0f, w.amount / fadeInAmount);
                break;
            case WarpMode::bitcrush:
                w.crushScale = static_cast<float> (std::exp2 (7.0 * (1.0 - a)));
                w.wet = std::min (1.0f, w.amount / fadeInAmount);
                break;
            case WarpMode::mirror: // la lectura "en espejo" va al doble de velocidad; la principal, normal
            case WarpMode::none:
                break;
        }
        return w;
    }

    // Los modos con SALTOS (Sync, Quantize, Bitcrush) o con QUIEBRES (cambios bruscos de pendiente: Sync, PWM,
    // Mirror) necesitan las correcciones polyBLEP / polyBLAMP (ver WavetableOscillator::renderWarped).
    // Bend no: su curva no tiene quiebres.
    [[nodiscard]] bool needsCorrection() const noexcept
    {
        return mode == WarpMode::sync || mode == WarpMode::quantize || mode == WarpMode::bitcrush || mode == WarpMode::pwm
               || mode == WarpMode::mirror;
    }

    static constexpr double minPulseWidth = 1.0 / 16.0;
    static constexpr float fadeInAmount = 0.1f;
    static constexpr double mirrorSpeed = 2.0;
};

// Fase deformada para Sync, Bend y PWM (los modos que solo cambian el recorrido).
[[nodiscard]] inline double warpPhase (const Warp& w, double phase) noexcept
{
    switch (w.mode)
    {
        case WarpMode::sync:
            // Hard sync: el esclavo corre 'syncRatio' veces más rápido y vuelve a 0 cada vez que el maestro
            // (la fase de la nota) completa un ciclo. El tono sigue siendo el de la nota; el timbre, no.
            return wrapPhase (phase * w.syncRatio);

        case WarpMode::bendPlus:
        case WarpMode::bendMinus:
        {
            // Simétrico: la mitad izquierda es la curva y la derecha su espejo. Bend + va rápido en los bordes
            // y lento en el centro (el centro de la onda "se estira"); Bend − al revés.
            const double slope = w.mode == WarpMode::bendPlus ? w.bendSlope : 1.0 / w.bendSlope;
            if (phase < 0.5)
                return 0.5 * bendCurve (2.0 * phase, slope);
            return wrapPhase (1.0 - 0.5 * bendCurve (2.0 * (1.0 - phase), slope));
        }

        case WarpMode::pwm:
            // El ciclo entero se comprime en 'pulseWidth' y el resto se queda en el principio de la onda.
            // Como la onda termina donde empieza, el paso a la zona quieta es continuo (sin salto).
            return phase < w.pulseWidth ? phase / w.pulseWidth : 0.0;

        case WarpMode::none:
        case WarpMode::mirror:
        case WarpMode::quantize:
        case WarpMode::bitcrush:
            break;
    }
    return phase;
}

// Mirror: el ciclo de ida (al doble de velocidad) y de vuelta. Es continuo: termina donde empieza.
[[nodiscard]] inline double mirrorPhase (double phase) noexcept
{
    return wrapPhase (phase < 0.5 ? 2.0 * phase : 2.0 - 2.0 * phase);
}

// Quantize: la fase avanza a saltos, 'steps' escalones por ciclo, y cada escalón lee el CENTRO de su tramo
// (con 2 escalones un seno se vuelve una cuadrada, no silencio).
[[nodiscard]] inline double quantizedPhase (const Warp& w, double phase) noexcept
{
    return wrapPhase ((std::floor (phase * w.steps) + 0.5) / w.steps);
}

// Bitcrush: la amplitud avanza a saltos ('crushScale' escalones por unidad).
[[nodiscard]] inline float crush (const Warp& w, float value) noexcept
{
    return std::round (value * w.crushScale) / w.crushScale;
}

// El valor "ingenuo" de la onda deformada en una fase (sin corregir los saltos). Lo usa la GUI para dibujar
// exactamente lo que suena. 'read' lee la tabla en una fase; 'readFast' la lee con el mipmap de la parte rápida
// del Mirror (en la GUI pueden ser la misma función).
template <typename Read, typename ReadFast>
[[nodiscard]] float warpedValue (const Warp& w, double phase, Read&& read, ReadFast&& readFast)
{
    switch (w.mode)
    {
        case WarpMode::mirror:
        {
            const float mirrored = readFast (mirrorPhase (phase));
            if (w.amount >= 1.0f)
                return mirrored;
            const float original = read (phase);
            return original + w.amount * (mirrored - original);
        }
        case WarpMode::quantize:
        {
            const float stepped = read (quantizedPhase (w, phase));
            if (w.wet >= 1.0f)
                return stepped;
            const float original = read (phase);
            return original + w.wet * (stepped - original);
        }
        case WarpMode::bitcrush:
        {
            const float original = read (phase);
            return original + w.wet * (crush (w, original) - original);
        }
        case WarpMode::none:
        case WarpMode::sync:
        case WarpMode::bendPlus:
        case WarpMode::bendMinus:
        case WarpMode::pwm:
            break;
    }
    return read (warpPhase (w, phase));
}

} // namespace undertow::dsp
