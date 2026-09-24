// Tests de las piezas de DSP y de gestión de voces. No dependen de JUCE: se compilan como un
// ejecutable normal y se lanzan con ctest. Si algo falla, imprime el archivo, la línea y la condición.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

#include "dsp/AdsrEnvelope.h"
#include "synth/VoiceManager.h"

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
    manager.prepare (48000.0);
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
    manager.prepare (48000.0);
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
    manager.prepare (48000.0);
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
    manager.prepare (48000.0);

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
    manager.prepare (48000.0);
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
            manager.prepare (sr);
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
        manager.prepare (48000.0);
        manager.setEnvelopeParameters ({ 0.001f, 0.001f, 1.0f, 0.1f });
        manager.noteOn (note, 1.0f);
        renderSilently (manager, 480);
        std::vector<float> signal (48000, 0.0f);
        manager.render (signal.data(), static_cast<int> (signal.size()));
        std::printf ("  %9d | %9s | %13.4f | %11.4f\n", note, flName, undertow::dsp::midiNoteToHz (note),
                     measureFrequency (signal, 48000.0));
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

    if (failures == 0)
        std::printf ("\nTodos los tests pasaron.\n");
    else
        std::printf ("\n%d comprobaciones fallaron.\n", failures);

    return failures == 0 ? 0 : 1;
}
