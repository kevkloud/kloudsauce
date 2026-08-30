#include "dsp/Generator.h"
#include "dsp/SauceCore.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

using namespace kloudsauce;

namespace
{
    int failures = 0;
    constexpr double kSampleRate = 48000.0;
    constexpr int    kBlockSize  = 512;

    void check (bool ok, const std::string& what)
    {
        if (! ok) { std::cerr << "FAIL: " << what << '\n'; ++failures; }
    }

    void checkClose (double actual, double expected, double tol, const std::string& what)
    {
        if (! (std::abs (actual - expected) <= tol))
        {
            std::cerr << "FAIL: " << what << " -- expected " << expected
                      << " +/- " << tol << ", got " << actual << '\n';
            ++failures;
        }
    }

    //== Signal ================================================================

    /** A loop-ish test signal: a repeating two-bar pattern of transients over a
        sustained tone. Not musical, but it has the two things a loop has and a
        sine does not -- attacks the crusher and the drive can bite on, and a
        steady part where a filter sweep is audible. */
    std::vector<std::vector<float>> makeLoop (int numSamples, int numChannels = 2)
    {
        std::vector<std::vector<float>> out ((size_t) numChannels,
                                             std::vector<float> ((size_t) numSamples, 0.0f));

        std::mt19937 rng { 20260830u };
        std::uniform_real_distribution<float> noise { -1.0f, 1.0f };

        const auto hitEvery = (int) (kSampleRate * 0.25);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto t = (double) i / kSampleRate;
            auto v = 0.25f * (float) std::sin (2.0 * 3.14159265358979323846 * 110.0 * t);

            const auto sinceHit = i % hitEvery;
            v += 0.5f * noise (rng) * std::exp (-(float) sinceHit / (kSampleRate * 0.01f));

            out[0][(size_t) i] = v;

            if (numChannels > 1)
                out[1][(size_t) i] = v * 0.85f + 0.05f * noise (rng);
        }

        return out;
    }

    std::vector<float*> pointers (std::vector<std::vector<float>>& buffers)
    {
        std::vector<float*> p;

        for (auto& b : buffers)
            p.push_back (b.data());

        return p;
    }

    double peak (const std::vector<std::vector<float>>& buffers)
    {
        double m = 0.0;

        for (const auto& b : buffers)
            for (auto v : b)
                m = std::max (m, (double) std::abs (v));

        return m;
    }

    bool allFinite (const std::vector<std::vector<float>>& buffers)
    {
        for (const auto& b : buffers)
            for (auto v : b)
                if (! std::isfinite (v))
                    return false;

        return true;
    }

    /** Run the core over a whole signal, block by block, with a transport that
        advances the way a host's does. */
    void run (SauceCore& core, std::vector<std::vector<float>>& buffers, double bpm = 120.0,
              bool playing = true)
    {
        const auto numSamples  = (int) buffers[0].size();
        const auto numChannels = (int) buffers.size();
        const auto ppqPerSample = bpm / 60.0 / kSampleRate;

        SauceCore::Transport transport;
        transport.bpm = bpm;
        transport.playing = playing;

        for (int start = 0; start < numSamples; start += kBlockSize)
        {
            const auto n = std::min (kBlockSize, numSamples - start);

            std::vector<float*> p;

            for (auto& b : buffers)
                p.push_back (b.data() + start);

            transport.ppq = ppqPerSample * start;
            core.process (p.data(), numChannels, n, transport);
        }
    }

    SauceCore::Params fullyEngaged()
    {
        SauceCore::Params p;

        p.amount = 1.0f;
        p.mix    = 1.0f;
        p.trim   = 1.0f;

        p.filterOn = true;  p.filterAmt = 0.8f; p.filterTone = 0.5f; p.filterMode = 1;
        p.driveOn  = true;  p.driveAmt  = 0.7f; p.driveTone  = 0.7f;
        p.crushOn  = true;  p.crushAmt  = 0.6f; p.crushTone  = 0.5f;
        p.tremOn   = true;  p.tremAmt   = 0.9f; p.tremShape  = 4;
        p.widthOn  = true;  p.widthAmt  = 0.9f;

        p.beatsPerCycle = 0.5;

        return p;
    }
}

