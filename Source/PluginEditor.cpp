#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    struct UIScalePreset
    {
        int id;
        const char* label;
        float scale;
    };

    static constexpr std::array<UIScalePreset, 6> kUIScalePresets = {{
        { 1, "UI: 50% Compact",   0.50f },
        { 2, "UI: 60% Default",   0.60f },
        { 3, "UI: 70% Medium",    0.70f },
        { 4, "UI: 80% Large",     0.80f },
        { 5, "UI: 100% Native",   1.00f },
        { 6, "UI: 125% HiDPI",    1.25f }
    }};
}

CasioCTK1000AudioProcessorEditor::CasioCTK1000AudioProcessorEditor(CasioCTK1000AudioProcessor& p)
    : AudioProcessorEditor(&p),
      processorRef(p),
      mainCanvas(*this),
      wavefolderScope(p),
      midiKeyboard(p.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel(&ctkLookAndFeel);
    addAndMakeVisible(mainCanvas);

    auto& apvts = processorRef.getAPVTS();

    //==============================================================================
    // 1. Top Console Bezel Setup (Added to mainCanvas)
    crossModeCombo.addItemList({ "Pure IXA", "Hybrid PCM + IXA", "Dual PCM" }, 1);
    mainCanvas.addAndMakeVisible(crossModeCombo);
    crossModeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, "CROSS_MODE", crossModeCombo);

    routingModeCombo.addItemList({ "Parallel Detuned", "2-Op Cascade (Op2->Op1)", "Single Op 1" }, 1);
    mainCanvas.addAndMakeVisible(routingModeCombo);
    routingModeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, "ROUTING_MODE", routingModeCombo);

    dac1993Button.setClickingTogglesState(true);
    mainCanvas.addAndMakeVisible(dac1993Button);
    dac1993Attach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, "HARDWARE_DAC_MODE", dac1993Button);

    adaaButton.setClickingTogglesState(true);
    mainCanvas.addAndMakeVisible(adaaButton);
    adaaAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, "ADAA_ENABLE", adaaButton);

    initRotaryKnob(synthPanKnob,   "SYNTH_PAN",   "IXA PAN", true);
    initRotaryKnob(masterGainKnob, "MASTER_GAIN", "MASTER",  false);

    loadSa51FolderButton.getProperties().set("royalBlue", true);
    loadSa51FolderButton.onClick = [this] { openSampleFolderChooser(); };
    mainCanvas.addAndMakeVisible(loadSa51FolderButton);

    // Quick-Scale Header Dropdown (50% to 125%)
    for (const auto& preset : kUIScalePresets)
        uiScaleBox.addItem(preset.label, preset.id);

    uiScaleBox.onChange = [this]
    {
        const int selId = uiScaleBox.getSelectedId();
        for (const auto& preset : kUIScalePresets)
        {
            if (preset.id == selId)
            {
                applyUIScaleFactor(preset.scale, true);
                break;
            }
        }
    };
    mainCanvas.addAndMakeVisible(uiScaleBox);

    mainCanvas.addAndMakeVisible(wavefolderScope);

    //==============================================================================
    // 2. Dual PCM Sample Slots Setup (Element A & Element B)
    auto setupPCMSlot = [&](PCMSlotControls& slot, const juce::String& prefix, bool isSlotA)
    {
        slot.enableBtn.setClickingTogglesState(true);
        mainCanvas.addAndMakeVisible(slot.enableBtn);
        slot.enableAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, prefix + "ENABLE", slot.enableBtn);

        mainCanvas.addAndMakeVisible(slot.instrumentCombo);
        slot.instrumentCombo.onChange = [this, isSlotA, &slot]
        {
            const int selIdx = slot.instrumentCombo.getSelectedItemIndex();
            if (selIdx >= 0)
            {
                if (isSlotA)
                    processorRef.setSlotAInstrumentByIndex(selIdx);
                else
                    processorRef.setSlotBInstrumentByIndex(selIdx);
            }
        };

        slot.loopModeCombo.addItemList({ "One-Shot", "Forward Loop", "Ping-Pong (Pat.5,319,151)" }, 1);
        mainCanvas.addAndMakeVisible(slot.loopModeCombo);
        slot.loopModeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            apvts, prefix + "LOOP_MODE", slot.loopModeCombo);

        slot.loopOverrideBtn.setClickingTogglesState(true);
        mainCanvas.addAndMakeVisible(slot.loopOverrideBtn);
        slot.loopOverrideAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, prefix + "LOOP_OVERRIDE", slot.loopOverrideBtn);

        initRotaryKnob(slot.level,     prefix + "LEVEL",       "LEVEL");
        initRotaryKnob(slot.pan,       prefix + "PAN",         "PAN");
        initRotaryKnob(slot.coarse,    prefix + "COARSE_TUNE", "COARSE");
        initRotaryKnob(slot.fine,      prefix + "FINE_CENTS",  "FINE");
        initRotaryKnob(slot.velSens,   prefix + "VEL_SENS",    "VEL SENS");
        initRotaryKnob(slot.loopStart, prefix + "LOOP_START",  "LP START", true);
        initRotaryKnob(slot.loopEnd,   prefix + "LOOP_END",    "LP END",   true);

        initRotaryKnob(slot.attack,    prefix + "AMP_ATTACK",  "AMP A", true);
        initRotaryKnob(slot.decay,     prefix + "AMP_DECAY",   "AMP D", true);
        initRotaryKnob(slot.sustain,   prefix + "AMP_SUSTAIN", "AMP S", true);
        initRotaryKnob(slot.release,   prefix + "AMP_RELEASE", "AMP R", true);
    };

    setupPCMSlot(pcmSlotA, "PCM_A_", true);
    setupPCMSlot(pcmSlotB, "PCM_B_", false);

    refreshInstrumentComboBoxes();

    //==============================================================================
    // 3. Dual-Operator IXA Synthesis Engine Setup (Operators 1 & 2) + 12dB LPF
    auto setupIXAOperator = [&](IXAOperatorControls& op, const juce::String& prefix)
    {
        initRotaryKnob(op.coarseRatio, prefix + "COARSE_RATIO",   "RATIO (r)");
        initRotaryKnob(op.fineDetune,  prefix + "FINE_DETUNE_HZ", "DETUNE Hz");
        initRotaryKnob(op.baseIndex,   prefix + "BASE_INDEX",     "INDEX (n0)");
        initRotaryKnob(op.envDepth,    prefix + "ENV_DEPTH",      "ENV DEPTH");
        initRotaryKnob(op.velDepth,    prefix + "VEL_DEPTH",      "VEL DEPTH");
        initRotaryKnob(op.carrierWarp, prefix + "CARRIER_WARP",   "WARP (a)");
        initRotaryKnob(op.feedback,    prefix + "FEEDBACK",       "FDBK (b)");
        initRotaryKnob(op.level,       prefix + "LEVEL",          "OP LEVEL");

        initRotaryKnob(op.modA, prefix + "MOD_ATTACK",  "MOD A", true);
        initRotaryKnob(op.modD, prefix + "MOD_DECAY",   "MOD D", true);
        initRotaryKnob(op.modS, prefix + "MOD_SUSTAIN", "MOD S", true);
        initRotaryKnob(op.modR, prefix + "MOD_RELEASE", "MOD R", true);

        initRotaryKnob(op.ampA, prefix + "AMP_ATTACK",  "AMP A");
        initRotaryKnob(op.ampD, prefix + "AMP_DECAY",   "AMP D");
        initRotaryKnob(op.ampS, prefix + "AMP_SUSTAIN", "AMP S");
        initRotaryKnob(op.ampR, prefix + "AMP_RELEASE", "AMP R");
    };

    setupIXAOperator(op1Controls, "OP1_");
    setupIXAOperator(op2Controls, "OP2_");

    initRotaryKnob(ixaLpfCutoffKnob,   "IXA_LPF_CUTOFF",   "LPF CUTOFF", false);
    initRotaryKnob(ixaLpfEnvKnob,      "IXA_LPF_ENV",      "LPF ENV",    true);
    initRotaryKnob(ixaLpfKeyTrackKnob, "IXA_LPF_KEYTRACK", "LPF KEY",    true);

    //==============================================================================
    // 4. Bottom-Left Panel — Legacy 1993 CTK-1000 Tone Editor Override
    legacyMacroEnableBtn.setClickingTogglesState(true);
    legacyMacroEnableBtn.getProperties().set("crimsonBtn", true);
    mainCanvas.addAndMakeVisible(legacyMacroEnableBtn);
    legacyMacroEnableAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, "LEGACY_MACRO_ENABLE", legacyMacroEnableBtn);

    initRotaryKnob(macroWaveKnob,    "MACRO_WAVE",    "WAVE (0-9)");
    initRotaryKnob(macroAttackKnob,  "MACRO_ATTACK",  "ATTACK (0-9)");
    initRotaryKnob(macroReleaseKnob, "MACRO_RELEASE", "RELEASE (0-9)");

    //==============================================================================
    // 5. Bottom-Right Panel — 8-Mode Semi-OBS DSP Effect Unit (Sanyo LC33832PL-70)
    dspModeCombo.addItemList({
        "1: Hall Reverb",
        "2: Delay",
        "3: Chorus",
        "4: Distortion",
        "5: Room Reverb",
        "6: Stereo Delay",
        "7: Flanger",
        "8: Chorus Reverb"
    }, 1);
    mainCanvas.addAndMakeVisible(dspModeCombo);
    dspModeAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, "DSP_MODE", dspModeCombo);

    dspLofiPsramBtn.setClickingTogglesState(true);
    mainCanvas.addAndMakeVisible(dspLofiPsramBtn);
    dspLofiPsramAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, "DSP_LOFI_PSRAM", dspLofiPsramBtn);

    dspBypassBtn.setClickingTogglesState(true);
    dspBypassBtn.getProperties().set("crimsonBtn", true);
    mainCanvas.addAndMakeVisible(dspBypassBtn);
    dspBypassAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, "DSP_BYPASS", dspBypassBtn);

    initRotaryKnob(dspParam1Knob, "DSP_PARAM1", "RATE / TIME");
    initRotaryKnob(dspParam2Knob, "DSP_PARAM2", "DEPTH / FDBK");
    initRotaryKnob(dspToneKnob,   "DSP_TONE",   "PrFit TONE", true);
    initRotaryKnob(dspMixKnob,    "DSP_MIX",    "WET / DRY");

    //==============================================================================
    // 6. Phase 6: Dual Assignable Multi-Target LFO Engine (LFO 1 & LFO 2)
    auto setupLFOGroup = [&](LFOControlGroup& lfo, const juce::String& prefix)
    {
        lfo.waveCombo.addItemList({
            "Triangle",
            "Sine",
            "Saw Up",
            "Square",
            "Sample & Hold"
        }, 1);
        mainCanvas.addAndMakeVisible(lfo.waveCombo);
        lfo.waveAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            apvts, prefix + "WAVE", lfo.waveCombo);

        lfo.targetCombo.addItemList({
            "None / Off",
            "Master Pitch (Vibrato)",
            "Master Amp (Tremolo)",
            "IXA 12dB LPF Cutoff",
            "IXA Op 1 Mod Index (n)",
            "IXA Op 2 Mod Index (n)",
            "IXA Both Ops Index (n1+n2)",
            "IXA Op 1 Carrier Warp (a)",
            "IXA Op 1 Self-Feedback (b)",
            "IXA Op 2 Fine Freq / Ratio",
            "PCM A/B Crossfade Balance"
        }, 1);
        mainCanvas.addAndMakeVisible(lfo.targetCombo);
        lfo.targetAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            apvts, prefix + "TARGET", lfo.targetCombo);

        lfo.syncBtn.setClickingTogglesState(true);
        mainCanvas.addAndMakeVisible(lfo.syncBtn);
        lfo.syncAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, prefix + "SYNC", lfo.syncBtn);

        initRotaryKnob(lfo.rateKnob,  prefix + "RATE",  "RATE (Hz)", false);
        initRotaryKnob(lfo.delayKnob, prefix + "DELAY", "DELAY (s)", true);
        initRotaryKnob(lfo.depthKnob, prefix + "DEPTH", "DEPTH",     false);
    };

    setupLFOGroup(lfo1Controls, "LFO1_");
    setupLFOGroup(lfo2Controls, "LFO2_");

    // Configure MIDI keyboard colors to match CTK-1000 hardware (36..96 = 36 white keys)
    midiKeyboard.setAvailableRange(36, 96);
    midiKeyboard.setScrollButtonsVisible(false);
    midiKeyboard.setColour(juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, juce::Colour(0xff1a1d21));
    midiKeyboard.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour(0xffe6e6e2));
    midiKeyboard.setColour(juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour(0xff18191b));
    midiKeyboard.setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour(0xff3a3c40));
    midiKeyboard.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, CasioIXA::CTK1000LookAndFeel::kVermilion.withAlpha(0.5f));
    midiKeyboard.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, CasioIXA::CTK1000LookAndFeel::kVermilion);
    mainCanvas.addAndMakeVisible(midiKeyboard);

    // Enable native corner resizing with locked aspect ratio (45% to 135% of 1180 x 760)
    mainCanvas.setBounds(0, 0, kBaseWidth, kBaseHeight);
    setResizable(true, true);
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio(static_cast<double>(kBaseWidth) / static_cast<double>(kBaseHeight));

    setResizeLimits(static_cast<int>(kBaseWidth * 0.45f),
                    static_cast<int>(kBaseHeight * 0.45f),
                    static_cast<int>(kBaseWidth * 1.35f),
                    static_cast<int>(kBaseHeight * 1.35f));

    // Load saved UI scale factor from settings.xml (defaults to 0.60f = 708 x 456 for FL Studio)
    const float initialScale = processorRef.getSampleBankManager().getSavedUIScale();
    syncUIScaleComboBoxToFactor(initialScale);
    applyUIScaleFactor(initialScale, false);
    isInitializingSize = false;

    startTimerHz(30);
}

