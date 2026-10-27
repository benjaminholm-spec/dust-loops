#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// A tired 70s neon alphanumeric display (14-segment) behind a cracked smoked lens.
// Shows a two-digit program number and a 16-character name that scrolls when it's longer.
// Some segments are dead, one flickers on a loose contact, one character has dimmed with age.
class SegmentDisplay : public juce::Component
{
public:
    SegmentDisplay();

    void setProgram (const juce::String& code, const juce::String& name); // code: two characters, e.g. "07"
    void advance (float dt); // animate marquee and flicker; call at the editor frame rate

    std::function<void()> onClick;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    static constexpr int nameCells = 16;
    static constexpr int totalCells = 2 + 1 + nameCells;

private:
    juce::String visibleText() const;
    void drawCell (juce::Graphics&, int cell, juce::uint16 mask, juce::Rectangle<float> area) const;
    juce::Rectangle<float> cellArea (int cell) const;
    void renderGlass();

    juce::String programText { "01" }, name;
    float marqueeOffset = 0.0f, holdTime = 0.0f, time = 0.0f;
    float shimmer = 1.0f;
    bool looseContactOn = true;
    juce::Random rng { 1979 };
    juce::Image glass;
};
