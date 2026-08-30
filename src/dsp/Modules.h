#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

/** Small, JUCE-free DSP primitives.

    Everything here is a struct with a `reset`, a setter or two and a per-sample
    `process`. No allocation, no branching on parameters inside the sample loop
    beyond what the effect itself needs, and no dependency on a framework -- the
    whole chain has to build and run in a bare container so CI can assert the
    claims in the README without an audio backend.
*/
namespace kloudsauce::dsp
{

inline constexpr float kPi = 3.14159265358979323846f;

/** Denormals cost more than the sample is worth once a loop stops. */
inline float flushDenormal (float x) noexcept
{
    return std::abs (x) < 1.0e-20f ? 0.0f : x;
}

inline float clamp01 (float x) noexcept { return std::clamp (x, 0.0f, 1.0f); }

inline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }

//==============================================================================
/** One-pole smoother, used for every parameter that reaches the sample loop.

    Loop mangling means large, sudden parameter jumps -- the dice lands and six
    values move at once. Without this, every throw is a click.
*/
struct Smoother
{
    void setTimeConstant (float milliseconds, double sampleRate) noexcept
    {
        const auto samples = std::max (1.0, milliseconds * 0.001 * sampleRate);
        coeff = (float) std::exp (-1.0 / samples);
    }

    void snapTo (float v) noexcept { current = v; }

    float process (float target) noexcept
    {
        current = target + coeff * (current - target);
        return current;
    }

    float value() const noexcept { return current; }

private:
    float coeff   = 0.0f;
    float current = 0.0f;
};

//==============================================================================
/** Topology-preserving state-variable filter (Zavalishin).

    One structure gives low-, band- and high-pass simultaneously and stays
    stable when the cutoff is swept at audio rate, which is the whole point:
    the filter here is modulated, not set.
*/
struct Svf
{
    void reset() noexcept { s1 = s2 = 0.0f; }

    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        reset();
        setCutoff (1000.0f);
        setResonance (0.5f);
    }

    void setCutoff (float hz) noexcept
    {
        const auto nyquist = (float) (sampleRate * 0.5);
        hz = std::clamp (hz, 20.0f, nyquist * 0.95f);
        g = std::tan (kPi * hz / (float) sampleRate);
    }

    /** 0 = heavily damped, 1 = just short of self-oscillation. */
    void setResonance (float r) noexcept
    {
        k = 2.0f - 1.94f * clamp01 (r);
    }

    struct Outputs { float lp, bp, hp; };

    Outputs process (float x) noexcept
    {
        const auto a1 = 1.0f / (1.0f + g * (g + k));
        const auto a2 = g * a1;
        const auto a3 = g * a2;

        const auto v3 = x - s2;
        const auto v1 = a1 * s1 + a2 * v3;
        const auto v2 = s2 + a2 * s1 + a3 * v3;

        s1 = flushDenormal (2.0f * v1 - s1);
        s2 = flushDenormal (2.0f * v2 - s2);

        return { v2, v1, x - k * v1 - v2 };
    }

private:
    double sampleRate = 44100.0;
    float g = 0.0f, k = 1.0f, s1 = 0.0f, s2 = 0.0f;
};

//==============================================================================
/** One-pole low-pass. Used for tone controls, not for anything that gets swept. */
struct OnePole
{
    void prepare (double sr) noexcept { sampleRate = sr; z = 0.0f; setCutoff (10000.0f); }
    void reset() noexcept { z = 0.0f; }

    void setCutoff (float hz) noexcept
    {
        const auto nyquist = (float) (sampleRate * 0.5);
        hz = std::clamp (hz, 10.0f, nyquist * 0.95f);
        a = 1.0f - std::exp (-2.0f * kPi * hz / (float) sampleRate);
    }

    float process (float x) noexcept
    {
        z = flushDenormal (z + a * (x - z));
        return z;
    }

private:
    double sampleRate = 44100.0;
    float a = 1.0f, z = 0.0f;
};

//==============================================================================
/** Asymmetric soft clipper.

    `drive` is a pre-gain in linear units; `bias` offsets the input before the
    curve, which is what puts even harmonics in and stops the saturation from
    sounding like a symmetrical fuzz. Output is normalised by the drive so
    turning it up thickens rather than simply gets louder -- the level match
    matters here because the whole plugin is auditioned against a dry loop.
*/
struct Saturator
{
    static float shape (float x, float drive, float bias) noexcept
    {
        const auto y = std::tanh ((x + bias) * drive) - std::tanh (bias * drive);

        // 1/tanh(drive) would blow up the quiet parts at low drive; normalising
        // by the drive itself keeps the curve continuous with unity at drive=1.
        return y / std::max (1.0f, std::sqrt (drive));
    }
};

//==============================================================================
/** Bit reduction plus sample-and-hold decimation.

    Both halves of the classic lo-fi pair, because either alone is recognisable
    as itself and the two together read as "different source material", which is
    what a loop needs.
*/
struct Crusher
{
    void reset() noexcept { held = 0.0f; phase = 0.0f; }

    /** `bits` may be fractional; `hold` is how many input samples each output
        sample lasts, and may also be fractional. */
    float process (float x, float bits, float hold) noexcept
    {
        phase += 1.0f;

        if (phase >= hold)
        {
            phase -= hold;

            const auto levels = std::pow (2.0f, std::clamp (bits, 1.0f, 24.0f)) * 0.5f;
            held = std::round (std::clamp (x, -1.5f, 1.5f) * levels) / levels;
        }

        return held;
    }

private:
    float held = 0.0f, phase = 0.0f;
};

//==============================================================================
/** Mid/side width. `width` of 1 is untouched, 0 is mono, 2 is doubled sides. */
struct Widener
{
    static void process (float& l, float& r, float width) noexcept
    {
        const auto mid  = (l + r) * 0.5f;
        const auto side = (l - r) * 0.5f * width;

        l = mid + side;
        r = mid - side;
    }
};

} // namespace kloudsauce::dsp
