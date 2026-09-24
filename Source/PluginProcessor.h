#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/SineOscillator.h"

// Fase 1: sintetizador monofónico con una onda senoidal.
// La polifonía y la envolvente ADSR llegan en la Fase 2.
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

    // Aún no hay parámetros que guardar; llegará con el AudioProcessorValueTreeState.
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    void handleMidiMessage (const juce::MidiMessage& message) noexcept;
    void renderSamples (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept;

    undertow::dsp::SineOscillator oscillator;

    // Rampa corta de amplitud. Sin ella, empezar o cortar la onda a mitad de ciclo
    // produce un salto instantáneo que se oye como un "clic".
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> amplitude;

    int currentNote = -1; // -1 = ninguna nota sonando

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UndertowAudioProcessor)
};
