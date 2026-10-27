// Unit test runner. Suites register themselves as static juce::UnitTest instances in this file
// (and, from milestone 3 on, in the tests/*.cpp files listed in CMakeLists.txt).
#include <juce_audio_processors/juce_audio_processors.h>
#include "Plugin/PluginProcessor.h"
#include "Core/Params.h"

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;

void prepare (DustLoopsProcessor& p)
{
    p.setPlayConfigDetails (2, 2, sampleRate, blockSize);
    p.prepareToPlay (sampleRate, blockSize);
}

bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (ch, i)))
                return false;
    return true;
}

struct SafetyTests : juce::UnitTest
{
    SafetyTests() : UnitTest ("Safety", "DSP") {}

    void runTest() override
    {
        beginTest ("sanitize removes NaN and inf");
        expectEquals (dust::sanitize (std::numeric_limits<float>::quiet_NaN()), 0.0f);
        expectEquals (dust::sanitize (std::numeric_limits<float>::infinity()), 0.0f);
        expectEquals (dust::sanitize (0.5f), 0.5f);

        beginTest ("output limiter holds the ceiling");
        dust::OutputLimiter lim;
        lim.prepare ((float) sampleRate);
        std::vector<float> l (4096), r (4096);
        for (size_t i = 0; i < l.size(); ++i)
            l[i] = r[i] = 40.0f * std::sin ((float) i * 0.05f);
        lim.process (l.data(), r.data(), (int) l.size());
        for (auto v : l)
            expect (std::abs (v) <= dust::OutputLimiter::ceiling + 1.0e-6f);
    }
};

struct ProcessorTests : juce::UnitTest
{
    ProcessorTests() : UnitTest ("Processor", "Plugin") {}

    void runTest() override
    {
        beginTest ("silence in gives silence out");
        {
            DustLoopsProcessor p;
            prepare (p);
            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;
            float peak = 0.0f;
            for (int block = 0; block < 400; ++block)
            {
                buffer.clear();
                p.processBlock (buffer, midi);
                peak = std::max (peak, buffer.getMagnitude (0, blockSize));
            }
            expectEquals (peak, 0.0f);
        }

        beginTest ("a note makes sound, stays finite and under the ceiling");
        {
            DustLoopsProcessor p;
            prepare (p);
            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
            float peak = 0.0f;
            bool finite = true;
            for (int block = 0; block < 100; ++block)
            {
                buffer.clear();
                p.processBlock (buffer, midi);
                midi.clear();
                peak = std::max (peak, buffer.getMagnitude (0, blockSize));
                finite = finite && allFinite (buffer);
            }
            expect (finite);
            expectGreaterThan (peak, 0.01f);
            expectLessOrEqual (peak, dust::OutputLimiter::ceiling);
        }

        beginTest ("parameter IDs are unique and sectioned");
        {
            DustLoopsProcessor p;
            juce::StringArray ids;
            for (auto* param : p.getParameters())
                if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
                    ids.add (withId->paramID);
            juce::StringArray unique (ids);
            unique.removeDuplicates (false);
            expectEquals (unique.size(), ids.size());
            for (auto& id : ids)
                expect (id.contains ("."), id + " has no section prefix");
            logMessage (juce::String (ids.size()) + " parameters");
        }
    }
};

SafetyTests safetyTests;
ProcessorTests processorTests;
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit; // the processor owns an APVTS, which needs the message manager
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runAllTests();

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;
    std::cout << (failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED: " + std::to_string (failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
