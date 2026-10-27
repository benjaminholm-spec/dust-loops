#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Tape/TapeMachine.h"

// The physical controls of the deck: knurled knobs with label-maker labels, cassette piano keys,
// the yellow FREEZE button, bat toggle switches, loop REC LEDs, 7-segment readouts and the cassette.
// Every control carries a one-line "hint" that the editor shows while the mouse is over it.
namespace deck
{
using APVTS = juce::AudioProcessorValueTreeState;

inline void setHint (juce::Component& c, const juce::String& hint) { c.getProperties().set ("hint", hint); }

// Embossed white letters on black label-maker tape, like the labels stuck on a modded deck.
void drawDymoLabel (juce::Graphics&, juce::Point<float> centre, const juce::String& text, float height, int seed);

//==============================================================================
class DetentSlider : public juce::Slider
{
public:
    std::vector<double> detents;
    double detentWindow = 0.0;

    double snapValue (double value, DragMode) override
    {
        for (auto d : detents)
            if (std::abs (value - d) < detentWindow)
                return d;
        return value;
    }
};

class Knob : public juce::Component
{
public:
    enum class Size { small, medium, big };

    Knob (APVTS&, const juce::String& paramId, const juce::String& label, const juce::String& hint, Size, int seed);

    void paint (juce::Graphics&) override;
    void resized() override;

    // Text for the hint strip while the knob moves, e.g. "FEED  72 %".
    juce::String valueText() const;
    std::function<void (Knob&)> onTurn;

    DetentSlider slider;

private:
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
    juce::String label;
    Size size;
    int seed;
};

//==============================================================================
class PianoKey : public juce::Component
{
public:
    enum class Symbol { rec, play, rewind, fastForward, stop, pause };

    PianoKey (APVTS&, const juce::String& paramId, Symbol, const juce::String& hint);

    std::function<bool()> isLatched; // drawn held down while this returns true (PLAY while playing, ...)
    void refresh();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::RangedAudioParameter* param = nullptr;
    Symbol symbol;
    bool held = false, drawnDown = false;
};

//==============================================================================
// Base for clickable controls bound to one parameter.
class ParamControl : public juce::Component
{
public:
    ParamControl (APVTS&, const juce::String& paramId);
    float value() const noexcept { return current; }

protected:
    void setParam (float realValue) { attachment.setValueAsCompleteGesture (realValue); }

    juce::RangedAudioParameter* param = nullptr;

private:
    float current = 0.0f;
    juce::ParameterAttachment attachment;
};

class FreezeButton : public ParamControl
{
public:
    FreezeButton (APVTS&, const juce::String& hint);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
};

// Two-position bat-handle toggle for a two-choice parameter: `upLabel` is choice 0.
class ToggleSwitch : public ParamControl
{
public:
    ToggleSwitch (APVTS&, const juce::String& paramId, const juce::String& upLabel, const juce::String& downLabel,
                  const juce::String& hint);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::String upLabel, downLabel;
};

// Loop REC LED: lit when the loop is armed, flickers with what is on the tape. Click to arm/disarm.
class LoopLed : public ParamControl
{
public:
    LoopLed (APVTS&, int loopIndex, const juce::String& hint);
    void setActivity (float level);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    int index;
    float activity = 0.0f;
};

// Small black push button with a red LED. Modes: toggles a bool, momentary press, or selects one choice.
class SmallButton : public ParamControl
{
public:
    enum class Mode { toggle, momentary, choice };
    SmallButton (APVTS&, const juce::String& paramId, const juce::String& label, Mode, int choiceIndex,
                 const juce::String& hint);
    std::function<void()> onClick; // user clicks only, not automation
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    bool isLit() const;
    juce::String label;
    Mode mode;
    int choiceIndex;
    bool held = false;
};

// A push button that runs a callback (not bound to a parameter).
class ActionButton : public juce::Component
{
public:
    ActionButton (const juce::String& label, const juce::String& hint);
    std::function<void()> onPress, onRelease;
    void setLabel (const juce::String& l) { label = l; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::String label;
    bool held = false;
};

//==============================================================================
// Red 7-segment LED readout with visible unlit segments and a faint glow.
class LedReadout : public juce::Component
{
public:
    LedReadout (int numDigits, const juce::String& caption);
    void setText (const juce::String&);
    void setCaption (const juce::String& c) { if (c != caption) { caption = c; repaint(); } }
    void paint (juce::Graphics&) override;

private:
    int digits;
    juce::String caption, text;
};

class HintStrip : public juce::Component
{
public:
    void setText (const juce::String&);
    void setRecording (bool);
    void paint (juce::Graphics&) override;

private:
    juce::String text;
    bool recording = false;
};

//==============================================================================
// The cassette in its window. Reels turn at the real tape speed (backwards in reverse, still in FREEZE);
// the tape packs grow and shrink as loop A goes round.
class CassetteView : public juce::Component, private juce::Timer
{
public:
    CassetteView (const dust::TapeMachine&, std::atomic<float>* freezeParam);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    void drawReel (juce::Graphics&, juce::Point<float> centre, float hubRadius, float packRadius, float angle) const;

    const dust::TapeMachine& tape;
    std::atomic<float>* freeze;
    float leftAngle = 0.0f, rightAngle = 0.0f, phase = 0.0f;
    double lastTime = 0.0;
};
} // namespace deck
