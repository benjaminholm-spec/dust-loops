#include "TestHelpers.h"
#include "Core/Params.h"

// What happens around the audio: interface changes, saving and reloading, odd host call orders.
// A recording on the tape must never be lost.
namespace
{
using namespace test;

// Records a chord onto the tape, then freezes it so the loops hold exactly what was recorded.
void recordAndFreeze (DustLoopsProcessor& p, double rate)
{
    setParam (p, ID::pedal::id (4, ID::pedal::on), 0.0f); // no reverb tail to confuse the measurement
    setParam (p, ID::synth::release, 0.05f);
    setParam (p, ID::tape::mix, 1.0f);
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    for (int note : { 48, 55, 60, 64 })
        midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.9f), 0);
    for (int b = 0; b < (int) (3.0 * rate / blockSize); ++b)
    {
        buffer.clear();
        p.processBlock (buffer, midi);
        midi.clear();
    }
    for (int note : { 48, 55, 60, 64 })
        midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
    setParam (p, ID::tape::freeze, 1.0f);
    buffer.clear();
    p.processBlock (buffer, midi);
}

// RMS of what plays back over a full pass of the loops, with nothing new going in.
float tapeLevel (DustLoopsProcessor& p, double rate, float seconds = 9.0f)
{
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    double sum = 0.0;
    const auto blocks = (int) (seconds * rate / blockSize);
    for (int b = 0; b < blocks; ++b)
    {
        buffer.clear();
        p.processBlock (buffer, midi);
        for (int i = 0; i < blockSize; ++i)
            sum += (double) buffer.getSample (0, i) * buffer.getSample (0, i);
    }
    return (float) std::sqrt (sum / (blocks * blockSize));
}

void prepareAt (DustLoopsProcessor& p, double rate, int block = blockSize)
{
    p.setPlayConfigDetails (2, 2, rate, block);
    p.prepareToPlay (rate, block);
}

class RobustnessTests : public juce::UnitTest
{
public:
    RobustnessTests() : juce::UnitTest ("Robustness", "Dust Loops") {}

    void runTest() override
    {
        beginTest ("the tape survives an audio interface change (new sample rate and block size)");
        {
            DustLoopsProcessor p;
            prepareAt (p, 48000.0);
            recordAndFreeze (p, 48000.0);
            const auto before = tapeLevel (p, 48000.0);
            expectGreaterThan (before, 0.01f, "nothing was recorded");

            for (auto [rate, block] : { std::pair { 44100.0, 1024 }, std::pair { 96000.0, 128 }, std::pair { 48000.0, 256 } })
            {
                p.releaseResources();
                prepareAt (p, rate, block);
                const auto after = tapeLevel (p, rate);
                expectWithinAbsoluteError (after, before, before * 0.25f,
                                           "tape level changed after switching to " + juce::String (rate) + " Hz");
            }
        }

        beginTest ("the tape is saved with the project and comes back on load");
        {
            DustLoopsProcessor p;
            prepareAt (p, 48000.0);
            recordAndFreeze (p, 48000.0);
            const auto before = tapeLevel (p, 48000.0);
            juce::MemoryBlock saved;
            p.getStateInformation (saved);
            expectGreaterThan ((int) saved.getSize(), 100000, "the tape isn't in the saved state");

            // Loaded before prepareToPlay (how Logic opens a project)...
            DustLoopsProcessor reopened;
            reopened.setStateInformation (saved.getData(), (int) saved.getSize());
            prepareAt (reopened, 44100.0);
            expectWithinAbsoluteError (tapeLevel (reopened, 44100.0), before, before * 0.25f);

            // ...and loaded into an instance that is already running.
            DustLoopsProcessor running;
            prepareAt (running, 48000.0);
            running.setStateInformation (saved.getData(), (int) saved.getSize());
            expectWithinAbsoluteError (tapeLevel (running, 48000.0), before, before * 0.25f);

            // The loop arms and settings come back too.
            setParam (p, ID::loop::rec[1], 0.0f);
            p.getStateInformation (saved);
            DustLoopsProcessor again;
            again.setStateInformation (saved.getData(), (int) saved.getSize());
            expect (again.apvts.getRawParameterValue (ID::loop::rec[1])->load() < 0.5f);
            expect (again.apvts.getRawParameterValue (ID::loop::rec[0])->load() >= 0.5f);
        }

        beginTest ("old saved states (settings only, no tape) still load");
        {
            DustLoopsProcessor p;
            setParam (p, ID::tape::feed, 0.33f);
            juce::MemoryBlock oldStyle;
            if (auto xml = p.apvts.copyState().createXml())
                juce::AudioProcessor::copyXmlToBinary (*xml, oldStyle);
            DustLoopsProcessor loaded;
            loaded.setStateInformation (oldStyle.getData(), (int) oldStyle.getSize());
            expectWithinAbsoluteError (loaded.apvts.getRawParameterValue (ID::tape::feed)->load(), 0.33f, 0.001f);
        }

        beginTest ("garbage state is ignored safely");
        {
            DustLoopsProcessor p;
            juce::Random r (99);
            for (int n = 0; n < 50; ++n)
            {
                juce::MemoryBlock junk ((size_t) r.nextInt (5000));
                r.fillBitsRandomly (junk.getData(), junk.getSize());
                if (junk.getSize() > 4 && n % 2 == 0)
                    static_cast<juce::uint32*> (junk.getData())[0] = 0x32534c44; // a valid header with junk after it
                p.setStateInformation (junk.getData(), (int) junk.getSize());
            }
            prepareAt (p, 48000.0);
            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;
            p.processBlock (buffer, midi);
            expect (allFinite (buffer));
        }

        beginTest ("odd host call orders don't crash: process before prepare, huge blocks, re-prepare");
        {
            DustLoopsProcessor p;
            juce::AudioBuffer<float> buffer (2, 4096);
            juce::MidiBuffer midi;
            buffer.clear();
            p.processBlock (buffer, midi); // never prepared: silence
            expectEquals (buffer.getMagnitude (0, 4096), 0.0f);

            prepareAt (p, 48000.0, 64);
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
            p.processBlock (buffer, midi); // a block 64x bigger than announced
            expect (allFinite (buffer));
            for (int i = 0; i < 20; ++i)
            {
                prepareAt (p, i % 2 == 0 ? 44100.0 : 96000.0, 32 << (i % 6));
                buffer.clear();
                p.processBlock (buffer, midi);
                expect (allFinite (buffer));
            }
        }

        beginTest ("presets never change which loops record");
        {
            DustLoopsProcessor p;
            for (bool deck : { false, true })
            {
                p.presets->setLoadsDeck (deck, false);
                setParam (p, ID::tape::select, 1.0f); // REC key arms loop B only
                setParam (p, ID::loop::rec[0], 0.0f);
                setParam (p, ID::loop::rec[1], 1.0f);
                setParam (p, ID::loop::rec[2], 0.0f);
                for (int i = 0; i < p.presets->numFactory(); ++i)
                {
                    p.presets->load (i);
                    expectEquals ((int) p.apvts.getRawParameterValue (ID::tape::select)->load(), 1);
                    expect (p.apvts.getRawParameterValue (ID::loop::rec[0])->load() < 0.5f);
                    expect (p.apvts.getRawParameterValue (ID::loop::rec[1])->load() >= 0.5f);
                    expect (p.apvts.getRawParameterValue (ID::loop::rec[2])->load() < 0.5f);
                }
            }
        }
    }
};

RobustnessTests robustnessTests;
} // namespace
