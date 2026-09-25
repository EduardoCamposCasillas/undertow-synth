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
//
// Dos capas de valores (Fase 5):
//  - Las perillas (setParameters) se suavizan 5 ms: evitan el zipper de la automatización por bloques.
//  - La modulación (setModulation) se suma DESPUÉS de ese suavizado, muestra a muestra: una envolvente
//    de 1 ms sobre el cutoff debe llegar en 1 ms, no emborronada por el suavizado de la perilla.
class Filter
{
public:
    void setSampleRate (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        smoothingCoef = static_cast<float> (1.0 - std::exp (-1.0 / (smoothingSeconds * sampleRate)));
        minLog2Cutoff = static_cast<float> (std::log2 (filter_detail::minCutoffHz));
        maxLog2Cutoff = static_cast<float> (std::log2 (filter_detail::maxCutoffHz (sampleRate)));
        setParameters (parameters);
        snapToTargets();
    }

    void setParameters (const FilterParameters& newParameters) noexcept
    {
        parameters = newParameters;

        target.log2Cutoff = std::clamp (static_cast<float> (std::log2 (std::max (parameters.cutoffHz, 1.0f))),
                                        minLog2Cutoff, maxLog2Cutoff);
        target.resonance = std::clamp (parameters.resonance, 0.0f, 1.0f);
        target.drive = std::clamp (parameters.drive, 0.0f, 1.0f);
        target.secondStage = parameters.slope == FilterSlope::db24 ? 1.0f : 0.0f;
        target.lowPass = parameters.type == FilterType::lowPass ? 1.0f : 0.0f;
        target.bandPass = parameters.type == FilterType::bandPass ? 1.0f : 0.0f;
        target.highPass = parameters.type == FilterType::highPass ? 1.0f : 0.0f;
    }

    // Desplazamientos de la matriz de modulación: cutoff en octavas; resonancia y drive en fracción del
    // recorrido de la perilla (se suman al valor de la perilla y el resultado se limita a 0..1).
    void setModulation (float cutoffOctaves, float resonanceOffset, float driveOffset) noexcept
    {
        modulation = { cutoffOctaves, resonanceOffset, driveOffset };
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
        if (driveMix > 0.0f)
            x += driveMix * (std::tanh (driveGain * x) - x);

        const auto out1 = stage1.process (x, coefficients1);
        const float y1 = mix (out1, coefficients1.k);

        // La etapa 2 procesa siempre (también en 12 dB) para que su estado esté "caliente" y pasar
        // de 12 a 24 dB sea un fundido continuo y no un arranque desde cero.
        const auto out2 = stage2.process (y1, coefficients2);
        const float y2 = mix (out2, coefficients2.k);

        return (y1 + (y2 - y1) * current.secondStage) * compensation;
    }

    [[nodiscard]] const FilterParameters& getParameters() const noexcept { return parameters; }

    // Cutoff que suena ahora mismo (perilla suavizada + modulación). Lo usan los tests.
    [[nodiscard]] float getEffectiveCutoffHz() const noexcept { return std::exp2 (effectiveLog2Cutoff); }

private:
    struct Smoothed
    {
        float log2Cutoff = 10.0f;
        float resonance = 0.0f;
        float drive = 0.0f;
        float secondStage = 0.0f;
        float lowPass = 1.0f, bandPass = 0.0f, highPass = 0.0f;
    };

    struct Modulation
    {
        float cutoffOctaves = 0.0f, resonance = 0.0f, drive = 0.0f;
        bool operator== (const Modulation&) const = default;
    };

    // El band-pass se multiplica por k para que su pico valga 1 con cualquier resonancia.
    [[nodiscard]] float mix (const SvfStage::Outputs& o, float k) const noexcept
    {
        return current.lowPass * o.lowPass + current.bandPass * k * o.bandPass + current.highPass * o.highPass;
    }

    // Devuelve true si el valor se movió.
    static bool approach (float& value, float goal, float coef) noexcept
    {
        if (value == goal)
            return false;
        value += (goal - value) * coef;
        if (std::abs (goal - value) < 1.0e-5f)
            value = goal;
        return true;
    }

