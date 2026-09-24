#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class UndertowAudioProcessor;

// GUI mínima de la Fase 1. La GUI profesional llega en la Fase 9.
class UndertowAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit UndertowAudioProcessorEditor (UndertowAudioProcessor&);
    ~UndertowAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override {}

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UndertowAudioProcessorEditor)
};
