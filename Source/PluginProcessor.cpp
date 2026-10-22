#include "PluginProcessor.h"
#include "PluginEditor.h"

CasioCTK1000AudioProcessor::CasioCTK1000AudioProcessor()
    : AudioProcessor(BusesProperties()
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "CasioCTK1000_Parameters", createParameterLayout())
{
    synth.setNoteStealingEnabled(true);
    synth.addSound(new CasioIXA::IXAHybridSound());

    for (int i = 0; i < kNumVoices; ++i)
    {
        synth.addVoice(new CasioIXA::IXAHybridVoice(sampleBankManager, currentVoiceParams));
    }

    // Automatically select initial instruments if available from saved SA-51 bank or procedural fallback
    const auto names = sampleBankManager.getInstrumentNames();
    if (!names.isEmpty())
    {
        int defaultA = sampleBankManager.findInstrumentIndex("epianoone");
        if (defaultA < 0) defaultA = 0;

        int defaultB = sampleBankManager.findInstrumentIndex("strings");
        if (defaultB < 0) defaultB = std::min(1, names.size() - 1);

        sampleBankManager.ensureInstrumentGroupLoaded(defaultA);
        sampleBankManager.ensureInstrumentGroupLoaded(defaultB);
        selectedSlotAIndex.store(defaultA);
        selectedSlotBIndex.store(defaultB);
    }
}

