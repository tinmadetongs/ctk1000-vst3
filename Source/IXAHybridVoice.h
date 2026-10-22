#pragma once

#include "IXAOscillator.h"
#include "CasioSampleBankManager.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace CasioIXA
{

enum class IXARoutingMode : int
{
    ParallelDetuned = 0,
    SeriesCascadeOp2ToOp1 = 1,
    SingleOp1 = 2
};

enum class LfoWaveform : int
{
    Triangle      = 0,
    Sine          = 1,
    SawUp         = 2,
    Square        = 3,
    SampleAndHold = 4
};

enum class LfoTarget : int
{
    None                 = 0,
    MasterPitch          = 1, // +/- 2.0 semitones max at full depth (IXA Ops + PCM Slots A & B)
    MasterAmplitude      = 2, // Modulates voice output VCA gain (Tremolo)
    IxaLpfCutoff         = 3, // +/- 3.5 octaves added to Phase 4 filter cutoff exponent
    IxaOp1ModIndex       = 4, // +/- 3.0 added to Op 1 instantaneous index n[k]
    IxaOp2ModIndex       = 5, // +/- 3.0 added to Op 2 instantaneous index n[k]
    IxaBothOpsIndex      = 6, // Simultaneous wavefolder sweep across Op 1 and Op 2
    IxaOp1CarrierWarp    = 7, // +/- 0.8 added to Op 1 alpha in [0, 1]
    IxaOp1Feedback       = 8, // +/- 0.35 added to Op 1 beta in [0, 0.5]
    IxaOp2FineFreq       = 9, // Dynamic FM/IXA harmonic clang & metallic shifting (+/- 40 Hz + ratio offset)
    PcmABCrossfade       = 10 // Dynamically pans/morphs between PCM Element A and PCM Element B levels
};

struct LfoParameters
{
    LfoWaveform waveform = LfoWaveform::Triangle;
    LfoTarget target     = LfoTarget::MasterPitch;
    float rateHz         = 5.2f;  // 0.05 to 30.0 Hz
    float delaySec       = 0.35f; // 0.0 to 5.0 s fade-in ramp
    float depth          = 0.0f;  // -1.0 to +1.0
    bool keySync         = true;
};

/**
 * Per-Voice Multi-Waveform LFO Generator with Smooth Fade-In Delay Envelope
 * and deterministic xorshift32 Sample & Hold latch.
 */
class VoiceLFOGenerator
{
public:
    void prepare(double newSampleRate) noexcept
    {
        sampleRate = (newSampleRate > 0.0) ? newSampleRate : 44100.0;
        reset(0x13579BDFu);
    }

    void reset(uint32_t seed = 0x13579BDFu) noexcept
    {
        phase = 0.0;
        delayEnv = 0.0;
        rngState = (seed != 0) ? seed : 0x13579BDFu;
        heldRandom = 0.0;
    }

    void noteOn(bool keySync, float delaySeconds, uint32_t noteSeed) noexcept
    {
        if (keySync)
        {
            phase = 0.0;
            rngState ^= (noteSeed * 2654435761u + 0x9E3779B9u);
            if (rngState == 0)
                rngState = 0x13579BDFu;
            heldRandom = nextBipolarRandom();
        }

        delayEnv = (delaySeconds <= 0.001f) ? 1.0 : 0.0;
    }

    /**
     * Advances the LFO by `numSamples` steps and returns the delayed, depth-scaled
     * modulation signal in [-1.0, +1.0] (or slightly beyond if CC#1 boosts LFO 1).
     */
    double advance(const LfoParameters& params, int numSamples, double extraDepth = 0.0) noexcept
    {
        const double totalDepth = std::clamp(static_cast<double>(params.depth) + extraDepth, -1.0, 1.0);
        const double dt = static_cast<double>(numSamples) / sampleRate;

        // Advance fade-in delay ramp envelope E_delay[k] in [0, 1]
        if (params.delaySec <= 0.001f)
        {
            delayEnv = 1.0;
        }
        else if (delayEnv < 1.0)
        {
            delayEnv = std::min(1.0, delayEnv + dt / static_cast<double>(params.delaySec));
        }

        // Advance normalized phase in [0, 1)
        const double freq = std::clamp(static_cast<double>(params.rateHz), 0.05, 30.0);
        const double prevPhase = phase;
        phase += freq * dt;

        if (phase >= 1.0)
        {
            phase -= std::floor(phase);
            if (params.waveform == LfoWaveform::SampleAndHold)
                heldRandom = nextBipolarRandom();
        }
        else if (prevPhase == 0.0 && params.waveform == LfoWaveform::SampleAndHold && heldRandom == 0.0)
        {
            heldRandom = nextBipolarRandom();
        }

        double rawWave = 0.0;
        switch (params.waveform)
        {
            case LfoWaveform::Triangle:
            {
                // Authentic Casio 6-bit stepped digital triangle LFO
                const double quantizedPhase = std::floor(phase * 64.0) / 64.0;
                rawWave = 1.0 - 4.0 * std::abs(quantizedPhase - 0.5);
                break;
            }
            case LfoWaveform::Sine:
            {
                rawWave = std::sin(juce::MathConstants<double>::twoPi * phase);
                break;
            }
            case LfoWaveform::SawUp:
            {
                rawWave = 2.0 * phase - 1.0;
                break;
            }
            case LfoWaveform::Square:
            {
                rawWave = (phase < 0.5) ? 1.0 : -1.0;
                break;
            }
            case LfoWaveform::SampleAndHold:
            default:
            {
                rawWave = heldRandom;
                break;
            }
        }

        return rawWave * delayEnv * totalDepth;
    }

private:
    double nextBipolarRandom() noexcept
    {
        uint32_t x = rngState;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        rngState = x;
        return (static_cast<double>(x & 0x00FFFFFFu) / static_cast<double>(0x00800000u)) - 1.0;
    }

    double sampleRate = 44100.0;
    double phase = 0.0;
    double delayEnv = 1.0;
    uint32_t rngState = 0x13579BDFu;
    double heldRandom = 0.0;
};

struct PCMSlotParameters
{
    bool enabled = true;
    int instrumentIndex = 0;
    float level = 0.8f;              // 0.0 to 1.0
    float pan = 0.0f;                // -1.0 (L) to +1.0 (R)
    int coarseTuneSemitones = 0;     // -24 to +24
    float fineTuneCents = 0.0f;      // -100 to +100
    float velocitySensitivity = 0.7f;// 0.0 to 1.0
    SampleLoopMode loopMode = SampleLoopMode::PingPongLoop;
    bool overrideLoopPoints = false;
    float loopStartPercent = 40.0f;  // 0.0 to 100.0
    float loopEndPercent = 95.0f;    // 0.0 to 100.0
    juce::ADSR::Parameters ampEnvParams { 0.005f, 0.25f, 0.8f, 0.20f };
};

struct IXAVoiceParameters
{
    PCMSlotParameters slotA;
    PCMSlotParameters slotB;

    bool synthEnabled = true;
    IXARoutingMode routingMode = IXARoutingMode::ParallelDetuned;
    float op1Level = 0.8f;
    float op2Level = 0.6f;
    float synthPan = 0.0f;

    // Phase 4: Per-Voice 12 dB/oct Non-Resonant IXA Lowpass Filter Parameters
    float ixaLpfCutoffHz = 18000.0f; // [80.0 Hz, 20000.0 Hz]
    float ixaLpfEnvOct   = 0.0f;     // [-4.0, +4.0] octaves
    float ixaLpfKeyTrack = 0.50f;    // [0.0, 1.0] around MIDI note 60

    // Phase 6: Dual Assignable Multi-Target LFO Parameters
    LfoParameters lfo1;
    LfoParameters lfo2;

    IXAOscillator::Parameters op1Params;
    juce::ADSR::Parameters op1ModEnv { 0.005f, 0.35f, 0.2f, 0.25f };
    juce::ADSR::Parameters op1AmpEnv { 0.005f, 0.40f, 0.7f, 0.25f };

    IXAOscillator::Parameters op2Params;
    juce::ADSR::Parameters op2ModEnv { 0.005f, 0.25f, 0.1f, 0.20f };
    juce::ADSR::Parameters op2AmpEnv { 0.005f, 0.35f, 0.6f, 0.20f };
};

/**
 * 2-Pole (12 dB/octave) Zero-Delay-Feedback (ZDF) Trapezoidal State Variable Lowpass Filter
 * Locked to Butterworth non-resonant damping Q = 1/sqrt(2) (~0.70710678), R2 = 2*R = sqrt(2) (~1.41421356).
 * Guarantees zero zipper noise or instability under fast envelope and velocity modulation.
 */
class IXAZdfButterworthLPF
{
public:
    void prepare(double newSampleRate) noexcept
    {
        sampleRate = (newSampleRate > 0.0) ? newSampleRate : 44100.0;
        reset();
        updateCoefficients(18000.0);
    }

    void reset() noexcept
    {
        ic1eq = 0.0;
        ic2eq = 0.0;
    }

    void updateCoefficients(double cutoffHz) noexcept
    {
        const double clampedFc = std::clamp(cutoffHz, 40.0, 0.48 * sampleRate);
        const double g = std::tan(juce::MathConstants<double>::pi * clampedFc / sampleRate);
        static constexpr double k = 1.4142135623730951;
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    inline double processSample(double v0) noexcept
    {
        const double v3 = v0 - ic2eq;
        const double v1 = a1 * ic1eq + a2 * v3;
        const double v2 = ic2eq + a2 * ic1eq + a3 * v3;
        ic1eq = 2.0 * v1 - ic1eq;
        ic2eq = 2.0 * v2 - ic2eq;
        return v2; // 12 dB/octave lowpass output
    }

private:
    double sampleRate = 44100.0;
    double ic1eq = 0.0;
    double ic2eq = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
    double a3 = 0.0;
};

/**
 * Dual-Element Algorithmic PCM Playback Head (US Patent 5,319,151)
 * Supports One-Shot, Forward Loop, and Bidirectional Ping-Pong Loop with Linear interpolation.
 */
class PCMSampleElementPlayer
{
public:
    void prepare(double newSampleRate) noexcept
    {
        hostSampleRate = (newSampleRate > 0.0) ? newSampleRate : 44100.0;
        ampEnv.setSampleRate(hostSampleRate);
        reset();
    }

    void reset() noexcept
    {
        activeZone = nullptr;
        readPosition = 0.0;
        direction = 1;
        isPlaying = false;
        ampEnv.reset();
    }

    void startNote(const SampleZone* zone,
                   int midiNoteNumber,
                   float velocity,
                   const PCMSlotParameters& params) noexcept
    {
        activeZone = zone;
        playedMidiNote = midiNoteNumber;
        noteVelocity = std::clamp(velocity, 0.0f, 1.0f);
        ampEnv.setParameters(params.ampEnvParams);

        if (activeZone == nullptr || activeZone->audioData.getNumSamples() <= 1)
        {
            isPlaying = false;
            return;
        }

        readPosition = 0.0;
        direction = 1;
        isPlaying = true;
        ampEnv.noteOn();
    }

    void stopNote(bool allowTailOff) noexcept
    {
        if (allowTailOff)
        {
            ampEnv.noteOff();
        }
        else
        {
            ampEnv.reset();
            isPlaying = false;
        }
    }

    bool isActive() const noexcept
    {
        return isPlaying && ampEnv.isActive();
    }

    void renderNextSample(const PCMSlotParameters& params,
                          double pitchBendSemitones,
                          float levelScale,
                          float& outLeft,
                          float& outRight) noexcept
    {
        outLeft = 0.0f;
        outRight = 0.0f;

        if (!params.enabled || !isPlaying || activeZone == nullptr || !ampEnv.isActive())
        {
            if (!ampEnv.isActive())
                isPlaying = false;
            return;
        }

        const int totalSamples = activeZone->audioData.getNumSamples();
        if (totalSamples <= 1)
        {
            isPlaying = false;
            return;
        }

        const float envVal = ampEnv.getNextSample();
        if (!ampEnv.isActive())
        {
            isPlaying = false;
            return;
        }

        // Compute loop boundaries in samples
        int64_t loopStart = activeZone->loopStartSample;
        int64_t loopEnd = activeZone->loopEndSample;

        if (params.overrideLoopPoints)
        {
            const double startNorm = std::clamp(static_cast<double>(params.loopStartPercent) * 0.01, 0.0, 0.99);
            const double endNorm = std::clamp(static_cast<double>(params.loopEndPercent) * 0.01, startNorm + 0.01, 1.0);
            loopStart = static_cast<int64_t>(std::floor(startNorm * (totalSamples - 1)));
            loopEnd = std::max<int64_t>(loopStart + 1, static_cast<int64_t>(std::floor(endNorm * (totalSamples - 1))));
        }
        else
        {
            loopStart = std::clamp<int64_t>(loopStart, 0, totalSamples - 2);
            loopEnd = std::clamp<int64_t>(loopEnd, loopStart + 1, totalSamples - 1);
        }

        // Interpolated sample read
        const int idx0 = std::clamp(static_cast<int>(std::floor(readPosition)), 0, totalSamples - 1);
        const int idx1 = std::clamp(idx0 + 1, 0, totalSamples - 1);
        const float frac = static_cast<float>(readPosition - std::floor(readPosition));

        const int numCh = activeZone->audioData.getNumChannels();
        const float* ch0 = activeZone->audioData.getReadPointer(0);
        const float sampleL = ch0[idx0] + frac * (ch0[idx1] - ch0[idx0]);

        float sampleR = sampleL;
        if (numCh > 1)
        {
            const float* ch1 = activeZone->audioData.getReadPointer(1);
            sampleR = ch1[idx0] + frac * (ch1[idx1] - ch1[idx0]);
        }

        // Compute pitch ratio including root note transposition, coarse/fine tune, pitch wheel, and LFO vibrato
        const double totalSemitones = static_cast<double>(playedMidiNote - activeZone->rootMidiNote)
                                    + static_cast<double>(params.coarseTuneSemitones)
                                    + static_cast<double>(params.fineTuneCents) * 0.01
                                    + pitchBendSemitones;

        const double pitchRatio = std::pow(2.0, totalSemitones / 12.0)
                                * (activeZone->sourceSampleRate / hostSampleRate);

        // Advance playback position according to Loop Mode (US Patent 5,319,151)
        if (params.loopMode == SampleLoopMode::OneShot)
        {
            readPosition += pitchRatio;
            if (readPosition >= static_cast<double>(totalSamples - 1))
            {
                isPlaying = false;
                ampEnv.reset();
            }
        }
        else if (params.loopMode == SampleLoopMode::ForwardLoop)
        {
            readPosition += pitchRatio;
            if (readPosition >= static_cast<double>(loopEnd))
            {
                const double loopLen = static_cast<double>( std::max<int64_t>(1, loopEnd - loopStart) );
                readPosition = static_cast<double>(loopStart)
                             + IXAOscillator::positiveMod(readPosition - static_cast<double>(loopStart), loopLen);
            }
        }
        else // SampleLoopMode::PingPongLoop (US Patent 5,319,151 Bidirectional Ping-Pong)
        {
            readPosition += static_cast<double>(direction) * pitchRatio;
            const double dStart = static_cast<double>(loopStart);
            const double dEnd = static_cast<double>(loopEnd);

            if (direction > 0 && readPosition >= dEnd)
            {
                const double overshoot = readPosition - dEnd;
                readPosition = std::max(dStart, dEnd - overshoot);
                direction = -1;
            }
            else if (direction < 0 && readPosition <= dStart)
            {
                const double overshoot = dStart - readPosition;
                readPosition = std::min(dEnd, dStart + overshoot);
                direction = 1;
            }
        }

        // Velocity scaling & equal-power stereo pan
        const float velGain = (1.0f - params.velocitySensitivity) + params.velocitySensitivity * noteVelocity;
        const float amp = std::clamp(params.level * levelScale, 0.0f, 1.5f) * velGain * envVal;

        const float panAngle = ( std::clamp(params.pan, -1.0f, 1.0f) + 1.0f ) * 0.25f * juce::MathConstants<float>::pi;
        const float gainL = std::cos(panAngle) * amp;
        const float gainR = std::sin(panAngle) * amp;

        outLeft = sampleL * gainL;
        outRight = sampleR * gainR;
    }

private:
    double hostSampleRate = 44100.0;
    const SampleZone* activeZone = nullptr;
    int playedMidiNote = 60;
    float noteVelocity = 1.0f;
    double readPosition = 0.0;
    int direction = 1;
    bool isPlaying = false;
    juce::ADSR ampEnv;
};

/**
 * Dummy SynthesiserSound matching all MIDI notes/channels for the 16-voice IXA Hybrid Engine.
 */
class IXAHybridSound : public juce::SynthesiserSound
{
public:
    IXAHybridSound() = default;
    bool appliesToNote(int) override { return true; }
    bool appliesToChannel(int) override { return true; }
};

/**
 * IXAHybridVoice
 * Combines Dual PCM Sample Elements (Slot A & Slot B) with Dual-Operator IXA Triangular Wave Modulation,
 * Per-Voice 12 dB/oct ZDF Butterworth Lowpass Filter, and Dual Assignable Multi-Target LFOs (LFO 1 & LFO 2).
 */
class IXAHybridVoice : public juce::SynthesiserVoice
{
public:
    IXAHybridVoice(const CasioSampleBankManager& bankManagerRef,
                   const IXAVoiceParameters& sharedParamsRef)
        : sampleBank(bankManagerRef),
          voiceParams(sharedParamsRef)
    {
    }

    bool canPlaySound(juce::SynthesiserSound* sound) override
    {
        return dynamic_cast<IXAHybridSound*>(sound) != nullptr;
    }

    void prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
    {
        setCurrentPlaybackSampleRate(sampleRate);
        slotAPlayer.prepare(sampleRate);
        slotBPlayer.prepare(sampleRate);
        op1.prepare(sampleRate);
        op2.prepare(sampleRate);
        ixaLowpassFilter.prepare(sampleRate);
        lfo1Gen.prepare(sampleRate);
        lfo2Gen.prepare(sampleRate);
    }

    void startNote(int midiNoteNumber,
                   float velocity,
                   juce::SynthesiserSound* /*sound*/,
                   int currentPitchWheelPosition) override
    {
        currentMidiNote = midiNoteNumber;
        currentNormalizedVelocity = std::clamp(static_cast<double>(velocity), 0.0, 1.0);
        updatePitchWheel(currentPitchWheelPosition);

        const int midiVel127 = std::clamp(static_cast<int>(std::round(velocity * 127.0f)), 1, 127);
        const double baseFreqHz = juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber);

        // Configure & trigger Dual-Operator IXA Synth
        op1.setModEnvelopeParameters(voiceParams.op1ModEnv);
        op1.setAmpEnvelopeParameters(voiceParams.op1AmpEnv);
        op2.setModEnvelopeParameters(voiceParams.op2ModEnv);
        op2.setAmpEnvelopeParameters(voiceParams.op2AmpEnv);

        op1.noteOn(baseFreqHz * pitchBendFactor, midiVel127);
        op2.noteOn(baseFreqHz * pitchBendFactor, midiVel127);

        // Reset & prime the per-voice 12 dB/oct ZDF IXA lowpass filter and LFOs
        ixaLowpassFilter.reset();
        const uint32_t seed = static_cast<uint32_t>(midiNoteNumber * 131 + midiVel127 * 17 + 1);
        lfo1Gen.noteOn(voiceParams.lfo1.keySync, voiceParams.lfo1.delaySec, seed);
        lfo2Gen.noteOn(voiceParams.lfo2.keySync, voiceParams.lfo2.delaySec, seed ^ 0xA5A55A5Au);
        lpfControlCounter = 0;

        // Configure & trigger Dual PCM Sample Slots (Element A & Element B)
        const SampleZone* zoneA = nullptr;
        const SampleZone* zoneB = nullptr;
        {
            const juce::ScopedLock sl(sampleBank.getLock());
            if (const auto* groupA = sampleBank.getInstrumentGroupByIndex(voiceParams.slotA.instrumentIndex))
                zoneA = groupA->findZoneForNote(midiNoteNumber);
            if (const auto* groupB = sampleBank.getInstrumentGroupByIndex(voiceParams.slotB.instrumentIndex))
                zoneB = groupB->findZoneForNote(midiNoteNumber);
        }

        slotAPlayer.startNote(zoneA, midiNoteNumber, velocity, voiceParams.slotA);
        slotBPlayer.startNote(zoneB, midiNoteNumber, velocity, voiceParams.slotB);
    }

    void stopNote(float /*velocity*/, bool allowTailOff) override
    {
        slotAPlayer.stopNote(allowTailOff);
        slotBPlayer.stopNote(allowTailOff);

        if (allowTailOff)
        {
            op1.noteOff();
            op2.noteOff();
        }
        else
        {
            op1.reset();
            op2.reset();
            ixaLowpassFilter.reset();
            clearCurrentNote();
        }
    }

    void pitchWheelMoved(int newPitchWheelValue) override
    {
        updatePitchWheel(newPitchWheelValue);
        const double baseFreqHz = juce::MidiMessage::getMidiNoteInHertz(currentMidiNote);
        op1.setFrequency(baseFreqHz * pitchBendFactor);
        op2.setFrequency(baseFreqHz * pitchBendFactor);
    }

    void controllerMoved(int controllerNumber, int newControllerValue) override
    {
        if (controllerNumber == 1) // MIDI CC#1 Mod Wheel
        {
            modWheelDepth = std::clamp(static_cast<double>(newControllerValue) / 127.0, 0.0, 1.0);
        }
    }

    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                         int startSample,
                         int numSamples) override
    {
        if (!isVoiceActive())
            return;

        // Keep envelopes synchronized with real-time parameter updates
        op1.setModEnvelopeParameters(voiceParams.op1ModEnv);
        op1.setAmpEnvelopeParameters(voiceParams.op1AmpEnv);
        op2.setModEnvelopeParameters(voiceParams.op2ModEnv);
        op2.setAmpEnvelopeParameters(voiceParams.op2AmpEnv);

        const double noteFreqHz = juce::MidiMessage::getMidiNoteInHertz(currentMidiNote);

        const float synthPanAngle = (std::clamp(voiceParams.synthPan, -1.0f, 1.0f) + 1.0f)
                                  * 0.25f * juce::MathConstants<float>::pi;
        const float synthGainL = std::cos(synthPanAngle);
        const float synthGainR = std::sin(synthPanAngle);

        // Precompute LPF static terms for this block
        const double fCutBase = static_cast<double>(voiceParams.ixaLpfCutoffHz);
        const double envDepthOct = static_cast<double>(voiceParams.ixaLpfEnvOct);
        const double keyTrack = static_cast<double>(voiceParams.ixaLpfKeyTrack);
        const double keyTrackOct = keyTrack * (static_cast<double>(currentMidiNote - 60) / 12.0);
        const double velScale = 0.3 + 0.7 * currentNormalizedVelocity;
        const double maxValidCutoff = 0.48 * getSampleRate();

        // Cached LFO modulation offsets updated every 8 samples
        IXAOscillator::Parameters modOp1Params = voiceParams.op1Params;
        IXAOscillator::Parameters modOp2Params = voiceParams.op2Params;

        float* outL = outputBuffer.getWritePointer(0, startSample);
        float* outR = outputBuffer.getNumChannels() > 1
                    ? outputBuffer.getWritePointer(1, startSample)
                    : nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            if ((lpfControlCounter++ & 7) == 0)
            {
                // Evaluate LFO 1 (with CC#1 ModWheel boost) and LFO 2 every 8 samples
                const double lfo1Val = lfo1Gen.advance(voiceParams.lfo1, 8, modWheelDepth * 0.5);
                const double lfo2Val = lfo2Gen.advance(voiceParams.lfo2, 8, 0.0);

                pitchLfoSemitones = 0.0;
                ampTremoloGain = 1.0f;
                lpfCutoffLfoOct = 0.0;
                double op1IdxOffset = 0.0;
                double op2IdxOffset = 0.0;
                double op1WarpOffset = 0.0;
                double op1FbOffset = 0.0;
                double op2FreqOffsetHz = 0.0;
                double op2RatioOffset = 0.0;
                double pcmCrossfade = 0.0;

                auto accumulateLfoTarget = [&](LfoTarget target, double val) noexcept
                {
                    switch (target)
                    {
                        case LfoTarget::MasterPitch:
                            pitchLfoSemitones += val * 2.0; // +/- 2.0 semitones
                            break;
                        case LfoTarget::MasterAmplitude:
                            ampTremoloGain *= static_cast<float>(std::clamp(1.0 + val * 0.5, 0.0, 1.5));
                            break;
                        case LfoTarget::IxaLpfCutoff:
                            lpfCutoffLfoOct += val * 3.5; // +/- 3.5 octaves
                            break;
                        case LfoTarget::IxaOp1ModIndex:
                            op1IdxOffset += val * 3.0; // +/- 3.0 index
                            break;
                        case LfoTarget::IxaOp2ModIndex:
                            op2IdxOffset += val * 3.0; // +/- 3.0 index
                            break;
                        case LfoTarget::IxaBothOpsIndex:
                            op1IdxOffset += val * 3.0;
                            op2IdxOffset += val * 3.0;
                            break;
                        case LfoTarget::IxaOp1CarrierWarp:
                            op1WarpOffset += val * 0.8; // +/- 0.8 warp
                            break;
                        case LfoTarget::IxaOp1Feedback:
                            op1FbOffset += val * 0.35; // +/- 0.35 feedback
                            break;
                        case LfoTarget::IxaOp2FineFreq:
                            op2FreqOffsetHz += val * 35.0;
                            op2RatioOffset  += val * 0.5;
                            break;
                        case LfoTarget::PcmABCrossfade:
                            pcmCrossfade += val;
                            break;
                        case LfoTarget::None:
                        default:
                            break;
                    }
                };

                accumulateLfoTarget(voiceParams.lfo1.target, lfo1Val);
                accumulateLfoTarget(voiceParams.lfo2.target, lfo2Val);

                // Apply pitch bend + LFO vibrato to Operators 1 & 2
                const double totalPitchSemi = pitchBendSemitones + pitchLfoSemitones;
                const double instFreqHz = noteFreqHz * std::pow(2.0, totalPitchSemi / 12.0);
                op1.setFrequency(instFreqHz);
                op2.setFrequency(instFreqHz);

                // Apply LFO modulation offsets to IXA Operator parameters
                modOp1Params = voiceParams.op1Params;
                modOp1Params.baseModIndex = std::clamp(voiceParams.op1Params.baseModIndex + op1IdxOffset, 0.0, 6.0);
                modOp1Params.carrierWarp  = std::clamp(voiceParams.op1Params.carrierWarp  + op1WarpOffset, 0.0, 1.0);
                modOp1Params.feedback     = std::clamp(voiceParams.op1Params.feedback     + op1FbOffset,   0.0, 0.5);

                modOp2Params = voiceParams.op2Params;
                modOp2Params.baseModIndex = std::clamp(voiceParams.op2Params.baseModIndex + op2IdxOffset, 0.0, 6.0);
                modOp2Params.coarseRatio  = std::clamp(voiceParams.op2Params.coarseRatio  + op2RatioOffset, 0.5, 16.0);
                modOp2Params.fineDetuneHz = std::clamp(voiceParams.op2Params.fineDetuneHz + op2FreqOffsetHz, -100.0, 100.0);

                // Compute PCM A/B Crossfade scale factors
                pcmALevelScale = static_cast<float>(std::clamp(1.0 - std::max(0.0, pcmCrossfade), 0.0, 1.5));
                pcmBLevelScale = static_cast<float>(std::clamp(1.0 + std::min(0.0, pcmCrossfade), 0.0, 1.5));
                if (pcmCrossfade > 0.0)
                    pcmBLevelScale = static_cast<float>(std::clamp(1.0 + pcmCrossfade * 0.5, 0.0, 1.5));
                else if (pcmCrossfade < 0.0)
                    pcmALevelScale = static_cast<float>(std::clamp(1.0 - pcmCrossfade * 0.5, 0.0, 1.5));

                // Update Phase 4 12dB LPF coefficients (including LFO cutoff modulation)
                const bool bypassLpf = (fCutBase >= 19800.0
                                     && std::abs(envDepthOct) < 1.0e-4
                                     && std::abs(lpfCutoffLfoOct) < 1.0e-4);
                currentBypassLpf = bypassLpf;
                if (!bypassLpf)
                {
                    const double eMod = op1.getLastModEnvValue();
                    const double exponentOct = keyTrackOct + (envDepthOct * eMod * velScale) + lpfCutoffLfoOct;
                    const double fInstant = std::clamp(fCutBase * std::pow(2.0, exponentOct),
                                                       40.0, maxValidCutoff);
                    ixaLowpassFilter.updateCoefficients(fInstant);
                }
            }

            float pcmAL = 0.0f, pcmAR = 0.0f;
            float pcmBL = 0.0f, pcmBR = 0.0f;

            const double totalPitchSemi = pitchBendSemitones + pitchLfoSemitones;
            slotAPlayer.renderNextSample(voiceParams.slotA, totalPitchSemi, pcmALevelScale, pcmAL, pcmAR);
            slotBPlayer.renderNextSample(voiceParams.slotB, totalPitchSemi, pcmBLevelScale, pcmBL, pcmBR);

            float synthL = 0.0f;
            float synthR = 0.0f;

            if (voiceParams.synthEnabled)
            {
                double synthMono = 0.0;
                switch (voiceParams.routingMode)
                {
                    case IXARoutingMode::ParallelDetuned:
                    {
                        const double s1 = op1.processSample(modOp1Params, 0.0) * voiceParams.op1Level;
                        const double s2 = op2.processSample(modOp2Params, 0.0) * voiceParams.op2Level;
                        synthMono = s1 + s2;
                        break;
                    }
                    case IXARoutingMode::SeriesCascadeOp2ToOp1:
                    {
                        const double op2Out = op2.processSample(modOp2Params, 0.0)
                                            * (voiceParams.op2Level * 4.0);
                        synthMono = op1.processSample(modOp1Params, op2Out) * voiceParams.op1Level;
                        break;
                    }
                    case IXARoutingMode::SingleOp1:
                    default:
                    {
                        synthMono = op1.processSample(modOp1Params, 0.0) * voiceParams.op1Level;
                        break;
                    }
                }

                if (!currentBypassLpf)
                    synthMono = ixaLowpassFilter.processSample(synthMono);

                synthL = static_cast<float>(synthMono) * synthGainL;
                synthR = static_cast<float>(synthMono) * synthGainR;
            }

            const float totalL = (pcmAL + pcmBL + synthL) * ampTremoloGain;
            const float totalR = (pcmAR + pcmBR + synthR) * ampTremoloGain;

            outL[i] += totalL;
            if (outR != nullptr)
                outR[i] += totalR;

            const bool anyActive = slotAPlayer.isActive()
                                || slotBPlayer.isActive()
                                || (voiceParams.synthEnabled && (op1.isActive() || op2.isActive()));

            if (!anyActive)
            {
                clearCurrentNote();
                break;
            }
        }
    }

