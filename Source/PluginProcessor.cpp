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

std::unique_ptr<juce::AudioParameterFloat> makeTimeParameter (const char* id, const juce::String& name, float minSeconds,
                                                              float defaultSeconds, int version = undertow::params::versionHint)
{
    // Rango con "skew": la mitad del recorrido de la perilla cubre de minSeconds a 0.5 s,
    // donde se hacen casi todos los ajustes finos. Un rango lineal de 0 a 10 s haría
    // imposible ajustar un attack de 5 ms.
    juce::NormalisableRange<float> range (minSeconds, 10.0f);
    range.setSkewForCentre (0.5f);

    return std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id, version }, name, range, defaultSeconds,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction (timeToText)
            .withValueFromStringFunction (textToTime));
}

// Frecuencias como "850 Hz" o "2.40 kHz".
juce::String frequencyToText (float hz, int /*maxLength*/)
{
    if (hz < 1000.0f)
        return juce::String (juce::roundToInt (hz)) + " Hz";
    return juce::String (hz / 1000.0f, hz < 10000.0f ? 2 : 1) + " kHz";
}

// Acepta "850", "850 Hz", "2.4k" o "2.4 kHz".
float textToFrequency (const juce::String& text)
{
    const auto trimmed = text.trim().toLowerCase();
    const float value = trimmed.getFloatValue();
    return trimmed.containsChar ('k') ? value * 1000.0f : value;
}

juce::AudioParameterFloatAttributes percentAttributes()
{
    return juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })
        .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue() / 100.0f; });
}

// Rango logarítmico exacto: cada octava ocupa el mismo recorrido de la perilla.
juce::NormalisableRange<float> logRange (float minValue, float maxValue)
{
    const float ratio = maxValue / minValue;
    return { minValue, maxValue,
             [=] (float, float, float normalised) { return minValue * std::pow (ratio, normalised); },
             [=] (float, float, float value) { return std::log (value / minValue) / std::log (ratio); },
             [] (float start, float end, float value) { return juce::jlimit (start, end, value); } };
}

// Paneo como "L 30", "C" o "R 100" (porcentaje hacia cada lado).
juce::String panToText (float pan, int /*maxLength*/)
{
    const int percent = juce::roundToInt (pan * 100.0f);
    if (percent == 0)
        return "C";
    return (percent < 0 ? "L " : "R ") + juce::String (std::abs (percent));
}

// Acepta "L 30", "R 100", "C" o un número de -100 a 100.
float textToPan (const juce::String& text)
{
    const auto trimmed = text.trim().toUpperCase();
    if (trimmed.startsWith ("C"))
        return 0.0f;
    const float value = trimmed.trimCharactersAtStart ("LR ").getFloatValue() / 100.0f;
    return trimmed.startsWith ("L") ? -value : value;
}

juce::String signedText (int value, const juce::String& unit)
{
    return (value > 0 ? "+" : "") + juce::String (value) + unit;
}

template <size_t N>
juce::StringArray toStringArray (const std::array<const char*, N>& names)
{
    juce::StringArray result;
    for (const char* name : names)
        result.add (name);
    return result;
}

