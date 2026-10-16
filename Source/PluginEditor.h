#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include <memory>
#include <vector>
#include <array>

namespace CasioIXA
{

/**
 * Authentic 1993 Casio CTK-1000 Hardware LookAndFeel (R&D Engineer's Workstation Edition)
 * Strictly uses JUCE 8 FontOptions API and implements the exact CTK-1000 color palette:
 *  - Matte Charcoal Chassis (#1B1C1E)
 *  - Recessed Jet-Black Console Windows (#0D0E10) with Bevel Borders (#2E3035)
 *  - Casio Vermilion / Orange-Red Silkscreen & Arcs (#E84A27)
 *  - Warm Amber-Gold Sub-Labels (#D9822B) & Crisp Off-White Text (#EAEAEA)
 *  - Glowing Crimson-Red 7-Segment LEDs & Discrete Indicator Dots (#FF1E1E) on Maroon-Black (#1A0505)
 *  - Royal Blue (#2B4C8C) and Crimson Red (#A62639) Hardware Accent Buttons
 */
class CTK1000LookAndFeel : public juce::LookAndFeel_V4
{
public:
    static inline const juce::Colour kChassisBlack   { 0xff1b1c1e };
    static inline const juce::Colour kRecessedBlack  { 0xff0d0e10 };
    static inline const juce::Colour kBevelBorder    { 0xff2e3035 };
    static inline const juce::Colour kVermilion      { 0xffe84a27 };
    static inline const juce::Colour kAmberGold      { 0xffd9822b };
    static inline const juce::Colour kOffWhite       { 0xffeaeaea };
    static inline const juce::Colour kLedCrimson     { 0xffff1e1e };
    static inline const juce::Colour kLedBezelDark   { 0xff1a0505 };
    static inline const juce::Colour kButtonMatte    { 0xff2a2c30 };
    static inline const juce::Colour kRoyalBlue      { 0xff2b4c8c };
    static inline const juce::Colour kAccentCrimson  { 0xffa62639 };

    CTK1000LookAndFeel()
    {
        setColour(juce::ResizableWindow::backgroundColourId, kChassisBlack);
        setColour(juce::Label::textColourId, kOffWhite);

        setColour(juce::ComboBox::backgroundColourId, kRecessedBlack);
        setColour(juce::ComboBox::textColourId, kOffWhite);
        setColour(juce::ComboBox::outlineColourId, kBevelBorder);
        setColour(juce::ComboBox::arrowColourId, kVermilion);

        setColour(juce::PopupMenu::backgroundColourId, kRecessedBlack);
        setColour(juce::PopupMenu::textColourId, kOffWhite);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, kVermilion);
        setColour(juce::PopupMenu::highlightedTextColourId, kOffWhite);

        setColour(juce::Slider::rotarySliderFillColourId, kVermilion);
        setColour(juce::Slider::thumbColourId, kOffWhite);
        setColour(juce::Slider::textBoxTextColourId, kAmberGold);
        setColour(juce::Slider::textBoxBackgroundColourId, kRecessedBlack);
        setColour(juce::Slider::textBoxOutlineColourId, kBevelBorder);

        setColour(juce::TextButton::buttonColourId, kButtonMatte);
        setColour(juce::TextButton::buttonOnColourId, kAccentCrimson);
        setColour(juce::TextButton::textColourOffId, kOffWhite);
        setColour(juce::TextButton::textColourOnId, kOffWhite);
    }

    static juce::Font makeFont(float height, bool bold = false, bool monospaced = false)
    {
        juce::FontOptions opts(height);
        if (monospaced)
            opts = opts.withName(juce::Font::getDefaultMonospacedFontName());
        if (bold)
            opts = opts.withStyle("Bold");
        return juce::Font(opts);
    }

