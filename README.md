# KloudSauce

A dice for loops. VST3 / AU / Standalone.

Drop it on a sample you like but have heard too many times, press **THROW**, and
get a version of it that is recognisably the same performance and audibly not
the same record. Press it again for a different one. The Amount knob decides how
far you commit, the Mix knob decides how much of it you keep, and the seed on the
panel is the number that gets you back to the throw you liked.

## Status

Scaffolding. The chain, the dice, the panel, the offline profiler and CI are all
in and tested; the sound design is not finished and the module set is a starting
point rather than the plan. See [docs/plan.md](docs/plan.md) for what this is
meant to become and which decisions are still open.

## The idea

There are two plugins in this space and neither does this job.

[Drip](https://dripplugin.com/) is a fixed multi-effect chain behind a preset
list and a one-knob macro. It is fast, and after a week you can hear which of
the 24 presets someone used. Its randomness is a menu.

[ShaperBox](https://www.cableguys.com/shaperbox) is the opposite: nine multiband
processors with drawable LFOs, and it will do anything you can specify. That is
the catch — you have to already know what you want. It is a precision
instrument, not an idea generator.

KloudSauce is the third thing: a chain with **a distribution instead of a preset
list**. A throw is a number, a number is a sound, and the character selector
changes what kind of sound numbers turn into. That gets you two things at once —
variation on a loop that is genuinely different every time, and a way to find a
direction you would not have dialled in yourself.

## How a throw works

The seed is an ordinary integer parameter, 0–9999, saved in your project. The
generator maps `(seed, character)` to a complete set of module settings, and
that mapping is **deterministic and permanent**: the same number is the same
sound on every machine, every build, and — this is the part that matters — every
time you reopen the set next year. `tests/DspTests.cpp` asserts it over every
seed and character.

A throw then writes those settings into **real, automatable parameters**. It is
not hidden state. You can throw, decide the filter is right but the crush is too
much, pull the crush back by hand, and save that — the way you would with any
other plugin. Nothing about the dice bypasses the parameter system.

Three controls are deliberately never randomised: **Amount**, **Mix** and
**Trim**. If a throw could change how loud the plugin is or how much of it you
are hearing, throws would not be comparable to each other, and comparing them is
the entire workflow.

## The chain

Fixed order: **Filter → Drive → Crush → Volume → Width**, then Mix and Trim.

Fixed because the order that sounds right is not a decision worth spending a
control on. Filtering before saturation keeps the drive from turning every throw
into the same broadband fizz; the volume shape sits after the things that change
the timbre so it stays a rhythm rather than a texture.

All the movement comes from one tempo-locked LFO whose position is read from the
host's PPQ rather than accumulated locally. On a loop that is the whole game: bar
5 has to sound like bar 5 every time you reach it, or the effect cannot be
committed to a bounce.

## What is asserted

From `tests/DspTests.cpp`, which asserts every claim below.

| | |
|---|---|
| Same seed, same sound | identical over 512 seeds × 6 characters |
| Bypass | **bit-identical** to the input |
| Mix at 0 % | dry to better than a 24-bit LSB |
| Amount at 0 % | dry to better than 1e-5 |
| Block size 64 vs 512 | **bitwise identical** output |
| Replaying a bar | **bitwise identical** |
| Bar-to-bar, non-random shape | **bitwise identical** |
| Silence in | silence out |
| 256 seeds × 6 characters | no NaN, no infinity, nothing above +12 dB |
| Loudest throw measured | **+1.4 dB** over the source |
| Every throw | enables at least one module |

Regenerate the distribution tables with `./build/measure profile`.

## Build

```
git clone --recurse-submodules https://github.com/kevkloud/kloudsauce.git
cd kloudsauce
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
```

macOS builds VST3, AU and a standalone; Windows and Linux build VST3 and the
standalone. A development build installs itself into your plugin folders;
`./scripts/build.sh` builds and then checks that it actually landed, which
Ableton will otherwise let fail silently.

The DSP core and the generator have no JUCE dependency, so the interesting half
builds and tests on a bare container in seconds:

```
cmake -B build-dsp -DKLOUDSAUCE_DSP_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-dsp --parallel
ctest --test-dir build-dsp --output-on-failure
```

## Licence

AGPL-3.0. © LT3 Audio.
