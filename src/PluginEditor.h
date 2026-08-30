#pragma once

#include "PluginProcessor.h"
#include "gui/Controls.h"
#include "gui/MovementDisplay.h"

#include <memory>
#include <vector>

//==============================================================================
/** The panel.

    One row per module, one knob per thing that matters, and the dice at the
    top. Deliberately not resizable and deliberately not scrolling: the point of
    the plugin is that you can see the whole state of a throw at once and decide
    in a second whether to keep it.
*/
class KloudSauceAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit KloudSauceAudioProcessorEditor (KloudSauceAudioProcessor&);
    ~KloudSauceAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    /** A module: its on/off toggle and the knobs that belong to it. Grouped so
        the toggle can grey out its own controls -- a module that is off should
        look off, not merely sound off. */
    struct Strip : public juce::Component,
                   private juce::Timer
    {
        Strip (juce::AudioProcessorValueTreeState&,
               const juce::String& toggleId,
               const juce::String& name,
               const juce::StringArray& knobIds,
               const juce::StringArray& knobCaptions);

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void timerCallback() override;

        juce::AudioProcessorValueTreeState& state;
        juce::String toggleParameterId;

        kloudsauce::gui::SwitchButton toggle;
        std::vector<std::unique_ptr<kloudsauce::gui::LabelledKnob>> knobs;

        bool lastEnabled = true;
    };

    KloudSauceAudioProcessor& processor;
    kloudsauce::gui::KloudSauceLookAndFeel lookAndFeel;

    juce::TextButton dice { "THROW" };

    juce::ComboBox character;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> characterAttachment;

    kloudsauce::gui::LabelledKnob seed, rate, amount, mix, trim;
    kloudsauce::gui::SwitchButton bypass;

    kloudsauce::gui::MovementDisplay movement;
    kloudsauce::gui::LevelMeter inputMeter, outputMeter;

    std::vector<std::unique_ptr<Strip>> strips;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KloudSauceAudioProcessorEditor)
};
