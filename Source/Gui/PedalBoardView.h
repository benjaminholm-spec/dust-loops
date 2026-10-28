#pragma once

#include "DeckParts.h"

class DustLoopsProcessor;

// The pedalboard beside the deck: five stompboxes and the TAPE block in a two-column grid, wired in
// series with patch cables. The signal snakes through it: left to right on the first row, right to
// left on the second, left to right on the third. Drag a pedal (or the TAPE block) by its body to any
// spot to change the order; the cables follow.
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
    juce::String title, planetName;
    juce::Colour paint1;
    int planet = 0;
    CachedBackground background;
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
    static constexpr int width = 384;

    PedalBoardView (DustLoopsProcessor&, std::function<void (Knob&)> onTurn);

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    // Drag protocol, called by the items (positions relative to this component).
    void dragStarted (int slot);
    void dragMoved (int slot, juce::Point<int> delta);
    void dragEnded (int slot);

private:
    void timerCallback() override;
    void layout (bool animate);
    std::array<int, 6> currentOrder() const;
    juce::Rectangle<int> boundsForPosition (int position, int slot) const;
    float positionKey (juce::Rectangle<int> itemBounds) const; // continuous position along the snake
    int rowOf (const BoardItem&) const;
    std::array<BoardItem*, 6> itemsInChainOrder() const;

    DustLoopsProcessor& processor;
    std::array<std::unique_ptr<BoardItem>, 6> items; // indexed by slot: pedals 0-4, TAPE 5
    int dragging = -1;
    juce::Point<int> dragStart;
    bool wasMoving = false;
    juce::Image background;
    float backgroundScale = 0.0f;
};
} // namespace deck
