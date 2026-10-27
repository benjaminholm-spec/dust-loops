#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Slider properties read by the look-and-feel:
//   "style"  : "" (standard knob) or "macro" (large knob with a coloured ring)
//   "accent" : macro ring colour (juce::Colour::toString())
//   "seed"   : int, makes each knob's wear unique but stable
class DustLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DustLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    juce::Font getSliderPopupFont (juce::Slider&) override;
    int getSliderPopupPlacement (juce::Slider&) override;
    void drawBubble (juce::Graphics&, juce::BubbleComponent&, const juce::Point<float>& tip,
                     const juce::Rectangle<float>& body) override;
    void setComponentEffectForBubbleComponent (juce::BubbleComponent&) override {}

    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;

    static void drawKnob (juce::Graphics&, juce::Rectangle<float> bounds, float angle, int seed, juce::Colour accent, bool macro);
};
