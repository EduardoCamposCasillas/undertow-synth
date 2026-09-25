#pragma once

#include <algorithm>
#include <array>
#include <complex>
#include <cstddef>
#include <string>
#include <vector>

namespace undertow::dsp
{

// Espectro de un ciclo de onda: spectrum[h] es la amplitud compleja del armónico h.
// El índice 0 (DC, componente continua) se ignora siempre.
// Convención:  x(φ) = Σ Re( spectrum[h] · e^(i·2π·h·φ) ),  con φ = fase en [0, 1).
// Así un seno de amplitud b en el armónico h es spectrum[h] = -i·b, y un coseno es spectrum[h] = b.
using HarmonicSpectrum = std::vector<std::complex<double>>;

// Wavetable inmutable: varios frames (ciclos de onda) y, de cada uno, una versión por octava
// con cada vez menos armónicos (mipmaps). Se construye una vez fuera del hilo de audio y después
// solo se lee, así que varias voces (e incluso varias instancias del plugin) pueden compartirla.
class Wavetable
{
public:
    // Nivel 0: hasta 1024 armónicos; cada nivel siguiente, la mitad. El último tiene solo la fundamental.
    static constexpr int numLevels = 11;
    static constexpr int maxHarmonics = 1024;

    [[nodiscard]] static constexpr int maxHarmonicsAtLevel (int level) noexcept { return maxHarmonics >> level; }

    // Muestras por ciclo en cada nivel. Si el armónico más alto tiene pocas muestras por ciclo, la
    // interpolación deja "imágenes" que se reflejan hacia la banda audible. Medido en los tests con un
    // pulso fino en graves: 4 muestras por ciclo -> alias a -62 dB; 8 -> -72 dB; 16 -> -87 dB.
    // Por eso los niveles ricos son más largos: 16384, 8192, 4096 y luego 2048 (el tamaño clásico).
    // Cuesta memoria (no CPU): el banco de fábrica ocupa ~47 MB, compartidos entre instancias.
    static constexpr int samplesPerHarmonicCycle = 16;
    static constexpr int baseFrameSize = 2048;

    [[nodiscard]] static constexpr int levelSize (int level) noexcept
    {
        return std::max (baseFrameSize, samplesPerHarmonicCycle * maxHarmonicsAtLevel (level));
    }

    // Construye la tabla a partir del espectro de cada frame (la forma más limpia: no hay aliasing
    // dentro de la tabla porque se decide exactamente qué armónicos existen).
    static Wavetable fromSpectra (std::string name, const std::vector<HarmonicSpectrum>& frames);

    // Construye la tabla a partir de ciclos ya muestreados (tamaño potencia de 2, mínimo 2048).
    // Conviene pasarlos sobremuestreados (p. ej. 8192 muestras) si tienen saltos bruscos.
    static Wavetable fromWaveforms (std::string name, const std::vector<std::vector<float>>& cycles);

    [[nodiscard]] const std::string& getName() const noexcept { return name; }
    [[nodiscard]] int getNumFrames() const noexcept { return numFrames; }

    // Puntero a la muestra 0 del ciclo (de levelSize (level) muestras). Son válidos también los índices
    // -1, size y size + 1 (copias del otro extremo): así la interpolación cúbica nunca "da la vuelta".
    [[nodiscard]] const float* getFrame (int level, int frame) const noexcept
    {
        return samples.data() + frameOffset (level, frame);
    }

private:
    Wavetable() = default;

    [[nodiscard]] size_t frameOffset (int level, int frame) const noexcept
    {
        return levelOffsets[static_cast<size_t> (level)] + static_cast<size_t> (frame) * rowStride (level) + 1;
    }

    [[nodiscard]] static constexpr size_t rowStride (int level) noexcept
    {
        return static_cast<size_t> (levelSize (level)) + 3; // 1 muestra de guarda antes y 2 después
    }

    std::string name;
    int numFrames = 0;
    std::array<size_t, numLevels> levelOffsets {};
    std::vector<float> samples; // [nivel][frame][muestra con guardas]
};

} // namespace undertow::dsp
