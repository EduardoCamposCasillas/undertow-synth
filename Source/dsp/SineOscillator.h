#pragma once

#include <cmath>
#include <numbers>

namespace undertow::dsp
{

// Frecuencia en Hz de una nota MIDI con afinación estándar (La4 = nota 69 = 440 Hz).
// Cada semitono multiplica la frecuencia por 2^(1/12); 12 semitonos la duplican (una octava).
[[nodiscard]] inline double midiNoteToHz (int midiNote) noexcept
{
    return 440.0 * std::pow (2.0, (midiNote - 69) / 12.0);
}

// Oscilador senoidal por acumulador de fase. Sin dependencias de JUCE para poder probarlo aislado.
class SineOscillator
{
public:
    void setSampleRate (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        updateIncrement();
    }

    void setFrequency (double newFrequencyHz) noexcept
    {
        frequencyHz = newFrequencyHz;
        updateIncrement();
    }

    void resetPhase() noexcept { phase = 0.0; }

    [[nodiscard]] float processSample() noexcept
    {
        const auto output = static_cast<float> (std::sin (phase * twoPi));

        // La fase se guarda normalizada en [0, 1) en vez de en radianes que crecen sin fin:
        // un número que crece pierde precisión y la afinación se degradaría con el tiempo.
        // Además, en la Fase 3 esta misma fase servirá directamente como índice del wavetable.
        phase += phaseIncrement;
        if (phase >= 1.0)
            phase -= 1.0;

        return output;
    }

private:
    void updateIncrement() noexcept
    {
        // Ciclos por muestra: con 440 Hz a 48 kHz avanzamos 440/48000 de ciclo en cada muestra.
        // Por eso el tono es correcto a cualquier sample rate.
        phaseIncrement = sampleRate > 0.0 ? frequencyHz / sampleRate : 0.0;
    }

    static constexpr double twoPi = 2.0 * std::numbers::pi;

    double sampleRate = 44100.0;
    double frequencyHz = 440.0;
    double phase = 0.0;          // double: con float la fase acumula error audible en notas graves
    double phaseIncrement = 0.0;
};

} // namespace undertow::dsp