    juce::Font getLabelFont(juce::Label&) override
    {
        return makeFont(11.5f, false, false);
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override
    {
        return makeFont(12.0f, true, true);
    }

    juce::Font getPopupMenuFont() override
    {
        return makeFont(12.0f, false, true);
    }

    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override
    {
        return makeFont( std::min(12.0f, buttonHeight * 0.50f), true, false );
    }

    void drawRotarySlider(juce::Graphics& g,
                          int x, int y, int width, int height,
                          float sliderPosProportional,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(2.5f);
        const float radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();
        const float rx = cx - radius;
        const float ry = cy - radius;
        const float rw = radius * 2.0f;
        const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

        // Recessed outer track arc
        juce::Path bgArc;
        bgArc.addCentredArc(cx, cy, radius * 0.88f, radius * 0.88f, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(kRecessedBlack);
        g.strokePath(bgArc, juce::PathStrokeType(3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Active Vermilion (#E84A27) value arc
        juce::Path valArc;
        valArc.addCentredArc(cx, cy, radius * 0.88f, radius * 0.88f, 0.0f, rotaryStartAngle, angle, true);
        const bool isAccent = slider.getProperties().contains("amberArc");
        g.setColour(isAccent ? kAmberGold : kVermilion);
        g.strokePath(valArc, juce::PathStrokeType(3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Hardware knurled cap body
        const float capRadius = radius * 0.66f;
        g.setColour(juce::Colour(0xff232529));
        g.fillEllipse(cx - capRadius, cy - capRadius, capRadius * 2.0f, capRadius * 2.0f);
        g.setColour(kBevelBorder);
        g.drawEllipse(cx - capRadius, cy - capRadius, capRadius * 2.0f, capRadius * 2.0f, 1.2f);

        // Crisp pointer line
        juce::Path pointer;
        const float pLen = capRadius * 0.85f;
        pointer.addRectangle(-1.2f, -pLen, 2.4f, pLen * 0.65f);
        pointer.applyTransform(juce::AffineTransform::rotation(angle).translated(cx, cy));
        g.setColour(kOffWhite);
        g.fillPath(pointer);

        juce::ignoreUnused(rx, ry, rw);
    }

    void drawButtonBackground(juce::Graphics& g,
                              juce::Button& button,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
        juce::Colour baseCol = backgroundColour;

        if (button.getProperties().contains("royalBlue"))
            baseCol = kRoyalBlue;
        else if (button.getProperties().contains("crimsonBtn"))
            baseCol = button.getToggleState() ? kAccentCrimson : juce::Colour(0xff42161e);
        else if (button.getClickingTogglesState() && button.getToggleState())
            baseCol = juce::Colour(0xff38221e);

        if (shouldDrawButtonAsDown)
            baseCol = baseCol.darker(0.25f);
        else if (shouldDrawButtonAsHighlighted)
            baseCol = baseCol.brighter(0.12f);

        g.setColour(baseCol);
        g.fillRoundedRectangle(bounds, 3.0f);

        g.setColour(button.getToggleState() ? kVermilion : kBevelBorder);
        g.drawRoundedRectangle(bounds, 3.0f, 1.2f);

        // Draw discrete circular red LED indicator dot on toggle buttons
        if (button.getClickingTogglesState())
        {
            const float ledR = 4.0f;
            const float ledX = bounds.getX() + 8.0f;
            const float ledY = bounds.getCentreY() - ledR;

            g.setColour(kLedBezelDark);
            g.fillEllipse(ledX - 1.0f, ledY - 1.0f, (ledR + 1.0f) * 2.0f, (ledR + 1.0f) * 2.0f);

            if (button.getToggleState())
            {
                g.setColour(kLedCrimson.withAlpha(0.35f));
                g.fillEllipse(ledX - 2.5f, ledY - 2.5f, (ledR + 2.5f) * 2.0f, (ledR + 2.5f) * 2.0f);
                g.setColour(kLedCrimson);
                g.fillEllipse(ledX, ledY, ledR * 2.0f, ledR * 2.0f);
            }
            else
            {
                g.setColour(juce::Colour(0xff4a1212));
                g.fillEllipse(ledX, ledY, ledR * 2.0f, ledR * 2.0f);
            }
        }
    }

    void drawButtonText(juce::Graphics& g,
                        juce::TextButton& button,
                        bool /*isMouseOverButton*/,
                        bool /*isButtonDown*/) override
    {
        g.setFont(getTextButtonFont(button, button.getHeight()));
        g.setColour(button.findColour(button.getToggleState()
                                      ? juce::TextButton::textColourOnId
                                      : juce::TextButton::textColourOffId));

        auto textBounds = button.getLocalBounds();
        if (button.getClickingTogglesState())
            textBounds.removeFromLeft(18);

        g.drawFittedText(button.getButtonText(), textBounds.reduced(4, 1), juce::Justification::centred, 1);
    }

    void drawComboBox(juce::Graphics& g,
                      int width, int height,
                      bool /*isButtonDown*/,
                      int /*buttonX*/, int /*buttonY*/, int /*buttonW*/, int /*buttonH*/,
                      juce::ComboBox& box) override
    {
        const auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat().reduced(0.5f);
        g.setColour(kRecessedBlack);
        g.fillRoundedRectangle(bounds, 2.5f);

        g.setColour(kBevelBorder);
        g.drawRoundedRectangle(bounds, 2.5f, 1.0f);

        // Vermilion indicator triangle
        juce::Path arrow;
        const float ax = width - 14.0f;
        const float ay = height * 0.5f;
        arrow.addTriangle(ax - 4.0f, ay - 2.5f, ax + 4.0f, ay - 2.5f, ax, ay + 3.0f);
        g.setColour(kVermilion);
        g.fillPath(arrow);

        juce::ignoreUnused(box);
    }
};

/**
 * Live IXA Wavefolder Transfer & Real-Time Audio Oscilloscope Display
 * Plots the exact analytical US Patent 5,164,530 wavefolder cycle Delta(W_alpha(phi) + n sin(2*pi*r*phi))
 * superimposed with live audio output from the processor's lock-free ring buffer.
 */
class IXAWavefolderScopeComponent : public juce::Component
{
public:
    explicit IXAWavefolderScopeComponent(CasioCTK1000AudioProcessor& proc)
        : processor(proc)
    {
        scopeSamples.resize(256, 0.0f);
    }

    void updateScope()
    {
        processor.copyLatestScopeSamples(scopeSamples, 256);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();

        // Recessed console bezel
        g.setColour(CTK1000LookAndFeel::kRecessedBlack);
        g.fillRoundedRectangle(bounds, 3.0f);
        g.setColour(CTK1000LookAndFeel::kBevelBorder);
        g.drawRoundedRectangle(bounds, 3.0f, 1.2f);

        const auto plotArea = bounds.reduced(6.0f, 16.0f);

        // Amber-gold graticule grid
        g.setColour(CTK1000LookAndFeel::kAmberGold.withAlpha(0.16f));
        for (int i = 1; i < 4; ++i)
        {
            const float y = plotArea.getY() + plotArea.getHeight() * (static_cast<float>(i) / 4.0f);
            g.drawHorizontalLine(static_cast<int>(y), plotArea.getX(), plotArea.getRight());
        }
        for (int i = 1; i < 8; ++i)
        {
            const float x = plotArea.getX() + plotArea.getWidth() * (static_cast<float>(i) / 8.0f);
            g.drawVerticalLine(static_cast<int>(x), plotArea.getY(), plotArea.getBottom());
        }

        // Read current Op 1 & Op 2 continuous parameters for analytical wavefolder transfer preview
        auto& apvts = processor.getAPVTS();
        const double r1     = static_cast<double>(apvts.getRawParameterValue("OP1_COARSE_RATIO")->load());
        const double n1     = static_cast<double>(apvts.getRawParameterValue("OP1_BASE_INDEX")->load());
        const double alpha1 = static_cast<double>(apvts.getRawParameterValue("OP1_CARRIER_WARP")->load());
        const double beta1  = static_cast<double>(apvts.getRawParameterValue("OP1_FEEDBACK")->load());
        const double lvl1   = static_cast<double>(apvts.getRawParameterValue("OP1_LEVEL")->load());

        const double r2     = static_cast<double>(apvts.getRawParameterValue("OP2_COARSE_RATIO")->load());
        const double n2     = static_cast<double>(apvts.getRawParameterValue("OP2_BASE_INDEX")->load());
        const double alpha2 = static_cast<double>(apvts.getRawParameterValue("OP2_CARRIER_WARP")->load());
        const double lvl2   = static_cast<double>(apvts.getRawParameterValue("OP2_LEVEL")->load());
        const int routing   = static_cast<int>(apvts.getRawParameterValue("ROUTING_MODE")->load());
        const double lpfCutoffHz = static_cast<double>(apvts.getRawParameterValue("IXA_LPF_CUTOFF")->load());

        juce::Path transferPath;
        const int numPts = static_cast<int>(plotArea.getWidth());
        double prevY1 = 0.0;

        // Visual 2-pole Butterworth preview filter (normalized to a 261.6 Hz C4 fundamental cycle)
        IXAZdfButterworthLPF previewLpf;
        const double previewSr = std::max(128.0, static_cast<double>(numPts) * 261.6256);
        previewLpf.prepare(previewSr);
        previewLpf.updateCoefficients(lpfCutoffHz);
        const bool applyPreviewLpf = (lpfCutoffHz < 19800.0);

        // Pre-warm 1 cycle so steady-state filtered waveform is plotted without startup transient
        if (applyPreviewLpf)
        {
            for (int px = 0; px < numPts; ++px)
            {
                const double phi = static_cast<double>(px) / static_cast<double>(std::max(1, numPts - 1));
                const double w2 = IXAOscillator::computeWarpedPhase(phi, alpha2);
                const double y2 = IXAOscillator::evaluateWavefolder(w2 + n2 * std::sin(juce::MathConstants<double>::twoPi * r2 * phi));
                const double w1 = IXAOscillator::computeWarpedPhase(phi, alpha1);
                const double extPhase = (routing == 1) ? (y2 * lvl2 * 4.0) : 0.0;
                const double y1 = IXAOscillator::evaluateWavefolder(w1 + n1 * std::sin(juce::MathConstants<double>::twoPi * r1 * phi) + extPhase + beta1 * prevY1);
                prevY1 = y1;
                const double raw = (routing == 0) ? ((y1 * lvl1 + y2 * lvl2) * 0.65) : (y1 * lvl1);
                (void)previewLpf.processSample(raw);
            }
        }

        for (int px = 0; px < numPts; ++px)
        {
            const double phi = static_cast<double>(px) / static_cast<double>(std::max(1, numPts - 1));
            const double w2 = IXAOscillator::computeWarpedPhase(phi, alpha2);
            const double x2 = w2 + n2 * std::sin(juce::MathConstants<double>::twoPi * r2 * phi);
            const double y2 = IXAOscillator::evaluateWavefolder(x2);

            const double w1 = IXAOscillator::computeWarpedPhase(phi, alpha1);
            const double extPhase = (routing == 1) ? (y2 * lvl2 * 4.0) : 0.0;
            const double x1 = w1 + n1 * std::sin(juce::MathConstants<double>::twoPi * r1 * phi) + extPhase + beta1 * prevY1;
            const double y1 = IXAOscillator::evaluateWavefolder(x1);
            prevY1 = y1;

            double combined = y1 * lvl1;
            if (routing == 0)
                combined = (y1 * lvl1 + y2 * lvl2) * 0.65;

            if (applyPreviewLpf)
                combined = previewLpf.processSample(combined);

            combined = std::clamp(combined, -1.0, 1.0);
            const float sx = plotArea.getX() + static_cast<float>(px);
            const float sy = plotArea.getCentreY() - static_cast<float>(combined) * (plotArea.getHeight() * 0.44f);

            if (px == 0)
                transferPath.startNewSubPath(sx, sy);
            else
                transferPath.lineTo(sx, sy);
        }

        // Glow + Crisp Vermilion curve (#E84A27)
        g.setColour(CTK1000LookAndFeel::kVermilion.withAlpha(0.28f));
        g.strokePath(transferPath, juce::PathStrokeType(3.5f));
        g.setColour(CTK1000LookAndFeel::kVermilion);
        g.strokePath(transferPath, juce::PathStrokeType(1.6f));

        // Superimpose live audio output trace in Amber-Gold if signal is present
        float peak = 0.0f;
        for (float s : scopeSamples)
            peak = std::max(peak, std::abs(s));

        if (peak > 0.002f && !scopeSamples.empty())
        {
            juce::Path audioPath;
            const size_t N = scopeSamples.size();
            for (size_t i = 0; i < N; ++i)
            {
                const float normX = static_cast<float>(i) / static_cast<float>(N - 1);
                const float sx = plotArea.getX() + normX * plotArea.getWidth();
                const float clampedS = std::clamp(scopeSamples[i], -1.0f, 1.0f);
                const float sy = plotArea.getCentreY() - clampedS * (plotArea.getHeight() * 0.44f);
                if (i == 0)
                    audioPath.startNewSubPath(sx, sy);
                else
                    audioPath.lineTo(sx, sy);
            }
            g.setColour(CTK1000LookAndFeel::kAmberGold.withAlpha(0.80f));
            g.strokePath(audioPath, juce::PathStrokeType(1.2f));
        }

        // Scope Header & Footer captions
        g.setFont(CTK1000LookAndFeel::makeFont(10.0f, true, true));
        g.setColour(CTK1000LookAndFeel::kAmberGold);
        g.drawText("IXA WAVEFOLDER DELTA(W_a(phi) + n*sin)",
                   bounds.reduced(6.0f, 2.0f).removeFromTop(13.0f),
                   juce::Justification::centredLeft, false);

        g.setColour(CTK1000LookAndFeel::kLedCrimson);
        g.drawText(juce::String::formatted("n0:%.2f a:%.2f LPF:%.0fHz", n1, alpha1, lpfCutoffHz),
                   bounds.reduced(6.0f, 2.0f).removeFromBottom(13.0f),
                   juce::Justification::centredRight, false);
    }

private:
    CasioCTK1000AudioProcessor& processor;
    std::vector<float> scopeSamples;
};

} // namespace CasioIXA

//==============================================================================
class CasioCTK1000AudioProcessorEditor : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit CasioCTK1000AudioProcessorEditor(CasioCTK1000AudioProcessor&);
    ~CasioCTK1000AudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void openSampleFolderChooser();
    void refreshInstrumentComboBoxes();

    struct LabeledKnob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void initRotaryKnob(LabeledKnob& knob,
                        const juce::String& paramID,
                        const juce::String& titleText,
                        bool amberArc = false);

    static void layoutKnobGrid(const std::vector<LabeledKnob*>& knobs,
                               juce::Rectangle<int> area,
                               int columns);

    static void drawHardwareSectionFrame(juce::Graphics& g,
                                         juce::Rectangle<int> bounds,
                                         const juce::String& titleText);

    static void drawSevenSegmentDisplayBezel(juce::Graphics& g,
                                             juce::Rectangle<int> bounds,
                                             const juce::String& topCaption,
                                             const juce::String& mainReadout,
                                             const juce::String& subReadout);

    CasioCTK1000AudioProcessor& processorRef;
    CasioIXA::CTK1000LookAndFeel ctkLookAndFeel;

    //==============================================================================
    // 1. Top Center Console Bezel Controls
    juce::ComboBox crossModeCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> crossModeAttach;

    juce::ComboBox routingModeCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> routingModeAttach;

    juce::TextButton dac1993Button { "1993 22.05kHz DAC" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> dac1993Attach;

    juce::TextButton adaaButton { "ADAA FILTER" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> adaaAttach;

    LabeledKnob synthPanKnob;
    LabeledKnob masterGainKnob;

    juce::TextButton loadSa51FolderButton { "SELECT SA-51 WAV FOLDER..." };
    std::unique_ptr<juce::FileChooser> activeFileChooser;
    juce::String sampleBankStatusText;

    CasioIXA::IXAWavefolderScopeComponent wavefolderScope;

    //==============================================================================
    // 2. Left Panel — Dual PCM Sample Engine (Element A & Element B)
    struct PCMSlotControls
    {
        juce::TextButton enableBtn { "ON" };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableAttach;

        juce::ComboBox instrumentCombo;
        juce::ComboBox loopModeCombo;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> loopModeAttach;

        juce::TextButton loopOverrideBtn { "LOOP OVR" };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> loopOverrideAttach;

        LabeledKnob level, pan, coarse, fine, velSens, loopStart, loopEnd;
        LabeledKnob attack, decay, sustain, release;
    };

    PCMSlotControls pcmSlotA;
    PCMSlotControls pcmSlotB;

    //==============================================================================
    // 3. Center Panel — Dual-Operator IXA Synthesis Engine (Operators 1 & 2)
    struct IXAOperatorControls
    {
        LabeledKnob coarseRatio, fineDetune, baseIndex, envDepth, velDepth, carrierWarp, feedback, level;
        LabeledKnob modA, modD, modS, modR;
        LabeledKnob ampA, ampD, ampS, ampR;
    };

    IXAOperatorControls op1Controls;
    IXAOperatorControls op2Controls;

    // Phase 4: Per-Voice 12 dB/oct Non-Resonant IXA Lowpass Filter Controls
    LabeledKnob ixaLpfCutoffKnob;
    LabeledKnob ixaLpfEnvKnob;
    LabeledKnob ixaLpfKeyTrackKnob;

    //==============================================================================
    // 4. Bottom-Left Panel — Legacy 1993 CTK-1000 Tone Editor Override
    juce::TextButton legacyMacroEnableBtn { "TONE EDITOR 0-9 OVERRIDE" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> legacyMacroEnableAttach;

    LabeledKnob macroWaveKnob;
    LabeledKnob macroAttackKnob;
    LabeledKnob macroReleaseKnob;

    //==============================================================================
    // 5. Bottom-Right Panel — 8-Mode Semi-OBS DSP Effect Unit (Sanyo LC33832PL-70)
    juce::ComboBox dspModeCombo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> dspModeAttach;

    juce::TextButton dspLofiPsramBtn { "32KB PSRAM LO-FI" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> dspLofiPsramAttach;

    juce::TextButton dspBypassBtn { "DSP BYPASS" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> dspBypassAttach;

    LabeledKnob dspParam1Knob;
    LabeledKnob dspParam2Knob;
    LabeledKnob dspToneKnob;
    LabeledKnob dspMixKnob;

    // On-screen interactive MIDI keyboard strip at the bottom edge
    juce::MidiKeyboardComponent midiKeyboard;

    // Cached layout rectangles for custom hardware silkscreen & LED rendering
    juce::Rectangle<int> topBezelBounds;
    juce::Rectangle<int> centerLedWindowBounds;
    juce::Rectangle<int> sa51PathLedBounds;
    juce::Rectangle<int> pcmABounds;
    juce::Rectangle<int> pcmBBounds;
    juce::Rectangle<int> op1Bounds;
    juce::Rectangle<int> op2Bounds;
    juce::Rectangle<int> ixaLpfBounds;
    juce::Rectangle<int> legacyMacroBounds;
    juce::Rectangle<int> legacyLedReadoutBounds;
    juce::Rectangle<int> dspBounds;
    juce::Rectangle<int> dspLedMatrixBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CasioCTK1000AudioProcessorEditor)
};
