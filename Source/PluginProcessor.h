#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "IXAOscillator.h"
#include "CasioSampleBankManager.h"
#include "IXAHybridVoice.h"
#include "CTK1000Effects.h"
#include <atomic>

class CasioCTK1000AudioProcessor : public juce::AudioProcessor
{
public:
    enum class CrossSoundMode : int
    {
        PureIXA      = 0,
        HybridPCMIXA = 1,
        DualPCM      = 2
    };

    static constexpr int kNumVoices = 16;

    CasioCTK1000AudioProcessor();
    ~CasioCTK1000AudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    //==============================================================================
    // Public API for PluginEditor (Sample Bank & Instrument Selection)
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }
    const juce::AudioProcessorValueTreeState& getAPVTS() const noexcept { return apvts; }

    juce::MidiKeyboardState& getKeyboardState() noexcept { return keyboardState; }

    CasioIXA::CasioSampleBankManager& getSampleBankManager() noexcept { return sampleBankManager; }
    const CasioIXA::CasioSampleBankManager& getSampleBankManager() const noexcept { return sampleBankManager; }

    bool loadSampleFolder(const juce::File& folder);
    juce::StringArray getAvailableInstrumentNames() const;

    void setSlotAInstrumentByIndex(int index);
    void setSlotBInstrumentByIndex(int index);
    void setSlotAInstrumentByName(const juce::String& name);
    void setSlotBInstrumentByName(const juce::String& name);

    int getSlotAInstrumentIndex() const noexcept { return selectedSlotAIndex.load(); }
    int getSlotBInstrumentIndex() const noexcept { return selectedSlotBIndex.load(); }
    juce::String getSlotAInstrumentName() const;
    juce::String getSlotBInstrumentName() const;

    // Phase 8: Custom External .WAV Loader API for Slot A (0) and Slot B (1)
    bool loadCustomWaveFileForSlot(int slotIndex, const juce::File& wavFile);
    bool isSlotUsingCustomWav(int slotIndex) const noexcept;
    bool isSlotCustomWavMissing(int slotIndex) const noexcept;
    juce::String getSlotCustomWavPath(int slotIndex) const;
    juce::String getSlotDisplayLabel(int slotIndex) const;

    /** Copy of recent output waveform for real-time GUI oscilloscope inspection */
    void copyLatestScopeSamples(std::vector<float>& dest, int numRequested) const noexcept;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    void updateVoiceAndEffectParameters() noexcept;

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;

    CasioIXA::CasioSampleBankManager sampleBankManager;
    CasioIXA::IXAVoiceParameters currentVoiceParams;
    CasioIXA::CTK1000Effects::Parameters currentEffectParams;

    juce::Synthesiser synth;
    CasioIXA::CTK1000Effects effectsProcessor;

    // 2x Polyphase IIR Oversampler for Modern Anti-Aliased IXA synthesis
    juce::dsp::Oversampling<float> oversampler { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true };
    juce::AudioBuffer<float> oversampledSynthBuffer;

    double currentHostSampleRate = 44100.0;
    int currentMaxBlockSize = 512;
    bool wasOversampledLastBlock = true;

    std::atomic<int> selectedSlotAIndex { 0 };
    std::atomic<int> selectedSlotBIndex { 0 };

    // Phase 8: Per-Slot Custom External .WAV Tracking
    std::atomic<bool> isUsingCustomWavA { false };
    std::atomic<bool> isUsingCustomWavB { false };
    std::atomic<bool> missingCustomWavA { false };
    std::atomic<bool> missingCustomWavB { false };
    juce::String customWavPathA;
    juce::String customWavPathB;
    mutable juce::CriticalSection customWavPathLock;

    // Lock-free ring buffer for GUI oscilloscope
    static constexpr int kScopeBufferSize = 2048;
    std::array<float, kScopeBufferSize> scopeRingBuffer {};
    std::atomic<int> scopeWritePos { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CasioCTK1000AudioProcessor)
};