template <typename Enum>
Enum choiceToEnum (const std::atomic<float>* parameter) noexcept
{
    return static_cast<Enum> (static_cast<int> (parameter->load()));
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
    for (size_t o = 0; o < oscillatorParams.size(); ++o)
    {
        const auto& ids = id::oscillators[o];
        const auto get = [this] (const char* parameterId) { return parameters.getRawParameterValue (parameterId); };
        oscillatorParams[o] = { get (ids.on),     get (ids.wavetable), get (ids.position), get (ids.octave),
                                get (ids.semitones), get (ids.fine),   get (ids.level),    get (ids.pan),
                                get (ids.unison), get (ids.detune),    get (ids.width) };
    }
    for (size_t o = 0; o < oscillatorWarpParams.size(); ++o)
    {
        const auto& ids = id::oscillatorWarps[o];
        oscillatorWarpParams[o] = { parameters.getRawParameterValue (ids.warpMode), parameters.getRawParameterValue (ids.warpAmount),
                                    parameters.getRawParameterValue (ids.fmMode), parameters.getRawParameterValue (ids.fmAmount) };
    }
    subOnParam = parameters.getRawParameterValue (id::subOn);
    subShapeParam = parameters.getRawParameterValue (id::subShape);
    subOctaveParam = parameters.getRawParameterValue (id::subOctave);
    subLevelParam = parameters.getRawParameterValue (id::subLevel);
    noiseOnParam = parameters.getRawParameterValue (id::noiseOn);
    noiseLevelParam = parameters.getRawParameterValue (id::noiseLevel);
    noiseColorParam = parameters.getRawParameterValue (id::noiseColor);
    filterOnParam = parameters.getRawParameterValue (id::filter1On);
    filterTypeParam = parameters.getRawParameterValue (id::filter1Type);
    filterSlopeParam = parameters.getRawParameterValue (id::filter1Slope);
    filterCutoffParam = parameters.getRawParameterValue (id::filter1Cutoff);
    filterResonanceParam = parameters.getRawParameterValue (id::filter1Resonance);
    filterDriveParam = parameters.getRawParameterValue (id::filter1Drive);
    filterKeyTrackParam = parameters.getRawParameterValue (id::filter1KeyTrack);

    for (size_t e = 0; e < modEnvelopeParams.size(); ++e)
    {
        const auto& ids = id::modEnvelopes[e];
        modEnvelopeParams[e] = { parameters.getRawParameterValue (ids.attack), parameters.getRawParameterValue (ids.decay),
                                 parameters.getRawParameterValue (ids.sustain), parameters.getRawParameterValue (ids.release) };
    }
    for (size_t l = 0; l < lfoParams.size(); ++l)
    {
        const auto& ids = id::lfos[l];
        lfoParams[l] = { parameters.getRawParameterValue (ids.shape), parameters.getRawParameterValue (ids.mode),
                         parameters.getRawParameterValue (ids.sync), parameters.getRawParameterValue (ids.rate),
                         parameters.getRawParameterValue (ids.division) };
    }
    for (size_t s = 0; s < modSlotParams.size(); ++s)
    {
        const auto& ids = id::modSlots[s];
        modSlotParams[s] = { parameters.getRawParameterValue (ids.source), parameters.getRawParameterValue (ids.destination),
                             parameters.getRawParameterValue (ids.amount) };
    }

    const auto get = [this] (const char* parameterId) { return parameters.getRawParameterValue (parameterId); };
    auto& fx = effectsParams;
    fx.distortionOn = get (id::distortionOn);
    fx.distortionMode = get (id::distortionMode);
    fx.distortionDrive = get (id::distortionDrive);
    fx.distortionTone = get (id::distortionTone);
    fx.distortionMix = get (id::distortionMix);
    fx.chorusOn = get (id::chorusOn);
    fx.chorusRate = get (id::chorusRate);
    fx.chorusDepth = get (id::chorusDepth);
    fx.chorusFeedback = get (id::chorusFeedback);
    fx.chorusMix = get (id::chorusMix);
    fx.delayOn = get (id::delayOn);
    fx.delaySync = get (id::delaySync);
    fx.delayTime = get (id::delayTime);
    fx.delayDivision = get (id::delayDivision);
    fx.delayFeedback = get (id::delayFeedback);
    fx.delayPingPong = get (id::delayPingPong);
    fx.delayTone = get (id::delayTone);
    fx.delayMix = get (id::delayMix);
    fx.reverbOn = get (id::reverbOn);
    fx.reverbSize = get (id::reverbSize);
    fx.reverbDecay = get (id::reverbDecay);
    fx.reverbDamping = get (id::reverbDamping);
    fx.reverbPreDelay = get (id::reverbPreDelay);
    fx.reverbMix = get (id::reverbMix);
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

    // Filtro 1. Apagado por defecto: un proyecto de la Fase 3 suena exactamente igual al abrirlo.
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::filter1On, id::versionHintFilter },
                                                            "Filter On", false));
    // El orden de las opciones debe coincidir con dsp::FilterType / FilterSlope y NO cambiar (se guarda el índice).
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { id::filter1Type, id::versionHintFilter }, "Filter Type",
        juce::StringArray { "Low Pass", "High Pass", "Band Pass" }, 0));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { id::filter1Slope, id::versionHintFilter }, "Filter Slope",
        juce::StringArray { "12 dB", "24 dB" }, 1));

    // Cutoff en escala logarítmica exacta: cada octava ocupa el mismo recorrido de la perilla
    // (de 20 Hz a 20 kHz hay 10 octavas: una por cada 10 %). Así el barrido se oye "parejo".
    constexpr float minCutoff = 20.0f;
    constexpr float cutoffRatio = 1000.0f; // 20 kHz / 20 Hz
    juce::NormalisableRange<float> cutoffRange (
        minCutoff, minCutoff * cutoffRatio,
        [] (float, float, float normalised) { return minCutoff * std::pow (cutoffRatio, normalised); },
        [] (float, float, float hz) { return std::log (hz / minCutoff) / std::log (cutoffRatio); },
        [] (float start, float end, float hz) { return juce::jlimit (start, end, hz); });
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::filter1Cutoff, id::versionHintFilter }, "Filter Cutoff", cutoffRange, 2000.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("Hz")
            .withStringFromValueFunction (frequencyToText)
            .withValueFromStringFunction (textToFrequency)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::filter1Resonance, id::versionHintFilter },
                                                             "Filter Resonance", juce::NormalisableRange<float> (0.0f, 1.0f),
                                                             0.0f, percentAttributes()));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::filter1Drive, id::versionHintFilter },
                                                             "Filter Drive", juce::NormalisableRange<float> (0.0f, 1.0f),
                                                             0.0f, percentAttributes()));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::filter1KeyTrack, id::versionHintFilter },
                                                             "Filter Key Track", juce::NormalisableRange<float> (0.0f, 1.0f),
                                                             0.0f, percentAttributes()));

    // --- Fase 5: modulación. Todas las rutas vacías por defecto: un proyecto de la Fase 4 suena igual. ---
    namespace synth = undertow::synth;
    constexpr int v5 = id::versionHintModulation;

    // Envolventes 2 y 3. Por defecto sustain 0 %: una "caída" que se oye en cuanto se conectan a algo.
    const synth::ModulationSettings defaults;
    for (size_t e = 0; e < id::modEnvelopes.size(); ++e)
    {
        const auto& ids = id::modEnvelopes[e];
        const auto& d = e == 0 ? defaults.envelope2 : defaults.envelope3;
        const juce::String name = "Env " + juce::String (static_cast<int> (e) + 2) + " ";
        layout.add (makeTimeParameter (ids.attack, name + "Attack", 0.001f, d.attackSeconds, v5));
        layout.add (makeTimeParameter (ids.decay, name + "Decay", 0.001f, d.decaySeconds, v5));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids.sustain, v5 }, name + "Sustain",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f), d.sustainLevel,
                                                                 percentAttributes()));
        layout.add (makeTimeParameter (ids.release, name + "Release", 0.005f, d.releaseSeconds, v5));
    }

    // LFOs. La velocidad libre va de 0.02 Hz (un ciclo cada 50 s) a 40 Hz, en escala logarítmica.
    juce::StringArray divisionNames;
    for (const auto& division : synth::lfoDivisions)
        divisionNames.add (division.name);

    const synth::LfoSettings lfoDefaults;
    for (size_t l = 0; l < id::lfos.size(); ++l)
    {
        const auto& ids = id::lfos[l];
        const juce::String name = "LFO " + juce::String (static_cast<int> (l) + 1) + " ";
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids.shape, v5 }, name + "Shape",
                                                                  toStringArray (synth::lfoShapeNames), 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids.mode, v5 }, name + "Mode",
                                                                  toStringArray (synth::lfoModeNames),
                                                                  static_cast<int> (lfoDefaults.mode)));
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ids.sync, v5 }, name + "Sync",
                                                                lfoDefaults.tempoSync));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ids.rate, v5 }, name + "Rate", logRange (0.02f, 40.0f), lfoDefaults.rateHz,
            juce::AudioParameterFloatAttributes()
                .withLabel ("Hz")
                .withStringFromValueFunction ([] (float hz, int) { return juce::String (hz, hz < 1.0f ? 3 : 2) + " Hz"; })));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids.division, v5 }, name + "Division",
                                                                  divisionNames, lfoDefaults.division));
    }

    // Matriz de modulación: 8 rutas fuente → destino con amount bipolar (-100 %..+100 %).
    for (size_t s = 0; s < id::modSlots.size(); ++s)
    {
        const auto& ids = id::modSlots[s];
        const juce::String name = "Mod " + juce::String (static_cast<int> (s) + 1) + " ";
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids.source, v5 }, name + "Source",
                                                                  toStringArray (synth::modSourceNames), 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids.destination, v5 },
                                                                  name + "Destination",
                                                                  toStringArray (synth::modDestinationNames), 0));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids.amount, v5 }, name + "Amount",
                                                                 juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f,
                                                                 percentAttributes()));
    }

    // --- Fase 6: osciladores A y B, sub y ruido. Por defecto solo suena Osc A sin unison: el sonido de la Fase 5. ---
    constexpr int v6 = id::versionHintSources;
    const synth::SourceSettings sourceDefaults;

    for (size_t o = 0; o < id::oscillators.size(); ++o)
    {
        const auto& ids = id::oscillators[o];
        const auto& d = sourceDefaults.oscillators[o];
        const juce::String name = juce::String ("Osc ") + (o == 0 ? "A " : "B ");

        // Osc A ya tiene Wavetable y Position desde la Fase 3 (creados arriba con su versionHint).
        if (o > 0)
        {
            layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids.wavetable, v6 },
                                                                      name + "Wavetable", tableNames, 0));
            layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids.position, v6 }, name + "Position",
                                                                     juce::NormalisableRange<float> (0.0f, 1.0f), d.position,
                                                                     percentAttributes()));
        }

        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ids.on, v6 }, name + "On", d.enabled));
        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { ids.octave, v6 }, name + "Octave", -4, 4, d.octave,
            juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return signedText (v, " oct"); })));
        layout.add (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { ids.semitones, v6 }, name + "Semi", -12, 12, d.semitones,
            juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return signedText (v, " st"); })));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ids.fine, v6 }, name + "Fine", juce::NormalisableRange<float> (-100.0f, 100.0f), d.fineCents,
            juce::AudioParameterFloatAttributes()
                .withLabel ("ct")
                .withStringFromValueFunction ([] (float v, int) { return signedText (juce::roundToInt (v), " ct"); })));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids.level, v6 }, name + "Level",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f), d.level,
                                                                 percentAttributes()));
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { ids.pan, v6 }, name + "Pan", juce::NormalisableRange<float> (-1.0f, 1.0f), d.pan,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (panToText).withValueFromStringFunction (textToPan)));
        layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { ids.unison, v6 }, name + "Unison", 1,
                                                               undertow::dsp::WavetableOscillator::maxUnison, d.unison));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids.detune, v6 }, name + "Detune",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f), d.detune,
                                                                 percentAttributes()));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids.width, v6 }, name + "Width",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f), d.width,
                                                                 percentAttributes()));
    }

    // Sub: el orden de las formas se guarda (índice): no cambiarlo.
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::subOn, v6 }, "Sub On",
                                                            sourceDefaults.sub.enabled));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::subShape, v6 }, "Sub Shape",
                                                              toStringArray (synth::subShapeNames),
                                                              static_cast<int> (sourceDefaults.sub.shape)));
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { id::subOctave, v6 }, "Sub Octave", -3, 0, sourceDefaults.sub.octave,
        juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return signedText (v, " oct"); })));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::subLevel, v6 }, "Sub Level",
                                                             juce::NormalisableRange<float> (0.0f, 1.0f),
                                                             sourceDefaults.sub.level, percentAttributes()));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id::noiseOn, v6 }, "Noise On",
                                                            sourceDefaults.noise.enabled));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::noiseLevel, v6 }, "Noise Level",
                                                             juce::NormalisableRange<float> (0.0f, 1.0f),
                                                             sourceDefaults.noise.level, percentAttributes()));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::noiseColor, v6 }, "Noise Color",
                                                             juce::NormalisableRange<float> (0.0f, 1.0f),
                                                             sourceDefaults.noise.color, percentAttributes()));

    // --- Fase 7: warp y FM/RM. Por defecto None/Off: un proyecto de la Fase 6 suena igual. ---
    // El orden de las opciones se guarda (índice): solo añadir al final.
    constexpr int v7 = id::versionHintWarp;
    for (size_t o = 0; o < id::oscillatorWarps.size(); ++o)
    {
        const auto& ids = id::oscillatorWarps[o];
        const auto& d = sourceDefaults.oscillators[o];
        const juce::String name = juce::String ("Osc ") + (o == 0 ? "A " : "B ");

        layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { ids.warpMode, v7 }, name + "Warp",
                                                                  toStringArray (undertow::dsp::warpModeNames),
                                                                  static_cast<int> (d.warpMode)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids.warpAmount, v7 }, name + "Warp Amount",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f), d.warpAmount,
                                                                 percentAttributes()));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ids.fmMode, v7 }, name + "FM/RM",
            toStringArray (o == 0 ? synth::fmModeNamesA : synth::fmModeNamesB), static_cast<int> (d.fmMode)));
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { ids.fmAmount, v7 }, name + "FM/RM Amount",
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f), d.fmAmount,
                                                                 percentAttributes()));
    }

    // --- Fase 8: efectos. Todos apagados por defecto: un proyecto de la Fase 7 suena igual. ---
    constexpr int v8 = id::versionHintEffects;
    const synth::EffectsSettings fxDefaults;
    const auto addPercent = [&layout] (const char* parameterId, const juce::String& name, float defaultValue) {
        layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { parameterId, v8 }, name,
                                                                 juce::NormalisableRange<float> (0.0f, 1.0f), defaultValue,
                                                                 percentAttributes()));
    };
    const auto addToggle = [&layout] (const char* parameterId, const juce::String& name, bool defaultValue) {
        layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { parameterId, v8 }, name, defaultValue));
    };
    const auto timeAttributes = [] {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction (timeToText).withValueFromStringFunction (textToTime);
    };

    // Distorsión. El orden de los modos se guarda (índice): solo añadir al final.
    const auto& dist = fxDefaults.distortion;
    addToggle (id::distortionOn, "Distortion On", dist.enabled);
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::distortionMode, v8 }, "Distortion Mode",
                                                              toStringArray (undertow::dsp::distortionModeNames),
                                                              static_cast<int> (dist.parameters.mode)));
    addPercent (id::distortionDrive, "Distortion Drive", dist.parameters.drive);
    addPercent (id::distortionTone, "Distortion Tone", dist.parameters.tone);
    addPercent (id::distortionMix, "Distortion Mix", dist.parameters.mix);

    // Chorus.
    const auto& chorus = fxDefaults.chorus;
    addToggle (id::chorusOn, "Chorus On", chorus.enabled);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::chorusRate, v8 }, "Chorus Rate", logRange (0.05f, 8.0f), chorus.parameters.rateHz,
        juce::AudioParameterFloatAttributes()
            .withLabel ("Hz")
            .withStringFromValueFunction ([] (float hz, int) { return juce::String (hz, hz < 1.0f ? 2 : 1) + " Hz"; })));
    addPercent (id::chorusDepth, "Chorus Depth", chorus.parameters.depth);
    addPercent (id::chorusFeedback, "Chorus Feedback", chorus.parameters.feedback);
    addPercent (id::chorusMix, "Chorus Mix", chorus.parameters.mix);

    // Delay: tiempo libre (1 ms a 2 s) o una división del compás. El orden de las divisiones se guarda.
    const auto& delay = fxDefaults.delay;
    addToggle (id::delayOn, "Delay On", delay.enabled);
    addToggle (id::delaySync, "Delay Sync", delay.tempoSync);
    juce::NormalisableRange<float> delayTimeRange (0.001f, 2.0f);
    delayTimeRange.setSkewForCentre (0.3f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::delayTime, v8 }, "Delay Time",
                                                             delayTimeRange, delay.timeSeconds, timeAttributes()));
    juce::StringArray delayDivisionNames;
    for (const auto& division : synth::delayDivisions)
        delayDivisionNames.add (division.name);
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id::delayDivision, v8 }, "Delay Division",
                                                              delayDivisionNames, delay.division));
    addPercent (id::delayFeedback, "Delay Feedback", delay.parameters.feedback);
    addToggle (id::delayPingPong, "Delay Ping-Pong", delay.parameters.pingPong);
    addPercent (id::delayTone, "Delay Tone", delay.parameters.tone);
    addPercent (id::delayMix, "Delay Mix", delay.parameters.mix);

    // Reverb.
    const auto& reverb = fxDefaults.reverb;
    addToggle (id::reverbOn, "Reverb On", reverb.enabled);
    addPercent (id::reverbSize, "Reverb Size", reverb.parameters.size);
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { id::reverbDecay, v8 }, "Reverb Decay",
        logRange (undertow::dsp::Reverb::minDecaySeconds, undertow::dsp::Reverb::maxDecaySeconds),
        reverb.parameters.decaySeconds, timeAttributes()));
    addPercent (id::reverbDamping, "Reverb Damping", reverb.parameters.damping);
    juce::NormalisableRange<float> preDelayRange (0.0f, static_cast<float> (undertow::dsp::Reverb::maxPreDelaySeconds));
    preDelayRange.setSkewForCentre (0.05f);
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id::reverbPreDelay, v8 }, "Reverb Pre-Delay",
                                                             preDelayRange, reverb.parameters.preDelaySeconds, timeAttributes()));
    addPercent (id::reverbMix, "Reverb Mix", reverb.parameters.mix);

    return layout;
}

void UndertowAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate.store (sampleRate, std::memory_order_relaxed);
    voiceManager.prepare (sampleRate);
    updateVoiceParameters();

    // Las líneas de retardo de los efectos se reservan aquí (fuera del hilo de audio).
    effects.prepare (sampleRate);
    effects.setSettings (readEffectsSettings(), hostBpm.load (std::memory_order_relaxed));
    effects.snapSwitches();

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

    voiceManager.setSourceSettings (readSourceSettings());

    // Cada voz suaviza el filtro por muestra, así que aquí basta con pasar los valores una vez por bloque.
    voiceManager.setFilterSettings (readFilterSettings());
    voiceManager.setModulationSettings (readModulationSettings());
}

undertow::synth::SourceSettings UndertowAudioProcessor::readSourceSettings() const noexcept
{
    namespace synth = undertow::synth;
    synth::SourceSettings settings;

    // Cambiar de tabla es solo cambiar un puntero a datos que ya existen: nada se reserva aquí.
    const auto tableAt = [this] (const std::atomic<float>* parameter) {
        return &wavetableBank->get (static_cast<int> (parameter->load()));
    };

    for (size_t o = 0; o < oscillatorParams.size(); ++o)
    {
        const auto& p = oscillatorParams[o];
        auto& osc = settings.oscillators[o];
        osc.enabled = p.on->load() >= 0.5f;
        osc.table = tableAt (p.wavetable);
        osc.position = p.position->load();
        osc.octave = static_cast<int> (p.octave->load());
        osc.semitones = static_cast<int> (p.semitones->load());
        osc.fineCents = p.fine->load();
        osc.level = p.level->load();
        osc.pan = p.pan->load();
        osc.unison = static_cast<int> (p.unison->load());
        osc.detune = p.detune->load();
        osc.width = p.width->load();

        const auto& w = oscillatorWarpParams[o];
        osc.warpMode = choiceToEnum<undertow::dsp::WarpMode> (w.warpMode);
        osc.warpAmount = w.warpAmount->load();
        osc.fmMode = choiceToEnum<synth::FmMode> (w.fmMode);
        osc.fmAmount = w.fmAmount->load();
    }

    settings.sub = { subOnParam->load() >= 0.5f, choiceToEnum<synth::SubShape> (subShapeParam),
                     static_cast<int> (subOctaveParam->load()), subLevelParam->load() };
    settings.noise = { noiseOnParam->load() >= 0.5f, noiseLevelParam->load(), noiseColorParam->load() };
    settings.subTable = &wavetableBank->get (0); // Basic Shapes: seno, triángulo, sierra, cuadrada
    return settings;
}