//==============================================================================
// The dice.
//==============================================================================

/** The seed is a saved parameter, so the mapping from a number to a sound has
    to be stable. If this ever fails, every project anyone has saved has changed
    what it sounds like. */
void testGeneratorIsDeterministic()
{
    for (std::uint32_t seed = 0; seed < 512; ++seed)
    {
        for (int c = 0; c < (int) Character::NumCharacters; ++c)
        {
            const auto a = generate (seed, (Character) c, 1.0f);
            const auto b = generate (seed, (Character) c, 1.0f);

            const auto same = a.filterOn == b.filterOn && a.filterAmt == b.filterAmt
                           && a.filterTone == b.filterTone && a.filterMode == b.filterMode
                           && a.driveOn == b.driveOn && a.driveAmt == b.driveAmt
                           && a.crushOn == b.crushOn && a.crushAmt == b.crushAmt
                           && a.tremOn == b.tremOn && a.tremAmt == b.tremAmt
                           && a.tremShape == b.tremShape
                           && a.widthOn == b.widthOn && a.widthAmt == b.widthAmt
                           && a.rateIndex == b.rateIndex && a.mix == b.mix;

            if (! same)
            {
                check (false, "generator is deterministic (seed " + std::to_string (seed) + ")");
                return;
            }
        }
    }

    check (true, "generator is deterministic");
}

/** Every value the dice writes goes straight into a parameter with a fixed
    range. Anything out of range would be silently clamped, which means a whole
    region of seeds would collapse onto the same sound. */
void testGeneratorStaysInRange()
{
    auto ok = true;

    for (std::uint32_t seed = 0; seed < 4096; ++seed)
    {
        for (int c = 0; c < (int) Character::NumCharacters; ++c)
        {
            const auto r = generate (seed, (Character) c, 1.0f);

            const auto inUnit = [] (float v) { return v >= 0.0f && v <= 1.0f; };

            ok = ok && inUnit (r.filterAmt) && inUnit (r.filterTone)
                    && inUnit (r.driveAmt)  && inUnit (r.driveTone)
                    && inUnit (r.crushAmt)  && inUnit (r.crushTone)
                    && inUnit (r.tremAmt)   && inUnit (r.widthAmt)
                    && inUnit (r.mix)
                    && r.filterMode >= 0 && r.filterMode < 3
                    && r.tremShape  >= 0 && r.tremShape  < 5
                    && r.rateIndex  >= 0 && r.rateIndex  < dsp::kNumRateDivisions;
        }
    }

    check (ok, "every generated value is inside its parameter's range");
}

/** A throw that switched nothing on is a wasted press of the button. */
void testEveryThrowDoesSomething()
{
    auto worst = 5;

    for (std::uint32_t seed = 0; seed < 4096; ++seed)
        for (int c = 0; c < (int) Character::NumCharacters; ++c)
            worst = std::min (worst, generate (seed, (Character) c, 1.0f).activeModules());

    check (worst >= 1, "every throw enables at least one module");
}

/** Amount has to be a continuous "how much of this idea", not a re-roll. At
    zero it must be inaudible whatever the throw decided to switch on. */
void testAmountZeroIsSilentIdea()
{
    auto ok = true;

    for (std::uint32_t seed = 0; seed < 1024; ++seed)
    {
        const auto r = generate (seed, Character::Broken, 0.0f);

        ok = ok && r.filterAmt == 0.0f && r.driveAmt == 0.0f
                && r.crushAmt == 0.0f && r.tremAmt == 0.0f
                && std::abs (r.widthAmt - 0.5f) < 1.0e-6f;
    }

    check (ok, "Amount at zero produces no depth on any module");
}

