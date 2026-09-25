// Tests de las piezas de DSP y de gestión de voces. No dependen de JUCE: se compilan como un
// ejecutable normal y se lanzan con ctest. Si algo falla, imprime el archivo, la línea y la condición.

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <complex>
#include <cstdio>
#include <numbers>
#include <vector>

#include "dsp/AdsrEnvelope.h"
#include "dsp/Fft.h"
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

    if (failures == 0)
        std::printf ("\nTodos los tests pasaron.\n");
    else
        std::printf ("\n%d comprobaciones fallaron.\n", failures);

    return failures == 0 ? 0 : 1;
}
