#pragma once

#include <array>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

class UndertowAudioProcessor;

// GUI funcional de la Fase 2: perillas para la envolvente y la voz. La GUI profesional llega en la Fase 9.
class UndertowAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit UndertowAudioProcessorEditor (UndertowAudioProcessor&);
    ~UndertowAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    static constexpr size_t numEnvelopeKnobs = 4;
    static constexpr size_t numVoiceKnobs = 3;

    UndertowAudioProcessor& processor;
    std::array<Knob, numEnvelopeKnobs + numVoiceKnobs> knobs;
    juce::GroupComponent envelopeGroup { {}, "Envolvente de amplitud" };
    juce::GroupComponent voiceGroup { {}, "Voz" };
    juce::Label activeVoicesLabel;
    int lastShownVoiceCount = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UndertowAudioProcessorEditor)
};
