#pragma once

#include <array>
#include <atomic>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "gui/FilterResponseDisplay.h"
#include "gui/WavetableDisplay.h"

class UndertowAudioProcessor;

// GUI funcional: oscilador, filtro, envolvente y voz. La GUI profesional llega en la Fase 9.
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

    juce::GroupComponent oscillatorGroup { {}, "Oscilador A" };
    juce::Label wavetableLabel;
    juce::ComboBox wavetableBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> wavetableAttachment;
    Knob positionKnob;
    undertow::gui::WavetableDisplay wavetableDisplay;
    std::atomic<float>* wavetableParam = nullptr;
    std::atomic<float>* positionParam = nullptr;

    static constexpr size_t numFilterKnobs = 4;

    juce::GroupComponent filterGroup { {}, "Filtro" };
    juce::ToggleButton filterOnButton { "On" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> filterOnAttachment;
    juce::ComboBox filterTypeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> filterTypeAttachment;
    juce::ComboBox filterSlopeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> filterSlopeAttachment;
    std::array<Knob, numFilterKnobs> filterKnobs;
    undertow::gui::FilterResponseDisplay filterDisplay;

    juce::GroupComponent envelopeGroup { {}, "Envolvente de amplitud" };
    juce::GroupComponent voiceGroup { {}, "Voz" };
    juce::Label activeVoicesLabel;
    int lastShownVoiceCount = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UndertowAudioProcessorEditor)
};
