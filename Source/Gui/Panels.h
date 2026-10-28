#pragma once

#include "Core/Identity.h"
#include "DeckParts.h"
#include "SegmentDisplay.h"
#include "VuMeter.h"
#include "PedalBoardView.h"
#include "SynthPanels.h"
#include "PresetBrowser.h"

class DustLoopsProcessor;

// The whole plugin face at its logical size, laid out wide for laptop screens: on the left the top
// strip, the synth strip (a nameplate in the FX) and the deck; on the right, the full-height pedalboard.
// The editor scales it for 75-200 %.
//   Top strip : logo, preset display with arrows, SPEED and TEMPO readouts, INPUT/SYNTH levels, BACK.
//   Synth     : model, the four macros, cutoff/resonance/attack/release, Atmos, EDIT.
//   Deck area : one of four views - the deck front (knobs on the grille, FREEZE, VU, loop LEDs,
//               cassette, piano keys), its back (loop lengths, tape wear, output), the synth EDIT page,
//               or the preset browser.
namespace deck
{
constexpr int deckWidth = 1000, topHeight = 70, frontHeight = 570;
constexpr int synthHeight = SynthStrip::height;
constexpr int deckTop = topHeight + synthHeight, deckHeight = deckTop + frontHeight;
constexpr int logicalWidth = deckWidth + PedalBoardView::width, logicalHeight = deckHeight;

class FrontPanel : public juce::Component
{
public:
    FrontPanel (DustLoopsProcessor&, std::function<void (Knob&)> onTurn);
    void update (float frameSeconds);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    DustLoopsProcessor& processor;
    Knob time, feed, volume, mix, speed;
    FreezeButton freeze;
    VuMeter vu;
    std::array<std::unique_ptr<LoopLed>, 3> loopLeds;
    ActionButton erase;
    ToggleSwitch modeSwitch, syncSwitch;
    CassetteView cassette;
    std::array<std::unique_ptr<LedReadout>, 3> lengthReadouts;
    std::array<std::unique_ptr<PianoKey>, 6> keys;
    CachedBackground background;
};

class BackPanel : public juce::Component
{
public:
    BackPanel (DustLoopsProcessor&, std::function<void (Knob&)> onTurn);
    void update();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    DustLoopsProcessor& processor;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::array<Knob*, 3> beatsKnobs {}, secondsKnobs {};
    std::vector<std::unique_ptr<SmallButton>> buttons;
    std::vector<std::pair<juce::Component*, juce::Rectangle<int>>> layout;
    CachedBackground background;
};

class DeckContent : public juce::Component
{
public:
    explicit DeckContent (DustLoopsProcessor&);
    void update (float frameSeconds);
    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    enum class View { front, back, edit, browser };
    void showHint (const juce::String&);
    void showView (View);
    void updatePresetDisplay();

    DustLoopsProcessor& processor;
    View view = View::front;
    HintStrip hintStrip;
    SegmentDisplay presetDisplay;
    ArrowButton prevPreset { false, "PREVIOUS PRESET (follows the bank and search chosen in the browser)." },
                nextPreset { true, "NEXT PRESET (follows the bank and search chosen in the browser)." };
    LedReadout speedReadout { 4, "Speed" }, tempoReadout { 4, "Tempo" };
    std::unique_ptr<Knob> inputKnob, synthKnob;
    ActionButton backButton { "Back", "BACK PANEL: loop lengths, tape wear and output. Click again to return to the deck." };
    FrontPanel front;
    BackPanel back;
    std::unique_ptr<SynthStrip> synthStrip;
    std::unique_ptr<SynthEditPanel> synthEdit;
    PresetBrowser browser;
    PedalBoardView board;
    CachedBackground background;
    juce::String defaultHint;
    std::pair<int, int> lastPresetState { -2, 0 };
};
} // namespace deck
