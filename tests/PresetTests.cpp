#include "TestHelpers.h"
#include "Core/Params.h"

namespace
{
using namespace test;

float realValue (DustLoopsProcessor& p, const juce::String& id)
{
    auto* param = p.apvts.getParameter (id);
    return param->convertFrom0to1 (param->getValue());
}

class PresetTests : public juce::UnitTest
{
public:
    PresetTests() : juce::UnitTest ("Presets", "Dust Loops") {}

    void runTest() override
    {
        DustLoopsProcessor p;
        auto& pm = *p.presets;
        const auto& list = pm.presets();

        beginTest ("by default a preset changes only the sound: never the tape, the pedals or REC");
        {
            pm.setLoadsDeck (false, false);
            setParam (p, ID::tape::feed, 0.123f);
            setParam (p, ID::loop::rec[0], 0.0f);
            setParam (p, ID::loop::rec[1], 1.0f);
            setParam (p, ID::pedal::id (0, ID::pedal::on), 1.0f);
            p.setPedalOrder (DustLoopsProcessor::orderFromString ("hall,tape,cloud,drift,warble,dirt"));
            for (int i = 0; i < pm.numFactory(); i += 7)
            {
                pm.load (i);
                expectWithinAbsoluteError (realValue (p, ID::tape::feed), 0.123f, 1.0e-3f);
                expectEquals (realValue (p, ID::loop::rec[0]), 0.0f, "a preset armed a loop");
                expectEquals (realValue (p, ID::loop::rec[1]), 1.0f);
                expectEquals (realValue (p, ID::pedal::id (0, ID::pedal::on)), 1.0f);
                expectEquals (DustLoopsProcessor::orderToString (p.getPedalOrder()), juce::String ("hall,tape,cloud,drift,warble,dirt"));
            }
            const auto& data = list[14].data["params"];
            pm.load (14);
            expectWithinAbsoluteError (realValue (p, ID::synth::cutoff), (float) (double) data[ID::synth::cutoff], 0.5f);
            setParam (p, ID::tape::feed, 0.5f);
            expect (! pm.isModified(), "tape changes don't edit a sound-only preset");
            setParam (p, ID::synth::resonance, 0.9f);
            expect (pm.isModified());
            pm.setLoadsDeck (true, false); // the rest of the tests load everything
        }

        beginTest ("at least 60 factory presets, grouped by instrument type");
        {
            expectGreaterOrEqual (pm.numFactory(), 60);
            std::map<juce::String, int> perCategory;
            for (int i = 0; i < pm.numFactory(); ++i)
            {
                ++perCategory[list[(size_t) i].bank];
                expect (PresetManager::factoryBanks().contains (list[(size_t) i].bank), list[(size_t) i].name + ": unknown category");
            }
            for (auto& category : PresetManager::factoryBanks())
                expectGreaterOrEqual (perCategory[category], 3, category + " has too few presets");
            expectEquals (p.getNumPrograms(), pm.numFactory());

            // Sorted by category, in browser order
            int lastRank = -1;
            for (int i = 0; i < pm.numFactory(); ++i)
            {
                const auto rank = PresetManager::factoryBanks().indexOf (list[(size_t) i].bank);
                expectGreaterOrEqual (rank, lastRank);
                lastRank = rank;
            }
        }

        beginTest ("every stored value names a real parameter and a valid choice");
        {
            for (int i = 0; i < pm.numFactory(); ++i)
            {
                const auto& preset = list[(size_t) i];
                expect (preset.name.isNotEmpty() && preset.description.isNotEmpty(), "preset " + juce::String (i) + " needs a name and description");
                auto* params = preset.data["params"].getDynamicObject();
                expect (params != nullptr, preset.name);
                if (params == nullptr) continue;
                for (auto& prop : params->getProperties())
                {
                    const auto id = prop.name.toString();
                    auto* param = p.apvts.getParameter (id);
                    expect (param != nullptr, preset.name + ": unknown parameter " + id);
                    if (param == nullptr) continue;
                    expect (PresetManager::isStored (id), preset.name + ": stores " + id);
                    if (prop.value.isString())
                        expect (param->getAllValueStrings().contains (prop.value.toString()),
                                preset.name + ": bad choice " + prop.value.toString() + " for " + id);
                    else if (! prop.value.isBool())
                    {
                        const auto range = param->getNormalisableRange();
                        const auto v = (float) (double) prop.value;
                        expect (v >= range.start && v <= range.end, preset.name + ": " + id + " out of range");
                    }
                }
                expectEquals (DustLoopsProcessor::orderToString (DustLoopsProcessor::orderFromString (preset.data["pedalOrder"].toString())),
                              preset.data["pedalOrder"].toString(), preset.name + ": bad pedal order");
            }
        }

        beginTest ("loading sets the stored values and resets everything else to default");
        {
            setParam (p, ID::synth::glide, 1.7f);
            setParam (p, ID::tape::freeze, 1.0f);
            setParam (p, ID::loop::rec[2], 0.0f);
            const auto index = 1;
            pm.load (index);
            const auto& data = list[(size_t) index].data["params"];
            expectWithinAbsoluteError (realValue (p, ID::synth::cutoff), (float) (double) data[ID::synth::cutoff], 0.5f);
            auto* glide = p.apvts.getParameter (ID::synth::glide);
            expectWithinAbsoluteError (glide->getValue(), glide->getDefaultValue(), 1.0e-5f);
            expectEquals (realValue (p, ID::tape::freeze), 1.0f, "FREEZE must not be touched by presets");
            expectEquals (realValue (p, ID::loop::rec[2]), 0.0f, "REC must not be touched by presets");
            setParam (p, ID::loop::rec[2], 1.0f);
            expect (! pm.isModified());
            expectEquals (pm.currentIndex(), index);
            setParam (p, ID::tape::freeze, 0.0f);
            setParam (p, ID::synth::cutoff, 400.0f);
            expect (pm.isModified());
        }

        beginTest ("a captured sound loads back exactly, pedal order included");
        {
            pm.load (5);
            setParam (p, ID::synth::resonance, 0.77f);
            setParam (p, ID::synth::model, 2.0f);
            p.setPedalOrder (DustLoopsProcessor::orderFromString ("hall,tape,cloud,drift,warble,dirt"));
            const auto snapshot = pm.capture ("Test", "User", "");
            pm.load (0);
            pm.applyPreset (snapshot);
            expectWithinAbsoluteError (realValue (p, ID::synth::resonance), 0.77f, 1.0e-3f);
            expectEquals ((int) realValue (p, ID::synth::model), 2);
            expectEquals (DustLoopsProcessor::orderToString (p.getPedalOrder()), juce::String ("hall,tape,cloud,drift,warble,dirt"));
        }

        beginTest ("the project remembers which preset was loaded");
        {
            pm.load (7);
            juce::MemoryBlock block;
            p.getStateInformation (block);
            DustLoopsProcessor other;
            other.setStateInformation (block.getData(), (int) block.getSize());
            expectEquals (other.presets->currentIndex(), 7);
            expect (! other.presets->isModified());
        }

        beginTest ("search and favourites filter the list");
        {
            expectGreaterThan ((int) pm.filter ("", "rain").size(), 0);
            expectEquals ((int) pm.filter ("Utility", "").size(), 7);
            expectEquals ((int) pm.filter ("", "zzzz no such preset").size(), 0);
            const auto all = pm.filter ("", "");
            expectEquals ((int) all.size(), (int) list.size());
        }

        beginTest ("every factory preset plays: audible, finite, under the ceiling");
        {
            prepare (p);
            juce::AudioBuffer<float> buffer (2, blockSize);
            for (int i = 0; i < pm.numFactory(); ++i)
            {
                pm.load (i);
                p.prepareToPlay (sampleRate, blockSize); // fresh tape for each preset
                juce::MidiBuffer midi;
                for (int note : { 48, 55, 60, 64 })
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.8f), 0);
                float peak = 0.0f;
                double sum = 0.0;
                bool finite = true;
                const auto blocks = (int) (3.0 * sampleRate / blockSize);
                for (int b = 0; b < blocks; ++b)
                {
                    buffer.clear();
                    p.processBlock (buffer, midi);
                    midi.clear();
                    finite = finite && allFinite (buffer);
                    peak = std::max (peak, buffer.getMagnitude (0, blockSize));
                    for (int s = 0; s < blockSize; ++s)
                        sum += (double) buffer.getSample (0, s) * buffer.getSample (0, s);
                }
                const auto rms = std::sqrt (sum / (blocks * blockSize));
                const auto& name = list[(size_t) i].name;
                expect (finite, name + " made NaN/inf");
                expectLessOrEqual (peak, dust::OutputLimiter::ceiling, name);
                expectGreaterThan (rms, 0.003, name + " is nearly silent");
            }
        }
    }
};

PresetTests presetTests;
} // namespace
