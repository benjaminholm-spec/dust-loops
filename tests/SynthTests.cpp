#include "TestHelpers.h"
#include "Synth/SynthEngine.h"

namespace
{
using namespace test;

struct EngineRig
{
    explicit EngineRig (double sr = sampleRate) : rate (sr) { engine.prepare (sr); }

    // Renders `seconds`, sending `midi` at the start. Returns the peak.
    float render (float seconds, juce::MidiBuffer midi = {})
    {
        float peak = 0.0f;
        const auto blocks = (int) (seconds * rate / blockSize);
        for (int b = 0; b < blocks; ++b)
        {
            engine.process (l.data(), r.data(), blockSize, b == 0 ? midi : juce::MidiBuffer(), settings);
            for (int i = 0; i < blockSize; ++i)
            {
                finite = finite && std::isfinite (l[(size_t) i]) && std::isfinite (r[(size_t) i]);
                peak = std::max ({ peak, std::abs (l[(size_t) i]), std::abs (r[(size_t) i]) });
                sumSquares += (double) l[(size_t) i] * l[(size_t) i];
                ++count;
            }
        }
        return peak;
    }

    float rms() const { return count > 0 ? (float) std::sqrt (sumSquares / (double) count) : 0.0f; }
    void resetMeter() { sumSquares = 0.0; count = 0; }

    static juce::MidiBuffer notes (std::initializer_list<int> list, bool on = true, float velocity = 0.8f)
    {
        juce::MidiBuffer m;
        for (auto n : list)
            m.addEvent (on ? juce::MidiMessage::noteOn (1, n, velocity) : juce::MidiMessage::noteOff (1, n), 0);
        return m;
    }

    double rate;
    dust::SynthEngine engine;
    dust::SynthSettings settings;
    std::array<float, blockSize> l {}, r {};
    bool finite = true;
    double sumSquares = 0.0;
    long count = 0;
};

class SynthTests : public juce::UnitTest
{
public:
    SynthTests() : juce::UnitTest ("Synth", "Dust Loops") {}

