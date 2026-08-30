#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace kloudsauce;
namespace p = kloudsauce::params;

//==============================================================================
KloudSauceAudioProcessor::KloudSauceAudioProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KLOUDSAUCE", p::create())
{
    const auto bind = [this] (const char* id)
    {
        auto* raw = apvts.getRawParameterValue (id);
        jassert (raw != nullptr);
        return raw;
    };

    amountParam     = bind (p::kAmount);
    mixParam        = bind (p::kMix);
    trimParam       = bind (p::kTrim);
    bypassParam     = bind (p::kBypass);
    rateParam       = bind (p::kRate);
    filterOnParam   = bind (p::kFilterOn);
    filterAmtParam  = bind (p::kFilterAmt);
    filterToneParam = bind (p::kFilterTone);
    filterModeParam = bind (p::kFilterMode);
    driveOnParam    = bind (p::kDriveOn);
    driveAmtParam   = bind (p::kDriveAmt);
    driveToneParam  = bind (p::kDriveTone);
    crushOnParam    = bind (p::kCrushOn);
    crushAmtParam   = bind (p::kCrushAmt);
    crushToneParam  = bind (p::kCrushTone);
    tremOnParam     = bind (p::kTremOn);
    tremAmtParam    = bind (p::kTremAmt);
    tremShapeParam  = bind (p::kTremShape);
    widthOnParam    = bind (p::kWidthOn);
    widthAmtParam   = bind (p::kWidthAmt);

    apvts.addParameterListener (p::kSeed, this);
    apvts.addParameterListener (p::kCharacter, this);
}

KloudSauceAudioProcessor::~KloudSauceAudioProcessor()
{
    apvts.removeParameterListener (p::kSeed, this);
    apvts.removeParameterListener (p::kCharacter, this);
}

//==============================================================================
void KloudSauceAudioProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    core.setParams (currentParams());
    core.prepare (sampleRate, maximumExpectedSamplesPerBlock, getTotalNumOutputChannels());

    for (auto& v : inputPeak)  v.store (0.0f);
    for (auto& v : outputPeak) v.store (0.0f);
}

void KloudSauceAudioProcessor::releaseResources()
{
}

bool KloudSauceAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

//==============================================================================
SauceCore::Params KloudSauceAudioProcessor::currentParams() const noexcept
{
    const auto load = [] (const std::atomic<float>* v) { return v != nullptr ? v->load() : 0.0f; };
    const auto norm = [&load] (const std::atomic<float>* v) { return load (v) * 0.01f; };

    SauceCore::Params out;

    out.amount = norm (amountParam);
    out.mix    = norm (mixParam);
    out.trim   = juce::Decibels::decibelsToGain (load (trimParam));
    out.bypass = load (bypassParam) > 0.5f;

    const auto rate = juce::jlimit (0, dsp::kNumRateDivisions - 1, (int) load (rateParam));
    out.beatsPerCycle = dsp::kRateDivisions[rate];

    out.filterOn   = load (filterOnParam) > 0.5f;
    out.filterAmt  = norm (filterAmtParam);
    out.filterTone = norm (filterToneParam);
    out.filterMode = (int) load (filterModeParam);

    out.driveOn    = load (driveOnParam) > 0.5f;
    out.driveAmt   = norm (driveAmtParam);
    out.driveTone  = norm (driveToneParam);

    out.crushOn    = load (crushOnParam) > 0.5f;
    out.crushAmt   = norm (crushAmtParam);
    out.crushTone  = norm (crushToneParam);

    out.tremOn     = load (tremOnParam) > 0.5f;
    out.tremAmt    = norm (tremAmtParam);
    out.tremShape  = (int) load (tremShapeParam);

    out.widthOn    = load (widthOnParam) > 0.5f;
    out.widthAmt   = norm (widthAmtParam);

    return out;
}

void KloudSauceAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numChannels = juce::jmin (buffer.getNumChannels(), getTotalNumOutputChannels());
    const auto numSamples  = buffer.getNumSamples();

    for (int ch = getTotalNumInputChannels(); ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    for (int ch = 0; ch < juce::jmin (2, numChannels); ++ch)
        inputPeak[(size_t) ch].store (buffer.getMagnitude (ch, 0, numSamples), std::memory_order_relaxed);

    SauceCore::Transport transport;

    if (auto* playHead = getPlayHead())
    {
        if (const auto position = playHead->getPosition())
        {
            transport.bpm     = position->getBpm().orFallback (120.0);
            transport.ppq     = position->getPpqPosition().orFallback (0.0);
            transport.playing = position->getIsPlaying();
        }
    }

    core.setParams (currentParams());
    core.process (buffer.getArrayOfWritePointers(), numChannels, numSamples, transport);

    for (int ch = 0; ch < juce::jmin (2, numChannels); ++ch)
        outputPeak[(size_t) ch].store (buffer.getMagnitude (ch, 0, numSamples), std::memory_order_relaxed);
}

