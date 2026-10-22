#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <algorithm>
#include <array>

namespace CasioIXA
{

/**
 * US Patent 5,164,530: Triangular Wave Modulation Oscillator (NEC uPD939GD IXA Engine)
 * Implements exact 3-branch monotonous pre-distorted carrier phase W_alpha(phi_c),
 * continuous modulation index n[k], modulo-4.0 wavefolder Delta(x[k]),
 * First-Order Antiderivative Anti-Aliasing (ADAA), and 1993 22,050 Hz DAC emulation.
 */
class IXAOscillator
{
public:
    struct Parameters
    {
        double coarseRatio = 1.0;        // 0.5 to 16.0
        double fineDetuneHz = 0.0;       // -50.0 to +50.0 Hz
        double baseModIndex = 1.0;       // n_0 in [0.0, 6.0]
        double envModDepth = 0.0;        // n_env in [-5.0, +5.0]
        double velModDepth = 0.0;        // n_vel in [0.0, 5.0]
        double carrierWarp = 1.0;        // alpha in [0.0, 1.0]
        double feedback = 0.0;           // beta in [0.0, 0.5]
        bool adaaEnabled = true;         // First-order ADAA toggle
        bool dac1993Mode = false;        // 22,050 Hz ZOH & ADAA bypass
        bool legacyMacroOverride = false;// Stepped 0-9 CTK-1000 Tone Editor table
        int legacyMacroStep = 0;         // 0 to 9
    };

    IXAOscillator()
    {
        reset();
    }

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = (newSampleRate > 0.0) ? newSampleRate : 44100.0;
        modEnv.setSampleRate(sampleRate);
        ampEnv.setSampleRate(sampleRate);
        reset();
    }

    void reset() noexcept
    {
        phiC = 0.0;
        phiM = 0.0;
        prevX = 0.0;
        prevF1 = 0.0;
        prevY = 0.0;
        lastModEnvValue = 0.0;
        firstSample = true;
        dacPhase = 0.0;
        heldSample = 0.0;
        holdingAmpForFakeSustain = false;
        heldAmpEnvValue = 0.0;
        modEnv.reset();
        ampEnv.reset();
    }

    void setModEnvelopeParameters(const juce::ADSR::Parameters& params) noexcept
    {
        modEnv.setParameters(params);
    }

    void setAmpEnvelopeParameters(const juce::ADSR::Parameters& params) noexcept
    {
        ampEnv.setParameters(params);
    }

    void noteOn(double freqHz, int midiVelocity) noexcept
    {
        carrierFreqHz = freqHz;
        normalizedVelocity = std::clamp(static_cast<double>(midiVelocity) / 127.0, 0.0, 1.0);
        velocityCurve = std::pow(normalizedVelocity, 1.8);
        phiC = 0.0;
        phiM = 0.0;
        prevX = 0.0;
        prevF1 = 0.0;
        prevY = 0.0;
        lastModEnvValue = 0.0;
        firstSample = true;
        dacPhase = 0.0;
        heldSample = 0.0;
        holdingAmpForFakeSustain = false;
        heldAmpEnvValue = 0.0;
        modEnv.noteOn();
        ampEnv.noteOn();
    }

    void noteOff(bool holdAmpForFakeSustain = false) noexcept
    {
        modEnv.noteOff();
        if (holdAmpForFakeSustain)
            holdingAmpForFakeSustain = true;
        else
            ampEnv.noteOff();
    }

    bool isActive() const noexcept
    {
        return holdingAmpForFakeSustain || ampEnv.isActive();
    }

    void setFrequency(double freqHz) noexcept
    {
        carrierFreqHz = freqHz;
    }

    double getLastModEnvValue() const noexcept { return lastModEnvValue; }
    double getNormalizedVelocity() const noexcept { return normalizedVelocity; }

    static inline double positiveMod(double a, double b) noexcept
    {
        return a - b * std::floor(a / b);
    }

