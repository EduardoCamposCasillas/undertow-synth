#include "dsp/Wavetable.h"

#include <cassert>
#include <cmath>

#include "dsp/Fft.h"

namespace undertow::dsp
{

Wavetable Wavetable::fromSpectra (std::string name, const std::vector<HarmonicSpectrum>& frames)
{
    assert (! frames.empty());

    Wavetable table;
    table.name = std::move (name);
    table.numFrames = static_cast<int> (frames.size());

    size_t totalSamples = 0;
    for (int level = 0; level < numLevels; ++level)
    {
        table.levelOffsets[static_cast<size_t> (level)] = totalSamples;
        totalSamples += static_cast<size_t> (table.numFrames) * rowStride (level);
    }
    table.samples.assign (totalSamples, 0.0f);

    std::vector<std::complex<double>> bins;

    for (int frame = 0; frame < table.numFrames; ++frame)
    {
        const auto& spectrum = frames[static_cast<size_t> (frame)];
        double scale = 1.0;

        for (int level = 0; level < numLevels; ++level)
        {
            const auto n = static_cast<size_t> (levelSize (level));
            bins.assign (n, {});

            // Mipmap = el mismo ciclo, pero borrando los armónicos por encima del límite del nivel.
            // Se hace en el dominio de la frecuencia: ahí "quitar armónicos" es literalmente poner ceros.
            const auto harmonics = std::min (static_cast<size_t> (maxHarmonicsAtLevel (level)), spectrum.size() - 1);
            for (size_t h = 1; h <= harmonics; ++h)
            {
                // Una señal real tiene un espectro simétrico: el armónico h aparece en el bin h y,
                // conjugado, en el bin N - h. Con N/2 de cada lado, la IFFT da exactamente x(φ).
                bins[h] = spectrum[h] * (static_cast<double> (n) / 2.0);
                bins[n - h] = std::conj (bins[h]);
            }

            fft (bins, true);

            // Todos los niveles de un frame usan la escala del nivel 0 (el de más armónicos). Si cada
            // nivel se normalizara por separado, el volumen cambiaría un poco al pasar de una octava a otra.
            if (level == 0)
            {
                double peak = 0.0;
                for (const auto& value : bins)
                    peak = std::max (peak, std::abs (value.real()));
                scale = peak > 1.0e-9 ? 1.0 / peak : 1.0;
            }

            float* cycle = table.samples.data() + table.frameOffset (level, frame);
            for (size_t i = 0; i < n; ++i)
                cycle[i] = static_cast<float> (bins[i].real() * scale);

            cycle[-1] = cycle[n - 1];
            cycle[n] = cycle[0];
            cycle[n + 1] = cycle[1];
        }
    }

    return table;
}

Wavetable Wavetable::fromWaveforms (std::string name, const std::vector<std::vector<float>>& cycles)
{
    std::vector<HarmonicSpectrum> spectra;
    spectra.reserve (cycles.size());

    for (const auto& cycle : cycles)
    {
        const size_t m = cycle.size();
        assert (m >= static_cast<size_t> (baseFrameSize) && (m & (m - 1)) == 0);

        std::vector<std::complex<double>> bins (cycle.begin(), cycle.end());
        fft (bins, false);

        // Con la convención de HarmonicSpectrum, el armónico h vale 2·X[h]/M.
        const size_t harmonics = std::min (static_cast<size_t> (maxHarmonics), m / 2 - 1);
        HarmonicSpectrum spectrum (harmonics + 1);
        for (size_t h = 1; h <= harmonics; ++h)
            spectrum[h] = 2.0 * bins[h] / static_cast<double> (m);

        spectra.push_back (std::move (spectrum));
    }

    return fromSpectra (std::move (name), spectra);
}

} // namespace undertow::dsp