undertow::synth::ModulationSettings UndertowAudioProcessor::readModulationSettings() const noexcept
{
    namespace synth = undertow::synth;
    synth::ModulationSettings settings;

    const auto readEnvelope = [] (const EnvelopeParams& p) {
        return undertow::dsp::AdsrParameters { p.attack->load(), p.decay->load(), p.sustain->load(), p.release->load() };
    };
    settings.envelope2 = readEnvelope (modEnvelopeParams[0]);
    settings.envelope3 = readEnvelope (modEnvelopeParams[1]);

    for (size_t l = 0; l < lfoParams.size(); ++l)
    {
        auto& lfo = settings.lfos[l];
        lfo.shape = choiceToEnum<undertow::dsp::LfoShape> (lfoParams[l].shape);
        lfo.mode = choiceToEnum<synth::LfoMode> (lfoParams[l].mode);
        lfo.tempoSync = lfoParams[l].sync->load() >= 0.5f;
        lfo.rateHz = lfoParams[l].rate->load();
        lfo.division = static_cast<int> (lfoParams[l].division->load());
    }

    for (size_t s = 0; s < modSlotParams.size(); ++s)
    {
        auto& slot = settings.slots[s];
        slot.source = choiceToEnum<synth::ModSource> (modSlotParams[s].source);
        slot.destination = choiceToEnum<synth::ModDestination> (modSlotParams[s].destination);
        slot.amount = modSlotParams[s].amount->load();
    }

    return settings;
}

