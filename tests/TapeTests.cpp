#include "TestHelpers.h"
#include "Core/Params.h"

namespace
{
using namespace test;

// Drives a TapeMachine directly with a test signal.
struct Rig
{
    dust::TapeMachine tape;
    dust::TapeSettings settings;
    std::vector<float> inL, inR, outL, outR;

    Rig()
    {
        tape.prepare (sampleRate);
        inL.resize (blockSize); inR.resize (blockSize); outL.resize (blockSize); outR.resize (blockSize);
    }

    // Returns the peak of the output over `seconds`. `source` fills the input.
    template <typename Source>
    float run (double seconds, Source&& source, bool* finite = nullptr)
    {
        float peak = 0.0f;
        const auto blocks = (int) (seconds * sampleRate / blockSize);
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < blockSize; ++i)
                source (inL[(size_t) i], inR[(size_t) i]);
            tape.process (inL.data(), inR.data(), outL.data(), outR.data(), blockSize, settings);
            for (int i = 0; i < blockSize; ++i)
            {
                if (finite != nullptr && ! (std::isfinite (outL[(size_t) i]) && std::isfinite (outR[(size_t) i])))
                    *finite = false;
                peak = std::max ({ peak, std::abs (outL[(size_t) i]), std::abs (outR[(size_t) i]) });
            }
        }
        return peak;
    }

    float runSilence (double seconds) { return run (seconds, [] (float& l, float& r) { l = r = 0.0f; }); }
};

struct TapeTests : juce::UnitTest
{
    TapeTests() : UnitTest ("Tape", "DSP") {}

