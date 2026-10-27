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

bool on (const std::atomic<float>* p) { return p->load() >= 0.5f; }
float dbToGain (const std::atomic<float>* p) { return juce::Decibels::decibelsToGain (p->load(), -47.9f); }
} // namespace

DustLoopsProcessor::DustLoopsProcessor()
    : AudioProcessor (makeBuses()),
      apvts (*this, nullptr, stateType, Params::createLayout (! dust::kIsFx))
{
    apvts.state.setProperty (pedalOrderProp, defaultPedalOrder, nullptr);

    const auto get = [this] (const juce::String& id) { return apvts.getRawParameterValue (id); };
    masterParam = get (ID::global::master);
    inputGainParam = get (ID::global::inputGain);

    using namespace ID::tape;
    tp.mode = get (mode);       tp.time = get (ID::tape::time);      tp.feed = get (feed);         tp.mix = get (mix);
    tp.volume = get (volume);   tp.speed = get (speed);     tp.inertia = get (inertia);   tp.freeze = get (freeze);
    tp.sync = get (sync);       tp.select = get (select);   tp.brake = get (brake);       tp.type = get (type);
    tp.wow = get (wow);         tp.flutter = get (flutter); tp.hiss = get (hiss);         tp.saturation = get (saturation);
    tp.age = get (age);         tp.dropouts = get (dropouts);
    tp.keyRec = get (ID::xport::rec);   tp.keyPlay = get (ID::xport::play); tp.keyRew = get (ID::xport::rew);
    tp.keyFfwd = get (ID::xport::ffwd); tp.keyStop = get (ID::xport::stop); tp.keyPause = get (ID::xport::pause);
    for (size_t i = 0; i < 3; ++i)
    {
        tp.beats[i] = get (ID::loop::beats[i]);   tp.seconds[i] = get (ID::loop::seconds[i]);
        tp.level[i] = get (ID::loop::level[i]);   tp.pan[i] = get (ID::loop::pan[i]);
        tp.speedOffset[i] = get (ID::loop::speed[i]); tp.mute[i] = get (ID::loop::mute[i]);
        tp.rec[i] = get (ID::loop::rec[i]);       tp.clear[i] = get (ID::loop::clear[i]);
    }

    using namespace ID::pedal;
    for (size_t i = 0; i < 5; ++i)
    {
        pp.on[i] = get (id ((int) i, ID::pedal::on));
        pp.loop[i] = get (id ((int) i, ID::pedal::loop));
        pp.loopAmt[i] = get (id ((int) i, ID::pedal::loopAmt));
    }
    pp.dirtDrive = get (dirt::drive);       pp.dirtBlend = get (dirt::blend);     pp.dirtTone = get (dirt::tone);
    pp.dirtMix = get (dirt::mix);           pp.warbleRate = get (warble::rate);   pp.warbleDepth = get (warble::depth);
    pp.warbleMode = get (warble::mode);     pp.warbleMix = get (warble::mix);     pp.driftFreq = get (drift::freq);
    pp.driftReso = get (drift::reso);       pp.driftMode = get (drift::mode);     pp.driftLfoRate = get (drift::lfoRate);
    pp.driftLfoDepth = get (drift::lfoDepth); pp.driftEnv = get (drift::envAmt);  pp.cloudPitch = get (cloud::pitch);
    pp.cloudSmear = get (cloud::smear);     pp.cloudDensity = get (cloud::density); pp.cloudMix = get (cloud::mix);
    pp.hallDecay = get (hall::decay);       pp.hallTone = get (hall::tone);       pp.hallMix = get (hall::mix);
    pp.hallFreeze = get (hall::freeze);

    setPedalOrder (orderFromString (defaultPedalOrder));

    if constexpr (! dust::kIsFx)
    {
        synthLevelParam = get (ID::synth::level);
        attackParam = get (ID::synth::attack);
        decayParam = get (ID::synth::decay);
        sustainParam = get (ID::synth::sustain);
        releaseParam = get (ID::synth::release);

        for (auto& v : voices)
            synth.addVoice (v = new dust::SineVoice());
        synth.addSound (new dust::SineSound());
    }

    startTimerHz (30);
}

DustLoopsProcessor::~DustLoopsProcessor()
{
    stopTimer();
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
    const auto maxBlock = std::max (samplesPerBlock, 512);
    inputScratch.setSize (2, maxBlock);
    tapeIn.setSize (2, maxBlock);
    tape.prepare (sampleRate);
    pedals.prepare ((float) sampleRate, maxBlock);
    chain.setSize (2, maxBlock);
    tapeReturn.setSize (2, maxBlock);
    loopInject.setSize (2, maxBlock);
    loopInject.clear();
    outputLimiter.prepare ((float) sampleRate);

    const auto initGain = [sampleRate] (juce::SmoothedValue<float>& s, std::atomic<float>* p)
    {
        s.reset (sampleRate, 0.05);
        s.setCurrentAndTargetValue (p != nullptr ? dbToGain (p) : 0.0f);
    };
    initGain (masterGain, masterParam);
    initGain (inputGain, inputGainParam);
    initGain (synthGain, synthLevelParam);
}

