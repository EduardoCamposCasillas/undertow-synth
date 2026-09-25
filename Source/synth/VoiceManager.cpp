#include "synth/VoiceManager.h"

#include <algorithm>

namespace undertow::synth
{

void VoiceManager::prepare (double sampleRate) noexcept
{
    for (auto& voice : voices)
        voice.prepare (sampleRate);

    sustainPedalDown = false;
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
    // Cada voz suaviza la posición por su cuenta; en la Fase 5 cada voz tendrá además su propia modulación.
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

    // Misma nota que ya suena (incluso en release): se redispara la MISMA voz.
    // Evita que una tecla repetida con release largo acumule copias de sí misma.
    for (auto& voice : voices)
    {
        if (voice.isPlaying() && voice.getNote() == midiNote)
        {
            voice.start (midiNote, gain, ++noteCounter);
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

    findFreeSlot().start (midiNote, gain, ++noteCounter);
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

    for (auto& voice : voices)
        if (voice.isActive())
            voice.render (output, numSamples);
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
