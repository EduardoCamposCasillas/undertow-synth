#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "dsp/Filter.h"
#include "dsp/HalfbandDecimator.h"

namespace undertow::dsp
{

// --- Distorsión con oversampling ×4 y ADAA --------------------------------------------------------------
//
// Distorsionar = pasar la señal por una curva que no es una recta (waveshaping): y = f(x). Una curva que
// "aplasta" los picos convierte un seno en algo más cuadrado, y eso son armónicos nuevos (más brillo, más
// agresividad). El problema digital: esos armónicos no tienen límite, y todo lo que pasa de sr/2 se refleja
// hacia abajo como aliasing (notas "fantasma" desafinadas, un siseo metálico que no sigue a la nota).
//
// Dos defensas combinadas:
//  1. Oversampling ×4: se sube la señal a 4·sr (dos interpoladores halfband en cascada), se distorsiona allí y
//     se vuelve a sr (dos decimadores). Hay espacio hasta 2·sr para los armónicos antes de que se reflejen, y lo
//     que se refleja por encima de 20 kHz lo borran los decimadores.
//  2. ADAA (anti-derivative anti-aliasing, Parker/Zavalishin/Le Bivic 2016): en vez de evaluar f en cada muestra,
//     se calcula el PROMEDIO de f entre la muestra anterior y la actual: (F(x) − F(x₋₁)) / (x − x₋₁), con F la
//     primitiva (integral) de f. Promediar es un low-pass "dentro" de la curva: suaviza las esquinas que crean
//     los armónicos más agudos, justo los que el oversampling no alcanza a contener.
//
// Con x ≈ x₋₁ esa división es 0/0 (inestable en coma flotante); ahí se usa f del punto medio, que es su límite.

enum class DistortionMode { softClip, hardClip, tube, fold };

// El orden se guarda en el estado (índice): solo añadir al final.
inline constexpr std::array<const char*, 4> distortionModeNames { "Soft Clip", "Hard Clip", "Tube", "Fold" };

namespace distortion_detail
{
    // Tube: tanh desplazada. La asimetría (los picos positivos se aplastan antes que los negativos) crea armónicos
    // PARES, como una válvula; restar tanh(bias) mantiene f(0) = 0.
    inline constexpr double tubeBias = 0.3;

    // log(cosh(x)) sin desbordar con |x| grande: |x| + log(1 + e^(−2|x|)) − log 2.
    [[nodiscard]] inline double logCosh (double x) noexcept
    {
        const double a = std::abs (x);
        return a + std::log1p (std::exp (-2.0 * a)) - std::numbers::ln2;
    }

    [[nodiscard]] inline double shape (DistortionMode mode, double x) noexcept
    {
        switch (mode)
        {
            case DistortionMode::softClip: return std::tanh (x);
            case DistortionMode::hardClip: return std::clamp (x, -1.0, 1.0);
            case DistortionMode::tube:     return std::tanh (x + tubeBias) - std::tanh (tubeBias);
            case DistortionMode::fold:     return std::sin (x);
        }
        return x;
    }

    // Primitiva de cada curva (la constante no importa: se restan dos valores).
    [[nodiscard]] inline double antiderivative (DistortionMode mode, double x) noexcept
    {
        switch (mode)
        {
            case DistortionMode::softClip: return logCosh (x);
            case DistortionMode::hardClip: return std::abs (x) <= 1.0 ? 0.5 * x * x : std::abs (x) - 0.5;
            case DistortionMode::tube:     return logCosh (x + tubeBias) - x * std::tanh (tubeBias);
            case DistortionMode::fold:     return -std::cos (x);
        }
        return 0.5 * x * x;
    }

    // Promedio de f(g·u) entre la entrada anterior y la actual. Se trabaja con la señal ANTES del drive (u) para
    // que el estado del ADAA no dependa del modo ni de la ganancia: así se pueden fundir dos modos con drives
    // distintos. La primitiva de f(g·u) es F(g·u) / g.
    [[nodiscard]] inline double adaa (DistortionMode mode, double gain, double u, double previous) noexcept
    {
        const double x = gain * u;
        const double xPrevious = gain * previous;
        const double dx = x - xPrevious;
        if (std::abs (dx) < 1.0e-5)
            return shape (mode, 0.5 * (x + xPrevious));
        return (antiderivative (mode, x) - antiderivative (mode, xPrevious)) / dx;
    }
} // namespace distortion_detail

struct DistortionParameters
{
    DistortionMode mode = DistortionMode::softClip;
    float drive = 0.5f; // 0..1 → 0..+36 dB antes de la curva (Fold: 0..+20 dB)
    float tone = 1.0f;  // 0..1 → low-pass de 12 dB después de la curva, de 400 Hz a 40 kHz (100 % = abierto)
    float mix = 1.0f;   // 0..1