    void smoothParameters() noexcept
    {
        // tan() y pow() son caros: cada grupo de coeficientes solo se recalcula si algo de lo que depende
        // se movió (perilla suavizándose o modulación distinta). Con todo quieto, el filtro cuesta unas
        // pocas multiplicaciones por muestra.
        bool cutoffMoved = approach (current.log2Cutoff, target.log2Cutoff, smoothingCoef);
        bool shapeMoved = approach (current.resonance, target.resonance, smoothingCoef);
        shapeMoved |= approach (current.secondStage, target.secondStage, smoothingCoef);
        shapeMoved |= approach (current.bandPass, target.bandPass, smoothingCoef);
        approach (current.lowPass, target.lowPass, smoothingCoef);
        approach (current.highPass, target.highPass, smoothingCoef);
        bool driveMoved = approach (current.drive, target.drive, smoothingCoef);

        if (modulation != appliedModulation)
        {
            cutoffMoved |= modulation.cutoffOctaves != appliedModulation.cutoffOctaves;
            shapeMoved |= modulation.resonance != appliedModulation.resonance;
            driveMoved |= modulation.drive != appliedModulation.drive;
            appliedModulation = modulation;
        }

        if (cutoffMoved)
            updateCutoff();
        if (shapeMoved)
            updateShape();
        if (cutoffMoved || shapeMoved)
            updateCoefficients();
        if (driveMoved)
            updateDrive();
    }

    void snapToTargets() noexcept
    {
        current = target;
        appliedModulation = modulation;
        updateCutoff();
        updateShape();
        updateCoefficients();
        updateDrive();
    }

    void updateCutoff() noexcept
    {
        // El cutoff vive en octavas (log2), no en Hz: el suavizado y la modulación avanzan igual en cada
        // octava, que es como lo percibe el oído (un LFO de ±1 octava sube y baja lo mismo "de oído").
        effectiveLog2Cutoff = std::clamp (current.log2Cutoff + appliedModulation.cutoffOctaves, minLog2Cutoff, maxLog2Cutoff);
        const double cutoff = std::exp2 (static_cast<double> (effectiveLog2Cutoff));
        g = static_cast<float> (std::tan (std::numbers::pi * cutoff / sampleRate));
    }

    void updateShape() noexcept
    {
        using namespace filter_detail;
        const float resonance = std::clamp (current.resonance + appliedModulation.resonance, 0.0f, 1.0f);
        const float resonant = resonanceMultiplier (resonance);

        // 12 dB: la etapa 1 es la resonante. 24 dB: la 1 es Butterworth fija y la 2 lleva la resonancia.
        // secondStage se funde de 0 a 1 al cambiar de pendiente: los k se interpolan con él y no saltan.
        const float s = current.secondStage;
        k1 = 1.0f / (q12 * resonant) + (1.0f / q24First - 1.0f / (q12 * resonant)) * s;
        k2 = 1.0f / q12 + (1.0f / (q24Second * resonant) - 1.0f / q12) * s;

        // LP/HP pierden hasta -6 dB con resonancia alta (tipo ladder); el band-pass no (ver resonanceCompensation).
        compensation = current.bandPass + (1.0f - current.bandPass) * resonanceCompensation (FilterType::lowPass, resonance);
    }

    void updateCoefficients() noexcept
    {
        coefficients1 = SvfStage::Coefficients::make (g, k1);
        coefficients2 = SvfStage::Coefficients::make (g, k2);
    }

    void updateDrive() noexcept
    {
        // La mezcla con la señal limpia entra en el primer 10 % de la perilla: con drive 0 el filtro es
        // lineal (no colorea nada) y al empezar a girarla la saturación aparece de forma continua.
        const float drive = std::clamp (current.drive + appliedModulation.drive, 0.0f, 1.0f);
        driveMix = std::min (1.0f, drive * 10.0f);
        driveGain = std::pow (10.0f, drive * maxDriveDb / 20.0f);
    }

    static constexpr double smoothingSeconds = 0.005;
    static constexpr float maxDriveDb = 30.0f;

    FilterParameters parameters;
    Smoothed target;
    Smoothed current;
    Modulation modulation, appliedModulation;
    SvfStage stage1, stage2;
    SvfStage::Coefficients coefficients1, coefficients2;
    double sampleRate = 44100.0;
    float smoothingCoef = 1.0f;
    float minLog2Cutoff = 2.3f, maxLog2Cutoff = 14.2f;

    // Valores derivados (se recalculan solo cuando cambia algo de lo que dependen).
    float effectiveLog2Cutoff = 10.0f;
    float g = 0.0f, k1 = 1.0f, k2 = 1.0f;
    float compensation = 1.0f;
    float driveMix = 0.0f, driveGain = 1.0f;
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