void DustLoopsProcessor::readTapeSettings()
{
    auto& s = tapeSettings;
    s.mode = (int) tp.mode->load();
    s.time = tp.time->load();
    s.feed = tp.feed->load();
    s.mix = tp.mix->load();
    s.volume = dbToGain (tp.volume);
    s.speed = tp.speed->load();
    s.inertia = tp.inertia->load();
    s.brakeSeconds = tp.brake->load();
    s.freeze = on (tp.freeze);
    s.sync = (int) tp.sync->load() == 0;
    s.type = (int) tp.type->load();
    s.wow = tp.wow->load();
    s.flutter = tp.flutter->load();
    s.hiss = tp.hiss->load();
    s.saturation = tp.saturation->load();
    s.age = tp.age->load();
    s.dropouts = tp.dropouts->load();
    s.rewindHeld = on (tp.keyRew);
    s.fastForwardHeld = on (tp.keyFfwd);
    for (size_t i = 0; i < 3; ++i)
    {
        auto& l = s.loops[i];
        l.beats = tp.beats[i]->load();
        l.seconds = tp.seconds[i]->load();
        l.gain = dbToGain (tp.level[i]);
        l.pan = tp.pan[i]->load();
        l.speedSemis = tp.speedOffset[i]->load();
        l.mute = on (tp.mute[i]);
        l.armed = on (tp.rec[i]);
    }
}

void DustLoopsProcessor::readPedalSettings()
{
    auto& s = pedalSettings;
    for (size_t i = 0; i < 5; ++i)
    {
        s.on[i] = on (pp.on[i]);
        s.inLoop[i] = on (pp.loop[i]);
        s.loopAmount[i] = pp.loopAmt[i]->load();
    }
    s.dirt = { pp.dirtDrive->load(), pp.dirtBlend->load(), pp.dirtTone->load(), pp.dirtMix->load() };
    s.warble = { pp.warbleRate->load(), pp.warbleDepth->load(), pp.warbleMix->load(), (int) pp.warbleMode->load() };
    s.drift = { pp.driftFreq->load(), pp.driftReso->load(), pp.driftLfoRate->load(), pp.driftLfoDepth->load(),
                pp.driftEnv->load(), (int) pp.driftMode->load() };
    s.cloud = { (int) pp.cloudPitch->load(), pp.cloudSmear->load(), pp.cloudDensity->load(), pp.cloudMix->load() };
    s.hall = { pp.hallDecay->load(), pp.hallTone->load(), pp.hallMix->load(), on (pp.hallFreeze) };
}

DustLoopsProcessor::PedalOrder DustLoopsProcessor::orderFromString (const juce::String& text)
{
    // Unknown or missing names fall back to the default order for whatever is left.
    PedalOrder order {};
    std::array<bool, 6> used {};
    size_t n = 0;
    const auto nameToSlot = [] (const juce::String& name)
    {
        if (name == "tape") return dust::PedalBoard::tapeSlot;
        for (int i = 0; i < ID::pedal::count; ++i)
            if (name == ID::pedal::names[i]) return i;
        return -1;
    };
    for (auto& token : juce::StringArray::fromTokens (text, ",", ""))
    {
        const auto slot = nameToSlot (token.trim());
        if (slot >= 0 && ! used[(size_t) slot] && n < order.size())
        {
            used[(size_t) slot] = true;
            order[n++] = slot;
        }
    }
    for (int slot : { 0, 1, 2, dust::PedalBoard::tapeSlot, 3, 4 })
        if (! used[(size_t) slot] && n < order.size())
            order[n++] = slot;
    return order;
}

juce::String DustLoopsProcessor::orderToString (const PedalOrder& order)
{
    juce::StringArray names;
    for (auto slot : order)
        names.add (slot == dust::PedalBoard::tapeSlot ? juce::String ("tape") : juce::String (ID::pedal::names[slot]));
    return names.joinIntoString (",");
}

DustLoopsProcessor::PedalOrder DustLoopsProcessor::getPedalOrder() const noexcept
{
    const auto packed = packedOrder.load();
    PedalOrder order {};
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = (int) ((packed >> (3 * i)) & 7u);
    return order;
}

void DustLoopsProcessor::setPedalOrder (const PedalOrder& order)
{
    uint32_t packed = 0;
    for (size_t i = 0; i < order.size(); ++i)
        packed |= (uint32_t) order[i] << (3 * i);
    packedOrder.store (packed);
    apvts.state.setProperty (pedalOrderProp, orderToString (order), nullptr);
}

void DustLoopsProcessor::handleKeys()
{
    const auto edge = [] (const std::atomic<float>* p, bool& last)
    {
        const auto now = on (p);
        const auto rising = now && ! last;
        last = now;
        return rising;
    };

    if (edge (tp.keyPlay, lastPlay))   tape.pressPlay();
    if (edge (tp.keyStop, lastStop))   tape.pressStop();
    if (edge (tp.keyPause, lastPause)) tape.pressPause();
    if (edge (tp.keyRec, lastRec))     recKeyPressed.store (true);
    for (size_t i = 0; i < 3; ++i)
        if (edge (tp.clear[i], lastClear[i]))
            tape.clearLoop ((int) i);
}