void UndertowAudioProcessor::updateTransport() noexcept
{
    // El host informa el tempo y la posición de la canción. getPosition() está pensado para llamarse
    // desde processBlock: no reserva memoria ni bloquea.
    undertow::synth::Transport transport;
    if (auto* host = getPlayHead())
    {
        if (const auto position = host->getPosition())
        {
            if (const auto bpm = position->getBpm())
                transport.bpm = *bpm;
            if (const auto ppq = position->getPpqPosition())
            {
                transport.ppqPosition = *ppq;
                transport.hasPosition = true;
            }
            transport.isPlaying = position->getIsPlaying();
        }
    }
    voiceManager.setTransport (transport);
    hostBpm.store (transport.bpm, std::memory_order_relaxed);
}

undertow::synth::FilterSettings UndertowAudioProcessor::readFilterSettings() const noexcept
{
    undertow::synth::FilterSettings settings;
    settings.enabled = filterOnParam->load() >= 0.5f;
    settings.parameters.type = static_cast<undertow::dsp::FilterType> (static_cast<int> (filterTypeParam->load()));
    settings.parameters.slope = static_cast<undertow::dsp::FilterSlope> (static_cast<int> (filterSlopeParam->load()));
    settings.parameters.cutoffHz = filterCutoffParam->load();
    settings.parameters.resonance = filterResonanceParam->load();
    settings.parameters.drive = filterDriveParam->load();
    settings.keyTrack = filterKeyTrackParam->load();
    return settings;
}

