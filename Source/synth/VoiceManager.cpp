#include "synth/VoiceManager.h"

#include <algorithm>
#include <cmath>

namespace undertow::synth
{

void VoiceManager::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate;
    for (auto& voice : voices)
    {
        voice.prepare (sampleRate);
        voice.setModulationEnvelopes (modulation.envelope2, modulation.envelope3);
    }

    sustainPedalDown = false;
    freeClocks = {};
    updateLfoIncrements();
}

void VoiceManager::setEnvelopeParameters (const dsp::AdsrParameters& parameters) noexcept
{
    // Los cambios afectan también a las notas que ya suenan (como en un sinte analógico).
    for (auto& voice : voices)
        voice.setEnvelopeParameters (parameters);
}

void VoiceManager::setPolyphony (int numVoices) noexcept
{
    // Si se reduce mientras suenan notas, no se cortan: el límite se aplica en el siguiente noteOn.
    polyphony = std::clamp (numVoices, 1, maxPolyphony);
}

void VoiceManager::setVelocitySensitivity (float amount) noexcept
{
    velocitySensitivity = std::clamp (amount, 0.0f, 1.0f);
}

void VoiceManager::setWavetable (const dsp::Wavetable* table) noexcept
{
    for (auto& voice : voices)
        voice.setWavetable (table);
}

void VoiceManager::setWavetablePosition (float position) noexcept
{
    // Cada voz suaviza la posición por su cuenta (y le suma su propia modulación).
    for (auto& voice : voices)
        voice.setWavetablePosition (position);
}

void VoiceManager::setFilterSettings (const FilterSettings& settings) noexcept
{
    // Se llama una vez por bloque: si nada cambió no se recalculan los objetivos de 32 filtros.
    if (settings == filterSettings)
        return;

    filterSettings = settings;
    for (auto& voice : voices)
        voice.setFilterSettings (settings);
}

void VoiceManager::setModulationSettings (const ModulationSettings& settings) noexcept
{
    if (settings == modulation)
        return;

    modulation = settings;
    for (auto& voice : voices)
        voice.setModulationEnvelopes (modulation.envelope2, modulation.envelope3);
    updateLfoIncrements();
}

void VoiceManager::setTransport (const Transport& newTransport) noexcept
{
    transport = newTransport;
    if (! (transport.bpm > 0.0))
        transport.bpm = 120.0;
    updateLfoIncrements();

    // Modo Free + Sync con la canción sonando: la fase se calcula desde la posición en negras. Así un LFO
    // de 1/4 cae siempre en el pulso, aunque se empiece a reproducir a mitad de un compás o se repita un loop.
    if (! transport.isPlaying || ! transport.hasPosition)
        return;

    for (size_t l = 0; l < modulation.lfos.size(); ++l)
    {
        const auto& lfo = modulation.lfos[l];
        if (lfo.mode != LfoMode::free || ! lfo.tempoSync)
            continue;

        const double cycles = transport.ppqPosition / lfoDivisions[static_cast<size_t> (lfo.division)].beats;
        const double whole = std::floor (cycles);
        freeClocks[l].phase = cycles - whole;
        freeClocks[l].cycle = static_cast<std::uint32_t> (static_cast<std::int64_t> (whole));
    }
}

void VoiceManager::updateLfoIncrements() noexcept
{
    for (size_t l = 0; l < modulation.lfos.size(); ++l)
    {
        const auto& lfo = modulation.lfos[l];
        const auto division = static_cast<size_t> (std::clamp (lfo.division, 0, static_cast<int> (lfoDivisions.size()) - 1));

        // Sync: un ciclo dura 'beats' negras; a 120 BPM una negra dura 0.5 s, así que 1/4 = 2 Hz.
        const double hz = lfo.tempoSync ? transport.bpm / 60.0 / lfoDivisions[division].beats
                                        : static_cast<double> (lfo.rateHz);
        lfoIncrements[l] = hz / sampleRate;
    }
}

float VoiceManager::velocityToGain (float velocity) const noexcept
{
    // Curva cuadrática: velocity 64 (~0.5) da 0.25 (≈ -12 dB). Una curva lineal comprime
    // demasiado el rango dinámico porque el oído percibe el volumen de forma logarítmica.
    const float curved = velocity * velocity;
    return (1.0f - velocitySensitivity) + velocitySensitivity * curved;
}