/** The characters have to actually differ, or the selector is decoration. */
void testCharactersHaveDifferentTaste()
{
    const auto meanDepth = [] (Character c)
    {
        double total = 0.0;

        for (std::uint32_t seed = 0; seed < 4096; ++seed)
        {
            const auto r = generate (seed, c, 1.0f);
            total += r.filterAmt + r.driveAmt + r.crushAmt + r.tremAmt;
        }

        return total / 4096.0;
    };

    const auto subtle = meanDepth (Character::Subtle);
    const auto broken = meanDepth (Character::Broken);

    check (broken > subtle * 1.5, "Broken throws harder than Subtle");

    const auto crushRate = [] (Character c)
    {
        int n = 0;

        for (std::uint32_t seed = 0; seed < 4096; ++seed)
            n += generate (seed, c, 1.0f).crushOn ? 1 : 0;

        return (double) n / 4096.0;
    };

    check (crushRate (Character::Broken) > 0.8,  "Broken almost always crushes");
    check (crushRate (Character::Subtle) < 0.25, "Subtle rarely crushes");
}

/** Two characters on one seed must be two ideas, not one idea twice. */
void testCharactersAreUncorrelatedOnOneSeed()
{
    int identical = 0;

    for (std::uint32_t seed = 0; seed < 2048; ++seed)
    {
        const auto a = generate (seed, Character::Subtle, 1.0f);
        const auto b = generate (seed, Character::Warped, 1.0f);

        if (a.filterMode == b.filterMode && a.rateIndex == b.rateIndex
            && std::abs (a.filterTone - b.filterTone) < 1.0e-6f)
            ++identical;
    }

    check (identical < 64, "changing character gives a different throw, not the same one restyled");
}

/** Neighbouring seeds must not be neighbouring sounds -- the seed knob is meant
    to browse ideas, and a smooth seed is a knob that does nothing for the first
    twenty clicks. */
void testAdjacentSeedsDiffer()
{
    double totalDistance = 0.0;

    for (std::uint32_t seed = 0; seed < 2048; ++seed)
    {
        const auto a = generate (seed, Character::Wildcard, 1.0f);
        const auto b = generate (seed + 1, Character::Wildcard, 1.0f);

        totalDistance += std::abs (a.filterTone - b.filterTone)
                       + std::abs (a.crushAmt  - b.crushAmt);
    }

    check (totalDistance / 2048.0 > 0.4, "adjacent seeds are unrelated sounds");
}

//==============================================================================
// The chain.
//==============================================================================

/** Bypass is the reference you judge every throw against. It has to be exact,
    not approximately silent. */
void testBypassIsBitIdentical()
{
    auto input = makeLoop ((int) kSampleRate);
    auto buffers = input;

    SauceCore core;
    auto p = fullyEngaged();
    p.bypass = true;

    core.setParams (p);
    core.prepare (kSampleRate, kBlockSize, 2);
    run (core, buffers);

    auto identical = true;

    for (size_t ch = 0; ch < buffers.size(); ++ch)
        for (size_t i = 0; i < buffers[ch].size(); ++i)
            identical = identical && buffers[ch][i] == input[ch][i];

    check (identical, "bypass is bit-identical to the input");
}

/** Mix at zero has to be the dry signal exactly, or A/B against the loop lies
    to you by whatever the chain's DC or level offset happens to be. */
