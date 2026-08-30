#pragma once

#include <cstdint>

namespace kloudsauce
{

/** The characters the dice can throw in.

    A character is not a preset -- it is a set of distributions. Two throws of
    the same character give you two different sounds that belong to the same
    family, which is the behaviour you want when you are looking for a direction
    rather than a sound.

    Index order is a permanent part of the parameter schema.
*/
enum class Character
{
    Subtle = 0,   // safe -- shallow depths, gentle rates, always usable
    Bounce,       // rhythmic -- volume and filter locked to the grid
    Warped,       // wobbly -- deep, slow filter and pitchy width movement
    Hazy,         // washed -- dark, soft, low-passed, low contrast
    Broken,       // destroyed -- crush and drive foregrounded, fast rates
    Wildcard,     // anything, uniform across the whole space
    NumCharacters
};

/** A complete throw of the dice: every value the generator is allowed to move.

    Deliberately not a `Params` -- the generator produces *normalised* values in
    0..1 (and small integer indices), which the processor writes back into the
    APVTS as parameter values. That is what makes a throw automatable, undoable
    and savable in the host: nothing here bypasses the parameter system.
*/
struct Recipe
{
    bool  filterOn   = false;
    float filterAmt  = 0.0f;   // 0..1 modulation depth
    float filterTone = 0.5f;   // 0..1 centre frequency, log-mapped downstream
    int   filterMode = 0;      // 0 = low-pass, 1 = band-pass, 2 = high-pass

    bool  driveOn    = false;
    float driveAmt   = 0.0f;
    float driveTone  = 0.5f;

    bool  crushOn    = false;
    float crushAmt   = 0.0f;
    float crushTone  = 0.5f;

    bool  tremOn     = false;
    float tremAmt    = 0.0f;
    int   tremShape  = 0;      // dsp::Lfo::Shape

    bool  widthOn    = false;
    float widthAmt   = 0.5f;   // 0.5 is untouched; below narrows, above widens

    int   rateIndex  = 7;      // index into dsp::kRateDivisions
    float mix        = 1.0f;   // 0..1

    /** How many of the five modules this throw switched on. Used by the tests
        and by the panel's "this throw is doing very little" hint. */
    int activeModules() const noexcept;
};

/** Throw the dice.

    Deterministic in `seed`: the same seed, character and amount always produce
    the same recipe, on every machine and every build. That is what lets the
    seed be a plain integer parameter -- the host saves a number, and the sound
    comes back. Storing twenty-odd randomised values in the state would work
    too, but then a throw could not be automated, shared as a number, or
    stepped through with an arrow key.

    @param amount  0..1 global intensity. At 0 every depth is 0 and the throw is
                   inaudible regardless of which modules it enabled, so the
                   Amount knob is a true "how much of this idea" control rather
                   than a re-roll.
*/
Recipe generate (std::uint32_t seed, Character character, float amount) noexcept;

/** Human-readable name, for the panel and the offline profiler. */
const char* characterName (Character) noexcept;

} // namespace kloudsauce
