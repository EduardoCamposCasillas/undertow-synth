#pragma once

#include <array>
#include <vector>

#include "dsp/Wavetable.h"

namespace undertow::synth
{

// Wavetables de fábrica, generadas por código al crear el banco (fuera del hilo de audio).
// Cada una enseña un concepto de timbre distinto. Ocupan unos 47 MB y tardan ~0.4 s en generarse,
// así que el procesador comparte un único banco entre todas las instancias del plugin.
class WavetableBank
{
public:
    // El ORDEN es parte del estado guardado (el parámetro guarda el índice):
    // las tablas nuevas se añaden siempre al final.
    static constexpr std::array<const char*, 5> names {
        "Basic Shapes",   // seno -> triángulo -> sierra -> cuadrada
        "Pulse Width",    // cuadrada que se estrecha hasta un pulso fino (PWM)
        "Harmonic Build", // de 1 a 64 armónicos, añadidos uno a uno
        "Hard Sync",      // sierra "sincronizada" a una frecuencia de 1x a 8x
        "Vowels",         // formantes de las vocales A-E-I-O-U
    };

    static constexpr int numTables = static_cast<int> (names.size());

    WavetableBank();

    [[nodiscard]] const dsp::Wavetable& get (int index) const noexcept;

private:
    std::vector<dsp::Wavetable> tables;
};

} // namespace undertow::synth
