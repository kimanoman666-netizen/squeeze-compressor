#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace sqz
{

enum class Mode { Clean = 0, Fat = 1, Smooth = 2 };

//==============================================================================
// Static curve (soft knee). Returns gain reduction in dB (>= 0).
inline float gainReduction (float xDb, float thrDb, float ratio, float kneeDb) noexcept
{
    if (ratio <= 1.0001f)
        return 0.0f;

    const float over = xDb - thrDb;

    if (2.0f * over < -kneeDb)
        return 0.0f;

    if (kneeDb > 0.0f && 2.0f * std::abs (over) <= kneeDb)
    {
        const float t = over + kneeDb * 0.5f;
        return (1.0f - 1.0f / ratio) * t * t / (2.0f * kneeDb);
    }

    return over * (1.0f - 1.0f / ratio);
}

//==============================================================================
// Character of each mode. "Punch" = left end of the PUNCH/GLUE knob, "Glue" = right end.
struct ModeSettings
{
    float maxRatio, thrRange, kneeDb;
    float attPunch, attGlue;          // ms, fast stage attack
    float relPunch, relGlue;          // ms, fast stage release
    float slowAtt;                    // ms, slow (program-dependent) stage attack
    float slowRelPunch, slowRelGlue;  // ms
    float slowWPunch, slowWGlue;      // weight of the slow stage
    bool  rms;
    float rmsMs;
    bool  optoRelease;                // release gets slower the harder it compresses
};

inline const ModeSettings& settingsFor (Mode m) noexcept
{
    //                                  ratio thrR knee  attP  attG  relP   relG  slowA  sRelP   sRelG  wP    wG    rms    rmsMs opto
    static const ModeSettings clean  {  6.0f, 36.f, 10.f, 18.f, 3.0f, 70.f, 250.f, 300.f,  600.f, 1500.f, 0.3f, 0.8f, true,   5.f, false };
    static const ModeSettings fat    { 12.0f, 40.f,  4.f, 10.f, 0.5f, 45.f, 180.f, 200.f,  400.f, 1000.f, 0.2f, 0.7f, false,  0.f, false };
    static const ModeSettings smooth {  4.0f, 40.f, 16.f, 15.f, 8.0f, 80.f, 200.f, 400.f, 1500.f, 4000.f, 0.5f, 0.9f, true,  20.f, true  };

    switch (m)
    {
        case Mode::Clean:  return clean;
        case Mode::Smooth: return smooth;
        case Mode::Fat:
        default:           return fat;
    }
}

//==============================================================================
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;

    void setHighpass (double fs, double freq, double q) noexcept
    {
        const double w0 = juce::MathConstants<double>::twoPi * freq / fs;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = (float) (((1.0 + cw) * 0.5) / a0);
        b1 = (float) (-(1.0 + cw) / a0);
        b2 = b0;
        a1 = (float) ((-2.0 * cw) / a0);
        a2 = (float) ((1.0 - alpha) / a0);
    }

    void reset() noexcept { z1 = z2 = 0.0f; }

    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

//==============================================================================
class Compressor
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& f : scHpf)
            f.setHighpass (fs, 90.0, 0.7071);   // sidechain HPF: kick / 808 don't pump everything
        agCoef = coef (3000.0f);
        mkCoef = coef (80.0f);
        reset();
    }

    void reset()
    {
        for (auto& f : scHpf) f.reset();
        ms = 0.0f; envFast = envSlow = 0.0f; inPow = outPow = 0.0f; lossDb = 0.0f; makeup = 0.0f;
    }

    void setParameters (float squeeze01, float punchGlue01, Mode m, bool autoGainOn)
    {
        const auto& st = settingsFor (m);
        const float s = juce::jlimit (0.0f, 1.0f, squeeze01);
        const float p = juce::jlimit (0.0f, 1.0f, punchGlue01);

        thr   = -st.thrRange * s;
        ratio = 1.0f + (st.maxRatio - 1.0f) * std::pow (s, 1.3f);
        knee  = st.kneeDb;

        attCoef     = coef (juce::jmap (p, st.attPunch, st.attGlue));
        relMs       = juce::jmap (p, st.relPunch, st.relGlue);
        relCoef     = coef (relMs);
        slowAttCoef = coef (st.slowAtt);
        slowRelCoef = coef (juce::jmap (p, st.slowRelPunch, st.slowRelGlue));
        slowW       = juce::jmap (p, st.slowWPunch, st.slowWGlue);
        useRms      = st.rms;
        rmsCoef     = st.rms ? coef (st.rmsMs) : 0.0f;
        opto        = st.optoRelease;
        autoGain    = autoGainOn;
    }

    // Applies gain reduction in place. Reports the block's max GR and max detector level.
    void process (juce::AudioBuffer<float>& buffer, int numCh, float& grMaxOut, float& detMaxDbOut) noexcept
    {
        const int n = buffer.getNumSamples();
        auto* const* data = buffer.getArrayOfWritePointers();
        numCh = juce::jmin (numCh, 2);

        float grMax = 0.0f, detMax = -120.0f;
        float optoRelCoef = relCoef;

        for (int i = 0; i < n; ++i)
        {
            // --- detector (stereo-linked, high-passed)
            float sq = 0.0f, raw = 0.0f;
            for (int c = 0; c < numCh; ++c)
            {
                const float x = data[c][i];
                const float s = scHpf[(size_t) c].process (x);
                sq  = std::max (sq, s * s);
                raw = std::max (raw, x * x);
            }

            float power = sq;
            if (useRms)
            {
                ms = rmsCoef * ms + (1.0f - rmsCoef) * sq;
                power = ms;
            }

            const float xDb = 10.0f * std::log10 (power + 1.0e-12f);
            const float target = gainReduction (xDb, thr, ratio, knee);

            // --- fast stage
            if (target > envFast)
            {
                envFast = attCoef * envFast + (1.0f - attCoef) * target;
            }
            else
            {
                if (opto)
                {
                    if ((i & 15) == 0)
                        optoRelCoef = coef (relMs * (1.0f + envFast * 0.08f));
                    envFast = optoRelCoef * envFast + (1.0f - optoRelCoef) * target;
                }
                else
                {
                    envFast = relCoef * envFast + (1.0f - relCoef) * target;
                }
            }

            // --- slow, program-dependent stage (auto release)
            const float sc = target > envSlow ? slowAttCoef : slowRelCoef;
            envSlow = sc * envSlow + (1.0f - sc) * target;

            const float gr = std::max (envFast, envSlow * slowW);

            // --- honest auto gain: matches long-term loudness before / after compression
            const float gComp = std::pow (10.0f, -gr * 0.05f);
            if (raw > 1.0e-6f)   // only while there is signal (-60 dB)
            {
                inPow  = agCoef * inPow  + (1.0f - agCoef) * raw;
                outPow = agCoef * outPow + (1.0f - agCoef) * raw * gComp * gComp;
            }
            if ((i & 31) == 0)
                lossDb = juce::jlimit (0.0f, 24.0f, 10.0f * std::log10 ((inPow + 1.0e-12f) / (outPow + 1.0e-12f)));

            const float mkTarget = autoGain ? lossDb : 0.0f;
            makeup = mkCoef * makeup + (1.0f - mkCoef) * mkTarget;

            const float g = gComp * std::pow (10.0f, makeup * 0.05f);
            for (int c = 0; c < numCh; ++c)
                data[c][i] *= g;

            grMax  = std::max (grMax, gr);
            detMax = std::max (detMax, xDb);
        }

        grMaxOut = grMax;
        detMaxDbOut = detMax;
    }

    float getThreshold() const noexcept { return thr; }
    float getRatio() const noexcept     { return ratio; }
    float getKnee() const noexcept      { return knee; }

