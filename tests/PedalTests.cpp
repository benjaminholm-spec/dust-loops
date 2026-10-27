#include "TestHelpers.h"
#include "Core/Params.h"

namespace
{
using namespace test;

float rms (const std::vector<float>& v)
{
    double sum = 0.0;
    for (auto x : v) sum += (double) x * x;
    return (float) std::sqrt (sum / (double) std::max<size_t> (1, v.size()));
}

struct PedalTests : juce::UnitTest
{
    PedalTests() : UnitTest ("Pedals", "DSP") {}

    void runTest() override
    {
        beginTest ("each pedal alone stays finite and bounded on loud noise");
        for (int pedal = 0; pedal < 5; ++pedal)
        {
            dust::PedalBoard board;
            board.prepare ((float) sampleRate, blockSize);
            dust::PedalBoardSettings s;
            s.on[(size_t) pedal] = true;
            s.dirt = { 1.0f, 1.0f, 1.0f, 1.0f };
            s.warble = { 12.0f, 1.0f, 1.0f, 1 };
            s.drift = { 12000.0f, 1.0f, 2.0f, 1.0f, 1.0f, 1 };
            s.cloud = { 3, 1.0f, 1.0f, 1.0f };
            s.hall = { 60.0f, 1.0f, 1.0f, false };
            std::vector<float> l (blockSize), r (blockSize);
            dust::Random rng (9);
            float peak = 0.0f;
            bool finite = true;
            for (int b = 0; b < (int) (20.0 * sampleRate / blockSize); ++b)
            {
                for (int i = 0; i < blockSize; ++i) { l[(size_t) i] = 2.0f * rng.bipolar(); r[(size_t) i] = 2.0f * rng.bipolar(); }
                board.processMain (pedal, l.data(), r.data(), blockSize, s);
                for (int i = 0; i < blockSize; ++i)
                {
                    finite = finite && std::isfinite (l[(size_t) i]) && std::isfinite (r[(size_t) i]);
                    peak = std::max ({ peak, std::abs (l[(size_t) i]), std::abs (r[(size_t) i]) });
                }
            }
            expect (finite, "pedal " + juce::String (pedal));
            expectLessThan (peak, 40.0f, "pedal " + juce::String (pedal));
        }

        beginTest ("hall freeze holds its tail");
        {
            dust::Hall hall;
            hall.prepare ((float) sampleRate);
            dust::Hall::Settings s { 4.0f, 0.5f, 1.0f, false };
            std::vector<float> l (blockSize), r (blockSize);
            dust::Random rng (3);
            for (int b = 0; b < 100; ++b)
            {
                for (int i = 0; i < blockSize; ++i) l[(size_t) i] = r[(size_t) i] = 0.3f * rng.bipolar();
                hall.process (l.data(), r.data(), blockSize, s);
            }
            s.freeze = true;
            const auto measure = [&] (double seconds)
            {
                float level = 0.0f;
                for (int b = 0; b < (int) (seconds * sampleRate / blockSize); ++b)
                {
                    std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
                    hall.process (l.data(), r.data(), blockSize, s);
                    level = rms (l);
                }
                return level;
            };
            const auto early = measure (1.0);
            const auto late = measure (20.0);
            expectGreaterThan (early, 0.001f);
            expectGreaterThan (late, early * 0.7f);
        }

        beginTest ("everything maxed inside the tape loop never runs away");
        {
            DustLoopsProcessor p;
            prepare (p);
            for (auto* param : p.getParameters())
                if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
                {
                    const auto id = withId->paramID;
                    if (id.startsWith ("pedal.") && ! id.endsWith ("freeze") && ! id.endsWith ("pitch") && ! id.endsWith ("mode"))
                        param->setValueNotifyingHost (1.0f);
                }
            setParam (p, ID::tape::feed, 1.0f);
            setParam (p, ID::tape::saturation, 1.0f);
            setParam (p, ID::tape::age, 0.0f);
            setParam (p, ID::tape::time, 0.0f);
            setParam (p, ID::tape::volume, 12.0f);
            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;
            juce::Random rng (11);
            bool finite = true;
            float tapePeak = 0.0f;
            for (int block = 0; block < (int) (60.0 * sampleRate / blockSize); ++block)
            {
                const auto loud = block < (int) (20.0 * sampleRate / blockSize);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < blockSize; ++i)
                        buffer.setSample (ch, i, loud ? 2.0f * (rng.nextFloat() * 2.0f - 1.0f) : 0.0f);
                p.processBlock (buffer, midi);
                finite = finite && allFinite (buffer);
                for (size_t i = 0; i < 3; ++i)
                    tapePeak = std::max (tapePeak, p.getTape().loopPeak[i].load());
            }
            expect (finite);
            expectLessThan (tapePeak, 1.5f); // what is on the tape stays limited
            logMessage ("tape peak with everything maxed: " + juce::String (tapePeak, 3));
        }

        beginTest ("bypassed pedals and a dry mix are transparent");
        {
            DustLoopsProcessor p;
            prepare (p);
            setParam (p, ID::pedal::id (4, ID::pedal::on), 0.0f); // HALL is on by default
            setParam (p, ID::tape::mix, 0.0f);
            juce::AudioBuffer<float> buffer (2, blockSize), reference (2, blockSize);
            juce::MidiBuffer midi;
            juce::Random rng (5);
            float maxError = 0.0f;
            for (int block = 0; block < 400; ++block)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < blockSize; ++i)
                        buffer.setSample (ch, i, 0.5f * (rng.nextFloat() * 2.0f - 1.0f));
                reference.makeCopyOf (buffer);
                p.processBlock (buffer, midi);
                if (block > 50) // after the hall's bypass fade
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < blockSize; ++i)
                            maxError = std::max (maxError, std::abs (buffer.getSample (ch, i) - reference.getSample (ch, i)));
            }
            expectLessThan (maxError, 1.0e-5f);
        }

        beginTest ("pedal order round-trips through text and saved state");
        {
            using Order = DustLoopsProcessor::PedalOrder;
            const Order custom { 4, 5, 0, 3, 1, 2 };
            expect (DustLoopsProcessor::orderFromString (DustLoopsProcessor::orderToString (custom)) == custom);
            const auto fallback = DustLoopsProcessor::orderFromString ("hall,nonsense,hall");
            expectEquals (fallback[0], 4);
            expectEquals (DustLoopsProcessor::orderToString (fallback), juce::String ("hall,dirt,warble,drift,tape,cloud"));

            DustLoopsProcessor a;
            a.setPedalOrder (custom);
            juce::MemoryBlock state;
            a.getStateInformation (state);
            DustLoopsProcessor b;
            b.setStateInformation (state.getData(), (int) state.getSize());
            expect (b.getPedalOrder() == custom);
        }
    }
};

PedalTests pedalTests;
} // namespace
