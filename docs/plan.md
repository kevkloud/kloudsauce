# KloudSauce — plan

Status: scaffolding is in and tested. This document is what it should become,
and which decisions are still open. Nothing here is committed to until the
questions at the end are answered.

---

## 1. What the plugin is for

Two jobs, stated plainly, because they pull in slightly different directions and
the design has to serve both:

**Job A — variation.** You have a loop. You are going to use it four times in the
track and you do not want it to be the same four times, and you do not want
anyone to recognise the source. You need a different-but-related version, fast,
and you need it to be *committable* — the same on every playback and in the
bounce.

**Job B — brainstorming.** You have a loop and no idea what to do with it. You
want the plugin to suggest directions you would not have dialled in yourself,
quickly enough that you can audition twenty of them before you get bored.

Job A wants determinism, recall and level-matching. Job B wants surprise,
breadth and speed. The seed-based design serves both: **surprise on the way in,
determinism on the way out.**

## 2. Where it sits

| | Drip (Kyle Beats) | ShaperBox 3 (Cableguys) | KloudSauce |
|---|---|---|---|
| What it is | fixed chain + preset list + one macro | 9 multiband processors, drawable LFOs | chain + a distribution |
| Randomness | a menu of 24 | none | the primary interface |
| You need to know what you want | no | **yes** | no |
| Recall | preset name | full manual state | **a number** |
| Ceiling | low — recognisable after a week | very high | medium-high by design |

Drip is fast and shallow. ShaperBox is deep and demands intent. The gap is a
tool that is fast *and* does not converge on one sound, and the way you get that
is to make the randomness first-class rather than a "randomise" button bolted to
a preset browser.

The thing to protect: **a throw must be a real, editable, automatable state.**
The moment the dice becomes hidden state, the plugin becomes Drip with more
presets.

## 3. What is built

- Chain: Filter → Drive → Crush → Volume → Width → Mix → Trim, fixed order.
- One tempo-locked LFO, position read from host PPQ, 15 sync divisions.
- Generator: `(seed, character) → recipe`, deterministic and cross-platform.
- Six characters: Subtle, Bounce, Warped, Hazy, Broken, Wildcard.
- Panel: movement well, five module strips, Amount / Mix / Trim, meters, THROW.
- JUCE-free DSP + generator, tested on a bare container in CI.
- Offline profiler (`measure profile`) that prints the actual distributions.

## 4. What is missing, in priority order

### 4.1 Locks — the single most important missing feature

Right now a throw replaces everything. The workflow you actually want is:

> throw → "the filter movement is perfect, everything else is wrong" → **lock
> the filter** → keep throwing until the rest catches up.

Without locks the plugin is a slot machine. With locks it is a search. This is
the feature that turns Job B from "press until something works" into "converge
on something".

Implementation: a per-module lock flag (not automatable, saved in state, not
part of the recipe). The generator takes a mask of which modules to regenerate.

### 4.2 Throw history

Ten throws back, forward, and a "keep" list. You *will* throw past the good one.
Cheap to build, because a throw is just a seed plus a character plus any hand
edits — a history entry is a few dozen bytes.

### 4.3 More modules

The current five are the safe half of the space. The ones that actually make a
loop stop sounding like its source are the time-domain ones:

| Module | What it buys | Cost |
|---|---|---|
| **Reverse** | grid-locked reverse of the last 1/8, 1/4… | needs a buffer; latency-free if it reads behind |
| **Repeat / stutter** | the single most recognisable "this is a different edit" move | buffer + grid logic |
| **Varispeed / pitch** | half-speed, +5 semis, detune drift | resampling; changes length unless granular |
| **Space** | short reverb / slap, glued to the grid | a real reverb is real work |
| **Formant** | reuse the KloudFormant core — moves a vocal or a one-shot without touching pitch | already written and measured |
| **Wow / flutter** | tape drift; makes a quantised loop sound played | cheap, high value |
| **Noise / texture** | vinyl, room, hiss layered under | cheap, high value |

Reverse and Repeat are the two with the highest ratio of "sounds like a
different record" to implementation cost. Formant is nearly free given
KloudFormant exists, and is a differentiator neither competitor has.

### 4.4 Multiband

ShaperBox's core trick: filter the effect into 3 bands so the crush only hits
the top and the volume shape only hits the low end. It is what makes an
aggressive setting usable on a full loop rather than only on a stem.

This is a large change (every module needs a band mask) and it is the main
"do we go wide or deep" fork in the plan. See question 3.

### 4.5 Transient triggering

Restart the LFO on every transient rather than on the grid. ShaperBox 3 added
this and it is genuinely useful on loops whose groove is not quantised. Small
change to the LFO, medium change to the UI.

### 4.6 Sound-design work that is simply not done