void DustLoopsProcessor::timerCallback()
{
    // REC key: toggle the arm of the selected loop, or of all three when "All" is selected.
    if (recKeyPressed.exchange (false))
    {
        const auto selected = (int) tp.select->load();
        const auto first = selected == 3 ? 0 : selected, last = selected == 3 ? 2 : selected;
        bool anyArmed = false;
        for (int i = first; i <= last; ++i)
            anyArmed = anyArmed || on (tp.rec[(size_t) i]);
        for (int i = first; i <= last; ++i)
            if (auto* p = apvts.getParameter (ID::loop::rec[i]))
                p->setValueNotifyingHost (anyArmed ? 0.0f : 1.0f);
    }
}

void DustLoopsProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto numSamples = buffer.getNumSamples();
    const auto numIn = getTotalNumInputChannels();
    const auto numOut = getTotalNumOutputChannels();

    // Host tempo and transport: SYNC follows the tempo and realigns the loops when Logic starts playing.
    double bpm = 120.0;
    bool hostPlaying = false;
    if (auto* playHead = getPlayHead())
        if (auto pos = playHead->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            hostPlaying = pos->getIsPlaying();
        }
    currentBpm.store ((float) bpm);

    readTapeSettings();
    tapeSettings.bpm = bpm;
    if (hostPlaying && ! hostWasPlaying && tapeSettings.sync)
        tape.realign();
    hostWasPlaying = hostPlaying;
    handleKeys();

    // Take a copy of the input (FX main input, or the instrument's sidechain) before the buffer is reused for output.
    if (inputScratch.getNumSamples() < numSamples)
    {
        inputScratch.setSize (2, numSamples, false, false, true); // only if the host exceeds its announced block size
        tapeIn.setSize (2, numSamples, false, false, true);
    }

    for (int ch = 0; ch < 2; ++ch)
    {
        if (numIn > 0)
            inputScratch.copyFrom (ch, 0, buffer, std::min (ch, numIn - 1), 0, numSamples);
        else
            inputScratch.clear (ch, 0, numSamples);
    }

    // Everything that goes onto the tape: synth plus input.
    tapeIn.clear();
    if constexpr (! dust::kIsFx)
    {
        const juce::ADSR::Parameters env { attackParam->load(), decayParam->load(), sustainParam->load(), releaseParam->load() };
        for (auto* v : voices)
            v->envelopeParams = env;
        synth.renderNextBlock (tapeIn, midi, 0, numSamples);
        synthGain.setTargetValue (dbToGain (synthLevelParam));
        synthGain.applyGain (tapeIn, numSamples);
    }

    inputGain.setTargetValue (dbToGain (inputGainParam));
    inputGain.applyGain (inputScratch, numSamples);
    for (int ch = 0; ch < 2; ++ch)
        tapeIn.addFrom (ch, 0, inputScratch, ch, 0, numSamples);

    // The chain, in board order: pedals before TAPE, the tape (dry + wet), pedals after it. The
    // in-loop pedal copies process what the tape plays and hand it back one chunk later.
    readPedalSettings();
    const auto order = getPedalOrder();
    buffer.clear();
    const auto chunkSize = chain.getNumSamples();
    for (int start = 0; start < numSamples; start += chunkSize)
    {
        const auto n = std::min (chunkSize, numSamples - start);
        auto* l = chain.getWritePointer (0);
        auto* r = chain.getWritePointer (1);
        std::copy (tapeIn.getReadPointer (0, start), tapeIn.getReadPointer (0, start) + n, l);
        std::copy (tapeIn.getReadPointer (1, start), tapeIn.getReadPointer (1, start) + n, r);

        for (auto slot : order)
        {
            if (slot == dust::PedalBoard::tapeSlot)
                tape.process (l, r, l, r, n, tapeSettings, loopInject.getReadPointer (0), loopInject.getReadPointer (1),
                              tapeReturn.getWritePointer (0), tapeReturn.getWritePointer (1));
            else
                pedals.processMain (slot, l, r, n, pedalSettings);
        }
        pedals.processLoop (order, tapeReturn.getReadPointer (0), tapeReturn.getReadPointer (1),
                            loopInject.getWritePointer (0), loopInject.getWritePointer (1), n, pedalSettings);
        if (n < chunkSize)
            loopInject.clear (n, chunkSize - n);

        if (numOut > 1)
        {
            buffer.copyFrom (0, start, l, n);
            buffer.copyFrom (1, start, r, n);
        }
        else
            for (int i = 0; i < n; ++i)
                buffer.setSample (0, start + i, 0.5f * (l[i] + r[i]));
    }

    auto* left = buffer.getWritePointer (0);
    auto* right = numOut > 1 ? buffer.getWritePointer (1) : nullptr;

    masterGain.setTargetValue (dbToGain (masterParam));
    masterGain.applyGain (buffer, numSamples);

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
            const auto order = orderFromString (state.getProperty (pedalOrderProp, defaultPedalOrder).toString());
            apvts.replaceState (state);
            setPedalOrder (order);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DustLoopsProcessor();
}