void testMixZeroIsDry()
{
    auto input = makeLoop ((int) kSampleRate);
    auto buffers = input;

    SauceCore core;
    auto p = fullyEngaged();
    p.mix = 0.0f;

    core.setParams (p);
    core.prepare (kSampleRate, kBlockSize, 2);
    run (core, buffers);

    double worst = 0.0;

    for (size_t ch = 0; ch < buffers.size(); ++ch)
        for (size_t i = 0; i < buffers[ch].size(); ++i)
            worst = std::max (worst, (double) std::abs (buffers[ch][i] - input[ch][i]));

    // Not bitwise: Mix is smoothed, so the first few samples of the very first
    // block walk from the previous value. Below a 24-bit LSB is inaudible and
    // is what the smoother costs.
    check (worst < 1.0e-6, "Mix at zero returns the dry signal");
}

/** Amount at zero is the second reference: whatever the throw switched on, none
    of it should reach the output. */
void testAmountZeroIsDry()
{
    auto input = makeLoop ((int) kSampleRate);
    auto buffers = input;

    SauceCore core;
    auto p = fullyEngaged();
    p.amount = 0.0f;
    p.widthOn = false;   // Width is not depth-scaled; it has its own centre.

    core.setParams (p);
    core.prepare (kSampleRate, kBlockSize, 2);
    run (core, buffers);

    double worst = 0.0;

    for (size_t ch = 0; ch < buffers.size(); ++ch)
        for (size_t i = 0; i < buffers[ch].size(); ++i)
            worst = std::max (worst, (double) std::abs (buffers[ch][i] - input[ch][i]));

    check (worst < 1.0e-5, "Amount at zero passes the input through");
}

/** Silence in, silence out. A chain with a stuck sample-and-hold or a filter
    that self-oscillates would fail here and nowhere else. */
void testSilenceStaysSilent()
{
    std::vector<std::vector<float>> buffers (2, std::vector<float> ((size_t) kSampleRate, 0.0f));

    SauceCore core;
    core.setParams (fullyEngaged());
    core.prepare (kSampleRate, kBlockSize, 2);
    run (core, buffers);

    check (peak (buffers) < 1.0e-7, "silence in, silence out");
}

/** Every seed, at full amount, through the real chain. This is the test that
    would catch a character whose distribution can produce a filter that
    explodes -- and the user is going to press the button a thousand times. */
void testNoThrowBlowsUp()
{
    SauceCore core;
    core.prepare (kSampleRate, kBlockSize, 2);

    const auto source = makeLoop ((int) (kSampleRate * 0.5));

    double worstPeak = 0.0;
    auto finite = true;

    for (std::uint32_t seed = 0; seed < 256; ++seed)
    {
        for (int c = 0; c < (int) Character::NumCharacters; ++c)
        {
            const auto r = generate (seed, (Character) c, 1.0f);

            SauceCore::Params p;
            p.amount = 1.0f;
            p.mix    = r.mix;
            p.trim   = 1.0f;
            p.beatsPerCycle = dsp::kRateDivisions[r.rateIndex];
            p.filterOn = r.filterOn; p.filterAmt = r.filterAmt;
            p.filterTone = r.filterTone; p.filterMode = r.filterMode;
            p.driveOn = r.driveOn; p.driveAmt = r.driveAmt; p.driveTone = r.driveTone;
            p.crushOn = r.crushOn; p.crushAmt = r.crushAmt; p.crushTone = r.crushTone;
            p.tremOn = r.tremOn; p.tremAmt = r.tremAmt; p.tremShape = r.tremShape;
            p.widthOn = r.widthOn; p.widthAmt = r.widthAmt;

            auto buffers = source;

            core.setParams (p);
            core.reset();
            run (core, buffers);

            finite = finite && allFinite (buffers);
            worstPeak = std::max (worstPeak, peak (buffers));
        }
    }

    check (finite, "no throw produces a NaN or an infinity");

    // The input peaks at roughly 0.75. A throw is allowed to be louder than the
    // source -- drive and resonance both add level -- but not by an amount that
    // would blow a monitor when you are auditioning throws quickly.
    check (worstPeak < 4.0, "no throw exceeds +12 dB over the source");

    std::cout << "  worst peak across 256 seeds x " << (int) Character::NumCharacters
              << " characters: " << worstPeak << '\n';
}

