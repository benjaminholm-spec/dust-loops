#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// Functional editor: a header strip over JUCE's generic parameter list (grouped by section).
// The full Dust Planet front panel replaces this in milestone 6.
class DustLoopsEditor : public juce::AudioProcessorEditor
{
public:
    explicit DustLoopsEditor (DustLoopsProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::GenericAudioProcessorEditor generic;
    juce::Image logo;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DustLoopsEditor)
};
