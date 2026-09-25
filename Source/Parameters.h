#pragma once

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

// Versión con la que se introdujeron los parámetros (la usa VST3 para compatibilidad hacia atrás).
inline constexpr int versionHint = 1;
// Los parámetros añadidos en una versión posterior llevan un número mayor.
inline constexpr int versionHintOscillator = 2;
} // namespace undertow::params