    void runTest() override
    {
        beginTest ("no notes and no Atmos gives exact silence");
        {
            EngineRig rig;
            for (int model = 0; model < 3; ++model)
            {
                rig.settings.model = model;
                expectEquals (rig.render (1.0f), 0.0f);
            }
        }

        beginTest ("every model makes sound, stays finite and goes quiet after release");
        {
            for (int model = 0; model < 3; ++model)
            {
                EngineRig rig;
                rig.settings.model = model;
                rig.settings.release = 0.3f;
                rig.render (1.0f, EngineRig::notes ({ 48, 55, 60, 64 }));
                const auto level = rig.rms();
                expectGreaterThan (level, 0.01f, "model " + juce::String (model) + " is too quiet");
                expectLessThan (level, 0.8f, "model " + juce::String (model) + " is too loud");
                expect (rig.engine.numActiveVoices() == 4);

                rig.render (2.0f, EngineRig::notes ({ 48, 55, 60, 64 }, false));
                expect (rig.engine.numActiveVoices() == 0, "voices still sounding after release");
                rig.resetMeter();
                rig.render (0.5f);
                expectLessThan (rig.rms(), 1.0e-4f);
                expect (rig.finite);
            }
        }

        beginTest ("all eight voices, then stealing without stuck notes");
        {
            EngineRig rig;
            rig.settings.release = 0.2f;
            rig.render (0.3f, EngineRig::notes ({ 40, 43, 47, 50, 52, 55, 59, 62, 64, 67, 71 }));
            expectEquals (rig.engine.numActiveVoices(), 8);
            rig.render (2.0f, EngineRig::notes ({ 40, 43, 47, 50, 52, 55, 59, 62, 64, 67, 71 }, false));
            expectEquals (rig.engine.numActiveVoices(), 0);
        }

        beginTest ("mono and legato use one voice and release when the last key lifts");
        {
            for (int mode : { 1, 2 })
            {
                EngineRig rig;
                rig.settings.voiceMode = mode;
                rig.settings.glide = 0.1f;
                rig.settings.release = 0.2f;
                rig.render (0.2f, EngineRig::notes ({ 48 }));
                rig.render (0.2f, EngineRig::notes ({ 52 }));
                rig.render (0.2f, EngineRig::notes ({ 55 }));
                expectEquals (rig.engine.numActiveVoices(), 1);
                rig.render (0.2f, EngineRig::notes ({ 55 }, false)); // back to 52, still sounding
                expectEquals (rig.engine.numActiveVoices(), 1);
                rig.render (2.0f, EngineRig::notes ({ 48, 52 }, false));
                expectEquals (rig.engine.numActiveVoices(), 0);
            }
        }

        beginTest ("the sustain pedal holds notes until it is lifted");
        {
            EngineRig rig;
            rig.settings.release = 0.2f;
            juce::MidiBuffer down;
            down.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 0);
            down.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
            rig.render (0.2f, down);
            rig.render (1.0f, EngineRig::notes ({ 60 }, false));
            expectEquals (rig.engine.numActiveVoices(), 1);
            juce::MidiBuffer up;
            up.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), 0);
            rig.render (2.0f, up);
            expectEquals (rig.engine.numActiveVoices(), 0);
        }

        beginTest ("Atmos follows the notes, or runs all the time with ALWAYS");
        {
            EngineRig rig;
            rig.settings.atmos = { 1.0f, 1.0f, 1.0f, 1.0f, 0.5f, false, false };
            expectEquals (rig.render (0.5f), 0.0f);
            rig.settings.atmos.always = true;
            rig.resetMeter();
            rig.render (3.0f);
            expectGreaterThan (rig.rms(), 1.0e-3f);
            expectLessThan (rig.rms(), 0.3f);
            expect (rig.finite);
        }

        beginTest ("extreme settings at three sample rates stay finite and bounded");
        {
            for (double sr : { 44100.0, 96000.0, 192000.0 })
                for (int model = 0; model < 3; ++model)
                {
                    EngineRig rig (sr);
                    auto& s = rig.settings;
                    s.model = model;
                    s.resonance = 1.0f; s.drive = 1.0f; s.sync = true; s.polyModPitch = 1.0f; s.polyModPw = 1.0f;
                    s.cutoff = 20000.0f; s.filterEnv = 1.0f; s.keyTrack = 1.0f; s.noise = 1.0f; s.sub = 1.0f; s.pulse = 1.0f;
                    s.osc2Semi = 24.0f; s.lfoPitch = 1.0f; s.lfoFilter = 1.0f; s.lfoRate = 20.0f; s.lfoPwm = 1.0f; s.pwm = 1.0f;
                    s.macro = { 1.0f, 1.0f, 1.0f, 1.0f };
                    s.chorus = 3; s.ensEnsemble = 1.0f; s.hpf = 2000.0f; s.slop = 1.0f;
                    s.atmos = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, true, true };
                    const auto peak = rig.render (1.0f, EngineRig::notes ({ 24, 36, 60, 84, 96, 108, 115, 127 }, true, 1.0f));
                    expect (rig.finite);
                    expectLessThan (peak, 12.0f);
                }
        }

        beginTest ("band-limited oscillator: bounded, no DC, sync works");
        {
            dust::BlepOsc master, slave;
            master.reset (0.0f);
            slave.reset (0.0f);
            double sum = 0.0;
            float peak = 0.0f;
            const int n = 48000;
            for (int i = 0; i < n; ++i)
            {
                master.process (220.0f / 48000.0f, 0.5f, { 1.0f, 0.0f, 0.0f });
                const auto y = slave.process (577.0f / 48000.0f, 0.3f, { 0.5f, 0.5f, 0.0f }, master.wrapTau);
                sum += y;
                peak = std::max (peak, std::abs (y));
            }
            expectLessThan (peak, 1.3f);
            expectLessThan ((float) std::abs (sum / n), 0.08f);
        }
    }
};

SynthTests synthTests;
} // namespace
