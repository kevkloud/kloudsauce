#include "PluginEditor.h"
#include "dsp/Lfo.h"

using namespace kloudsauce;
namespace p = kloudsauce::params;

namespace
{
    constexpr int kWidth  = 720;
    constexpr int kHeight = 452;

    constexpr int kMargin      = 12;
    constexpr int kHeaderH     = 46;
    constexpr int kMovementH   = 96;
    constexpr int kStripH      = 132;
    constexpr int kFooterH     = 116;
}

//==============================================================================
KloudSauceAudioProcessorEditor::Strip::Strip (juce::AudioProcessorValueTreeState& s,
                                              const juce::String& toggleId,
                                              const juce::String& name,
                                              const juce::StringArray& knobIds,
                                              const juce::StringArray& knobCaptions)
    : state (s),
      toggleParameterId (toggleId),
      toggle (s, toggleId, name)
{
    addAndMakeVisible (toggle);

    jassert (knobIds.size() == knobCaptions.size());

    for (int i = 0; i < knobIds.size(); ++i)
    {
        auto knob = std::make_unique<gui::LabelledKnob> (s, knobIds[i], knobCaptions[i], false);
        knob->setKnobDiameter (38);
        addAndMakeVisible (*knob);
        knobs.push_back (std::move (knob));
    }

    startTimerHz (10);
    timerCallback();
}

void KloudSauceAudioProcessorEditor::Strip::timerCallback()
{
    const auto enabled = state.getRawParameterValue (toggleParameterId)->load() > 0.5f;

    if (enabled == lastEnabled)
        return;

    lastEnabled = enabled;

    for (auto& knob : knobs)
        knob->setKnobEnabled (enabled);

    repaint();
}

void KloudSauceAudioProcessorEditor::Strip::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (theme::panel);
    g.fillRoundedRectangle (bounds, theme::corner);

    // The engaged outline is what makes a throw readable at a glance: four
    // grey strips and one lit one says more than five knob positions do.
    g.setColour (lastEnabled ? theme::accent.withAlpha (0.55f) : theme::outline);
    g.drawRoundedRectangle (bounds, theme::corner, 1.0f);
}

void KloudSauceAudioProcessorEditor::Strip::resized()
{
    auto area = getLocalBounds().reduced (6);

    toggle.setBounds (area.removeFromTop (20));
    area.removeFromTop (6);

    if (knobs.empty())
        return;

    const auto cell = area.getWidth() / (int) knobs.size();

    for (auto& knob : knobs)
        knob->setBounds (area.removeFromLeft (cell).withTrimmedTop (2));
}

//==============================================================================
KloudSauceAudioProcessorEditor::KloudSauceAudioProcessorEditor (KloudSauceAudioProcessor& proc)
    : juce::AudioProcessorEditor (&proc),
      processor (proc),
      seed   (proc.getApvts(), p::kSeed,   "SEED",   false),
      rate   (proc.getApvts(), p::kRate,   "RATE",   false),
      amount (proc.getApvts(), p::kAmount, "AMOUNT", false),
      mix    (proc.getApvts(), p::kMix,    "MIX",    false),
      trim   (proc.getApvts(), p::kTrim,   "TRIM",   true),
      bypass (proc.getApvts(), p::kBypass, "BYPASS"),
      movement ([&proc] { return proc.getLfoPhase(); },
                [&proc] { return proc.getModulationDepth(); },
                [&proc] { return (int) proc.getApvts().getRawParameterValue (p::kTremShape)->load(); },
                [&proc]
                {
                    const auto rateIndex = (int) proc.getApvts().getRawParameterValue (p::kRate)->load();

                    if (auto* parameter = proc.getApvts().getParameter (p::kRate))
                        return "CYCLE  " + parameter->getText (parameter->convertTo0to1 ((float) rateIndex), 16);

                    return juce::String ("CYCLE");
                }),
      inputMeter  ("IN",  [&proc] { return juce::jmax (proc.getInputPeak (0),  proc.getInputPeak (1)); }),
      outputMeter ("OUT", [&proc] { return juce::jmax (proc.getOutputPeak (0), proc.getOutputPeak (1)); })
{
    setLookAndFeel (&lookAndFeel);

    dice.onClick = [this] { processor.roll(); };
    dice.setColour (juce::TextButton::buttonColourId, theme::accent.withAlpha (0.85f));
    dice.setColour (juce::TextButton::textColourOffId, juce::Colours::black.withAlpha (0.85f));
    addAndMakeVisible (dice);

    if (auto* parameter = dynamic_cast<juce::AudioParameterChoice*> (proc.getApvts().getParameter (p::kCharacter)))
        character.addItemList (parameter->choices, 1);

    character.setColour (juce::ComboBox::backgroundColourId, theme::panel);
    character.setColour (juce::ComboBox::outlineColourId,    theme::outline);
    character.setColour (juce::ComboBox::textColourId,       theme::text);
    addAndMakeVisible (character);

    characterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        proc.getApvts(), p::kCharacter, character);

    for (auto* c : { &seed, &rate, &amount, &mix, &trim })
        addAndMakeVisible (c);

    seed.setKnobDiameter (36);
    rate.setKnobDiameter (36);
    amount.setKnobDiameter (58);
    mix.setKnobDiameter (48);
    trim.setKnobDiameter (44);

    addAndMakeVisible (bypass);
    addAndMakeVisible (movement);
    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);

    const auto addStrip = [this, &proc] (const char* toggleId, const juce::String& name,
                                         const juce::StringArray& ids, const juce::StringArray& captions)
    {
        strips.push_back (std::make_unique<Strip> (proc.getApvts(), toggleId, name, ids, captions));
        addAndMakeVisible (*strips.back());
    };

    addStrip (p::kFilterOn, "FILTER", { p::kFilterAmt, p::kFilterTone, p::kFilterMode },
                                      { "DEPTH", "TONE", "MODE" });
    addStrip (p::kDriveOn,  "DRIVE",  { p::kDriveAmt,  p::kDriveTone },  { "DEPTH", "TONE" });
    addStrip (p::kCrushOn,  "CRUSH",  { p::kCrushAmt,  p::kCrushTone },  { "DEPTH", "TONE" });
    addStrip (p::kTremOn,   "VOLUME", { p::kTremAmt,   p::kTremShape },  { "DEPTH", "SHAPE" });
    addStrip (p::kWidthOn,  "WIDTH",  { p::kWidthAmt },                  { "DEPTH" });

    setSize (kWidth, kHeight);
}

