#include "PluginEditor.h"
#include "Parameters.h"
#include "PluginProcessor.h"

namespace
{
constexpr int knobWidth = 90;
constexpr int knobHeight = 110;
constexpr int groupPadding = 12;
} // namespace

UndertowAudioProcessorEditor::UndertowAudioProcessorEditor (UndertowAudioProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    namespace id = undertow::params;

    struct KnobInfo
    {
        const char* parameterId;
        const char* name;
    };
    constexpr std::array<KnobInfo, numEnvelopeKnobs + numVoiceKnobs> infos { {
        { id::attack, "Attack" },
        { id::decay, "Decay" },
        { id::sustain, "Sustain" },
        { id::release, "Release" },
        { id::voices, "Voices" },
        { id::velocity, "Velocity" },
        { id::master, "Master" },
    } };

    addAndMakeVisible (envelopeGroup);
    addAndMakeVisible (voiceGroup);

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& knob = knobs[i];
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, knobWidth - 10, 20);
        addAndMakeVisible (knob.slider);

        knob.label.setText (infos[i].name, juce::dontSendNotification);
        knob.label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (knob.label);

        // El attachment sincroniza la perilla con el parámetro en ambos sentidos
        // (y con la automatización de FL) sin que la GUI toque el hilo de audio.
        knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            processor.getParameters(), infos[i].parameterId, knob.slider);
    }

    activeVoicesLabel.setJustificationType (juce::Justification::centredRight);
    activeVoicesLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (activeVoicesLabel);

    setSize (groupPadding * 3 + knobWidth * static_cast<int> (knobs.size()) + groupPadding * 4, 230);

    startTimerHz (15); // la GUI consulta el contador; el audio nunca espera a la GUI
}

UndertowAudioProcessorEditor::~UndertowAudioProcessorEditor()
{
    stopTimer();
}

void UndertowAudioProcessorEditor::timerCallback()
{
    const int count = processor.getActiveVoiceCount();
    if (count != lastShownVoiceCount)
    {
        lastShownVoiceCount = count;
        activeVoicesLabel.setText ("Voces sonando: " + juce::String (count), juce::dontSendNotification);
    }
}

void UndertowAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff101418));

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("Undertow Synth", getLocalBounds().removeFromTop (44).reduced (groupPadding, 0),
                juce::Justification::centredLeft);
}

void UndertowAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (groupPadding);
    auto header = area.removeFromTop (32);
    activeVoicesLabel.setBounds (header.removeFromRight (200));

    const auto layoutGroup = [&] (juce::GroupComponent& group, size_t firstKnob, size_t count) {
        auto groupArea = area.removeFromLeft (static_cast<int> (count) * knobWidth + groupPadding * 2);
        group.setBounds (groupArea);

        auto inner = groupArea.reduced (groupPadding, 0).withTrimmedTop (22).withTrimmedBottom (groupPadding);
        for (size_t i = firstKnob; i < firstKnob + count; ++i)
        {
            auto column = inner.removeFromLeft (knobWidth).withHeight (knobHeight + 20);
            knobs[i].label.setBounds (column.removeFromTop (20));
            knobs[i].slider.setBounds (column);
        }
    };

    layoutGroup (envelopeGroup, 0, numEnvelopeKnobs);
    area.removeFromLeft (groupPadding);
    layoutGroup (voiceGroup, numEnvelopeKnobs, numVoiceKnobs);
}
