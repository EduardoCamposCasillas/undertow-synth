#pragma once

#include <array>
#include <cstdint>

#include "synth/Voice.h"

namespace undertow::synth
{

// Asigna notas a voces, aplica el límite de polifonía y roba voces sin clics.
// Sin dependencias de JUCE: recibe eventos ya traducidos (nota, velocity 0..1) y se puede testear aislado.
class VoiceManager
{
public:
    static constexpr int maxPolyphony = 16;

    // El doble de ranuras que de polifonía: una voz robada necesita ~5 ms para apagarse
    // y mientras tanto la nota nueva ya suena en OTRA ranura. Así el robo no retrasa la nota nueva.
    static constexpr int voicePoolSize = 2 * maxPolyphony;

    // Ganancia por voz: deja margen para que un acorde de varias notas no sature la salida.
    static constexpr float voiceLevel = 0.25f;

    void prepare (double sampleRate) noexcept;
    void setEnvelopeParameters (const dsp::AdsrParameters& parameters) noexcept;
    void setPolyphony (int numVoices) noexcept;
    void setVelocitySensitivity (float amount) noexcept; // 0 = ignora la velocity, 1 = sensibilidad total

    void noteOn (int midiNote, float velocity) noexcept;
    void noteOff (int midiNote) noexcept;
    void setSustainPedal (bool isDown) noexcept;
    void releaseAll() noexcept; // CC 123 (All Notes Off): release normal
    void killAll() noexcept;    // CC 120 (All Sound Off): fade rápido

    void render (float* output, int numSamples) noexcept;

    [[nodiscard]] int getNumActiveVoices() const noexcept;
    [[nodiscard]] const Voice& getVoice (int index) const noexcept { return voices[static_cast<size_t> (index)]; }

private:
    [[nodiscard]] int countPlayingVoices() const noexcept;
    [[nodiscard]] Voice* findVoiceToSteal() noexcept;
    [[nodiscard]] Voice& findFreeSlot() noexcept;
    [[nodiscard]] float velocityToGain (float velocity) const noexcept;

    std::array<Voice, voicePoolSize> voices;
    int polyphony = 8;
    float velocitySensitivity = 0.5f;
    bool sustainPedalDown = false;
    std::uint64_t noteCounter = 0;
};

} // namespace undertow::synth
