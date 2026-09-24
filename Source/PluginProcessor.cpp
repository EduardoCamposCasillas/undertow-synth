#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
constexpr float outputGain = 0.25f;          // ~-12 dB: deja margen para no saturar el master de FL
constexpr double declickRampSeconds = 0.005; // 5 ms: lo bastante corto para no notarse como "ataque"
} // namespace

UndertowAudioProcessor::UndertowAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void UndertowAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    oscillator.setSampleRate (sampleRate);
    oscillator.resetPhase();

    // La duración de la rampa se expresa en segundos y SmoothedValue la convierte a muestras,
    // así que dura lo mismo a 44.1 kHz que a 96 kHz.
    amplitude.reset (sampleRate, declickRampSeconds);
    amplitude.setCurrentAndTargetValue (0.0f);

    currentNote = -1;
}

void UndertowAudioProcessor::releaseResources() {}

bool UndertowAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo();
}

void UndertowAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    buffer.clear();

    // Renderizado "sample-accurate": cada evento MIDI trae su posición dentro del bloque.
    // Generamos audio hasta esa posición, aplicamos el evento y seguimos. Si aplicáramos
    // todos los eventos al principio del bloque, el timing variaría con el tamaño del buffer.
    int position = 0;

    for (const auto metadata : midiMessages)
    {
        const int eventPosition = juce::jlimit (0, buffer.getNumSamples(), metadata.samplePosition);
        renderSamples (buffer, position, eventPosition - position);
        position = eventPosition;

        // Solo mensajes cortos (notas, CC): un SysEx largo haría que MidiMessage reserve memoria.
        if (metadata.numBytes <= 3)
            handleMidiMessage (metadata.getMessage());
    }

    renderSamples (buffer, position, buffer.getNumSamples() - position);
}

void UndertowAudioProcessor::handleMidiMessage (const juce::MidiMessage& message) noexcept
{
    if (message.isNoteOn())
    {
        // Si no sonaba nada, reiniciamos la fase para que cada nota empiece igual.
        // Si ya sonaba otra (legato), solo cambiamos la frecuencia: saltar de fase causaría un clic.
        if (currentNote < 0)
            oscillator.resetPhase();

        currentNote = message.getNoteNumber();
        oscillator.setFrequency (undertow::dsp::midiNoteToHz (currentNote));
        amplitude.setTargetValue (message.getFloatVelocity() * outputGain);
    }
    else if (message.isNoteOff())
    {
        // Prioridad a la última nota: soltar una tecla anterior no debe cortar la actual.
        if (message.getNoteNumber() == currentNote)
        {
            currentNote = -1;
            amplitude.setTargetValue (0.0f);
        }
    }
    else if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        currentNote = -1;
        amplitude.setTargetValue (0.0f);
    }
}

void UndertowAudioProcessor::renderSamples (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept
{
    if (numSamples <= 0)
        return;

    // En silencio no hace falta calcular senos: el buffer ya está a cero.
    if (currentNote < 0 && ! amplitude.isSmoothing())
        return;

    const int numChannels = buffer.getNumChannels();

    for (int i = startSample; i < startSample + numSamples; ++i)
    {
        const float sample = oscillator.processSample() * amplitude.getNextValue();

        for (int channel = 0; channel < numChannels; ++channel)
            buffer.setSample (channel, i, sample);
    }
}

juce::AudioProcessorEditor* UndertowAudioProcessor::createEditor()
{
    return new UndertowAudioProcessorEditor (*this); // el host toma la propiedad del editor
}

// Punto de entrada que JUCE llama para crear una instancia del plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new UndertowAudioProcessor(); // el wrapper del formato (VST3/Standalone) toma la propiedad
}
