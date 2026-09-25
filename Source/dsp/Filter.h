#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace undertow::dsp
{

// --- Filtro de estado variable (SVF) con topología TPT / ZDF -----------------------------------
//
// Un filtro analógico es un circuito con integradores (condensadores) conectados en lazo de
// realimentación. La forma "clásica" de digitalizarlo mete un retardo de una muestra en ese lazo,
// y ese retardo desafina el corte y vuelve el filtro inestable con resonancia alta o al modular
// el cutoff rápido. TPT (Topology-Preserving Transform, Zavalishin) conserva el circuito tal cual
// y sustituye cada integrador por uno trapezoidal. El lazo queda "sin retardo" (Zero-Delay Feedback):
// en cada muestra se resuelve una ecuación lineal pequeña en vez de usar el valor de la muestra anterior.
//
// Resultado: la respuesta es la del filtro analógico con el eje de frecuencias "doblado" (bilineal),
// el corte cae exactamente donde se pide gracias a g = tan(π·fc/fs), y es estable con cualquier
// cutoff y resonancia, incluso si cambian en cada muestra (imprescindible para la modulación de la Fase 5).

enum class FilterType { lowPass, highPass, bandPass };
enum class FilterSlope { db12, db24 };

struct FilterParameters
{
    FilterType type = FilterType::lowPass;
    FilterSlope slope = FilterSlope::db12;
    float cutoffHz = 1000.0f;
    float resonance = 0.0f; // 0..1
    float drive = 0.0f;     // 0..1 (0 = señal limpia, 1 = +30 dB antes de saturar)

    bool operator== (const FilterParameters&) const = default;
};

// Cutoff con seguimiento de teclado: al 100 % el corte sube una octava por cada octava de la nota,
// así el filtro deja pasar los mismos armónicos en todo el teclado. La referencia es C5 de FL (MIDI 60).
[[nodiscard]] inline float keyTrackedCutoff (float cutoffHz, int midiNote, float amount) noexcept
{
    return cutoffHz * std::exp2 (amount * static_cast<float> (midiNote - 60) / 12.0f);
}

namespace filter_detail
{
    // Q de Butterworth (la respuesta más plana posible, sin pico) para 2 y 4 polos.
    // El de 4 polos son dos etapas de 2 polos con Q distintos: 0.541 y 1.307.
    inline constexpr float q12 = 0.70710678f;
    inline constexpr float q24First = 0.54119610f;
    inline constexpr float q24Second = 1.30656296f;

    // Resonancia 0..1 → el Q de la etapa resonante se multiplica hasta ×17 de forma exponencial
    // (12 dB: Q 0.71 → 12, pico de +21 dB). El oído percibe la resonancia de forma logarítmica: con
    // un mapeo lineal toda la acción se concentraría en el último 10 % de la perilla.
    inline constexpr float resonanceRange = 17.0f;

    [[nodiscard]] inline float resonanceMultiplier (float resonance) noexcept
    {
        return std::pow (resonanceRange, std::clamp (resonance, 0.0f, 1.0f));
    }

    // Compensación de volumen para LP/HP: con resonancia alta se pierden −6 dB en la banda de paso,
    // como en un filtro analógico tipo "ladder". Sin ella, subir la resonancia subiría demasiado el volumen.
    // El band-pass está normalizado a ganancia 1 en su pico: la resonancia lo estrecha, no lo sube.
    [[nodiscard]] inline float resonanceCompensation (FilterType type, float resonance) noexcept
    {
        return type == FilterType::bandPass ? 1.0f : 1.0f / std::sqrt (std::sqrt (resonanceMultiplier (resonance)));
    }

    [[nodiscard]] inline double maxCutoffHz (double sampleRate) noexcept { return std::min (0.45 * sampleRate, 22000.0); }
    inline constexpr double minCutoffHz = 5.0;
} // namespace filter_detail

// Una etapa SVF de 2 polos (12 dB/oct). Da a la vez las salidas low-pass, band-pass y high-pass.
class SvfStage
{
public:
    struct Coefficients
    {
        float g = 0.0f;  // tan(π·fc/fs): la ganancia de cada integrador ya "prewarpeada"
        float k = 1.0f;  // 1/Q: amortiguación. Cuanto menor, más resonancia
        float a1 = 1.0f; // 1 / (1 + g·(g + k)): la solución de la ecuación del lazo sin retardo

        static Coefficients make (float g, float k) noexcept { return { g, k, 1.0f / (1.0f + g * (g + k)) }; }
    };

    struct Outputs
    {
        float lowPass, bandPass, highPass;
    };

    void reset() noexcept { s1 = s2 = 0.0f; }

    [[nodiscard]] Outputs process (float x, const Coefficients& c) noexcept
    {
        // s1 y s2 son los "condensadores" (estado de los dos integradores).
        // Se despeja el high-pass de golpe (ZDF) y de él salen las otras dos salidas.
        const float highPass = (x - (c.g + c.k) * s1 - s2) * c.a1;
        const float v1 = c.g * highPass;
        const float bandPass = v1 + s1;
        s1 = bandPass + v1;
        const float v2 = c.g * bandPass;
        const float lowPass = v2 + s2;
        s2 = lowPass + v2;
        return { lowPass, bandPass, highPass };
    }

private:
    float s1 = 0.0f;
    float s2 = 0.0f;
};

// Filtro completo de una voz: drive → etapa 1 → etapa 2 (solo pesa en 24 dB).
// Todos los parámetros se suavizan por muestra: girar perillas o cambiar de tipo no hace clics.
class Filter
{
public:
    void setSampleRate (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        smoothingCoef = static_cast<float> (1.0 - std::exp (-1.0 / (smoothingSeconds * sampleRate)));
        setParameters (parameters);
        snapToTargets();
    }

    void setParameters (const FilterParameters& newParameters) noexcept
    {
        using namespace filter_detail;
        parameters = newParameters;

        const double clamped = std::clamp (static_cast<double> (parameters.cutoffHz), minCutoffHz, maxCutoffHz (sampleRate));
        target.log2Cutoff = static_cast<float> (std::log2 (clamped));

        const float resonant = resonanceMultiplier (parameters.resonance);
        const bool is24 = parameters.slope == FilterSlope::db24;
        target.k1 = 1.0f / (is24 ? q24First : q12 * resonant);
        target.k2 = 1.0f / (is24 ? q24Second * resonant : q12);
        target.secondStage = is24 ? 1.0f : 0.0f;

        target.lowPass = parameters.type == FilterType::lowPass ? 1.0f : 0.0f;
        target.bandPass = parameters.type == FilterType::bandPass ? 1.0f : 0.0f;
        target.highPass = parameters.type == FilterType::highPass ? 1.0f : 0.0f;
        target.compensation = resonanceCompensation (parameters.type, parameters.resonance);

        // La mezcla con la señal limpia entra en el primer 10 % de la perilla: con drive 0 el filtro es
        // lineal (no colorea nada) y al empezar a girarla la saturación aparece de forma continua.
        const float drive = std::clamp (parameters.drive, 0.0f, 1.0f);
        target.driveMix = std::min (1.0f, drive * 10.0f);
        target.driveGain = std::pow (10.0f, drive * maxDriveDb / 20.0f);
    }

    // Nota que empieza desde silencio: estado a cero y parámetros directamente en su valor final
    // (no tiene sentido "barrer" el filtro desde los valores de la nota anterior).
    void reset() noexcept
    {
        stage1.reset();
        stage2.reset();
        snapToTargets();
    }

    [[nodiscard]] float processSample (float input) noexcept
    {
        smoothParameters();

        // Drive: se amplifica y se satura con tanh, que redondea los picos como un transistor saturado.
        // Añade armónicos ANTES del filtro: con low-pass, el propio filtro recorta los más agudos.
        float x = input;
        if (current.driveMix > 0.0f)
            x += current.driveMix * (std::tanh (current.driveGain * x) - x);

        const auto out1 = stage1.process (x, coefficients1);
        const float y1 = mix (out1, coefficients1.k);

        // La etapa 2 procesa siempre (también en 12 dB) para que su estado esté "caliente" y pasar
        // de 12 a 24 dB sea un fundido continuo y no un arranque desde cero.
        const auto out2 = stage2.process (y1, coefficients2);
        const float y2 = mix (out2, coefficients2.k);

        return (y1 + (y2 - y1) * current.secondStage) * current.compensation;
    }

    [[nodiscard]] const FilterParameters& getParameters() const noexcept { return parameters; }

private:
    struct Smoothed
    {
        float log2Cutoff = 10.0f;
        float k1 = 1.0f, k2 = 1.0f;
        float secondStage = 0.0f;
        float lowPass = 1.0f, bandPass = 0.0f, highPass = 0.0f;
        float compensation = 1.0f;
        float driveMix = 0.0f, driveGain = 1.0f;
    };

    // El band-pass se multiplica por k para que su pico valga 1 con cualquier resonancia.
    [[nodiscard]] float mix (const SvfStage::Outputs& o, float k) const noexcept
    {
        return current.lowPass * o.lowPass + current.bandPass * k * o.bandPass + current.highPass * o.highPass;
    }

    static void approach (float& value, float goal, float coef) noexcept
    {
        value += (goal - value) * coef;
        if (std::abs (goal - value) < 1.0e-5f)
            value = goal;
    }

    void smoothParameters() noexcept
    {
        // tan() y exp2() son caros: los coeficientes solo se recalculan mientras el cutoff o la
        // resonancia se están moviendo. Con las perillas quietas el filtro cuesta unas pocas multiplicaciones.
        const bool coefficientsMoving = current.log2Cutoff != target.log2Cutoff || current.k1 != target.k1
                                        || current.k2 != target.k2;

        approach (current.log2Cutoff, target.log2Cutoff, smoothingCoef);
        approach (current.k1, target.k1, smoothingCoef);
        approach (current.k2, target.k2, smoothingCoef);
        approach (current.secondStage, target.secondStage, smoothingCoef);
        approach (current.lowPass, target.lowPass, smoothingCoef);
        approach (current.bandPass, target.bandPass, smoothingCoef);
        approach (current.highPass, target.highPass, smoothingCoef);
        approach (current.compensation, target.compensation, smoothingCoef);
        approach (current.driveMix, target.driveMix, smoothingCoef);
        approach (current.driveGain, target.driveGain, smoothingCoef);

        if (coefficientsMoving)
            updateCoefficients();
    }

    void snapToTargets() noexcept
    {
        current = target;
        updateCoefficients();
    }

    void updateCoefficients() noexcept
    {
        // El cutoff se suaviza en octavas (log2), no en Hz: así un barrido de 100 Hz a 10 kHz
        // avanza igual de rápido en cada octava, que es como lo percibe el oído.
        const double cutoff = std::exp2 (static_cast<double> (current.log2Cutoff));
        const auto g = static_cast<float> (std::tan (std::numbers::pi * cutoff / sampleRate));
        coefficients1 = SvfStage::Coefficients::make (g, current.k1);
        coefficients2 = SvfStage::Coefficients::make (g, current.k2);
    }

    static constexpr double smoothingSeconds = 0.005;
    static constexpr float maxDriveDb = 30.0f;

    FilterParameters parameters;
    Smoothed target;
    Smoothed current;
    SvfStage stage1, stage2;
    SvfStage::Coefficients coefficients1, coefficients2;
    double sampleRate = 44100.0;
    float smoothingCoef = 1.0f;
};

// Respuesta en frecuencia teórica (en ganancia lineal) del filtro con estos parámetros, sin drive.
// La usa la GUI para dibujar la curva y los tests para comprobar que el filtro hace lo que dice.
// Como TPT equivale a la transformada bilineal, basta evaluar el prototipo analógico en
// Ω = tan(π·f/fs) / tan(π·fc/fs).
[[nodiscard]] inline double filterMagnitude (const FilterParameters& p, double frequencyHz, double sampleRate) noexcept
{
    using namespace filter_detail;
    using Complex = std::complex<double>;

    const double cutoff = std::clamp (static_cast<double> (p.cutoffHz), minCutoffHz, maxCutoffHz (sampleRate));
    const double f = std::min (frequencyHz, 0.4999 * sampleRate);
    const Complex s { 0.0, std::tan (std::numbers::pi * f / sampleRate) / std::tan (std::numbers::pi * cutoff / sampleRate) };

    const auto stage = [&] (double q) {
        const double k = 1.0 / q;
        const Complex denominator = s * s + k * s + 1.0;
        switch (p.type)
        {
            case FilterType::highPass: return s * s / denominator;
            case FilterType::bandPass: return k * s / denominator;
            case FilterType::lowPass:
            default: return Complex { 1.0 } / denominator;
        }
    };

    const double resonant = resonanceMultiplier (p.resonance);
    const Complex response = p.slope == FilterSlope::db24 ? stage (q24First) * stage (q24Second * resonant)
                                                          : stage (q12 * resonant);
    return std::abs (response) * resonanceCompensation (p.type, p.resonance);
}

} // namespace undertow::dsp
