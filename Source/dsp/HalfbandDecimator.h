#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace undertow::dsp
{

// Decimador ×2 con un filtro FIR "halfband". Sin dependencias de JUCE.
//
// Oversampling: cuando el oscilador hace cosas que crean armónicos muy agudos (FM, sync, escalones), se calcula
// al DOBLE de la frecuencia de muestreo. Así hay espacio hasta 2·sr para esos armónicos antes de que se reflejen.
// Después hay que volver a la frecuencia del host quitándose de encima todo lo que no cabe: primero un
// low-pass que elimine lo que está por encima de ~20 kHz, luego quedarse con una de cada dos muestras.
//
// ¿Por qué "halfband"? Un filtro cuyo corte está justo en la mitad de la banda (sr/2 del host, que es la
// cuarta parte de la frecuencia sobremuestreada) tiene la mitad de sus coeficientes a CERO: cuesta la mitad.
// Y su respuesta es simétrica: si deja pasar hasta fp, rechaza desde sr − fp. Con fp = 20 kHz a 48 kHz, rechaza
// desde 28 kHz: justo lo que, al volver a 48 kHz, se reflejaría por debajo de 20 kHz. Lo que queda entre 20 y
// 28 kHz se refleja entre 20 y 24 kHz: fuera del oído.
//
// Se diseña con el método de la ventana: la respuesta ideal (un sinc) recortada con una ventana de Kaiser, que
// permite elegir la atenuación (aquí 100 dB) y calcula la longitud necesaria. FIR = fase lineal: todas las
// frecuencias llegan con el mismo retardo (unas 35 muestras a 44.1 kHz, 0.8 ms), sin deformar la onda.
class HalfbandDecimator
{
public:
    // Hasta dónde se conserva todo intacto: 20 kHz, o un poco menos si el host trabaja por debajo de 44.1 kHz.
    [[nodiscard]] static double passbandEdge (double outputSampleRate) noexcept
    {
        return std::min (20000.0, 0.4535 * outputSampleRate);
    }

    // Se llama fuera del hilo de audio (prepareToPlay): calcula los coeficientes para esta frecuencia.
    void prepare (double outputSampleRate) noexcept
    {
        const double oversampledRate = 2.0 * outputSampleRate;
        const double transitionHz = outputSampleRate - 2.0 * passbandEdge (outputSampleRate);
        const double transition = 2.0 * std::numbers::pi * transitionHz / oversampledRate; // en radianes/muestra

        // Fórmulas de Kaiser: longitud y "beta" para la atenuación pedida.
        const double beta = 0.1102 * (attenuationDb - 8.7);
        int length = static_cast<int> (std::ceil ((attenuationDb - 7.95) / (2.0 * 2.285 * transition)));
        length = std::clamp (length | 1, 1, maxHalfLength); // impar: el último coeficiente cae en un tap no nulo
        halfLength = length;

        // Coeficientes de los taps impares (los pares, salvo el central, son cero en un halfband).
        // Respuesta ideal: h[j] = sin(π·j/2) / (π·j). El central vale 0.5.
        numCoefficients = (halfLength + 1) / 2;
        for (int i = 0; i < numCoefficients; ++i)
        {
            const int j = 2 * i + 1;
            const double ideal = std::sin (std::numbers::pi * j / 2.0) / (std::numbers::pi * j);
            const double ratio = static_cast<double> (j) / static_cast<double> (halfLength);
            const double window = besselI0 (beta * std::sqrt (1.0 - ratio * ratio)) / besselI0 (beta);
            coefficients[static_cast<size_t> (i)] = static_cast<float> (ideal * window);
        }
        reset();
    }

    void reset() noexcept
    {
        history.fill (0.0f);
        writeIndex = 0;
    }

    // Recibe dos muestras a la frecuencia sobremuestreada (la primera es la más antigua) y devuelve una.
    [[nodiscard]] float process (float first, float second) noexcept
    {
        push (first);
        push (second);

        // Doble escritura en el buffer circular: las últimas N muestras están siempre seguidas en memoria.
        const float* newest = history.data() + writeIndex + bufferSize - 1;
        const float* centre = newest - halfLength;
        float sum = 0.5f * centre[0];
        for (int i = 0; i < numCoefficients; ++i)
        {
            const int j = 2 * i + 1;
            sum += coefficients[static_cast<size_t> (i)] * (centre[-j] + centre[j]);
        }
        return sum;
    }

    // Para pasar de mono a estéreo sin salto: el canal derecho empieza con la historia del izquierdo.
    void copyStateFrom (const HalfbandDecimator& other) noexcept
    {
        history = other.history;
        writeIndex = other.writeIndex;
    }

    // Retardo en muestras de salida (la mitad de la longitud del filtro, en muestras sobremuestreadas / 2).
    [[nodiscard]] double getLatency() const noexcept { return 0.5 * halfLength; }
    [[nodiscard]] int getNumTaps() const noexcept { return 2 * halfLength + 1; }

    static constexpr double attenuationDb = 100.0;

private:
    void push (float sample) noexcept
    {
        history[static_cast<size_t> (writeIndex)] = sample;
        history[static_cast<size_t> (writeIndex + bufferSize)] = sample;
        writeIndex = (writeIndex + 1) & (bufferSize - 1);
    }

    // Función de Bessel modificada de orden 0 (la de la ventana de Kaiser), por su serie de potencias.
    [[nodiscard]] static double besselI0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 50; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < sum * 1.0e-12)
                break;
        }
        return sum;
    }

    // 44.1 kHz es el caso más exigente (transición de 20 a 24.1 kHz): unos 69 taps a cada lado del central.
    static constexpr int maxHalfLength = 75;
    static constexpr int bufferSize = 256; // potencia de 2 mayor que la longitud del filtro

    std::array<float, (maxHalfLength + 1) / 2> coefficients {};
    int numCoefficients = 1;
    int halfLength = 1;
    std::array<float, 2 * bufferSize> history {};
    int writeIndex = 0;
};

} // namespace undertow::dsp