CasioCTK1000AudioProcessor::~CasioCTK1000AudioProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout CasioCTK1000AudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    //==============================================================================
    // 1. Global / Architecture Controls
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "CROSS_MODE", 1 },
        "Cross-Sound Mode",
        juce::StringArray { "Pure IXA", "Hybrid PCM + IXA", "Dual PCM" },
        1));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "ROUTING_MODE", 1 },
        "IXA Operator Routing",
        juce::StringArray { "Parallel Detuned", "2-Op Series Cascade (Op 2 -> Op 1)", "Single Op 1" },
        0));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "HARDWARE_DAC_MODE", 1 },
        "1993 22.05 kHz DAC Mode",
        false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "ADAA_ENABLE", 1 },
        "First-Order ADAA Anti-Aliasing",
        true));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "LEGACY_MACRO_ENABLE", 1 },
        "Legacy CTK-1000 0-9 Macro Override",
        false));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { "MACRO_WAVE", 1 },
        "CTK-1000 Macro WAVE (0-9)",
        0, 9, 4));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { "MACRO_ATTACK", 1 },
        "CTK-1000 Macro ATTACK (0-9)",
        0, 9, 2));

    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID { "MACRO_RELEASE", 1 },
        "CTK-1000 Macro RELEASE (0-9)",
        0, 9, 4));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "SYNTH_PAN", 1 },
        "IXA Synth Pan",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f),
        0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "IXA_LPF_CUTOFF", 1 },
        "IXA 12dB LPF Cutoff (Hz)",
        juce::NormalisableRange<float>(80.0f, 20000.0f, 1.0f, 0.25f),
        18000.0f,
        juce::AudioParameterFloatAttributes().withLabel("Hz")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "IXA_LPF_ENV", 1 },
        "IXA 12dB LPF Env Depth (Oct)",
        juce::NormalisableRange<float>(-4.0f, 4.0f, 0.01f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("Oct")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "IXA_LPF_KEYTRACK", 1 },
        "IXA 12dB LPF Key Tracking",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.50f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "MASTER_GAIN", 1 },
        "Master Output Gain",
        juce::NormalisableRange<float>(0.0f, 2.0f, 0.001f, 0.5f),
        0.85f));

    //==============================================================================
    // 2. IXA Operator 1 & Operator 2 Continuous Engineer Controls
    auto addIXAOperatorParams = [&params](int opNum,
                                          float defRatio,
                                          float defDetune,
                                          float defBaseIdx,
                                          float defEnvDepth,
                                          float defLevel)
    {
        const juce::String prefix = "OP" + juce::String(opNum) + "_";
        const juce::String label  = "Op " + juce::String(opNum) + " ";

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { prefix + "COARSE_RATIO", 1 },
            label + "Coarse Ratio",
            juce::NormalisableRange<float>(0.5f, 16.0f, 0.01f, 0.5f),
            defRatio));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { prefix + "FINE_DETUNE_HZ", 1 },
            label + "Fine Detune (Hz)",
            juce::NormalisableRange<float>(-50.0f, 50.0f, 0.01f),
            defDetune));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { prefix + "BASE_INDEX", 1 },
            label + "Base Mod Index (n0)",
            juce::NormalisableRange<float>(0.0f, 6.0f, 0.01f),
            defBaseIdx));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { prefix + "ENV_DEPTH", 1 },
            label + "Mod Env Depth (n_env)",
            juce::NormalisableRange<float>(-5.0f, 5.0f, 0.01f),
            defEnvDepth));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { prefix + "VEL_DEPTH", 1 },
            label + "Vel-to-Index (n_vel)",
            juce::NormalisableRange<float>(0.0f, 5.0f, 0.01f),
            1.2f));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { prefix + "CARRIER_WARP", 1 },
            label + "Carrier Warp (Alpha)",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
            1.0f));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { prefix + "FEEDBACK", 1 },
            label + "Self-Feedback (Beta)",
            juce::NormalisableRange<float>(0.0f, 0.5f, 0.001f),
            0.05f));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { prefix + "LEVEL", 1 },
            label + "Output Level",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
            defLevel));

        // Modulator ADSR
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { prefix + "MOD_ATTACK", 1 },
            label + "Mod Attack (s)",
            juce::NormalisableRange<float>(0.001f, 5.0f, 0.001f, 0.35f),
            0.008f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { prefix + "MOD_DECAY", 1 },
            label + "Mod Decay (s)",
            juce::NormalisableRange<float>(0.005f, 8.0f, 0.001f, 0.35f),
            0.35f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { prefix + "MOD_SUSTAIN", 1 },
            label + "Mod Sustain",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
            0.25f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { prefix + "MOD_RELEASE", 1 },
            label + "Mod Release (s)",
            juce::NormalisableRange<float>(0.005f, 8.0f, 0.001f, 0.35f),
            0.25f));

        // Amplitude ADSR
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { prefix + "AMP_ATTACK", 1 },
            label + "Amp Attack (s)",
            juce::NormalisableRange<float>(0.001f, 5.0f, 0.001f, 0.35f),
            0.005f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { prefix + "AMP_DECAY", 1 },
            label + "Amp Decay (s)",
            juce::NormalisableRange<float>(0.005f, 8.0f, 0.001f, 0.35f),
            0.45f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { prefix + "AMP_SUSTAIN", 1 },
            label + "Amp Sustain",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
            0.75f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { prefix + "AMP_RELEASE", 1 },
            label + "Amp Release (s)",
            juce::NormalisableRange<float>(0.005f, 8.0f, 0.001f, 0.35f),
            0.30f));
    };

    addIXAOperatorParams(1, 1.0f,  0.0f, 1.25f, 1.40f, 0.80f);
    addIXAOperatorParams(2, 2.0f,  1.8f, 0.85f, 0.90f, 0.55f);

    //==============================================================================
    // 3. Dual PCM Sample Slots (Slot A & Slot B)
    auto addPCMSlotParams = [&params](const juce::String& slotLetter,
                                      bool defEnabled,
                                      float defLevel,
                                      float defPan)
    {
        const juce::String prefix = "PCM_" + slotLetter + "_";
        const juce::String label  = "PCM Slot " + slotLetter + " ";

        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { prefix + "ENABLE", 1 },
            label + "Enable",
            defEnabled));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { prefix + "LEVEL", 1 },
            label + "Level",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
            defLevel));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { prefix + "PAN", 1 },
            label + "Pan",
            juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f),
            defPan));

        params.push_back(std::make_unique<juce::AudioParameterInt>(
            juce::ParameterID { prefix + "COARSE_TUNE", 1 },
            label + "Coarse Semitones",
            -24, 24, 0));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { prefix + "FINE_CENTS", 1 },
            label + "Fine Tune (Cents)",
            juce::NormalisableRange<float>(-100.0f, 100.0f, 0.1f),
            0.0f));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { prefix + "VEL_SENS", 1 },
            label + "Velocity to Level",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
            0.70f));

        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { prefix + "LOOP_MODE", 1 },
            label + "Loop Mode",
            juce::StringArray { "One-Shot", "Forward Loop", "Ping-Pong Loop (US Pat. 5,319,151)" },
            2));

        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { prefix + "LOOP_OVERRIDE", 1 },
            label + "Override Loop Points",
            false));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { prefix + "LOOP_START", 1 },
            label + "Loop Start Override",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
            0.40f));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { prefix + "LOOP_END", 1 },
            label + "Loop End Override",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
            0.95f));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { prefix + "AMP_ATTACK", 1 },
            label + "Amp Attack (s)",
            juce::NormalisableRange<float>(0.001f, 5.0f, 0.001f, 0.35f),
            0.005f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { prefix + "AMP_DECAY", 1 },
            label + "Amp Decay (s)",
            juce::NormalisableRange<float>(0.005f, 8.0f, 0.001f, 0.35f),
            0.35f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { prefix + "AMP_SUSTAIN", 1 },
            label + "Amp Sustain",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
            0.80f));
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { prefix + "AMP_RELEASE", 1 },
            label + "Amp Release (s)",
            juce::NormalisableRange<float>(0.005f, 8.0f, 0.001f, 0.35f),
            0.25f));
    };

    addPCMSlotParams("A", true,  0.75f, -0.15f);
    addPCMSlotParams("B", false, 0.65f,  0.15f);

    //==============================================================================
    // 4. 8-Mode Semi-OBS DSP Effect Processor (Sanyo LC33832PL-70)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "DSP_MODE", 1 },
        "DSP Effect Mode",
        juce::StringArray {
            "1: Hall Reverb",
            "2: Delay",
            "3: Chorus",
            "4: Distortion",
            "5: Room Reverb",
            "6: Stereo Delay",
            "7: Flanger",
            "8: Chorus Reverb"
        },
        0));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "DSP_PARAM1", 1 },
        "DSP Rate / Time / Size / Drive",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.55f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "DSP_PARAM2", 1 },
        "DSP Depth / Feedback / Damping",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.45f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "DSP_TONE", 1 },
        "DSP Tone Filter (PrFit)",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.78f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "DSP_MIX", 1 },
        "DSP Wet/Dry Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f),
        0.28f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "DSP_LOFI_PSRAM", 1 },
        "32 KB PSRAM Lo-Fi Mode (LC33832PL-70)",
        false));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID { "DSP_BYPASS", 1 },
        "DSP Bypass",
        false));

    //==============================================================================
    // 5. Phase 6: Dual Assignable Multi-Target LFO Engine (LFO 1 & LFO 2)
    const juce::StringArray lfoWaveChoices {
        "Triangle",
        "Sine",
        "Saw Up",
        "Square",
        "Sample & Hold"
    };

    const juce::StringArray lfoTargetChoices {
        "None / Off",
        "Master Pitch (Vibrato)",
        "Master Amplitude (Tremolo)",
        "IXA 12dB LPF Cutoff",
        "IXA Op 1 Mod Index (n)",
        "IXA Op 2 Mod Index (n)",
        "IXA Both Ops Index (n1+n2)",
        "IXA Op 1 Carrier Warp (a)",
        "IXA Op 1 Self-Feedback (b)",
        "IXA Op 2 Fine Freq / Ratio",
        "PCM A/B Crossfade Balance"
    };

    auto addLfoParams = [&](int lfoNum, int defTarget, float defRate, float defDelay)
    {
        const juce::String prefix = "LFO" + juce::String(lfoNum) + "_";
        const juce::String label  = "LFO " + juce::String(lfoNum) + " ";

        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { prefix + "WAVE", 1 },
            label + "Waveform",
            lfoWaveChoices,
            0));

        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID { prefix + "TARGET", 1 },
            label + "Target",
            lfoTargetChoices,
            defTarget));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { prefix + "RATE", 1 },
            label + "Rate (Hz)",
            juce::NormalisableRange<float>(0.05f, 30.0f, 0.01f, 0.35f),
            defRate,
            juce::AudioParameterFloatAttributes().withLabel("Hz")));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { prefix + "DELAY", 1 },
            label + "Fade-In Delay (s)",
            juce::NormalisableRange<float>(0.0f, 5.0f, 0.01f, 0.5f),
            defDelay,
            juce::AudioParameterFloatAttributes().withLabel("s")));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { prefix + "DEPTH", 1 },
            label + "Mod Depth",
            juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f),
            0.0f));

        params.push_back(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID { prefix + "SYNC", 1 },
            label + "Key Sync",
            true));
    };

    addLfoParams(1, 1, 5.2f, 0.35f); // LFO 1 default: Master Pitch (Vibrato), 5.2 Hz, 0.35s delay
    addLfoParams(2, 6, 1.2f, 0.00f); // LFO 2 default: IXA Both Ops Index, 1.2 Hz, 0.0s delay

    return { params.begin(), params.end() };
}

