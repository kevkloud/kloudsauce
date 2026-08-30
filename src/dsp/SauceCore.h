#pragma once

#include "Lfo.h"
#include "Modules.h"

#include <array>
#include <atomic>
#include <vector>

namespace kloudsauce
{

/** The whole audio path. No JUCE, no allocation after `prepare`.

    Chain order is fixed: filter, drive, crush, tremolo, width. Fixed because
    the order that sounds right is not a decision worth spending a control on --
    filtering before saturation is what keeps the drive from turning every
    throw into the same broadband fizz, and the volume shape belongs after the
    things that change the timbre so it stays a rhythm rather than a texture.
*/
class SauceCore
{
public:
    struct Params
    {
        float amount = 1.0f;      // 0..1, global depth scaler
        float mix    = 1.0f;      // 0..1 dry/wet
        float trim   = 1.0f;      // linear output gain
        bool  bypass = false;

        double beatsPerCycle = 1.0;

        bool  filterOn   = false;
        float filterAmt  = 0.0f;
        float filterTone = 0.5f;
        int   filterMode = 0;

        bool  driveOn    = false;
        float driveAmt   = 0.0f;
        float driveTone  = 0.5f;

        bool  crushOn    = false;
        float crushAmt   = 0.0f;
        float crushTone  = 0.5f;

        bool  tremOn     = false;
        float tremAmt    = 0.0f;
        int   tremShape  = 0;

        bool  widthOn    = false;
        float widthAmt   = 0.5f;
    };

    /** What the host knows about where we are. */
    struct Transport
    {
        double bpm     = 120.0;
        double ppq     = 0.0;
        bool   playing = false;
    };

    void prepare (double sampleRate, int maximumBlockSize, int numChannels);
    void reset();

    void setParams (const Params& p) noexcept { params = p; }

    /** In-place, non-interleaved. `data` must have `numChannels` pointers. */
    void process (float* const* data, int numChannels, int numSamples, const Transport&);

    /** The current LFO position, for the panel. Read off the message thread;
        written by the audio thread. */
    float getLfoPhase() const noexcept { return lfoPhase.load (std::memory_order_relaxed); }

    /** Post-chain modulation depth actually applied, 0..1 -- what the movement
        indicator draws. */
    float getModulationDepth() const noexcept { return modDepth.load (std::memory_order_relaxed); }

private:
    static float toneToHz (float normalised) noexcept
    {
        // 60 Hz to 12 kHz, logarithmic. The ear reads filter position that way
        // and so does every filter control anyone has used.
        return 60.0f * std::pow (200.0f, std::clamp (normalised, 0.0f, 1.0f));
    }

    Params params;

    double sampleRate = 44100.0;
    int    channels   = 2;

    std::array<dsp::Svf, 2>     filters;
    std::array<dsp::Crusher, 2> crushers;
    std::array<dsp::OnePole, 2> crushTone;

    dsp::Lfo lfo;

    dsp::Smoother amountSmooth, mixSmooth, trimSmooth;
    dsp::Smoother filterSmooth, driveSmooth, crushSmooth, tremSmooth, widthSmooth;

    std::vector<float> dryLeft, dryRight;

    std::atomic<float> lfoPhase { 0.0f };
    std::atomic<float> modDepth { 0.0f };
};

} // namespace kloudsauce
