#pragma once

#include "DeckParts.h"

class DustLoopsProcessor;

// The pedalboard under the deck: five stompboxes and the TAPE block, wired in series with patch
// cables. Drag a pedal (or the TAPE block) by its body to change the order; the cables follow.
namespace deck
{
class PedalBoardView;

// Chrome footswitch: bypass on/off.
class Footswitch : public ParamControl
{
public:
    Footswitch (APVTS&, const juce::String& paramId, const juce::String& hint);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
};

// A board item (pedal or TAPE block) that can be dragged by its body.
class BoardItem : public juce::Component
{
public:
    BoardItem (PedalBoardView&, int slot);
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    const int slot;

protected:
    PedalBoardView& board;
};

class PedalView : public BoardItem
{
public:
    PedalView (PedalBoardView&, APVTS&, int pedalIndex, std::function<void (Knob&)> onTurn);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    std::unique_ptr<juce::Component> led;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::unique_ptr<Knob> loopAmount, extraKnob;
    std::unique_ptr<SmallButton> loopButton, extraButton;
    std::unique_ptr<Footswitch> footswitch;
    juce::String title;
    juce::Colour paint1;
};

class TapeBlockView : public BoardItem
{
public:
    TapeBlockView (PedalBoardView&, const dust::TapeMachine&);
    void paint (juce::Graphics&) override;

private:
    const dust::TapeMachine& tape;
};

class PedalBoardView : public juce::Component, private juce::Timer
{
public:
    static constexpr int height = 262;

    PedalBoardView (DustLoopsProcessor&, std::function<void (Knob&)> onTurn);

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    // Drag protocol, called by the items (positions relative to this component).
    void dragStarted (int slot);
    void dragMoved (int slot, int deltaX);
    void dragEnded (int slot);

private:
    void timerCallback() override;
    void layout (bool animate);
    std::array<int, 6> currentOrder() const;
    juce::Rectangle<int> boundsForPosition (int position, int slot) const;

    DustLoopsProcessor& processor;
    std::array<std::unique_ptr<BoardItem>, 6> items; // indexed by slot: pedals 0-4, TAPE 5
    int dragging = -1, dragStartX = 0;
    juce::Image background;
    float backgroundScale = 0.0f;
};
} // namespace deck
