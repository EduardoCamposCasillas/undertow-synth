// Tests de las piezas de DSP y de gestión de voces. No dependen de JUCE: se compilan como un
// ejecutable normal y se lanzan con ctest. Si algo falla, imprime el archivo, la línea y la condición.

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <numbers>
#include <random>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "dsp/AdsrEnvelope.h"
#include "dsp/Fft.h"
#include "dsp/Filter.h"
#include "dsp/WavetableOscillator.h"
#include "dsp/Lfo.h"
#include "synth/VoiceManager.h"
#include "synth/WavetableBank.h"

namespace
{
int failures = 0;
bool verbose = false; // --verbose: detalle de las mediciones largas

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

// --- Fase 5: LFOs, envolventes de modulación y matriz ------------------------------------------

using undertow::dsp::Lfo;
using undertow::dsp::LfoShape;
using undertow::synth::LfoMode;
using undertow::synth::LfoSettings;
using undertow::synth::ModDestination;
using undertow::synth::ModSource;
using undertow::synth::ModulationSettings;
using undertow::synth::Transport;

void testLfoShapes()
{
    std::printf ("Formas del LFO en fases clave\n");

    const auto at = [] (LfoShape shape, double phase) { return Lfo::shapeValue (shape, phase, 0.0f); };
    const auto near = [] (float a, float b) { return std::abs (a - b) < 1.0e-6f; };

    CHECK (near (at (LfoShape::sine, 0.0), 0.0f));
    CHECK (near (at (LfoShape::sine, 0.25), 1.0f));
    CHECK (near (at (LfoShape::sine, 0.75), -1.0f));
    CHECK (near (at (LfoShape::triangle, 0.0), 0.0f));
    CHECK (near (at (LfoShape::triangle, 0.25), 1.0f));
    CHECK (near (at (LfoShape::triangle, 0.5), 0.0f));
    CHECK (near (at (LfoShape::triangle, 0.75), -1.0f));
    CHECK (near (at (LfoShape::sawUp, 0.0), -1.0f));
    CHECK (near (at (LfoShape::sawUp, 0.5), 0.0f));
    CHECK (near (at (LfoShape::sawDown, 0.0), 1.0f));
    CHECK (near (at (LfoShape::square, 0.25), 1.0f));
    CHECK (near (at (LfoShape::square, 0.75), -1.0f));
}

void testLfoRateIsExact()
{
    std::printf ("El LFO hace exactamente los ciclos pedidos (3 Hz durante 10 s, 4 sample rates)\n");

    for (const double sr : sampleRates)
    {
        Lfo lfo;
        lfo.reset (1);
        const double increment = 3.0 / sr;
        int wraps = 0;
        double previous = 0.0;
        for (int i = 0; i < static_cast<int> (10.0 * sr); ++i)
        {
            lfo.advance (increment);
            if (lfo.getPhase() < previous)
                ++wraps;
            previous = lfo.getPhase();
        }
        const double cycles = wraps + lfo.getPhase();
        CHECK (std::abs (cycles - 30.0) < 1.0e-6);
    }
}

void testLfoOneShot()
{
    std::printf ("LFO One Shot: un solo ciclo y se queda en el valor final\n");

    Lfo lfo;
    lfo.setShape (LfoShape::sawUp);
    lfo.setOneShot (true);
    lfo.reset (1);
    for (int i = 0; i < 1500; ++i)
        lfo.advance (1.0 / 1000.0);
    CHECK (lfo.getPhase() == 1.0);
    CHECK (lfo.getValue() == 1.0f);
    for (int i = 0; i < 5000; ++i)
        lfo.advance (1.0 / 1000.0);
    CHECK (lfo.getValue() == 1.0f);
}

void testSampleAndHold()
{
    std::printf ("Sample & Hold: constante dentro de cada ciclo, distinto entre ciclos, repetible\n");

    const auto sequence = [] (std::uint32_t seed) {
        Lfo lfo;
        lfo.setShape (LfoShape::sampleAndHold);
        lfo.reset (seed);
        std::vector<float> values;
        bool constantWithinCycle = true;
        for (int cycle = 0; cycle < 200; ++cycle)
        {
            const float first = lfo.getValue();
            for (int i = 0; i < 100; ++i)
            {
                constantWithinCycle = constantWithinCycle && lfo.getValue() == first;
                lfo.advance (0.01);
            }
            values.push_back (first);
        }
        CHECK (constantWithinCycle);
        return values;
    };

    const auto a = sequence (42);
    const auto b = sequence (42);
    const auto c = sequence (43);
    CHECK (a == b);
    CHECK (a != c);

    double mean = 0.0;
    int changes = 0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        CHECK (a[i] >= -1.0f && a[i] <= 1.0f);
        mean += a[i];
        if (i > 0 && a[i] != a[i - 1])
            ++changes;
    }
    mean /= static_cast<double> (a.size());
    std::printf ("  media %.3f, cambios %d de %zu\n", mean, changes, a.size() - 1);
    CHECK (std::abs (mean) < 0.15);
    CHECK (changes == static_cast<int> (a.size()) - 1);
}

void testTempoSyncRates()
{
    std::printf ("LFO sincronizado: la velocidad sale del tempo\n");

    VoiceManager manager;
    prepareManager (manager, 48000.0);
    ModulationSettings settings;
    settings.lfos[0] = { LfoShape::sine, LfoMode::retrigger, true, 2.0f, 5 };   // 1/4
    settings.lfos[1] = { LfoShape::sine, LfoMode::retrigger, true, 2.0f, 11 };  // 1/8 T
    manager.setModulationSettings (settings);
    manager.setTransport ({ 140.0, 0.0, false, false });

    // 140 BPM: una negra = 60/140 s -> 1/4 = 2.333 Hz; un tresillo de corchea dura 1/3 de negra -> 7 Hz.
    CHECK (std::abs (manager.getLfoIncrement (0) * 48000.0 - 140.0 / 60.0) < 1.0e-9);
    CHECK (std::abs (manager.getLfoIncrement (1) * 48000.0 - 7.0) < 1.0e-9);

    settings.lfos[0].tempoSync = false;
    settings.lfos[0].rateHz = 0.5f;
    manager.setModulationSettings (settings);
    CHECK (std::abs (manager.getLfoIncrement (0) * 48000.0 - 0.5) < 1.0e-9);
}

void testFreeModeFollowsSongPosition()
{
    std::printf ("Modo Free + Sync: la fase sale de la posición de la canción\n");

    VoiceManager manager;
    prepareManager (manager, 48000.0);
    ModulationSettings settings;
    settings.lfos[0] = { LfoShape::sine, LfoMode::free, true, 2.0f, 5 }; // 1/4: un ciclo por negra
    settings.lfos[1] = { LfoShape::sine, LfoMode::free, true, 2.0f, 3 }; // 1 bar: un ciclo cada 4 negras
    manager.setModulationSettings (settings);

    manager.setTransport ({ 120.0, 10.25, true, true });
    CHECK (std::abs (manager.getLfoDisplayPhase (0) - 0.25) < 1.0e-9);
    CHECK (std::abs (manager.getLfoDisplayPhase (1) - 0.5625) < 1.0e-9); // 10.25 / 4 = 2.5625

    // 12000 muestras a 48 kHz y 120 BPM = 0.25 s = media negra.
    renderSilently (manager, 12000);
    CHECK (std::abs (manager.getLfoDisplayPhase (0) - 0.75) < 1.0e-9);
}

void testFreeVersusRetrigger()
{
    std::printf ("Free: todas las voces en fase. Retrigger: cada nota empieza su ciclo\n");

    for (const auto mode : { LfoMode::free, LfoMode::retrigger })
    {
        VoiceManager manager;
        prepareManager (manager, 48000.0);
        manager.setEnvelopeParameters ({ 0.001f, 0.1f, 1.0f, 0.1f });
        ModulationSettings settings;
        settings.lfos[0] = { LfoShape::sine, mode, false, 3.0f, 5 };
        manager.setModulationSettings (settings);

        manager.noteOn (60, 1.0f);
        renderSilently (manager, 1000);
        manager.noteOn (64, 1.0f);
        renderSilently (manager, 7);

        const double first = findVoice (manager, 60)->getLfoPhase (0);
        const double second = findVoice (manager, 64)->getLfoPhase (0);
        const double increment = 3.0 / 48000.0;
        if (mode == LfoMode::free)
            CHECK (std::abs (first - second) < 1.0e-12);
        else
        {
            CHECK (std::abs (first - 1007 * increment) < 1.0e-9);
            CHECK (std::abs (second - 7 * increment) < 1.0e-9);
        }
    }
}

void testKeyToCutoffEqualsKeyTracking()
{
    std::printf ("Key -> Cutoff al 50 %% suena igual que el key tracking al 100 %%\n");

    for (const int note : { 36, 60, 84 })
    {
        const auto render = [note] (bool viaMatrix) {
            VoiceManager manager;
            manager.prepare (48000.0);
            manager.setWavetable (&factoryBank().get (0));
            manager.setWavetablePosition (2.0f / 3.0f);
            manager.setEnvelopeParameters ({ 0.001f, 0.1f, 1.0f, 0.1f });
            manager.setFilterSettings ({ true, { FilterType::lowPass, FilterSlope::db24, 500.0f, 0.3f, 0.0f },
                                         viaMatrix ? 0.0f : 1.0f });
            ModulationSettings settings;
            if (viaMatrix)
                settings.slots[0] = { ModSource::key, ModDestination::filterCutoff, 0.5f };
            manager.setModulationSettings (settings);
            manager.noteOn (note, 1.0f);
            std::vector<float> signal (4800, 0.0f);
            manager.render (signal.data(), static_cast<int> (signal.size()));
            return signal;
        };

        const auto a = render (false);
        const auto b = render (true);
        float maxDifference = 0.0f;
        for (size_t i = 0; i < a.size(); ++i)
            maxDifference = std::max (maxDifference, std::abs (a[i] - b[i]));
        CHECK (maxDifference < 1.0e-4f);
    }
}