CasioCTK1000AudioProcessorEditor::~CasioCTK1000AudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void CasioCTK1000AudioProcessorEditor::applyUIScaleFactor(float newScaleFactor, bool saveToPrefs)
{
    const float clamped = std::clamp(newScaleFactor, 0.45f, 1.35f);
    const int targetW = juce::roundToInt(static_cast<float>(kBaseWidth) * clamped);
    const int targetH = juce::roundToInt(static_cast<float>(kBaseHeight) * clamped);

    setSize(targetW, targetH);

    if (saveToPrefs)
        processorRef.getSampleBankManager().saveUIScaleToProperties(clamped);
}

void CasioCTK1000AudioProcessorEditor::syncUIScaleComboBoxToFactor(float scaleFactor)
{
    int bestId = 0;
    float minDiff = 100.0f;

    for (const auto& preset : kUIScalePresets)
    {
        const float diff = std::abs(scaleFactor - preset.scale);
        if (diff < minDiff)
        {
            minDiff = diff;
            bestId = preset.id;
        }
    }

    if (minDiff <= 0.025f && bestId > 0)
    {
        uiScaleBox.setSelectedId(bestId, juce::dontSendNotification);
    }
    else
    {
        const int pct = juce::roundToInt(scaleFactor * 100.0f);
        uiScaleBox.setText("UI: " + juce::String(pct) + "%", juce::dontSendNotification);
    }
}

