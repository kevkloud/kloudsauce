#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace kloudsauce::params
{

//==============================================================================
// Parameter IDs.
//
// This is a permanent, append-only schema. Once a user saves an Ableton set,
// automation lanes and stored state are keyed by these exact strings. Renaming
// or reordering silently loses settings in every existing project. Treat a
// change here the way you would treat a wire-protocol change: don't, and if you
// must, add a new ID and migrate on load.
//
// The same applies to the *order* of every choice parameter below, and to the
// seed-to-sound mapping in dsp/Generator.cpp. A saved seed is only worth saving
// if it still means the same sound next year.
//==============================================================================

inline constexpr auto kAmount    = "amount";
inline constexpr auto kMix       = "mix";
inline constexpr auto kTrim      = "trim";
inline constexpr auto kBypass    = "bypass";

inline constexpr auto kSeed      = "seed";
inline constexpr auto kCharacter = "character";
inline constexpr auto kRate      = "rate";

inline constexpr auto kFilterOn   = "flt_on";
inline constexpr auto kFilterAmt  = "flt_amt";
inline constexpr auto kFilterTone = "flt_tone";
inline constexpr auto kFilterMode = "flt_mode";

inline constexpr auto kDriveOn    = "drv_on";
inline constexpr auto kDriveAmt   = "drv_amt";
inline constexpr auto kDriveTone  = "drv_tone";

inline constexpr auto kCrushOn    = "crs_on";
inline constexpr auto kCrushAmt   = "crs_amt";
inline constexpr auto kCrushTone  = "crs_tone";

inline constexpr auto kTremOn     = "trm_on";
inline constexpr auto kTremAmt    = "trm_amt";
inline constexpr auto kTremShape  = "trm_shape";

inline constexpr auto kWidthOn    = "wid_on";
inline constexpr auto kWidthAmt   = "wid_amt";

/** Every parameter the dice is allowed to move. The seed, the character, Amount,
    Mix, Trim and Bypass are deliberately *not* here: a throw must never change
    how loud the plugin is or how much of it you are hearing, or the throws are
    not comparable to each other. */
extern const juce::StringArray kGeneratedParameters;

inline constexpr int kMinSeed = 0;
inline constexpr int kMaxSeed = 9999;

/** Bump only when adding parameters; existing entries keep their original hint. */
inline constexpr int kVersionHint  = 1;
inline constexpr int kStateVersion = 1;

juce::AudioProcessorValueTreeState::ParameterLayout create();

} // namespace kloudsauce::params
