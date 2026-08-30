#include "PluginProcessor.h"

#include <iostream>
#include <set>
#include <string>

namespace
{
    int failures = 0;

    void check (bool ok, const std::string& what)
    {
        if (! ok) { std::cerr << "FAIL: " << what << '\n'; ++failures; }
    }

    namespace p = kloudsauce::params;

    const char* const kAllIds[] =
    {
        p::kAmount, p::kMix, p::kTrim, p::kBypass,
        p::kSeed, p::kCharacter, p::kRate,
        p::kFilterOn, p::kFilterAmt, p::kFilterTone, p::kFilterMode,
        p::kDriveOn,  p::kDriveAmt,  p::kDriveTone,
        p::kCrushOn,  p::kCrushAmt,  p::kCrushTone,
        p::kTremOn,   p::kTremAmt,   p::kTremShape,
        p::kWidthOn,  p::kWidthAmt
    };
}

//==============================================================================
/** The parameter schema is a wire protocol: an Ableton set saved today has to
    open the same way in two years. These tests exist to make a rename or a
    reorder fail loudly here rather than silently in someone's project.
*/
static void testSchemaIsStable()
{
    KloudSauceAudioProcessor processor;
    auto& apvts = processor.getApvts();

    for (auto id : kAllIds)
        check (apvts.getParameter (id) != nullptr,
               std::string ("parameter '") + id + "' exists");

    std::set<std::string> unique;

    for (auto id : kAllIds)
        check (unique.insert (id).second, std::string ("parameter '") + id + "' is not a duplicate");
}

/** The choice lists and the DSP have to agree on how many of each thing there
    is, or an index saved by the host selects something else entirely. */
static void testChoiceListsMatchTheDsp()
{
    KloudSauceAudioProcessor processor;
    auto& apvts = processor.getApvts();

    const auto choiceCount = [&apvts] (const char* id)
    {
        auto* choice = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (id));
        return choice != nullptr ? choice->choices.size() : -1;
    };

    check (choiceCount (p::kRate) == kloudsauce::dsp::kNumRateDivisions,
           "Rate offers exactly the divisions the LFO knows");

    check (choiceCount (p::kCharacter) == (int) kloudsauce::Character::NumCharacters,
           "Character offers exactly the characters the dice knows");

    check (choiceCount (p::kTremShape) == kloudsauce::dsp::Lfo::NumShapes,
           "Shape offers exactly the shapes the LFO knows");

    check (choiceCount (p::kFilterMode) == 3, "Filter Mode offers three modes");
}

/** The dice must never touch the controls you use to judge a throw. */
static void testTheDiceLeavesTheHandControlsAlone()
{
    for (auto* id : { p::kAmount, p::kMix, p::kTrim, p::kBypass, p::kSeed, p::kCharacter })
        check (! p::kGeneratedParameters.contains (id),
               std::string ("'") + id + "' is not randomised");

    KloudSauceAudioProcessor processor;
    auto& apvts = processor.getApvts();

    const auto amountBefore = apvts.getRawParameterValue (p::kAmount)->load();
    const auto mixBefore    = apvts.getRawParameterValue (p::kMix)->load();
    const auto trimBefore   = apvts.getRawParameterValue (p::kTrim)->load();

    for (int i = 0; i < 32; ++i)
        processor.roll();

    check (apvts.getRawParameterValue (p::kAmount)->load() == amountBefore
        && apvts.getRawParameterValue (p::kMix)->load()    == mixBefore
        && apvts.getRawParameterValue (p::kTrim)->load()   == trimBefore,
           "thirty-two throws leave Amount, Mix and Trim untouched");
}

/** A throw has to land in real parameters, not hidden state -- otherwise you
    cannot fix the one thing it got wrong. */
static void testAThrowMovesRealParameters()
{
    KloudSauceAudioProcessor processor;
    auto& apvts = processor.getApvts();

    // Park every generated parameter somewhere the generator is unlikely to
    // reproduce, then check the throw actually wrote over it.
    for (const auto& id : p::kGeneratedParameters)
        if (auto* parameter = apvts.getParameter (id))
            parameter->setValueNotifyingHost (0.0f);

    auto moved = 0;

    processor.roll();

    for (const auto& id : p::kGeneratedParameters)
        if (auto* parameter = apvts.getParameter (id))
            if (parameter->getValue() != 0.0f)
                ++moved;

    check (moved >= 4, "a throw writes into at least four generated parameters");
}

/** The plugin opens doing something mild, not nothing and not everything. */
static void testDefaults()
{
    KloudSauceAudioProcessor processor;
    auto& apvts = processor.getApvts();

    check (*apvts.getRawParameterValue (p::kMix) == 100.0f, "the default Mix is fully wet");
    check (*apvts.getRawParameterValue (p::kTrim) == 0.0f,  "the default Trim is exactly 0 dB");
    check (*apvts.getRawParameterValue (p::kBypass) == 0.0f, "the plugin does not open bypassed");

    for (auto id : kAllIds)
    {
        auto* parameter = apvts.getParameter (id);
        check (parameter->getValue() == parameter->getDefaultValue(),
               std::string ("'") + id + "' opens at its default");
    }
}

/** Hand edits made after a throw have to survive a save and reload. If the
    state restored the seed and re-derived the recipe, they would not. */
static void testStateRoundTripsIncludingHandEdits()
{
    juce::MemoryBlock saved;
    float editedCrush = 0.0f;
    float seed = 0.0f;

    {
        KloudSauceAudioProcessor processor;
        auto& apvts = processor.getApvts();

        processor.roll();

        // The hand edit: turn the crush down to something the dice would not
        // have chosen, and switch it on so the value is meaningful.
        apvts.getParameter (p::kCrushOn)->setValueNotifyingHost (1.0f);
        apvts.getParameter (p::kCrushAmt)->setValueNotifyingHost (0.137f);

        editedCrush = apvts.getRawParameterValue (p::kCrushAmt)->load();
        seed        = apvts.getRawParameterValue (p::kSeed)->load();

        processor.getStateInformation (saved);
    }

    {
        KloudSauceAudioProcessor processor;
        processor.setStateInformation (saved.getData(), (int) saved.getSize());

        auto& apvts = processor.getApvts();

        check (apvts.getRawParameterValue (p::kSeed)->load() == seed,
               "the seed survives a save and reload");

        check (std::abs (apvts.getRawParameterValue (p::kCrushAmt)->load() - editedCrush) < 1.0e-4f,
               "a hand edit made after a throw survives a save and reload");
    }
}

/** Processing has to be safe before the host has said anything about the seed,
    and safe with no playhead at all -- which is what an offline render and a
    scanner both look like. */
static void testProcessesWithoutAHost()
{
    KloudSauceAudioProcessor processor;

    processor.roll();
    processor.setPlayConfigDetails (2, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    juce::Random random { 1234 };

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample (ch, i, random.nextFloat() * 0.5f - 0.25f);

    processor.processBlock (buffer, midi);

    auto finite = true;

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            finite = finite && std::isfinite (buffer.getSample (ch, i));

    check (finite, "processing with no playhead produces finite output");
}

//==============================================================================
int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "KloudSauce parameter tests\n";

    testSchemaIsStable();
    testChoiceListsMatchTheDsp();
    testTheDiceLeavesTheHandControlsAlone();
    testAThrowMovesRealParameters();
    testDefaults();
    testStateRoundTripsIncludingHandEdits();
    testProcessesWithoutAHost();

    if (failures == 0)
        std::cout << "all tests passed\n";
    else
        std::cout << failures << " test(s) failed\n";

    return failures == 0 ? 0 : 1;
}
