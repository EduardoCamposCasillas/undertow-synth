// Tests de las piezas de DSP y de gestión de voces. No dependen de JUCE: se compilan como un
// ejecutable normal y se lanzan con ctest. Si algo falla, imprime el archivo, la línea y la condición.

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <complex>
#include <cstdio>
#include <numbers>
#include <random>
#include <vector>

#include "dsp/AdsrEnvelope.h"
#include "dsp/Fft.h"
#include "dsp/Filter.h"
#include "dsp/WavetableOscillator.h"
#include "synth/VoiceManager.h"
#include "synth/WavetableBank.h"

namespace
{
int failures = 0;

#define CHECK(condition)                                                                   \
    do                                                                                     \
    {                                                                                      \
        if (! (condition))                                                                 \
        {                                                                                  \
            std::printf ("  FALLO %s:%d: %s\n", __FILE__, __LINE__, #condition);           \
            ++failures;                                                                    \
        }                                                                                  \
    } while (false)

using undertow::dsp::AdsrEnvelope;
using undertow::dsp::AdsrParameters;
using Stage = AdsrEnvelope::Stage;

constexpr std::array<double, 4> sampleRates { 44100.0, 48000.0, 88200.0, 96000.0 };

// Cuenta cuántas muestras permanece la envolvente en 'stage'.
int samplesInStage (AdsrEnvelope& envelope, Stage stage, int limit = 10'000'000)
{
    int count = 0;
    while (envelope.getStage() == stage && count < limit)
    {
        (void) envelope.processSample();
        ++count;
    }
    return count;
}

bool isWithin (int value, double expected, double tolerance)
{
    return std::abs (value - expected) <= tolerance;
}

void testSegmentTimesAreExactAtEverySampleRate()
{
    std::printf ("Tiempos de attack/decay/release a cualquier sample rate\n");

    for (const double sr : sampleRates)
    {
        AdsrEnvelope env;
        env.setSampleRate (sr);
        env.setParameters ({ 0.010f, 0.200f, 0.5f, 0.300f });

        env.noteOn();
        CHECK (isWithin (samplesInStage (env, Stage::attack), 0.010 * sr, 2.0));
        CHECK (isWithin (samplesInStage (env, Stage::decay), 0.200 * sr, 2.0));
        CHECK (std::abs (env.getLevel() - 0.5f) < 1.0e-6f);

        for (int i = 0; i < 1000; ++i)
            (void) env.processSample();
        CHECK (env.getStage() == Stage::sustain);

        env.noteOff();
        CHECK (isWithin (samplesInStage (env, Stage::release), 0.300 * sr, 2.0));
        CHECK (! env.isActive());
    }
}

void testReleaseFromMidAttackKeepsItsDuration()
{
    std::printf ("Release desde mitad del attack dura lo indicado\n");

    AdsrEnvelope env;
    env.setSampleRate (48000.0);
    env.setParameters ({ 1.0f, 0.1f, 0.8f, 0.1f });
    env.noteOn();
    for (int i = 0; i < 4800; ++i)
        (void) env.processSample();

    env.noteOff();
    CHECK (isWithin (samplesInStage (env, Stage::release), 0.1 * 48000.0, 2.0));
}

void testRetriggerIsContinuous()
{
    std::printf ("Redisparar durante el release no produce saltos de nivel\n");

    AdsrEnvelope env;
    env.setSampleRate (48000.0);
    env.setParameters ({ 0.005f, 0.1f, 0.7f, 0.5f });
    env.noteOn();
    for (int i = 0; i < 20'000; ++i)
        (void) env.processSample();
    env.noteOff();
    for (int i = 0; i < 2000; ++i)
        (void) env.processSample();

    float previous = env.getLevel();
    env.noteOn();
    float maxJump = 0.0f;
    for (int i = 0; i < 500; ++i)
    {
        const float current = env.processSample();
        maxJump = std::max (maxJump, std::abs (current - previous));
        previous = current;
    }
    // Un attack de 5 ms a 48 kHz sube como mucho ~1/240 por muestra; un salto sería mucho mayor.
    CHECK (maxJump < 0.01f);
}

void testQuickReleaseTakesFiveMilliseconds()
{
    std::printf ("Fade de robo de voz dura 5 ms\n");

    for (const double sr : sampleRates)
    {
        AdsrEnvelope env;
        env.setSampleRate (sr);
        env.setParameters ({ 0.001f, 0.1f, 1.0f, 2.0f });
        env.noteOn();
        for (int i = 0; i < 1000; ++i)
            (void) env.processSample();

        env.quickRelease();
        CHECK (isWithin (samplesInStage (env, Stage::quickRelease), AdsrEnvelope::quickReleaseSeconds * sr, 2.0));
        CHECK (! env.isActive());
    }
}

// --- VoiceManager ------------------------------------------------------------------------------

using undertow::synth::VoiceManager;
using undertow::dsp::HarmonicSpectrum;
using undertow::dsp::Wavetable;

// Tabla de un solo frame con un seno puro: con ella las voces suenan igual que en la Fase 2,
// lo que permite seguir midiendo la afinación y los clics de forma sencilla.
const Wavetable& sineTable()
{
    static const Wavetable table = [] {
        HarmonicSpectrum sine (2);
        sine[1] = { 0.0, -1.0 };
        return Wavetable::fromSpectra ("Sine", { sine });
    }();
    return table;
}

void prepareManager (VoiceManager& manager, double sampleRate)
{
    manager.prepare (sampleRate);
    manager.setWavetable (&sineTable());
}

void renderSilently (VoiceManager& manager, int numSamples)
{
    std::vector<float> scratch (static_cast<size_t> (numSamples), 0.0f);
    manager.render (scratch.data(), numSamples);
}

int countPlayingNotes (const VoiceManager& manager)
{
    int count = 0;
    for (int i = 0; i < VoiceManager::voicePoolSize; ++i)
        if (manager.getVoice (i).isPlaying())
            ++count;
    return count;
}

bool isNotePlaying (const VoiceManager& manager, int note)
{
    for (int i = 0; i < VoiceManager::voicePoolSize; ++i)
        if (manager.getVoice (i).isPlaying() && manager.getVoice (i).getNote() == note)
            return true;
    return false;
}

const undertow::synth::Voice* findVoice (const VoiceManager& manager, int note)
{
    for (int i = 0; i < VoiceManager::voicePoolSize; ++i)
        if (manager.getVoice (i).isPlaying() && manager.getVoice (i).getNote() == note)
            return &manager.getVoice (i);
    return nullptr;
}

void testPolyphonyLimit()
{
    std::printf ("Límite de polifonía y liberación de voces robadas\n");

    VoiceManager manager;
    prepareManager (manager, 48000.0);
    manager.setPolyphony (4);

    for (int note = 60; note < 66; ++note)
        manager.noteOn (note, 1.0f);

    CHECK (countPlayingNotes (manager) == 4);
    CHECK (! isNotePlaying (manager, 60)); // las dos más antiguas se robaron
    CHECK (! isNotePlaying (manager, 61));
    CHECK (isNotePlaying (manager, 65));

    renderSilently (manager, 480); // 10 ms: los fades de robo ya terminaron
    CHECK (manager.getNumActiveVoices() == 4);
}

void testStealingPrefersReleasedNotes()
{
    std::printf ("Se roba antes una nota soltada que una pulsada\n");

    VoiceManager manager;
    prepareManager (manager, 48000.0);
    manager.setEnvelopeParameters ({ 0.005f, 0.1f, 0.8f, 2.0f });
    manager.setPolyphony (2);

    manager.noteOn (60, 1.0f);
    manager.noteOn (62, 1.0f);
    manager.noteOff (62); // la más nueva, pero soltada: debe ser la víctima
    manager.noteOn (64, 1.0f);

    CHECK (isNotePlaying (manager, 60));
    CHECK (! isNotePlaying (manager, 62));
    CHECK (isNotePlaying (manager, 64));
}

void testRepeatedNoteReusesVoice()
{
    std::printf ("Repetir una nota reutiliza su voz\n");

    VoiceManager manager;
    prepareManager (manager, 48000.0);
    manager.setEnvelopeParameters ({ 0.005f, 0.1f, 0.8f, 2.0f });

    for (int i = 0; i < 5; ++i)
    {
        manager.noteOn (60, 1.0f);
        renderSilently (manager, 100);
        manager.noteOff (60);
        renderSilently (manager, 100);
    }
    CHECK (manager.getNumActiveVoices() == 1);
}

void testSustainPedal()
{
    std::printf ("Pedal de sustain\n");

    VoiceManager manager;
    prepareManager (manager, 48000.0);

    manager.setSustainPedal (true);
    manager.noteOn (60, 1.0f);
    manager.noteOff (60);
    renderSilently (manager, 1000);

    const auto* voice = findVoice (manager, 60);
    CHECK (voice != nullptr && voice->isSustainedByPedal());
    CHECK (voice != nullptr && voice->getEnvelopeStage() != Stage::release);

    manager.setSustainPedal (false);
    CHECK (voice != nullptr && voice->getEnvelopeStage() == Stage::release);
}

void testVoiceStealingIsClickFree()
{
    std::printf ("Robar una voz no produce clics\n");

    // Con polifonía 1 cada nota nueva roba a la anterior. Buscamos el mayor salto entre muestras
    // consecutivas. Un seno de 65 Hz a 48 kHz y amplitud 0.25 cambia como mucho ~0.002 por muestra;
    // un corte seco produciría un salto de hasta 0.25.
    VoiceManager manager;
    prepareManager (manager, 48000.0);
    manager.setEnvelopeParameters ({ 0.005f, 0.1f, 1.0f, 0.1f });
    manager.setPolyphony (1);
    manager.setVelocitySensitivity (0.0f);

    constexpr int blockSize = 64;
    std::vector<float> output;

    for (int step = 0; step < 20; ++step)
    {
        manager.noteOn (step % 2 == 0 ? 36 : 38, 1.0f);

        // Duraciones irregulares para que el robo caiga en fases distintas del seno.
        const int blocks = 5 + (step * 7) % 11;
        for (int b = 0; b < blocks; ++b)
        {
            std::array<float, blockSize> block {};
            manager.render (block.data(), blockSize);
            output.insert (output.end(), block.begin(), block.end());
        }
    }

    float maxJump = 0.0f;
    for (size_t i = 1; i < output.size(); ++i)
        maxJump = std::max (maxJump, std::abs (output[i] - output[i - 1]));

    std::printf ("  salto máximo entre muestras: %.5f\n", static_cast<double> (maxJump));
    CHECK (maxJump < 0.01f);
}
// Mide la frecuencia real de una señal contando los cruces por cero ascendentes.
// El instante exacto de cada cruce se interpola linealmente entre las dos muestras que lo rodean;
// así la medida es mucho más precisa que "una muestra" y detecta errores de centésimas de cent.
double measureFrequency (const std::vector<float>& signal, double sampleRate)
{
    double firstCrossing = -1.0;
    double lastCrossing = -1.0;
    int numCrossings = 0;

    for (size_t i = 1; i < signal.size(); ++i)
    {
        const double a = signal[i - 1];
        const double b = signal[i];
        if (a < 0.0 && b >= 0.0)
        {
            const double crossing = static_cast<double> (i - 1) + a / (a - b);
            if (numCrossings == 0)
                firstCrossing = crossing;
            lastCrossing = crossing;
            ++numCrossings;
        }
    }

    if (numCrossings < 2)
        return 0.0;

    return (numCrossings - 1) * sampleRate / (lastCrossing - firstCrossing);
}

double centsBetween (double measured, double expected)
{
    return 1200.0 * std::log2 (measured / expected);
}

void testReferencePitches()
{
    std::printf ("Tabla de afinación de midiNoteToHz\n");

    // Valores de referencia de la afinación estándar (La = 440 Hz, temperamento igual).
    struct Reference { int note; double hz; };
    constexpr std::array<Reference, 6> references { {
        { 21, 27.5 },         // A0, la tecla más grave del piano
        { 36, 65.406391 },    // C2
        { 60, 261.625565 },   // Do central (C4 en notación científica, "C5" en FL Studio)
        { 69, 440.0 },        // A4, el diapasón
        { 81, 880.0 },
        { 108, 4186.009045 }, // C8, la tecla más aguda del piano
    } };

    for (const auto& ref : references)
        CHECK (std::abs (centsBetween (undertow::dsp::midiNoteToHz (ref.note), ref.hz)) < 0.001);
}

void testRenderedPitchMatchesNote()
{
    std::printf ("Frecuencia medida en el audio renderizado (todas las teclas del piano, 4 sample rates)\n");

    double worstCents = 0.0;

    for (const double sr : sampleRates)
    {
        for (int note = 21; note <= 108; ++note)
        {
            VoiceManager manager;
            prepareManager (manager, sr);
            manager.setEnvelopeParameters ({ 0.001f, 0.001f, 1.0f, 0.1f });
            manager.noteOn (note, 1.0f);

            // Se descartan los primeros 10 ms (attack) y se mide durante 1 segundo.
            renderSilently (manager, static_cast<int> (0.01 * sr));
            std::vector<float> signal (static_cast<size_t> (sr), 0.0f);
            manager.render (signal.data(), static_cast<int> (signal.size()));

            const double expected = undertow::dsp::midiNoteToHz (note);
            const double cents = centsBetween (measureFrequency (signal, sr), expected);
            worstCents = std::max (worstCents, std::abs (cents));
            CHECK (std::abs (cents) < 0.05); // el oído más entrenado distingue ~2-5 cents
        }
    }

    std::printf ("  peor desviación: %.5f cents\n", worstCents);

    // Tabla legible de algunas notas a 48 kHz.
    std::printf ("  nota MIDI | nombre FL | esperado (Hz) | medido (Hz)\n");
    constexpr std::array<std::pair<int, const char*>, 6> samples { {
        { 24, "C2" }, { 33, "A2" }, { 57, "A4" }, { 60, "C5" }, { 69, "A5" }, { 93, "A7" } } };
    for (const auto& [note, flName] : samples)
    {
        VoiceManager manager;
        prepareManager (manager, 48000.0);
        manager.setEnvelopeParameters ({ 0.001f, 0.001f, 1.0f, 0.1f });
        manager.noteOn (note, 1.0f);
        renderSilently (manager, 480);
        std::vector<float> signal (48000, 0.0f);
        manager.render (signal.data(), static_cast<int> (signal.size()));
        std::printf ("  %9d | %9s | %13.4f | %11.4f\n", note, flName, undertow::dsp::midiNoteToHz (note),
                     measureFrequency (signal, 48000.0));
    }
}

// --- FFT y wavetables --------------------------------------------------------------------------

using Complex = std::complex<double>;
using undertow::dsp::WavetableOscillator;
using undertow::synth::WavetableBank;

constexpr double pi = std::numbers::pi;

const WavetableBank& factoryBank()
{
    static const WavetableBank bank;
    return bank;
}

void testFftRoundTrip()
{
    std::printf ("FFT: una senoidal cae en su bin y la inversa recupera la señal\n");

    constexpr size_t n = 1024;
    std::vector<Complex> data (n);
    for (size_t i = 0; i < n; ++i)
    {
        const auto x = static_cast<double> (i);
        data[i] = std::cos (2.0 * pi * 5.0 * x / n) + 0.25 * std::sin (0.37 * x * x);
    }
    const auto original = data;

    undertow::dsp::fft (data, false);
    // cos(2π·5·n/N) aporta N/2 al bin 5; el resto de la señal es "ruido" de fondo, mucho menor.
    CHECK (std::abs (data[5]) > 0.4 * n);

    undertow::dsp::fft (data, true);
    double maxError = 0.0;
    for (size_t i = 0; i < n; ++i)
        maxError = std::max (maxError, std::abs (data[i] - original[i]));
    CHECK (maxError < 1.0e-12);
}

// Magnitud de cada armónico de un ciclo de la tabla (vía FFT del propio ciclo).
std::vector<double> cycleHarmonics (const float* cycle, int size)
{
    std::vector<Complex> bins (cycle, cycle + size);
    undertow::dsp::fft (bins, false);
    std::vector<double> magnitudes (static_cast<size_t> (size / 2));
    for (size_t h = 0; h < magnitudes.size(); ++h)
        magnitudes[h] = 2.0 * std::abs (bins[h]) / size;
    return magnitudes;
}

void testMipmapsRemoveHarmonics()
{
    std::printf ("Cada mipmap tiene exactamente los armónicos de su nivel\n");

    const auto& table = sineTable();
    const float* frame = table.getFrame (0, 0);
    double maxError = 0.0;
    const int size = Wavetable::levelSize (0);
    for (int i = 0; i < size; ++i)
        maxError = std::max (maxError, std::abs (frame[i] - std::sin (2.0 * pi * i / size)));
    CHECK (maxError < 1.0e-6);

    // Las muestras de guarda son copias del otro extremo del ciclo.
    CHECK (frame[-1] == frame[size - 1]);
    CHECK (frame[size] == frame[0]);
    CHECK (frame[size + 1] == frame[1]);

    const auto& basic = factoryBank().get (0);
    constexpr int sawFrame = 2;
    for (int level = 0; level < Wavetable::numLevels; ++level)
    {
        const auto harmonics = cycleHarmonics (basic.getFrame (level, sawFrame), Wavetable::levelSize (level));
        const auto limit = static_cast<size_t> (Wavetable::maxHarmonicsAtLevel (level));

        double above = 0.0;
        for (size_t h = limit + 1; h < harmonics.size(); ++h)
            above = std::max (above, harmonics[h]);

        CHECK (above < 1.0e-5); // nada por encima del límite...
        CHECK (harmonics[limit] > 0.5 * harmonics[1] / static_cast<double> (limit)); // ...y el último sí está (≈ 1/h)
    }
}

void testMipLevelSelection()
{
    std::printf ("Elección del mipmap según la nota\n");

    WavetableOscillator osc;
    osc.setSampleRate (48000.0);
    osc.setFrequency (440.0); // límite 28 kHz -> 63 armónicos permitidos -> nivel de 32
    CHECK (osc.getMipLevel() == 5);
    osc.setFrequency (20.0); // 1400 armónicos permitidos -> nivel 0 (1024)
    CHECK (osc.getMipLevel() == 0);
    osc.setFrequency (15000.0);
    CHECK (osc.getMipLevel() == Wavetable::numLevels - 1);

    osc.setSampleRate (96000.0); // aquí el límite es Nyquist (48 kHz), sin reflejos
    osc.setFrequency (440.0);    // 109 armónicos permitidos -> nivel de 64
    CHECK (osc.getMipLevel() == 4);
}

std::vector<float> renderOscillator (const Wavetable& table, float position, double sampleRate, double hz,
                                     size_t numSamples)
{
    WavetableOscillator osc;
    osc.setSampleRate (sampleRate);
    osc.setWavetable (&table);
    osc.setPosition (position);
    osc.setFrequency (hz);
    osc.reset();

    std::vector<float> signal (numSamples);
    for (auto& sample : signal)
        sample = osc.processSample();
    return signal;
}

// Espectro con ventana Blackman-Harris de 4 términos: sus lóbulos laterales están a -92 dB,
// así que un armónico fuerte no "tapa" un alias débil en los bins vecinos.
std::vector<double> magnitudeSpectrum (const std::vector<float>& signal)
{
    const size_t n = signal.size();
    std::vector<Complex> bins (n);
    for (size_t i = 0; i < n; ++i)
    {
        const double x = 2.0 * pi * static_cast<double> (i) / static_cast<double> (n - 1);
        const double window = 0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2 * x) - 0.01168 * std::cos (3 * x);
        bins[i] = signal[i] * window;
    }
    undertow::dsp::fft (bins, false);

    std::vector<double> magnitudes (n / 2);
    for (size_t k = 0; k < magnitudes.size(); ++k)
        magnitudes[k] = std::abs (bins[k]);
    return magnitudes;
}

// Nivel de un componente sumando la energía de su lóbulo principal: no depende de si cae justo en un bin.
double componentLevel (const std::vector<double>& spectrum, double hz, double binHz)
{
    const auto centre = static_cast<long> (std::lround (hz / binHz));
    double energy = 0.0;
    for (long k = centre - 5; k <= centre + 5; ++k)
        if (k >= 0 && k < static_cast<long> (spectrum.size()))
            energy += spectrum[static_cast<size_t> (k)] * spectrum[static_cast<size_t> (k)];
    return std::sqrt (energy);
}

// Mayor componente NO armónico por debajo de 20 kHz, en dB respecto al componente más fuerte
// (no a la fundamental: en un sync 8x la fundamental casi no existe).
// Todo lo que no cae en un múltiplo de f0 es aliasing (o ruido de interpolación).
double worstAliasDb (const std::vector<double>& spectrum, double f0, double binHz)
{
    const double strongest = *std::max_element (spectrum.begin(), spectrum.end());
    double worst = 0.0;

    for (size_t k = 1; k < spectrum.size(); ++k)
    {
        const double hz = static_cast<double> (k) * binHz;
        if (hz > 20000.0)
            break;
        const double nearestHarmonic = std::round (hz / f0) * f0;
        if (std::abs (hz - nearestHarmonic) < 6.0 * binHz)
            continue;
        worst = std::max (worst, spectrum[k]);
    }

    return 20.0 * std::log10 (std::max (worst, 1.0e-12) / strongest);
}

void testNoAudibleAliasing()
{
    std::printf ("Aliasing medido por debajo de 20 kHz (sierra, sync y pulso fino, 4 sample rates)\n");

    struct Case { int table; float position; };
    // Sierra, sync a 7.65x (razón no entera: la más "sucia") y pulso del 3 % (espectro casi plano, el peor caso).
    constexpr std::array<Case, 3> cases { { { 0, 2.0f / 3.0f }, { 3, 0.95f }, { 1, 1.0f } } };
    // Notas MIDI de 36 a 115 (C3 a G9 en FL), incluidas algunas junto a un cambio de mipmap.
    constexpr std::array<int, 11> notes { 36, 48, 60, 72, 79, 84, 91, 96, 103, 108, 115 };

    for (const double sr : sampleRates)
    {
        const size_t n = sr > 50000.0 ? 131072 : 65536;
        const double binHz = sr / static_cast<double> (n);
        double worstOurs = -300.0;
        double worstNaive = -300.0;

        for (const auto& c : cases)
        {
            for (const int note : notes)
            {
                const double f0 = undertow::dsp::midiNoteToHz (note);
                const auto signal = renderOscillator (factoryBank().get (c.table), c.position, sr, f0, n);
                const double db = worstAliasDb (magnitudeSpectrum (signal), f0, binHz);
                worstOurs = std::max (worstOurs, db);
                // -85 dB: muy por debajo de lo audible (y además lo tapan los armónicos de la propia nota).
                CHECK (db < -85.0);
            }
        }

        // Referencia: una sierra "ingenua" (2·fase - 1, sin límite de armónicos), para comparar.
        for (const int note : notes)
        {
            const double f0 = undertow::dsp::midiNoteToHz (note);
            std::vector<float> naive (n);
            double phase = 0.0;
            for (auto& sample : naive)
            {
                sample = static_cast<float> (2.0 * phase - 1.0);
                phase += f0 / sr;
                phase -= std::floor (phase);
            }
            worstNaive = std::max (worstNaive, worstAliasDb (magnitudeSpectrum (naive), f0, binHz));
        }

        std::printf ("  %5.1f kHz: peor alias %7.1f dB   (sierra ingenua: %6.1f dB)\n", sr / 1000.0, worstOurs, worstNaive);
    }
}

void testHighHarmonicsArePreserved()
{
    std::printf ("Los mipmaps no oscurecen de más: el último armónico garantizado conserva su nivel\n");

    // Sierra: el armónico h debe estar a 1/h de la fundamental. Se comprueba el más alto que el
    // mipmap garantiza siempre (la mitad del límite, por el paso de una octava) y que sea audible.
    double worstErrorDb = 0.0;

    for (const double sr : sampleRates)
    {
        const size_t n = sr > 50000.0 ? 131072 : 65536;
        const double binHz = sr / static_cast<double> (n);
        const double limit = sr / 2.0 >= 40000.0 ? sr / 2.0 : std::max (sr / 2.0, sr - 20000.0);

        for (const int note : { 36, 48, 60, 72, 84, 96 })
        {
            const double f0 = undertow::dsp::midiNoteToHz (note);
            const auto spectrum = magnitudeSpectrum (renderOscillator (factoryBank().get (0), 2.0f / 3.0f, sr, f0, n));

            const double h = std::floor (std::min (limit / 2.0, 19000.0) / f0);
            const double measured = componentLevel (spectrum, h * f0, binHz) / componentLevel (spectrum, f0, binHz);
            const double errorDb = 20.0 * std::log10 (measured * h);
            worstErrorDb = std::max (worstErrorDb, std::abs (errorDb));
            CHECK (std::abs (errorDb) < 1.0);
        }
    }
    std::printf ("  mayor desviación: %.2f dB\n", worstErrorDb);
}

float maxSampleJump (const std::vector<float>& signal)
{
    float jump = 0.0f;
    for (size_t i = 1; i < signal.size(); ++i)
        jump = std::max (jump, std::abs (signal[i] - signal[i - 1]));
    return jump;
}

void testPositionChangeIsSmooth()
{
    std::printf ("Mover Position de golpe no produce saltos (morph suavizado)\n");

    // Seno a 65 Hz: cambia como mucho ~0.0085 por muestra. Saltar de seno a triángulo sin suavizar
    // daría un salto de hasta ~0.2 en una sola muestra.
    WavetableOscillator osc;
    osc.setSampleRate (48000.0);
    osc.setWavetable (&factoryBank().get (0));
    osc.setFrequency (65.0);
    osc.setPosition (0.0f);
    osc.reset();

    std::vector<float> signal;
    for (int step = 0; step < 8; ++step)
    {
        osc.setPosition (step % 2 == 0 ? 1.0f / 3.0f : 0.0f); // seno <-> triángulo
        for (int i = 0; i < 1100 + step * 137; ++i)
            signal.push_back (osc.processSample());
    }

    std::printf ("  salto máximo entre muestras: %.5f\n", static_cast<double> (maxSampleJump (signal)));
    CHECK (maxSampleJump (signal) < 0.012f);
}

void testTableSwitchIsClickFree()
{
    std::printf ("Cambiar de tabla con la nota sonando no produce clics (fundido cruzado)\n");

    // Seno y coseno: en fase 0 uno vale 0 y el otro 1. Un cambio seco sería un salto de hasta 1.
    HarmonicSpectrum sine (2), cosine (2);
    sine[1] = { 0.0, -1.0 };
    cosine[1] = { 1.0, 0.0 };
    const auto sineWave = Wavetable::fromSpectra ("sine", { sine });
    const auto cosineWave = Wavetable::fromSpectra ("cosine", { cosine });

    WavetableOscillator osc;
    osc.setSampleRate (48000.0);
    osc.setWavetable (&sineWave);
    osc.setFrequency (65.0);
    osc.reset();

    std::vector<float> signal;
    for (int step = 0; step < 10; ++step)
    {
        osc.setWavetable (step % 2 == 0 ? &cosineWave : &sineWave);
        for (int i = 0; i < 700 + step * 91; ++i)
            signal.push_back (osc.processSample());
    }

    std::printf ("  salto máximo entre muestras: %.5f\n", static_cast<double> (maxSampleJump (signal)));
    CHECK (maxSampleJump (signal) < 0.02f);
}

void testNewNoteStartsAtCurrentPosition()
{
    std::printf ("Una nota nueva empieza directamente en la posición actual (sin barrido heredado)\n");

    const auto& basic = factoryBank().get (0);
    WavetableOscillator used;
    used.setSampleRate (48000.0);
    used.setWavetable (&basic);
    used.setFrequency (220.0);
    used.setPosition (0.0f);
    used.reset();
    for (int i = 0; i < 1000; ++i)
        (void) used.processSample();

    used.setPosition (1.0f);
    used.reset(); // nota nueva
    const auto fresh = renderOscillator (basic, 1.0f, 48000.0, 220.0, 256);

    float maxDifference = 0.0f;
    for (const float expected : fresh)
        maxDifference = std::max (maxDifference, std::abs (used.processSample() - expected));
    CHECK (maxDifference < 1.0e-6f);
}

// --- Filtro ZDF / TPT --------------------------------------------------------------------------

using undertow::dsp::Filter;
using undertow::dsp::FilterParameters;
using undertow::dsp::FilterSlope;
using undertow::dsp::FilterType;

Filter makeFilter (const FilterParameters& parameters, double sampleRate)
{
    Filter filter;
    filter.setSampleRate (sampleRate);
    filter.setParameters (parameters);
    filter.reset();
    return filter;
}

// Ganancia de un filtro a una frecuencia: se le pasa un seno, se espera a que el transitorio muera y se
// mide la amplitud de la salida a esa frecuencia (correlación con ventana de Hann, inmune a la fase).
double measuredGain (const FilterParameters& parameters, double hz, double sampleRate)
{
    auto filter = makeFilter (parameters, sampleRate);
    const double step = 2.0 * pi * hz / sampleRate;
    const int settle = static_cast<int> (1.0 * sampleRate);
    const int length = static_cast<int> (0.5 * sampleRate);

    for (int i = 0; i < settle; ++i)
        (void) filter.processSample (static_cast<float> (std::sin (step * i)));

    Complex sum;
    double windowSum = 0.0;
    for (int i = 0; i < length; ++i)
    {
        const int n = settle + i;
        const double y = filter.processSample (static_cast<float> (std::sin (step * n)));
        const double w = 0.5 - 0.5 * std::cos (2.0 * pi * i / (length - 1));
        sum += y * w * std::polar (1.0, -step * n);
        windowSum += w;
    }
    return 2.0 * std::abs (sum) / windowSum;
}

double toDb (double gain) { return 20.0 * std::log10 (std::max (gain, 1.0e-12)); }

void testFilterMatchesTheory()
{
    std::printf ("El filtro medido coincide con la respuesta teórica (la curva de la GUI)\n");

    double worstErrorDb = 0.0;
    for (const double sr : sampleRates)
        for (const auto type : { FilterType::lowPass, FilterType::highPass, FilterType::bandPass })
            for (const auto slope : { FilterSlope::db12, FilterSlope::db24 })
                for (const float resonance : { 0.0f, 0.5f, 1.0f })
                    for (const float cutoff : { 200.0f, 2000.0f, 12000.0f })
                        for (const double ratio : { 0.25, 0.7, 1.0, 1.5, 4.0 })
                        {
                            const double hz = cutoff * ratio;
                            if (hz > 0.45 * sr)
                                continue;

                            const FilterParameters p { type, slope, cutoff, resonance, 0.0f };
                            const double expectedDb = toDb (undertow::dsp::filterMagnitude (p, hz, sr));
                            if (expectedDb < -90.0)
                                continue; // por debajo de eso manda la precisión de float, no el filtro

                            const double errorDb = std::abs (toDb (measuredGain (p, hz, sr)) - expectedDb);
                            worstErrorDb = std::max (worstErrorDb, errorDb);
                        }

    std::printf ("  peor diferencia: %.4f dB\n", worstErrorDb);
    CHECK (worstErrorDb < 0.05);
}

void testFilterCutoffAndSlopes()
{
    std::printf ("Cutoff a -3 dB y pendientes de 12 y 24 dB/octava\n");

    for (const double sr : sampleRates)
    {
        // Sin resonancia, en el cutoff exacto: -3 dB (Butterworth) en 12 y en 24 dB. Gracias al prewarp
        // de tan() se cumple también con cutoffs altos, donde un filtro "ingenuo" se desafina.
        for (const auto slope : { FilterSlope::db12, FilterSlope::db24 })
            for (const float cutoff : { 100.0f, 1000.0f, 15000.0f })
            {
                const double db = toDb (measuredGain ({ FilterType::lowPass, slope, cutoff, 0.0f, 0.0f }, cutoff, sr));
                CHECK (std::abs (db + 3.01) < 0.05);
            }

        // Tres octavas por encima del cutoff: ~ -36 dB (12 dB/oct) y ~ -72 dB (24 dB/oct).
        const double lp12 = toDb (measuredGain ({ FilterType::lowPass, FilterSlope::db12, 500.0f, 0.0f, 0.0f }, 4000.0, sr));
        const double lp24 = toDb (measuredGain ({ FilterType::lowPass, FilterSlope::db24, 500.0f, 0.0f, 0.0f }, 4000.0, sr));
        const double hp12 = toDb (measuredGain ({ FilterType::highPass, FilterSlope::db12, 4000.0f, 0.0f, 0.0f }, 500.0, sr));
        const double hp24 = toDb (measuredGain ({ FilterType::highPass, FilterSlope::db24, 4000.0f, 0.0f, 0.0f }, 500.0, sr));
        if (sr == 48000.0)
            std::printf ("  48 kHz, 3 octavas: LP12 %.1f dB, LP24 %.1f dB, HP12 %.1f dB, HP24 %.1f dB\n", lp12, lp24, hp12, hp24);
        CHECK (std::abs (lp12 + 36.0) < 1.5);
        CHECK (std::abs (lp24 + 72.0) < 3.0);
        CHECK (std::abs (hp12 + 36.0) < 1.5);
        CHECK (std::abs (hp24 + 72.0) < 3.0);
    }
}

void testResonance()
{
    std::printf ("La resonancia crea un pico en el cutoff; el band-pass mantiene su pico en 0 dB\n");

    for (const auto slope : { FilterSlope::db12, FilterSlope::db24 })
    {
        const double flat = toDb (measuredGain ({ FilterType::lowPass, slope, 1000.0f, 0.0f, 0.0f }, 1000.0, 48000.0));
        const double peak = toDb (measuredGain ({ FilterType::lowPass, slope, 1000.0f, 1.0f, 0.0f }, 1000.0, 48000.0));
        const double bass = toDb (measuredGain ({ FilterType::lowPass, slope, 1000.0f, 1.0f, 0.0f }, 50.0, 48000.0));
        std::printf ("  %s: en el cutoff %.1f dB -> %.1f dB con resonancia 100 %%, graves %.1f dB\n",
                     slope == FilterSlope::db12 ? "12 dB" : "24 dB", flat, peak, bass);
        CHECK (peak > 12.0);
        CHECK (peak < 18.0);
        CHECK (std::abs (bass + 6.15) < 0.3); // compensación: los graves bajan ~6 dB, como en un ladder

        for (const float resonance : { 0.0f, 0.5f, 1.0f })
        {
            const double bandPeak = toDb (measuredGain ({ FilterType::bandPass, slope, 1000.0f, resonance, 0.0f }, 1000.0, 48000.0));
            CHECK (std::abs (bandPeak) < 0.05);
        }
    }
}

void testFilterModulationIsStable()
{
    std::printf ("Estable con resonancia máxima y el cutoff saltando en cada muestra\n");

    std::mt19937 random (1234);
    std::uniform_real_distribution<float> unit (0.0f, 1.0f);

    for (const double sr : sampleRates)
        for (const auto slope : { FilterSlope::db12, FilterSlope::db24 })
        {
            auto filter = makeFilter ({ FilterType::lowPass, slope, 1000.0f, 1.0f, 1.0f }, sr);
            float peak = 0.0f;
            bool finite = true;

            for (int i = 0; i < static_cast<int> (sr); ++i)
            {
                // Cutoff aleatorio entre 20 Hz y 20 kHz: el peor caso posible de modulación.
                FilterParameters p { static_cast<FilterType> (i / 4000 % 3), slope,
                                     20.0f * std::pow (1000.0f, unit (random)), 1.0f, unit (random) };
                filter.setParameters (p);
                const float input = (i / 50) % 2 == 0 ? 1.0f : -1.0f; // cuadrada llena de armónicos
                const float y = filter.processSample (input);
                finite = finite && std::isfinite (y);
                peak = std::max (peak, std::abs (y));
            }
            CHECK (finite);
            CHECK (peak < 30.0f);
        }

    // La etapa SVF sola, con coeficientes al azar sin suavizar: el TPT sigue siendo estable.
    undertow::dsp::SvfStage stage;
    float peak = 0.0f;
    for (int i = 0; i < 200'000; ++i)
    {
        const auto c = undertow::dsp::SvfStage::Coefficients::make (std::tan (1.5f * unit (random)), 0.05f + 2.0f * unit (random));
        const auto out = stage.process (unit (random) * 2.0f - 1.0f, c);
        peak = std::max ({ peak, std::abs (out.lowPass), std::abs (out.highPass) });
    }
    CHECK (std::isfinite (peak));
    CHECK (peak < 100.0f);
}

void testFilterChangesAreClickFree()
{
    std::printf ("Cambiar tipo, pendiente o cutoff con la nota sonando no produce clics\n");

    // Un seno de 150 Hz avanza como mucho ~0.02 por muestra a 48 kHz; un clic sería un salto mucho mayor.
    auto filter = makeFilter ({ FilterType::lowPass, FilterSlope::db12, 1000.0f, 0.3f, 0.0f }, 48000.0);
    const double step = 2.0 * pi * 150.0 / 48000.0;

    std::vector<float> signal;
    const std::array<FilterParameters, 6> changes { {
        { FilterType::highPass, FilterSlope::db12, 1000.0f, 0.3f, 0.0f },
        { FilterType::highPass, FilterSlope::db24, 1000.0f, 0.3f, 0.0f },
        // El seno NO cae en el pico de resonancia: allí pasar de band-pass (pico en 0 dB) a low-pass
        // (pico en +15 dB) es un cambio de volumen real (suavizado), no un clic, y el salto medido crecería.
        { FilterType::bandPass, FilterSlope::db24, 600.0f, 0.8f, 0.0f },
        { FilterType::lowPass, FilterSlope::db24, 20000.0f, 0.0f, 0.0f },
        { FilterType::lowPass, FilterSlope::db12, 40.0f, 1.0f, 1.0f },
        { FilterType::lowPass, FilterSlope::db12, 5000.0f, 0.0f, 0.0f },
    } };

    int n = 0;
    for (const auto& change : changes)
    {
        for (int i = 0; i < 2000; ++i, ++n)
            signal.push_back (filter.processSample (static_cast<float> (std::sin (step * n))));
        filter.setParameters (change);
    }

    std::printf ("  salto máximo entre muestras: %.4f\n", static_cast<double> (maxSampleJump (signal)));
    CHECK (maxSampleJump (signal) < 0.06f);
}

void testDrive()
{
    std::printf ("Drive: 0 %% es lineal; al subirlo aparecen armónicos\n");

    const double sr = 48000.0;
    const double f0 = 187.5; // cae justo en un bin de la FFT de 2^15 muestras a 48 kHz
    const int n = 1 << 15;
    const auto render = [&] (float drive) {
        auto filter = makeFilter ({ FilterType::lowPass, FilterSlope::db12, 20000.0f, 0.0f, drive }, sr);
        std::vector<float> signal (n);
        for (int i = 0; i < n; ++i)
            signal[static_cast<size_t> (i)] = filter.processSample (static_cast<float> (0.8 * std::sin (2.0 * pi * f0 * i / sr)));
        return magnitudeSpectrum (signal);
    };

    const double binHz = sr / n;
    for (const float drive : { 0.0f, 0.3f, 1.0f })
    {
        const auto spectrum = render (drive);
        const double thirdDb = toDb (componentLevel (spectrum, 3.0 * f0, binHz) / componentLevel (spectrum, f0, binHz));
        std::printf ("  drive %3.0f %%: 3.er armónico a %.1f dB\n", static_cast<double> (drive * 100.0f), thirdDb);
        if (drive == 0.0f)
            CHECK (thirdDb < -90.0);
        else
            CHECK (thirdDb > -20.0);
    }
}

// Informativo (sin CHECK): la saturación crea armónicos por encima de Nyquist que se reflejan.
// Se mide para documentar el límite; el oversampling llegará con la distorsión (Fase 8).
void reportDriveAliasing()
{
    std::printf ("Aliasing del drive (sierra, low-pass 24 dB abierto, 48 kHz), informativo\n");

    const double sr = 48000.0;
    const int n = 1 << 15;
    for (const double f0 : { 110.0, 440.0, 1760.0 })
        for (const float drive : { 0.3f, 1.0f })
        {
            const auto saw = renderOscillator (factoryBank().get (0), 2.0f / 3.0f, sr, f0, n);
            auto filter = makeFilter ({ FilterType::lowPass, FilterSlope::db24, 20000.0f, 0.0f, drive }, sr);
            std::vector<float> signal (saw.size());
            for (size_t i = 0; i < saw.size(); ++i)
                signal[i] = filter.processSample (saw[i]);
            std::printf ("  %6.0f Hz, drive %3.0f %%: peor alias %.1f dB\n", f0, static_cast<double> (drive * 100.0f),
                         worstAliasDb (magnitudeSpectrum (signal), f0, sr / n));
        }
}

void testKeyTracking()
{
    std::printf ("Key tracking: el cutoff sigue a la nota\n");

    using undertow::dsp::keyTrackedCutoff;
    CHECK (std::abs (keyTrackedCutoff (1000.0f, 60, 1.0f) - 1000.0f) < 0.01f);
    CHECK (std::abs (keyTrackedCutoff (1000.0f, 72, 1.0f) - 2000.0f) < 0.01f);
    CHECK (std::abs (keyTrackedCutoff (1000.0f, 48, 1.0f) - 500.0f) < 0.01f);
    CHECK (std::abs (keyTrackedCutoff (1000.0f, 84, 0.5f) - 2000.0f) < 0.01f);
    CHECK (std::abs (keyTrackedCutoff (1000.0f, 84, 0.0f) - 1000.0f) < 0.01f);
}

void testFilterToggleInVoiceIsClickFree()
{
    std::printf ("Encender y apagar el filtro con notas sonando no produce clics\n");

    VoiceManager manager;
    prepareManager (manager, 48000.0);
    manager.setEnvelopeParameters ({ 0.001f, 0.1f, 1.0f, 0.1f });
    manager.noteOn (45, 1.0f); // A3 de FL, 110 Hz
    renderSilently (manager, 4800);

    undertow::synth::FilterSettings settings;
    settings.parameters = { FilterType::highPass, FilterSlope::db24, 5000.0f, 0.0f, 0.0f }; // quita casi todo
    std::vector<float> signal (48000, 0.0f);
    for (int block = 0; block < 10; ++block)
    {
        settings.enabled = block % 2 == 1;
        manager.setFilterSettings (settings);
        manager.render (signal.data() + block * 4800, 4800);
    }

    std::printf ("  salto máximo entre muestras: %.5f\n", static_cast<double> (maxSampleJump (signal)));
    CHECK (maxSampleJump (signal) < 0.01f);
}

void testFilterCpuCost()
{
    std::printf ("Coste de CPU: 16 voces de sierra con filtro 24 dB, drive y cutoff en movimiento\n");

    for (const bool enabled : { false, true })
    {
        VoiceManager manager;
        manager.prepare (48000.0);
        manager.setWavetable (&factoryBank().get (0));
        manager.setWavetablePosition (2.0f / 3.0f);
        manager.setPolyphony (16);
        manager.setEnvelopeParameters ({ 0.001f, 0.1f, 1.0f, 0.1f });
        for (int note = 40; note < 56; ++note)
            manager.noteOn (note, 1.0f);

        const int blockSize = 256;
        const int numBlocks = 48000 * 5 / blockSize; // 5 segundos de audio
        std::vector<float> block (blockSize);
        undertow::synth::FilterSettings settings { enabled, { FilterType::lowPass, FilterSlope::db24, 1000.0f, 0.5f, 0.5f }, 0.5f };

        const auto start = std::chrono::steady_clock::now();
        for (int b = 0; b < numBlocks; ++b)
        {
            // El cutoff cambia en cada bloque (como con automatización): obliga a recalcular coeficientes.
            settings.parameters.cutoffHz = 300.0f + 3000.0f * static_cast<float> (b % 100) / 100.0f;
            manager.setFilterSettings (settings);
            std::fill (block.begin(), block.end(), 0.0f);
            manager.render (block.data(), blockSize);
        }
        const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
        std::printf ("  filtro %s: %.1f %% de un núcleo\n", enabled ? "on " : "off", 100.0 * seconds / 5.0);
    }
}
} // namespace


int main()
{
    testSegmentTimesAreExactAtEverySampleRate();
    testReleaseFromMidAttackKeepsItsDuration();
    testRetriggerIsContinuous();
    testQuickReleaseTakesFiveMilliseconds();
    testPolyphonyLimit();
    testStealingPrefersReleasedNotes();
    testRepeatedNoteReusesVoice();
    testSustainPedal();
    testVoiceStealingIsClickFree();
    testReferencePitches();
    testRenderedPitchMatchesNote();

    const auto start = std::chrono::steady_clock::now();
    (void) factoryBank();
    const auto elapsed = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - start).count();
    std::printf ("Banco de wavetables de fábrica construido en %.0f ms\n", elapsed);

    testFftRoundTrip();
    testMipmapsRemoveHarmonics();
    testMipLevelSelection();
    testNoAudibleAliasing();
    testHighHarmonicsArePreserved();
    testPositionChangeIsSmooth();
    testTableSwitchIsClickFree();
    testNewNoteStartsAtCurrentPosition();

    testFilterMatchesTheory();
    testFilterCutoffAndSlopes();
    testResonance();
    testFilterModulationIsStable();
    testFilterChangesAreClickFree();
    testDrive();
    reportDriveAliasing();
    testKeyTracking();
    testFilterToggleInVoiceIsClickFree();
    testFilterCpuCost();

    if (failures == 0)
        std::printf ("\nTodos los tests pasaron.\n");
    else
        std::printf ("\n%d comprobaciones fallaron.\n", failures);

    return failures == 0 ? 0 : 1;
}
