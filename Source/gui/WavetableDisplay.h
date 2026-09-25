#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "dsp/Wavetable.h"

namespace undertow::gui
{

// Dibuja un ciclo de la wavetable en la posición actual, con la misma mezcla entre frames
// que hace el oscilador. Es un visor simple; la visualización completa llega en la Fase 9.
class WavetableDisplay final : public juce::Component
{
public:
    void setWavetable (const dsp::Wavetable* newTable, float newPosition)
    {
        if (newTable == table && juce::approximatelyEqual (newPosition, position))
            return;

        table = newTable;
        position = newPosition;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0b0e11));
        g.fillRoundedRectangle (bounds, 6.0f);

        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine (juce::roundToInt (bounds.getCentreY()), bounds.getX(), bounds.getRight());

        if (table == nullptr)
            return;

        // Nivel 0 = el ciclo con todos sus armónicos, tal como se diseñó.
        const int numFrames = table->getNumFrames();
        const float framePosition = position * static_cast<float> (numFrames - 1);
        const int first = juce::jmin (static_cast<int> (framePosition), juce::jmax (0, numFrames - 2));
        const int second = juce::jmin (first + 1, numFrames - 1);
        const float blend = framePosition - static_cast<float> (first);
        const float* a = table->getFrame (0, first);
        const float* b = table->getFrame (0, second);

        const auto area = bounds.reduced (8.0f, 10.0f);
        const int numPoints = juce::jmax (2, static_cast<int> (area.getWidth()));
        juce::Path path;

        for (int i = 0; i < numPoints; ++i)
        {
            const int index = i * dsp::Wavetable::levelSize (0) / numPoints;
            const float value = a[index] + (b[index] - a[index]) * blend;
            const float x = area.getX() + area.getWidth() * static_cast<float> (i) / static_cast<float> (numPoints - 1);
            const float y = area.getCentreY() - value * area.getHeight() * 0.5f;

            if (i == 0)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }

        g.setColour (juce::Colour (0xff4fc3f7));
        g.strokePath (path, juce::PathStrokeType (2.0f));

        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("Frame " + juce::String (juce::roundToInt (framePosition) + 1) + " / " + juce::String (numFrames),
                    bounds.reduced (8.0f, 4.0f), juce::Justification::topRight);
    }

private:
    const dsp::Wavetable* table = nullptr;
    float position = -1.0f;
};

} // namespace undertow::gui