- The saturator's gain normalisation currently makes heavy drive *quieter*
  (measured: Broken tops out at −0.2 dB while Warped hits +1.4 dB). It should be
  loudness-matched across the drive range, not peak-normalised.
- The crusher's downsampler has no anti-aliasing at all. That is a legitimate
  aesthetic choice for a lo-fi module and a bug everywhere else; it should at
  least be a switch.
- The filter's resonance rises with depth, which is a coupling nobody asked for.
- The characters were tuned by reading the profiler table, not by listening. All
  six distributions are a first guess.

### 4.7 Release engineering

- Universal binaries on tags — already in CI, untested.
- Code signing and notarisation for macOS. Without it, everyone who downloads it
  gets a Gatekeeper warning. Needs an Apple Developer account.
- pluginval in CI before any release is tagged.
- A `docs/` page of seeds worth trying, generated by the profiler.

## 5. The Mix knob

Confirmed as a hard requirement and already built, with three properties worth
stating because they are easy to get wrong and expensive to change later:

1. **Linear crossfade, not equal-power.** The wet path is a processed copy of
   the dry one, not an uncorrelated signal, so equal-power pushes the middle of
   the knob about 3 dB loud.
2. **Never randomised.** A throw that changes the wet level is a throw you
   cannot compare to the last one.
3. **Bit-exact at 0 %.** Asserted in the tests. The dry loop is the reference
   you judge every throw against; a Mix that is a fraction of a dB off is a
   reference that lies to you.

Open: whether Mix should be *parallel* (current) or become a **wet-solo /
dry-kill** three-way, which matters for the "layer the mangled version under the
original" workflow that is arguably the main way this gets used on drums.

## 6. UI direction

The panel is built and follows the FrostyEQ / KloudFormant house style — flat,
Ableton-ish, thin value arcs, dark well. Changes to make:

- The movement well should show the **actual modulation** rather than an
  idealised shape, once more than one LFO exists.
- Locks need to live on the strips, which means the strip header becomes
  name + lock rather than just a toggle.
- History needs somewhere to live — probably a thin strip under the header.
- A "this throw is barely doing anything" hint, since Subtle at low Amount can
  produce a genuinely inaudible throw and that reads as a bug.

## 7. Decided

Settled 2026-08-30. The rest of this document is written as if these hold.

| Decision | Answer |
|---|---|
| What to build next | **Per-module locks.** Before anything else, because they change the state format and the strip layout and only get more expensive later. |
| Wide or deep | **Wide.** More modules, not multiband. Multiband stays on the list but behind the module set. |
| Time-domain modules | **In scope, all of them** — Reverse, Repeat/stutter and varispeed. |
| Mix | **Parallel dry/wet plus a wet-solo mode**, for layering the mangled version under the original. |

The consequences worth writing down:

- **Locks come first**, so the recipe generator needs to take a regenerate mask
  from the start rather than always producing a whole recipe. Cheap now,
  invasive later.
- **Varispeed is in**, which means the plugin will change *when* things happen.
  That is a real interaction with Ableton's warping and it needs testing against
  a warped clip early, not at the end — if it fights the host, the module has to
  be grid-quantised rather than free.
- **Multiband is deferred, not dropped.** Every module added between now and
  then should keep its processing in one place per band-able stage, so the
  retrofit is a band loop rather than a rewrite of five modules.
- **Wet-solo means the dry path needs latency compensation** the moment Reverse
  or anything else with lookahead lands, because a dry-killed output has nothing
  to hide misalignment behind.

## 8. Suggested order of work

1. Locks (4.1), including the regenerate mask in the generator
2. Throw history (4.2)
3. Reverse + Repeat (4.3) — grid-locked, buffer-based
4. Wet-solo mode for Mix (5), with the dry path aligned ready for lookahead
5. Character retuning by ear (4.6)
6. Drive loudness matching, crush anti-aliasing switch (4.6)
7. Formant module, reusing KloudFormant (4.3)
8. Varispeed (4.3) — tested against a warped clip before anything is built on it
9. Space, wow/flutter, noise (4.3)
10. Transient triggering (4.5)
11. pluginval, notarisation, first tagged release
12. Multiband retrofit (4.4), if the module set has settled by then

---

## Still open

1. **Character list.** Six is a guess — Subtle, Bounce, Warped, Hazy, Broken,
   Wildcard. Are these the right six, and should they be named for what they do
   or for a genre? Adding a character is adding a row to one table, so this is
   cheap to change until people have saved projects with the indices in them.

2. **Seed range.** 0–9999 is readable and writable-down; it is also only ten
   thousand sounds per character. Enough, or should it be wider with the panel
   showing a short alphanumeric code instead?

3. **How locks are saved.** A lock is a workflow state, not a sound — so it
   probably should not be automatable, and arguably should not travel with a
   preset either. Saved per instance, or not saved at all?