undertow::synth::EffectsSettings UndertowAudioProcessor::readEffectsSettings() const noexcept
{
    const auto on = [] (const std::atomic<float>* parameter) { return parameter->load() >= 0.5f; };
    const auto& p = effectsParams;
    undertow::synth::EffectsSettings settings;

    settings.distortion.enabled = on (p.distortionOn);
    settings.distortion.parameters = { choiceToEnum<undertow::dsp::DistortionMode> (p.distortionMode), p.distortionDrive->load(),
                                       p.distortionTone->load(), p.distortionMix->load() };

    settings.chorus.enabled = on (p.chorusOn);
    settings.chorus.parameters = { p.chorusRate->load(), p.chorusDepth->load(), p.chorusFeedback->load(), p.chorusMix->load() };

    auto& delay = settings.delay;
    delay.enabled = on (p.delayOn);
    delay.tempoSync = on (p.delaySync);
    delay.timeSeconds = p.delayTime->load();
    delay.division = static_cast<int> (p.delayDivision->load());
    delay.parameters.feedback = p.delayFeedback->load();
    delay.parameters.pingPong = on (p.delayPingPong);
    delay.parameters.tone = p.delayTone->load();
    delay.parameters.mix = p.delayMix->load();

    settings.reverb.enabled = on (p.reverbOn);
    settings.reverb.parameters = { p.reverbSize->load(), p.reverbDecay->load(), p.reverbDamping->load(),
                                   p.reverbPreDelay->load(), p.reverbMix->load() };
    return settings;
}

