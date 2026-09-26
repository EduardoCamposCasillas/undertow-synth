#pragma once

#include <array>
#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "synth/VoiceManager.h"
#include "synth/WavetableBank.h"

// Fase 6: sintetizador polifónico estéreo: 2 osciladores wavetable con unison + sub + ruido → filtro ZDF →
// envolvente ADSR de amplitud, con 2 envolventes y 2 LFOs de modulación conectados mediante una matriz de 8 rutas.
class UndertowAudioProcessor final : public juce::AudioProcessor
{
public:
    UndertowAudioProcessor();
    ~UndertowAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock; // evita ocultar la versión double de la clase base

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }

    // Lo lee la GUI para mostrar cuántas voces suenan (útil para ver el voice stealing).
    int getActiveVoiceCount() const noexcept { return activeVoiceCount.load (std::memory_order_relaxed); }

    // La GUI dibuja la forma de onda leyendo el mismo banco (es inmutable: leerlo desde otro hilo es seguro).
    const undertow::synth::WavetableBank& getWavetableBank() const noexcept { return *wavetableBank; }

    // Lo lee la GUI para dibujar la curva del filtro con el sample rate real (atómico: lo escribe prepareToPlay).
    double getCurrentSampleRate() const noexcept { return currentSampleRate.load (std::memory_order_relaxed); }

    // Traduce los parámetros del filtro a la estructura del DSP. Se usa en el audio y en la GUI.
    undertow::synth::FilterSettings readFilterSettings() const noexcept;
    undertow::synth::ModulationSettings readModulationSettings() const noexcept;
    undertow::synth::SourceSettings readSourceSettings() const noexcept;

    // Fase del LFO (0..1) para el punto que se mueve sobre su dibujo en la GUI.
    float getLfoDisplayPhase (int lfo) const noexcept
    {
        return lfoDisplayPhases[static_cast<size_t> (lfo)].load (std::memory_order_relaxed);
    }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void updateVoiceParameters() noexcept;
    void handleMidiMessage (const juce::MidiMessage& message) noexcept;
    void updateTransport() noexcept;

    juce::AudioProcessorValueTreeState parameters;

    // Punteros a los valores atómicos de cada parámetro: leerlos en el hilo de audio es seguro y sin locks.
    std::atomic<float>* attackParam = nullptr;
    std::atomic<float>* decayParam = nullptr;
    std::atomic<float>* sustainParam = nullptr;
    std::atomic<float>* releaseParam = nullptr;
    std::atomic<float>* voicesParam = nullptr;
    std::atomic<float>* velocityParam = nullptr;
    std::atomic<float>* masterParam = nullptr;
    std::atomic<float>* filterOnParam = nullptr;
    std::atomic<float>* filterTypeParam = nullptr;
    std::atomic<float>* filterSlopeParam = nullptr;
    std::atomic<float>* filterCutoffParam = nullptr;
    std::atomic<float>* filterResonanceParam = nullptr;
    std::atomic<float>* filterDriveParam = nullptr;
    std::atomic<float>* filterKeyTrackParam = nullptr;

    struct EnvelopeParams
    {
        std::atomic<float>* attack = nullptr;
        std::atomic<float>* decay = nullptr;
        std::atomic<float>* sustain = nullptr;
        std::atomic<float>* release = nullptr;
    };
    struct LfoParams
    {
        std::atomic<float>* shape = nullptr;
        std::atomic<float>* mode = nullptr;
        std::atomic<float>* sync = nullptr;
        std::atomic<float>* rate = nullptr;
        std::atomic<float>* division = nullptr;
    };
    struct ModSlotParams
    {
        std::atomic<float>* source = nullptr;
        std::atomic<float>* destination = nullptr;
        std::atomic<float>* amount = nullptr;
    };
    struct OscillatorParams
    {
        std::atomic<float>* on = nullptr;
        std::atomic<float>* wavetable = nullptr;
        std::atomic<float>* position = nullptr;
        std::atomic<float>* octave = nullptr;
        std::atomic<float>* semitones = nullptr;
        std::atomic<float>* fine = nullptr;
        std::atomic<float>* level = nullptr;
        std::atomic<float>* pan = nullptr;
        std::atomic<float>* unison = nullptr;
        std::atomic<float>* detune = nullptr;
        std::atomic<float>* width = nullptr;
    };
    std::array<OscillatorParams, undertow::synth::numOscillators> oscillatorParams {};
    std::atomic<float>* subOnParam = nullptr;
    std::atomic<float>* subShapeParam = nullptr;
    std::atomic<float>* subOctaveParam = nullptr;
    std::atomic<float>* subLevelParam = nullptr;
    std::atomic<float>* noiseOnParam = nullptr;
    std::atomic<float>* noiseLevelParam = nullptr;
    std::atomic<float>* noiseColorParam = nullptr;

    std::array<EnvelopeParams, 2> modEnvelopeParams {};
    std::array<LfoParams, undertow::synth::numLfos> lfoParams {};
    std::array<ModSlotParams, undertow::synth::numModSlots> modSlotParams {};

    // Un solo banco para todas las instancias del plugin: se crea con la primera y se libera con la última.
    juce::SharedResourcePointer<undertow::synth::WavetableBank> wavetableBank;

    undertow::synth::VoiceManager voiceManager;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> masterGain;
    std::atomic<int> activeVoiceCount { 0 };
    std::atomic<double> currentSampleRate { 48000.0 };
    std::array<std::atomic<float>, undertow::synth::numLfos> lfoDisplayPhases {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UndertowAudioProcessor)
};
