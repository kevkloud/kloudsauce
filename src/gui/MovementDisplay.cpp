#include "MovementDisplay.h"
#include <cmath>

namespace kloudsauce::gui
{

MovementDisplay::MovementDisplay (std::function<float()> phaseSource,
                                  std::function<float()> depthSource,
                                  std::function<int()>   shapeSource,
                                  std::function<juce::String()> captionSource)
    : phase (std::move (phaseSource)),
      depth (std::move (depthSource)),
      shape (std::move (shapeSource)),
      caption (std::move (captionSource))
{
    // 30 Hz. The playhead only has to look continuous, and a plugin that
    // repaints at 60 on every open editor is a plugin that shows up in the
    // host's CPU meter for no musical reason.
    startTimerHz (30);
}

void MovementDisplay::timerCallback()
{
    const auto target = depth ? depth() : 0.0f;

    // The depth reading is a per-block peak, so it jumps. Smoothed here rather
    // than in the DSP, because the audio thread should not be doing work that
    // only exists to make a picture look nice.
    smoothedDepth = smoothedDepth * 0.8f + target * 0.2f;

    repaint();
}

float MovementDisplay::shapeValue (int shape, float p) noexcept
{
    switch (shape)
    {
        case 0: return 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * p);
        case 1: return p < 0.5f ? p * 2.0f : 2.0f - p * 2.0f;
        case 2: return 1.0f - p;
        case 3: return p < 0.5f ? 1.0f : 0.0f;

        // The random shape holds one value per eighth of a cycle. Drawn from a
        // fixed sequence rather than the live one: the picture is meant to say
        // "this steps" and would be unreadable if it changed every repaint.
        case 4:
        {
            static constexpr float steps[8] = { 0.7f, 0.2f, 0.9f, 0.45f, 0.15f, 0.8f, 0.35f, 0.6f };
            return steps[juce::jlimit (0, 7, (int) (p * 8.0f))];
        }

        default: break;
    }

    return 0.0f;
}

void MovementDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (theme::panelDeep);
    g.fillRoundedRectangle (bounds, theme::corner);

    auto plot = bounds.reduced (10.0f, 12.0f);

    // Grid: quarters of the cycle. Enough to read where a shape lands without
    // turning the well into graph paper.
    g.setColour (theme::grid);

    for (int i = 1; i < 4; ++i)
    {
        const auto x = plot.getX() + plot.getWidth() * (float) i / 4.0f;
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
    }

    g.setColour (theme::gridEmphasis);
    g.drawHorizontalLine ((int) plot.getCentreY(), plot.getX(), plot.getRight());

    const auto currentShape = shape ? shape() : 0;
    const auto d = juce::jlimit (0.0f, 1.0f, smoothedDepth);

    const auto yFor = [&plot, d] (float v)
    {
        // The trace collapses towards the centre line as the depth falls, so a
        // throw doing nothing looks like it is doing nothing.
        const auto scaled = 0.5f + (v - 0.5f) * juce::jmax (0.04f, d);
        return plot.getBottom() - scaled * plot.getHeight();
    };

    juce::Path trace;
    constexpr int steps = 192;

    for (int i = 0; i <= steps; ++i)
    {
        const auto t = (float) i / (float) steps;
        const auto y = yFor (shapeValue (currentShape, t));
        const auto x = plot.getX() + t * plot.getWidth();

        if (i == 0) trace.startNewSubPath (x, y);
        else        trace.lineTo (x, y);
    }

    g.setColour (d > 0.01f ? theme::trace : theme::traceSoft);
    g.strokePath (trace, juce::PathStrokeType (1.6f));

    // Playhead.
    const auto ph = juce::jlimit (0.0f, 1.0f, phase ? phase() : 0.0f);
    const auto px = plot.getX() + ph * plot.getWidth();
    const auto py = yFor (shapeValue (currentShape, ph));

    g.setColour (theme::accentSoft);
    g.drawVerticalLine ((int) px, plot.getY(), plot.getBottom());

    g.setColour (theme::accent);
    g.fillEllipse (px - 3.5f, py - 3.5f, 7.0f, 7.0f);

    if (caption)
    {
        g.setColour (theme::textDim);
        g.setFont (theme::labelFont (10.0f));
        g.drawText (caption(), bounds.reduced (8.0f, 4.0f).removeFromTop (12.0f),
                    juce::Justification::topLeft, false);
    }

    g.setColour (theme::outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), theme::corner, 1.0f);
}

} // namespace kloudsauce::gui