private:
    void updatePitchWheel(int pitchWheelVal) noexcept
    {
        const double norm = (static_cast<double>(pitchWheelVal) - 8192.0) / 8192.0;
        pitchBendSemitones = norm * 2.0; // +/- 2 semitones standard bend range
        pitchBendFactor = std::pow(2.0, pitchBendSemitones / 12.0);
    }

    const CasioSampleBankManager& sampleBank;
    const IXAVoiceParameters& voiceParams;

    PCMSampleElementPlayer slotAPlayer;
    PCMSampleElementPlayer slotBPlayer;
    IXAOscillator op1;
    IXAOscillator op2;
    IXAZdfButterworthLPF ixaLowpassFilter;
    VoiceLFOGenerator lfo1Gen;
    VoiceLFOGenerator lfo2Gen;

    int currentMidiNote = 60;
    double currentNormalizedVelocity = 1.0;
    double pitchBendSemitones = 0.0;
    double pitchBendFactor = 1.0;
    double modWheelDepth = 0.0;

    // Control-rate LFO state
    double pitchLfoSemitones = 0.0;
    float ampTremoloGain = 1.0f;
    double lpfCutoffLfoOct = 0.0;
    float pcmALevelScale = 1.0f;
    float pcmBLevelScale = 1.0f;
    bool currentBypassLpf = true;
    int lpfControlCounter = 0;
};

} // namespace CasioIXA
