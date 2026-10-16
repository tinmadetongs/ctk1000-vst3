#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>

namespace CasioIXA
{

enum class CTK1000DSPMode : int
{
    HallReverb   = 0,
    Delay        = 1,
    Chorus       = 2,
    Distortion   = 3,
    RoomReverb   = 4,
    StereoDelay  = 5,
    Flanger      = 6,
    ChorusReverb = 7
};

/**
 * 8-Mode Semi-OBS DSP Effects Unit (`Sanyo LC33832PL-70` 32 KB PSRAM Emulation)
 * Implements the 8 hardware effect modes of the 1993 Casio CTK-1000 plus the
 * diagnostic service mode post-filter (`PrFit`) and 32 KB PSRAM Lo-Fi bandwidth/quantization model.
 */
class CTK1000Effects
{
public:
    struct Parameters
    {
        CTK1000DSPMode dspMode = CTK1000DSPMode::HallReverb;
        float param1 = 0.5f;         // Rate / Delay Time / Reverb Size / Drive [0, 1]
        float param2 = 0.45f;        // Depth / Feedback / Damping [0, 1]
        float toneFilter = 0.75f;    // PrFit post-filter cutoff / damping [0, 1]
        float wetDryMix = 0.30f;     // Wet/Dry balance [0, 1]
        bool lofiPsramMode = false;  // 32 KB Sanyo LC33832PL-70 PSRAM 12-bit/22.05kHz & 10kHz roll-off
        bool bypass = false;
    };

    CTK1000Effects()
    {
        reset();
    }

    void prepare(double newSampleRate, int /*maxBlockSize*/)
    {
        sampleRate = (newSampleRate > 0.0) ? newSampleRate : 44100.0;

        // Allocate circular delay buffers (up to 2.0 seconds at current sample rate)
        maxDelaySamples = std::max(4096, static_cast<int>(std::ceil(sampleRate * 2.0)));
        delayBufL.assign(static_cast<size_t>(maxDelaySamples), 0.0f);
        delayBufR.assign(static_cast<size_t>(maxDelaySamples), 0.0f);

        modDelayBufL.assign(static_cast<size_t>(maxDelaySamples), 0.0f);
        modDelayBufR.assign(static_cast<size_t>(maxDelaySamples), 0.0f);

        // Configure Freeverb-style Schroeder/Moorer comb and allpass networks scaled to sampleRate
        const double srScale = sampleRate / 44100.0;
        static constexpr std::array<int, 4> kBaseCombLengthsL = { 1116, 1188, 1277, 1356 };
        static constexpr std::array<int, 4> kBaseCombLengthsR = { 1139, 1211, 1300, 1379 };
        static constexpr std::array<int, 2> kBaseApLengthsL   = { 556, 441 };
        static constexpr std::array<int, 2> kBaseApLengthsR   = { 579, 464 };

        for (size_t i = 0; i < 4; ++i)
        {
            const int lenL = std::max(64, static_cast<int>(std::round(kBaseCombLengthsL[i] * srScale)));
            const int lenR = std::max(64, static_cast<int>(std::round(kBaseCombLengthsR[i] * srScale)));
            combBufL[i].assign(static_cast<size_t>(lenL), 0.0f);
            combBufR[i].assign(static_cast<size_t>(lenR), 0.0f);
            combIdxL[i] = 0;
            combIdxR[i] = 0;
            combFilterStateL[i] = 0.0f;
            combFilterStateR[i] = 0.0f;
        }

        for (size_t i = 0; i < 2; ++i)
        {
            const int lenL = std::max(32, static_cast<int>(std::round(kBaseApLengthsL[i] * srScale)));
            const int lenR = std::max(32, static_cast<int>(std::round(kBaseApLengthsR[i] * srScale)));
            apBufL[i].assign(static_cast<size_t>(lenL), 0.0f);
            apBufR[i].assign(static_cast<size_t>(lenR), 0.0f);
            apIdxL[i] = 0;
            apIdxR[i] = 0;
        }

        reset();
    }

