#pragma once

#include <array>

// IDs de los parámetros. Son la "llave" con la que se guardan en el proyecto de FL y en los presets:
// una vez publicados NO deben cambiar, o los proyectos antiguos perderían sus valores.
namespace undertow::params
{
inline constexpr const char* attack = "attack";
inline constexpr const char* decay = "decay";
inline constexpr const char* sustain = "sustain";
inline constexpr const char* release = "release";
inline constexpr const char* voices = "voices";
inline constexpr const char* velocity = "velocity";
inline constexpr const char* master = "master";

// Fase 3: oscilador A. Llevan el prefijo "oscA" porque en la Fase 6 llegará un oscilador B.
inline constexpr const char* oscAWavetable = "oscAWavetable";
inline constexpr const char* oscAPosition = "oscAPosition";

// Fase 4: filtro 1. Llevan el número porque más adelante puede llegar un segundo filtro.
inline constexpr const char* filter1On = "filter1On";
inline constexpr const char* filter1Type = "filter1Type";
inline constexpr const char* filter1Slope = "filter1Slope";
inline constexpr const char* filter1Cutoff = "filter1Cutoff";
inline constexpr const char* filter1Resonance = "filter1Resonance";
inline constexpr const char* filter1Drive = "filter1Drive";
inline constexpr const char* filter1KeyTrack = "filter1KeyTrack";

// Fase 5: modulación. Se escriben uno a uno (y no generados con un bucle) para que se vea que son fijos.
struct EnvelopeIds
{
    const char* attack;
    const char* decay;
    const char* sustain;
    const char* release;
};

inline constexpr std::array<EnvelopeIds, 2> modEnvelopes { {
    { "env2Attack", "env2Decay", "env2Sustain", "env2Release" },
    { "env3Attack", "env3Decay", "env3Sustain", "env3Release" },
} };

struct LfoIds
{
    const char* shape;
    const char* mode;
    const char* sync;
    const char* rate;
    const char* division;
};

inline constexpr std::array<LfoIds, 2> lfos { {
    { "lfo1Shape", "lfo1Mode", "lfo1Sync", "lfo1Rate", "lfo1Division" },
    { "lfo2Shape", "lfo2Mode", "lfo2Sync", "lfo2Rate", "lfo2Division" },
} };

struct ModSlotIds
{
    const char* source;
    const char* destination;
    const char* amount;
};

inline constexpr std::array<ModSlotIds, 8> modSlots { {
    { "mod1Source", "mod1Destination", "mod1Amount" },
    { "mod2Source", "mod2Destination", "mod2Amount" },
    { "mod3Source", "mod3Destination", "mod3Amount" },
    { "mod4Source", "mod4Destination", "mod4Amount" },
    { "mod5Source", "mod5Destination", "mod5Amount" },
    { "mod6Source", "mod6Destination", "mod6Amount" },
    { "mod7Source", "mod7Destination", "mod7Amount" },
    { "mod8Source", "mod8Destination", "mod8Amount" },
} };

// Fase 6: osciladores A y B, sub y ruido. Osc A conserva los IDs de la Fase 3 para Wavetable y Position.
struct OscillatorIds
{
    const char* on;
    const char* wavetable;
    const char* position;
    const char* octave;
    const char* semitones;
    const char* fine;
    const char* level;
    const char* pan;
    const char* unison;
    const char* detune;
    const char* width;
};

inline constexpr std::array<OscillatorIds, 2> oscillators { {
    { "oscAOn", oscAWavetable, oscAPosition, "oscAOctave", "oscASemi", "oscAFine", "oscALevel", "oscAPan", "oscAUnison",
      "oscADetune", "oscAWidth" },
    { "oscBOn", "oscBWavetable", "oscBPosition", "oscBOctave", "oscBSemi", "oscBFine", "oscBLevel", "oscBPan", "oscBUnison",
      "oscBDetune", "oscBWidth" },
} };

inline constexpr const char* subOn = "subOn";
inline constexpr const char* subShape = "subShape";
inline constexpr const char* subOctave = "subOctave";
inline constexpr const char* subLevel = "subLevel";

inline constexpr const char* noiseOn = "noiseOn";
inline constexpr const char* noiseLevel = "noiseLevel";
inline constexpr const char* noiseColor = "noiseColor";

// Fase 7: warp y FM/RM de cada oscilador.
struct OscillatorWarpIds
{
    const char* warpMode;
    const char* warpAmount;
    const char* fmMode;
    const char* fmAmount;
};

inline constexpr std::array<OscillatorWarpIds, 2> oscillatorWarps { {
    { "oscAWarpMode", "oscAWarpAmount", "oscAFmMode", "oscAFmAmount" },
    { "oscBWarpMode", "oscBWarpAmount", "oscBFmMode", "oscBFmAmount" },
} };

// Fase 8: efectos globales (distorsión → chorus → delay → reverb).
inline constexpr const char* distortionOn = "fxDistOn";
inline constexpr const char* distortionMode = "fxDistMode";
inline constexpr const char* distortionDrive = "fxDistDrive";
inline constexpr const char* distortionTone = "fxDistTone";
inline constexpr const char* distortionMix = "fxDistMix";

inline constexpr const char* chorusOn = "fxChorusOn";
inline constexpr const char* chorusRate = "fxChorusRate";
inline constexpr const char* chorusDepth = "fxChorusDepth";
inline constexpr const char* chorusFeedback = "fxChorusFeedback";
inline constexpr const char* chorusMix = "fxChorusMix";

inline constexpr const char* delayOn = "fxDelayOn";
inline constexpr const char* delaySync = "fxDelaySync";
inline constexpr const char* delayTime = "fxDelayTime";
inline constexpr const char* delayDivision = "fxDelayDivision";
inline constexpr const char* delayFeedback = "fxDelayFeedback";
inline constexpr const char* delayPingPong = "fxDelayPingPong";
inline constexpr const char* delayTone = "fxDelayTone";
inline constexpr const char* delayMix = "fxDelayMix";

inline constexpr const char* reverbOn = "fxReverbOn";
inline constexpr const char* reverbSize = "fxReverbSize";
inline constexpr const char* reverbDecay = "fxReverbDecay";
inline constexpr const char* reverbDamping = "fxReverbDamping";
inline constexpr const char* reverbPreDelay = "fxReverbPreDelay";
inline constexpr const char* reverbMix = "fxReverbMix";

// Versión con la que se introdujeron los parámetros (la usa VST3 para compatibilidad hacia atrás).
inline constexpr int versionHint = 1;
// Los parámetros añadidos en una versión posterior llevan un número mayor.
inline constexpr int versionHintOscillator = 2;
inline constexpr int versionHintFilter = 3;
inline constexpr int versionHintModulation = 4;
inline constexpr int versionHintSources = 5;
inline constexpr int versionHintWarp = 6;
inline constexpr int versionHintEffects = 7;
} // namespace undertow::params
