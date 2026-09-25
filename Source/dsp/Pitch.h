#pragma once

#include <cmath>

namespace undertow::dsp
{

// Frecuencia en Hz de una nota MIDI con afinación estándar (La = nota 69 = 440 Hz).
// Cada semitono multiplica la frecuencia por 2^(1/12); 12 semitonos la duplican (una octava).
[[nodiscard]] inline double midiNoteToHz (int midiNote) noexcept
{
    return 440.0 * std::pow (2.0, (midiNote - 69) / 12.0);
}

} // namespace undertow::dsp