//==============================================================================
const juce::String CasioCTK1000AudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool CasioCTK1000AudioProcessor::acceptsMidi() const  { return true; }
bool CasioCTK1000AudioProcessor::producesMidi() const { return false; }
bool CasioCTK1000AudioProcessor::isMidiEffect() const { return false; }
double CasioCTK1000AudioProcessor::getTailLengthSeconds() const { return 2.5; }

int CasioCTK1000AudioProcessor::getNumPrograms()    { return 1; }
int CasioCTK1000AudioProcessor::getCurrentProgram() { return 0; }
void CasioCTK1000AudioProcessor::setCurrentProgram(int) {}
const juce::String CasioCTK1000AudioProcessor::getProgramName(int) { return "Default Workstation Patch"; }
void CasioCTK1000AudioProcessor::changeProgramName(int, const juce::String&) {}

//==============================================================================
void CasioCTK1000AudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentHostSampleRate = (sampleRate > 0.0) ? sampleRate : 44100.0;
    currentMaxBlockSize = std::max(64, samplesPerBlock);

    oversampler.reset();
    oversampler.initProcessing(static_cast<size_t>(currentMaxBlockSize));

    const double oversampledRate = currentHostSampleRate * 2.0;
    oversampledSynthBuffer.setSize(2, currentMaxBlockSize * 2);

    const bool hardwareLoFi = apvts.getRawParameterValue("HARDWARE_DAC_MODE")->load() > 0.5f;
    const double activeSynthRate = hardwareLoFi ? currentHostSampleRate : oversampledRate;
    wasOversampledLastBlock = !hardwareLoFi;

    synth.setCurrentPlaybackSampleRate(activeSynthRate);
    for (int i = 0; i < synth.getNumVoices(); ++i)
    {
        if (auto* v = dynamic_cast<CasioIXA::IXAHybridVoice*>(synth.getVoice(i)))
            v->prepareToPlay(activeSynthRate, currentMaxBlockSize * (hardwareLoFi ? 1 : 2));
    }

    effectsProcessor.prepare(currentHostSampleRate, currentMaxBlockSize);
    keyboardState.reset();
}