void CasioCTK1000AudioProcessorEditor::initRotaryKnob(LabeledKnob& knob,
                                                      const juce::String& paramID,
                                                      const juce::String& titleText,
                                                      bool amberArc)
{
    knob.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 54, 13);
    if (amberArc)
        knob.slider.getProperties().set("amberArc", true);

    mainCanvas.addAndMakeVisible(knob.slider);

    knob.label.setText(titleText, juce::dontSendNotification);
    knob.label.setJustificationType(juce::Justification::centred);
    knob.label.setColour(juce::Label::textColourId,
                         amberArc ? CasioIXA::CTK1000LookAndFeel::kAmberGold
                                  : CasioIXA::CTK1000LookAndFeel::kOffWhite);
    knob.label.setFont(CasioIXA::CTK1000LookAndFeel::makeFont(9.5f, true, false));
    mainCanvas.addAndMakeVisible(knob.label);

    knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.getAPVTS(), paramID, knob.slider);
}

void CasioCTK1000AudioProcessorEditor::layoutKnobGrid(const std::vector<LabeledKnob*>& knobs,
                                                      juce::Rectangle<int> area,
                                                      int columns)
{
    if (knobs.empty() || columns <= 0)
        return;

    const int rows = (static_cast<int>(knobs.size()) + columns - 1) / columns;
    const int cellW = area.getWidth() / columns;
    const int cellH = area.getHeight() / std::max(1, rows);

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const int r = static_cast<int>(i) / columns;
        const int c = static_cast<int>(i) % columns;
        juce::Rectangle<int> cell(area.getX() + c * cellW,
                                  area.getY() + r * cellH,
                                  cellW,
                                  cellH);

        auto labelRect = cell.removeFromTop(13);
        knobs[i]->label.setBounds(labelRect);
        knobs[i]->slider.setBounds(cell.reduced(2, 0));
    }
}

void CasioCTK1000AudioProcessorEditor::openSampleFolderChooser()
{
    const juce::File startDir = processorRef.getSampleBankManager().getCurrentDirectory().exists()
                              ? processorRef.getSampleBankManager().getCurrentDirectory()
                              : juce::File::getSpecialLocation(juce::File::userHomeDirectory);

    activeFileChooser = std::make_unique<juce::FileChooser>(
        "Select Casio SA-51 WAV Sample Directory (761 .wav files)", startDir);

    const auto chooserFlags = juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectDirectories;

    activeFileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc)
    {
        const juce::File chosen = fc.getResult();
        if (chosen.exists() && chosen.isDirectory())
        {
            if (processorRef.loadSampleFolder(chosen))
            {
                refreshInstrumentComboBoxes();
                mainCanvas.repaint();
            }
        }
    });
}

