#pragma once

#include <algorithm>
#include <cmath>

namespace undertow::dsp
{

struct AdsrParameters
{
    float attackSeconds = 0.005f;
    float decaySeconds = 0.5f;
    float sustainLevel = 0.8f; // 0..1, nivel de amplitud (no un tiempo)
    float releaseSeconds = 0.15f;

    bool operator== (const AdsrParameters&) const = default;
};

// Envolvente ADSR con tramos exponenciales. Sin dependencias de JUCE para poder probarla aislada.
//
// Cada tramo es un filtro de un polo que persigue un objetivo:  nivel = objetivo + (nivel - objetivo) * coef.
// El objetivo se coloca un poco MÁS ALLÁ del destino (p. ej. 1.3 en el attack) para que la curva
// cruce el destino en un tiempo exacto en lugar de acercarse asintóticamente para siempre.
class AdsrEnvelope
{
public:
    enum class Stage { idle, attack, decay, sustain, release, quickRelease };

    void setSampleRate (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        updateCoefficients();
    }

    void setParameters (const AdsrParameters& newParameters) noexcept
    {
        // Se llama una vez por bloque; si nada cambió evitamos los exp()/log().
        if (newParameters == parameters)
            return;

        parameters = newParameters;
        updateCoefficients();
    }

    // El attack parte del nivel ACTUAL, no de 0: al redisparar una nota que aún suena
    // la curva continúa desde donde estaba y no hay salto (= no hay clic).
    void noteOn() noexcept { stage = Stage::attack; }

    void noteOff() noexcept
    {
        if (stage == Stage::idle || stage == Stage::release || stage == Stage::quickRelease)
            return;

        if (level <= 0.0)
        {
            reset();
            return;
        }

        // El objetivo es proporcional al nivel de partida: así el release dura exactamente
        // releaseSeconds tanto si se suelta la tecla en el pico como a mitad del decay.
        releaseTarget = -decayReleaseOvershoot * level;
        stage = Stage::release;
    }

    // Fade lineal muy corto que se usa al robar una voz. Es lo bastante rápido para liberar
    // la voz enseguida y lo bastante lento para que no haya discontinuidad audible.
    void quickRelease() noexcept
    {
        if (stage == Stage::idle)
            return;

        if (level <= 0.0)
        {
            reset();
            return;
        }

        quickReleaseStep = level / std::max (1.0, quickReleaseSeconds * sampleRate);
        stage = Stage::quickRelease;
    }

    void reset() noexcept
    {
        stage = Stage::idle;
        level = 0.0;
    }

    [[nodiscard]] float processSample() noexcept
    {
        switch (stage)
        {
            case Stage::idle:
                return 0.0f;

            case Stage::attack:
                level = attackTarget + (level - attackTarget) * attackCoef;
                if (level >= 1.0)
                {
                    level = 1.0;
                    stage = Stage::decay;
                }
                break;

            case Stage::decay:
            {
                // El objetivo se recalcula en cada muestra porque el sustain puede moverse durante el decay.
                const double sustain = parameters.sustainLevel;
                const double target = sustain - decayReleaseOvershoot * (1.0 - sustain);
                level = target + (level - target) * decayCoef;
                if (level <= sustain)
                {
                    level = sustain;
                    stage = Stage::sustain;
                }
                break;
            }

            case Stage::sustain:
                // Si el usuario gira el sustain mientras se mantiene la nota, el nivel lo sigue
                // suavemente (~5 ms) en lugar de saltar: sin esto habría zipper noise.
                level += (parameters.sustainLevel - level) * sustainSmoothingCoef;
                break;

            case Stage::release:
                level = releaseTarget + (level - releaseTarget) * releaseCoef;
                if (level <= 0.0)
                    reset();
                break;

            case Stage::quickRelease:
                level -= quickReleaseStep;
                if (level <= 0.0)
                    reset();
                break;
        }

        return static_cast<float> (level);
    }

    [[nodiscard]] bool isActive() const noexcept { return stage != Stage::idle; }
    [[nodiscard]] Stage getStage() const noexcept { return stage; }
    [[nodiscard]] float getLevel() const noexcept { return static_cast<float> (level); }

    static constexpr double quickReleaseSeconds = 0.005;

private:
    // Cuánto "se pasa" el objetivo del destino, como fracción del recorrido del tramo.
    // - Attack 0.3: curva ligeramente convexa, como la carga de un condensador analógico.
    //   Con valores más altos el attack se volvería casi lineal.
    // - Decay/Release 0.001: curva muy exponencial. El nivel cae de forma casi lineal EN DECIBELIOS
    //   (unos 60 dB en el tiempo indicado), que es como el oído percibe un desvanecimiento natural.
    static constexpr double attackOvershoot = 0.3;
    static constexpr double decayReleaseOvershoot = 0.001;
    static constexpr double attackTarget = 1.0 + attackOvershoot;
    static constexpr double sustainSmoothingSeconds = 0.005;

    // Coeficiente para que un tramo recorra la distancia (1 + r) -> r en 'seconds'.
    // Resolver (1 + r) · coef^N = r  da  coef = exp(-ln((1 + r) / r) / N).
    [[nodiscard]] double coefficientFor (double seconds, double overshoot) const noexcept
    {
        const double samples = std::max (1.0, seconds * sampleRate);
        return std::exp (-std::log ((1.0 + overshoot) / overshoot) / samples);
    }

    void updateCoefficients() noexcept
    {
        attackCoef = coefficientFor (parameters.attackSeconds, attackOvershoot);
        decayCoef = coefficientFor (parameters.decaySeconds, decayReleaseOvershoot);
        releaseCoef = coefficientFor (parameters.releaseSeconds, decayReleaseOvershoot);
        sustainSmoothingCoef = 1.0 - std::exp (-1.0 / (sustainSmoothingSeconds * sampleRate));
    }

    AdsrParameters parameters;
    double sampleRate = 44100.0;

    Stage stage = Stage::idle;
    double level = 0.0; // double: con tiempos largos coef ≈ 0.999993 y en float se perdería precisión

    double attackCoef = 0.0;
    double decayCoef = 0.0;
    double releaseCoef = 0.0;
    double sustainSmoothingCoef = 0.0;
    double releaseTarget = 0.0;
    double quickReleaseStep = 0.0;
};

} // namespace undertow::dsp
