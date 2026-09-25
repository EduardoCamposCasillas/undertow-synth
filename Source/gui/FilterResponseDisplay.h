#pragma once

#include <cmath>

#include <juce_gui_basics/juce_gui_basics.h>

#include "dsp/Filter.h"

namespace undertow::gui
{

// Dibuja la respuesta en frecuencia del filtro: cuánto deja pasar de cada frecuencia (20 Hz – 20 kHz).
// Usa la misma fórmula que verifican los tests, así que la curva es exactamente lo que hace el audio.
// Se muestra para la nota de referencia C5 (sin key tracking) y sin drive.
class FilterResponseDisplay final : public juce::Component
{
public:
    void setResponse (const dsp::FilterParameters& newParameters, bool newEnabled, double newSampleRate)
    {
        if (newParameters == parameters && newEnabled == enabled && juce::approximatelyEqual (newSampleRate, sampleRate))
            return;

        parameters = newParameters;
        enabled = newEnabled;
        sampleRate = newSampleRate;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff0b0e11));
        g.fillRoundedRectangle (bounds, 6.0f);

        const auto area = bounds.reduced (8.0f, 10.0f);
        const auto xForHz = [&] (double hz) {
            return area.getX() + area.getWidth() * static_cast<float> (std::log (hz / minHz) / std::log (maxHz / minHz));
        };
        const auto yForDb = [&] (double db) {
            const double clamped = juce::jlimit (minDb, maxDb, db);
            return area.getBottom() - area.getHeight() * static_cast<float> ((clamped - minDb) / (maxDb - minDb));
        };

        // Rejilla: 100 Hz, 1 kHz y 10 kHz, y la línea de 0 dB (el filtro ni sube ni baja).
        g.setFont (juce::FontOptions (11.0f));
        for (const double hz : { 100.0, 1000.0, 10000.0 })
        {
            const float x = xForHz (hz);
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawText (hz < 1000.0 ? juce::String (juce::roundToInt (hz)) : juce::String (juce::roundToInt (hz / 1000.0)) + "k",
                        juce::Rectangle<float> (x + 3.0f, area.getBottom() - 14.0f, 30.0f, 14.0f),
                        juce::Justification::centredLeft);
        }
        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.drawHorizontalLine (juce::roundToInt (yForDb (0.0)), area.getX(), area.getRight());

        juce::Path path;
        const int numPoints = juce::jmax (2, static_cast<int> (area.getWidth()));
        for (int i = 0; i < numPoints; ++i)
        {
            const double hz = minHz * std::pow (maxHz / minHz, static_cast<double> (i) / (numPoints - 1));
            const double gain = enabled ? dsp::filterMagnitude (parameters, hz, sampleRate) : 1.0;
            const double db = 20.0 * std::log10 (juce::jmax (gain, 1.0e-6));
            const float x = xForHz (hz);
            const float y = yForDb (db);

            if (i == 0)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }

        // Filtro apagado: línea plana y tenue (todo pasa sin cambios).
        g.setColour (juce::Colour (0xffffb74d).withAlpha (enabled ? 1.0f : 0.3f));
        g.strokePath (path, juce::PathStrokeType (2.0f));
    }

private:
    static constexpr double minHz = 20.0;
    static constexpr double maxHz = 20000.0;
    static constexpr double minDb = -48.0;
    static constexpr double maxDb = 24.0;

    dsp::FilterParameters parameters;
    bool enabled = false;
    double sampleRate = 48000.0;
};

} // namespace undertow::gui