void testPitchModulation()
{
    std::printf ("Mod Wheel -> Pitch: +12 semitonos con amount 50 %% y +1 semitono con 1/24\n");

    for (const auto& [amount, semitones] : { std::pair { 0.5f, 12.0 }, std::pair { 1.0f / 24.0f, 1.0 } })
    {
        VoiceManager manager;
        prepareManager (manager, 48000.0);
        manager.setEnvelopeParameters ({ 0.001f, 0.001f, 1.0f, 0.1f });
        ModulationSettings settings;
        settings.slots[0] = { ModSource::modWheel, ModDestination::oscAPitch, amount };
        manager.setModulationSettings (settings);
        manager.setModWheel (1.0f);
        manager.noteOn (57, 1.0f); // A4 de FL, 220 Hz

        renderSilently (manager, 4800);
        std::vector<float> signal (48000, 0.0f);
        manager.render (signal.data(), static_cast<int> (signal.size()));
        const double cents = centsBetween (measureFrequency (signal, 48000.0), 220.0 * std::exp2 (semitones / 12.0));
        std::printf ("  %+.0f semitonos: desviación %.4f cents\n", semitones, cents);
        CHECK (std::abs (cents) < 0.05);
    }
}

void testVibratoRange()
{
    std::printf ("LFO -> Pitch (vibrato): el tono oscila exactamente ±1 semitono\n");

    VoiceManager manager;
    prepareManager (manager, 48000.0);
    manager.setEnvelopeParameters ({ 0.001f, 0.001f, 1.0f, 0.1f });
    ModulationSettings settings;
    settings.lfos[0] = { LfoShape::sine, LfoMode::retrigger, false, 5.0f, 5 };
    settings.slots[0] = { ModSource::lfo1, ModDestination::oscAPitch, 1.0f / 24.0f };
    manager.setModulationSettings (settings);
    manager.noteOn (69, 1.0f); // A5 de FL, 440 Hz

    double lowest = 1.0e9, highest = 0.0;
    for (int block = 0; block < 48000 / 16; ++block)
    {
        renderSilently (manager, 16);
        const double hz = findVoice (manager, 69)->getOscillatorFrequency();
        lowest = std::min (lowest, hz);
        highest = std::max (highest, hz);
    }
    const double upCents = centsBetween (highest, 440.0);
    const double downCents = centsBetween (lowest, 440.0);
    std::printf ("  arriba %+.2f cents, abajo %+.2f cents\n", upCents, downCents);
    CHECK (std::abs (upCents - 100.0) < 1.0);
    CHECK (std::abs (downCents + 100.0) < 1.0);
}

void testEnvelopeSweepsCutoff()
{
    std::printf ("Env 2 -> Cutoff: el filtro se abre 5 octavas en el attack y vuelve en el decay\n");

    VoiceManager manager;
    prepareManager (manager, 48000.0);
    manager.setEnvelopeParameters ({ 0.001f, 0.1f, 1.0f, 0.1f });
    manager.setFilterSettings ({ true, { FilterType::lowPass, FilterSlope::db24, 200.0f, 0.0f, 0.0f }, 0.0f });
    ModulationSettings settings;
    settings.envelope2 = { 0.001f, 0.2f, 0.0f, 0.1f };
    settings.slots[0] = { ModSource::env2, ModDestination::filterCutoff, 0.5f }; // +5 octavas = ×32
    manager.setModulationSettings (settings);
    manager.noteOn (48, 1.0f);

    float peak = 0.0f;
    int peakSample = 0;
    for (int i = 0; i < 480; ++i) // primeros 10 ms
    {
        renderSilently (manager, 1);
        const float cutoff = findVoice (manager, 48)->getFilterCutoffHz();
        if (cutoff > peak)
        {
            peak = cutoff;
            peakSample = i;
        }
    }
    renderSilently (manager, 48000);
    const float settled = findVoice (manager, 48)->getFilterCutoffHz();
    std::printf ("  pico %.0f Hz a los %.1f ms; tras 1 s: %.1f Hz\n", static_cast<double> (peak), peakSample / 48.0,
                 static_cast<double> (settled));
    CHECK (peak > 5800.0f && peak <= 6400.5f);
    CHECK (peakSample < 144); // < 3 ms: el suavizado de 1 ms no frena un attack de 1 ms
    CHECK (std::abs (settled - 200.0f) < 1.0f);
}

void testModulationIsClickFree()
{
    std::printf ("Modulación con saltos (LFO cuadrado, S&H, rutas que cambian) sin clics\n");

    // Seno de 110 Hz a amplitud 0.25: cambia como mucho ~0.0036 por muestra (0.0054 con volumen ×1.5).
    // Sin suavizado, el LFO cuadrado sobre Volume daría saltos de 0.25 en una muestra.
    VoiceManager manager;
    prepareManager (manager, 48000.0);
    manager.setEnvelopeParameters ({ 0.001f, 0.1f, 1.0f, 0.1f });
    ModulationSettings settings;
    settings.lfos[0] = { LfoShape::square, LfoMode::retrigger, false, 6.0f, 5 };
    settings.lfos[1] = { LfoShape::sampleAndHold, LfoMode::free, false, 20.0f, 5 };
    settings.slots[0] = { ModSource::lfo1, ModDestination::volume, -0.5f };
    settings.slots[2] = { ModSource::lfo2, ModDestination::oscAPitch, 0.5f }; // saltos de hasta ±12 semitonos
    manager.setModulationSettings (settings);
    manager.noteOn (45, 1.0f); // A3 de FL, 110 Hz

    std::vector<float> signal (48000 * 2, 0.0f);
    for (int block = 0; block < 40; ++block)
    {
        // Cada 50 ms una ruta aparece y desaparece con el amount al 80 %.
        settings.slots[1] = block % 2 == 0 ? undertow::synth::ModSlot { ModSource::lfo1, ModDestination::volume, 0.8f }
                                           : undertow::synth::ModSlot {};
        manager.setModulationSettings (settings);
        manager.render (signal.data() + block * 2400, 2400);
    }

    std::printf ("  salto máximo entre muestras: %.5f\n", static_cast<double> (maxSampleJump (signal)));
    CHECK (maxSampleJump (signal) < 0.02f);
}

void testMipBlendIsContinuous()
{
    std::printf ("Cruzar un límite de mipmap (vibrato, pitch bend) no cambia el sonido de golpe\n");

    // A 48 kHz el límite es 28 kHz: a 875 Hz caben justo 32 armónicos (frontera entre los niveles de 32 y 16).
    const auto& basic = factoryBank().get (0);
    const double boundary = 28000.0 / 32.0;
    const auto below = renderOscillator (basic, 2.0f / 3.0f, 48000.0, boundary * (1.0 - 1.0e-7), 2048);
    const auto above = renderOscillator (basic, 2.0f / 3.0f, 48000.0, boundary * (1.0 + 1.0e-7), 2048);
    float difference = 0.0f;
    for (size_t i = 0; i < below.size(); ++i)
        difference = std::max (difference, std::abs (below[i] - above[i]));

    // Justo por debajo del límite todavía se lee el nivel de 32 armónicos, pero mezclado al 100 % con el de 16.
    WavetableOscillator osc;
    osc.setSampleRate (48000.0);
    osc.setFrequency (boundary * (1.0 - 1.0e-7));
    std::printf ("  a cada lado del límite: diferencia máxima %.6f (mezcla %.3f)\n", static_cast<double> (difference),
                 static_cast<double> (osc.getMipBlend()));
    CHECK (difference < 1.0e-3f);
    CHECK (osc.getMipBlend() > 0.999f);

    // Fuera de la franja de 1/6 de octava no hay mezcla: el brillo es el de la Fase 3.
    osc.setFrequency (boundary * std::exp2 (-0.2));
    CHECK (osc.getMipBlend() == 0.0f);
}

void testModulationCpuCost()
{
    std::printf ("Coste de CPU: 16 voces, filtro 24 dB y 8 rutas de modulación activas\n");

    VoiceManager manager;
    manager.prepare (48000.0);
    manager.setWavetable (&factoryBank().get (4)); // Vowels, 64 frames
    manager.setPolyphony (16);
    manager.setEnvelopeParameters ({ 0.001f, 0.1f, 1.0f, 0.1f });
    manager.setFilterSettings ({ true, { FilterType::lowPass, FilterSlope::db24, 800.0f, 1.0f, 0.3f }, 0.5f });

    ModulationSettings settings;
    settings.lfos[0] = { LfoShape::sine, LfoMode::retrigger, false, 6.0f, 5 };
    settings.lfos[1] = { LfoShape::sampleAndHold, LfoMode::free, false, 40.0f, 5 };
    settings.slots = { {
        { ModSource::lfo1, ModDestination::filterCutoff, 0.3f },
        { ModSource::lfo2, ModDestination::oscAPitch, 0.02f },
        { ModSource::env2, ModDestination::filterCutoff, 0.5f },
        { ModSource::env3, ModDestination::oscAPosition, 1.0f },
        { ModSource::lfo2, ModDestination::filterResonance, 1.0f },
        { ModSource::velocity, ModDestination::volume, -0.3f },
        { ModSource::modWheel, ModDestination::filterDrive, 1.0f },
        { ModSource::key, ModDestination::filterCutoff, 0.5f },
    } };
    settings.envelope3 = { 2.0f, 1.0f, 0.5f, 0.2f };
    manager.setModulationSettings (settings);
    manager.setModWheel (0.7f);
    for (int note = 40; note < 56; ++note)
        manager.noteOn (note, 0.8f);

    const int blockSize = 256;
    const int numBlocks = 48000 * 5 / blockSize;
    std::vector<float> block (blockSize);
    float peak = 0.0f;
    bool finite = true;

    const auto start = std::chrono::steady_clock::now();
    for (int b = 0; b < numBlocks; ++b)
    {
        std::fill (block.begin(), block.end(), 0.0f);
        manager.render (block.data(), blockSize);
        for (const float s : block)
        {
            finite = finite && std::isfinite (s);
            peak = std::max (peak, std::abs (s));
        }
    }
    const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    std::printf ("  %.1f %% de un núcleo (pico de salida %.2f)\n", 100.0 * seconds / 5.0, static_cast<double> (peak));
    CHECK (finite);
    CHECK (peak < 20.0f);
}