private:
    float coef (float timeMs) const noexcept
    {
        return (float) std::exp (-1.0 / (std::max (0.01, (double) timeMs) * 0.001 * fs));
    }

    double fs = 44100.0;
    std::array<Biquad, 2> scHpf;

    float thr = 0.0f, ratio = 1.0f, knee = 6.0f;
    float attCoef = 0.0f, relCoef = 0.0f, relMs = 100.0f;
    float slowAttCoef = 0.0f, slowRelCoef = 0.0f, slowW = 0.5f;
    float rmsCoef = 0.0f, agCoef = 0.0f, mkCoef = 0.0f;
    bool useRms = false, opto = false, autoGain = true;

    float ms = 0.0f, envFast = 0.0f, envSlow = 0.0f, makeup = 0.0f;
    float inPow = 0.0f, outPow = 0.0f, lossDb = 0.0f;
};

//==============================================================================
// Asymmetric tanh saturation. Small-signal gain is exactly 1, so it only colours peaks.
class Saturator
{
public:
    void setMode (Mode m, float squeeze01) noexcept
    {
        const float s = juce::jlimit (0.0f, 1.0f, squeeze01);
        switch (m)
        {
            case Mode::Fat:    active = true;  k = 0.30f + 0.50f * s; b = 0.15f; break;
            case Mode::Smooth: active = true;  k = 0.20f + 0.25f * s; b = 0.08f; break;
            case Mode::Clean:
            default:           active = false; k = 1.0f; b = 0.0f; break;
        }
        off  = std::tanh (k * b);
        norm = 1.0f / (k * (1.0f - off * off));
    }

    bool isActive() const noexcept { return active; }

    float process (float x) const noexcept
    {
        return (std::tanh (k * (x + b)) - off) * norm;
    }

private:
    bool active = false;
    float k = 1.0f, b = 0.0f, off = 0.0f, norm = 1.0f;
};

} // namespace sqz