void CasioCTK1000AudioProcessor::releaseResources()
{
    keyboardState.reset();
    effectsProcessor.reset();
}

bool CasioCTK1000AudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void CasioCTK1000AudioProcessor::updateVoiceAndEffectParameters() noexcept
{
    const int crossMode = static_cast<int>(apvts.getRawParameterValue("CROSS_MODE")->load());
    const int routingMode = static_cast<int>(apvts.getRawParameterValue("ROUTING_MODE")->load());
    const bool dac1993 = apvts.getRawParameterValue("HARDWARE_DAC_MODE")->load() > 0.5f;
    const bool adaaEnable = apvts.getRawParameterValue("ADAA_ENABLE")->load() > 0.5f;
    const bool legacyMacro = apvts.getRawParameterValue("LEGACY_MACRO_ENABLE")->load() > 0.5f;
    const int macroWave = static_cast<int>(apvts.getRawParameterValue("MACRO_WAVE")->load());
    const int macroAttack = static_cast<int>(apvts.getRawParameterValue("MACRO_ATTACK")->load());
    const int macroRelease = static_cast<int>(apvts.getRawParameterValue("MACRO_RELEASE")->load());

    // Classic 0-9 CTK-1000 Tone Editor Attack & Release Lookup Tables (in seconds)
    static constexpr std::array<float, 10> kMacroAttackSec  = { 0.002f, 0.008f, 0.020f, 0.055f, 0.120f, 0.250f, 0.450f, 0.800f, 1.400f, 2.500f };
    static constexpr std::array<float, 10> kMacroReleaseSec = { 0.015f, 0.050f, 0.120f, 0.250f, 0.450f, 0.800f, 1.350f, 2.100f, 3.200f, 5.000f };

    const float macroAtk = kMacroAttackSec[static_cast<size_t>(std::clamp(macroAttack, 0, 9))];
    const float macroRel = kMacroReleaseSec[static_cast<size_t>(std::clamp(macroRelease, 0, 9))];

    // Cross-Sound Architecture layer enables
    const bool allowSynth = (crossMode == static_cast<int>(CrossSoundMode::PureIXA))
                         || (crossMode == static_cast<int>(CrossSoundMode::HybridPCMIXA));
    const bool allowPCM   = (crossMode == static_cast<int>(CrossSoundMode::HybridPCMIXA))
                         || (crossMode == static_cast<int>(CrossSoundMode::DualPCM));

    currentVoiceParams.synthEnabled   = allowSynth;
    currentVoiceParams.routingMode    = static_cast<CasioIXA::IXARoutingMode>(std::clamp(routingMode, 0, 2));
    currentVoiceParams.synthPan       = apvts.getRawParameterValue("SYNTH_PAN")->load();
    currentVoiceParams.ixaLpfCutoffHz = apvts.getRawParameterValue("IXA_LPF_CUTOFF")->load();
    currentVoiceParams.ixaLpfEnvOct   = apvts.getRawParameterValue("IXA_LPF_ENV")->load();
    currentVoiceParams.ixaLpfKeyTrack = apvts.getRawParameterValue("IXA_LPF_KEYTRACK")->load();
    currentVoiceParams.op1Level       = apvts.getRawParameterValue("OP1_LEVEL")->load();
    currentVoiceParams.op2Level       = apvts.getRawParameterValue("OP2_LEVEL")->load();

    auto fillOpParams = [&](const juce::String& prefix,
                            CasioIXA::IXAOscillator::Parameters& oscP,
                            juce::ADSR::Parameters& modEnv,
                            juce::ADSR::Parameters& ampEnv)
    {
        oscP.coarseRatio         = static_cast<double>(apvts.getRawParameterValue(prefix + "COARSE_RATIO")->load());
        oscP.fineDetuneHz        = static_cast<double>(apvts.getRawParameterValue(prefix + "FINE_DETUNE_HZ")->load());
        oscP.baseModIndex        = static_cast<double>(apvts.getRawParameterValue(prefix + "BASE_INDEX")->load());
        oscP.envModDepth         = static_cast<double>(apvts.getRawParameterValue(prefix + "ENV_DEPTH")->load());
        oscP.velModDepth         = static_cast<double>(apvts.getRawParameterValue(prefix + "VEL_DEPTH")->load());
        oscP.carrierWarp         = static_cast<double>(apvts.getRawParameterValue(prefix + "CARRIER_WARP")->load());
        oscP.feedback            = static_cast<double>(apvts.getRawParameterValue(prefix + "FEEDBACK")->load());
        oscP.adaaEnabled         = adaaEnable;
        oscP.dac1993Mode         = dac1993;
        oscP.legacyMacroOverride = legacyMacro;
        oscP.legacyMacroStep     = macroWave;

        modEnv.attack  = apvts.getRawParameterValue(prefix + "MOD_ATTACK")->load();
        modEnv.decay   = apvts.getRawParameterValue(prefix + "MOD_DECAY")->load();
        modEnv.sustain = apvts.getRawParameterValue(prefix + "MOD_SUSTAIN")->load();
        modEnv.release = apvts.getRawParameterValue(prefix + "MOD_RELEASE")->load();

        ampEnv.attack  = legacyMacro ? macroAtk : apvts.getRawParameterValue(prefix + "AMP_ATTACK")->load();
        ampEnv.decay   = apvts.getRawParameterValue(prefix + "AMP_DECAY")->load();
        ampEnv.sustain = apvts.getRawParameterValue(prefix + "AMP_SUSTAIN")->load();
        ampEnv.release = legacyMacro ? macroRel : apvts.getRawParameterValue(prefix + "AMP_RELEASE")->load();
    };

    fillOpParams("OP1_", currentVoiceParams.op1Params, currentVoiceParams.op1ModEnv, currentVoiceParams.op1AmpEnv);
    fillOpParams("OP2_", currentVoiceParams.op2Params, currentVoiceParams.op2ModEnv, currentVoiceParams.op2AmpEnv);

    auto fillSlotParams = [&](const juce::String& prefix,
                              CasioIXA::PCMSlotParameters& slotP,
                              int selectedInstIdx,
                              bool forceEnableInDualMode)
    {
        const bool userEnable = apvts.getRawParameterValue(prefix + "ENABLE")->load() > 0.5f;
        slotP.enabled               = allowPCM && (userEnable || forceEnableInDualMode);
        slotP.instrumentIndex       = selectedInstIdx;
        slotP.level                 = apvts.getRawParameterValue(prefix + "LEVEL")->load();
        slotP.pan                   = apvts.getRawParameterValue(prefix + "PAN")->load();
        slotP.coarseTuneSemitones   = static_cast<int>(apvts.getRawParameterValue(prefix + "COARSE_TUNE")->load());
        slotP.fineTuneCents         = apvts.getRawParameterValue(prefix + "FINE_CENTS")->load();
        slotP.velocitySensitivity   = apvts.getRawParameterValue(prefix + "VEL_SENS")->load();
        slotP.loopMode              = static_cast<CasioIXA::SampleLoopMode>(
                                        std::clamp(static_cast<int>(apvts.getRawParameterValue(prefix + "LOOP_MODE")->load()), 0, 2));
        slotP.overrideLoopPoints    = apvts.getRawParameterValue(prefix + "LOOP_OVERRIDE")->load() > 0.5f;
        slotP.loopStartPercent      = apvts.getRawParameterValue(prefix + "LOOP_START")->load() * 100.0f;
        slotP.loopEndPercent        = apvts.getRawParameterValue(prefix + "LOOP_END")->load() * 100.0f;

        slotP.ampEnvParams.attack   = legacyMacro ? macroAtk : apvts.getRawParameterValue(prefix + "AMP_ATTACK")->load();
        slotP.ampEnvParams.decay    = apvts.getRawParameterValue(prefix + "AMP_DECAY")->load();
        slotP.ampEnvParams.sustain  = apvts.getRawParameterValue(prefix + "AMP_SUSTAIN")->load();
        slotP.ampEnvParams.release  = legacyMacro ? macroRel : apvts.getRawParameterValue(prefix + "AMP_RELEASE")->load();
    };

    const bool isDualPcmMode = (crossMode == static_cast<int>(CrossSoundMode::DualPCM));
    fillSlotParams("PCM_A_", currentVoiceParams.slotA, selectedSlotAIndex.load(), false);
    fillSlotParams("PCM_B_", currentVoiceParams.slotB, selectedSlotBIndex.load(), isDualPcmMode);

    // Update 8-Mode Semi-OBS DSP Effect parameters
    currentEffectParams.dspMode       = static_cast<CasioIXA::CTK1000DSPMode>(
                                          std::clamp(static_cast<int>(apvts.getRawParameterValue("DSP_MODE")->load()), 0, 7));
    currentEffectParams.param1        = apvts.getRawParameterValue("DSP_PARAM1")->load();
    currentEffectParams.param2        = apvts.getRawParameterValue("DSP_PARAM2")->load();
    currentEffectParams.toneFilter    = apvts.getRawParameterValue("DSP_TONE")->load();
    currentEffectParams.wetDryMix     = apvts.getRawParameterValue("DSP_MIX")->load();
    currentEffectParams.lofiPsramMode = apvts.getRawParameterValue("DSP_LOFI_PSRAM")->load() > 0.5f;
    currentEffectParams.bypass        = apvts.getRawParameterValue("DSP_BYPASS")->load() > 0.5f;

    // Update Phase 6 Dual LFO parameters
    auto fillLfoParams = [&](const juce::String& prefix, CasioIXA::LfoParameters& lfoP)
    {
        lfoP.waveform = static_cast<CasioIXA::LfoWaveform>(
            std::clamp(static_cast<int>(apvts.getRawParameterValue(prefix + "WAVE")->load()), 0, 4));
        lfoP.target = static_cast<CasioIXA::LfoTarget>(
            std::clamp(static_cast<int>(apvts.getRawParameterValue(prefix + "TARGET")->load()), 0, 10));
        lfoP.rateHz   = apvts.getRawParameterValue(prefix + "RATE")->load();
        lfoP.delaySec = apvts.getRawParameterValue(prefix + "DELAY")->load();
        lfoP.depth    = apvts.getRawParameterValue(prefix + "DEPTH")->load();
        lfoP.keySync  = apvts.getRawParameterValue(prefix + "SYNC")->load() > 0.5f;
    };

    fillLfoParams("LFO1_", currentVoiceParams.lfo1);
    fillLfoParams("LFO2_", currentVoiceParams.lfo2);
}