    void runTest() override
    {
        beginTest ("silence in gives silence out (hiss off), in both modes");
        for (int mode : { 0, 1 })
        {
            DustLoopsProcessor p;
            prepare (p);
            setParam (p, ID::tape::hiss, 0.0f);
            setParam (p, ID::tape::mode, (float) mode);
            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;
            float peak = 0.0f;
            for (int block = 0; block < 2000; ++block)
            {
                buffer.clear();
                p.processBlock (buffer, midi);
                peak = std::max (peak, buffer.getMagnitude (0, blockSize));
            }
            expectEquals (peak, 0.0f);
        }

        beginTest ("hiss on: silence in gives only quiet hiss");
        {
            Rig rig;
            rig.settings.hiss = 1.0f;
            rig.settings.dropouts = 0.0f;
            const auto peak = rig.runSilence (5.0);
            expectGreaterThan (peak, 0.0f);
            expectLessThan (peak, 0.05f);
        }

        beginTest ("loop feedback at maximum stays bounded and finite");
        for (int mode : { 0, 1 })
        {
            Rig rig;
            auto& s = rig.settings;
            s.mode = mode;
            s.feed = 1.0f; s.saturation = 1.0f; s.age = 0.0f; s.hiss = 1.0f; s.dropouts = 1.0f;
            s.wow = 1.0f; s.flutter = 1.0f; s.volume = 4.0f; s.mix = 1.0f; s.time = 0.0f; // short loops: many passes
            for (auto& l : s.loops) { l.gain = 2.0f; l.speedSemis = 12.0f; }
            s.speed = 2.0f;
            dust::Random r (77);
            bool finite = true;
            const auto loud = rig.run (20.0, [&r] (float& l, float& rr) { l = 4.0f * r.bipolar(); rr = 4.0f * r.bipolar(); }, &finite);
            const auto tail = rig.runSilence (20.0);
            expect (finite);
            // Worst case for the raw tape output: 3 loops x gain 2 x volume 4 x limited tape (< 1), plus
            // up to ~25 % overshoot from cubic interpolation of full-scale noise. And it must not grow.
            expectLessThan (loud, 30.0f);
            expectLessThan (tail, std::max (loud, 1.0f) * 1.05f);
            logMessage ((mode == 0 ? juce::String ("loop") : juce::String ("echo")) + " max-feedback peak " + juce::String (tail, 2));
        }

        beginTest ("freeze holds the loop without decay");
        {
            Rig rig;
            auto& s = rig.settings;
            s.sync = false; s.time = 0.5f; s.wow = s.flutter = s.hiss = s.dropouts = s.age = s.saturation = 0.0f;
            s.mix = 1.0f; s.feed = 0.5f;
            for (auto& l : s.loops) l.seconds = 1.0f;
            dust::Random r (5);
            rig.run (3.0, [&r] (float& l, float& rr) { l = rr = 0.3f * r.bipolar(); });
            s.freeze = true;
            const auto first = rig.runSilence (2.0);
            const auto later = rig.runSilence (10.0);
            expectGreaterThan (first, 0.05f);
            expectWithinAbsoluteError (later, first, first * 0.1f);
        }

        beginTest ("loop lengths follow host tempo exactly in SYNC");
        {
            DustLoopsProcessor p;
            FakePlayHead head;
            head.bpm = 97.0;
            p.setPlayHead (&head);
            prepare (p);
            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;
            p.processBlock (buffer, midi);
            const int beats[] { 16, 13, 11 };
            for (int i = 0; i < 3; ++i)
                expectEquals (p.getTape().getLoopLength (i), (int) std::round (beats[i] * 60.0 / 97.0 * sampleRate));
            p.setPlayHead (nullptr);
        }

        beginTest ("heads advance one cell per sample at speed 1 and realign on host play");
        {
            DustLoopsProcessor p;
            FakePlayHead head;
            head.bpm = 120.0;
            p.setPlayHead (&head);
            prepare (p);
            setParam (p, ID::tape::wow, 0.0f);
            setParam (p, ID::tape::flutter, 0.0f);
            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;
            for (int b = 0; b < (int) (10.0 * sampleRate / blockSize); ++b) // let the motor settle
                p.processBlock (buffer, midi);

            const auto length = p.getTape().getLoopLength (0);
            expectEquals (length, 384000); // 16 beats at 120 bpm, 48 kHz
            const auto before = p.getTape().getHeadPosition (0);
            const int blocks = 1500;
            for (int b = 0; b < blocks; ++b)
                p.processBlock (buffer, midi);
            auto moved = p.getTape().getHeadPosition (0) - before;
            if (moved < 0.0) moved += length;
            expectWithinAbsoluteError (moved, (double) blocks * blockSize, 0.5);

            head.playing = true;
            p.processBlock (buffer, midi);
            expectLessThan (p.getTape().getHeadPosition (0), (double) blockSize + 1.0);
            p.setPlayHead (nullptr);
        }

        beginTest ("no NaN/inf and nothing over the ceiling under random automation");
        {
            DustLoopsProcessor p;
            FakePlayHead head;
            p.setPlayHead (&head);
            prepare (p);
            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;
            juce::Random r (1234);
            auto params = p.getParameters();
            bool finite = true;
            float peak = 0.0f;
            for (int block = 0; block < 6000; ++block)
            {
                for (int k = 0; k < 8; ++k)
                    params[r.nextInt (params.size())]->setValueNotifyingHost (r.nextFloat());
                if (block % 50 == 0)
                {
                    head.bpm = 20.0 + r.nextDouble() * 300.0;
                    head.playing = r.nextBool();
                }
                midi.clear();
                if (r.nextInt (10) == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 30 + r.nextInt (60), r.nextFloat()), r.nextInt (blockSize));
                if (r.nextInt (10) == 0)
                    midi.addEvent (juce::MidiMessage::allNotesOff (1), r.nextInt (blockSize));
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < blockSize; ++i)
                        buffer.setSample (ch, i, r.nextFloat() * 2.0f - 1.0f);
                p.processBlock (buffer, midi);
                finite = finite && allFinite (buffer);
                peak = std::max (peak, buffer.getMagnitude (0, blockSize));
            }
            expect (finite);
            expectLessOrEqual (peak, dust::OutputLimiter::ceiling);
            p.setPlayHead (nullptr);
        }
    }
};

TapeTests tapeTests;
} // namespace
