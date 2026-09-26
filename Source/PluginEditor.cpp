#include "PluginEditor.h"
#include "Parameters.h"
#include "PluginProcessor.h"

namespace
{
constexpr int knobWidth = 90;
constexpr int knobHeight = 110;
constexpr int groupPadding = 12;
constexpr int rowHeight = 174;
constexpr int wavetableColumnWidth = 170;
constexpr int filterDisplayWidth = 280;
constexpr int headerHeight = 32;
constexpr int lfoOptionsWidth = 130;
constexpr int lfoRateWidth = 84;
constexpr int matrixRowHeight = 30;
constexpr int oscillatorColumnWidth = 150; // On, tabla y visor de la forma de onda
constexpr int oscillatorKnobWidth = 72;    // 9 perillas por oscilador: un poco más estrechas que las demás

// El ancho lo marca la fila del filtro: opciones + 4 perillas + un visor de 280 px (igual que en la Fase 4).
constexpr int pageWidth = 2 * groupPadding + wavetableColumnWidth + 4 * knobWidth + filterDisplayWidth;
constexpr int pageHeight = 3 * rowHeight + 2 * groupPadding;
} // namespace

UndertowAudioProcessorEditor::UndertowAudioProcessorEditor (UndertowAudioProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    namespace id = undertow::params;
    auto& apvts = processor.getParameters();

    struct KnobInfo
    {
        const char* parameterId;
        const char* name;
    };
    constexpr std::array<KnobInfo, numEnvelopeKnobs + numVoiceKnobs> infos { {
        { id::attack, "Attack" },
        { id::decay, "Decay" },
        { id::sustain, "Sustain" },
        { id::release, "Release" },
        { id::voices, "Voices" },
        { id::velocity, "Velocity" },
        { id::master, "Master" },
    } };

    // --- Pestañas ---
    // Los literales con tildes se pasan con fromUTF8: un const char* suelto JUCE lo lee como Latin-1.
    modulationTab.setButtonText (juce::String::fromUTF8 ("Modulaci\xc3\xb3n"));
    for (auto* tab : { &oscillatorTab, &warpTab, &soundTab, &modulationTab })
    {
        tab->setClickingTogglesState (true);
        tab->setRadioGroupId (1);
        tab->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2b7a8c)); // la pestaña activa se resalta
        addAndMakeVisible (tab);
    }
    oscillatorTab.onClick = [this] { showPage (Page::oscillators); };
    warpTab.onClick = [this] { showPage (Page::warp); };
    soundTab.onClick = [this] { showPage (Page::sound); };
    modulationTab.onClick = [this] { showPage (Page::modulation); };
    addChildComponent (oscillatorPage);
    addChildComponent (warpPage);
    addChildComponent (soundPage);
    addChildComponent (modulationPage);

    // ================= Página "Osciladores": Osc A, Osc B, sub y ruido =================
    for (size_t o = 0; o < oscillators.size(); ++o)
    {
        auto& osc = oscillators[o];
        const auto& ids = id::oscillators[o];
        osc.group.setText (o == 0 ? "Oscilador A" : "Oscilador B");
        oscillatorPage.addAndMakeVisible (osc.group);

        oscillatorPage.addAndMakeVisible (osc.onButton);
        osc.onAttachment = std::make_unique<ButtonAttachment> (apvts, ids.on, osc.onButton);
        setUpComboBox (osc.tableBox, oscillatorPage, ids.wavetable, osc.tableAttachment);
        oscillatorPage.addAndMakeVisible (osc.display);
        osc.tableParam = apvts.getRawParameterValue (ids.wavetable);
        osc.positionParam = apvts.getRawParameterValue (ids.position);

        const std::array<KnobInfo, numOscillatorKnobs> oscillatorInfos { {
            { ids.position, "Position" },
            { ids.octave, "Octave" },
            { ids.semitones, "Semi" },
            { ids.fine, "Fine" },
            { ids.level, "Level" },
            { ids.pan, "Pan" },
            { ids.unison, "Unison" },
            { ids.detune, "Detune" },
            { ids.width, "Width" },
        } };
        for (size_t k = 0; k < osc.knobs.size(); ++k)
            setUpKnob (osc.knobs[k], oscillatorPage, oscillatorInfos[k].parameterId, oscillatorInfos[k].name,
                       oscillatorKnobWidth);
    }

    oscillatorPage.addAndMakeVisible (subGroup);
    oscillatorPage.addAndMakeVisible (subOnButton);
    subOnAttachment = std::make_unique<ButtonAttachment> (apvts, id::subOn, subOnButton);
    setUpComboBox (subShapeBox, oscillatorPage, id::subShape, subShapeAttachment);
    setUpKnob (subKnobs[0], oscillatorPage, id::subOctave, "Octave", oscillatorKnobWidth);
    setUpKnob (subKnobs[1], oscillatorPage, id::subLevel, "Level", oscillatorKnobWidth);

    oscillatorPage.addAndMakeVisible (noiseGroup);
    oscillatorPage.addAndMakeVisible (noiseOnButton);
    noiseOnAttachment = std::make_unique<ButtonAttachment> (apvts, id::noiseOn, noiseOnButton);
    setUpKnob (noiseKnobs[0], oscillatorPage, id::noiseLevel, "Level", oscillatorKnobWidth);
    setUpKnob (noiseKnobs[1], oscillatorPage, id::noiseColor, "Color", oscillatorKnobWidth);

    // ================= Página "Warp y FM": una columna por oscilador =================
    for (size_t o = 0; o < warps.size(); ++o)
    {
        auto& warp = warps[o];
        const auto& ids = id::oscillatorWarps[o];
        warp.group.setText (o == 0 ? "Oscilador A" : "Oscilador B");
        warpPage.addAndMakeVisible (warp.group);
        warpPage.addAndMakeVisible (warp.display);

        warp.warpLabel.setText ("Warp", juce::dontSendNotification);
        warp.fmLabel.setText ("FM / RM", juce::dontSendNotification);
        for (auto* label : { &warp.warpLabel, &warp.fmLabel })
        {
            label->setJustificationType (juce::Justification::centred);
            warpPage.addAndMakeVisible (label);
        }
        setUpComboBox (warp.warpBox, warpPage, ids.warpMode, warp.warpAttachment);
        setUpComboBox (warp.fmBox, warpPage, ids.fmMode, warp.fmAttachment);
        setUpKnob (warp.warpKnob, warpPage, ids.warpAmount, "Amount", knobWidth);
        setUpKnob (warp.fmKnob, warpPage, ids.fmAmount, "Amount", knobWidth);

        warp.warpModeParam = apvts.getRawParameterValue (ids.warpMode);
        warp.warpAmountParam = apvts.getRawParameterValue (ids.warpAmount);
    }

    // ================= Página "Filtro y Amp": filtro, Env 1 y voz =================
    soundPage.addAndMakeVisible (filterGroup);
    soundPage.addAndMakeVisible (envelopeGroup);
    soundPage.addAndMakeVisible (voiceGroup);

    for (size_t i = 0; i < knobs.size(); ++i)
        setUpKnob (knobs[i], soundPage, infos[i].parameterId, infos[i].name, knobWidth);

    soundPage.addAndMakeVisible (filterOnButton);
    filterOnAttachment = std::make_unique<ButtonAttachment> (apvts, id::filter1On, filterOnButton);
    setUpComboBox (filterTypeBox, soundPage, id::filter1Type, filterTypeAttachment);
    setUpComboBox (filterSlopeBox, soundPage, id::filter1Slope, filterSlopeAttachment);

    constexpr std::array<KnobInfo, numFilterKnobs> filterInfos { {
        { id::filter1Cutoff, "Cutoff" },
        { id::filter1Resonance, "Resonance" },
        { id::filter1Drive, "Drive" },
        { id::filter1KeyTrack, "Key Track" },
    } };
    for (size_t i = 0; i < filterKnobs.size(); ++i)
        setUpKnob (filterKnobs[i], soundPage, filterInfos[i].parameterId, filterInfos[i].name, knobWidth);
    soundPage.addAndMakeVisible (filterDisplay);

    // ================= Página "Modulación": LFOs, Env 2 y 3, matriz =================
    for (size_t l = 0; l < lfos.size(); ++l)
    {
        auto& lfo = lfos[l];
        const auto& ids = id::lfos[l];
        lfo.group.setText ("LFO " + juce::String (static_cast<int> (l) + 1));
        modulationPage.addAndMakeVisible (lfo.group);

        setUpComboBox (lfo.shapeBox, modulationPage, ids.shape, lfo.shapeAttachment);
        setUpComboBox (lfo.modeBox, modulationPage, ids.mode, lfo.modeAttachment);
        setUpComboBox (lfo.divisionBox, modulationPage, ids.division, lfo.divisionAttachment);
        modulationPage.addAndMakeVisible (lfo.syncButton);
        lfo.syncAttachment = std::make_unique<ButtonAttachment> (apvts, ids.sync, lfo.syncButton);

        // Rate (Hz) y Division (tempo) ocupan el mismo sitio: el timer muestra uno u otro según Sync.
        setUpKnob (lfo.rateKnob, modulationPage, ids.rate, "Rate", lfoRateWidth);
        lfo.divisionLabel.setText ("Division", juce::dontSendNotification);
        lfo.divisionLabel.setJustificationType (juce::Justification::centred);
        modulationPage.addChildComponent (lfo.divisionLabel);

        modulationPage.addAndMakeVisible (lfo.display);
        lfo.shapeParam = apvts.getRawParameterValue (ids.shape);
        lfo.syncParam = apvts.getRawParameterValue (ids.sync);
    }

    for (size_t e = 0; e < modEnvelopes.size(); ++e)
    {
        auto& env = modEnvelopes[e];
        const auto& ids = id::modEnvelopes[e];
        env.group.setText ("Env " + juce::String (static_cast<int> (e) + 2));
        modulationPage.addAndMakeVisible (env.group);
        setUpKnob (env.knobs[0], modulationPage, ids.attack, "Attack", knobWidth);
        setUpKnob (env.knobs[1], modulationPage, ids.decay, "Decay", knobWidth);
        setUpKnob (env.knobs[2], modulationPage, ids.sustain, "Sustain", knobWidth);
        setUpKnob (env.knobs[3], modulationPage, ids.release, "Release", knobWidth);
    }

    matrixGroup.setText (juce::String::fromUTF8 ("Matriz de modulaci\xc3\xb3n"));
    modulationPage.addAndMakeVisible (matrixGroup);
    for (size_t s = 0; s < modSlots.size(); ++s)
    {
        auto& slot = modSlots[s];
        const auto& ids = id::modSlots[s];

        slot.number.setText (juce::String (static_cast<int> (s) + 1), juce::dontSendNotification);
        slot.number.setJustificationType (juce::Justification::centred);
        slot.number.setColour (juce::Label::textColourId, juce::Colours::grey);
        modulationPage.addAndMakeVisible (slot.number);

        setUpComboBox (slot.sourceBox, modulationPage, ids.source, slot.sourceAttachment);
        setUpComboBox (slot.destinationBox, modulationPage, ids.destination, slot.destinationAttachment);

        slot.arrow.setText (juce::String::fromUTF8 ("\xe2\x86\x92") /* → */, juce::dontSendNotification);
        slot.arrow.setJustificationType (juce::Justification::centred);
        modulationPage.addAndMakeVisible (slot.arrow);

        slot.amount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 22);
        slot.amount.setDoubleClickReturnValue (true, 0.0); // doble clic = amount 0 %
        modulationPage.addAndMakeVisible (slot.amount);
        slot.amountAttachment = std::make_unique<SliderAttachment> (apvts, ids.amount, slot.amount);
    }

    activeVoicesLabel.setJustificationType (juce::Justification::centredRight);
    activeVoicesLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible (activeVoicesLabel);

    // Mismo tamaño que la ventana de la Fase 4: cabe en un portátil con el escalado de Windows al 150 %.
    setSize (pageWidth + 2 * groupPadding, pageHeight + headerHeight + 3 * groupPadding);

    oscillatorTab.setToggleState (true, juce::dontSendNotification);
    showPage (Page::oscillators);
    timerCallback(); // estado inicial (p. ej. Rate o Division visibles según Sync) sin esperar al primer tick
    startTimerHz (30); // la GUI consulta el estado; el audio nunca espera a la GUI
}