// --- Fase 6: osciladores A y B, sub, ruido y unison ----------------------------------------------

using undertow::synth::SourceSettings;
using undertow::synth::SubShape;

struct StereoSignal
{
    std::vector<float> left, right;
};

StereoSignal renderStereo (VoiceManager& manager, int numSamples)
{
    StereoSignal signal { std::vector<float> (static_cast<size_t> (numSamples), 0.0f),
                          std::vector<float> (static_cast<size_t> (numSamples), 0.0f) };
    manager.render (signal.left.data(), signal.right.data(), numSamples);
    return signal;
}

double rmsOf (const std::vector<float>& signal)
{
    double sum = 0.0;
    for (const float s : signal)
        sum += static_cast<double> (s) * s;
    return std::sqrt (sum / static_cast<double> (signal.size()));
}

// Osc A y B con el seno de prueba (B apagado) y el sub con "Basic Shapes", como en el plugin.
SourceSettings sineSources()
{
    SourceSettings sources;
    sources.oscillators[0].table = &sineTable();
    sources.oscillators[1].table = &sineTable();
    sources.subTable = &factoryBank().get (0);
    return sources;
}

void prepareWithSources (VoiceManager& manager, double sampleRate, const SourceSettings& sources)
{
    manager.prepare (sampleRate);
    manager.setSourceSettings (sources);
    manager.setEnvelopeParameters ({ 0.001f, 0.001f, 1.0f, 0.1f });
    manager.setVelocitySensitivity (0.0f);
}

void testUnisonDetuneSpread()
{
    std::printf ("Unison: copias a distancias iguales en cents (detune 100 %% = ±100 cents, 50 %% = ±25 cents)\n");

    double worstCents = 0.0;
    for (const double sr : sampleRates)
    {
        for (const auto& [voices, detune, spread] : { std::tuple { 3, 1.0f, 100.0 }, std::tuple { 7, 0.5f, 25.0 } })
        {
            auto sources = sineSources();
            sources.oscillators[0].unison = voices;
            sources.oscillators[0].detune = detune;
            VoiceManager manager;
            prepareWithSources (manager, sr, sources);
            manager.noteOn (69, 1.0f); // A5 de FL, 440 Hz
            renderSilently (manager, 64);

            const auto& osc = findVoice (manager, 69)->getOscillator (0);
            CHECK (osc.getNumVoices() == voices);
            for (int k = 0; k < voices; ++k)
            {
                const double expected = -spread + 2.0 * spread * k / (voices - 1);
                const double cents = centsBetween (osc.getVoiceFrequency (k), 440.0);
                worstCents = std::max (worstCents, std::abs (cents - expected));
            }
        }
    }
    std::printf ("  peor error de la posición de cada copia: %.6f cents\n", worstCents);
    CHECK (worstCents < 0.001);

    // En el audio: 3 copias con detune 100 % = tres picos del mismo nivel en 415.3, 440 y 466.2 Hz, y nada entre ellos.
    // Ancho 0: con las copias abiertas, la suma mono de las de los lados pierde nivel (ley de paneo equal power).
    auto sources = sineSources();
    sources.oscillators[0].unison = 3;
    sources.oscillators[0].detune = 1.0f;
    sources.oscillators[0].width = 0.0f;
    VoiceManager manager;
    prepareWithSources (manager, 48000.0, sources);
    manager.noteOn (69, 1.0f);
    renderSilently (manager, 480);
    std::vector<float> signal (65536, 0.0f);
    manager.render (signal.data(), static_cast<int> (signal.size()));

    const auto spectrum = magnitudeSpectrum (signal);
    const double binHz = 48000.0 / 65536.0;
    const double low = componentLevel (spectrum, 440.0 * std::exp2 (-1.0 / 12.0), binHz);
    const double centre = componentLevel (spectrum, 440.0, binHz);
    const double high = componentLevel (spectrum, 440.0 * std::exp2 (1.0 / 12.0), binHz);
    const double between = componentLevel (spectrum, 427.0, binHz);
    std::printf ("  picos: %.2f / %.2f / %.2f dB; entre ellos: %.1f dB\n", toDb (low / centre), 0.0, toDb (high / centre),
                 toDb (between / centre));
    CHECK (std::abs (toDb (low / centre)) < 0.1);
    CHECK (std::abs (toDb (high / centre)) < 0.1);
    CHECK (toDb (between / centre) < -60.0);
}

void testUnisonKeepsLoudness()
{
    std::printf ("Unison: el volumen percibido (RMS) no cambia al subir el número de copias\n");

    const auto measure = [] (int voices) {
        auto sources = sineSources();
        sources.oscillators[0].unison = voices;
        sources.oscillators[0].detune = 1.0f;
        sources.oscillators[0].width = 0.0f;
        VoiceManager manager;
        prepareWithSources (manager, 48000.0, sources);
        manager.noteOn (57, 1.0f); // 220 Hz
        renderSilently (manager, 480);
        std::vector<float> signal (48000 * 4, 0.0f);
        manager.render (signal.data(), static_cast<int> (signal.size()));
        return rmsOf (signal);
    };

    const double reference = measure (1);
    for (const int voices : { 2, 4, 8, 16 })
    {
        const double db = toDb (measure (voices) / reference);
        std::printf ("  %2d copias: %+.2f dB\n", voices, db);
        CHECK (std::abs (db) < 1.5);
    }
}

void testStereoWidthAndPan()
{
    std::printf ("Estéreo: sin unison ni paneo L = R (= salida mono); ancho y paneo equal power\n");

    const auto render = [] (const SourceSettings& sources, bool stereo) {
        VoiceManager manager;
        prepareWithSources (manager, 48000.0, sources);
        manager.noteOn (57, 1.0f);
        manager.noteOn (64, 1.0f);
        renderSilently (manager, 480);
        if (stereo)
            return renderStereo (manager, 48000);
        StereoSignal mono { std::vector<float> (48000, 0.0f), {} };
        manager.render (mono.left.data(), 48000);
        return mono;
    };
    const auto maxDifference = [] (const std::vector<float>& a, const std::vector<float>& b) {
        float difference = 0.0f;
        for (size_t i = 0; i < a.size(); ++i)
            difference = std::max (difference, std::abs (a[i] - b[i]));
        return difference;
    };

    // Por defecto (Osc A sin unison, centrado): los dos canales son idénticos e iguales a la salida mono.
    const auto centred = render (sineSources(), true);
    CHECK (maxDifference (centred.left, centred.right) == 0.0f);
    CHECK (maxDifference (centred.left, render (sineSources(), false).left) < 1.0e-7f);

    // Unison con ancho 0: sigue siendo mono.
    auto sources = sineSources();
    sources.oscillators[0].unison = 8;
    sources.oscillators[0].width = 0.0f;
    const auto narrow = render (sources, true);
    CHECK (maxDifference (narrow.left, narrow.right) == 0.0f);

    // Ancho 100 %: los canales se separan (correlación baja) pero quedan equilibrados.
    sources.oscillators[0].width = 1.0f;
    const auto wide = render (sources, true);
    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (size_t i = 0; i < wide.left.size(); ++i)
    {
        lr += static_cast<double> (wide.left[i]) * wide.right[i];
        ll += static_cast<double> (wide.left[i]) * wide.left[i];
        rr += static_cast<double> (wide.right[i]) * wide.right[i];
    }
    const double correlation = lr / std::sqrt (ll * rr);
    const double balanceDb = toDb (rmsOf (wide.left) / rmsOf (wide.right));
    std::printf ("  8 copias, ancho 100 %%: correlación L/R %.2f, equilibrio %+.2f dB\n", correlation, balanceDb);
    CHECK (correlation < 0.7);
    CHECK (std::abs (balanceDb) < 1.5);

    // Paneo: todo a la derecha deja el izquierdo en silencio; la potencia total (L² + R²) no cambia con el paneo.
    const double centrePower = 2.0 * rmsOf (centred.left) * rmsOf (centred.left);
    for (const float pan : { 1.0f, -0.5f, 0.3f })
    {
        auto panned = sineSources();
        panned.oscillators[0].pan = pan;
        const auto out = render (panned, true);
        const double power = rmsOf (out.left) * rmsOf (out.left) + rmsOf (out.right) * rmsOf (out.right);
        std::printf ("  paneo %+.1f: L %.4f  R %.4f  potencia %+.3f dB\n", static_cast<double> (pan), rmsOf (out.left),
                     rmsOf (out.right), 10.0 * std::log10 (power / centrePower));
        CHECK (std::abs (10.0 * std::log10 (power / centrePower)) < 0.05);
        if (pan == 1.0f)
            CHECK (rmsOf (out.left) < 1.0e-6);
    }
}

