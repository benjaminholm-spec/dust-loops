#include "SynthPanels.h"
#include "Art.h"
#include "Core/Params.h"
#include "Plugin/PluginProcessor.h"

namespace deck
{
using namespace juce;

namespace
{
constexpr int knobW = 70, knobH = 94;

struct Def2 { const char* id; const char* label; const char* hint; };

Rectangle<int> knobAt (Point<int> centreTop) { return { centreTop.x - knobW / 2, centreTop.y, knobW, knobH }; }

void paintPlate (Image& image, float scale, Rectangle<int> area, uint32 seed)
{
    Art::drawBlackPanel (image, area, scale, seed);
    Graphics g (image);
    g.addTransform (AffineTransform::scale (scale));
    Art::drawPanelWear (g, area.toFloat(), seed * 7u + 3u);
}

// A printed box around a group of controls.
void drawSectionBox (Graphics& g, Rectangle<float> area)
{
    g.setColour (Art::Colours::line.withAlpha (0.55f));
    g.drawRoundedRectangle (area, 5.0f, 1.0f);
}

void printLegend (Graphics& g, const String& text, Rectangle<float> area, Justification just = Justification::centred)
{
    g.setFont (Art::silk (11.0f));
    g.setColour (Art::Colours::silkDim);
    g.drawText (text.toUpperCase(), area, just, false);
}
} // namespace

//==============================================================================
SynthStrip::SynthStrip (DustLoopsProcessor& p, std::function<void (Knob&)> onTurn)
{
    const std::array<std::pair<const char*, const char*>, 3> models { {
        { "DCO", "DCO-POLY: a warm, steady Juno-style synth with a lush chorus. Pads and keys." },
        { "VCO", "VCO-POLY: two drifting oscillators, Prophet-style. Fatter, dirtier and less stable." },
        { "Ensemble", "ENSEMBLE: an old string machine. Wide, seasick strings and choirs." },
    } };
    for (int i = 0; i < 3; ++i)
    {
        modelButtons.push_back (std::make_unique<SmallButton> (p.apvts, ID::synth::model, models[(size_t) i].first,
                                                               SmallButton::Mode::choice, i, models[(size_t) i].second));
        addAndMakeVisible (*modelButtons.back());
    }

    struct Def { const char* id; const char* label; const char* hint; };
    const Def defs[] {
        { ID::synth::macro[0], "Bloom", "BLOOM: notes swell in slowly and fade out long. Turns keys into pads." },
        { ID::synth::macro[1], "Motion", "MOTION: vibrato, a breathing filter and pulse width. The mod wheel turns it up too." },
        { ID::synth::macro[2], "Warp", "WARP: detunes the voices and makes the pitch wobble like a stretched tape." },
        { ID::synth::macro[3], "Haze", "HAZE: darker, softer and further away." },
        { ID::synth::cutoff, "Cutoff", "CUTOFF: brightness. Left is dark and muffled, right is bright." },
        { ID::synth::resonance, "Reso", "RESONANCE: a whistling peak at the cutoff. Higher = more vocal and squelchy." },
        { ID::synth::attack, "Attack", "ATTACK: how slowly notes fade in." },
        { ID::synth::release, "Release", "RELEASE: how long notes ring after you let go." },
        { ID::atmos::level, "Atmos", "ATMOS: vinyl crackle, rain and hum around the synth. It fades in while you play. Mix it on the EDIT page." },
    };
    int seed = 300;
    for (auto& d : defs)
    {
        knobs.push_back (std::make_unique<Knob> (p.apvts, d.id, d.label, d.hint, Knob::Size::small, ++seed));
        knobs.back()->onTurn = onTurn;
        addAndMakeVisible (*knobs.back());
    }

    edit.onPress = [this] { if (onEdit) onEdit(); };
    addAndMakeVisible (edit);
}

void SynthStrip::resized()
{
    for (size_t i = 0; i < modelButtons.size(); ++i)
        modelButtons[i]->setBounds (24, 20 + (int) i * 29, 104, 25);

    // Macros 160..480, filter + envelope 500..820, Atmos 840
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        const auto x = i < 4 ? 200 + (int) i * 80 : (i < 8 ? 540 + (int) (i - 4) * 80 : 880);
        knobs[i]->setBounds (knobAt ({ x, 14 }));
    }
    edit.setBounds (930, 42, 58, 28);
}