UndertowAudioProcessorEditor::~UndertowAudioProcessorEditor()
{
    stopTimer();
}

void UndertowAudioProcessorEditor::showPage (Page page)
{
    oscillatorPage.setVisible (page == Page::oscillators);
    warpPage.setVisible (page == Page::warp);
    soundPage.setVisible (page == Page::sound);
    modulationPage.setVisible (page == Page::modulation);
}

void UndertowAudioProcessorEditor::setUpKnob (Knob& knob, juce::Component& page, const char* parameterId,
                                              const juce::String& name, int width)
{
    knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, width - 10, 20);
    page.addAndMakeVisible (knob.slider);

    knob.label.setText (name, juce::dontSendNotification);
    knob.label.setJustificationType (juce::Justification::centred);
    page.addAndMakeVisible (knob.label);

    // El attachment sincroniza la perilla con el parámetro en ambos sentidos
    // (y con la automatización de FL) sin que la GUI toque el hilo de audio.
    knob.attachment = std::make_unique<SliderAttachment> (processor.getParameters(), parameterId, knob.slider);
}

void UndertowAudioProcessorEditor::setUpComboBox (juce::ComboBox& box, juce::Component& page, const char* parameterId,
                                                  std::unique_ptr<ComboBoxAttachment>& attachment)
{
    // Las opciones deben existir ANTES de crear el attachment (usa el índice del ítem, empezando en 1).
    // Salen del propio parámetro: así la GUI nunca se desincroniza de la lista guardada.
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (processor.getParameters().getParameter (parameterId)))
        box.addItemList (choice->choices, 1);
    page.addAndMakeVisible (box);
    attachment = std::make_unique<ComboBoxAttachment> (processor.getParameters(), parameterId, box);
}