void VoiceManager::noteOn (int midiNote, float velocity) noexcept
{
    const float gain = velocityToGain (velocity) * voiceLevel;
    const auto seed = static_cast<std::uint32_t> ((noteCounter + 1) * 0x9E3779B97F4A7C15ull >> 32);

    // Misma nota que ya suena (incluso en release): se redispara la MISMA voz.
    // Evita que una tecla repetida con release largo acumule copias de sí misma.
    for (auto& voice : voices)
    {
        if (voice.isPlaying() && voice.getNote() == midiNote)
        {
            voice.start (midiNote, velocity, gain, ++noteCounter, seed);
            return;
        }
    }

    // 'while' y no 'if': si el usuario bajó la polifonía, puede que sobre más de una voz.
    while (countPlayingVoices() >= polyphony)
    {
        if (auto* victim = findVoiceToSteal())
            victim->steal();
        else
            break;
    }

    findFreeSlot().start (midiNote, velocity, gain, ++noteCounter, seed);
}

void VoiceManager::noteOff (int midiNote) noexcept
{
    for (auto& voice : voices)
    {
        if (voice.isPlaying() && voice.isKeyHeld() && voice.getNote() == midiNote)
        {
            if (sustainPedalDown)
                voice.holdWithPedal();
            else
                voice.release();
        }
    }
}

void VoiceManager::setSustainPedal (bool isDown) noexcept
{
    sustainPedalDown = isDown;

    if (! isDown)
        for (auto& voice : voices)
            if (voice.isPlaying() && voice.isSustainedByPedal())
                voice.release();
}

void VoiceManager::releaseAll() noexcept
{
    sustainPedalDown = false;

    for (auto& voice : voices)
        if (voice.isPlaying())
            voice.release();
}

void VoiceManager::killAll() noexcept
{
    sustainPedalDown = false;

    for (auto& voice : voices)
        voice.steal();
}

void VoiceManager::render (float* output, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    ModulationContext context { .settings = modulation, .lfoIncrements = lfoIncrements, .freeClocks = freeClocks,
                                .modWheel = modWheel, .aftertouch = aftertouch };
    for (const auto& slot : modulation.slots)
        if (slot.isRouted())
        {
            context.sourceUsed[static_cast<size_t> (slot.source)] = true;
            context.anyRouted = true;
        }

    for (auto& voice : voices)
        if (voice.isActive())
            voice.render (output, numSamples, context);

    // El reloj común avanza aunque no suene ninguna nota: una nota nueva en modo Free entra "en fase".
    for (size_t l = 0; l < freeClocks.size(); ++l)
        freeClocks[l].advance (lfoIncrements[l], numSamples);
}

double VoiceManager::getLfoDisplayPhase (int lfo) const noexcept
{
    const Voice* newest = nullptr;
    for (const auto& voice : voices)
        if (voice.isPlaying() && (newest == nullptr || voice.getNoteOrder() > newest->getNoteOrder()))
            newest = &voice;

    if (newest != nullptr)
        return newest->getLfoPhase (lfo);

    const auto l = static_cast<size_t> (lfo);
    return modulation.lfos[l].mode == LfoMode::free ? freeClocks[l].phase : 0.0;
}

int VoiceManager::getNumActiveVoices() const noexcept
{
    return static_cast<int> (std::count_if (voices.begin(), voices.end(), [] (const Voice& v) { return v.isActive(); }));
}

int VoiceManager::countPlayingVoices() const noexcept
{
    return static_cast<int> (std::count_if (voices.begin(), voices.end(), [] (const Voice& v) { return v.isPlaying(); }));
}

Voice* VoiceManager::findVoiceToSteal() noexcept
{
    // Prioridad para robar (de menos a más importante musicalmente):
    //   0) notas ya soltadas que se están desvaneciendo en su release,
    //   1) notas sostenidas solo por el pedal,
    //   2) teclas pulsadas.
    // Dentro del mismo grupo se roba la más antigua: suele ser la que menos atención recibe.
    const auto priority = [] (const Voice& v) {
        if (v.isKeyHeld())
            return 2;
        return v.isSustainedByPedal() ? 1 : 0;
    };

    Voice* victim = nullptr;

    for (auto& voice : voices)
    {
        if (! voice.isPlaying())
            continue;

        if (victim == nullptr
            || priority (voice) < priority (*victim)
            || (priority (voice) == priority (*victim) && voice.getNoteOrder() < victim->getNoteOrder()))
        {
            victim = &voice;
        }
    }

    return victim;
}

Voice& VoiceManager::findFreeSlot() noexcept
{
    for (auto& voice : voices)
        if (! voice.isActive())
            return voice;

    // Caso extremo: más de 16 notas en menos de 5 ms y todas las ranuras ocupadas.
    // Se corta la voz más silenciosa, donde un corte seco apenas se oye.
    auto& quietest = *std::min_element (voices.begin(), voices.end(), [] (const Voice& a, const Voice& b) {
        return a.getEnvelopeLevel() < b.getEnvelopeLevel();
    });
    quietest.hardStop();
    return quietest;
}

} // namespace undertow::synth