void SynthStrip::paint (Graphics& g)
{
    background.draw (g, getLocalBounds(), [this] (Image& image, float scale)
    {
        paintPlate (image, scale, getLocalBounds(), 0x5157);
        Graphics gi (image);
        gi.addTransform (AffineTransform::scale (scale));
        drawDymoLabel (gi, { 76.0f, 8.0f }, "Synth", 14.0f, 71);
        drawSectionBox (gi, { 152.0f, 6.0f, 336.0f, 100.0f });
        drawSectionBox (gi, { 496.0f, 6.0f, 336.0f, 100.0f });
        drawSectionBox (gi, { 840.0f, 6.0f, 80.0f, 100.0f });
        gi.setColour (Colours::black.withAlpha (0.7f));
        gi.fillRect (0.0f, (float) getHeight() - 2.0f, (float) getWidth(), 2.0f);
    });
}

//==============================================================================
Knob* SynthEditPanel::addKnob (const String& id, const String& label, const String& hint, Point<int> centreTop)
{
    knobs.push_back (std::make_unique<Knob> (processor.apvts, id, label, hint, Knob::Size::small, 400 + (int) knobs.size()));
    auto* k = knobs.back().get();
    k->onTurn = onTurn;
    addAndMakeVisible (*k);
    layout.push_back ({ k, knobAt (centreTop) });
    return k;
}

SmallButton* SynthEditPanel::addButton (const String& id, const String& label, SmallButton::Mode mode, int index,
                                        const String& hint, Rectangle<int> bounds)
{
    buttons.push_back (std::make_unique<SmallButton> (processor.apvts, id, label, mode, index, hint));
    auto* b = buttons.back().get();
    addAndMakeVisible (*b);
    layout.push_back ({ b, bounds });
    return b;
}

