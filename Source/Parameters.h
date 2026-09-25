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

// Versión con la que se introdujeron los parámetros (la usa VST3 para compatibilidad hacia atrás).
inline constexpr int versionHint = 1;
// Los parámetros añadidos en una versión posterior llevan un número mayor.
inline constexpr int versionHintOscillator = 2;
inline constexpr int versionHintFilter = 3;
inline constexpr int versionHintModulation = 4;
} // namespace undertow::params
