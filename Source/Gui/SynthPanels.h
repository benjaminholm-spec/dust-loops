#pragma once

#include "DeckParts.h"

class DustLoopsProcessor;

// The synth's controls. The strip above the deck holds the essentials (model, the four macros,
// cutoff, resonance, attack, release, Atmos); EDIT swaps the deck for a page with everything else.
namespace deck
{
class SynthStrip : public juce::Component
{
public:
    static constexpr int height = 112;

    SynthStrip (DustLoopsProcessor&, std::function<void (Knob&)> onTurn);
    void setEditShown (bool shown) { edit.setLabel (shown ? "Deck" : "Edit"); }
    std::function<void()> onEdit;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<SmallButton>> modelButtons;
    ActionButton edit { "Edit", "EDIT: every synth setting (oscillators, filter, envelopes, LFO, chorus, Atmos). Click again to go back to the deck." };
    CachedBackground background;
};

class SynthEditPanel : public juce::Component
{
public:
    SynthEditPanel (DustLoopsProcessor&, std::function<void (Knob&)> onTurn);
    void update(); // shows the oscillator controls for the current model

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Section { juce::String title; juce::Rectangle<int> area; };

    Knob* addKnob (const juce::String& id, const juce::String& label, const juce::String& hint, juce::Point<int> centreTop);
    SmallButton* addButton (const juce::String& id, const juce::String& label, SmallButton::Mode, int index,
                            const juce::String& hint, juce::Rectangle<int>);

    DustLoopsProcessor& processor;
    std::function<void (Knob&)> onTurn;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<SmallButton>> buttons;
    std::vector<std::pair<juce::Component*, juce::Rectangle<int>>> layout;
    std::array<std::vector<juce::Component*>, 3> modelControls; // DCO, VCO, Ensemble
    juce::Component* lfoPwm = nullptr;
    std::vector<Section> sections;
    int shownModel = -1;
    CachedBackground background;
};
} // namespace deck