void CasioCTK1000AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int totalNumOutputChannels = getTotalNumOutputChannels();

    for (int ch = 0; ch < totalNumOutputChannels; ++ch)
        buffer.clear(ch, 0, numSamples);

    keyboardState.processNextMidiBuffer(midiMessages, 0, numSamples, true);
    updateVoiceAndEffectParameters();

    const bool useOversampling = !currentVoiceParams.op1Params.dac1993Mode;

    // Smoothly reconfigure voice sample rates if the user toggles between 1993 DAC Mode and Modern 2x Oversampling
    if (useOversampling != wasOversampledLastBlock)
    {
        const double targetSr = useOversampling ? (currentHostSampleRate * 2.0) : currentHostSampleRate;
        synth.setCurrentPlaybackSampleRate(targetSr);
        for (int i = 0; i < synth.getNumVoices(); ++i)
        {
            if (auto* v = dynamic_cast<CasioIXA::IXAHybridVoice*>(synth.getVoice(i)))
                v->prepareToPlay(targetSr, currentMaxBlockSize * (useOversampling ? 2 : 1));
        }
        oversampler.reset();
        wasOversampledLastBlock = useOversampling;
    }

    if (useOversampling && numSamples > 0)
    {
        const int osSamples = numSamples * 2;
        if (oversampledSynthBuffer.getNumSamples() < osSamples)
            oversampledSynthBuffer.setSize(2, osSamples, false, false, true);

        juce::dsp::AudioBlock<float> baseBlock(buffer);
        auto upBlock = oversampler.processSamplesUp(baseBlock);

        oversampledSynthBuffer.clear(0, 0, osSamples);
        oversampledSynthBuffer.clear(1, 0, osSamples);

        // Scale MIDI timestamps by 2x for the oversampled synthesis block
        juce::MidiBuffer scaledMidi;
        for (const auto metadata : midiMessages)
        {
            scaledMidi.addEvent(metadata.getMessage(),
                                std::clamp(metadata.samplePosition * 2, 0, osSamples - 1));
        }

        synth.renderNextBlock(oversampledSynthBuffer, scaledMidi, 0, osSamples);

        for (size_t ch = 0; ch < std::min<size_t>(2, upBlock.getNumChannels()); ++ch)
        {
            float* dst = upBlock.getChannelPointer(ch);
            const float* src = oversampledSynthBuffer.getReadPointer(static_cast<int>(ch));
            juce::FloatVectorOperations::copy(dst, src, osSamples);
        }

        oversampler.processSamplesDown(baseBlock);
    }
    else
    {
        synth.renderNextBlock(buffer, midiMessages, 0, numSamples);
    }

    // Process through 8-Mode Semi-OBS DSP Effect Unit (Sanyo LC33832PL-70)
    effectsProcessor.processBlock(buffer, currentEffectParams);

    // Apply Master Gain
    const float masterGain = apvts.getRawParameterValue("MASTER_GAIN")->load();
    buffer.applyGain(masterGain);

    // Push mono sum into lock-free oscilloscope ring buffer for the GUI
    if (numSamples > 0)
    {
        const float* lPtr = buffer.getReadPointer(0);
        const float* rPtr = (buffer.getNumChannels() > 1) ? buffer.getReadPointer(1) : nullptr;
        int wPos = scopeWritePos.load(std::memory_order_relaxed);
        for (int i = 0; i < numSamples; ++i)
        {
            const float s = (rPtr != nullptr) ? 0.5f * (lPtr[i] + rPtr[i]) : lPtr[i];
            scopeRingBuffer[static_cast<size_t>(wPos)] = s;
            wPos = (wPos + 1) % kScopeBufferSize;
        }
        scopeWritePos.store(wPos, std::memory_order_release);
    }
}