void testSourceChangesAreClickFree()
{
    std::printf ("Cambiar unison, detune, ancho, paneo, afinación o encender/apagar fuentes no produce clics\n");

    auto sources = sineSources();
    sources.oscillators[1].octave = -1;
    VoiceManager manager;
    prepareWithSources (manager, 48000.0, sources);
    manager.noteOn (45, 1.0f); // 110 Hz

    constexpr std::array<int, 5> unison { 1, 5, 16, 2, 9 };
    constexpr std::array<float, 3> pans { -1.0f, 0.7f, 0.0f };
    StereoSignal output;
    for (int step = 0; step < 40; ++step)
    {
        auto& a = sources.oscillators[0];
        a.unison = unison[static_cast<size_t> (step) % unison.size()];
        a.detune = step % 3 == 0 ? 1.0f : 0.1f;
        a.width = step % 2 == 0 ? 1.0f : 0.0f;
        a.pan = pans[static_cast<size_t> (step) % pans.size()];
        a.fineCents = step % 2 == 0 ? 40.0f : -40.0f;
        a.level = step % 4 == 0 ? 0.3f : 1.0f;
        sources.oscillators[1].enabled = step % 2 == 1;
        sources.sub.enabled = step % 3 == 1;
        manager.setSourceSettings (sources);

        const auto block = renderStereo (manager, 2400);
        output.left.insert (output.left.end(), block.left.begin(), block.left.end());
        output.right.insert (output.right.end(), block.right.begin(), block.right.end());
    }

    const float jump = std::max (maxSampleJump (output.left), maxSampleJump (output.right));
    std::printf ("  salto máximo entre muestras: %.5f\n", static_cast<double> (jump));
    // Sin suavizado, apagar Osc B o pasar de 1 a 16 copias daría saltos de ~0.2.
    CHECK (jump < 0.03f);
}

void testOscillatorTuning()
{
    std::printf ("Afinación de Osc B (Octave / Semi / Fine) y del sub, medida en el audio (4 sample rates)\n");

    double worstCents = 0.0;
    for (const double sr : sampleRates)
    {
        struct Case
        {
            int octave, semitones;
            float fine;
        };
        for (const auto& c : { Case { 1, 7, 50.0f }, Case { -2, -12, -100.0f }, Case { 0, 3, 12.5f } })
        {
            auto sources = sineSources();
            sources.oscillators[0].enabled = false;
            sources.oscillators[1].enabled = true;
            sources.oscillators[1].octave = c.octave;
            sources.oscillators[1].semitones = c.semitones;
            sources.oscillators[1].fineCents = c.fine;
            VoiceManager manager;
            prepareWithSources (manager, sr, sources);
            manager.noteOn (57, 1.0f); // 220 Hz
            renderSilently (manager, static_cast<int> (0.01 * sr));
            std::vector<float> signal (static_cast<size_t> (sr), 0.0f);
            manager.render (signal.data(), static_cast<int> (signal.size()));

            const double semitones = 12.0 * c.octave + c.semitones + c.fine / 100.0;
            const double cents = centsBetween (measureFrequency (signal, sr), 220.0 * std::exp2 (semitones / 12.0));
            worstCents = std::max (worstCents, std::abs (cents));
        }

        for (const int octave : { -1, -2 })
        {
            auto sources = sineSources();
            sources.oscillators[0].enabled = false;
            sources.sub.enabled = true;
            sources.sub.octave = octave;
            VoiceManager manager;
            prepareWithSources (manager, sr, sources);
            manager.noteOn (57, 1.0f);
            renderSilently (manager, static_cast<int> (0.01 * sr));
            std::vector<float> signal (static_cast<size_t> (sr), 0.0f);
            manager.render (signal.data(), static_cast<int> (signal.size()));
            const double cents = centsBetween (measureFrequency (signal, sr), 220.0 * std::exp2 (octave));
            worstCents = std::max (worstCents, std::abs (cents));
        }
    }
    std::printf ("  peor desviación: %.5f cents\n", worstCents);
    CHECK (worstCents < 0.05);
}

void testSubShapes()
{
    std::printf ("Formas del sub: seno puro; cuadrada con armónicos impares (3.º a 1/3)\n");

    const auto spectrumOf = [] (SubShape shape) {
        auto sources = sineSources();
        sources.oscillators[0].enabled = false;
        sources.sub.enabled = true;
        sources.sub.shape = shape;
        VoiceManager manager;
        prepareWithSources (manager, 48000.0, sources);
        manager.noteOn (57, 1.0f); // sub a 110 Hz
        renderSilently (manager, 4800);
        std::vector<float> signal (65536, 0.0f);
        manager.render (signal.data(), static_cast<int> (signal.size()));
        return magnitudeSpectrum (signal);
    };
    const double binHz = 48000.0 / 65536.0;

    const auto sine = spectrumOf (SubShape::sine);
    const double sineFundamental = componentLevel (sine, 110.0, binHz);
    const double sineSecond = toDb (componentLevel (sine, 220.0, binHz) / sineFundamental);
    const double sineThird = toDb (componentLevel (sine, 330.0, binHz) / sineFundamental);

    const auto square = spectrumOf (SubShape::square);
    const double squareFundamental = componentLevel (square, 110.0, binHz);
    const double squareSecond = toDb (componentLevel (square, 220.0, binHz) / squareFundamental);
    const double squareThird = toDb (componentLevel (square, 330.0, binHz) / squareFundamental);

    std::printf ("  seno: 2.º %.0f dB, 3.º %.0f dB | cuadrada: 2.º %.0f dB, 3.º %.2f dB (ideal -9.54)\n", sineSecond,
                 sineThird, squareSecond, squareThird);
    CHECK (sineSecond < -80.0 && sineThird < -80.0);
    CHECK (squareSecond < -60.0);
    CHECK (std::abs (squareThird + 9.54) < 0.3);
}

void testNoiseColor()
{
    std::printf ("Ruido: blanco al 50 %% (espectro plano), oscuro al 0 %%, brillante al 100 %%\n");

    const auto bandBalanceDb = [] (float color) {
        auto sources = sineSources();
        sources.oscillators[0].enabled = false;
        sources.noise.enabled = true;
        sources.noise.color = color;
        VoiceManager manager;
        prepareWithSources (manager, 48000.0, sources);
        manager.noteOn (60, 1.0f);
        renderSilently (manager, 480);
        std::vector<float> signal (65536, 0.0f);
        manager.render (signal.data(), static_cast<int> (signal.size()));

        double mean = 0.0;
        bool finite = true;
        for (const float s : signal)
        {
            mean += s;
            finite = finite && std::isfinite (s);
        }
        CHECK (finite);
        CHECK (std::abs (mean / static_cast<double> (signal.size())) < 0.05 * rmsOf (signal) + 1.0e-6);

        // Energía media por bin (= por Hz) en una banda grave y en una aguda.
        const auto spectrum = magnitudeSpectrum (signal);
        const double binHz = 48000.0 / 65536.0;
        const auto bandPower = [&] (double fromHz, double toHz) {
            double sum = 0.0;
            int count = 0;
            for (auto k = static_cast<size_t> (fromHz / binHz); k < static_cast<size_t> (toHz / binHz); ++k, ++count)
                sum += spectrum[k] * spectrum[k];
            return sum / count;
        };
        return 10.0 * std::log10 (bandPower (8000.0, 12000.0) / bandPower (300.0, 700.0));
    };

    const double white = bandBalanceDb (0.5f);
    const double dark = bandBalanceDb (0.0f);
    const double bright = bandBalanceDb (1.0f);
    std::printf ("  agudos (8-12 kHz) respecto a graves (300-700 Hz): blanco %+.1f dB, oscuro %+.1f dB, brillante %+.1f dB\n",
                 white, dark, bright);
    CHECK (std::abs (white) < 1.5);
    CHECK (dark < -20.0);
    CHECK (bright > 20.0);
}

void testPhase6Destinations()
{
    std::printf ("Destinos nuevos: Global Pitch mueve A, B y sub; Osc B Pitch solo a B; Detune y Level desde la matriz\n");

    const auto setUp = [] (VoiceManager& manager, undertow::synth::ModSlot slot, SourceSettings sources) {
        sources.oscillators[1].enabled = true;
        sources.sub.enabled = true;
        prepareWithSources (manager, 48000.0, sources);
        ModulationSettings settings;
        settings.slots[0] = slot;
        manager.setModulationSettings (settings);
        manager.setModWheel (1.0f);
        manager.noteOn (57, 1.0f); // 220 Hz
        renderSilently (manager, 4800);
        return findVoice (manager, 57);
    };

    {
        VoiceManager manager;
        const auto* voice = setUp (manager, { ModSource::modWheel, ModDestination::globalPitch, 0.5f }, sineSources());
        CHECK (std::abs (centsBetween (voice->getOscillatorFrequency (0), 440.0)) < 0.01);
        CHECK (std::abs (centsBetween (voice->getOscillatorFrequency (1), 440.0)) < 0.01);
        CHECK (std::abs (centsBetween (voice->getSubFrequency(), 220.0)) < 0.01); // sub: -1 octava +1 octava
    }
    {
        VoiceManager manager;
        const auto* voice = setUp (manager, { ModSource::modWheel, ModDestination::oscBPitch, 0.5f }, sineSources());
        CHECK (std::abs (centsBetween (voice->getOscillatorFrequency (0), 220.0)) < 0.01);
        CHECK (std::abs (centsBetween (voice->getOscillatorFrequency (1), 440.0)) < 0.01);
        CHECK (std::abs (centsBetween (voice->getSubFrequency(), 110.0)) < 0.01);
    }
    {
        // Detune de la perilla en 0 % + 100 % desde la matriz = ±100 cents.
        auto sources = sineSources();
        sources.oscillators[0].unison = 3;
        sources.oscillators[0].detune = 0.0f;
        VoiceManager manager;
        const auto* voice = setUp (manager, { ModSource::modWheel, ModDestination::oscADetune, 1.0f }, sources);
        CHECK (std::abs (centsBetween (voice->getOscillator (0).getVoiceFrequency (0), 220.0) + 100.0) < 0.01);
        CHECK (std::abs (centsBetween (voice->getOscillator (0).getVoiceFrequency (2), 220.0) - 100.0) < 0.01);
    }
    {
        // Sub Level -100 % con la perilla al 75 %: el sub se calla.
        auto sources = sineSources();
        sources.oscillators[0].enabled = false;
        VoiceManager manager;
        (void) setUp (manager, { ModSource::modWheel, ModDestination::subLevel, -1.0f }, sources);
        auto quiet = sources;
        quiet.oscillators[1].enabled = false;
        quiet.sub.enabled = true;
        manager.setSourceSettings (quiet);
        renderSilently (manager, 4800);
        std::vector<float> signal (4800, 0.0f);
        manager.render (signal.data(), static_cast<int> (signal.size()));
        CHECK (rmsOf (signal) < 1.0e-6);
    }
}