    /**
     * Exact 3-branch piecewise algebraic equivalence of W(phi_c) from US Patent 5,164,530
     * Maps [0, 1) monotonically onto [0, 4) with zero-slope saddle points at 0.25 and 0.75.
     */
    static inline double computeWarpedPhase(double phi, double alpha) noexcept
    {
        const double sc = std::sin(juce::MathConstants<double>::twoPi * phi);
        double w = 0.0;
        if (phi < 0.25)
        {
            w = sc;
        }
        else if (phi < 0.75)
        {
            w = 2.0 - sc;
        }
        else
        {
            w = 4.0 + sc;
        }

        const double clampedAlpha = std::clamp(alpha, 0.0, 1.0);
        return (1.0 - clampedAlpha) * (4.0 * phi) + clampedAlpha * w;
    }

    /**
     * Wavefolder Delta(x) in [-1, +1]
     */
    static inline double evaluateWavefolder(double x) noexcept
    {
        const double u = positiveMod(x + 1.0, 4.0);
        return 1.0 - std::abs(u - 2.0);
    }

    /**
     * First-Order Antiderivative F_1(x) of the modulo-4.0 triangle wavefolder
     * Continuous on R with C^1 continuity across period boundaries via periodic unwrapping.
     */
    static inline double evaluateAntiderivativeF1(double x) noexcept
    {
        const double u = positiveMod(x + 1.0, 4.0);
        if (u < 2.0)
            return 0.5 * (u - 1.0) * (u - 1.0) - 0.5;
        else
            return -0.5 * (3.0 - u) * (3.0 - u) + 0.5;
    }

