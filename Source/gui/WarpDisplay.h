#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "dsp/Warp.h"
#include "dsp/Wavetable.h"

namespace undertow::gui
{

// Dibuja un ciclo de la wavetable antes (tenue) y después del warp (brillante). Usa la misma función que el
// oscilador (dsp::warpedValue), así que lo que se ve es lo que suena. La FM y el ring mod no se dibujan: dependen de
// la otra señal y de la relación de tonos, no son una forma fija.
class WarpDisplay final : public juce::Component
{
public:
    void setState (const dsp::Wavetable* newTable, float newPosition, dsp::WarpMode newMode, float newAmount)
    {
        if (newTable == table && juce::approximatelyEqual (newPosition, position) && newMode == mode
            && juce::approximatelyEqual (newAmount, amount))
            return;

        table = newTable;
        position = newPosition;
        mode = newMode;
        amount = newAmount;
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

        // Nivel 0 (todos los armónicos), con la misma mezcla entre frames que el oscilador.
        const int numFrames = table->getNumFrames();
        const float framePosition = position * static_cast<float> (numFrames - 1);
        const int first = juce::jmin (static_cast<int> (framePosition), juce::jmax (0, numFrames - 2));
        const int second = juce::jmin (first + 1, numFrames - 1);
        const float blend = framePosition - static_cast<float> (first);
        const float* a = table->getFrame (0, first);
        const float* b = table->getFrame (0, second);
        const int size = dsp::Wavetable::levelSize (0);

        const auto read = [a, b, blend, size] (double phase) {
            const double exact = phase * size;
            const int index = static_cast<int> (exact);
            const auto fraction = static_cast<float> (exact - index);
            const float fromA = a[index] + (a[index + 1] - a[index]) * fraction;
            const float fromB = b[index] + (b[index + 1] - b[index]) * fraction;
            return fromA + (fromB - fromA) * blend;
        };

        const auto warp = dsp::Warp::make (mode, amount);
        const auto area = bounds.reduced (8.0f, 14.0f);
        const int numPoints = juce::jmax (2, static_cast<int> (area.getWidth() * 2.0f));
        juce::Path original, warped;

        for (int i = 0; i < numPoints; ++i)
        {
            const double phase = static_cast<double> (i) / static_cast<double> (numPoints);
            const float x = area.getX() + area.getWidth() * static_cast<float> (i) / static_cast<float> (numPoints - 1);
            const float yOriginal = area.getCentreY() - read (phase) * area.getHeight() * 0.5f;
            const float yWarped = area.getCentreY() - dsp::warpedValue (warp, phase, read, read) * area.getHeight() * 0.5f;

            if (i == 0)
            {
                original.startNewSubPath (x, yOriginal);
                warped.startNewSubPath (x, yWarped);
            }
            else
            {
                original.lineTo (x, yOriginal);
                warped.lineTo (x, yWarped);
            }
        }

        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.strokePath (original, juce::PathStrokeType (1.5f));
        g.setColour (juce::Colour (0xffffb74d));
        g.strokePath (warped, juce::PathStrokeType (2.0f));

        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.setFont (juce::FontOptions (12.0f));
        const juce::String caption = mode == dsp::WarpMode::none
                                         ? juce::String ("Sin warp")
                                         : juce::String (dsp::warpModeNames[static_cast<size_t> (mode)]) + "  "
                                               + juce::String (juce::roundToInt (amount * 100.0f)) + " %";
        g.drawText (caption, bounds.reduced (8.0f, 4.0f), juce::Justification::topRight);
    }

private:
    const dsp::Wavetable* table = nullptr;
    float position = -1.0f;
    dsp::WarpMode mode = dsp::WarpMode::none;
    float amount = -1.0f;
};

} // namespace undertow::gui