    void reset() noexcept
    {
        std::fill(delayBufL.begin(), delayBufL.end(), 0.0f);
        std::fill(delayBufR.begin(), delayBufR.end(), 0.0f);
        std::fill(modDelayBufL.begin(), modDelayBufL.end(), 0.0f);
        std::fill(modDelayBufR.begin(), modDelayBufR.end(), 0.0f);

        for (size_t i = 0; i < 4; ++i)
        {
            std::fill(combBufL[i].begin(), combBufL[i].end(), 0.0f);
            std::fill(combBufR[i].begin(), combBufR[i].end(), 0.0f);
            combIdxL[i] = 0;
            combIdxR[i] = 0;
            combFilterStateL[i] = 0.0f;
            combFilterStateR[i] = 0.0f;
        }

        for (size_t i = 0; i < 2; ++i)
        {
            std::fill(apBufL[i].begin(), apBufL[i].end(), 0.0f);
            std::fill(apBufR[i].begin(), apBufR[i].end(), 0.0f);
            apIdxL[i] = 0;
            apIdxR[i] = 0;
        }

        writePos = 0;
        modWritePos = 0;
        lfoPhase = 0.0;
        prFitStateL = 0.0f;
        prFitStateR = 0.0f;
        psramLpStateL = 0.0f;
        psramLpStateR = 0.0f;
        psramHoldPhase = 0.0;
        psramHeldWetL = 0.0f;
        psramHeldWetR = 0.0f;
    }

    void processBlock(juce::AudioBuffer<float>& buffer, const Parameters& params) noexcept
    {
        if (params.bypass || buffer.getNumSamples() == 0)
            return;

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        float* left = buffer.getWritePointer(0);
        float* right = (numChannels > 1) ? buffer.getWritePointer(1) : nullptr;

        const float p1 = std::clamp(params.param1, 0.0f, 1.0f);
        const float p2 = std::clamp(params.param2, 0.0f, 1.0f);
        const float tone = std::clamp(params.toneFilter, 0.0f, 1.0f);
        const float mix = std::clamp(params.wetDryMix, 0.0f, 1.0f);

        // Diagnostic service mode post-filter (`PrFit`) cutoff (400 Hz to 18,000 Hz)
        const double cutoffHz = 400.0 * std::pow(45.0, static_cast<double>(tone));
        const double rc = 1.0 / (juce::MathConstants<double>::twoPi * std::min(cutoffHz, sampleRate * 0.48));
        const double dt = 1.0 / sampleRate;
        const float prFitAlpha = static_cast<float>(dt / (rc + dt));

        // 10 kHz feedback anti-aliasing / bandwidth limit for Sanyo LC33832PL-70 32 KB PSRAM mode
        const double psramCutoffHz = 9500.0;
        const double psramRc = 1.0 / (juce::MathConstants<double>::twoPi * std::min(psramCutoffHz, sampleRate * 0.48));
        const float psramAlpha = static_cast<float>(dt / (psramRc + dt));

        for (int i = 0; i < numSamples; ++i)
        {
            const float dryL = left[i];
            const float dryR = (right != nullptr) ? right[i] : dryL;

            float wetL = 0.0f;
            float wetR = 0.0f;

            switch (params.dspMode)
            {
                case CTK1000DSPMode::HallReverb:
                    processReverbSample(dryL, dryR, p1, p2, true, params.lofiPsramMode, psramAlpha, wetL, wetR);
                    break;

                case CTK1000DSPMode::RoomReverb:
                    processReverbSample(dryL, dryR, p1, p2, false, params.lofiPsramMode, psramAlpha, wetL, wetR);
                    break;

                case CTK1000DSPMode::Delay:
                    processDelaySample(dryL, dryR, p1, p2, false, params.lofiPsramMode, psramAlpha, wetL, wetR);
                    break;

                case CTK1000DSPMode::StereoDelay:
                    processDelaySample(dryL, dryR, p1, p2, true, params.lofiPsramMode, psramAlpha, wetL, wetR);
                    break;

                case CTK1000DSPMode::Chorus:
                    processModulatedDelaySample(dryL, dryR, p1, p2, false, params.lofiPsramMode, psramAlpha, wetL, wetR);
                    break;

                case CTK1000DSPMode::Flanger:
                    processModulatedDelaySample(dryL, dryR, p1, p2, true, params.lofiPsramMode, psramAlpha, wetL, wetR);
                    break;

                case CTK1000DSPMode::Distortion:
                    processDistortionSample(dryL, dryR, p1, p2, params.lofiPsramMode, wetL, wetR);
                    break;

                case CTK1000DSPMode::ChorusReverb:
                {
                    float chL = 0.0f, chR = 0.0f;
                    processModulatedDelaySample(dryL, dryR, p1, 0.45f, false, params.lofiPsramMode, psramAlpha, chL, chR);
                    const float cascadeInL = 0.6f * dryL + 0.4f * chL;
                    const float cascadeInR = 0.6f * dryR + 0.4f * chR;
                    float revL = 0.0f, revR = 0.0f;
                    processReverbSample(cascadeInL, cascadeInR, 0.65f, p2, true, params.lofiPsramMode, psramAlpha, revL, revR);
                    wetL = 0.45f * chL + 0.55f * revL;
                    wetR = 0.45f * chR + 0.55f * revR;
                    break;
                }
            }

            // Apply 32 KB PSRAM sample-and-hold (22.05 kHz clock) and 12-bit quantization if enabled
            if (params.lofiPsramMode)
            {
                const double ratio = 22050.0 / sampleRate;
                psramHoldPhase += ratio;
                if (psramHoldPhase >= 1.0)
                {
                    psramHoldPhase -= 1.0;
                    psramHeldWetL = quantizePsram(wetL);
                    psramHeldWetR = quantizePsram(wetR);
                }
                wetL = psramHeldWetL;
                wetR = psramHeldWetR;
            }

            // Apply PrFit post-filter
            prFitStateL += prFitAlpha * (wetL - prFitStateL);
            prFitStateR += prFitAlpha * (wetR - prFitStateR);

            const float filteredWetL = prFitStateL;
            const float filteredWetR = prFitStateR;

            left[i] = (1.0f - mix) * dryL + mix * filteredWetL;
            if (right != nullptr)
                right[i] = (1.0f - mix) * dryR + mix * filteredWetR;
        }
    }

private:
    static inline float quantizePsram(float x) noexcept
    {
        // Emulates 12-bit floating/compressed PSRAM word resolution in Sanyo LC33832PL-70
        const float clamped = std::clamp(x, -1.0f, 1.0f);
        return std::round(clamped * 2047.0f) / 2047.0f;
    }

