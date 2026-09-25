#include "PluginEditor.h"
#include "Parameters.h"
#include "PluginProcessor.h"

namespace
{
constexpr int knobWidth = 90;
constexpr int knobHeight = 110;
constexpr int groupPadding = 12;
constexpr int rowHeight = 174;
constexpr int wavetableColumnWidth = 170;
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

    addAndMakeVisible (oscillatorGroup);
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

    // --- Oscilador A ---
    wavetableLabel.setText ("Wavetable", juce::dontSendNotification);
    wavetableLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (wavetableLabel);

    // Las opciones deben existir ANTES de crear el attachment (usa el índice del ítem, empezando en 1).
    for (int i = 0; i < undertow::synth::WavetableBank::numTables; ++i)
        wavetableBox.addItem (undertow::synth::WavetableBank::names[static_cast<size_t> (i)], i + 1);
    addAndMakeVisible (wavetableBox);
    wavetableAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.getParameters(), id::oscAWavetable, wavetableBox);

    positionKnob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, knobWidth - 10, 20);
    addAndMakeVisible (positionKnob.slider);
    positionKnob.label.setText ("Position", juce::dontSendNotification);
    positionKnob.label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (positionKnob.label);
    positionKnob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.getParameters(), id::oscAPosition, positionKnob.slider);

    addAndMakeVisible (wavetableDisplay);
    wavetableParam = processor.getParameters().getRawParameterValue (id::oscAWavetable);
    positionParam = processor.getParameters().getRawParameterValue (id::oscAPosition);

    activeVoicesLabel.setJustificationType (juce::Justification::centredRight);
    activeVoicesLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (activeVoicesLabel);

    setSize (groupPadding * 3 + knobWidth * static_cast<int> (knobs.size()) + groupPadding * 4,
             groupPadding * 3 + 32 + rowHeight * 2);

    startTimerHz (30); // la GUI consulta el estado; el audio nunca espera a la GUI
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

    // Se lee el valor del parámetro (no el de la perilla) para que el visor siga también la automatización.
    const auto& bank = processor.getWavetableBank();
    wavetableDisplay.setWavetable (&bank.get (static_cast<int> (wavetableParam->load())), positionParam->load());
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

    // Fila 1: oscilador (selector de tabla, perilla Position y visor de la forma de onda).
    {
        auto groupArea = area.removeFromTop (rowHeight);
        oscillatorGroup.setBounds (groupArea);
        auto inner = groupArea.reduced (groupPadding, 0).withTrimmedTop (22).withTrimmedBottom (groupPadding);

        auto tableColumn = inner.removeFromLeft (wavetableColumnWidth);
        wavetableLabel.setBounds (tableColumn.removeFromTop (20));
        wavetableBox.setBounds (tableColumn.removeFromTop (28).reduced (4, 0));

        auto knobColumn = inner.removeFromLeft (knobWidth).withHeight (knobHeight + 20);
        positionKnob.label.setBounds (knobColumn.removeFromTop (20));
        positionKnob.slider.setBounds (knobColumn);

        wavetableDisplay.setBounds (inner.withTrimmedLeft (groupPadding));
    }
    area.removeFromTop (groupPadding);

    const auto layoutGroup = [&] (juce::GroupComponent& group, size_t firstKnob, size_t count) {
        auto groupArea = area.removeFromLeft (static_cast<int> (count) * knobWidth + groupPadding * 2).withHeight (rowHeight);
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