double UndertowAudioProcessor::getTailLengthSeconds() const
{
    // Cuánto sigue sonando después de soltar la última nota: el host lo usa al exportar para no cortar la cola.
    // Release de la voz + la cola de los efectos hasta −60 dB.
    const auto fx = readEffectsSettings();
    double tail = releaseParam->load();
    if (fx.reverb.enabled)
        tail += fx.reverb.parameters.preDelaySeconds + fx.reverb.parameters.decaySeconds;
    if (fx.delay.enabled)
    {
        // Número de ecos hasta caer 60 dB: feedback^n = 10^−3.
        const double feedback = fx.delay.parameters.feedback * undertow::dsp::StereoDelay::maxFeedback;
        const double repeats = feedback > 0.001 ? std::min (200.0, -3.0 / std::log10 (feedback)) + 1.0 : 1.0;
        tail += repeats * fx.delay.resolvedSeconds (hostBpm.load (std::memory_order_relaxed));
    }
    return tail;
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
    updateTransport();

    // Las voces se suman directamente en los canales de salida. Con salida mono, cada voz suma (L + R) / 2.
    float* left = buffer.getWritePointer (0);
    float* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;
    const auto render = [&] (int start, int count) {
        voiceManager.render (left + start, right != nullptr ? right + start : nullptr, count);
    };

    // Renderizado "sample-accurate": se genera audio hasta la posición de cada evento MIDI,
    // se aplica el evento y se sigue. El timing no depende del tamaño de buffer.
    int position = 0;

    for (const auto metadata : midiMessages)
    {
        const int eventPosition = juce::jlimit (0, numSamples, metadata.samplePosition);
        render (position, eventPosition - position);
        position = eventPosition;

        // Solo mensajes cortos (notas, CC): un SysEx largo haría que MidiMessage reserve memoria.
        if (metadata.numBytes <= 3)
            handleMidiMessage (metadata.getMessage());
    }

    render (position, numSamples - position);

    // Efectos globales sobre la suma de las voces, antes del volumen master.
    effects.setSettings (readEffectsSettings(), hostBpm.load (std::memory_order_relaxed));
    effects.process (left, right, numSamples);

    // Un solo SmoothedValue para los dos canales: se avanza una vez por muestra y se aplica a ambos
    // (applyGain canal por canal avanzaría el suavizado dos veces).
    masterGain.setTargetValue (juce::Decibels::decibelsToGain (masterParam->load(), minusInfinityDb));
    for (int i = 0; i < numSamples; ++i)
    {
        const float gain = masterGain.getNextValue();
        left[i] *= gain;
        if (right != nullptr)
            right[i] *= gain;
    }

    activeVoiceCount.store (voiceManager.getNumActiveVoices(), std::memory_order_relaxed);
    for (size_t l = 0; l < lfoDisplayPhases.size(); ++l)
        lfoDisplayPhases[l].store (static_cast<float> (voiceManager.getLfoDisplayPhase (static_cast<int> (l))),
                                   std::memory_order_relaxed);
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
    else if (message.isControllerOfType (1)) // CC 1: rueda de modulación
        voiceManager.setModWheel (static_cast<float> (message.getControllerValue()) / 127.0f);
    else if (message.isChannelPressure())
        voiceManager.setAftertouch (static_cast<float> (message.getChannelPressureValue()) / 127.0f);
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