/** The host decides the block size and can change it. The sound must not. */
void testBlockSizeIndependence()
{
    const auto source = makeLoop ((int) kSampleRate);

    const auto render = [&source] (int blockSize)
    {
        auto buffers = source;

        SauceCore core;
        core.setParams (fullyEngaged());
        core.prepare (kSampleRate, blockSize, 2);

        const auto ppqPerSample = 120.0 / 60.0 / kSampleRate;
        const auto numSamples = (int) buffers[0].size();

        SauceCore::Transport transport;
        transport.bpm = 120.0;
        transport.playing = true;

        for (int start = 0; start < numSamples; start += blockSize)
        {
            const auto n = std::min (blockSize, numSamples - start);

            std::vector<float*> p { buffers[0].data() + start, buffers[1].data() + start };

            transport.ppq = ppqPerSample * start;
            core.process (p.data(), 2, n, transport);
        }

        return buffers;
    };

    const auto a = render (64);
    const auto b = render (512);

    double worst = 0.0;

    for (size_t ch = 0; ch < a.size(); ++ch)
        for (size_t i = 0; i < a[ch].size(); ++i)
            worst = std::max (worst, (double) std::abs (a[ch][i] - b[ch][i]));

    // Not exact: the crusher's sample-and-hold and the smoothers both carry
    // state that a different block boundary re-anchors. What matters is that
    // the result is the same sound, not a different one.
    check (worst < 0.05, "block size does not change the sound");

    std::cout << "  worst sample difference between 64- and 512-sample blocks: " << worst << '\n';
}

/** The modulation is locked to the host's PPQ, so a given point in the timeline
    gets the same treatment however you arrived at it -- looping back round,
    punching in, or rendering the section on its own. That is the difference
    between an effect you can commit to a bounce and one that sounds different
    every time.

    Note that this is *not* the same claim as "every bar sounds identical": the
    random shape is deliberately different from cycle to cycle, or it would be a
    static pattern rather than a modulation. What has to hold is that bar 5 is
    always the same bar 5.
*/
void testModulationIsPositionLocked()
{
    SauceCore core;
    auto p = fullyEngaged();
    p.beatsPerCycle = 1.0;
    p.crushOn = false;   // sample-and-hold state carries across the two passes

    core.setParams (p);
    core.prepare (kSampleRate, kBlockSize, 2);

    const auto barSamples = (int) (kSampleRate * 2.0);   // 4 beats at 120 bpm
    const auto source = makeLoop (barSamples);

    const auto renderFromPpq = [&] (double startPpq)
    {
        auto buffers = source;

        core.reset();

        SauceCore::Transport transport;
        transport.bpm = 120.0;
        transport.playing = true;

        const auto ppqPerSample = 2.0 / kSampleRate;

        for (int start = 0; start < barSamples; start += kBlockSize)
        {
            const auto n = std::min (kBlockSize, barSamples - start);

            std::vector<float*> ptr { buffers[0].data() + start, buffers[1].data() + start };

            transport.ppq = startPpq + ppqPerSample * start;
            core.process (ptr.data(), 2, n, transport);
        }

        return buffers;
    };

    // The same bar of the timeline, played twice. Everything about the second
    // pass -- filter state, smoother state, the random sequence -- has to land
    // where it did the first time.
    const auto first  = renderFromPpq (4.0);
    const auto second = renderFromPpq (4.0);

    double worst = 0.0;

    for (size_t ch = 0; ch < first.size(); ++ch)
        for (size_t i = 0; i < first[ch].size(); ++i)
            worst = std::max (worst, (double) std::abs (first[ch][i] - second[ch][i]));

    check (worst == 0.0, "replaying the same bar reproduces it exactly");

    // And a shape without a random component has to repeat bar to bar as well,
    // because that is what "locked to the grid" means when you are looking at
    // the picture rather than at the timeline.
    p.tremShape = 0;               // sine
    core.setParams (p);

    const auto barOne = renderFromPpq (0.0);
    const auto barTwo = renderFromPpq (4.0);

    double periodic = 0.0;

    // Skipping the first 10 ms: the filter and the smoothers start from rest on
    // each pass, and that settling is not what this measures.
    const auto skip = (size_t) (kSampleRate * 0.01);

    for (size_t ch = 0; ch < barOne.size(); ++ch)
        for (size_t i = skip; i < barOne[ch].size(); ++i)
            periodic = std::max (periodic, (double) std::abs (barOne[ch][i] - barTwo[ch][i]));

    check (periodic < 1.0e-3, "a non-random shape treats every bar of a loop identically");

    std::cout << "  bar-to-bar difference with a sine shape: " << periodic << '\n';
}