void CasioCTK1000AudioProcessorEditor::refreshInstrumentComboBoxes()
{
    const juce::StringArray names = processorRef.getAvailableInstrumentNames();
    const juce::File curDir = processorRef.getSampleBankManager().getCurrentDirectory();

    pcmSlotA.instrumentCombo.clear(juce::dontSendNotification);
    pcmSlotB.instrumentCombo.clear(juce::dontSendNotification);

    for (int i = 0; i < names.size(); ++i)
    {
        const auto* group = processorRef.getSampleBankManager().getInstrumentGroupByIndex(i);
        const int zoneCount = (group != nullptr) ? static_cast<int>(group->zones.size()) : 1;
        const juce::String cleanName = names[i].retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_- ");
        const juce::String numPrefix = (i + 1 < 10) ? ("0" + juce::String(i + 1)) : juce::String(i + 1);
        const juce::String itemLabel = numPrefix + ": " + cleanName + " (" + juce::String(zoneCount) + " zn)";
        pcmSlotA.instrumentCombo.addItem(itemLabel, i + 1);
        pcmSlotB.instrumentCombo.addItem(itemLabel, i + 1);
    }

    const int idxA = processorRef.getSlotAInstrumentIndex();
    const int idxB = processorRef.getSlotBInstrumentIndex();

    if (idxA >= 0 && idxA < names.size())
        pcmSlotA.instrumentCombo.setSelectedItemIndex(idxA, juce::dontSendNotification);
    if (idxB >= 0 && idxB < names.size())
        pcmSlotB.instrumentCombo.setSelectedItemIndex(idxB, juce::dontSendNotification);

    if (curDir.exists() && curDir.isDirectory())
    {
        sampleBankStatusText = "BANK: " + curDir.getFileName().toUpperCase()
                             + " (" + juce::String(names.size()) + " GROUPS LOADED)";
    }
    else
    {
        sampleBankStatusText = "BANK: INTERNAL FALLBACK (" + juce::String(names.size())
                             + " GROUPS) - CLICK TO LOAD SA-51 WAVS";
    }
}

void CasioCTK1000AudioProcessorEditor::timerCallback()
{
    wavefolderScope.updateScope();

    // Keep ComboBox selections synced if state was restored by the host DAW
    const int hostIdxA = processorRef.getSlotAInstrumentIndex();
    const int hostIdxB = processorRef.getSlotBInstrumentIndex();

    if (pcmSlotA.instrumentCombo.getNumItems() != processorRef.getAvailableInstrumentNames().size())
        refreshInstrumentComboBoxes();

    if (pcmSlotA.instrumentCombo.getSelectedItemIndex() != hostIdxA)
        pcmSlotA.instrumentCombo.setSelectedItemIndex(hostIdxA, juce::dontSendNotification);

    if (pcmSlotB.instrumentCombo.getSelectedItemIndex() != hostIdxB)
        pcmSlotB.instrumentCombo.setSelectedItemIndex(hostIdxB, juce::dontSendNotification);

    const double dt = 1.0 / 30.0;
    const double r1 = static_cast<double>(processorRef.getAPVTS().getRawParameterValue("LFO1_RATE")->load());
    const double r2 = static_cast<double>(processorRef.getAPVTS().getRawParameterValue("LFO2_RATE")->load());
    guiLfo1Phase = std::fmod(guiLfo1Phase + r1 * dt, 1.0);
    guiLfo2Phase = std::fmod(guiLfo2Phase + r2 * dt, 1.0);

    mainCanvas.repaint(centerLedWindowBounds);
    mainCanvas.repaint(legacyLedReadoutBounds);
    mainCanvas.repaint(dspLedMatrixBounds);
    mainCanvas.repaint(lfo1Bounds.withHeight(22));
    mainCanvas.repaint(lfo2Bounds.withHeight(22));
}

void CasioCTK1000AudioProcessorEditor::drawHardwareSectionFrame(juce::Graphics& g,
                                                                juce::Rectangle<int> bounds,
                                                                const juce::String& titleText)
{
    const auto fBounds = bounds.toFloat();

    // Recessed jet-black console panel (#0D0E10)
    g.setColour(CasioIXA::CTK1000LookAndFeel::kRecessedBlack);
    g.fillRoundedRectangle(fBounds, 3.0f);

    // Subtle bevel border + 1px Casio Vermilion (#E84A27) silkscreen outline
    g.setColour(CasioIXA::CTK1000LookAndFeel::kBevelBorder);
    g.drawRoundedRectangle(fBounds.expanded(1.0f), 3.5f, 1.0f);
    g.setColour(CasioIXA::CTK1000LookAndFeel::kVermilion);
    g.drawRoundedRectangle(fBounds, 3.0f, 1.2f);

    // Top Vermilion header bar rule
    auto headerRect = bounds.removeFromTop(20);
    g.setColour(CasioIXA::CTK1000LookAndFeel::kVermilion.withAlpha(0.18f));
    g.fillRect(headerRect);
    g.setColour(CasioIXA::CTK1000LookAndFeel::kVermilion);
    g.drawHorizontalLine(headerRect.getBottom(), fBounds.getX(), fBounds.getRight());

    g.setFont(CasioIXA::CTK1000LookAndFeel::makeFont(11.5f, true, false));
    g.setColour(CasioIXA::CTK1000LookAndFeel::kVermilion);
    g.drawText(titleText, headerRect.reduced(8, 0), juce::Justification::centredLeft, false);
}