void testExtremePitchIsSafe()
{
    std::printf ("Tono extremo (nota 127, Octave +4, Semi +12, Global Pitch +100 %%) no se sale de la tabla\n");

    // Pedirían ~1.6 MHz. Sin el tope de frecuencia, la fase avanzaba más de un ciclo por muestra y la lectura
    // se salía de la tabla (pluginval lo encontró como un crash en su test de automatización).
    for (const double sr : sampleRates)
    {
        auto sources = sineSources();
        sources.oscillators[0].table = &factoryBank().get (0);
        sources.oscillators[0].octave = 4;
        sources.oscillators[0].semitones = 12;
        sources.oscillators[0].unison = 16;
        sources.oscillators[0].detune = 1.0f;
        sources.sub.enabled = true;
        VoiceManager manager;
        prepareWithSources (manager, sr, sources);
        ModulationSettings settings;
        settings.slots[0] = { ModSource::modWheel, ModDestination::globalPitch, 1.0f };
        manager.setModulationSettings (settings);
        manager.setModWheel (1.0f);
        manager.noteOn (127, 1.0f);

        const auto out = renderStereo (manager, static_cast<int> (sr * 0.2));
        bool finite = true;
        for (size_t i = 0; i < out.left.size(); ++i)
            finite = finite && std::isfinite (out.left[i]) && std::isfinite (out.right[i]) && std::abs (out.left[i]) < 10.0f;
        CHECK (finite);
        CHECK (findVoice (manager, 127)->getOscillatorFrequency (0) <= 0.45 * sr);
    }
}

void testUnisonMipmapHasNoAlias()
{
    std::printf ("Unison: el mipmap se elige para la copia más aguda (ninguna copia se refleja)\n");

    // Comprobación exacta: con detune 100 % la copia más aguda está 1 semitono por encima. Su armónico más alto
    // debe quedar por debajo del límite (28 kHz a 48 kHz) en todo el rango de notas.
    WavetableOscillator osc;
    osc.setSampleRate (48000.0);
    osc.setUnison (16, 1.0f, 0.0f, 0.0f);
    bool allBelow = true;
    for (double hz = 20.0; hz < 15000.0; hz *= 1.01)
    {
        osc.setFrequency (hz);
        const double highest = osc.getVoiceFrequency (15);
        if (osc.getMipLevel() < Wavetable::numLevels - 1)
            allBelow = allBelow && Wavetable::maxHarmonicsAtLevel (osc.getMipLevel()) * highest <= 28000.0 * (1.0 + 1.0e-9);
    }
    CHECK (allBelow);

    // En el audio: sierra con 2 copias a ±1 semitono. Todo lo que no sea armónico de una de las dos es alias.
    double worst = -300.0;
    for (const int note : { 72, 84, 96, 103 })
    {
        const double f0 = undertow::dsp::midiNoteToHz (note);
        const double fLow = f0 * std::exp2 (-1.0 / 12.0);
        const double fHigh = f0 * std::exp2 (1.0 / 12.0);
        WavetableOscillator saw;
        saw.setSampleRate (48000.0);
        saw.setWavetable (&factoryBank().get (0));
        saw.setPosition (2.0f / 3.0f);
        saw.setUnison (2, 1.0f, 0.0f, 0.0f);
        saw.setFrequency (f0);
        saw.reset (1234);
        std::vector<float> signal (65536);
        for (auto& s : signal)
            s = saw.processSample();

        const auto spectrum = magnitudeSpectrum (signal);
        const double binHz = 48000.0 / 65536.0;
        const double strongest = *std::max_element (spectrum.begin(), spectrum.end());
        double worstHere = 0.0;
        for (size_t k = 1; k < spectrum.size(); ++k)
        {
            const double hz = static_cast<double> (k) * binHz;
            if (hz > 20000.0)
                break;
            const auto nearHarmonic = [&] (double f) { return std::abs (hz - std::round (hz / f) * f) < 6.0 * binHz; };
            if (nearHarmonic (fLow) || nearHarmonic (fHigh))
                continue;
            worstHere = std::max (worstHere, spectrum[k]);
        }
        worst = std::max (worst, toDb (worstHere / strongest));
    }
    std::printf ("  peor alias (sierra, 2 copias a ±1 semitono, 48 kHz): %.1f dB\n", worst);
    CHECK (worst < -85.0);
}

// Mide el coste de 3 s de audio a 48 kHz con 'notes' notas sonando y el filtro encendido.
void measureVoiceCpu (const char* label, int notes, const SourceSettings& sources, bool modulated)
{
    VoiceManager manager;
    manager.prepare (48000.0);
    manager.setPolyphony (16);
    manager.setSourceSettings (sources);
    manager.setEnvelopeParameters ({ 0.001f, 0.1f, 1.0f, 0.1f });
    manager.setFilterSettings ({ true, { FilterType::lowPass, FilterSlope::db24, 800.0f, 0.5f, 0.3f }, 0.5f });
    if (modulated)
    {
        ModulationSettings settings;
        settings.lfos[0] = { LfoShape::sine, LfoMode::retrigger, false, 6.0f, 5 };
        settings.slots[0] = { ModSource::lfo1, ModDestination::filterCutoff, 0.3f };
        settings.slots[1] = { ModSource::lfo1, ModDestination::oscADetune, 0.2f };
        settings.slots[2] = { ModSource::env2, ModDestination::oscBPosition, 1.0f };
        settings.slots[3] = { ModSource::lfo1, ModDestination::globalPitch, 0.01f };
        manager.setModulationSettings (settings);
    }
    for (int n = 0; n < notes; ++n)
        manager.noteOn (40 + n, 0.8f);

    const int blockSize = 256;
    const int numBlocks = 48000 * 3 / blockSize;
    std::vector<float> left (blockSize), right (blockSize);
    float peak = 0.0f;
    bool finite = true;
    const auto start = std::chrono::steady_clock::now();
    for (int b = 0; b < numBlocks; ++b)
    {
        std::fill (left.begin(), left.end(), 0.0f);
        std::fill (right.begin(), right.end(), 0.0f);
        manager.render (left.data(), right.data(), blockSize);
        for (int i = 0; i < blockSize; ++i)
        {
            const auto s = static_cast<size_t> (i);
            finite = finite && std::isfinite (left[s]) && std::isfinite (right[s]);
            peak = std::max ({ peak, std::abs (left[s]), std::abs (right[s]) });
        }
    }
    const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
    std::printf ("  %-58s %5.1f %% de un núcleo (pico %.2f)\n", label, 100.0 * seconds / 3.0, static_cast<double> (peak));
    CHECK (finite);
    CHECK (peak < 20.0f);
}

void testUnisonCpuCost()
{
    std::printf ("Coste de CPU con unison (48 kHz)\n");

    SourceSettings sources;
    sources.subTable = &factoryBank().get (0);
    sources.oscillators[0].table = &factoryBank().get (0);
    sources.oscillators[0].position = 2.0f / 3.0f;
    sources.oscillators[1].table = &factoryBank().get (4);

    measureVoiceCpu ("8 notas, Osc A sierra sin unison (referencia)", 8, sources, false);

    sources.oscillators[0].unison = 7;
    sources.oscillators[0].detune = 0.4f;
    measureVoiceCpu ("8 notas, supersaw: Osc A con 7 copias", 8, sources, false);

    sources.oscillators[0].unison = 16;
    sources.oscillators[1].enabled = true;
    sources.oscillators[1].unison = 16;
    sources.sub.enabled = true;
    sources.noise.enabled = true;
    measureVoiceCpu ("16 notas, A y B con 16 copias, sub, ruido, 4 rutas (peor caso)", 16, sources, true);
}

// --- Fase 7: warp, FM y ring mod -----------------------------------------------------------------

using undertow::dsp::HalfbandDecimator;
using undertow::dsp::Warp;
using undertow::dsp::WarpMode;
using undertow::synth::FmMode;

constexpr std::array<WarpMode, 7> warpModes { WarpMode::sync,   WarpMode::bendPlus, WarpMode::bendMinus, WarpMode::pwm,
                                              WarpMode::mirror, WarpMode::quantize, WarpMode::bitcrush };

const char* warpName (WarpMode mode) { return undertow::dsp::warpModeNames[static_cast<size_t> (mode)]; }

