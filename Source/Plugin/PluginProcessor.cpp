#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Core/Params.h"

juce::AudioProcessor::BusesProperties DustLoopsProcessor::makeBuses()
{
    const auto stereo = juce::AudioChannelSet::stereo();
    if constexpr (dust::kIsFx)
        return BusesProperties().withInput ("Input", stereo, true).withOutput ("Output", stereo, true);
    else
        return BusesProperties().withInput ("Sidechain", stereo, true).withOutput ("Output", stereo, true);
}

namespace
{
// The ValueTree type is fixed (not the product name) so renaming the plugin never breaks saved projects.
const juce::Identifier stateType { "DustLoopsState" };
const juce::Identifier pedalOrderProp { "pedalOrder" };
} // namespace

DustLoopsProcessor::DustLoopsProcessor()
    : AudioProcessor (makeBuses()),
      apvts (*this, nullptr, stateType, Params::createLayout (! dust::kIsFx))
{
    apvts.state.setProperty (pedalOrderProp, defaultPedalOrder, nullptr);

    masterParam = apvts.getRawParameterValue (ID::global::master);
    inputGainParam = apvts.getRawParameterValue (ID::global::inputGain);

    if constexpr (! dust::kIsFx)
    {
        synthLevelParam = apvts.getRawParameterValue (ID::synth::level);
        attackParam = apvts.getRawParameterValue (ID::synth::attack);
        decayParam = apvts.getRawParameterValue (ID::synth::decay);
        sustainParam = apvts.getRawParameterValue (ID::synth::sustain);
        releaseParam = apvts.getRawParameterValue (ID::synth::release);

        for (auto& v : voices)
            synth.addVoice (v = new dust::SineVoice());
        synth.addSound (new dust::SineSound());
    }
}

bool DustLoopsProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();
    const auto out = layouts.getMainOutputChannelSet();
    if (out != mono && out != stereo)
        return false;

    const auto in = layouts.getMainInputChannelSet();
    if constexpr (dust::kIsFx)
        return in == mono || in == stereo;
    else
        return in.isDisabled() || in == mono || in == stereo; // sidechain is optional
}

void DustLoopsProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);
    inputScratch.setSize (2, std::max (samplesPerBlock, 512));
    outputLimiter.prepare ((float) sampleRate);

    const auto initGain = [sampleRate] (juce::SmoothedValue<float>& s, std::atomic<float>* p)
    {
        s.reset (sampleRate, 0.05);
        s.setCurrentAndTargetValue (p != nullptr ? juce::Decibels::decibelsToGain (p->load(), -47.9f) : 0.0f);
    };
    initGain (masterGain, masterParam);
    initGain (inputGain, inputGainParam);
    initGain (synthGain, synthLevelParam);
}

void DustLoopsProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto numSamples = buffer.getNumSamples();
    const auto numIn = getTotalNumInputChannels();
    const auto numOut = getTotalNumOutputChannels();

    // Take a copy of the input (FX main input, or the instrument's sidechain) before the buffer is reused for output.
    if (inputScratch.getNumSamples() < numSamples)
        inputScratch.setSize (2, numSamples, false, false, true); // only if the host exceeds its announced block size

    for (int ch = 0; ch < 2; ++ch)
    {
        if (numIn > 0)
            inputScratch.copyFrom (ch, 0, buffer, std::min (ch, numIn - 1), 0, numSamples);
        else
            inputScratch.clear (ch, 0, numSamples);
    }

    buffer.clear();

    if constexpr (! dust::kIsFx)
    {
        const juce::ADSR::Parameters env { attackParam->load(), decayParam->load(), sustainParam->load(), releaseParam->load() };
        for (auto* v : voices)
            v->envelopeParams = env;
        synth.renderNextBlock (buffer, midi, 0, numSamples);
        synthGain.setTargetValue (juce::Decibels::decibelsToGain (synthLevelParam->load(), -47.9f));
        synthGain.applyGain (buffer, numSamples);
    }

    inputGain.setTargetValue (juce::Decibels::decibelsToGain (inputGainParam->load(), -47.9f));
    inputGain.applyGain (inputScratch, numSamples);
    for (int ch = 0; ch < numOut; ++ch)
        buffer.addFrom (ch, 0, inputScratch, std::min (ch, 1), 0, numSamples);

    masterGain.setTargetValue (juce::Decibels::decibelsToGain (masterParam->load(), -47.9f));
    masterGain.applyGain (buffer, numSamples);

    auto* left = buffer.getWritePointer (0);
    auto* right = numOut > 1 ? buffer.getWritePointer (1) : nullptr;
    outputLimiter.process (left, right, numSamples);

    for (int ch = 0; ch < 2; ++ch)
        outputPeak[ch].store (buffer.getMagnitude (std::min (ch, numOut - 1), 0, numSamples));
}

juce::AudioProcessorEditor* DustLoopsProcessor::createEditor()
{
    return new DustLoopsEditor (*this);
}

void DustLoopsProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void DustLoopsProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (stateType))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            if (! state.hasProperty (pedalOrderProp))
                state.setProperty (pedalOrderProp, defaultPedalOrder, nullptr);
            apvts.replaceState (state);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DustLoopsProcessor();
}