/** Width at the centre is untouched, not "almost". */
void testWidthCentreIsNeutral()
{
    auto input = makeLoop ((int) (kSampleRate * 0.25));
    auto buffers = input;

    SauceCore core;

    SauceCore::Params p;
    p.widthOn = true;
    p.widthAmt = 0.5f;

    core.setParams (p);
    core.prepare (kSampleRate, kBlockSize, 2);
    run (core, buffers);

    double worst = 0.0;

    for (size_t ch = 0; ch < buffers.size(); ++ch)
        for (size_t i = 0; i < buffers[ch].size(); ++i)
            worst = std::max (worst, (double) std::abs (buffers[ch][i] - input[ch][i]));

    check (worst < 1.0e-6, "Width at 50 % leaves the stereo image alone");
}

/** Mono hosts exist and drum busses get summed. Nothing here may read a second
    channel that is not there. */
void testMonoIsSafe()
{
    auto buffers = makeLoop ((int) (kSampleRate * 0.25), 1);

    SauceCore core;
    core.setParams (fullyEngaged());
    core.prepare (kSampleRate, kBlockSize, 1);
    run (core, buffers);

    check (allFinite (buffers), "a mono channel layout produces finite output");
}

/** Trim is a plain gain and must be exactly that -- it is the control you use
    to level-match a throw against the dry loop before deciding. */
void testTrimIsExactGain()
{
    auto input = makeLoop ((int) (kSampleRate * 0.25));
    auto buffers = input;

    SauceCore core;

    SauceCore::Params p;
    p.mix  = 1.0f;
    p.trim = 0.5f;

    core.setParams (p);
    core.prepare (kSampleRate, kBlockSize, 2);
    run (core, buffers);

    double worst = 0.0;

    for (size_t ch = 0; ch < buffers.size(); ++ch)
        for (size_t i = 0; i < buffers[ch].size(); ++i)
            worst = std::max (worst, (double) std::abs (buffers[ch][i] - input[ch][i] * 0.5f));

    checkClose (worst, 0.0, 1.0e-5, "Trim is an exact gain");
}

//==============================================================================
int main()
{
    std::cout << "KloudSauce DSP tests\n";

    testGeneratorIsDeterministic();
    testGeneratorStaysInRange();
    testEveryThrowDoesSomething();
    testAmountZeroIsSilentIdea();
    testCharactersHaveDifferentTaste();
    testCharactersAreUncorrelatedOnOneSeed();
    testAdjacentSeedsDiffer();

    testBypassIsBitIdentical();
    testMixZeroIsDry();
    testAmountZeroIsDry();
    testSilenceStaysSilent();
    testNoThrowBlowsUp();
    testBlockSizeIndependence();
    testModulationIsPositionLocked();
    testWidthCentreIsNeutral();
    testMonoIsSafe();
    testTrimIsExactGain();

    if (failures == 0)
        std::cout << "all tests passed\n";
    else
        std::cout << failures << " test(s) failed\n";

    return failures == 0 ? 0 : 1;
}