//==============================================================================
void KloudSauceAudioProcessor::parameterChanged (const juce::String&, float)
{
    if (applyingRecipe.load (std::memory_order_relaxed))
        return;

    seedNeedsApplying.store (true, std::memory_order_relaxed);
    triggerAsyncUpdate();
}

void KloudSauceAudioProcessor::handleAsyncUpdate()
{
    if (seedNeedsApplying.exchange (false, std::memory_order_relaxed))
        applyCurrentSeed();
}

void KloudSauceAudioProcessor::roll()
{
    // A new seed rather than a new recipe: the sound has to be recoverable from
    // the number afterwards, and that only holds if the number came first.
    if (auto* seed = apvts.getParameter (p::kSeed))
    {
        const auto next = seedSource.nextInt ({ p::kMinSeed, p::kMaxSeed + 1 });

        seed->beginChangeGesture();
        seed->setValueNotifyingHost (seed->convertTo0to1 ((float) next));
        seed->endChangeGesture();
    }

    applyCurrentSeed();
}

void KloudSauceAudioProcessor::applyCurrentSeed()
{
    const auto seed = (std::uint32_t) juce::roundToInt (
        apvts.getParameterAsValue (p::kSeed).getValue().operator float());

    const auto character = (Character) juce::jlimit (
        0, (int) Character::NumCharacters - 1,
        (int) apvts.getRawParameterValue (p::kCharacter)->load());

    // Amount is a hand control and is not part of the throw, so the recipe is
    // generated at full intensity and Amount scales it live in the DSP. That
    // way turning Amount down and back up returns the same sound rather than a
    // quantised approximation of it.
    applyRecipe (generate (seed, character, 1.0f));
}

void KloudSauceAudioProcessor::applyRecipe (const Recipe& r)
{
    applyingRecipe.store (true, std::memory_order_relaxed);

    const auto set = [this] (const char* id, float plainValue)
    {
        if (auto* parameter = apvts.getParameter (id))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
            parameter->endChangeGesture();
        }
    };

    const auto setPercent = [&set] (const char* id, float normalised)
    {
        set (id, juce::jlimit (0.0f, 100.0f, normalised * 100.0f));
    };

    set (p::kRate, (float) r.rateIndex);

    set        (p::kFilterOn,   r.filterOn ? 1.0f : 0.0f);
    setPercent (p::kFilterAmt,  r.filterAmt);
    setPercent (p::kFilterTone, r.filterTone);
    set        (p::kFilterMode, (float) r.filterMode);

    set        (p::kDriveOn,    r.driveOn ? 1.0f : 0.0f);
    setPercent (p::kDriveAmt,   r.driveAmt);
    setPercent (p::kDriveTone,  r.driveTone);

    set        (p::kCrushOn,    r.crushOn ? 1.0f : 0.0f);
    setPercent (p::kCrushAmt,   r.crushAmt);
    setPercent (p::kCrushTone,  r.crushTone);

    set        (p::kTremOn,     r.tremOn ? 1.0f : 0.0f);
    setPercent (p::kTremAmt,    r.tremAmt);
    set        (p::kTremShape,  (float) r.tremShape);

    set        (p::kWidthOn,    r.widthOn ? 1.0f : 0.0f);
    setPercent (p::kWidthAmt,   r.widthAmt);

    applyingRecipe.store (false, std::memory_order_relaxed);
}

//==============================================================================
void KloudSauceAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", p::kStateVersion, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void KloudSauceAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            // Replace rather than merge: a partial state would leave the
            // generated parameters holding values from the previous throw,
            // which is a sound nobody chose.
            apvts.replaceState (juce::ValueTree::fromXml (*xml));

            // The recipe is already in the restored parameters. Re-deriving it
            // from the seed here would clobber any hand edits the user made
            // after the throw, which are the whole point of the throw being
            // written into real parameters.
            seedNeedsApplying.store (false, std::memory_order_relaxed);
        }
}

//==============================================================================
juce::AudioProcessorEditor* KloudSauceAudioProcessor::createEditor()
{
    return new KloudSauceAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KloudSauceAudioProcessor();
}