    /**
     * Renders a single sample of the IXA Operator.
     * @param params     Operator parameter struct
     * @param extPhase   External phase modulation input phi_ext (used in 2-Op Series Cascade)
     * @return           Envelope-scaled operator output in [-1, +1]
     */
    double processSample(const Parameters& params, double extPhase = 0.0) noexcept
    {
        if (!holdingAmpForFakeSustain && !ampEnv.isActive())
            return 0.0;

        const double eMod = static_cast<double>(modEnv.getNextSample());
        double eAmp = 0.0;
        if (holdingAmpForFakeSustain)
        {
            eAmp = heldAmpEnvValue;
        }
        else
        {
            eAmp = static_cast<double>(ampEnv.getNextSample());
            heldAmpEnvValue = eAmp;
        }
        lastModEnvValue = eMod;

        // Legacy 0-9 CTK-1000 Tone Editor Macro Lookup Table
        static constexpr std::array<double, 10> kLegacyBaseIndex = {
            0.0, 0.35, 0.75, 1.20, 1.75, 2.35, 3.00, 3.80, 4.65, 5.60
        };
        static constexpr std::array<double, 10> kLegacyEnvDepth = {
            0.0, 0.25, 0.50, 0.85, 1.25, 1.75, 2.25, 2.85, 3.50, 4.25
        };

        double n0 = params.baseModIndex;
        double nEnv = params.envModDepth;
        if (params.legacyMacroOverride)
        {
            const int step = std::clamp(params.legacyMacroStep, 0, 9);
            n0 = kLegacyBaseIndex[static_cast<size_t>(step)];
            nEnv = kLegacyEnvDepth[static_cast<size_t>(step)];
        }

        // Continuous Engineer-Level Modulation Index n[k]
        const double modIndex = std::max(0.0, n0 + (nEnv + params.velModDepth * velocityCurve) * eMod);

        // Effective sample rate handling for 1993 DAC Mode (22,050 Hz internal hold)
        const bool loFi1993 = params.dac1993Mode;
        const bool useADAA = params.adaaEnabled && !loFi1993;

        if (loFi1993 && sampleRate > 22050.0)
        {
            const double dacRatio = 22050.0 / sampleRate;
            dacPhase += dacRatio;
            if (dacPhase >= 1.0 || firstSample)
            {
                if (!firstSample)
                    dacPhase -= 1.0;

                heldSample = computeCoreSample(params, modIndex, extPhase, 22050.0, false);
                firstSample = false;
            }
            return heldSample * eAmp;
        }

        const double rawOut = computeCoreSample(params, modIndex, extPhase, sampleRate, useADAA);
        firstSample = false;
        return rawOut * eAmp;
    }

private:
    double computeCoreSample(const Parameters& params,
                             double modIndex,
                             double extPhase,
                             double effectiveSr,
                             bool useADAA) noexcept
    {
        // Advance normalized modulo-1.0 phase accumulators
        const double fcOverFs = carrierFreqHz / effectiveSr;
        const double fmHz = std::max(0.0, params.coarseRatio * carrierFreqHz + params.fineDetuneHz);
        const double fmOverFs = fmHz / effectiveSr;

        phiC = positiveMod(phiC + fcOverFs, 1.0);
        phiM = positiveMod(phiM + fmOverFs, 1.0);

        // Pre-distorted monotonous carrier phase W_alpha(phi_c)
        const double wAlpha = computeWarpedPhase(phiC, params.carrierWarp);

        // Composite phase argument x[k]
        const double beta = std::clamp(params.feedback, 0.0, 0.5);
        const double x = wAlpha
                       + modIndex * std::sin(juce::MathConstants<double>::twoPi * phiM)
                       + extPhase
                       + beta * prevY;

        double y = 0.0;
        if (useADAA)
        {
            const double f1 = evaluateAntiderivativeF1(x);
            if (firstSample)
            {
                y = evaluateWavefolder(x);
            }
            else
            {
                // Unwrap phase jump across modulo-4.0 boundary so ADAA finite difference is spike-free
                double dx = x - prevX;
                // Since F1(x) is strictly periodic with period 4.0 and continuous everywhere,
                // we evaluate difference directly unless |dx| is tiny or spans multiple periods
                if (std::abs(dx) < 1.0e-7)
                {
                    y = evaluateWavefolder(0.5 * (x + prevX));
                }
                else
                {
                    // Wrap x relative to prevX within [-2, +2) when computing antiderivative delta
                    // if needed, or integrate directly over the principal period:
                    const double uCurr = positiveMod(x + 1.0, 4.0);
                    const double uPrev = positiveMod(prevX + 1.0, 4.0);
                    double du = uCurr - uPrev;
                    if (du > 2.0)
                        du -= 4.0;
                    else if (du < -2.0)
                        du += 4.0;

                    if (std::abs(du) < 1.0e-7)
                    {
                        y = evaluateWavefolder(0.5 * (x + prevX));
                    }
                    else
                    {
                        y = std::clamp((f1 - prevF1) / du, -1.0, 1.0);
                    }
                }
            }
            prevX = x;
            prevF1 = f1;
        }
        else
        {
            y = evaluateWavefolder(x);
            prevX = x;
        }

        if (params.dac1993Mode)
        {
            // 16-bit integer quantization characteristic of NEC uPD939GD / 1993 DAC path
            y = std::round(y * 32767.0) / 32767.0;
        }

        prevY = y;
        return y;
    }

    double sampleRate = 44100.0;
    double carrierFreqHz = 440.0;
    double normalizedVelocity = 1.0;
    double velocityCurve = 1.0;

    double phiC = 0.0;
    double phiM = 0.0;
    double prevX = 0.0;
    double prevF1 = 0.0;
    double prevY = 0.0;
    double lastModEnvValue = 0.0;
    bool firstSample = true;

    double dacPhase = 0.0;
    double heldSample = 0.0;
    bool holdingAmpForFakeSustain = false;
    double heldAmpEnvValue = 0.0;

    juce::ADSR modEnv;
    juce::ADSR ampEnv;
};

} // namespace CasioIXA
