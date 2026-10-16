#pragma once

#include "IXAOscillator.h"
#include "CasioSampleBankManager.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <algorithm>

namespace CasioIXA
{

enum class IXARoutingMode : int
{
    ParallelDetuned = 0,
    SeriesCascadeOp2ToOp1 = 1,
    SingleOp1 = 2
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
        // Butterworth Q = 1 / sqrt(2) => k = 1 / Q = sqrt(2)
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

        // Compute pitch ratio including root note transposition, coarse/fine tune, and pitch wheel
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
        const float amp = params.level * velGain * envVal;

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
 * Combines Dual PCM Sample Elements (Slot A & Slot B) with Dual-Operator IXA Triangular Wave Modulation
 * (Operators 1 & 2 in Parallel Detuned, 2-Op Series Cascade, or Single-Op routing) followed by a
 * dedicated per-voice 12 dB/oct Butterworth ZDF Non-Resonant Lowpass Filter on the IXA Synth path.
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

        // Reset & prime the per-voice 12 dB/oct ZDF IXA lowpass filter
        ixaLowpassFilter.reset();
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

    void controllerMoved(int /*controllerNumber*/, int /*newControllerValue*/) override {}

    void renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                         int startSample,
                         int numSamples) override
    {
        if (!isVoiceActive())
            return;

        // Keep envelopes and frequencies synchronized with real-time parameter updates
        op1.setModEnvelopeParameters(voiceParams.op1ModEnv);
        op1.setAmpEnvelopeParameters(voiceParams.op1AmpEnv);
        op2.setModEnvelopeParameters(voiceParams.op2ModEnv);
        op2.setAmpEnvelopeParameters(voiceParams.op2AmpEnv);

        const double baseFreqHz = juce::MidiMessage::getMidiNoteInHertz(currentMidiNote) * pitchBendFactor;
        op1.setFrequency(baseFreqHz);
        op2.setFrequency(baseFreqHz);

        const float synthPanAngle = (std::clamp(voiceParams.synthPan, -1.0f, 1.0f) + 1.0f)
                                  * 0.25f * juce::MathConstants<float>::pi;
        const float synthGainL = std::cos(synthPanAngle);
        const float synthGainR = std::sin(synthPanAngle);

        // Precompute LPF static terms for this block
        const double fCutBase = static_cast<double>(voiceParams.ixaLpfCutoffHz);
        const double envDepthOct = static_cast<double>(voiceParams.ixaLpfEnvOct);
        const double keyTrack = static_cast<double>(voiceParams.ixaLpfKeyTrack);
        const bool bypassLpf = (fCutBase >= 19800.0 && std::abs(envDepthOct) < 1.0e-4);
        const double keyTrackOct = keyTrack * (static_cast<double>(currentMidiNote - 60) / 12.0);
        const double velScale = 0.3 + 0.7 * currentNormalizedVelocity;
        const double maxValidCutoff = 0.48 * getSampleRate();

        float* outL = outputBuffer.getWritePointer(0, startSample);
        float* outR = outputBuffer.getNumChannels() > 1
                    ? outputBuffer.getWritePointer(1, startSample)
                    : nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            float pcmAL = 0.0f, pcmAR = 0.0f;
            float pcmBL = 0.0f, pcmBR = 0.0f;

            slotAPlayer.renderNextSample(voiceParams.slotA, pitchBendSemitones, pcmAL, pcmAR);
            slotBPlayer.renderNextSample(voiceParams.slotB, pitchBendSemitones, pcmBL, pcmBR);

            float synthL = 0.0f;
            float synthR = 0.0f;

            if (voiceParams.synthEnabled)
            {
                double synthMono = 0.0;
                switch (voiceParams.routingMode)
                {
                    case IXARoutingMode::ParallelDetuned:
                    {
                        const double s1 = op1.processSample(voiceParams.op1Params, 0.0) * voiceParams.op1Level;
                        const double s2 = op2.processSample(voiceParams.op2Params, 0.0) * voiceParams.op2Level;
                        synthMono = s1 + s2;
                        break;
                    }
                    case IXARoutingMode::SeriesCascadeOp2ToOp1:
                    {
                        // Op 2 modulates Op 1 external phase phi_ext
                        const double op2Out = op2.processSample(voiceParams.op2Params, 0.0)
                                            * (voiceParams.op2Level * 4.0);
                        synthMono = op1.processSample(voiceParams.op1Params, op2Out) * voiceParams.op1Level;
                        break;
                    }
                    case IXARoutingMode::SingleOp1:
                    default:
                    {
                        synthMono = op1.processSample(voiceParams.op1Params, 0.0) * voiceParams.op1Level;
                        break;
                    }
                }

                // Per-Voice 12 dB/octave Non-Resonant ZDF Butterworth Lowpass Filter on IXA Synth signal
                if (!bypassLpf)
                {
                    if ((lpfControlCounter++ & 7) == 0)
                    {
                        const double eMod = op1.getLastModEnvValue();
                        const double exponentOct = keyTrackOct + (envDepthOct * eMod * velScale);
                        const double fInstant = std::clamp(fCutBase * std::pow(2.0, exponentOct),
                                                           40.0, maxValidCutoff);
                        ixaLowpassFilter.updateCoefficients(fInstant);
                    }
                    synthMono = ixaLowpassFilter.processSample(synthMono);
                }

                synthL = static_cast<float>(synthMono) * synthGainL;
                synthR = static_cast<float>(synthMono) * synthGainR;
            }

            const float totalL = pcmAL + pcmBL + synthL;
            const float totalR = pcmAR + pcmBR + synthR;

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

    int currentMidiNote = 60;
    double currentNormalizedVelocity = 1.0;
    double pitchBendSemitones = 0.0;
    double pitchBendFactor = 1.0;
    int lpfControlCounter = 0;
};

} // namespace CasioIXA