    float readFractionalDelay(const std::vector<float>& buf, int writeIndex, double delaySamples) const noexcept
    {
        const int size = static_cast<int>(buf.size());
        double readPos = static_cast<double>(writeIndex) - delaySamples;
        while (readPos < 0.0)
            readPos += static_cast<double>(size);
        while (readPos >= static_cast<double>(size))
            readPos -= static_cast<double>(size);

        const int idx0 = static_cast<int>(readPos) % size;
        const int idx1 = (idx0 + 1) % size;
        const float frac = static_cast<float>(readPos - std::floor(readPos));
        return buf[static_cast<size_t>(idx0)] + frac * (buf[static_cast<size_t>(idx1)] - buf[static_cast<size_t>(idx0)]);
    }

    void processReverbSample(float inL,
                             float inR,
                             float sizeParam,
                             float dampParam,
                             bool isHall,
                             bool lofiPsram,
                             float psramAlpha,
                             float& outL,
                             float& outR) noexcept
    {
        const float monoIn = 0.5f * (inL + inR) * 0.25f;
        const float feedback = isHall
                             ? (0.74f + 0.22f * sizeParam)
                             : (0.50f + 0.30f * sizeParam);
        const float damp = 0.15f + 0.70f * dampParam;

        float sumL = 0.0f;
        float sumR = 0.0f;

        for (size_t c = 0; c < 4; ++c)
        {
            auto& bL = combBufL[c];
            auto& bR = combBufR[c];

            const float yL = bL[combIdxL[c]];
            const float yR = bR[combIdxR[c]];

            combFilterStateL[c] = (1.0f - damp) * yL + damp * combFilterStateL[c];
            combFilterStateR[c] = (1.0f - damp) * yR + damp * combFilterStateR[c];

            float fbL = monoIn + feedback * combFilterStateL[c];
            float fbR = monoIn + feedback * combFilterStateR[c];

            if (lofiPsram)
            {
                fbL = quantizePsram(fbL);
                fbR = quantizePsram(fbR);
            }

            bL[combIdxL[c]] = fbL;
            bR[combIdxR[c]] = fbR;

            if (++combIdxL[c] >= bL.size()) combIdxL[c] = 0;
            if (++combIdxR[c] >= bR.size()) combIdxR[c] = 0;

            sumL += yL;
            sumR += yR;
        }

        // Series Allpass Diffusers
        for (size_t a = 0; a < 2; ++a)
        {
            auto& aL = apBufL[a];
            auto& aR = apBufR[a];

            const float bufOutL = aL[apIdxL[a]];
            const float bufOutR = aR[apIdxR[a]];

            const float g = 0.5f;
            const float newL = sumL + g * bufOutL;
            const float newR = sumR + g * bufOutR;

            aL[apIdxL[a]] = newL;
            aR[apIdxR[a]] = newR;

            if (++apIdxL[a] >= aL.size()) apIdxL[a] = 0;
            if (++apIdxR[a] >= aR.size()) apIdxR[a] = 0;

            sumL = bufOutL - g * newL;
            sumR = bufOutR - g * newR;
        }

        if (lofiPsram)
        {
            psramLpStateL += psramAlpha * (sumL - psramLpStateL);
            psramLpStateR += psramAlpha * (sumR - psramLpStateR);
            sumL = psramLpStateL;
            sumR = psramLpStateR;
        }

        outL = sumL;
        outR = sumR;
    }

