/** Offline profiler for the dice.

    Drives the real generator and the real chain with no host and no framework
    involved, and prints the numbers the README quotes. A random effect is only
    trustworthy if you can see the shape of what it produces, and you cannot do
    that by pressing a button in a DAW -- so it is measured here instead.

        measure characters   what each character tends to switch on, and how hard
        measure spread       how different two neighbouring seeds are
        measure headroom     the loudest a throw gets, per character
        measure profile      all of the above
*/

#include "dsp/Generator.h"
#include "dsp/SauceCore.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace kloudsauce;

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr int    kBlockSize  = 512;
    constexpr int    kSeeds      = 4096;

    std::vector<std::vector<float>> makeLoop (int numSamples)
    {
        std::vector<std::vector<float>> out (2, std::vector<float> ((size_t) numSamples, 0.0f));

        std::mt19937 rng { 20260830u };
        std::uniform_real_distribution<float> noise { -1.0f, 1.0f };

        const auto hitEvery = (int) (kSampleRate * 0.25);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto t = (double) i / kSampleRate;
            auto v = 0.25f * (float) std::sin (2.0 * 3.14159265358979323846 * 110.0 * t);

            v += 0.5f * noise (rng) * std::exp (-(float) (i % hitEvery) / (kSampleRate * 0.01f));

            out[0][(size_t) i] = v;
            out[1][(size_t) i] = v * 0.85f;
        }

        return out;
    }

    SauceCore::Params toParams (const Recipe& r)
    {
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

        return p;
    }

    void characters()
    {
        std::cout << "\nWhat each character switches on, over " << kSeeds << " seeds\n"
                  << "  character   filter  drive  crush  volume  width   mean depth  modules\n";

        for (int c = 0; c < (int) Character::NumCharacters; ++c)
        {
            double onFilter = 0, onDrive = 0, onCrush = 0, onTrem = 0, onWidth = 0;
            double depth = 0, modules = 0;

            for (std::uint32_t seed = 0; seed < kSeeds; ++seed)
            {
                const auto r = generate (seed, (Character) c, 1.0f);

                onFilter += r.filterOn; onDrive += r.driveOn; onCrush += r.crushOn;
                onTrem   += r.tremOn;   onWidth += r.widthOn;

                depth   += (r.filterAmt + r.driveAmt + r.crushAmt + r.tremAmt) / 4.0;
                modules += r.activeModules();
            }

            const auto pct = [] (double n) { return 100.0 * n / (double) kSeeds; };

            std::cout << "  " << std::left << std::setw (12) << characterName ((Character) c)
                      << std::right << std::fixed << std::setprecision (0)
                      << std::setw (5) << pct (onFilter) << "%"
                      << std::setw (6) << pct (onDrive)  << "%"
                      << std::setw (6) << pct (onCrush)  << "%"
                      << std::setw (6) << pct (onTrem)   << "%"
                      << std::setw (7) << pct (onWidth)  << "%"
                      << std::setprecision (2)
                      << std::setw (12) << depth / kSeeds
                      << std::setw (9) << modules / kSeeds << '\n';
        }
    }

    void spread()
    {
        std::cout << "\nHow far apart two neighbouring seeds land (0 = identical, 1 = opposite)\n";

        for (int c = 0; c < (int) Character::NumCharacters; ++c)
        {
            double distance = 0.0;

            for (std::uint32_t seed = 0; seed < kSeeds; ++seed)
            {
                const auto a = generate (seed, (Character) c, 1.0f);
                const auto b = generate (seed + 1, (Character) c, 1.0f);

                distance += (std::abs (a.filterTone - b.filterTone)
                           + std::abs (a.filterAmt  - b.filterAmt)
                           + std::abs (a.crushAmt   - b.crushAmt)
                           + std::abs (a.tremAmt    - b.tremAmt)) / 4.0;
            }

            std::cout << "  " << std::left << std::setw (12) << characterName ((Character) c)
                      << std::right << std::fixed << std::setprecision (3)
                      << distance / kSeeds << '\n';
        }
    }

    void headroom()
    {
        std::cout << "\nLoudest throw per character, relative to the source (dB)\n";

        const auto source = makeLoop ((int) (kSampleRate * 0.5));

        double sourcePeak = 0.0;

        for (const auto& ch : source)
            for (auto v : ch)
                sourcePeak = std::max (sourcePeak, (double) std::abs (v));

        SauceCore core;
        core.prepare (kSampleRate, kBlockSize, 2);

        for (int c = 0; c < (int) Character::NumCharacters; ++c)
        {
            double worst = 0.0;
            std::uint32_t worstSeed = 0;

            for (std::uint32_t seed = 0; seed < 512; ++seed)
            {
                auto buffers = source;

                core.setParams (toParams (generate (seed, (Character) c, 1.0f)));
                core.reset();

                SauceCore::Transport transport;
                transport.bpm = 120.0;
                transport.playing = true;

                const auto numSamples = (int) buffers[0].size();

                for (int start = 0; start < numSamples; start += kBlockSize)
                {
                    const auto n = std::min (kBlockSize, numSamples - start);
                    std::vector<float*> p { buffers[0].data() + start, buffers[1].data() + start };

                    transport.ppq = 2.0 / kSampleRate * start;
                    core.process (p.data(), 2, n, transport);
                }

                double peak = 0.0;

                for (const auto& ch : buffers)
                    for (auto v : ch)
                        peak = std::max (peak, (double) std::abs (v));

                if (peak > worst) { worst = peak; worstSeed = seed; }
            }

            std::cout << "  " << std::left << std::setw (12) << characterName ((Character) c)
                      << std::right << std::fixed << std::setprecision (1)
                      << std::setw (7) << 20.0 * std::log10 (std::max (worst, 1.0e-12) / sourcePeak)
                      << "   (seed " << worstSeed << ")\n";
        }
    }
}

int main (int argc, char** argv)
{
    const std::string what = argc > 1 ? argv[1] : "profile";

    std::cout << "KloudSauce -- offline profile\n";

    if (what == "characters" || what == "profile") characters();
    if (what == "spread"     || what == "profile") spread();
    if (what == "headroom"   || what == "profile") headroom();

    std::cout << '\n';
    return 0;
}
