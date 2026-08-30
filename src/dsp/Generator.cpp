#include "Generator.h"

#include <algorithm>
#include <cmath>

namespace kloudsauce
{

namespace
{
    /** splitmix64. Chosen over the platform's RNG for one reason: the seed is a
        user-visible parameter saved in the host's project file, so the mapping
        from seed to sound has to be identical on macOS, Windows, and whatever
        the CI container is running -- forever. std::mt19937 would do, but its
        distribution objects are not specified to produce the same sequence
        across standard libraries, which is exactly the guarantee we need. */
    struct Rng
    {
        explicit Rng (std::uint64_t s) noexcept : state (s + 0x9E3779B97F4A7C15ull) {}

        std::uint64_t next() noexcept
        {
            auto z = (state += 0x9E3779B97F4A7C15ull);
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }

        /** Uniform in [0, 1). */
        float uniform() noexcept
        {
            return (float) (next() >> 40) / (float) (1u << 24);
        }

        float range (float lo, float hi) noexcept { return lo + (hi - lo) * uniform(); }

        bool chance (float p) noexcept { return uniform() < p; }

        int index (int count) noexcept
        {
            return count <= 1 ? 0 : (int) (next() % (std::uint64_t) count);
        }

        /** Biased towards the low end when `shape` > 1, towards the high end
            when < 1. Most musical parameters want more of their resolution
            near "a little" than near "all the way". */
        float skewed (float shape) noexcept { return std::pow (uniform(), shape); }

    private:
        std::uint64_t state;
    };

    /** Per-character distribution. Everything the dice knows about taste lives
        in this table rather than in branching code, so adding a character is
        adding a row. */
    struct Profile
    {
        float onFilter, onDrive, onCrush, onTrem, onWidth;
        float depthLo,  depthHi;   // module depth range before Amount scales it
        float depthSkew;           // >1 pulls depths down
        int   rateLo,   rateHi;    // inclusive indices into kRateDivisions
        float toneLo,   toneHi;    // filter centre, normalised
        float mixLo,    mixHi;
        bool  allowHarshShapes;    // square / random LFO, high-pass filter
    };

    // Indexed by Character. Order must match the enum.
    constexpr Profile kProfiles[(int) Character::NumCharacters] =
    {
        // on:  flt   drv   crs   trm   wid | depth lo/hi/skew | rate lo/hi | tone lo/hi | mix lo/hi | harsh
        /* Subtle   */ { 0.70f, 0.45f, 0.10f, 0.35f, 0.50f,  0.10f, 0.40f, 1.60f,   2,  8,  0.35f, 0.85f, 0.55f, 1.00f, false },
        /* Bounce   */ { 0.75f, 0.40f, 0.20f, 0.95f, 0.35f,  0.35f, 0.85f, 0.90f,   9, 13,  0.25f, 0.75f, 0.70f, 1.00f, true  },
        /* Warped   */ { 0.95f, 0.55f, 0.25f, 0.45f, 0.85f,  0.45f, 0.95f, 0.80f,   0,  7,  0.15f, 0.70f, 0.65f, 1.00f, false },
        /* Hazy     */ { 0.90f, 0.35f, 0.35f, 0.30f, 0.60f,  0.20f, 0.60f, 1.30f,   0,  6,  0.05f, 0.45f, 0.45f, 0.90f, false },
        /* Broken   */ { 0.60f, 0.90f, 0.95f, 0.60f, 0.40f,  0.55f, 1.00f, 0.70f,   8, 14,  0.20f, 0.90f, 0.75f, 1.00f, true  },
        /* Wildcard */ { 0.60f, 0.60f, 0.60f, 0.60f, 0.60f,  0.00f, 1.00f, 1.00f,   0, 14,  0.00f, 1.00f, 0.30f, 1.00f, true  },
    };

    constexpr const char* kNames[(int) Character::NumCharacters] =
        { "Subtle", "Bounce", "Warped", "Hazy", "Broken", "Wildcard" };
}

int Recipe::activeModules() const noexcept
{
    return (filterOn ? 1 : 0) + (driveOn ? 1 : 0) + (crushOn ? 1 : 0)
         + (tremOn ? 1 : 0) + (widthOn ? 1 : 0);
}

const char* characterName (Character c) noexcept
{
    const auto i = (int) c;

    return (i >= 0 && i < (int) Character::NumCharacters) ? kNames[i] : "?";
}

Recipe generate (std::uint32_t seed, Character character, float amount) noexcept
{
    const auto index = std::clamp ((int) character, 0, (int) Character::NumCharacters - 1);
    const auto& p = kProfiles[index];

    amount = std::clamp (amount, 0.0f, 1.0f);

    // The character is folded into the seed rather than selecting a different
    // stream, so seed 42 Subtle and seed 42 Broken are unrelated sounds. If the
    // character only changed the distribution and not the stream, stepping
    // through characters on one seed would give you five versions of the same
    // throw, which reads as the plugin having fewer ideas than it has.
    Rng rng { (std::uint64_t) seed * 0x100000001B3ull + (std::uint64_t) index };

    Recipe r;

    const auto depth = [&]
    {
        const auto d = p.depthLo + (p.depthHi - p.depthLo) * rng.skewed (p.depthSkew);

        // Amount scales depth rather than gating it, so sweeping Amount from 0
        // walks continuously into the throw instead of switching it on.
        return d * amount;
    };

    r.filterOn   = rng.chance (p.onFilter);
    r.filterAmt  = depth();
    r.filterTone = rng.range (p.toneLo, p.toneHi);
    r.filterMode = p.allowHarshShapes ? rng.index (3) : rng.index (2);

    r.driveOn    = rng.chance (p.onDrive);
    r.driveAmt   = depth();
    r.driveTone  = rng.uniform();

    r.crushOn    = rng.chance (p.onCrush);
    r.crushAmt   = depth();
    r.crushTone  = rng.range (0.2f, 1.0f);

    r.tremOn     = rng.chance (p.onTrem);
    r.tremAmt    = depth();
    r.tremShape  = p.allowHarshShapes ? rng.index (5) : rng.index (3);

    r.widthOn    = rng.chance (p.onWidth);

    // Width is bipolar around 0.5, and Amount pulls it back towards untouched
    // for the same reason it scales the depths.
    r.widthAmt   = 0.5f + (rng.uniform() - 0.5f) * amount;

    r.rateIndex  = p.rateLo + rng.index (p.rateHi - p.rateLo + 1);
    r.mix        = rng.range (p.mixLo, p.mixHi);

    // A throw that enabled nothing is a wasted press of the button. Rather than
    // re-rolling until something lands -- which would bias the distribution in
    // a way that is hard to reason about -- switch on the one module the
    // character most wants.
    if (r.activeModules() == 0)
    {
        if      (index == (int) Character::Broken) r.crushOn  = true;
        else if (index == (int) Character::Bounce) r.tremOn   = true;
        else                                       r.filterOn = true;
    }

    return r;
}

} // namespace kloudsauce
