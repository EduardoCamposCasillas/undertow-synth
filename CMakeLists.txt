cmake_minimum_required(VERSION 3.22)

project(UndertowSynth VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- JUCE vía FetchContent -----------------------------------------------------
# Se fija a un tag (no a una rama) para que el build sea reproducible:
# el mismo CMakeLists.txt siempre descarga exactamente el mismo JUCE.
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.2
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(JUCE)

# --- Plugin --------------------------------------------------------------------
juce_add_plugin(UndertowSynth
    PRODUCT_NAME             "Undertow Synth"
    COMPANY_NAME             "Undertow"
    PLUGIN_MANUFACTURER_CODE Undw          # 4 caracteres, al menos una mayúscula
    PLUGIN_CODE              Uwt1          # único por plugin de este fabricante
    FORMATS                  VST3 Standalone
    IS_SYNTH                 TRUE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD  FALSE         # copiar a Common Files requiere administrador
    VST3_CATEGORIES          Instrument Synth
)

target_sources(UndertowSynth
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

target_include_directories(UndertowSynth PRIVATE Source)

target_compile_definitions(UndertowSynth
    PUBLIC
        JUCE_WEB_BROWSER=0             # no usamos WebView; evita dependencias extra
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0   # no existe una versión VST2 que reemplazar
)

target_link_libraries(UndertowSynth
    PRIVATE
        juce::juce_audio_utils         # incluye audio_processors, audio_devices, gui, etc.
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
