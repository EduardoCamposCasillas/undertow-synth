#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

#include "dsp/Wavetable.h"

namespace undertow::dsp
{

// Oscilador wavetable sin aliasing audible, con unison. Sin dependencias de JUCE para poder probarlo aislado.
//
// Cuatro ideas:
//  1) Acumulador de fase (como en la Fase 1): la fase en [0, 1) multiplicada por el tamaño del frame
//     es la posición de lectura dentro del ciclo.
//  2) Mipmaps: según la frecuencia de la nota se lee la versión del ciclo con los armónicos justos
//     para que ninguno se refleje (alias) dentro de la banda audible.
//  3) Morphing: la posición (0..1) cae entre dos frames y se mezclan los dos.
//  4) Unison (Fase 6): hasta 16 copias de la misma onda, cada una con su fase, un poco desafinadas entre sí
//     y repartidas en el panorama estéreo. Comparten tabla, mipmap y frames: solo cambia la fase de cada una,
//     así que una copia extra cuesta un par de interpolaciones y no un oscilador entero.
class WavetableOscillator
{
public:
    static constexpr int maxUnison = 16;

    // Anchura de la zona de fundido entre mipmaps: 1/6 de octava (2 semitonos) antes de cada límite.
    static constexpr double mipBlendOctaves = 1.0 / 6.0;

    // Detune al 100 %: las copias de los extremos quedan a ±100 cents (±1 semitono) de la nota.
    static constexpr double maxDetuneCents = 100.0;

    void setSampleRate (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;

        // Un armónico por encima de Nyquist (sr/2) se refleja: si está en f, aparece en sr - f.
        // Si f < sr - 20 kHz, su reflejo cae POR ENCIMA de 20 kHz y no se oye. A 44.1/48 kHz eso deja
        // subir los armónicos hasta 24-28 kHz en lugar de 22-24 kHz: las notas agudas conservan más brillo.
        // A 88.2/96 kHz no hace falta: una octava por debajo de Nyquist ya cubre toda la banda audible,
        // así que ahí no se permite ningún reflejo.
        const double nyquist = 0.5 * sampleRate;
        maxHarmonicFrequency = nyquist >= 2.0 * audibleLimitHz ? nyquist
                                                                : std::max (nyquist, sampleRate - audibleLimitHz);

        positionSmoothingCoef = smoothingCoefficient (positionSmoothingSeconds);
        unisonSmoothingCoef = smoothingCoefficient (unisonSmoothingSeconds);
        crossfadeLength = std::max (1, static_cast<int> (tableCrossfadeSeconds * sampleRate));
        updateIncrement();
    }

    void setFrequency (double newFrequencyHz) noexcept
    {
        baseFrequencyHz = newFrequencyHz;
        updateIncrement();
    }

    // Desplazamiento de tono en semitonos: afinación del oscilador (octava, semitonos, fine) más la modulación
    // (vibrato, caídas de pitch). La voz la llama en cada muestra: si el valor no cambió no se recalcula nada.
    void setPitchOffset (float semitones) noexcept
    {
        if (semitones == pitchOffset)
            return;
        pitchOffset = semitones;
        updateIncrement();
    }

    // Modulación de Position (fracción del recorrido, -1..1). Se suma DESPUÉS del suavizado de la perilla:
    // la perilla se mueve con inercia (sin zipper) y la modulación llega al instante (un LFO rápido o un
    // attack corto no se "emborronan").
    void setPositionModulation (float amount) noexcept { positionModulation = amount; }

    // Cambiar de tabla con la nota sonando haría un salto en la forma de onda (clic):
    // durante unos milisegundos se leen las dos y se hace un fundido cruzado.
    void setWavetable (const Wavetable* newTable) noexcept
    {
        if (newTable == table)
            return;

        if (table != nullptr && newTable != nullptr)
        {
            previousTable = table;
            crossfadeRemaining = crossfadeLength;
        }
        table = newTable;
    }

    void setPosition (float newPosition) noexcept { targetPosition = std::clamp (newPosition, 0.0f, 1.0f); }

    // Unison: cuántas copias suenan, cuánto se desafinan (0..1), cuánto se abren en estéreo (0..1) y el
    // paneo del oscilador entero (-1 izquierda .. +1 derecha). Se llama una vez por bloque.
    void setUnison (int voices, float newDetune, float width, float pan) noexcept
    {
        voices = std::clamp (voices, 1, maxUnison);
        targetDetune = std::clamp (newDetune, 0.0f, 1.0f);
        width = std::clamp (width, 0.0f, 1.0f);
        pan = std::clamp (pan, -1.0f, 1.0f);

        if (voices == numVoices && width == stereoWidth && pan == panPosition)
            return;

        const bool countChanged = voices != numVoices;
        numVoices = voices;
        stereoWidth = width;
        panPosition = pan;
        updateGainTargets();
        if (countChanged)
            updateIncrement(); // las posiciones de detune dependen del número de copias
    }