void CasioCTK1000AudioProcessorEditor::drawSevenSegmentDisplayBezel(juce::Graphics& g,
                                                                    juce::Rectangle<int> bounds,
                                                                    const juce::String& topCaption,
                                                                    const juce::String& mainReadout,
                                                                    const juce::String& subReadout)
{
    const auto fBounds = bounds.toFloat();
    g.setColour(CasioIXA::CTK1000LookAndFeel::kLedBezelDark);
    g.fillRoundedRectangle(fBounds, 3.0f);

    g.setColour(CasioIXA::CTK1000LookAndFeel::kVermilion.withAlpha(0.75f));
    g.drawRoundedRectangle(fBounds, 3.0f, 1.2f);

    auto inner = bounds.reduced(8, 4);
    g.setFont(CasioIXA::CTK1000LookAndFeel::makeFont(9.5f, true, false));
    g.setColour(CasioIXA::CTK1000LookAndFeel::kAmberGold);
    g.drawText(topCaption, inner.removeFromTop(12), juce::Justification::centredLeft, false);

    g.setFont(CasioIXA::CTK1000LookAndFeel::makeFont(16.0f, true, true));
    g.setColour(CasioIXA::CTK1000LookAndFeel::kLedCrimson);
    g.drawText(mainReadout, inner.removeFromTop(20), juce::Justification::centredLeft, false);

    if (subReadout.isNotEmpty())
    {
        g.setFont(CasioIXA::CTK1000LookAndFeel::makeFont(11.0f, true, true));
        g.setColour(CasioIXA::CTK1000LookAndFeel::kLedCrimson.withAlpha(0.85f));
        g.drawText(subReadout, inner, juce::Justification::centredLeft, false);
    }
}

void CasioCTK1000AudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(CasioIXA::CTK1000LookAndFeel::kChassisBlack);
}

void CasioCTK1000AudioProcessorEditor::resized()
{
    const float scaleX = static_cast<float>(getWidth())  / static_cast<float>(kBaseWidth);
    const float scaleY = static_cast<float>(getHeight()) / static_cast<float>(kBaseHeight);
    const float scale  = std::min(scaleX, scaleY);

    mainCanvas.setTransform(juce::AffineTransform::scale(scale));
    mainCanvas.setBounds(0, 0, kBaseWidth, kBaseHeight);

    if (!isInitializingSize)
    {
        syncUIScaleComboBoxToFactor(scale);
        processorRef.getSampleBankManager().saveUIScaleToProperties(scale);
    }
}

