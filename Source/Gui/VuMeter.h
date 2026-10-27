#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>

class VuMeter : public juce::Component
{
public:
    VuMeter();

    // Feed the latest RMS (linear) and advance the needle; call at the editor's frame rate.
    void update (float rms, float frameSeconds);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Rectangle<float> faceArea() const;
    juce::Point<float> pivot() const;
    float needleLength() const;
    static float angleFor (float dbVu);

    void renderFace();
    void renderGlass();

    juce::Image faceLayer, glassLayer;
    float position = -20.0f, velocity = 0.0f;
    juce::Random jitter { 4242 };
};
