#include "SauceCore.h"

namespace kloudsauce
{

using namespace kloudsauce::dsp;

void SauceCore::prepare (double sr, int maximumBlockSize, int numChannels)
{
    sampleRate = sr;
    channels   = std::max (1, numChannels);

    for (auto& f : filters)   f.prepare (sr);
    for (auto& c : crushTone) c.prepare (sr);

    lfo.prepare (sr);

    // 12 ms on the depths: long enough that a dice throw does not click, short
    // enough that it still feels like the sound changed when you pressed the
    // button rather than a moment afterwards.
    for (auto* s : { &amountSmooth, &filterSmooth, &driveSmooth,
                     &crushSmooth, &tremSmooth, &widthSmooth })
        s->setTimeConstant (12.0f, sr);

    // Mix and Trim are hand controls, and a slow smoother on Mix makes A/B
    // comparison feel mushy. 5 ms is below the click threshold and above the
    // point where it is audible as a lag.
    mixSmooth.setTimeConstant (5.0f, sr);
    trimSmooth.setTimeConstant (5.0f, sr);

    dryLeft.assign ((size_t) std::max (1, maximumBlockSize), 0.0f);
    dryRight.assign ((size_t) std::max (1, maximumBlockSize), 0.0f);

    reset();
}

void SauceCore::reset()
{
    for (auto& f : filters)   f.reset();
    for (auto& c : crushers)  c.reset();
    for (auto& c : crushTone) c.reset();

    amountSmooth.snapTo (params.amount);
    mixSmooth.snapTo (params.mix);
    trimSmooth.snapTo (params.trim);

    filterSmooth.snapTo (params.filterOn ? params.filterAmt : 0.0f);
    driveSmooth .snapTo (params.driveOn  ? params.driveAmt  : 0.0f);
    crushSmooth .snapTo (params.crushOn  ? params.crushAmt  : 0.0f);
    tremSmooth  .snapTo (params.tremOn   ? params.tremAmt   : 0.0f);
    widthSmooth .snapTo (params.widthOn  ? params.widthAmt  : 0.5f);
}

void SauceCore::process (float* const* data, int numChannels, int numSamples, const Transport& transport)
{
    if (numSamples <= 0 || numChannels <= 0)
        return;

    const auto p = params;

    // Bypass has to be exact, not approximately silent: the dry loop is the
    // reference you are judging every throw against, and a bypass that is a
    // fraction of a dB off is a bypass that lies to you.
    if (p.bypass)
    {
        lfoPhase.store ((float) lfo.getPhase(), std::memory_order_relaxed);
        modDepth.store (0.0f, std::memory_order_relaxed);
        return;
    }

    const auto stereo = numChannels >= 2;
    auto* left  = data[0];
    auto* right = stereo ? data[1] : data[0];

    const auto n = (size_t) std::min (numSamples, (int) dryLeft.size());

    for (size_t i = 0; i < n; ++i)
    {
        dryLeft[i]  = left[i];
        dryRight[i] = right[i];
    }

    lfo.advance (p.beatsPerCycle, transport.bpm, transport.ppq, transport.playing, numSamples);

    // Targets, resolved once per block. The per-sample smoothers do the rest.
    const auto filterTarget = p.filterOn ? p.filterAmt : 0.0f;
    const auto driveTarget  = p.driveOn  ? p.driveAmt  : 0.0f;
    const auto crushTarget  = p.crushOn  ? p.crushAmt  : 0.0f;
    const auto tremTarget   = p.tremOn   ? p.tremAmt   : 0.0f;
    const auto widthTarget  = p.widthOn  ? p.widthAmt  : 0.5f;

    const auto centreHz  = toneToHz (p.filterTone);
    const auto crushHz   = toneToHz (std::clamp (0.35f + p.crushTone * 0.65f, 0.0f, 1.0f));
    const auto driveBias = (p.driveTone - 0.5f) * 0.6f;

    float peakMod = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto amount = amountSmooth.process (p.amount);
        const auto mod    = lfo.value (p.tremOn || p.filterOn ? p.tremShape : 0);

        const auto filterAmt = filterSmooth.process (filterTarget) * amount;
        const auto driveAmt  = driveSmooth .process (driveTarget)  * amount;
        const auto crushAmt  = crushSmooth .process (crushTarget)  * amount;
        const auto tremAmt   = tremSmooth  .process (tremTarget)   * amount;
        const auto widthAmt  = widthSmooth .process (widthTarget);

        peakMod = std::max (peakMod, std::max ({ filterAmt, driveAmt, crushAmt, tremAmt }));

        //== Filter ============================================================
        if (filterAmt > 1.0e-4f)
        {
            // The modulation sweeps the cutoff over up to three octaves either
            // side of the centre; depth 0 leaves it parked and inaudible.
            const auto octaves = (mod - 0.5f) * 6.0f * filterAmt;
            const auto cutoff  = centreHz * std::pow (2.0f, octaves);

            for (int ch = 0; ch < (stereo ? 2 : 1); ++ch)
            {
                filters[(size_t) ch].setCutoff (cutoff);
                filters[(size_t) ch].setResonance (0.25f + 0.5f * filterAmt);

                auto* buf = ch == 0 ? left : right;
                const auto out = filters[(size_t) ch].process (buf[i]);

                const auto filtered = p.filterMode == 0 ? out.lp
                                    : p.filterMode == 1 ? out.bp
                                                        : out.hp;

                // Blended rather than switched, so the filter fades in with its
                // own depth instead of appearing at full strength.
                buf[i] = lerp (buf[i], filtered, std::min (1.0f, filterAmt * 1.5f));
            }
        }

        //== Drive =============================================================
        if (driveAmt > 1.0e-4f)
        {
            const auto gain = 1.0f + driveAmt * 24.0f;

            left[i] = lerp (left[i], Saturator::shape (left[i], gain, driveBias), driveAmt);

            if (stereo)
                right[i] = lerp (right[i], Saturator::shape (right[i], gain, driveBias), driveAmt);
        }

        //== Crush =============================================================
        if (crushAmt > 1.0e-4f)
        {
            const auto bits = lerp (16.0f, 3.0f, crushAmt);
            const auto hold = lerp (1.0f, 48.0f, crushAmt * crushAmt);

            for (int ch = 0; ch < (stereo ? 2 : 1); ++ch)
            {
                auto* buf = ch == 0 ? left : right;

                crushTone[(size_t) ch].setCutoff (crushHz);

                const auto crushed = crushTone[(size_t) ch].process (
                    crushers[(size_t) ch].process (buf[i], bits, hold));

                buf[i] = lerp (buf[i], crushed, crushAmt);
            }
        }

        //== Tremolo ===========================================================
        if (tremAmt > 1.0e-4f)
        {
            const auto gain = 1.0f - tremAmt * (1.0f - mod);

            left[i] *= gain;

            if (stereo)
                right[i] *= gain;
        }

        //== Width =============================================================
        if (stereo && std::abs (widthAmt - 0.5f) > 1.0e-4f)
        {
            // 0 -> mono, 0.5 -> untouched, 1 -> double the sides.
            Widener::process (left[i], right[i], widthAmt * 2.0f);
        }

        //== Mix and trim ======================================================
        const auto wet  = mixSmooth.process (p.mix);
        const auto trim = trimSmooth.process (p.trim);

        // Linear crossfade, not equal-power. The wet path here is a processed
        // version of the dry one, not an uncorrelated signal, so equal-power
        // would push the middle of the knob about 3 dB loud.
        if ((size_t) i < n)
        {
            left[i]  = lerp (dryLeft[(size_t) i],  left[i],  wet) * trim;

            if (stereo)
                right[i] = lerp (dryRight[(size_t) i], right[i], wet) * trim;
        }

        lfo.tick (p.beatsPerCycle, transport.bpm);
    }

    lfoPhase.store ((float) lfo.getPhase(), std::memory_order_relaxed);
    modDepth.store (std::clamp (peakMod, 0.0f, 1.0f), std::memory_order_relaxed);
}

} // namespace kloudsauce