void CasioCTK1000AudioProcessorEditor::paintCanvas(juce::Graphics& g)
{
    // 1. Matte dark charcoal-black outer chassis (#1B1C1E)
    g.fillAll(CasioIXA::CTK1000LookAndFeel::kChassisBlack);

    // Subtle diagonal speaker vent-grille lines in top-left and top-right corners (nodding to 1993 CTK-1000 molding)
    g.setColour(juce::Colour(0xff111214));
    for (int i = 0; i < 14; ++i)
    {
        const float offset = static_cast<float>(i * 7);
        g.drawLine(6.0f + offset, 4.0f, 2.0f + offset, 26.0f, 2.0f);
        g.drawLine(static_cast<float>(kBaseWidth) - 98.0f + offset, 4.0f,
                   static_cast<float>(kBaseWidth) - 102.0f + offset, 26.0f, 2.0f);
    }

    // 2. Top Center Console Bezel Frame
    drawHardwareSectionFrame(g, topBezelBounds, "CASIO CTK-1000 // IXA INTEGRATED CROSS-SOUND WORKSTATION (NEC uPD939GD)");

    // Left Branding inside Top Bezel (widened to 268 px so sub-header never clips)
    {
        auto brandArea = juce::Rectangle<int>(topBezelBounds.getX() + 12, topBezelBounds.getY() + 24, 268, 58);
        g.setFont(CasioIXA::CTK1000LookAndFeel::makeFont(22.0f, true, false));
        g.setColour(CasioIXA::CTK1000LookAndFeel::kOffWhite);
        g.drawText("CASIO CTK-1000", brandArea.removeFromTop(26), juce::Justification::centredLeft, false);

        g.setFont(CasioIXA::CTK1000LookAndFeel::makeFont(10.0f, true, false));
        g.setColour(CasioIXA::CTK1000LookAndFeel::kVermilion);
        g.drawText("IXA SOUND SOURCE // R&D ENGINEER'S EDITION", brandArea.removeFromTop(15), juce::Justification::centredLeft, false);

        g.setFont(CasioIXA::CTK1000LookAndFeel::makeFont(9.2f, false, true));
        g.setColour(CasioIXA::CTK1000LookAndFeel::kAmberGold);
        g.drawText("US PAT. 5,164,530 (TWM) + 5,319,151 (PCM)", brandArea, juce::Justification::centredLeft, false);
    }

    // Center 7-Segment LED Readout Window
    {
        auto& apvts = processorRef.getAPVTS();
        const int crossIdx = static_cast<int>(apvts.getRawParameterValue("CROSS_MODE")->load());
        const int routeIdx = static_cast<int>(apvts.getRawParameterValue("ROUTING_MODE")->load());
        const bool dac1993 = apvts.getRawParameterValue("HARDWARE_DAC_MODE")->load() > 0.5f;

        static const char* kCrossNames[3] = { "PURE IXA", "HYBRID PCM+IXA", "DUAL PCM" };
        static const char* kRouteNames[3] = { "PARALLEL", "2-OP CASCADE", "SINGLE OP1" };

        const juce::String mainLine = juce::String(kCrossNames[std::clamp(crossIdx, 0, 2)])
                                    + " / " + juce::String(kRouteNames[std::clamp(routeIdx, 0, 2)]);
        const juce::String subLine  = "A:" + processorRef.getSlotAInstrumentName().toUpperCase()
                                    + "  B:" + processorRef.getSlotBInstrumentName().toUpperCase()
                                    + (dac1993 ? "  [22.05kHz]" : "  [2x OVS]");

        drawSevenSegmentDisplayBezel(g, centerLedWindowBounds,
                                     "MULTI-FUNCTION LED DISPLAY",
                                     mainLine,
                                     subLine);
    }

    // SA-51 Sample Bank Path Readout LED Bar
    {
        const auto fRect = sa51PathLedBounds.toFloat();
        g.setColour(CasioIXA::CTK1000LookAndFeel::kLedBezelDark);
        g.fillRoundedRectangle(fRect, 2.5f);
        g.setColour(CasioIXA::CTK1000LookAndFeel::kBevelBorder);
        g.drawRoundedRectangle(fRect, 2.5f, 1.0f);

        g.setFont(CasioIXA::CTK1000LookAndFeel::makeFont(11.0f, true, true));
        g.setColour(CasioIXA::CTK1000LookAndFeel::kLedCrimson);
        g.drawText(sampleBankStatusText, sa51PathLedBounds.reduced(8, 0), juce::Justification::centredLeft, true);
    }

    // 3. Left Panel — Dual PCM Sample Engine Frames
    drawHardwareSectionFrame(g, pcmABounds, "PCM ELEMENT A (16-BIT 22.05kHz DUAL-LOOP ENGINE)");
    drawHardwareSectionFrame(g, pcmBBounds, "PCM ELEMENT B (SECONDARY LAYER / SPLIT ELEMENT)");

    // 4. Center Panel — Dual-Operator IXA Synthesis Engine & 12dB LPF Frames
    drawHardwareSectionFrame(g, op1Bounds, "IXA OPERATOR 1 (CARRIER WARP W_a + WAVEFOLDER DELTA)");
    drawHardwareSectionFrame(g, op2Bounds, "IXA OPERATOR 2 (CASCADE MODULATOR / PARALLEL DETUNE OP)");
    drawHardwareSectionFrame(g, ixaLpfBounds, "IXA 12dB LPF");

    // 5. Bottom-Left Panel — Legacy 1993 CTK-1000 Tone Editor Override
    drawHardwareSectionFrame(g, legacyMacroBounds, "1993 CTK-1000 FRONT-PANEL TONE EDITOR (0-9 MACRO OVERRIDE)");
    {
        auto& apvts = processorRef.getAPVTS();
        const bool macroOn = apvts.getRawParameterValue("LEGACY_MACRO_ENABLE")->load() > 0.5f;
        const int wStep = static_cast<int>(apvts.getRawParameterValue("MACRO_WAVE")->load());
        const int aStep = static_cast<int>(apvts.getRawParameterValue("MACRO_ATTACK")->load());
        const int rStep = static_cast<int>(apvts.getRawParameterValue("MACRO_RELEASE")->load());

        const juce::String digits = juce::String::formatted("W:%d  A:%d  R:%d", wStep, aStep, rStep);
        drawSevenSegmentDisplayBezel(g,
                                     legacyLedReadoutBounds,
                                     macroOn ? "ACTIVE: HARDWARE STEPPED TABLES" : "STANDBY: ENGINEER CONTINUOUS",
                                     digits,
                                     {});
    }

    // 6. Bottom-Right Panel — 8-Mode Semi-OBS DSP Effect Unit (Sanyo LC33832PL-70)
    drawHardwareSectionFrame(g, dspBounds, "SEMI-OBS DSP EFFECT PROCESSOR (SANYO LC33832PL-70 32KB PSRAM)");
    {
        // Draw simulated red hardware LED matrix dots for the 8 CTK-1000 DSP modes
        const int activeMode = static_cast<int>(processorRef.getAPVTS().getRawParameterValue("DSP_MODE")->load());
        const bool bypassed  = processorRef.getAPVTS().getRawParameterValue("DSP_BYPASS")->load() > 0.5f;

        static const char* kShortModeLabels[8] = {
            "HALL", "DELAY", "CHORUS", "DIST", "ROOM", "ST.DLY", "FLANGE", "CH+REV"
        };

        const int cols = 4;
        const int cellW = dspLedMatrixBounds.getWidth() / cols;
        const int cellH = dspLedMatrixBounds.getHeight() / 2;

        g.setFont(CasioIXA::CTK1000LookAndFeel::makeFont(9.5f, true, false));
        for (int m = 0; m < 8; ++m)
        {
            const int r = m / cols;
            const int c = m % cols;
            const juce::Rectangle<int> cell(dspLedMatrixBounds.getX() + c * cellW,
                                            dspLedMatrixBounds.getY() + r * cellH,
                                            cellW, cellH);

            const bool isLit = (!bypassed && m == activeMode);
            const float dotR = 3.8f;
            const float dotX = static_cast<float>(cell.getX() + 4);
            const float dotY = static_cast<float>(cell.getCentreY()) - dotR;

            g.setColour(CasioIXA::CTK1000LookAndFeel::kLedBezelDark);
            g.fillEllipse(dotX - 1.0f, dotY - 1.0f, (dotR + 1.0f) * 2.0f, (dotR + 1.0f) * 2.0f);

            if (isLit)
            {
                g.setColour(CasioIXA::CTK1000LookAndFeel::kLedCrimson.withAlpha(0.35f));
                g.fillEllipse(dotX - 2.5f, dotY - 2.5f, (dotR + 2.5f) * 2.0f, (dotR + 2.5f) * 2.0f);
                g.setColour(CasioIXA::CTK1000LookAndFeel::kLedCrimson);
                g.fillEllipse(dotX, dotY, dotR * 2.0f, dotR * 2.0f);
            }
            else
            {
                g.setColour(juce::Colour(0xff4a1212));
                g.fillEllipse(dotX, dotY, dotR * 2.0f, dotR * 2.0f);
            }

            g.setColour(isLit ? CasioIXA::CTK1000LookAndFeel::kOffWhite
                              : CasioIXA::CTK1000LookAndFeel::kAmberGold.withAlpha(0.75f));
            g.drawText(kShortModeLabels[m],
                       cell.withTrimmedLeft(15),
                       juce::Justification::centredLeft, false);
        }
    }

    // 7. Phase 6: Dual Assignable Multi-Target LFO Panels with Live Pulsing Rate LEDs
    drawHardwareSectionFrame(g, lfo1Bounds, "MODULATION LFO 1 (CC#1 MOD WHEEL + DELAYED VIBRATO / MULTI-TARGET)");
    drawHardwareSectionFrame(g, lfo2Bounds, "MODULATION LFO 2 (AUXILIARY WAVEFOLDER / FILTER / PCM CROSSFADE LFO)");

    auto drawPulsingLfoHeaderLed = [&](juce::Rectangle<int> boxBounds, double phase, const juce::String& prefix)
    {
        const int targetIdx = static_cast<int>(processorRef.getAPVTS().getRawParameterValue(prefix + "TARGET")->load());
        const float depthVal = std::abs(processorRef.getAPVTS().getRawParameterValue(prefix + "DEPTH")->load());
        const bool isActive = (targetIdx != 0) && (depthVal > 0.005f || prefix == "LFO1_");

        const float pulse = 0.5f + 0.5f * static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * phase));
        const float ledR = 4.2f;
        const float ledX = static_cast<float>(boxBounds.getRight() - 18);
        const float ledY = static_cast<float>(boxBounds.getY() + 10) - ledR;

        g.setColour(CasioIXA::CTK1000LookAndFeel::kLedBezelDark);
        g.fillEllipse(ledX - 1.0f, ledY - 1.0f, (ledR + 1.0f) * 2.0f, (ledR + 1.0f) * 2.0f);

        if (isActive)
        {
            g.setColour(CasioIXA::CTK1000LookAndFeel::kLedCrimson.withAlpha(0.20f + 0.65f * pulse));
            g.fillEllipse(ledX - 2.5f, ledY - 2.5f, (ledR + 2.5f) * 2.0f, (ledR + 2.5f) * 2.0f);
            g.setColour(CasioIXA::CTK1000LookAndFeel::kLedCrimson.withAlpha(0.35f + 0.65f * pulse));
            g.fillEllipse(ledX, ledY, ledR * 2.0f, ledR * 2.0f);
        }
        else
        {
            g.setColour(juce::Colour(0xff4a1212));
            g.fillEllipse(ledX, ledY, ledR * 2.0f, ledR * 2.0f);
        }
    };

    drawPulsingLfoHeaderLed(lfo1Bounds, guiLfo1Phase, "LFO1_");
    drawPulsingLfoHeaderLed(lfo2Bounds, guiLfo2Phase, "LFO2_");
}