KloudSauceAudioProcessorEditor::~KloudSauceAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

//==============================================================================
void KloudSauceAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    auto header = getLocalBounds().reduced (kMargin).removeFromTop (kHeaderH);

    g.setColour (theme::text);
    g.setFont (theme::labelFont (19.0f).withExtraKerningFactor (0.22f));
    g.drawText ("KLOUDSAUCE", header.removeFromLeft (170).withTrimmedTop (10),
                juce::Justification::topLeft, false);

    g.setColour (theme::outline);
    const auto y = kMargin + kHeaderH - 2;
    g.drawHorizontalLine (y, (float) kMargin, (float) (getWidth() - kMargin));
}

void KloudSauceAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (kMargin);

    //== Header ===============================================================
    {
        auto header = area.removeFromTop (kHeaderH);
        header.removeFromLeft (180);                 // the wordmark, painted

        auto right = header.removeFromRight (250);
        dice.setBounds (right.removeFromRight (86).reduced (0, 8));
        right.removeFromRight (8);
        character.setBounds (right.removeFromRight (110).reduced (0, 10));

        header.removeFromLeft (8);
        seed.setBounds (header.removeFromLeft (74));
        rate.setBounds (header.removeFromLeft (74));
    }

    area.removeFromTop (8);

    //== Movement =============================================================
    movement.setBounds (area.removeFromTop (kMovementH));
    area.removeFromTop (10);

    //== Modules ==============================================================
    {
        auto row = area.removeFromTop (kStripH);
        const auto gap = 8;
        const auto cell = (row.getWidth() - gap * ((int) strips.size() - 1)) / (int) strips.size();

        for (size_t i = 0; i < strips.size(); ++i)
        {
            strips[i]->setBounds (row.removeFromLeft (cell));

            if (i + 1 < strips.size())
                row.removeFromLeft (gap);
        }
    }

    area.removeFromTop (10);

    //== Footer ===============================================================
    {
        auto footer = area.removeFromTop (kFooterH);

        auto meters = footer.removeFromRight (72);
        bypass.setBounds (meters.removeFromBottom (22).reduced (2, 0));
        meters.removeFromBottom (8);
        inputMeter .setBounds (meters.removeFromLeft (32));
        meters.removeFromLeft (8);
        outputMeter.setBounds (meters.removeFromLeft (32));

        // Amount first and largest. It is the control you reach for after every
        // throw, and the panel should say so before you read a single label.
        amount.setBounds (footer.removeFromLeft (110));
        footer.removeFromLeft (6);
        mix   .setBounds (footer.removeFromLeft (96));
        footer.removeFromLeft (6);
        trim  .setBounds (footer.removeFromLeft (88));
    }
}