    // Modulación del detune desde la matriz (fracción del recorrido de la perilla), por muestra.
    void setDetuneModulation (float amount) noexcept { detuneModulation = amount; }

    // Para una nota que empieza desde silencio: sin arrastrar transiciones (morph, fundido de tabla, cambios
    // de ganancia del unison) que se quedaron a medias en la nota anterior.
    //
    // Fases: con una sola copia, fase 0 (cada nota arranca igual, con el mismo "golpe"). Con varias, cada una
    // empieza en una fase al azar: si todas salieran juntas, el principio de la nota sonaría a un pico fuerte
    // seguido de un barrido de "flanger" mientras se separan. 'seed' hace que cada nota tenga su reparto.
    void reset (std::uint32_t seed = 0) noexcept
    {
        for (size_t k = 0; k < phases.size(); ++k)
            phases[k] = numVoices == 1 && k == 0 ? 0.0 : randomPhase (seed, static_cast<std::uint32_t> (k));

        position = targetPosition;
        previousTable = nullptr;
        crossfadeRemaining = 0;
        detune = targetDetune;
        gains = targetGains;
        gainsSettled = true;
        updateActiveCount();
        updateIncrement();
    }

    // Salida mono: la mezcla (L + R) / 2 de lo que daría processStereo.
    [[nodiscard]] float processSample() noexcept
    {
        float left = 0.0f, right = 0.0f;
        render<false> (left, right);
        return left;
    }

    void processStereo (float& left, float& right) noexcept
    {
        left = right = 0.0f;
        render<true> (left, right);
    }

    // True si los canales izquierdo y derecho pueden ser distintos (unison abierto o paneo fuera del centro).
    // La voz lo usa para procesar el filtro en mono cuando no hace falta el estéreo.
    [[nodiscard]] bool isStereo() const noexcept { return stereoTarget || ! gainsSettled; }

    [[nodiscard]] int getMipLevel() const noexcept { return mipLevel; }
    [[nodiscard]] float getMipBlend() const noexcept { return mipBlend; }
    [[nodiscard]] double getFrequency() const noexcept { return frequencyHz; } // la de la copia central
    [[nodiscard]] int getNumVoices() const noexcept { return numVoices; }
    [[nodiscard]] double getVoiceFrequency (int voice) const noexcept
    {
        return frequencyHz * ratios[static_cast<size_t> (voice)];
    }

private:
    // Punteros de lectura de un nivel de una tabla en la posición actual: se calculan una vez por muestra
    // y los comparten todas las copias del unison.
    struct FrameReader
    {
        const float* a = nullptr;
        const float* b = nullptr; // nullptr si la tabla tiene un solo frame
        float frameFraction = 0.0f;
        double size = 0.0;

        [[nodiscard]] float read (double phase) const noexcept
        {
            const double readPosition = phase * size;
            const int index = static_cast<int> (readPosition);
            const auto fraction = static_cast<float> (readPosition - index);

            const float first = interpolate (a + index, fraction);
            if (b == nullptr)
                return first;
            return first + (interpolate (b + index, fraction) - first) * frameFraction;
        }
    };

    // Cerca del límite entre dos mipmaps se mezclan los dos niveles (ver updateIncrement).
    struct TableReader
    {
        FrameReader rich, poor;
        float mipBlend = 0.0f;

        [[nodiscard]] float read (double phase) const noexcept
        {
            const float value = rich.read (phase);
            if (mipBlend <= 0.0f)
                return value;
            return value + (poor.read (phase) - value) * mipBlend;
        }
    };

    [[nodiscard]] static FrameReader makeFrameReader (const Wavetable& wavetable, int level, float normalisedPosition) noexcept
    {
        FrameReader reader;
        reader.size = Wavetable::levelSize (level);

        const int numFrames = wavetable.getNumFrames();
        if (numFrames == 1)
        {
            reader.a = wavetable.getFrame (level, 0);
            return reader;
        }

        const float framePosition = normalisedPosition * static_cast<float> (numFrames - 1);
        const int firstFrame = std::min (static_cast<int> (framePosition), numFrames - 2);
        reader.a = wavetable.getFrame (level, firstFrame);
        reader.b = wavetable.getFrame (level, firstFrame + 1);
        reader.frameFraction = framePosition - static_cast<float> (firstFrame);
        return reader;
    }

