#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/Generator.h"
#include "dsp/SauceCore.h"
#include "params/ParameterLayout.h"

#include <array>
#include <atomic>

//==============================================================================
/** The host-facing wrapper.

    Everything that touches audio lives in kloudsauce::SauceCore, which has no
    JUCE dependency; this class exists to move parameter values into it, run the
    dice, and keep the editor supplied.
*/
class KloudSauceAudioProcessor final : public juce::AudioProcessor,
                                       private juce::AudioProcessorValueTreeState::Listener,
                                       private juce::AsyncUpdater
{
public:
    KloudSauceAudioProcessor();
    ~KloudSauceAudioProcessor() override;

    //== AudioProcessor ========================================================
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                          { return true; }

    const juce::String getName() const override              { return JucePlugin_Name; }
    bool acceptsMidi() const override                        { return false; }
    bool producesMidi() const override                       { return false; }
    bool isMidiEffect() const override                       { return false; }
    double getTailLengthSeconds() const override             { return 0.0; }

    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //== Ours ==================================================================
    juce::AudioProcessorValueTreeState& getApvts() noexcept  { return apvts; }

    /** Throw the dice: pick a new seed and write the resulting recipe into the
        generated parameters. Message thread only. */
    void roll();

    /** Re-apply the current seed and character. Called when either changes, and
        by the editor when you want the throw back after hand-editing it. */
    void applyCurrentSeed();

    /** For the panel's movement indicator. */
    float getLfoPhase() const noexcept        { return core.getLfoPhase(); }
    float getModulationDepth() const noexcept { return core.getModulationDepth(); }

    /** Metering, written by the audio thread and polled by the editor on a
        timer. Publish-and-sample; never push from audio to UI. */
    float getInputPeak  (int ch) const noexcept { return read (inputPeak,  ch); }
    float getOutputPeak (int ch) const noexcept { return read (outputPeak, ch); }

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;

    void applyRecipe (const kloudsauce::Recipe&);
    kloudsauce::SauceCore::Params currentParams() const noexcept;

    static float read (const std::array<std::atomic<float>, 2>& a, int ch) noexcept
    {
        return a[(size_t) juce::jlimit (0, 1, ch)].load (std::memory_order_relaxed);
    }

    juce::AudioProcessorValueTreeState apvts;

    // Resolved once in the constructor. Looking parameters up by string ID on
    // the audio thread would be a hash lookup per block.
    std::atomic<float>* amountParam     = nullptr;
    std::atomic<float>* mixParam        = nullptr;
    std::atomic<float>* trimParam       = nullptr;
    std::atomic<float>* bypassParam     = nullptr;
    std::atomic<float>* rateParam       = nullptr;
    std::atomic<float>* filterOnParam   = nullptr;
    std::atomic<float>* filterAmtParam  = nullptr;
    std::atomic<float>* filterToneParam = nullptr;
    std::atomic<float>* filterModeParam = nullptr;
    std::atomic<float>* driveOnParam    = nullptr;
    std::atomic<float>* driveAmtParam   = nullptr;
    std::atomic<float>* driveToneParam  = nullptr;
    std::atomic<float>* crushOnParam    = nullptr;
    std::atomic<float>* crushAmtParam   = nullptr;
    std::atomic<float>* crushToneParam  = nullptr;
    std::atomic<float>* tremOnParam     = nullptr;
    std::atomic<float>* tremAmtParam    = nullptr;
    std::atomic<float>* tremShapeParam  = nullptr;
    std::atomic<float>* widthOnParam    = nullptr;
    std::atomic<float>* widthAmtParam   = nullptr;

    kloudsauce::SauceCore core;

    // Set by parameterChanged when the seed or character moves; acted on from
    // the message thread, because writing parameters is not something to do
    // from whichever thread the host chose to notify us on.
    std::atomic<bool> seedNeedsApplying { false };

    // While a recipe is being written, the listener must not queue another
    // apply -- the generated parameters notify too.
    std::atomic<bool> applyingRecipe { false };

    std::array<std::atomic<float>, 2> inputPeak  { };
    std::array<std::atomic<float>, 2> outputPeak { };

    juce::Random seedSource;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KloudSauceAudioProcessor)
};
