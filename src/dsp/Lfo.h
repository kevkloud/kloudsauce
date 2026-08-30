#pragma once

#include "Modules.h"

namespace kloudsauce::dsp
{

/** Tempo-locked LFO.

    Phase is derived from the host's PPQ position rather than accumulated
    locally whenever the transport is running. That is the difference between a
    modulation that lands on the loop's downbeat every time and one that drifts
    -- and drift is fatal here, because the entire plugin is aimed at material
    that repeats. When the transport is stopped it free-runs so the panel still
    moves while you audition.
*/
struct Lfo
{
    enum Shape { Sine = 0, Triangle, SawDown, Square, Random, NumShapes };

    void prepare (double sr) noexcept
    {
        sampleRate = sr;
        position = 0.0;
    }

    /** @param beatsPerCycle  4.0 is a bar of 4/4, 0.25 a sixteenth.
        @param ppq            host quarter-note position, ignored when stopped.
        @param playing        whether to lock to the host. */
    void advance (double beatsPerCycle, double bpm, double ppq, bool playing, int numSamples) noexcept
    {
        beatsPerCycle = std::max (0.015625, beatsPerCycle);

        if (playing)
        {
            position = ppq / beatsPerCycle;
        }
        else
        {
            const auto cyclesPerSecond = (bpm / 60.0) / beatsPerCycle;
            position += cyclesPerSecond * numSamples / sampleRate;
        }
    }

    /** Advance a single sample within the block. Called from the sample loop so
        the modulation is smooth rather than block-stepped; the block-level
        `advance` sets the anchor. */
    void tick (double beatsPerCycle, double bpm) noexcept
    {
        beatsPerCycle = std::max (0.015625, beatsPerCycle);

        position += ((bpm / 60.0) / beatsPerCycle) / sampleRate;
    }

    /** Unipolar, 0..1. Everything downstream wants a depth, not a swing. */
    float value (int shape) const noexcept
    {
        const auto p = (float) (position - std::floor (position));

        switch (shape)
        {
            case Sine:     return 0.5f - 0.5f * std::cos (2.0f * kPi * p);
            case Triangle: return p < 0.5f ? p * 2.0f : 2.0f - p * 2.0f;
            case SawDown:  return 1.0f - p;
            case Square:   return p < 0.5f ? 1.0f : 0.0f;

            // Eight held values per cycle, hashed from the absolute step number
            // rather than drawn from a running generator. That is what makes the
            // random shape usable on a loop at all: a running generator gives a
            // different sequence on every pass, so the bar you liked is gone the
            // moment you play it again, and the effect cannot be committed to a
            // render. Hashing the position instead means bar 5 of a loop sounds
            // the same every time you reach bar 5, while still not repeating
            // cycle to cycle.
            case Random:   return hash (std::floor (position * 8.0));

            default: break;
        }

        return 0.0f;
    }

    double getPhase() const noexcept { return position - std::floor (position); }

private:
    /** Deterministic across platforms and builds -- see Generator.cpp for why
        that matters more here than the quality of the distribution does. */
    static float hash (double step) noexcept
    {
        auto x = (std::uint64_t) (std::int64_t) step * 0x9E3779B97F4A7C15ull;

        x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ull;
        x ^= x >> 27; x *= 0x94D049BB133111EBull;
        x ^= x >> 31;

        return (float) (x >> 40) / (float) (1u << 24);
    }

    double sampleRate = 44100.0;

    // Continuous cycle count, not a wrapped phase: the random shape needs to
    // know *which* cycle it is in, and wrapping throws that away.
    double position = 0.0;
};

/** The sync divisions the Rate control offers, in quarter notes per LFO cycle.
    Index order is a permanent part of the parameter schema. */
inline constexpr double kRateDivisions[] =
{
    16.0,      // 4 bars
    8.0,       // 2 bars
    4.0,       // 1 bar
    3.0,       // 1/2 dotted
    2.0,       // 1/2
    4.0 / 3.0, // 1/2 triplet
    1.5,       // 1/4 dotted
    1.0,       // 1/4
    2.0 / 3.0, // 1/4 triplet
    0.75,      // 1/8 dotted
    0.5,       // 1/8
    1.0 / 3.0, // 1/8 triplet
    0.25,      // 1/16
    1.0 / 6.0, // 1/16 triplet
    0.125      // 1/32
};

inline constexpr int kNumRateDivisions = (int) (sizeof (kRateDivisions) / sizeof (kRateDivisions[0]));

} // namespace kloudsauce::dsp