    [[nodiscard]] TableReader makeTableReader (const Wavetable& wavetable, float framePosition) const noexcept
    {
        TableReader reader;
        reader.rich = makeFrameReader (wavetable, mipLevel, framePosition);
        reader.mipBlend = mipBlend;
        if (mipBlend > 0.0f)
            reader.poor = makeFrameReader (wavetable, mipLevel + 1, framePosition);
        return reader;
    }

    template <bool stereo>
    void render (float& left, float& right) noexcept
    {
        if (table == nullptr)
            return;

        // Position se suaviza por muestra: girar la perilla recorre los frames intermedios en vez de saltar.
        position += (targetPosition - position) * positionSmoothingCoef;
        const float framePosition = std::clamp (position + positionModulation, 0.0f, 1.0f);

        smoothUnison();

        const TableReader current = makeTableReader (*table, framePosition);
        TableReader previous;
        float oldWeight = 0.0f;
        if (crossfadeRemaining > 0)
        {
            previous = makeTableReader (*previousTable, framePosition);
            oldWeight = static_cast<float> (crossfadeRemaining) / static_cast<float> (crossfadeLength);
            if (--crossfadeRemaining == 0)
                previousTable = nullptr;
        }

        for (int k = 0; k < activeCount; ++k)
        {
            const auto v = static_cast<size_t> (k);
            float value = current.read (phases[v]);
            if (oldWeight > 0.0f)
                value += (previous.read (phases[v]) - value) * oldWeight;

            if constexpr (stereo)
            {
                left += gains[v].left * value;
                right += gains[v].right * value;
            }
            else
            {
                left += gains[v].mono * value;
            }

            phases[v] += phaseIncrement * ratios[v];
            if (phases[v] >= 1.0)
                phases[v] -= 1.0;
        }
    }

    // Interpolación cúbica de Catmull-Rom con 4 muestras (p[-1], p[0], p[1], p[2]).
    // La lineal (2 muestras) es más barata pero apaga los armónicos altos y deja "imágenes" espurias;
    // la cúbica pasa por las muestras y además respeta la pendiente de la onda en cada punto.
    [[nodiscard]] static float interpolate (const float* p, float t) noexcept
    {
        const float c1 = 0.5f * (p[1] - p[-1]);
        const float c2 = p[-1] - 2.5f * p[0] + 2.0f * p[1] - 0.5f * p[2];
        const float c3 = 0.5f * (p[2] - p[-1]) + 1.5f * (p[0] - p[1]);
        return ((c3 * t + c2) * t + c1) * t + p[0];
    }

    // Posición de la copia k en el abanico del unison: de -1 (la más grave) a +1 (la más aguda), repartidas
    // a distancias iguales. Con una sola copia, 0 (sin detune).
    [[nodiscard]] float spreadPosition (int k) const noexcept
    {
        if (numVoices == 1)
            return 0.0f;
        return -1.0f + 2.0f * static_cast<float> (k) / static_cast<float> (numVoices - 1);
    }

    void smoothUnison() noexcept
    {
        // Detune: perilla suavizada + modulación. Solo se recalculan las razones de frecuencia si cambió.
        if (detune != targetDetune)
        {
            detune += (targetDetune - detune) * unisonSmoothingCoef;
            if (std::abs (targetDetune - detune) < 1.0e-5f)
                detune = targetDetune;
        }
        if (std::clamp (detune + detuneModulation, 0.0f, 1.0f) != appliedDetune)
            updateIncrement();

        // Ganancias de cada copia: se deslizan 5 ms hacia su objetivo. Así cambiar el número de copias, el
        // ancho o el paneo con la nota sonando no produce saltos de nivel (clics) en ningún canal.
        if (gainsSettled)
            return;

        gainsSettled = true;
        for (size_t k = 0; k < gains.size(); ++k)
        {
            auto& g = gains[k];
            const auto& t = targetGains[k];
            g.left += (t.left - g.left) * unisonSmoothingCoef;
            g.right += (t.right - g.right) * unisonSmoothingCoef;
            if (std::abs (t.left - g.left) < 1.0e-6f && std::abs (t.right - g.right) < 1.0e-6f)
            {
                g.left = t.left;
                g.right = t.right;
            }
            else
            {
                gainsSettled = false;
            }
            g.mono = 0.5f * (g.left + g.right);
        }
        updateActiveCount();
    }

