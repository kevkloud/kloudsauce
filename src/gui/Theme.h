#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace kloudsauce::theme
{

/** Palette and metrics.

    The same finish as FrostyEQ and KloudFormant, which is Ableton's own: flat,
    restrained, no bevels, controls drawn as thin value arcs, a dark well for
    the display. Three plugins from the same house should look like it.

    The departure is the accent. The other two are analysis tools where the
    accent marks the one thing you changed. Here every throw of the dice moves
    six things at once, so the accent marks *movement* -- the modulation
    indicator and whichever modules the current throw switched on. Modules the
    throw left alone stay neutral, so you can read what a seed is doing from
    across the room without opening anything.
*/

inline const juce::Colour background   { 0xff303030 };
inline const juce::Colour panel        { 0xff383838 };
inline const juce::Colour panelDeep    { 0xff1a1a1a };   // display well
inline const juce::Colour outline      { 0xff4a4a4a };
inline const juce::Colour grid         { 0xff2b2b2b };
inline const juce::Colour gridEmphasis { 0xff3d3d3d };

inline const juce::Colour text         { 0xffd8d8d8 };
inline const juce::Colour textDim      { 0xff8c8c8c };

inline const juce::Colour accent       { 0xffd94f6a };   // movement
inline const juce::Colour accentSoft   { 0x33d94f6a };
inline const juce::Colour trace        { 0xff9fb8c8 };   // the LFO trace
inline const juce::Colour traceSoft    { 0x40b0b0b0 };   // the cycle behind it
inline const juce::Colour active       { 0xffe8b84b };   // engaged toggles
inline const juce::Colour inactive     { 0xff4f4f4f };

inline const juce::Colour engaged      { 0xff7fbf5f };
inline const juce::Colour meterLow     { 0xff7fbf5f };
inline const juce::Colour meterHigh    { 0xffe8b84b };
inline const juce::Colour meterClip    { 0xffe05a4a };

inline constexpr float knobTrack = 3.0f;
inline constexpr float knobValue = 3.5f;
inline constexpr float corner    = 3.0f;

inline juce::Font labelFont (float height)
{
    return juce::Font (juce::FontOptions {}.withHeight (height));
}

} // namespace kloudsauce::theme
