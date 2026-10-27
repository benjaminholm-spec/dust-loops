#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "Gui/DustLookAndFeel.h"
#include "Gui/Panels.h"

// Hosts the deck face at its logical size and scales it to the window (75-200 %, remembered globally).
class DustLoopsEditor : public juce::AudioProcessorEditor,
                        private juce::Timer
{
public:
    explicit DustLoopsEditor (DustLoopsProcessor&);
    ~DustLoopsEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    DustLookAndFeel lookAndFeel;
    deck::DeckContent content;
    double lastFrame = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DustLoopsEditor)
};