    void processDelaySample(float inL,
                            float inR,
                            float timeParam,
                            float feedbackParam,
                            bool stereoPingPong,
                            bool lofiPsram,
                            float psramAlpha,
                            float& outL,
                            float& outR) noexcept
    {
        // Delay time range: 45 ms to 680 ms (fits within 32KB PSRAM @ 22.05kHz)
        const double delaySecL = 0.045 + 0.635 * static_cast<double>(timeParam);
        const double delaySecR = stereoPingPong ? (delaySecL * 0.75) : delaySecL;

        const double dSamplesL = std::clamp(delaySecL * sampleRate, 1.0, static_cast<double>(maxDelaySamples - 2));
        const double dSamplesR = std::clamp(delaySecR * sampleRate, 1.0, static_cast<double>(maxDelaySamples - 2));

        const float tapL = readFractionalDelay(delayBufL, writePos, dSamplesL);
        const float tapR = readFractionalDelay(delayBufR, writePos, dSamplesR);

        const float fb = 0.85f * feedbackParam;

        float nextL = stereoPingPong ? (inL + fb * tapR) : (0.5f * (inL + inR) + fb * tapL);
        float nextR = stereoPingPong ? (inR + fb * tapL) : nextL;

        if (lofiPsram)
        {
            psramLpStateL += psramAlpha * (nextL - psramLpStateL);
            psramLpStateR += psramAlpha * (nextR - psramLpStateR);
            nextL = quantizePsram(psramLpStateL);
            nextR = quantizePsram(psramLpStateR);
        }

        delayBufL[static_cast<size_t>(writePos)] = nextL;
        delayBufR[static_cast<size_t>(writePos)] = nextR;

        if (++writePos >= maxDelaySamples)
            writePos = 0;

        outL = tapL;
        outR = tapR;
    }

