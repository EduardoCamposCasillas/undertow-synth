#pragma once

#include <array>
#include <cstddef>

#include "dsp/AdsrEnvelope.h"
#include "dsp/Lfo.h"

namespace undertow::synth
{

// --- Matriz de modulación -----------------------------------------------------------------------
//
// Una "ruta" (slot) conecta una FUENTE (algo que cambia solo: envolvente, LFO, velocity, rueda de
// modulación…) con un DESTINO (un parámetro del sonido) con una cantidad (amount, -100 %..+100 %).
// Cada voz calcula en cada muestra:   destino = perilla + Σ (fuente × amount)   de todas las rutas.
//
// Convención de escala: amount 100 % con la fuente al máximo mueve el destino TODO el recorrido de su
// perilla. Así "50 %" significa lo mismo en cualquier destino: media perilla. (Pitch y Volume, que no
// tienen perilla propia, llevan su escala anotada abajo.)
//
// Los ÓRDENES de las listas siguientes son parte del estado guardado (los parámetros guardan el índice):
// solo se pueden añadir elementos al final.

enum class ModSource { none, env1, env2, env3, lfo1, lfo2, velocity, key, modWheel, aftertouch };

inline constexpr std::array<const char*, 10> modSourceNames {
    "None",
    "Env 1 (Amp)", // la envolvente de amplitud, 0..1
    "Env 2",       // 0..1
    "Env 3",       // 0..1
    "LFO 1",       // bipolar, -1..1
    "LFO 2",       // bipolar, -1..1
    "Velocity",    // 0..1, fuerza con la que se tocó la nota
    "Key",         // bipolar: 0 en C5 (MIDI 60), ±1 a ±5 octavas
    "Mod Wheel",   // CC 1, 0..1
    "Aftertouch",  // presión del canal, 0..1
};

enum class ModDestination
{
    none, oscAPosition, oscAPitch, filterCutoff, filterResonance, filterDrive, volume,
    // Fase 6
    oscBPosition, oscBPitch, oscALevel, oscBLevel, subLevel, noiseLevel, oscADetune, oscBDetune, globalPitch,
    // Fase 7
    oscAWarp, oscBWarp, oscAFm, oscBFm
};

inline constexpr std::array<const char*, 20> modDestinationNames {
    "None",
    "Osc A Position",
    "Osc A Pitch",
    "Filter Cutoff",
    "Filter Resonance",
    "Filter Drive",
    "Volume",
    "Osc B Position",
    "Osc B Pitch",
    "Osc A Level",
    "Osc B Level",
    "Sub Level",
    "Noise Level",
    "Osc A Detune",
    "Osc B Detune",
    "Global Pitch", // mueve a la vez Osc A, Osc B y Sub (la escala de los Pitch: 100 % = ±24 semitonos)
    "Osc A Warp",   // amount del warp (100 % = toda la perilla)
    "Osc B Warp",
    "Osc A FM/RM",  // amount de la FM o del ring mod (100 % = toda la perilla)
    "Osc B FM/RM",
};

inline constexpr int numModSources = static_cast<int> (modSourceNames.size());
inline constexpr int numModDestinations = static_cast<int> (modDestinationNames.size());
inline constexpr int numModSlots = 8;
inline constexpr int numLfos = 2;

// Escalas de los destinos que no tienen una perilla de 0..1.
// Pitch: 100 % = ±24 semitonos (2 octavas), se suma a la afinación (Octave/Semi/Fine) del oscilador.
inline constexpr float pitchModulationSemitones = 24.0f;
// Cutoff: la perilla recorre 20 Hz – 20 kHz ≈ 10 octavas. 100 % = 10 octavas exactas: cada 10 % = 1 octava.
inline constexpr float cutoffModulationOctaves = 10.0f;
// Key: 60 semitonos (5 octavas) por unidad. Con Key → Cutoff al 50 % el cutoff sube 1 octava por octava:
// exactamente el key tracking del 100 % de la Fase 4.
inline constexpr float keySemitonesPerUnit = 60.0f;

struct ModSlot
{
    ModSource source = ModSource::none;
    ModDestination destination = ModDestination::none;
    float amount = 0.0f; // -1..1

    [[nodiscard]] bool isRouted() const noexcept { return source != ModSource::none && destination != ModDestination::none; }
    bool operator== (const ModSlot&) const = default;
};

// --- LFO ----------------------------------------------------------------------------------------

enum class LfoMode
{
    free,      // un reloj común para todas las voces (y ligado al compás si está sincronizado)
    retrigger, // cada nota reinicia su LFO en la fase 0
    oneShot,   // como retrigger, pero hace un solo ciclo y se detiene: una "envolvente dibujada"
};

inline constexpr std::array<const char*, 6> lfoShapeNames { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Sample & Hold" };
inline constexpr std::array<const char*, 3> lfoModeNames { "Free", "Retrigger", "One Shot" };

// Divisiones de tempo. 'beats' = duración de un ciclo en negras (compás de 4/4).
// T = tresillo (2/3 de la duración), D = con puntillo (3/2 de la duración).
struct LfoDivision
{
    const char* name;
    double beats;
};

inline constexpr std::array<LfoDivision, 17> lfoDivisions { {
    { "8 bars", 32.0 },
    { "4 bars", 16.0 },
    { "2 bars", 8.0 },
    { "1 bar", 4.0 },
    { "1/2", 2.0 },
    { "1/4", 1.0 },
    { "1/8", 0.5 },
    { "1/16", 0.25 },
    { "1/32", 0.125 },
    { "1/2 T", 4.0 / 3.0 },
    { "1/4 T", 2.0 / 3.0 },
    { "1/8 T", 1.0 / 3.0 },
    { "1/16 T", 1.0 / 6.0 },
    { "1/2 D", 3.0 },
    { "1/4 D", 1.5 },
    { "1/8 D", 0.75 },
    { "1/16 D", 0.375 },
} };

inline constexpr int defaultLfoDivision = 5; // 1/4

struct LfoSettings
{
    dsp::LfoShape shape = dsp::LfoShape::sine;
    LfoMode mode = LfoMode::retrigger;
    bool tempoSync = true;
    float rateHz = 2.0f;
    int division = defaultLfoDivision;

    bool operator== (const LfoSettings&) const = default;
};

// --- Todo junto -----------------------------------------------------------------------------------

struct ModulationSettings
{
    // Envolventes de modulación: por defecto sustain 0 %, una "caída" que se oye en cuanto se conectan.
    dsp::AdsrParameters envelope2 { 0.005f, 0.5f, 0.0f, 0.15f };
    dsp::AdsrParameters envelope3 { 0.005f, 0.5f, 0.0f, 0.15f };
    std::array<LfoSettings, numLfos> lfos;
    std::array<ModSlot, numModSlots> slots;

    bool operator== (const ModulationSettings&) const = default;
};

// Tempo y posición de la canción que informa el host (FL). Sin host (Standalone) se usa 120 BPM.
struct Transport
{
    double bpm = 120.0;
    double ppqPosition = 0.0; // posición en negras desde el inicio de la canción
    bool isPlaying = false;
    bool hasPosition = false;
};

} // namespace undertow::synth