//==============================================================================
bool CasioCTK1000AudioProcessor::loadSampleFolder(const juce::File& folder)
{
    const juce::String prevAName = getSlotAInstrumentName();
    const juce::String prevBName = getSlotBInstrumentName();

    if (!sampleBankManager.loadSampleDirectory(folder, true))
        return false;

    const auto names = sampleBankManager.getInstrumentNames();
    if (!names.isEmpty())
    {
        int idxA = sampleBankManager.findInstrumentIndex(prevAName);
        if (idxA < 0) idxA = sampleBankManager.findInstrumentIndex("epianoone");
        if (idxA < 0) idxA = 0;

        int idxB = sampleBankManager.findInstrumentIndex(prevBName);
        if (idxB < 0) idxB = sampleBankManager.findInstrumentIndex("strings");
        if (idxB < 0) idxB = std::min(1, names.size() - 1);

        sampleBankManager.ensureInstrumentGroupLoaded(idxA);
        sampleBankManager.ensureInstrumentGroupLoaded(idxB);
        selectedSlotAIndex.store(idxA);
        selectedSlotBIndex.store(idxB);
    }

    return true;
}

juce::StringArray CasioCTK1000AudioProcessor::getAvailableInstrumentNames() const
{
    return sampleBankManager.getInstrumentNames();
}

