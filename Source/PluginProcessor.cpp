#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

namespace
{
constexpr double masterSmoothingSeconds = 0.02; // 20 ms: suficiente para que girar el volumen no haga "zipper"
constexpr float minusInfinityDb = -60.0f;

// Muestra los tiempos como "12 ms" o "1.25 s", que es como se piensan al diseñar sonido.
juce::String timeToText (float seconds, int /*maxLength*/)
{
    if (seconds < 1.0f)
        return juce::String (seconds * 1000.0f, seconds < 0.01f ? 1 : 0) + " ms";
    return juce::String (seconds, 2) + " s";
}

// Acepta "250 ms", "1.5 s" o un número suelto (se interpreta en ms).
float textToTime (const juce::String& text)
{
    const auto trimmed = text.trim().toLowerCase();
    const float value = trimmed.getFloatValue();
    const bool isSeconds = trimmed.endsWith ("s") && ! trimmed.endsWith ("ms");
    return isSeconds ? value : value / 1000.0f;
}

std::unique_ptr<juce::AudioParameterFloat> makeTimeParameter (const char* id, const char* name,
                                                              float minSeconds, float defaultSeconds)
{
    // Rango con "skew": la mitad del recorrido de la perilla cubre de minSeconds a 0.5 s,
    // donde se hacen casi todos los ajustes finos. Un rango lineal de 0 a 10 s haría
    // imposible ajustar un attack de 5 ms.
    juce::NormalisableRange<float> range (minSeconds, 10.0f);
    range.setSkewForCentre (0.5f);

    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, undertow::params::versionHint }, name, range, defaultSeconds,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (timeToText)
            .withValueFromStringFunction (textToTime));
}

juce::AudioParameterFloatAttributes percentAttributes()
{
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })
        .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue() / 100.0f; });
}
} // namespace

UndertowAudioProcessor::UndertowAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "UndertowState", createParameterLayout())
{
    namespace id = undertow::params;
    attackParam = parameters.getRawParameterValue (id::attack);
    decayParam = parameters.getRawParameterValue (id::decay);
    sustainParam = parameters.getRawParameterValue (id::sustain);
    releaseParam = parameters.getRawParameterValue (id::release);
    voicesParam = parameters.getRawParameterValue (id::voices);
    velocityParam = parameters.getRawParameterValue (id::velocity);
    masterParam = parameters.getRawParameterValue (id::master);
    oscAWavetableParam = parameters.getRawParameterValue (id::oscAWavetable);
    oscAPositionParam = parameters.getRawParameterValue (id::oscAPosition);
}

juce::AudioProcessorValueTreeState::ParameterLayout UndertowAudioProcessor::createParameterLayout()
{
    namespace id = undertow::params;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Attack y decay pueden ser de 1 ms (ataques percusivos). El release mínimo es de 5 ms:
    // soltar una nota grave con un release más corto corta la onda a mitad de ciclo y se oye un clic.
    layout.add (makeTimeParameter (id::attack, "Attack", 0.001f, 0.005f));
    layout.add (makeTimeParameter (id::decay, "Decay", 0.001f, 0.5f));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::sustain, id::versionHint }, "Sustain",
                                                             juce::NormalisableRange<float> (0.0f, 1.0f), 0.8f,
                                                             percentAttributes()));
    layout.add (makeTimeParameter (id::release, "Release", 0.005f, 0.15f));

    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { id::voices, id::versionHint }, "Voices", 1,
                                                           undertow::synth::VoiceManager::maxPolyphony, 8));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::velocity, id::versionHint },
                                                             "Velocity Sens", juce::NormalisableRange<float> (0.0f, 1.0f),
                                                             0.5f, percentAttributes()));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::master, id::versionHint }, "Master", juce::NormalisableRange<float> (minusInfinityDb, 6.0f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int) {
            return v <= minusInfinityDb ? juce::String ("-inf dB") : juce::String (v, 1) + " dB";
        })));

    // Oscilador A. Por defecto "Basic Shapes" en posición 0 = seno: el sonido de la Fase 2.
    juce::StringArray tableNames;
    for (const auto* name : undertow::synth::WavetableBank::names)
        tableNames.add (name);

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { id::oscAWavetable, id::versionHintOscillator }, "Osc A Wavetable", tableNames, 0));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::oscAPosition, id::versionHintOscillator }, "Osc A Position",
        juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, percentAttributes()));

    return layout;
}

void UndertowAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    voiceManager.prepare (sampleRate);
    updateVoiceParameters();

    masterGain.reset (sampleRate, masterSmoothingSeconds);
    masterGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (masterParam->load(), minusInfinityDb));
}

void UndertowAudioProcessor::releaseResources() {}

bool UndertowAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo();
}

void UndertowAudioProcessor::updateVoiceParameters() noexcept
{
    // Los tiempos del ADSR no necesitan SmoothedValue: cambiar un tiempo cambia la VELOCIDAD de la
    // curva, no provoca un salto de nivel. El sustain sí es un nivel y la envolvente ya lo suaviza.
    voiceManager.setEnvelopeParameters ({ attackParam->load(), decayParam->load(), sustainParam->load(),
                                          releaseParam->load() });
    voiceManager.setPolyphony (static_cast<int> (voicesParam->load()));
    voiceManager.setVelocitySensitivity (velocityParam->load());

    // Cambiar de tabla es solo cambiar un puntero a datos que ya existen: nada se reserva aquí.
    voiceManager.setWavetable (&wavetableBank->get (static_cast<int> (oscAWavetableParam->load())));
    voiceManager.setWavetablePosition (oscAPositionParam->load());
}

void UndertowAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    buffer.clear();

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numChannels == 0)
        return;

    updateVoiceParameters();

    // Todas las voces se suman en el canal 0 (en esta fase el sinte es mono) y luego se copia al resto.
    float* mono = buffer.getWritePointer (0);

    // Renderizado "sample-accurate": se genera audio hasta la posición de cada evento MIDI,
    // se aplica el evento y se sigue. El timing no depende del tamaño de buffer.
    int position = 0;

    for (const auto metadata : midiMessages)
    {
        const int eventPosition = juce::jlimit (0, numSamples, metadata.samplePosition);
        voiceManager.render (mono + position, eventPosition - position);
        position = eventPosition;

        // Solo mensajes cortos (notas, CC): un SysEx largo haría que MidiMessage reserve memoria.
        if (metadata.numBytes <= 3)
            handleMidiMessage (metadata.getMessage());
    }

    voiceManager.render (mono + position, numSamples - position);

    masterGain.setTargetValue (juce::Decibels::decibelsToGain (masterParam->load(), minusInfinityDb));
    masterGain.applyGain (mono, numSamples);

    for (int channel = 1; channel < numChannels; ++channel)
        buffer.copyFrom (channel, 0, buffer, 0, 0, numSamples);

    activeVoiceCount.store (voiceManager.getNumActiveVoices(), std::memory_order_relaxed);
}

void UndertowAudioProcessor::handleMidiMessage (const juce::MidiMessage& message) noexcept
{
    if (message.isNoteOn())
        voiceManager.noteOn (message.getNoteNumber(), message.getFloatVelocity());
    else if (message.isNoteOff()) // incluye note-on con velocity 0, que muchos teclados envían como note-off
        voiceManager.noteOff (message.getNoteNumber());
    else if (message.isSustainPedalOn())
        voiceManager.setSustainPedal (true);
    else if (message.isSustainPedalOff())
        voiceManager.setSustainPedal (false);
    else if (message.isAllSoundOff())
        voiceManager.killAll();
    else if (message.isAllNotesOff())
        voiceManager.releaseAll();
}

void UndertowAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Se ejecuta en el hilo de mensajes (al guardar el proyecto de FL), no en el de audio.
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void UndertowAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
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
