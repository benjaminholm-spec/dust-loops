#pragma once

#include "DeckParts.h"
#include "SegmentDisplay.h"
#include "VuMeter.h"

class DustLoopsProcessor;

// The whole plugin face at its logical size (1000 x 640). The editor scales it for 75-200 %.
//   Top strip : logo, preset display, SPEED and TEMPO readouts, INPUT/SYNTH levels, BACK.
//   Front     : the deck - knobs on the grille, FREEZE, VU, loop LEDs, cassette, piano keys.
//   Back      : everything else (loop lengths, tape wear, output), out of the way.
namespace deck
{
constexpr int logicalWidth = 1000, logicalHeight = 640, topHeight = 70;

// Caches a painted background at the screen's physical resolution.
class CachedBackground
{
public:
    template <typename Painter>
    void draw (juce::Graphics& g, juce::Rectangle<int> area, Painter&& painter)
    {
        const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        if (image.isNull() || std::abs (scale - imageScale) > 0.01f || image.getWidth() != (int) std::ceil ((float) area.getWidth() * scale))
        {
            imageScale = scale;
            image = juce::Image (juce::Image::ARGB, (int) std::ceil ((float) area.getWidth() * scale),
                                 (int) std::ceil ((float) area.getHeight() * scale), true);
            painter (image, scale);
        }
        g.drawImage (image, area.toFloat());
    }

private:
    juce::Image image;
    float imageScale = 0.0f;
};

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
    void showHint (const juce::String&);
    void showBack (bool);

    DustLoopsProcessor& processor;
    HintStrip hintStrip;
    SegmentDisplay presetDisplay;
    LedReadout speedReadout { 4, "Speed" }, tempoReadout { 4, "Tempo" };
    std::unique_ptr<Knob> inputKnob, synthKnob;
    ActionButton backButton { "Back", "BACK PANEL: loop lengths, tape wear and output. Click again to return to the deck." };
    FrontPanel front;
    BackPanel back;
    CachedBackground background;
    juce::String defaultHint;
};
} // namespace deck