void testWarpShapes()
{
    std::printf ("Warp: amount 0 = onda original; Bend, PWM y Mirror continuos y con la velocidad acotada\n");

    // Onda de prueba suave (sin saltos) con varios armónicos.
    const auto wave = [] (double phase) {
        return static_cast<float> (std::sin (2.0 * pi * phase) + 0.3 * std::sin (6.0 * pi * phase)
                                   + 0.2 * std::cos (10.0 * pi * phase));
    };

    bool identical = true;
    for (const auto mode : warpModes)
    {
        const auto warp = Warp::make (mode, 0.0f);
        for (int i = 0; i < 1000; ++i)
        {
            const double phase = i / 1000.0;
            identical = identical && undertow::dsp::warpedValue (warp, phase, wave, wave) == wave (phase);
        }
    }
    CHECK (identical);

    // Bend y PWM: la fase deformada es continua (también al dar la vuelta) y nunca va más rápido que maxSpeed,
    // la velocidad con la que el oscilador elige el mipmap.
    constexpr int n = 200000;
    double worstSpeedRatio = 0.0;
    for (const auto mode : { WarpMode::bendPlus, WarpMode::bendMinus, WarpMode::pwm })
    {
        for (const float amount : { 0.3f, 0.7f, 1.0f })
        {
            const auto warp = Warp::make (mode, amount);
            for (int i = 0; i < n; ++i)
            {
                double step = undertow::dsp::warpPhase (warp, undertow::dsp::wrapPhase ((i + 1.0) / n))
                              - undertow::dsp::warpPhase (warp, static_cast<double> (i) / n);
                step -= std::round (step); // pasar de 0.999 a 0 es dar la vuelta, no saltar
                worstSpeedRatio = std::max (worstSpeedRatio, std::abs (step) * n / warp.maxSpeed);
            }
        }
    }
    std::printf ("  Bend/PWM: velocidad máxima medida / prevista = %.4f\n", worstSpeedRatio);
    CHECK (worstSpeedRatio <= 1.0001);

    // Mirror al 100 %: ida y vuelta al doble de velocidad, sin saltos.
    const auto mirror = Warp::make (WarpMode::mirror, 1.0f);
    float worstJump = 0.0f;
    for (int i = 0; i < n; ++i)
        worstJump = std::max (worstJump, std::abs (undertow::dsp::warpedValue (mirror, undertow::dsp::wrapPhase ((i + 1.0) / n), wave, wave)
                                                   - undertow::dsp::warpedValue (mirror, static_cast<double> (i) / n, wave, wave)));
    // Pendiente máxima de la onda de prueba: 2π·(1 + 0.9 + 1); al doble de velocidad, el doble.
    CHECK (worstJump < 2.0 * 2.0 * pi * 2.9 / n);

    // Quantize con 8 escalones: la onda cambia exactamente 8 veces por ciclo.
    const auto quantize = Warp::make (WarpMode::quantize, 5.0f / 7.0f);
    int changes = 0;
    float previous = undertow::dsp::warpedValue (quantize, (n - 1.0) / n, wave, wave);
    for (int i = 0; i < n; ++i)
    {
        const float value = undertow::dsp::warpedValue (quantize, static_cast<double> (i) / n, wave, wave);
        changes += value != previous ? 1 : 0;
        previous = value;
    }
    std::printf ("  Quantize (%.2f escalones): %d cambios por ciclo\n", quantize.steps, changes);
    CHECK (changes == 8);

    // Bitcrush al 100 % (1 bit): solo quedan valores enteros.
    const auto crush = Warp::make (WarpMode::bitcrush, 1.0f);
    bool integers = true;
    for (int i = 0; i < 1000; ++i)
    {
        const float value = undertow::dsp::warpedValue (crush, i / 1000.0, wave, wave);
        integers = integers && value == std::round (value);
    }
    CHECK (integers);
}

void testHalfbandDecimator()
{
    std::printf ("Decimador halfband (2x → 1x): plano hasta 20 kHz; rechaza lo que se reflejaría en lo audible\n");

    for (const double sr : { 44100.0, 48000.0 })
    {
        // Ganancia para un seno de frecuencia 'hz' a la frecuencia sobremuestreada (2·sr). En la banda de paso se mide
        // la amplitud exacta (correlación con ventana de Hann); en la de rechazo basta el RMS de lo que sale.
        const auto gainDb = [sr] (double hz) {
            HalfbandDecimator decimator;
            decimator.prepare (sr);
            const double rate = 2.0 * sr;
            constexpr int settle = 2000;
            constexpr int count = 40000;
            double sum = 0.0, windowSum = 0.0;
            Complex correlation {};
            for (int i = 0; i < settle + count; ++i)
            {
                const auto first = static_cast<float> (std::sin (2.0 * pi * hz * (2.0 * i) / rate));
                const auto second = static_cast<float> (std::sin (2.0 * pi * hz * (2.0 * i + 1.0) / rate));
                const double y = decimator.process (first, second);
                if (i < settle)
                    continue;
                const double window = 0.5 - 0.5 * std::cos (2.0 * pi * (i - settle) / (count - 1.0));
                correlation += window * y * std::polar (1.0, -2.0 * pi * hz * i / sr);
                windowSum += window;
                sum += y * y;
            }
            return hz < 0.5 * sr ? toDb (2.0 * std::abs (correlation) / windowSum) : toDb (std::sqrt (2.0 * sum / count));
        };

        double ripple = 0.0;
        for (const double hz : { 50.0, 1000.0, 5000.0, 10000.0, 15000.0, 19000.0, 20000.0 })
            ripple = std::max (ripple, std::abs (gainDb (hz)));

        // Todo lo que está entre sr − 20 kHz y sr se reflejaría por debajo de 20 kHz al volver a 1x.
        double rejection = -300.0;
        for (double hz = sr - 20000.0; hz < sr; hz += 250.0)
            rejection = std::max (rejection, gainDb (hz));

        HalfbandDecimator decimator;
        decimator.prepare (sr);
        std::printf ("  %4.1f kHz: %3d taps, retardo %.1f muestras; banda de paso ±%.5f dB; rechazo %.1f dB\n", sr / 1000.0,
                     decimator.getNumTaps(), decimator.getLatency(), ripple, rejection);
        CHECK (ripple < 0.001);
        CHECK (rejection < -95.0);
    }
}

// Espectro de una nota (mono) a través de la voz completa, con las fuentes indicadas.
std::vector<double> voiceSpectrum (const SourceSettings& sources, int note, double sampleRate, size_t n,
                                   const ModulationSettings* modulation = nullptr, float modWheel = 0.0f)
{
    VoiceManager manager;
    prepareWithSources (manager, sampleRate, sources);
    if (modulation != nullptr)
        manager.setModulationSettings (*modulation);
    manager.setModWheel (modWheel);
    manager.noteOn (note, 1.0f);
    renderSilently (manager, static_cast<int> (sampleRate * 0.02));
    std::vector<float> signal (n, 0.0f);
    manager.render (signal.data(), static_cast<int> (n));
    return magnitudeSpectrum (signal);
}

// Osc A: seno (portadora). Osc B: seno apagado dos octavas abajo, que solo modula: 110 Hz cuando A toca A5.
SourceSettings fmTestSources (FmMode mode, float amount)
{
    auto sources = sineSources();
    sources.oscillators[1].octave = -2;
    sources.oscillators[0].fmMode = mode;
    sources.oscillators[0].fmAmount = amount;
    return sources;
}

// Amount de la perilla FM que da el índice de modulación β (en radianes).
float fmAmountForIndex (double beta)
{
    return static_cast<float> (std::sqrt (beta / (2.0 * pi) / undertow::synth::maxFmCycles));
}

void testFmMatchesBessel()
{
    std::printf ("FM: las bandas laterales tienen el nivel de las funciones de Bessel J_n(β)\n");

    double worst = 0.0;
    for (const double sr : sampleRates)
    {
        const size_t n = sr > 50000.0 ? 131072 : 65536;
        const double binHz = sr / static_cast<double> (n);
        const auto spectrum = voiceSpectrum (fmTestSources (FmMode::fmOther, fmAmountForIndex (1.0)), 69, sr, n);
        const double carrier = componentLevel (spectrum, 440.0, binHz);
        for (int k = -3; k <= 3; ++k)
        {
            const double expected = std::abs (std::cyl_bessel_j (std::abs (k), 1.0) / std::cyl_bessel_j (0, 1.0));
            const double measured = componentLevel (spectrum, 440.0 + 110.0 * k, binHz) / carrier;
            worst = std::max (worst, std::abs (toDb (measured / expected)));
        }
    }
    std::printf ("  β = 1, bandas en 440 ± n·110 Hz (n = 1..3), 4 sample rates: peor desviación %.3f dB\n", worst);
    CHECK (worst < 0.2);

    // Con β = 2.405 (el primer cero de J0) la portadora desaparece: toda la energía pasa a las bandas laterales.
    const auto spectrum = voiceSpectrum (fmTestSources (FmMode::fmOther, fmAmountForIndex (2.404826)), 69, 48000.0, 65536);
    const double binHz = 48000.0 / 65536.0;
    const double carrierDb = toDb (componentLevel (spectrum, 440.0, binHz) / componentLevel (spectrum, 550.0, binHz));
    std::printf ("  β = 2.405: la portadora queda a %.1f dB de la primera banda lateral\n", carrierDb);
    CHECK (carrierDb < -40.0);
}

void testRingModulation()
{
    std::printf ("Ring mod: 440 Hz × 110 Hz = 330 Hz + 550 Hz (la original desaparece); al 50 %% es AM\n");

    const double sr = 48000.0;
    const size_t n = 65536;
    const double binHz = sr / static_cast<double> (n);
    const auto levelsAt = [&] (float amount) {
        const auto spectrum = voiceSpectrum (fmTestSources (FmMode::rmOther, amount), 69, sr, n);
        return std::array<double, 3> { componentLevel (spectrum, 330.0, binHz), componentLevel (spectrum, 440.0, binHz),
                                       componentLevel (spectrum, 550.0, binHz) };
    };
    const auto dry = levelsAt (0.0f);
    const auto full = levelsAt (1.0f);
    const auto half = levelsAt (0.5f);
    std::printf ("  100 %%: 330 Hz %.2f dB, 440 Hz %.1f dB, 550 Hz %.2f dB\n", toDb (full[0] / dry[1]), toDb (full[1] / dry[1]),
                 toDb (full[2] / dry[1]));
    std::printf ("   50 %%: 330 Hz %.2f dB, 440 Hz %.2f dB, 550 Hz %.2f dB\n", toDb (half[0] / dry[1]), toDb (half[1] / dry[1]),
                 toDb (half[2] / dry[1]));
    CHECK (std::abs (toDb (full[0] / dry[1]) + 6.02) < 0.05);
    CHECK (std::abs (toDb (full[2] / dry[1]) + 6.02) < 0.05);
    CHECK (toDb (full[1] / dry[1]) < -80.0);
    CHECK (std::abs (toDb (half[1] / dry[1]) + 6.02) < 0.05);
    CHECK (std::abs (toDb (half[0] / dry[1]) + 12.04) < 0.05);
}

