#include "synth/WavetableBank.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace undertow::synth
{
namespace
{
using dsp::HarmonicSpectrum;
using dsp::Wavetable;

constexpr double pi = std::numbers::pi;
constexpr int maxHarmonics = Wavetable::maxHarmonics;
const std::complex<double> minusI { 0.0, -1.0 }; // espectro de un seno: -i · amplitud

// Las tablas con evolución NO lineal usan 64 frames: la mezcla lineal entre dos frames vecinos
// se parece mucho a la forma real intermedia. En "Basic Shapes" bastan 4, porque ahí justamente
// queremos la mezcla lineal entre las formas clásicas.
constexpr int framesPerTable = 64;

// Cuatro formas clásicas, todas en "fase seno" (empiezan en 0 y suben), para que se mezclen bien.
Wavetable makeBasicShapes()
{
    HarmonicSpectrum sine (maxHarmonics + 1), triangle (maxHarmonics + 1), saw (maxHarmonics + 1),
        square (maxHarmonics + 1);

    sine[1] = minusI;

    for (int h = 1; h <= maxHarmonics; ++h)
    {
        const double hd = h;
        const bool odd = (h % 2) == 1;

        // Sierra: todos los armónicos, con amplitud 1/h (cada octava, -6 dB).
        saw[static_cast<size_t> (h)] = minusI * ((h % 2 == 1 ? 2.0 : -2.0) / (pi * hd));

        if (odd)
        {
            // Cuadrada: solo impares, 1/h. Triángulo: solo impares, 1/h² (mucho más oscuro que la cuadrada).
            square[static_cast<size_t> (h)] = minusI * (4.0 / (pi * hd));
            const double sign = ((h - 1) / 2) % 2 == 0 ? 1.0 : -1.0;
            triangle[static_cast<size_t> (h)] = minusI * (sign * 8.0 / (pi * pi * hd * hd));
        }
    }

    return Wavetable::fromSpectra (WavetableBank::names[0], { sine, triangle, saw, square });
}

// Pulso con ancho w (fracción del ciclo en alto): de 50 % (cuadrada) a 3 % (pulso fino y nasal).
// Coeficiente exacto del pulso que vale 1 en [0, w):  c_h = (1 - e^(-i·2π·h·w)) / (i·2π·h).
Wavetable makePulseWidth()
{
    std::vector<HarmonicSpectrum> frames;

    for (int frame = 0; frame < framesPerTable; ++frame)
    {
        const double t = static_cast<double> (frame) / (framesPerTable - 1);
        const double width = 0.5 + (0.03 - 0.5) * t;

        HarmonicSpectrum spectrum (maxHarmonics + 1);
        for (int h = 1; h <= maxHarmonics; ++h)
        {
            // Dividir entre i·a es multiplicar por -i/a.
            const double angle = 2.0 * pi * h * width;
            const auto c = (1.0 - std::polar (1.0, -angle)) * (minusI / (2.0 * pi * h));
            spectrum[static_cast<size_t> (h)] = 2.0 * c;
        }
        frames.push_back (std::move (spectrum));
    }

    return Wavetable::fromSpectra (WavetableBank::names[1], frames);
}

// Frame n = sierra con solo los primeros n+1 armónicos. Recorrer la tabla es "escuchar"
// cómo se construye un timbre sumando armónicos, uno por uno.
Wavetable makeHarmonicBuild()
{
    std::vector<HarmonicSpectrum> frames;

    for (int frame = 0; frame < framesPerTable; ++frame)
    {
        HarmonicSpectrum spectrum (static_cast<size_t> (frame + 2));
        for (int h = 1; h <= frame + 1; ++h)
            spectrum[static_cast<size_t> (h)] = minusI / static_cast<double> (h);
        frames.push_back (std::move (spectrum));
    }

    return Wavetable::fromSpectra (WavetableBank::names[2], frames);
}

// Hard sync: una sierra "esclava" a r veces la frecuencia se reinicia al empezar cada ciclo.
// Al subir r aparecen armónicos resonantes que se mueven: el clásico "sync lead".
// Se genera en el tiempo y se pasa a espectro con una FFT.
Wavetable makeHardSync()
{
    // 8192 puntos: 4 veces el mínimo para 1024 armónicos. Muestrear un salto brusco "dobla" los armónicos
    // que no caben sobre los que sí; con margen de sobra, lo doblado queda a más de -70 dB.
    constexpr int oversampledSize = 4 * 2 * Wavetable::maxHarmonics;
    std::vector<std::vector<float>> cycles;

    for (int frame = 0; frame < framesPerTable; ++frame)
    {
        const double ratio = 1.0 + 7.0 * static_cast<double> (frame) / (framesPerTable - 1);

        std::vector<float> cycle (oversampledSize);
        for (int i = 0; i < oversampledSize; ++i)
        {
            const double phase = static_cast<double> (i) / oversampledSize;
            const double slavePhase = ratio * phase - std::floor (ratio * phase);
            cycle[static_cast<size_t> (i)] = static_cast<float> (2.0 * slavePhase - 1.0);
        }
        cycles.push_back (std::move (cycle));
    }

    return Wavetable::fromWaveforms (WavetableBank::names[3], cycles);
}

// Vocales: los armónicos de una voz grave (~110 Hz) se refuerzan cerca de 3 resonancias (formantes).
// La posición de F1 y F2 es lo que el oído usa para distinguir una vocal de otra.
// Como la tabla sigue a la nota, los formantes suben con ella: suena "vocal" sobre todo en graves y medios.
Wavetable makeVowels()
{
    struct Vowel { double f1, f2, f3; };
    constexpr std::array<Vowel, 5> vowels { { { 730.0, 1090.0, 2440.0 },    // A
                                              { 530.0, 1840.0, 2480.0 },    // E
                                              { 270.0, 2290.0, 3010.0 },    // I
                                              { 570.0, 840.0, 2410.0 },     // O
                                              { 300.0, 870.0, 2240.0 } } }; // U

    constexpr double baseFrequency = 110.0;
    constexpr int harmonics = 72; // hasta ~8 kHz: por encima la voz casi no tiene energía

    const auto resonance = [] (double f, double centre, double bandwidth) {
        const double x = (f - centre) / bandwidth;
        return 1.0 / (1.0 + x * x);
    };

    std::vector<HarmonicSpectrum> frames;

    for (int frame = 0; frame < framesPerTable; ++frame)
    {
        const double t = 4.0 * static_cast<double> (frame) / (framesPerTable - 1);
        const auto segment = static_cast<size_t> (std::min (3, static_cast<int> (t)));
        const double blend = t - static_cast<double> (segment);

        // Interpolación geométrica: el oído percibe la frecuencia en escala logarítmica.
        const auto& a = vowels[segment];
        const auto& b = vowels[segment + 1];
        const auto mix = [blend] (double x, double y) { return x * std::pow (y / x, blend); };
        const double f1 = mix (a.f1, b.f1), f2 = mix (a.f2, b.f2), f3 = mix (a.f3, b.f3);

        HarmonicSpectrum spectrum (harmonics + 1);
        for (int h = 1; h <= harmonics; ++h)
        {
            const double f = baseFrequency * h;
            const double envelope = 0.03 + resonance (f, f1, 90.0) + 0.6 * resonance (f, f2, 110.0)
                                    + 0.3 * resonance (f, f3, 160.0);
            spectrum[static_cast<size_t> (h)] = minusI * (envelope / h);
        }
        frames.push_back (std::move (spectrum));
    }

    return Wavetable::fromSpectra (WavetableBank::names[4], frames);
}
} // namespace

WavetableBank::WavetableBank()
{
    tables.reserve (static_cast<size_t> (numTables));
    tables.push_back (makeBasicShapes());
    tables.push_back (makePulseWidth());
    tables.push_back (makeHarmonicBuild());
    tables.push_back (makeHardSync());
    tables.push_back (makeVowels());
}

const dsp::Wavetable& WavetableBank::get (int index) const noexcept
{
    return tables[static_cast<size_t> (std::clamp (index, 0, numTables - 1))];
}

} // namespace undertow::synth