    bool operator== (const DistortionParameters&) const = default;
};

// Distorsión estéreo. Todo se suaviza: se puede automatizar cualquier perilla y cambiar de modo sin clics.
class Distortion
{
public:
    static constexpr int oversampling = 4;

    // Fold tiene su propio rango: cada pliegue es como un índice de FM, y con +36 dB (×63) la onda se pliega
    // decenas de veces por ciclo: un ruido sin tono cuyo ancho de banda no cabe en ningún oversampling.
    [[nodiscard]] static float maxDriveDb (DistortionMode mode) noexcept { return mode == DistortionMode::fold ? 20.0f : 36.0f; }

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        const double internalRate = oversampling * sampleRate;
        for (auto& channel : channels)
        {
            channel.up1.prepare (sampleRate);
            channel.up2.prepare (2.0 * sampleRate);
            channel.down2.prepare (2.0 * sampleRate);
            channel.down1.prepare (sampleRate);
        }
        smoothingCoef = static_cast<float> (1.0 - std::exp (-1.0 / (smoothingSeconds * sampleRate)));
        modeFadeStep = static_cast<float> (1.0 / (modeFadeSeconds * sampleRate));
        dcCoef = static_cast<float> (std::exp (-2.0 * std::numbers::pi * dcBlockerHz / internalRate));
        snapToTargets();
        reset();
    }

    void setParameters (const DistortionParameters& newParameters) noexcept { target = newParameters; }

    // Vacía los filtros (la próxima muestra empieza desde silencio) y salta a los valores de las perillas.
    void reset() noexcept
    {
        for (auto& channel : channels)
        {
            channel.up1.reset();
            channel.up2.reset();
            channel.down2.reset();
            channel.down1.reset();
            channel.tone.reset();
            channel.previousInput = 0.0;
            channel.dcIn = channel.dcOut = 0.0f;
        }
        snapToTargets();
    }

    // Procesa en el sitio. Con 'right' nulo, solo el canal izquierdo.
    void process (float* left, float* right, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            advanceParameters();
            left[i] = processSample (channels[0], left[i]);
            if (right != nullptr)
                right[i] = processSample (channels[1], right[i]);
        }
    }

    // Retardo total en muestras del host: dos interpoladores y dos decimadores en cascada.
    [[nodiscard]] double getLatency() const noexcept
    {
        const auto& c = channels[0];
        return c.up1.getLatency() + c.down1.getLatency() + 0.5 * (c.up2.getLatency() + c.down2.getLatency());
    }

    // Ganancia antes de la curva y compensación de volumen después (la usan los tests y la explicación).
    [[nodiscard]] static float driveGain (DistortionMode mode, float drive) noexcept
    {
        return std::pow (10.0f, maxDriveDb (mode) * drive / 20.0f);
    }

    // Compensación: una señal de referencia (−12 dBFS, un nivel típico de un sinte) sale con el mismo pico
    // que entra, con cualquier drive. Sin esto, subir el drive subiría también el volumen y engañaría al oído
    // ("más fuerte" suena "mejor"). Se calcula con la tanh para todos los modos: todas se parecen a x con
    // señales pequeñas y todas quedan acotadas en ±1.
    [[nodiscard]] static float makeupGain (float gain) noexcept
    {
        return referenceLevel / std::tanh (referenceLevel * gain);
    }

    [[nodiscard]] static float toneCutoffHz (float tone) noexcept { return std::exp2 (toneOctavesFor (tone)); }

    // El tono se suaviza en octavas: un barrido suena parejo (igual que el cutoff del filtro).
    [[nodiscard]] static float toneOctavesFor (float tone) noexcept
    {
        return std::log2 (minToneHz) + std::clamp (tone, 0.0f, 1.0f) * std::log2 (maxToneHz / minToneHz);
    }

