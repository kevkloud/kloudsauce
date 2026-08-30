#include "ParameterLayout.h"

#include "../dsp/Generator.h"
#include "../dsp/Lfo.h"

namespace kloudsauce::params
{

const juce::StringArray kGeneratedParameters
{
    kRate,
    kFilterOn, kFilterAmt, kFilterTone, kFilterMode,
    kDriveOn,  kDriveAmt,  kDriveTone,
    kCrushOn,  kCrushAmt,  kCrushTone,
    kTremOn,   kTremAmt,   kTremShape,
    kWidthOn,  kWidthAmt
};

namespace
{
    using Attributes = juce::AudioParameterFloatAttributes;

    juce::String percent (float v, int)
    {
        return juce::String (juce::roundToInt (v));
    }

    std::unique_ptr<juce::AudioParameterFloat> depth (const char* id, const juce::String& name,
                                                      float defaultPercent)
    {
        return std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, kVersionHint }, name,
            juce::NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, defaultPercent,
            Attributes {}.withLabel ("%").withStringFromValueFunction (percent));
    }

    juce::StringArray rateNames()
    {
        // Must stay in the same order as dsp::kRateDivisions.
        return { "4 bars", "2 bars", "1 bar", "1/2 D", "1/2", "1/2 T", "1/4 D", "1/4",
                 "1/4 T", "1/8 D", "1/8", "1/8 T", "1/16", "1/16 T", "1/32" };
    }

    juce::StringArray characterNames()
    {
        juce::StringArray names;

        for (int i = 0; i < (int) Character::NumCharacters; ++i)
            names.add (characterName ((Character) i));

        return names;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout create()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    //== The three controls you actually touch ================================

    // Amount scales every generated depth. It is the difference between "show
    // me the idea" and "commit to it", and it is the one control that should
    // never be randomised.
    layout.add (depth (kAmount, "Amount", 60.0f));

    layout.add (depth (kMix, "Mix", 100.0f));

    // Continuous rather than stepped: a 0.1 dB interval snaps the 0 dB default
    // a few ULPs off unity gain, which is not "no trim" any more.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { kTrim, kVersionHint }, "Trim",
        juce::NormalisableRange<float> { -24.0f, 24.0f }, 0.0f,
        Attributes {}.withLabel ("dB")
                     .withStringFromValueFunction ([] (float v, int)
                     {
                         return (v > 0.0f ? "+" : "") + juce::String (v, 1);
                     })));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { kBypass, kVersionHint }, "Bypass", false));

    //== The dice =============================================================

    // An integer, and a small one, because it is meant to be read off the panel
    // and typed back in. A 32-bit seed would be unreadable and a float seed
    // would not survive being written down.
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { kSeed, kVersionHint }, "Seed", kMinSeed, kMaxSeed, 1));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { kCharacter, kVersionHint }, "Character", characterNames(), 0));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { kRate, kVersionHint }, "Rate", rateNames(), 7));

    //== What the dice writes into ============================================
    //
    // These are ordinary automatable parameters, not hidden state. A throw
    // lands here, you nudge whatever it got wrong, and the result saves and
    // automates like anything else in the host.

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { kFilterOn, kVersionHint }, "Filter", true));
    layout.add (depth (kFilterAmt, "Filter Depth", 40.0f));
    layout.add (depth (kFilterTone, "Filter Tone", 50.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { kFilterMode, kVersionHint }, "Filter Mode",
        juce::StringArray { "Low", "Band", "High" }, 0));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { kDriveOn, kVersionHint }, "Drive", false));
    layout.add (depth (kDriveAmt, "Drive Depth", 30.0f));
    layout.add (depth (kDriveTone, "Drive Tone", 50.0f));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { kCrushOn, kVersionHint }, "Crush", false));
    layout.add (depth (kCrushAmt, "Crush Depth", 30.0f));
    layout.add (depth (kCrushTone, "Crush Tone", 60.0f));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { kTremOn, kVersionHint }, "Volume", false));
    layout.add (depth (kTremAmt, "Volume Depth", 50.0f));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { kTremShape, kVersionHint }, "Shape",
        juce::StringArray { "Sine", "Triangle", "Saw", "Square", "Random" }, 0));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { kWidthOn, kVersionHint }, "Width", false));

    // 50 % is untouched: below narrows towards mono, above widens. Bipolar
    // around the middle so the knob reads as "how much narrower or wider".
    layout.add (depth (kWidthAmt, "Width Depth", 50.0f));

    return layout;
}

} // namespace kloudsauce::params
