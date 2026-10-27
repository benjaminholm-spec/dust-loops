#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

// Milestone 1 placeholder voice: a sine with the amp envelope. Replaced by the real models in milestone 2.
namespace dust
{
struct SineSound : juce::SynthesiserSound
{
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

class SineVoice : public juce::SynthesiserVoice
{
public:
    // Envelope settings are refreshed by the processor once per block.
    juce::ADSR::Parameters envelopeParams;

    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<SineSound*> (s) != nullptr; }

    void startNote (int note, float velocity, juce::SynthesiserSound*, int) override
    {
        increment = juce::MidiMessage::getMidiNoteInHertz (note) / getSampleRate();
        gain = 0.25f * (0.4f + 0.6f * velocity);
        envelope.setSampleRate (getSampleRate());
        envelope.setParameters (envelopeParams);
        envelope.noteOn();
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff)
            envelope.noteOff();
        else
        {
            envelope.reset();
            clearCurrentNote();
        }
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) override
    {
        if (! isVoiceActive())
            return;

        envelope.setParameters (envelopeParams);
        for (int i = start; i < start + num; ++i)
        {
            const auto s = (float) std::sin (juce::MathConstants<double>::twoPi * phase) * gain * envelope.getNextSample();
            phase += increment;
            if (phase >= 1.0) phase -= 1.0;
            for (int ch = 0; ch < out.getNumChannels(); ++ch)
                out.addSample (ch, i, s);
        }

        if (! envelope.isActive())
            clearCurrentNote();
    }

private:
    juce::ADSR envelope;
    double phase = 0.0, increment = 0.0;
    float gain = 0.0f;
};
} // namespace dust
