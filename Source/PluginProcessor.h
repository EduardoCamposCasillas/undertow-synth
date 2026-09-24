#pragma once

#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "synth/VoiceManager.h"

// Fase 2: sintetizador polifónico (senos) con envolvente ADSR de amplitud.
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

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void updateVoiceParameters() noexcept;
    void handleMidiMessage (const juce::MidiMessage& message) noexcept;

    juce::AudioProcessorValueTreeState parameters;

    // Punteros a los valores atómicos de cada parámetro: leerlos en el hilo de audio es seguro y sin locks.
    std::atomic<float>* attackParam = nullptr;
    std::atomic<float>* decayParam = nullptr;
    std::atomic<float>* sustainParam = nullptr;
    std::atomic<float>* releaseParam = nullptr;
    std::atomic<float>* voicesParam = nullptr;
    std::atomic<float>* velocityParam = nullptr;
    std::atomic<float>* masterParam = nullptr;

    undertow::synth::VoiceManager voiceManager;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> masterGain;
    std::atomic<int> activeVoiceCount { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UndertowAudioProcessor)
};