void CasioCTK1000AudioProcessor::setSlotAInstrumentByIndex(int index)
{
    const int numInst = sampleBankManager.getNumInstruments();
    if (numInst > 0)
    {
        const int clamped = std::clamp(index, 0, numInst - 1);
        sampleBankManager.ensureInstrumentGroupLoaded(clamped);
        selectedSlotAIndex.store(clamped);
    }
}

void CasioCTK1000AudioProcessor::setSlotBInstrumentByIndex(int index)
{
    const int numInst = sampleBankManager.getNumInstruments();
    if (numInst > 0)
    {
        const int clamped = std::clamp(index, 0, numInst - 1);
        sampleBankManager.ensureInstrumentGroupLoaded(clamped);
        selectedSlotBIndex.store(clamped);
    }
}

void CasioCTK1000AudioProcessor::setSlotAInstrumentByName(const juce::String& name)
{
    const int idx = sampleBankManager.findInstrumentIndex(name);
    if (idx >= 0)
    {
        sampleBankManager.ensureInstrumentGroupLoaded(idx);
        selectedSlotAIndex.store(idx);
    }
}

void CasioCTK1000AudioProcessor::setSlotBInstrumentByName(const juce::String& name)
{
    const int idx = sampleBankManager.findInstrumentIndex(name);
    if (idx >= 0)
    {
        sampleBankManager.ensureInstrumentGroupLoaded(idx);
        selectedSlotBIndex.store(idx);
    }
}