void testModulatedPathKeepsTheSound()
{
    std::printf ("Elegir un modo con amount 0 no cambia el sonido (camino sobremuestreado = camino normal)\n");

    double worstDb = 0.0;
    for (const double sr : sampleRates)
    {
        const size_t n = sr > 50000.0 ? 131072 : 65536;
        const double binHz = sr / static_cast<double> (n);
        SourceSettings plain;
        plain.subTable = &factoryBank().get (0);
        plain.oscillators[0].table = &factoryBank().get (0);
        plain.oscillators[0].position = 2.0f / 3.0f; // sierra
        const auto reference = voiceSpectrum (plain, 69, sr, n);
        const double fundamental = componentLevel (reference, 440.0, binHz);

        auto warped = plain;
        warped.oscillators[0].warpMode = WarpMode::sync;
        auto modulated = plain;
        modulated.oscillators[0].fmMode = FmMode::fmOther;
        for (const auto& sources : { warped, modulated })
        {
            const auto spectrum = voiceSpectrum (sources, 69, sr, n);
            // Solo los armónicos que la tabla tiene en esta nota (el mipmap de A5 a 48 kHz llega al 32).
            for (int h = 1; h * 440.0 < 19500.0; ++h)
            {
                if (componentLevel (reference, h * 440.0, binHz) < fundamental * 1.0e-4)
                    continue;
                const double db = toDb (componentLevel (spectrum, h * 440.0, binHz) / componentLevel (reference, h * 440.0, binHz));
                worstDb = std::max (worstDb, std::abs (db));
                if (verbose && std::abs (db) > 0.01)
                    std::printf ("    %5.1f kHz, armónico %2d: %+.3f dB\n", sr / 1000.0, h, db);
            }
        }
    }
    std::printf ("  sierra A5, armónicos hasta 19.5 kHz, 4 sample rates: diferencia máxima %.4f dB\n", worstDb);
    CHECK (worstDb < 0.01);

    // El ruido a 2x reparte su energía en el doble de ancho de banda: se compensa para que suene igual. Se compara la
    // banda audible (el decimador sí quita el ruido entre 20 y 24 kHz, que el camino normal deja pasar).
    const auto noisePower = [] (bool oversampled) {
        auto sources = sineSources();
        sources.oscillators[0].enabled = false;
        sources.noise.enabled = true;
        if (oversampled)
            sources.oscillators[0].fmMode = FmMode::fmNoise; // A apagada: solo activa el camino sobremuestreado
        const size_t n = 262144;
        const auto spectrum = voiceSpectrum (sources, 60, 48000.0, n);
        double power = 0.0;
        for (size_t k = 0; k < spectrum.size(); ++k)
        {
            const double hz = 48000.0 * static_cast<double> (k) / static_cast<double> (n);
            if (hz >= 200.0 && hz <= 18000.0)
                power += spectrum[k] * spectrum[k];
        }
        return power;
    };
    const double noiseDb = 10.0 * std::log10 (noisePower (true) / noisePower (false));
    std::printf ("  ruido blanco (200 Hz - 18 kHz), camino sobremuestreado respecto al normal: %+.3f dB\n", noiseDb);
    CHECK (std::abs (noiseDb) < 0.01);
}

// Peor alias (todo lo que no es múltiplo de f0, por debajo de 20 kHz) de una nota a través de la voz.
double voiceAliasDb (const SourceSettings& sources, int note, double sr)
{
    const size_t n = sr > 50000.0 ? 131072 : 65536;
    const auto spectrum = voiceSpectrum (sources, note, sr, n);
    return worstAliasDb (spectrum, undertow::dsp::midiNoteToHz (note), sr / static_cast<double> (n));
}

void testWarpAliasing()
{
    std::printf ("Aliasing de cada warp (seno y sierra, notas C4 a C7 de FL, 4 sample rates; C8 aparte)\n");

    // Umbral de cada modo: el medido con algo de margen, para que una regresión se note. Los dos más difíciles:
    //  - Bend −: su curva frena y acelera muy bruscamente en el centro del ciclo, y eso crea armónicos que ningún
    //    mipmap evita.
    //  - Bitcrush de una sierra: cerca de su salto la onda ondula (Gibbs) y a veces cruza un umbral y vuelve dentro
    //    de la misma muestra; ese pulso más corto que una muestra no se ve y no se puede corregir. (Con el seno: −92 dB.)
    struct Case
    {
        WarpMode mode;
        float amount;
        double thresholdDb;
    };
    constexpr std::array<Case, 7> cases { { { WarpMode::sync, 0.8f, -75.0 },
                                            { WarpMode::bendPlus, 1.0f, -72.0 },
                                            { WarpMode::bendMinus, 1.0f, -60.0 },
                                            { WarpMode::pwm, 0.8f, -75.0 },
                                            { WarpMode::mirror, 1.0f, -85.0 },
                                            { WarpMode::quantize, 0.6f, -75.0 },
                                            { WarpMode::bitcrush, 0.6f, -50.0 } } };
    // C8 (4186 Hz) se mide aparte: con estos amounts la lectura más rápida pasa de 30 kHz, por encima del límite de
    // armónicos incluso para un seno puro, así que ahí el warp crea contenido que ya no se puede limitar.
    constexpr std::array<int, 5> notes { 48, 60, 72, 84, 96 };

    for (const auto& c : cases)
    {
        double worst = -300.0, worstC8 = -300.0;
        for (const double sr : sampleRates)
        {
            for (const float position : { 0.0f, 2.0f / 3.0f })
            {
                SourceSettings sources;
                sources.subTable = &factoryBank().get (0);
                sources.oscillators[0].table = &factoryBank().get (0);
                sources.oscillators[0].position = position;
                sources.oscillators[0].warpMode = c.mode;
                sources.oscillators[0].warpAmount = c.amount;
                for (const int note : notes)
                {
                    const double db = voiceAliasDb (sources, note, sr);
                    auto& target = note < 96 ? worst : worstC8;
                    target = std::max (target, db);
                    if (verbose)
                        std::printf ("    %-9s %5.1f kHz %-6s nota %3d: %6.1f dB\n", warpName (c.mode), sr / 1000.0,
                                     position == 0.0f ? "seno" : "sierra", note, db);
                }
            }
        }
        std::printf ("  %-9s %3.0f %%: peor alias %6.1f dB   (C8: %6.1f dB)\n", warpName (c.mode), c.amount * 100.0, worst, worstC8);
        CHECK (worst < c.thresholdDb);
    }

    // Referencia: un sync "ingenuo" (seno reiniciado en cada ciclo, sin polyBLEP ni oversampling), a 48 kHz.
    const double ratio = Warp::make (WarpMode::sync, 0.8f).syncRatio;
    double naive = -300.0;
    for (const int note : notes)
    {
        const double f0 = undertow::dsp::midiNoteToHz (note);
        std::vector<float> signal (65536);
        double phase = 0.0;
        for (auto& sample : signal)
        {
            sample = static_cast<float> (std::sin (2.0 * pi * undertow::dsp::wrapPhase (phase * ratio)));
            phase = undertow::dsp::wrapPhase (phase + f0 / 48000.0);
        }
        naive = std::max (naive, worstAliasDb (magnitudeSpectrum (signal), f0, 48000.0 / 65536.0));
    }
    std::printf ("  (referencia: sync ingenuo de un seno a 48 kHz: %.1f dB)\n", naive);
}

void testFmAliasing()
{
    std::printf ("Aliasing de la FM (portadora seno y sierra, moduladora una octava arriba, notas C4 a C7 de FL)\n");

    for (const float amount : { 0.3f, 0.6f })
    {
        double worst = -300.0;
        for (const double sr : sampleRates)
        {
            for (const float position : { 0.0f, 2.0f / 3.0f })
            {
                SourceSettings sources;
                sources.subTable = &factoryBank().get (0);
                sources.oscillators[0].table = &factoryBank().get (0);
                sources.oscillators[0].position = position;
                sources.oscillators[0].fmMode = FmMode::fmOther;
                sources.oscillators[0].fmAmount = amount;
                sources.oscillators[1].enabled = false;
                sources.oscillators[1].table = &factoryBank().get (0); // seno
                sources.oscillators[1].octave = 1;
                for (const int note : { 48, 60, 72, 84 })
                {
                    const double db = voiceAliasDb (sources, note, sr);
                    worst = std::max (worst, db);
                    if (verbose)
                        std::printf ("    FM %2.0f %% %5.1f kHz %-6s nota %3d: %6.1f dB\n", amount * 100.0, sr / 1000.0,
                                     position == 0.0f ? "seno" : "sierra", note, db);
                }
            }
        }
        const double cycles = undertow::synth::maxFmCycles * amount * amount;
        std::printf ("  amount %2.0f %% (β = %.1f): peor alias %6.1f dB\n", amount * 100.0, 2.0 * pi * cycles, worst);
        CHECK (worst < -60.0);
    }
}