    // Nivel y paneo de cada copia.
    //
    // Nivel: N copias con fases al azar no se suman en amplitud sino en potencia, que crece como N. Cada copia
    // lleva 1/√N para que la potencia total (lo que el oído percibe como volumen) no cambie al subir el unison.
    //
    // Paneo: las copias se agrupan en parejas simétricas (la más grave con la más aguda, etc.) y cada pareja se
    // abre a los dos lados, alternando qué lado recibe la grave. Así cada canal tiene copias agudas y graves;
    // si el paneo siguiera al detune, un lado sonaría desafinado hacia abajo y el otro hacia arriba.
    //
    // Ley de paneo "equal power": L = cos θ, R = sin θ, con L² + R² constante. Escalada por √2 para que en el
    // centro L = R = 1: un sonido sin paneo ni unison suena exactamente igual que en la Fase 5.
    void updateGainTargets() noexcept
    {
        const float level = 1.0f / std::sqrt (static_cast<float> (numVoices));
        stereoTarget = false;

        for (int k = 0; k < maxUnison; ++k)
        {
            auto& t = targetGains[static_cast<size_t> (k)];
            if (k >= numVoices)
            {
                t = {};
                continue;
            }

            const int pair = std::min (k, numVoices - 1 - k);
            const float side = pair % 2 == 0 ? 1.0f : -1.0f;
            const float voicePan = std::clamp (panPosition + stereoWidth * side * spreadPosition (k), -1.0f, 1.0f);
            const float angle = (voicePan + 1.0f) * 0.25f * std::numbers::pi_v<float>;
            t.left = level * std::numbers::sqrt2_v<float> * std::cos (angle);
            t.right = level * std::numbers::sqrt2_v<float> * std::sin (angle);
            if (voicePan == 0.0f)
                t.left = t.right = level; // exacto en el centro (cos(π/4)·√2 no da 1 exacto en float)
            t.mono = 0.5f * (t.left + t.right);
            stereoTarget = stereoTarget || t.left != t.right;
        }
        gainsSettled = false;
    }

    // Solo se procesan las copias que suenan o que todavía se están apagando.
    void updateActiveCount() noexcept
    {
        activeCount = numVoices;
        for (int k = maxUnison - 1; k >= numVoices; --k)
        {
            const auto& g = gains[static_cast<size_t> (k)];
            if (g.left != 0.0f || g.right != 0.0f)
            {
                activeCount = k + 1;
                break;
            }
        }
    }

    // Fase inicial al azar (0..1) a partir de (semilla, copia): repetible y sin estado.
    [[nodiscard]] static double randomPhase (std::uint32_t seed, std::uint32_t k) noexcept
    {
        std::uint64_t x = (static_cast<std::uint64_t> (seed) << 32) ^ (k * 0x9E3779B97F4A7C15ull);
        x ^= x >> 30;
        x *= 0xBF58476D1CE4E5B9ull;
        x ^= x >> 27;
        x *= 0x94D049BB133111EBull;
        x ^= x >> 31;
        return static_cast<double> (x >> 11) * 0x1.0p-53;
    }

    [[nodiscard]] float smoothingCoefficient (double seconds) const noexcept
    {
        return static_cast<float> (1.0 - std::exp (-1.0 / (seconds * sampleRate)));
    }

