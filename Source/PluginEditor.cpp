#include "PluginEditor.h"
#include "PluginProcessor.h"

UndertowAudioProcessorEditor::UndertowAudioProcessorEditor (UndertowAudioProcessor& processor)
    : AudioProcessorEditor (processor)
{
    setSize (420, 200);
}

void UndertowAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff101418));

    auto area = getLocalBounds().reduced (20);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawFittedText ("Undertow Synth", area.removeFromTop (40), juce::Justification::centred, 1);

    g.setColour (juce::Colours::lightgrey);
    g.setFont (juce::FontOptions (15.0f));
    g.drawFittedText ("Fase 1 - onda senoidal monofonica\nToca notas MIDI para escucharla",
                      area, juce::Justification::centred, 2);
}