void UndertowAudioProcessorEditor::layoutKnob (Knob& knob, juce::Rectangle<int> column)
{
    column = column.withHeight (knobHeight + 20);
    knob.label.setBounds (column.removeFromTop (20));
    knob.slider.setBounds (column);
}

void UndertowAudioProcessorEditor::timerCallback()
{
    const int count = processor.getActiveVoiceCount();
    if (count != lastShownVoiceCount)
    {
        lastShownVoiceCount = count;
        activeVoicesLabel.setText ("Voces sonando: " + juce::String (count), juce::dontSendNotification);
    }

    // Se lee el valor del parámetro (no el de la perilla) para que el visor siga también la automatización.
    const auto& bank = processor.getWavetableBank();
    for (auto& osc : oscillators)
        osc.display.setWavetable (&bank.get (static_cast<int> (osc.tableParam->load())), osc.positionParam->load());
    for (size_t o = 0; o < warps.size(); ++o)
    {
        const auto& osc = oscillators[o];
        auto& warp = warps[o];
        warp.display.setState (&bank.get (static_cast<int> (osc.tableParam->load())), osc.positionParam->load(),
                               static_cast<undertow::dsp::WarpMode> (static_cast<int> (warp.warpModeParam->load())),
                               warp.warpAmountParam->load());
    }

    const auto filter = processor.readFilterSettings();
    filterDisplay.setResponse (filter.parameters, filter.enabled, processor.getCurrentSampleRate());

    for (size_t l = 0; l < lfos.size(); ++l)
    {
        auto& lfo = lfos[l];
        const bool synced = lfo.syncParam->load() >= 0.5f;
        lfo.rateKnob.slider.setVisible (! synced);
        lfo.rateKnob.label.setVisible (! synced);
        lfo.divisionBox.setVisible (synced);
        lfo.divisionLabel.setVisible (synced);

        lfo.display.setState (static_cast<undertow::dsp::LfoShape> (static_cast<int> (lfo.shapeParam->load())),
                              processor.getLfoDisplayPhase (static_cast<int> (l)));
    }
}

void UndertowAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff101418));

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("Undertow Synth", getLocalBounds().removeFromTop (44).reduced (groupPadding, 0),
                juce::Justification::centredLeft);
}

void UndertowAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (groupPadding);
    auto header = area.removeFromTop (headerHeight);
    activeVoicesLabel.setBounds (header.removeFromRight (170));
    header.removeFromRight (groupPadding);
    modulationTab.setBounds (header.removeFromRight (110).reduced (0, 2));
    soundTab.setBounds (header.removeFromRight (110).reduced (0, 2));
    warpTab.setBounds (header.removeFromRight (110).reduced (0, 2));
    oscillatorTab.setBounds (header.removeFromRight (110).reduced (0, 2));
    area.removeFromTop (groupPadding);

    oscillatorPage.setBounds (area);
    warpPage.setBounds (area);
    soundPage.setBounds (area);
    modulationPage.setBounds (area);

    const auto innerOf = [] (juce::Rectangle<int> groupArea) {
        return groupArea.reduced (groupPadding, 0).withTrimmedTop (22).withTrimmedBottom (groupPadding);
    };

    // ================= Página "Osciladores" =================
    {
        auto page = oscillatorPage.getLocalBounds();

        // Filas 1 y 2: Osc A y Osc B. Columna con On, tabla y visor; después sus 9 perillas.
        for (auto& osc : oscillators)
        {
            auto groupArea = page.removeFromTop (rowHeight);
            page.removeFromTop (groupPadding);
            osc.group.setBounds (groupArea);
            auto inner = innerOf (groupArea);

            auto column = inner.removeFromLeft (oscillatorColumnWidth);
            inner.removeFromLeft (8);
            osc.onButton.setBounds (column.removeFromTop (24));
            osc.tableBox.setBounds (column.removeFromTop (26));
            column.removeFromTop (6);
            osc.display.setBounds (column);

            for (auto& knob : osc.knobs)
                layoutKnob (knob, inner.removeFromLeft (oscillatorKnobWidth));
        }

        // Fila 3: Sub | Ruido.
        auto row = page.removeFromTop (rowHeight);
        const int halfWidth = (row.getWidth() - groupPadding) / 2;
        const auto layoutSmallGroup = [&] (juce::GroupComponent& group, juce::ToggleButton& onButton,
                                           juce::ComboBox* box, std::array<Knob, 2>& groupKnobs) {
            auto groupArea = row.removeFromLeft (halfWidth);
            row.removeFromLeft (groupPadding);
            group.setBounds (groupArea);
            auto inner = innerOf (groupArea);

            auto column = inner.removeFromLeft (oscillatorColumnWidth);
            onButton.setBounds (column.removeFromTop (24));
            if (box != nullptr)
                box->setBounds (column.removeFromTop (26));

            inner.removeFromLeft ((inner.getWidth() - 2 * oscillatorKnobWidth) / 2); // perillas centradas
            for (auto& knob : groupKnobs)
                layoutKnob (knob, inner.removeFromLeft (oscillatorKnobWidth));
        };
        layoutSmallGroup (subGroup, subOnButton, &subShapeBox, subKnobs);
        layoutSmallGroup (noiseGroup, noiseOnButton, nullptr, noiseKnobs);
    }

    // ================= Página "Warp y FM" =================
    // Dos columnas (A | B): arriba el dibujo del warp; abajo Warp y FM/RM, cada uno con su selector y su perilla.
    {
        auto page = warpPage.getLocalBounds();
        const int halfWidth = (page.getWidth() - groupPadding) / 2;
        for (auto& warp : warps)
        {
            auto groupArea = page.removeFromLeft (halfWidth);
            page.removeFromLeft (groupPadding);
            warp.group.setBounds (groupArea);
            auto inner = innerOf (groupArea);

            auto controls = inner.removeFromBottom (knobHeight + 20 + 28 + 26);
            inner.removeFromBottom (groupPadding);
            warp.display.setBounds (inner);

            const int columnWidth = controls.getWidth() / 2;
            const auto layoutColumn = [] (juce::Rectangle<int> column, juce::Label& label, juce::ComboBox& box, Knob& knob) {
                label.setBounds (column.removeFromTop (20));
                box.setBounds (column.removeFromTop (28).reduced (8, 0));
                column.removeFromTop (6);
                layoutKnob (knob, column.withSizeKeepingCentre (knobWidth, column.getHeight()));
            };
            layoutColumn (controls.removeFromLeft (columnWidth), warp.warpLabel, warp.warpBox, warp.warpKnob);
            layoutColumn (controls, warp.fmLabel, warp.fmBox, warp.fmKnob);
        }
    }

    // ================= Página "Filtro y Amp" =================
    {
        auto page = soundPage.getLocalBounds();

        // Fila 1: filtro (encendido, tipo y pendiente; perillas; curva de respuesta).
        {
            auto groupArea = page.removeFromTop (rowHeight);
            filterGroup.setBounds (groupArea);
            auto inner = innerOf (groupArea);

            auto optionsColumn = inner.removeFromLeft (wavetableColumnWidth).reduced (4, 0);
            filterOnButton.setBounds (optionsColumn.removeFromTop (28));
            optionsColumn.removeFromTop (6);
            filterTypeBox.setBounds (optionsColumn.removeFromTop (28));
            optionsColumn.removeFromTop (6);
            filterSlopeBox.setBounds (optionsColumn.removeFromTop (28));

            for (auto& knob : filterKnobs)
                layoutKnob (knob, inner.removeFromLeft (knobWidth));

            filterDisplay.setBounds (inner.withTrimmedLeft (groupPadding));
        }
        page.removeFromTop (groupPadding);

        // Fila 2: Env 1 y voz.
        auto row = page.removeFromTop (rowHeight);
        const auto layoutGroup = [&] (juce::GroupComponent& group, size_t firstKnob, size_t count) {
            auto groupArea = row.removeFromLeft (static_cast<int> (count) * knobWidth + groupPadding * 2);
            group.setBounds (groupArea);
            auto inner = innerOf (groupArea);
            for (size_t i = firstKnob; i < firstKnob + count; ++i)
                layoutKnob (knobs[i], inner.removeFromLeft (knobWidth));
        };
        layoutGroup (envelopeGroup, 0, numEnvelopeKnobs);
        row.removeFromLeft (groupPadding);
        layoutGroup (voiceGroup, numEnvelopeKnobs, numVoiceKnobs);
    }

    // ================= Página "Modulación" =================
    {
        auto page = modulationPage.getLocalBounds();
        const int halfWidth = (page.getWidth() - groupPadding) / 2;

        // Fila 1: LFO 1 | LFO 2 (opciones; Rate o Division; dibujo de la forma).
        {
            auto row = page.removeFromTop (rowHeight);
            for (auto& lfo : lfos)
            {
                auto groupArea = row.removeFromLeft (halfWidth);
                row.removeFromLeft (groupPadding);
                lfo.group.setBounds (groupArea);
                auto inner = innerOf (groupArea);

                auto options = inner.removeFromLeft (lfoOptionsWidth);
                lfo.shapeBox.setBounds (options.removeFromTop (28));
                options.removeFromTop (6);
                lfo.modeBox.setBounds (options.removeFromTop (28));
                options.removeFromTop (6);
                lfo.syncButton.setBounds (options.removeFromTop (28));

                auto rateColumn = inner.removeFromLeft (lfoRateWidth).withTrimmedLeft (4);
                layoutKnob (lfo.rateKnob, rateColumn);
                lfo.divisionLabel.setBounds (rateColumn.removeFromTop (20));
                lfo.divisionBox.setBounds (rateColumn.removeFromTop (28));

                lfo.display.setBounds (inner.withTrimmedLeft (8));
            }
        }
        page.removeFromTop (groupPadding);

        // Fila 2: Env 2 | Env 3.
        {
            auto row = page.removeFromTop (rowHeight);
            for (auto& env : modEnvelopes)
            {
                auto groupArea = row.removeFromLeft (halfWidth);
                row.removeFromLeft (groupPadding);
                env.group.setBounds (groupArea);
                auto inner = innerOf (groupArea);
                inner.removeFromLeft ((inner.getWidth() - 4 * knobWidth) / 2); // centradas en el grupo
                for (auto& knob : env.knobs)
                    layoutKnob (knob, inner.removeFromLeft (knobWidth));
            }
        }
        page.removeFromTop (groupPadding);

        // Fila 3: matriz, 8 rutas en 2 columnas de 4. Cada fila: número, fuente → destino, amount.
        matrixGroup.setBounds (page);
        auto inner = innerOf (page);
        const int columnWidth = (inner.getWidth() - 2 * groupPadding) / 2;
        const int rowStep = std::min (matrixRowHeight, inner.getHeight() / 4);

        for (size_t s = 0; s < modSlots.size(); ++s)
        {
            auto& slot = modSlots[s];
            const int column = static_cast<int> (s) / 4;
            const int row = static_cast<int> (s) % 4;
            auto line = juce::Rectangle<int> (inner.getX() + column * (columnWidth + 2 * groupPadding),
                                              inner.getY() + row * rowStep, columnWidth, rowStep)
                            .reduced (0, 2);

            slot.number.setBounds (line.removeFromLeft (16));
            slot.sourceBox.setBounds (line.removeFromLeft (104));
            slot.arrow.setBounds (line.removeFromLeft (14));
            slot.destinationBox.setBounds (line.removeFromLeft (128));
            line.removeFromLeft (4);
            slot.amount.setBounds (line);
        }
    }
}