void CasioCTK1000AudioProcessorEditor::layoutCanvas()
{
    auto fullArea = juce::Rectangle<int>(0, 0, kBaseWidth, kBaseHeight).reduced(8, 6);

    // Bottom MIDI Keyboard Strip (48 px) — dynamically sized so 36 white keys (36..96) fill 100% width
    auto keyboardArea = fullArea.removeFromBottom(48);
    midiKeyboard.setBounds(keyboardArea);
    midiKeyboard.setAvailableRange(36, 96);
    midiKeyboard.setKeyWidth(static_cast<float>(keyboardArea.getWidth()) / 36.0f);
    fullArea.removeFromBottom(6);

    // 1. Top Console Bezel (114 px)
    topBezelBounds = fullArea.removeFromTop(114);
    {
        auto innerTop = topBezelBounds.reduced(10, 6);
        innerTop.removeFromTop(18); // Header bar clearance

        // Top-Right Live IXA Wavefolder Oscilloscope (276 px wide)
        auto scopeRect = innerTop.removeFromRight(276);
        wavefolderScope.setBounds(scopeRect);
        innerTop.removeFromRight(8);

        // Bottom row inside Top Bezel: SA-51 Folder Loader Button + Red LED Path Readout + UI Scale Selector
        auto loaderRow = innerTop.removeFromBottom(24);
        loadSa51FolderButton.setBounds(loaderRow.removeFromLeft(215));
        loaderRow.removeFromLeft(8);
        uiScaleBox.setBounds(loaderRow.removeFromRight(138));
        loaderRow.removeFromRight(8);
        sa51PathLedBounds = loaderRow;

        innerTop.removeFromBottom(4);

        // Upper row inside Top Bezel: Left branding space (268 px), Center LED Window, Mode Combos, DAC Toggles & Knobs
        innerTop.removeFromLeft(268);

        centerLedWindowBounds = innerTop.removeFromLeft(210);
        innerTop.removeFromLeft(6);

        auto modeCol = innerTop.removeFromLeft(136);
        crossModeCombo.setBounds(modeCol.removeFromTop(26));
        modeCol.removeFromTop(4);
        routingModeCombo.setBounds(modeCol.removeFromTop(26));
        innerTop.removeFromLeft(6);

        auto btnCol = innerTop.removeFromLeft(124);
        dac1993Button.setBounds(btnCol.removeFromTop(26));
        btnCol.removeFromTop(4);
        adaaButton.setBounds(btnCol.removeFromTop(26));
        innerTop.removeFromLeft(6);

        // Remaining width (~116 px = 58 px per knob) ensures "IXA PAN" and "MASTER" labels never clip
        layoutKnobGrid({ &synthPanKnob, &masterGainKnob }, innerTop, 2);
    }

    fullArea.removeFromTop(6);

    // Phase 6 Row: Dual Assignable Multi-Target LFO Strip (90 px) right above the MIDI Keyboard
    auto lfoRowArea = fullArea.removeFromBottom(90);
    fullArea.removeFromBottom(6);

    // Legacy 0-9 Tone Editor Override (Left) + 8-Mode Semi-OBS DSP Effect Processor (Right) (142 px)
    auto bottomPanelsArea = fullArea.removeFromBottom(142);
    fullArea.removeFromBottom(6);

    // Middle Workstation Area: Left Column = Dual PCM Slots (430 px), Right Column = Dual IXA Operators + 12dB LPF
    auto pcmColumn = fullArea.removeFromLeft(430);
    fullArea.removeFromLeft(8);
    auto ixaColumn = fullArea;

    //==============================================================================
    // 2. Layout Left Panel — Dual PCM Sample Engine (Element A & Element B)
    const int pcmSlotHeight = (pcmColumn.getHeight() - 6) / 2;
    pcmABounds = pcmColumn.removeFromTop(pcmSlotHeight);
    pcmColumn.removeFromTop(6);
    pcmBBounds = pcmColumn;

    auto layoutPCMSlot = [](PCMSlotControls& slot, juce::Rectangle<int> bounds)
    {
        auto inner = bounds.reduced(8, 5);
        inner.removeFromTop(18); // Header clearance

        auto topBar = inner.removeFromTop(24);
        slot.enableBtn.setBounds(topBar.removeFromLeft(52));
        topBar.removeFromLeft(6);
        slot.instrumentCombo.setBounds(topBar.removeFromLeft(158));
        topBar.removeFromLeft(6);
        slot.loopModeCombo.setBounds(topBar.removeFromLeft(118));
        topBar.removeFromLeft(6);
        slot.loopOverrideBtn.setBounds(topBar);

        inner.removeFromTop(4);

        layoutKnobGrid({
            &slot.level, &slot.pan, &slot.coarse, &slot.fine, &slot.velSens, &slot.loopStart,
            &slot.loopEnd, &slot.attack, &slot.decay, &slot.sustain, &slot.release
        }, inner, 6);
    };

    layoutPCMSlot(pcmSlotA, pcmABounds);
    layoutPCMSlot(pcmSlotB, pcmBBounds);

    //==============================================================================
    // 3. Layout Center/Right Panel — Dual-Operator IXA Synthesis Engine + Dedicated 12dB LPF Sub-Panel
    ixaLpfBounds = ixaColumn.removeFromRight(96);
    ixaColumn.removeFromRight(6);

    {
        auto lpfInner = ixaLpfBounds.reduced(6, 6);
        lpfInner.removeFromTop(18); // Header clearance
        layoutKnobGrid({ &ixaLpfCutoffKnob, &ixaLpfEnvKnob, &ixaLpfKeyTrackKnob }, lpfInner, 1);
    }

    const int opHeight = (ixaColumn.getHeight() - 6) / 2;
    op1Bounds = ixaColumn.removeFromTop(opHeight);
    ixaColumn.removeFromTop(6);
    op2Bounds = ixaColumn;

    auto layoutIXAOp = [](IXAOperatorControls& op, juce::Rectangle<int> bounds)
    {
        auto inner = bounds.reduced(8, 5);
        inner.removeFromTop(18); // Header clearance

        layoutKnobGrid({
            &op.coarseRatio, &op.fineDetune, &op.baseIndex, &op.envDepth,
            &op.velDepth,    &op.carrierWarp, &op.feedback,  &op.level,
            &op.modA,        &op.modD,        &op.modS,      &op.modR,
            &op.ampA,        &op.ampD,        &op.ampS,      &op.ampR
        }, inner, 8);
    };

    layoutIXAOp(op1Controls, op1Bounds);
    layoutIXAOp(op2Controls, op2Bounds);

    //==============================================================================
    // 4. Layout Bottom-Left Panel — Legacy 1993 CTK-1000 Tone Editor Override
    legacyMacroBounds = bottomPanelsArea.removeFromLeft(430);
    bottomPanelsArea.removeFromLeft(8);
    dspBounds = bottomPanelsArea;

    {
        auto inner = legacyMacroBounds.reduced(8, 5);
        inner.removeFromTop(18);

        auto leftCol = inner.removeFromLeft(185);
        legacyMacroEnableBtn.setBounds(leftCol.removeFromTop(26));
        leftCol.removeFromTop(4);
        legacyLedReadoutBounds = leftCol;

        inner.removeFromLeft(8);
        layoutKnobGrid({ &macroWaveKnob, &macroAttackKnob, &macroReleaseKnob }, inner, 3);
    }

    //==============================================================================
    // 5. Layout Bottom-Right Panel — 8-Mode Semi-OBS DSP Effect Unit (Sanyo LC33832PL-70)
    {
        auto inner = dspBounds.reduced(8, 5);
        inner.removeFromTop(18);

        auto leftCol = inner.removeFromLeft(280);
        auto topCtrlRow = leftCol.removeFromTop(26);
        dspModeCombo.setBounds(topCtrlRow.removeFromLeft(145));
        topCtrlRow.removeFromLeft(6);
        dspLofiPsramBtn.setBounds(topCtrlRow);

        leftCol.removeFromTop(4);
        auto botCtrlRow = leftCol.removeFromBottom(24);
        dspBypassBtn.setBounds(botCtrlRow.removeFromLeft(125));

        dspLedMatrixBounds = leftCol.reduced(2, 2);

        inner.removeFromLeft(10);
        layoutKnobGrid({ &dspParam1Knob, &dspParam2Knob, &dspToneKnob, &dspMixKnob }, inner, 4);
    }

    //==============================================================================
    // 6. Layout Phase 6 Dual Assignable Multi-Target LFO Strip (LFO 1 Left, LFO 2 Right)
    const int halfLfoW = (lfoRowArea.getWidth() - 8) / 2;
    lfo1Bounds = lfoRowArea.removeFromLeft(halfLfoW);
    lfoRowArea.removeFromLeft(8);
    lfo2Bounds = lfoRowArea;

    auto layoutLFOBox = [](LFOControlGroup& lfo, juce::Rectangle<int> bounds)
    {
        auto inner = bounds.reduced(8, 4);
        inner.removeFromTop(18); // Header bar clearance

        auto leftSelectors = inner.removeFromLeft(275);
        auto row1 = leftSelectors.removeFromTop(26);
        lfo.waveCombo.setBounds(row1.removeFromLeft(165));
        row1.removeFromLeft(6);
        lfo.syncBtn.setBounds(row1);

        leftSelectors.removeFromTop(6);
        lfo.targetCombo.setBounds(leftSelectors.removeFromTop(26));

        inner.removeFromLeft(10);
        layoutKnobGrid({ &lfo.rateKnob, &lfo.delayKnob, &lfo.depthKnob }, inner, 3);
    };

    layoutLFOBox(lfo1Controls, lfo1Bounds);
    layoutLFOBox(lfo2Controls, lfo2Bounds);
}