    void updateIncrement() noexcept
    {
        frequencyHz = pitchOffset == 0.0f ? baseFrequencyHz
                                          : baseFrequencyHz * std::exp2 (static_cast<double> (pitchOffset) / 12.0);

        // Con Octave +4, Semi +12 y la modulación de pitch al máximo, una nota aguda pediría cientos de kHz.
        // Un incremento de fase >= 1 (más de un ciclo por muestra) haría que la fase se saliera de [0, 1) y la
        // lectura de la tabla, de la memoria. Por encima de 0.45·sr no queda nada audible que generar.
        frequencyHz = std::min (frequencyHz, maxFrequencyRatio * sampleRate);
        phaseIncrement = sampleRate > 0.0 ? frequencyHz / sampleRate : 0.0;

        // Detune: curva cuadrática (25 % → ±6 cents, 50 % → ±25 cents, 100 % → ±100 cents). La mitad baja de la
        // perilla queda para el "chorus" sutil, donde el oído distingue diferencias de un par de cents.
        // Las copias están a distancias iguales EN CENTS, así que sus razones de frecuencia forman una
        // progresión geométrica: bastan dos exp2 en vez de una por copia.
        appliedDetune = std::clamp (detune + detuneModulation, 0.0f, 1.0f);
        const double spreadSemitones = maxDetuneCents / 100.0 * appliedDetune * appliedDetune;
        double highestRatio = 1.0;
        if (numVoices == 1 || spreadSemitones == 0.0)
        {
            ratios.fill (1.0);
        }
        else
        {
            const double lowest = std::exp2 (-spreadSemitones / 12.0);
            const double step = std::exp2 (2.0 * spreadSemitones / 12.0 / (numVoices - 1));
            double ratio = lowest;
            for (int k = 0; k < numVoices; ++k, ratio *= step)
                ratios[static_cast<size_t> (k)] = ratio;
            highestRatio = 1.0 / lowest;
        }

        // Nivel de mipmap: el primero (el más rico) cuyo armónico más alto no supera el límite.
        // Ejemplo a 48 kHz (límite 28 kHz): A5 = 440 Hz admite 63 armónicos -> nivel 5 (32 armónicos).
        // Con unison se elige para la copia MÁS AGUDA: ninguna copia produce alias (las graves pierden, como
        // mucho, algo de brillo por encima de ~20 kHz). Solo se recalcula cuando cambia el tono o el detune.
        const double allowedHarmonics = maxHarmonicFrequency / std::max (frequencyHz * highestRatio, 1.0e-3);
        mipLevel = 0;
        while (mipLevel < Wavetable::numLevels - 1 && Wavetable::maxHarmonicsAtLevel (mipLevel) > allowedHarmonics)
            ++mipLevel;

        // Fundido entre mipmaps. Al subir el tono y cruzar un límite, el nivel siguiente tiene la mitad de
        // armónicos: con un cambio seco, la octava más aguda del sonido desaparecería de golpe (un salto de
        // brillo audible con vibrato o pitch bend). Por eso, en la franja de mipBlendOctaves antes del límite
        // se mezcla con el nivel siguiente de 0 % a 100 %: al llegar al límite ya suena solo el nivel siguiente
        // y el cambio es continuo. Se mezcla hacia el nivel MÁS POBRE (nunca hacia uno que se reflejaría),
        // así que no añade aliasing: el precio es un poco menos de brillo dentro de esa franja.
        mipBlend = 0.0f;
        if (mipLevel < Wavetable::numLevels - 1)
        {
            const double headroomOctaves = std::log2 (allowedHarmonics / Wavetable::maxHarmonicsAtLevel (mipLevel));
            mipBlend = static_cast<float> (std::clamp (1.0 - headroomOctaves / mipBlendOctaves, 0.0, 1.0));
        }
    }

    struct Gains
    {
        float left = 0.0f, right = 0.0f, mono = 0.0f;
    };

    static constexpr double audibleLimitHz = 20000.0;
    // Tope de la copia central: con el detune máximo (+1 semitono) la más aguda llega a 0.477·sr (< 1 ciclo/muestra).
    static constexpr double maxFrequencyRatio = 0.45;
    static constexpr double positionSmoothingSeconds = 0.01;
    static constexpr double unisonSmoothingSeconds = 0.005;
    static constexpr double tableCrossfadeSeconds = 0.005;

    const Wavetable* table = nullptr;
    const Wavetable* previousTable = nullptr;
    int crossfadeRemaining = 0;
    int crossfadeLength = 1;

    double sampleRate = 44100.0;
    double baseFrequencyHz = 440.0; // la de la nota
    double frequencyHz = 440.0;     // la que suena en la copia central: nota + afinación + modulación
    double maxHarmonicFrequency = 22050.0;
    double phaseIncrement = 0.0;
    float pitchOffset = 0.0f; // semitonos
    int mipLevel = 0;
    float mipBlend = 0.0f;

    float targetPosition = 0.0f;
    float position = 0.0f;
    float positionModulation = 0.0f;
    float positionSmoothingCoef = 1.0f;

    // Unison. Con los valores por defecto (1 copia, centro) el oscilador suena exactamente como en la Fase 5.
    int numVoices = 1;
    int activeCount = 1;
    float targetDetune = 0.0f, detune = 0.0f, detuneModulation = 0.0f, appliedDetune = 0.0f;
    float stereoWidth = 0.0f;
    float panPosition = 0.0f;
    float unisonSmoothingCoef = 1.0f;
    bool stereoTarget = false;
    bool gainsSettled = true;
    std::array<double, maxUnison> phases {};  // double: con float la fase acumula error audible en notas graves
    std::array<double, maxUnison> ratios = [] {
        std::array<double, maxUnison> r {};
        r.fill (1.0);
        return r;
    }();
    std::array<Gains, maxUnison> gains { { { 1.0f, 1.0f, 1.0f } } };
    std::array<Gains, maxUnison> targetGains { { { 1.0f, 1.0f, 1.0f } } };
};

} // namespace undertow::dsp
