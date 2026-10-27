#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
constexpr int headerHeight = 56;
}

DustLoopsEditor::DustLoopsEditor (DustLoopsProcessor& p)
    : AudioProcessorEditor (p), generic (p)
{
    logo = juce::ImageCache::getFromMemory (BinaryData::dpr_logo_horizontal_png, BinaryData::dpr_logo_horizontal_pngSize);
    addAndMakeVisible (generic);
    setResizable (true, true);
    setResizeLimits (480, 360, 1600, 1400);
    setSize (640, 720);
}

void DustLoopsEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff111111));
    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (12, 8);

    g.setColour (juce::Colour (0xffd9442b)); // LED red
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    juce::String title (dust::kProductName);
    if (dust::kIsFx)
        title << " FX";
    g.drawText (title.toUpperCase(), header, juce::Justification::centredLeft);

    if (logo.isValid())
    {
        const auto logoArea = header.removeFromRight (header.getHeight() * logo.getWidth() / logo.getHeight());
        g.drawImage (logo, logoArea.toFloat(), juce::RectanglePlacement::centred);
    }
}

void DustLoopsEditor::resized()
{
    generic.setBounds (getLocalBounds().withTrimmedTop (headerHeight));
}