private:
    struct Channel
    {
        HalfbandInterpolator up1, up2;
        HalfbandDecimator down2, down1;
        SvfStage tone;
        double previousInput = 0.0; // entrada anterior (antes del drive), para el ADAA
        float dcIn = 0.0f, dcOut = 0.0f;
    };

    void snapToTargets() noexcept
    {
        drive = target.drive;
        toneOctaves = toneOctavesFor (target.tone);
        mix = target.mix;
        mode = previousMode = target.mode;
        modeFade = 1.0f;
        updateGains();
        updateToneCoefficients();
    }

    void advanceParameters() noexcept
    {
        const auto approach = [this] (float& value, float goal) {
            if (value == goal)
                return false;
            value += (goal - value) * smoothingCoef;
            if (std::abs (goal - value) < 1.0e-5f)
                value = goal;
            return true;
        };

        if (approach (drive, target.drive))
            updateGains();
        if (approach (toneOctaves, toneOctavesFor (target.tone)))
            updateToneCoefficients();
        approach (mix, target.mix);

        // Cambio de modo: las dos curvas se calculan a la vez durante 10 ms y se funden (solo se inicia un
        // fundido nuevo cuando termina el anterior).
        if (modeFade < 1.0f)
            modeFade = std::min (1.0f, modeFade + modeFadeStep);
        else if (target.mode != mode)
        {
            previousMode = mode;
            mode = target.mode;
            modeFade = 0.0f;
            updateGains();
        }
    }

    void updateGains() noexcept
    {
        current.gain = driveGain (mode, drive);
        current.makeup = makeupGain (current.gain);
        previous.gain = driveGain (previousMode, drive);
        previous.makeup = makeupGain (previous.gain);
    }

    void updateToneCoefficients() noexcept
    {
        const double internalRate = oversampling * sampleRate;
        const double hz = std::min (static_cast<double> (std::exp2 (toneOctaves)), 0.45 * internalRate);
        toneCoefficients = SvfStage::Coefficients::make (static_cast<float> (std::tan (std::numbers::pi * hz / internalRate)),
                                                         1.0f / filter_detail::q12);
    }

    // Una muestra del host → 4 internas → una muestra del host.
    [[nodiscard]] float processSample (Channel& c, float input) noexcept
    {
        std::array<float, 2> half {};
        std::array<float, 4> quad {};
        c.up1.process (input, half[0], half[1]);
        c.up2.process (half[0], quad[0], quad[1]);
        c.up2.process (half[1], quad[2], quad[3]);

        for (auto& sample : quad)
            sample = processInternal (c, sample);

        // En dos líneas a propósito: C++ no fija en qué orden se evalúan los argumentos de una función, y el
        // decimador tiene memoria (la primera pareja DEBE entrar antes que la segunda).
        const float first = c.down2.process (quad[0], quad[1]);
        const float second = c.down2.process (quad[2], quad[3]);
        return c.down1.process (first, second);
    }

    [[nodiscard]] float processInternal (Channel& c, float dry) noexcept
    {
        namespace d = distortion_detail;
        double shaped = current.makeup * d::adaa (mode, current.gain, dry, c.previousInput);
        if (modeFade < 1.0f)
        {
            const double old = previous.makeup * d::adaa (previousMode, previous.gain, dry, c.previousInput);
            shaped = old + (shaped - old) * modeFade;
        }
        c.previousInput = dry;

        const auto wet = static_cast<float> (shaped);
        const float toned = c.tone.process (wet, toneCoefficients).lowPass;

        // Bloqueador de continua (high-pass de 5 Hz): la curva Tube, asimétrica, desplaza el centro de la onda.
        const float blocked = toned - c.dcIn + dcCoef * c.dcOut;
        c.dcIn = toned;
        c.dcOut = blocked;

        // La mezcla se hace aquí dentro: la señal limpia pasa por los mismos filtros y llega con el mismo retardo.
        return dry + mix * (blocked - dry);
    }

    static constexpr double smoothingSeconds = 0.02;
    static constexpr double modeFadeSeconds = 0.01;
    static constexpr double dcBlockerHz = 5.0;
    static constexpr float referenceLevel = 0.25f;
    static constexpr float minToneHz = 400.0f;
    static constexpr float maxToneHz = 40000.0f;

    std::array<Channel, 2> channels;
    DistortionParameters target;
    double sampleRate = 44100.0;
    float smoothingCoef = 1.0f;
    struct Gains
    {
        float gain = 1.0f;   // antes de la curva
        float makeup = 1.0f; // compensación después
    };

    float drive = 0.0f, toneOctaves = 0.0f, mix = 1.0f;
    Gains current, previous; // del modo actual y del anterior (durante el fundido entre modos)
    SvfStage::Coefficients toneCoefficients;
    DistortionMode mode = DistortionMode::softClip, previousMode = DistortionMode::softClip;
    float modeFade = 1.0f, modeFadeStep = 1.0f;
    float dcCoef = 0.0f;
};

} // namespace undertow::dsp
