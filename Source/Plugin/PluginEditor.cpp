#include "PluginEditor.h"
#include "Core/GlobalSettings.h"

namespace
{
const juce::Identifier uiScaleKey { "uiScale" };
constexpr double minScale = 0.75, maxScale = 2.0;
} // namespace

DustLoopsEditor::DustLoopsEditor (DustLoopsProcessor& p)
    : AudioProcessorEditor (p), content (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (content);

    auto scale = juce::jlimit (minScale, maxScale, (double) dust::settings::get (uiScaleKey, 1.0));
    // Never open taller than the screen
    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        scale = juce::jlimit (minScale, scale, (display->userBounds.getHeight() - 120) / (double) deck::logicalHeight);
    setResizable (true, true);
    setResizeLimits ((int) (deck::logicalWidth * minScale), (int) (deck::logicalHeight * minScale),
                     (int) (deck::logicalWidth * maxScale), (int) (deck::logicalHeight * maxScale));
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio ((double) deck::logicalWidth / (double) deck::logicalHeight);
    setSize (juce::roundToInt (deck::logicalWidth * scale), juce::roundToInt (deck::logicalHeight * scale));

    lastFrame = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (30);
}

DustLoopsEditor::~DustLoopsEditor()
{
    dust::settings::set (uiScaleKey, (double) getWidth() / deck::logicalWidth);
    setLookAndFeel (nullptr);
}

void DustLoopsEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void DustLoopsEditor::resized()
{
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) deck::logicalWidth));
}

void DustLoopsEditor::timerCallback()
{
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto dt = (float) juce::jlimit (0.001, 0.1, (now - lastFrame) * 0.001);
    lastFrame = now;
    content.update (dt);
}