SynthEditPanel::SynthEditPanel (DustLoopsProcessor& p, std::function<void (Knob&)> turn) : processor (p), onTurn (std::move (turn))
{
    using namespace ID::synth;
    using Mode = SmallButton::Mode;
    constexpr int row1 = 8, row2 = 140, row3 = 272, row4 = 404, top = 26; // rows of 132; knobs sit `top` below
    const auto col = [] (int x0, int i, int step = 72) { return x0 + 36 + i * step; };

    // ---- Row 1: oscillators (per model) and voice ----
    sections.push_back ({ "Oscillators", { 16, row1, 640, 126 } });
    auto& dco = modelControls[0];
    dco.push_back (addKnob (saw, "Saw", "SAW: the bright, buzzy wave. The classic pad sound.", { col (24, 0), row1 + top }));
    dco.push_back (addKnob (pulse, "Pulse", "PULSE: a hollower, woody wave. Mix it with SAW.", { col (24, 1), row1 + top }));
    dco.push_back (addKnob (pw, "Width", "WIDTH: the pulse's shape. Middle is hollow, the edges are thin and nasal.", { col (24, 2), row1 + top }));
    dco.push_back (addKnob (pwm, "PWM", "PWM: the LFO moves the pulse width. Makes the pulse shimmer like a chorus.", { col (24, 3), row1 + top }));
    dco.push_back (addKnob (ID::synth::sub, "Sub", "SUB: a square wave an octave below. Weight and warmth.", { col (24, 4), row1 + top }));
    dco.push_back (addKnob (noise, "Noise", "NOISE: white noise. Breath, wind and air.", { col (24, 5), row1 + top }));

    auto& vco = modelControls[1];
    constexpr int vstep = 68;
    vco.push_back (addKnob (osc1Wave, "Wave 1", "WAVE 1: oscillator 1 from saw (left) to pulse (right).", { col (24, 0, vstep), row1 + top }));
    vco.push_back (addKnob (osc2Wave, "Wave 2", "WAVE 2: oscillator 2 from saw (left) to pulse (right).", { col (24, 1, vstep), row1 + top }));
    vco.push_back (addKnob (pw, "Width", "WIDTH: the pulse's shape, for whichever oscillator is on pulse.", { col (24, 2, vstep), row1 + top }));
    vco.push_back (addKnob (osc2Semi, "Semi", "SEMI: oscillator 2's pitch in semitones. 7 = a fifth, 12 = an octave.", { col (24, 3, vstep), row1 + top }));
    vco.push_back (addKnob (osc2Detune, "Detune", "DETUNE: oscillator 2 slightly off pitch. Makes it thick and beating.", { col (24, 4, vstep), row1 + top }));
    vco.push_back (addKnob (osc2Level, "Level 2", "LEVEL 2: how loud oscillator 2 is.", { col (24, 5, vstep), row1 + top }));
    vco.push_back (addKnob (polyModPitch, "PM Pitch",
        "POLY-MOD PITCH: the filter envelope and oscillator 2 bend oscillator 1. With SYNC on: the classic sync sweep.", { col (24, 6, vstep), row1 + top }));
    vco.push_back (addKnob (polyModPw, "PM Width", "POLY-MOD WIDTH: the filter envelope moves the pulse width.", { col (24, 7, vstep), row1 + top }));
    vco.push_back (addKnob (drive, "Drive", "DRIVE: pushes the oscillators into the filter. Fatter and grittier.", { col (24, 8, vstep), row1 + top }));
    vco.push_back (addButton (ID::synth::sync, "Sync", Mode::toggle, 0,
        "SYNC: oscillator 1 restarts with oscillator 2. Turn PM PITCH or SEMI for a tearing, vocal sound.", { 560, row1 + 2, 84, 22 }));

    auto& ensemble = modelControls[2];
    const char* feet[] { "8'", "8' + 4'", "16' + 8'", "16' + 8' + 4'" };
    for (int i = 0; i < 4; ++i)
        ensemble.push_back (addButton (ensFeet, feet[i], Mode::choice, i,
            "REGISTERS: which octaves sound. 16' is an octave down, 4' an octave up.", { 40 + (i % 2) * 124, row1 + 34 + (i / 2) * 34, 118, 26 }));
    ensemble.push_back (addKnob (ensBright, "Bright", "BRIGHT: the string machine's tone, from dull to sharp.", { 340, row1 + top }));
    ensemble.push_back (addKnob (ensEnsemble, "Ensemble", "ENSEMBLE: the thick, swirling chorus that makes it sound like strings.", { 420, row1 + top }));

    sections.push_back ({ "Voice", { 672, row1, 312, 126 } });
    const char* modes[] { "Poly", "Mono", "Legato" };
    const char* modeHints[] { "POLY: play chords (8 voices).", "MONO: one note at a time; every note starts fresh.",
                              "LEGATO: one note at a time; overlapping notes slide without restarting." };
    for (int i = 0; i < 3; ++i)
        addButton (voiceMode, modes[i], Mode::choice, i, modeHints[i], { 688, row1 + 28 + i * 31, 82, 26 });
    addKnob (glide, "Glide", "GLIDE: notes slide into each other.", { 812, row1 + top });
    addKnob (bendRange, "Bend", "BEND: pitch-wheel range in semitones.", { 880, row1 + top });
    addKnob (slop, "Slop", "SLOP: how unstable the old circuits are: every note a little different.", { 948, row1 + top });

    // ---- Row 2: filter and its envelope ----
    sections.push_back ({ "Filter", { 16, row2, 560, 126 } });
    const Def2 filterDefs[] {
        { cutoff, "Cutoff", "CUTOFF: brightness. Left is dark and muffled, right is bright." },
        { resonance, "Reso", "RESONANCE: a whistling peak at the cutoff. Higher = more vocal and squelchy." },
        { hpf, "Hi-Pass", "HI-PASS: thins out the low end. Keeps pads out of the way of bass." },
        { filterEnv, "Env", "ENV: how far the filter envelope opens the filter. Left of centre closes it instead." },
        { keyTrack, "Key", "KEY: higher notes get brighter." },
        { velToFilter, "Velo", "VELO: playing harder makes notes brighter." },
        { atToFilter, "Press", "PRESS: pressing down harder on held keys (aftertouch) opens the filter." },
    };
    for (int i = 0; i < 7; ++i)
        addKnob (filterDefs[i].id, filterDefs[i].label, filterDefs[i].hint, { col (24, i, 76), row2 + top });

    sections.push_back ({ "Filter envelope", { 592, row2, 392, 126 } });
    const Def2 fenvDefs[] {
        { fAttack, "Attack", "FILTER ATTACK: how slowly the filter opens." },
        { fDecay, "Decay", "FILTER DECAY: how fast it falls back to the sustain level." },
        { fSustain, "Sustain", "FILTER SUSTAIN: where it rests while you hold the key." },
        { fRelease, "Release", "FILTER RELEASE: how it closes after you let go." },
    };
    for (int i = 0; i < 4; ++i)
        addKnob (fenvDefs[i].id, fenvDefs[i].label, fenvDefs[i].hint, { col (608, i, 90), row2 + top });

    // ---- Row 3: amp envelope and LFO ----
    sections.push_back ({ "Amp envelope", { 16, row3, 400, 126 } });
    const Def2 ampDefs[] {
        { attack, "Attack", "ATTACK: how slowly notes fade in." },
        { decay, "Decay", "DECAY: how fast notes fall to the sustain level." },
        { sustain, "Sustain", "SUSTAIN: the level while you hold the key." },
        { release, "Release", "RELEASE: how long notes ring after you let go." },
        { velToAmp, "Velo", "VELO: how much harder playing makes notes louder." },
    };
    for (int i = 0; i < 5; ++i)
        addKnob (ampDefs[i].id, ampDefs[i].label, ampDefs[i].hint, { col (24, i, 76), row3 + top });

    sections.push_back ({ "LFO", { 432, row3, 552, 126 } });
    const char* shapes[] { "Tri", "Sine", "Square", "Random" };
    for (int i = 0; i < 4; ++i)
        addButton (lfoShape, shapes[i], Mode::choice, i, "LFO SHAPE: smooth (TRI, SINE), switching (SQUARE) or random steps (RANDOM).",
                   { 448 + (i % 2) * 70, row3 + 34 + (i / 2) * 34, 66, 26 });
    addKnob (lfoRate, "Rate", "LFO RATE: how fast it moves.", { col (596, 0), row3 + top });
    addKnob (lfoDelay, "Delay", "LFO DELAY: the wobble fades in after each note starts.", { col (596, 1), row3 + top });
    addKnob (lfoPitch, "Pitch", "LFO > PITCH: vibrato.", { col (596, 2), row3 + top });
    addKnob (lfoFilter, "Filter", "LFO > FILTER: the brightness moves up and down.", { col (596, 3), row3 + top });
    lfoPwm = addKnob (ID::synth::lfoPwm, "PWM", "LFO > PWM: the pulse width moves. Only for pulse waves.", { col (596, 4), row3 + top });

    // ---- Row 4: chorus and Atmos ----
    sections.push_back ({ "Chorus", { 16, row4, 256, 126 } });
    const char* choruses[] { "Off", "I", "II", "I+II" };
    for (int i = 0; i < 4; ++i)
        addButton (chorus, choruses[i], Mode::choice, i, "CHORUS: the Juno's stereo chorus. I is slow and wide, II faster, I+II a fast shimmer.",
                   { 32 + (i % 2) * 70, row4 + 34 + (i / 2) * 34, 66, 26 });
    addKnob (chorusMix, "Mix", "CHORUS MIX: how much chorus.", { 216, row4 + top });

    sections.push_back ({ "Atmos", { 288, row4, 696, 126 } });
    using namespace ID::atmos;
    addKnob (ID::atmos::level, "Level", "ATMOS LEVEL: how loud the room is.", { col (296, 0, 76), row4 + top });
    addKnob (crackle, "Crackle", "CRACKLE: vinyl surface noise, clicks and the odd pop.", { col (296, 1, 76), row4 + top });
    addKnob (rain, "Rain", "RAIN: rain on a window.", { col (296, 2, 76), row4 + top });
    addKnob (hum, "Hum", "HUM: mains hum from old gear.", { col (296, 3, 76), row4 + top });
    addKnob (ID::atmos::tone, "Tone", "ATMOS TONE: dull (left) to crisp (right).", { col (296, 4, 76), row4 + top });
    addButton (humFreq, "50 Hz", Mode::choice, 0, "HUM: 50 Hz (UK/Europe mains).", { 696, row4 + 34, 80, 26 });
    addButton (humFreq, "60 Hz", Mode::choice, 1, "HUM: 60 Hz (US mains).", { 696, row4 + 68, 80, 26 });
    addButton (always, "Always", Mode::toggle, 0, "ALWAYS: keep the Atmos running even when nothing is playing.", { 800, row4 + 50, 90, 26 });

    update();
}

