#pragma once

#include <array>
#include <atomic>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "gui/FilterResponseDisplay.h"
#include "gui/LfoDisplay.h"
#include "gui/WavetableDisplay.h"
#include "synth/Modulation.h"
#include "synth/SourceSettings.h"

class UndertowAudioProcessor;

// GUI funcional: osciladores, sub, ruido, filtro, envolventes, LFOs, matriz de modulación y voz.
// La GUI profesional (escalable, arrastrar para modular) llega en la Fase 9.
class UndertowAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit UndertowAudioProcessorEditor (UndertowAudioProcessor&);
    ~UndertowAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showPage (int page);

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<SliderAttachment> attachment;
    };

    // Crea la perilla, su etiqueta y su attachment con el parámetro.
    void setUpKnob (Knob& knob, juce::Component& page, const char* parameterId, const juce::String& name, int width);
    // Rellena las opciones desde el propio parámetro (así la GUI nunca se desincroniza de la lista guardada).
    void setUpComboBox (juce::ComboBox& box, juce::Component& page, const char* parameterId,
                        std::unique_ptr<ComboBoxAttachment>& attachment);
    static void layoutKnob (Knob& knob, juce::Rectangle<int> column);

    static constexpr size_t numEnvelopeKnobs = 4;
    static constexpr size_t numVoiceKnobs = 3;

    UndertowAudioProcessor& processor;

    // Tres páginas del mismo tamaño que la ventana de la Fase 4 (cabe en pantallas pequeñas con escalado).
    juce::Component oscillatorPage, soundPage, modulationPage;
    juce::TextButton oscillatorTab { "Osciladores" };
    juce::TextButton soundTab { "Filtro y Amp" };
    juce::TextButton modulationTab;
    std::array<Knob, numEnvelopeKnobs + numVoiceKnobs> knobs;

    // --- Fase 6: osciladores A y B, sub y ruido ---
    static constexpr size_t numOscillatorKnobs = 9; // Position, Octave, Semi, Fine, Level, Pan, Unison, Detune, Width

    struct OscillatorControls
    {
        juce::GroupComponent group;
        juce::ToggleButton onButton { "On" };
        std::unique_ptr<ButtonAttachment> onAttachment;
        juce::ComboBox tableBox;
        std::unique_ptr<ComboBoxAttachment> tableAttachment;
        std::array<Knob, numOscillatorKnobs> knobs;
        undertow::gui::WavetableDisplay display;
        std::atomic<float>* tableParam = nullptr;
        std::atomic<float>* positionParam = nullptr;
    };
    std::array<OscillatorControls, undertow::synth::numOscillators> oscillators;

    juce::GroupComponent subGroup { {}, "Sub" };
    juce::ToggleButton subOnButton { "On" };
    std::unique_ptr<ButtonAttachment> subOnAttachment;
    juce::ComboBox subShapeBox;
    std::unique_ptr<ComboBoxAttachment> subShapeAttachment;
    std::array<Knob, 2> subKnobs;

    juce::GroupComponent noiseGroup { {}, "Ruido" };
    juce::ToggleButton noiseOnButton { "On" };
    std::unique_ptr<ButtonAttachment> noiseOnAttachment;
    std::array<Knob, 2> noiseKnobs;

    static constexpr size_t numFilterKnobs = 4;

    juce::GroupComponent filterGroup { {}, "Filtro" };
    juce::ToggleButton filterOnButton { "On" };
    std::unique_ptr<ButtonAttachment> filterOnAttachment;
    juce::ComboBox filterTypeBox;
    std::unique_ptr<ComboBoxAttachment> filterTypeAttachment;
    juce::ComboBox filterSlopeBox;
    std::unique_ptr<ComboBoxAttachment> filterSlopeAttachment;
    std::array<Knob, numFilterKnobs> filterKnobs;
    undertow::gui::FilterResponseDisplay filterDisplay;

    juce::GroupComponent envelopeGroup { {}, "Env 1 (amplitud)" };
    juce::GroupComponent voiceGroup { {}, "Voz" };
    juce::Label activeVoicesLabel;
    int lastShownVoiceCount = -1;

    // --- Fase 5: modulación ---
    struct ModEnvelopeControls
    {
        juce::GroupComponent group;
        std::array<Knob, 4> knobs;
    };
    std::array<ModEnvelopeControls, 2> modEnvelopes;

    struct LfoControls
    {
        juce::GroupComponent group;
        juce::ComboBox shapeBox, modeBox, divisionBox;
        std::unique_ptr<ComboBoxAttachment> shapeAttachment, modeAttachment, divisionAttachment;
        juce::ToggleButton syncButton { "Sync (tempo)" };
        std::unique_ptr<ButtonAttachment> syncAttachment;
        Knob rateKnob;
        juce::Label divisionLabel;
        undertow::gui::LfoDisplay display;
        std::atomic<float>* shapeParam = nullptr;
        std::atomic<float>* syncParam = nullptr;
    };
    std::array<LfoControls, undertow::synth::numLfos> lfos;

    struct ModSlotControls
    {
        juce::Label number;
        juce::ComboBox sourceBox, destinationBox;
        std::unique_ptr<ComboBoxAttachment> sourceAttachment, destinationAttachment;
        juce::Label arrow;
        juce::Slider amount { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        std::unique_ptr<SliderAttachment> amountAttachment;
    };
    juce::GroupComponent matrixGroup { {}, "Matriz de modulación" };
    std::array<ModSlotControls, undertow::synth::numModSlots> modSlots;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UndertowAudioProcessorEditor)
};
