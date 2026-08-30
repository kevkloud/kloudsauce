#pragma once

#include "Theme.h"
#include <functional>

namespace kloudsauce::gui
{

/** One cycle of the current modulation shape, with a playhead on it.

    Not an analyser. The question this answers is "what is moving, how fast, and
    where in the bar am I" -- which is the only thing you cannot tell from the
    knobs, and the thing you need when you are deciding whether a throw is worth
    keeping. A spectrum here would be decoration; the shape and the playhead are
    the two things that change what you do next.
*/
class MovementDisplay final : public juce::Component,
                              private juce::Timer
{
public:
    MovementDisplay (std::function<float()> phaseSource,
                     std::function<float()> depthSource,
                     std::function<int()>   shapeSource,
                     std::function<juce::String()> captionSource);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    static float shapeValue (int shape, float phase) noexcept;

    std::function<float()> phase;
    std::function<float()> depth;
    std::function<int()>   shape;
    std::function<juce::String()> caption;

    float smoothedDepth = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MovementDisplay)
};

} // namespace kloudsauce::gui
