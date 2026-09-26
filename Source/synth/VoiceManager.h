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
    // Fase 6: osciladores A y B, sub y ruido. Se llama una vez por bloque.
    void setSourceSettings (const SourceSettings& settings) noexcept;
    [[nodiscard]] const SourceSettings& getSourceSettings() const noexcept { return sources; }

    // Atajos para el oscilador A (los usan los tests).
    void setWavetable (const dsp::Wavetable* table) noexcept; // sin tabla, el oscilador no suena
    void setWavetablePosition (float position) noexcept;      // 0..1, recorre los frames de la tabla
    void setFilterSettings (const FilterSettings& settings) noexcept;

    // Fase 5: envolventes 2 y 3, LFOs y rutas de la matriz. Se llama una vez por bloque.
    void setModulationSettings (const ModulationSettings& settings) noexcept;
    // Tempo y posición del host, al principio de cada bloque (para los LFO sincronizados).
    void setTransport (const Transport& transport) noexcept;
    void setModWheel (float value) noexcept { modWheel = value; }     // 0..1
    void setAftertouch (float value) noexcept { aftertouch = value; } // 0..1

    void noteOn (int midiNote, float velocity) noexcept;
    void noteOff (int midiNote) noexcept;
    void setSustainPedal (bool isDown) noexcept;
    void releaseAll() noexcept; // CC 123 (All Notes Off): release normal
    void killAll() noexcept;    // CC 120 (All Sound Off): fade rápido

    // Suma todas las voces en 'left' y 'right'. Con 'right' nulo la salida es mono: (L + R) / 2 en 'left'.
    void render (float* left, float* right, int numSamples) noexcept;
    void render (float* output, int numSamples) noexcept { render (output, nullptr, numSamples); }

    [[nodiscard]] int getNumActiveVoices() const noexcept;
    [[nodiscard]] const Voice& getVoice (int index) const noexcept { return voices[static_cast<size_t> (index)]; }

    // Fase del LFO para dibujarla en la GUI: la de la nota más reciente que suena, o la del reloj común.
    [[nodiscard]] double getLfoDisplayPhase (int lfo) const noexcept;
    // Ciclos por muestra del LFO con los ajustes y el tempo actuales.
    [[nodiscard]] double getLfoIncrement (int lfo) const noexcept { return lfoIncrements[static_cast<size_t> (lfo)]; }

private:
    [[nodiscard]] int countPlayingVoices() const noexcept;
    [[nodiscard]] Voice* findVoiceToSteal() noexcept;
    [[nodiscard]] Voice& findFreeSlot() noexcept;
    [[nodiscard]] float velocityToGain (float velocity) const noexcept;
    void updateLfoIncrements() noexcept;

    std::array<Voice, voicePoolSize> voices;
    SourceSettings sources;
    FilterSettings filterSettings;
    ModulationSettings modulation;
    Transport transport;
    double sampleRate = 44100.0;
    std::array<double, numLfos> lfoIncrements {};
    std::array<dsp::LfoPhase, numLfos> freeClocks {}; // reloj común de los LFO en modo Free
    float modWheel = 0.0f;
    float aftertouch = 0.0f;
    int polyphony = 8;
    float velocitySensitivity = 0.5f;
    bool sustainPedalDown = false;
    std::uint64_t noteCounter = 0;
};

} // namespace undertow::synth