    void processModulatedDelaySample(float inL,
                                     float inR,
                                     float rateParam,
                                     float depthParam,
                                     bool isFlanger,
                                     bool lofiPsram,
                                     float psramAlpha,
                                     float& outL,
                                     float& outR) noexcept
    {
        const double lfoHz = isFlanger
                           ? (0.08 + 3.5 * static_cast<double>(rateParam))
                           : (0.15 + 4.5 * static_cast<double>(rateParam));

        lfoPhase += lfoHz / sampleRate;
        if (lfoPhase >= 1.0)
            lfoPhase -= 1.0;

        const double lfoL = std::sin(juce::MathConstants<double>::twoPi * lfoPhase);
        const double lfoR = std::cos(juce::MathConstants<double>::twoPi * lfoPhase); // Quadrature stereo LFO

        const double centerMs = isFlanger ? 3.2 : 18.5;
        const double sweepMs  = isFlanger ? (2.6 * depthParam) : (6.5 * depthParam);

        const double dSamplesL = std::clamp((centerMs + sweepMs * lfoL) * 0.001 * sampleRate,
                                            1.0, static_cast<double>(maxDelaySamples - 2));
        const double dSamplesR = std::clamp((centerMs + sweepMs * lfoR) * 0.001 * sampleRate,
                                            1.0, static_cast<double>(maxDelaySamples - 2));

        const float tapL = readFractionalDelay(modDelayBufL, modWritePos, dSamplesL);
        const float tapR = readFractionalDelay(modDelayBufR, modWritePos, dSamplesR);

        const float fb = isFlanger ? (0.78f * depthParam) : (0.18f * depthParam);
        float nextL = inL + fb * tapL;
        float nextR = inR + fb * tapR;

        if (lofiPsram)
        {
            psramLpStateL += psramAlpha * (nextL - psramLpStateL);
            psramLpStateR += psramAlpha * (nextR - psramLpStateR);
            nextL = quantizePsram(psramLpStateL);
            nextR = quantizePsram(psramLpStateR);
        }

        modDelayBufL[static_cast<size_t>(modWritePos)] = nextL;
        modDelayBufR[static_cast<size_t>(modWritePos)] = nextR;

        if (++modWritePos >= maxDelaySamples)
            modWritePos = 0;

        outL = tapL;
        outR = tapR;
    }

    void processDistortionSample(float inL,
                                 float inR,
                                 float driveParam,
                                 float shapeParam,
                                 bool lofiPsram,
                                 float& outL,
                                 float& outR) noexcept
    {
        const float drive = 1.0f + 24.0f * driveParam;
        const float hardClipBlend = std::clamp(shapeParam, 0.0f, 1.0f);

        auto waveshape = [drive, hardClipBlend, lofiPsram](float x) noexcept -> float
        {
            const float driven = x * drive;
            const float soft = std::tanh(driven);
            const float hard = std::clamp(driven, -1.0f, 1.0f);
            float shaped = (1.0f - hardClipBlend) * soft + hardClipBlend * hard;
            if (lofiPsram)
                shaped = quantizePsram(shaped);
            return shaped * 0.70f;
        };

        outL = waveshape(inL);
        outR = waveshape(inR);
    }

    double sampleRate = 44100.0;
    int maxDelaySamples = 88200;

    std::vector<float> delayBufL;
    std::vector<float> delayBufR;
    int writePos = 0;

    std::vector<float> modDelayBufL;
    std::vector<float> modDelayBufR;
    int modWritePos = 0;
    double lfoPhase = 0.0;

    std::array<std::vector<float>, 4> combBufL;
    std::array<std::vector<float>, 4> combBufR;
    std::array<size_t, 4> combIdxL { 0, 0, 0, 0 };
    std::array<size_t, 4> combIdxR { 0, 0, 0, 0 };
    std::array<float, 4> combFilterStateL { 0.0f, 0.0f, 0.0f, 0.0f };
    std::array<float, 4> combFilterStateR { 0.0f, 0.0f, 0.0f, 0.0f };

    std::array<std::vector<float>, 2> apBufL;
    std::array<std::vector<float>, 2> apBufR;
    std::array<size_t, 2> apIdxL { 0, 0 };
    std::array<size_t, 2> apIdxR { 0, 0 };

    float prFitStateL = 0.0f;
    float prFitStateR = 0.0f;
    float psramLpStateL = 0.0f;
    float psramLpStateR = 0.0f;

    double psramHoldPhase = 0.0;
    float psramHeldWetL = 0.0f;
    float psramHeldWetR = 0.0f;
};

} // namespace CasioIXA
