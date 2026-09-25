#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <juce_gui_basics/juce_gui_basics.h>

#include "dsp/Lfo.h"

namespace undertow::gui
{

// Dibuja un ciclo de la forma del LFO y un punto en la fase actual (la de la nota más reciente).
// Usa la misma función que el audio (Lfo::shapeValue), así que el dibujo es exactamente lo que suena.
class LfoDisplay final : public juce::Component
{
public:
    void setState (dsp::LfoShape newShape, float newPhase)
    {
        // Un píxel de diferencia no vale un repintado.
        if (newShape == shape && std::abs (newPhase - phase) < 0.002f)
            return;

        shape = newShape;
        phase = newPhase;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0b0e11));
        g.fillRoundedRectangle (bounds, 6.0f);

        const auto area = bounds.reduced (10.0f, 12.0f);
        const auto xForPhase = [&] (double p) { return area.getX() + area.getWidth() * static_cast<float> (p); };
        const auto yForValue = [&] (float v) { return area.getCentreY() - 0.5f * area.getHeight() * v; };

        // Línea del centro: por encima el LFO suma al destino, por debajo resta.
        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());

        juce::Path path;
        const int numPoints = juce::jmax (2, static_cast<int> (area.getWidth()));
        for (int i = 0; i < numPoints; ++i)
        {
            const double p = static_cast<double> (i) / (numPoints - 1);
            const float x = xForPhase (p);
            const float y = yForValue (valueAt (p));
            if (i == 0)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }

        const auto colour = juce::Colour (0xff4dd0e1);
        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (2.0f));

        const float x = xForPhase (phase);
        const float y = yForValue (valueAt (phase));
        g.setColour (colour.withAlpha (0.25f));
        g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
        g.setColour (juce::Colours::white);
        g.fillEllipse (x - 4.0f, y - 4.0f, 8.0f, 8.0f);
    }

private:
    // El Sample & Hold es al azar: se dibuja una secuencia fija de 8 escalones como ejemplo.
    float valueAt (double p) const
    {
        const auto step = static_cast<std::uint32_t> (std::min (p, 0.9999) * 8.0);
        return dsp::Lfo::shapeValue (shape, p, dsp::Lfo::randomValue (7u, step));
    }

    dsp::LfoShape shape = dsp::LfoShape::sine;
    float phase = 0.0f;
};

} // namespace undertow::gui