void testWarpSwitchIsClickFree()
{
    std::printf ("Cambiar el modo de warp o de FM/RM con la nota sonando no produce clics\n");

    auto sources = sineSources();
    sources.oscillators[0].warpAmount = 1.0f;
    sources.oscillators[0].fmAmount = 0.5f;
    VoiceManager manager;
    prepareWithSources (manager, 48000.0, sources);
    manager.noteOn (36, 1.0f); // C3 de FL, 65.4 Hz
    std::vector<float> warmUp (4800, 0.0f);
    manager.render (warmUp.data(), static_cast<int> (warmUp.size()));

    struct Step
    {
        WarpMode warp;
        FmMode fm;
    };
    constexpr std::array<Step, 7> steps { { { WarpMode::bendPlus, FmMode::off },
                                            { WarpMode::none, FmMode::off },
                                            { WarpMode::bendMinus, FmMode::off },
                                            { WarpMode::none, FmMode::fmOther },
                                            { WarpMode::mirror, FmMode::fmOther },
                                            { WarpMode::mirror, FmMode::rmSub },
                                            { WarpMode::none, FmMode::off } } };
    constexpr int segment = 4800;
    float switchJump = 0.0f, steadyJump = 0.0f;
    float previous = warmUp.back();
    for (const auto& step : steps)
    {
        sources.oscillators[0].warpMode = step.warp;
        sources.oscillators[0].fmMode = step.fm;
        manager.setSourceSettings (sources);
        std::vector<float> signal (segment, 0.0f);
        manager.render (signal.data(), segment);
        float switchHere = 0.0f, steadyHere = 0.0f;
        size_t where = 0;
        for (size_t i = 0; i < signal.size(); ++i)
        {
            // Los primeros 10 ms de cada tramo contienen el cambio (el fundido dura 5 ms); el resto es el sonido estable.
            auto& worst = i < 480 ? switchHere : steadyHere;
            const float jump = std::abs (signal[i] - (i == 0 ? previous : signal[i - 1]));
            if (i < 480 && jump > worst)
                where = i;
            worst = std::max (worst, jump);
        }
        if (verbose)
            std::printf ("    (salto mayor del cambio en la muestra %zu)\n", where);
        previous = signal.back();
        switchJump = std::max (switchJump, switchHere);
        steadyJump = std::max (steadyJump, steadyHere);
        if (verbose)
            std::printf ("    -> %-8s + FM/RM %d: al cambiar %.5f, estable %.5f\n", warpName (step.warp),
                         static_cast<int> (step.fm), switchHere, steadyHere);
    }
    std::printf ("  salto máximo al cambiar: %.5f (el propio sonido, ya estable: %.5f)\n", switchJump, steadyJump);
    CHECK (switchJump <= steadyJump * 1.05f);
}

void testWarpAndFmDestinations()
{
    std::printf ("Destinos nuevos: Osc A Warp y Osc A FM/RM desde la matriz (Mod Wheel)\n");

    const double sr = 48000.0;
    const size_t n = 65536;
    const double binHz = sr / static_cast<double> (n);

    // Warp: Sync al 0 % en la perilla; la rueda lo sube al 80 %. Sin rueda, el seno es puro.
    auto warpSources = sineSources();
    warpSources.oscillators[0].warpMode = WarpMode::sync;
    ModulationSettings warpRoute;
    warpRoute.slots[0] = { ModSource::modWheel, ModDestination::oscAWarp, 0.8f };
    const auto overtoneDb = [&] (float wheel) {
        const auto spectrum = voiceSpectrum (warpSources, 69, sr, n, &warpRoute, wheel);
        double overtones = 0.0;
        for (int h = 2; h <= 20; ++h)
            overtones = std::max (overtones, componentLevel (spectrum, h * 440.0, binHz));
        return toDb (overtones / componentLevel (spectrum, 440.0, binHz));
    };
    const double still = overtoneDb (0.0f);
    const double moved = overtoneDb (1.0f);
    std::printf ("  Warp: armónico más fuerte respecto a la fundamental: rueda abajo %.1f dB, arriba %.1f dB\n", still, moved);
    CHECK (still < -80.0);
    CHECK (moved > -6.0);

    // FM: la rueda lleva el índice a β = 1 → la primera banda lateral a J1/J0 (−4.8 dB).
    ModulationSettings fmRoute;
    fmRoute.slots[0] = { ModSource::modWheel, ModDestination::oscAFm, fmAmountForIndex (1.0) };
    const auto sidebandDb = [&] (float wheel) {
        const auto spectrum = voiceSpectrum (fmTestSources (FmMode::fmOther, 0.0f), 69, sr, n, &fmRoute, wheel);
        return toDb (componentLevel (spectrum, 550.0, binHz) / componentLevel (spectrum, 440.0, binHz));
    };
    const double expected = toDb (std::cyl_bessel_j (1, 1.0) / std::cyl_bessel_j (0, 1.0));
    const double off = sidebandDb (0.0f);
    const double on = sidebandDb (1.0f);
    std::printf ("  FM: banda lateral de 550 Hz: rueda abajo %.1f dB, arriba %.2f dB (J1/J0 = %.2f dB)\n", off, on, expected);
    CHECK (off < -80.0);
    CHECK (std::abs (on - expected) < 0.2);
}

void testExtremeWarpIsSafe()
{
    std::printf ("Warp y FM al máximo (unison 16, FM cruzada, notas extremas, modulación rápida) sin valores inválidos\n");

    for (const double sr : sampleRates)
    {
        for (const auto mode : warpModes)
        {
            SourceSettings sources;
            sources.subTable = &factoryBank().get (0);
            sources.sub.enabled = true;
            sources.noise.enabled = true;
            for (size_t o = 0; o < 2; ++o)
            {
                auto& osc = sources.oscillators[o];
                osc.enabled = true;
                osc.table = &factoryBank().get (static_cast<int> (o) * 3);
                osc.position = 0.9f;
                osc.octave = 4;
                osc.unison = 16;
                osc.detune = 1.0f;
                osc.width = 1.0f;
                osc.warpMode = mode;
                osc.warpAmount = 1.0f;
                osc.fmAmount = 1.0f;
            }
            sources.oscillators[0].fmMode = FmMode::fmOther;
            sources.oscillators[1].fmMode = FmMode::fmNoise;

            ModulationSettings modulation;
            modulation.lfos[0] = { LfoShape::sampleAndHold, LfoMode::retrigger, false, 40.0f, 5 };
            modulation.slots[0] = { ModSource::lfo1, ModDestination::oscAWarp, 1.0f };
            modulation.slots[1] = { ModSource::lfo1, ModDestination::oscBFm, 1.0f };
            modulation.slots[2] = { ModSource::lfo1, ModDestination::globalPitch, 1.0f };

            VoiceManager manager;
            prepareWithSources (manager, sr, sources);
            manager.setModulationSettings (modulation);
            manager.setPolyphony (4);
            for (const int note : { 0, 60, 120, 127 })
                manager.noteOn (note, 1.0f);
            const auto out = renderStereo (manager, static_cast<int> (sr / 2));

            bool finite = true;
            for (size_t i = 0; i < out.left.size(); ++i)
                finite = finite && std::isfinite (out.left[i]) && std::isfinite (out.right[i]) && std::abs (out.left[i]) < 20.0f
                         && std::abs (out.right[i]) < 20.0f;
            CHECK (finite);
        }
    }
}

void testWarpCpuCost()
{
    std::printf ("Coste de CPU con warp y FM (48 kHz, camino sobremuestreado)\n");

    SourceSettings sources;
    sources.subTable = &factoryBank().get (0);
    sources.oscillators[0].table = &factoryBank().get (0);
    sources.oscillators[0].position = 2.0f / 3.0f;
    sources.oscillators[1].table = &factoryBank().get (0);

    auto sync = sources;
    sync.oscillators[0].warpMode = WarpMode::sync;
    sync.oscillators[0].warpAmount = 0.6f;
    measureVoiceCpu ("8 notas, Osc A sierra con Sync (sin unison)", 8, sync, false);

    auto fm = sources;
    fm.oscillators[0].position = 0.0f;
    fm.oscillators[0].fmMode = FmMode::fmOther;
    fm.oscillators[0].fmAmount = 0.5f;
    fm.oscillators[1].enabled = false;
    measureVoiceCpu ("8 notas, FM de 2 operadores (B modula a A)", 8, fm, false);

    sync.oscillators[0].unison = 7;
    sync.oscillators[0].detune = 0.4f;
    measureVoiceCpu ("8 notas, Sync con 7 copias de unison", 8, sync, false);

    auto worst = sources;
    worst.sub.enabled = true;
    worst.noise.enabled = true;
    for (auto& osc : worst.oscillators)
    {
        osc.enabled = true;
        osc.unison = 16;
        osc.warpMode = WarpMode::mirror;
        osc.warpAmount = 0.5f;
        osc.fmAmount = 0.3f;
    }
    worst.oscillators[0].fmMode = FmMode::fmOther;
    worst.oscillators[1].fmMode = FmMode::rmSub;
    measureVoiceCpu ("16 notas, A y B: 16 copias, Mirror y FM/RM (peor caso)", 16, worst, true);
}
} // namespace


int main (int argc, char** argv)
{
    // Opciones: --verbose (detalle de las mediciones), --fase7 (solo los tests de la Fase 7, para iterar rápido).
    bool onlyPhase7 = false;
    for (int i = 1; i < argc; ++i)
    {
        verbose = verbose || std::string_view (argv[i]) == "--verbose";
        onlyPhase7 = onlyPhase7 || std::string_view (argv[i]) == "--fase7";
    }
    if (! onlyPhase7)
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

        testLfoShapes();
        testLfoRateIsExact();
        testLfoOneShot();
        testSampleAndHold();
        testTempoSyncRates();
        testFreeModeFollowsSongPosition();
        testFreeVersusRetrigger();
        testKeyToCutoffEqualsKeyTracking();
        testPitchModulation();
        testVibratoRange();
        testEnvelopeSweepsCutoff();
        testModulationIsClickFree();
        testMipBlendIsContinuous();
        testModulationCpuCost();

        testUnisonDetuneSpread();
        testUnisonKeepsLoudness();
        testStereoWidthAndPan();
        testSourceChangesAreClickFree();
        testOscillatorTuning();
        testSubShapes();
        testNoiseColor();
        testPhase6Destinations();
        testExtremePitchIsSafe();
        testUnisonMipmapHasNoAlias();
        testUnisonCpuCost();
    }

    testWarpShapes();
    testHalfbandDecimator();
    testFmMatchesBessel();
    testRingModulation();
    testModulatedPathKeepsTheSound();
    testWarpAliasing();
    testFmAliasing();
    testWarpSwitchIsClickFree();
    testWarpAndFmDestinations();
    testExtremeWarpIsSafe();
    testWarpCpuCost();

    if (failures == 0)
        std::printf ("\nTodos los tests pasaron.\n");
    else
        std::printf ("\n%d comprobaciones fallaron.\n", failures);

    return failures == 0 ? 0 : 1;
}