void SynthEditPanel::update()
{
    const auto model = juce::jlimit (0, 2, (int) processor.apvts.getRawParameterValue (ID::synth::model)->load());
    if (model == shownModel)
        return;
    shownModel = model;
    for (int m = 0; m < 3; ++m)
        for (auto* c : modelControls[(size_t) m])
            c->setVisible (false);
    for (auto* c : modelControls[(size_t) model])
        c->setVisible (true);
    lfoPwm->setVisible (model == 1);
    repaint();
}

void SynthEditPanel::resized()
{
    for (auto& [component, bounds] : layout)
        component->setBounds (bounds);
}

void SynthEditPanel::paint (Graphics& g)
{
    background.draw (g, getLocalBounds(), [this] (Image& image, float scale)
    {
        paintPlate (image, scale, getLocalBounds(), 0xed17);
        Graphics gi (image);
        gi.addTransform (AffineTransform::scale (scale));
        int seed = 80;
        for (auto& s : sections)
        {
            drawSectionBox (gi, s.area.toFloat().withTrimmedTop (8.0f));
            drawDymoLabel (gi, { (float) s.area.getX() + 14.0f + (float) s.title.length() * 4.6f, (float) s.area.getY() + 8.0f },
                           s.title, 15.0f, ++seed);
        }
    });

    static const char* modelNames[] { "DCO-Poly", "VCO-Poly", "Ensemble" };
    if (shownModel >= 0)
        printLegend (g, modelNames[shownModel], { 150.0f, 4.0f, 200.0f, 16.0f }, Justification::centredLeft);
}
} // namespace deck