juce::String CasioCTK1000AudioProcessor::getSlotAInstrumentName() const
{
    if (const auto* g = sampleBankManager.getInstrumentGroupByIndex(selectedSlotAIndex.load()))
        return g->name;
    return {};
}

juce::String CasioCTK1000AudioProcessor::getSlotBInstrumentName() const
{
    if (const auto* g = sampleBankManager.getInstrumentGroupByIndex(selectedSlotBIndex.load()))
        return g->name;
    return {};
}

void CasioCTK1000AudioProcessor::copyLatestScopeSamples(std::vector<float>& dest, int numRequested) const noexcept
{
    const int count = std::clamp(numRequested, 1, kScopeBufferSize);
    dest.resize(static_cast<size_t>(count));

    const int wPos = scopeWritePos.load(std::memory_order_acquire);
    int rPos = (wPos - count + kScopeBufferSize) % kScopeBufferSize;
    for (int i = 0; i < count; ++i)
    {
        dest[static_cast<size_t>(i)] = scopeRingBuffer[static_cast<size_t>(rPos)];
        rPos = (rPos + 1) % kScopeBufferSize;
    }
}

//==============================================================================
bool CasioCTK1000AudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* CasioCTK1000AudioProcessor::createEditor()
{
    return new CasioCTK1000AudioProcessorEditor(*this);
}

//==============================================================================
void CasioCTK1000AudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    juce::ValueTree state = apvts.copyState();
    sampleBankManager.saveStateToValueTree(state, getSlotAInstrumentName(), getSlotBInstrumentName());

    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    if (xml != nullptr)
        copyXmlToBinary(*xml, destData);
}

void CasioCTK1000AudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
    {
        const juce::ValueTree restoredTree = juce::ValueTree::fromXml(*xmlState);
        apvts.replaceState(restoredTree);

        juce::String slotAName, slotBName;
        sampleBankManager.restoreStateFromValueTree(restoredTree, slotAName, slotBName);

        if (slotAName.isNotEmpty())
            setSlotAInstrumentByName(slotAName);
        if (slotBName.isNotEmpty())
            setSlotBInstrumentByName(slotBName);
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CasioCTK1000AudioProcessor();
}
