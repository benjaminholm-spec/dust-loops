#include "Panels.h"
#include "Art.h"
#include "Core/Params.h"
#include "Plugin/PluginProcessor.h"

namespace deck
{
using namespace juce;

namespace
{
const String defaultHintText = "Play a chord: it goes onto three tape loops of different lengths. "
                               "FEED sets how long it lasts, FREEZE holds it forever.";

// Speed knob geometry (front panel, local coordinates); the scale is printed around it.
const Rectangle<int> speedBounds { 460, 40, 170, 226 };

float rotaryAngle (float value, float min, float max)
{
    constexpr auto start = MathConstants<float>::pi * 1.2f, end = MathConstants<float>::pi * 2.8f; // JUCE default sweep
    return start + (value - min) / (max - min) * (end - start);
}

Point<float> polarPoint (Point<float> c, float r, float angle) { return { c.x + r * std::sin (angle), c.y - r * std::cos (angle) }; }

// Punched speaker grille: rows of holes, lit from above.
void drawGrille (Graphics& g, Rectangle<float> area)
{
    ColourGradient plate (Colour (0xff353538), area.getX(), area.getY(), Colour (0xff222224), area.getX(), area.getBottom(), false);
    Art::drawRecess (g, area, 6.0f); // fills black: must come first
    g.setGradientFill (plate);
    g.fillRoundedRectangle (area.reduced (1.0f), 6.0f);
    constexpr float pitch = 11.0f, hole = 2.7f;
    for (float y = area.getY() + 10.0f; y < area.getBottom() - 6.0f; y += pitch)
        for (float x = area.getX() + 10.0f + (std::fmod (y, 2.0f * pitch) < pitch ? 0.0f : pitch * 0.5f); x < area.getRight() - 6.0f; x += pitch)
        {
            g.setColour (Colours::black);
            g.fillEllipse (x - hole, y - hole, hole * 2.0f, hole * 2.0f);
            g.setColour (Colours::white.withAlpha (0.13f));
            g.drawLine (x - hole * 0.7f, y + hole * 1.05f, x + hole * 0.7f, y + hole * 1.05f, 0.7f);
        }
}

void paintPanel (Image& image, float scale, Rectangle<int> bounds, uint32 seed)
{
    Art::drawBlackPanel (image, bounds, scale, seed);
}

Rectangle<int> knobBounds (int cx, int y, int width = 90, int height = 92) { return { cx - width / 2, y, width, height }; }
} // namespace

//==============================================================================
FrontPanel::FrontPanel (DustLoopsProcessor& p, std::function<void (Knob&)> onTurn)
    : processor (p),
      time (p.apvts, ID::tape::time, "Time", "TIME: in LOOP mode it stretches or shrinks the loops (noon = normal). In ECHO mode it is the echo time.",
            Knob::Size::medium, 11),
      feed (p.apvts, ID::tape::feed, "Feed", "FEED: how much of the old tape survives each pass. Low fades fast, high piles up into drones.",
            Knob::Size::medium, 12),
      volume (p.apvts, ID::tape::volume, "Volume", "VOLUME: how loud the tape is.", Knob::Size::medium, 13),
      mix (p.apvts, ID::tape::mix, "Mix", "MIX: left is only what you play, right is only the tape.", Knob::Size::medium, 14),
      speed (p.apvts, ID::tape::speed, "Tape Speed",
             "TAPE SPEED: slows, speeds up or reverses the tape; the pitch follows. Clicks in at 1x, 0.5x and -1x. Double-click for 1x.",
             Knob::Size::big, 15),
      freeze (p.apvts, "FREEZE: holds what is on the tape forever. Nothing new goes on, nothing fades. Play over it."),
      erase ("Erase", "ERASE: wipes all three loops."),
      modeSwitch (p.apvts, ID::tape::mode, "LOOP", "ECHO",
                  "LOOP: three tape loops that repeat and slowly wear out. ECHO: a tape echo with three playback heads."),
      syncSwitch (p.apvts, ID::tape::sync, "SYNC", "FREE",
                  "SYNC: loop lengths in beats, following Logic's tempo. FREE: lengths in seconds, ignoring tempo."),
      cassette (p.getTape(), p.apvts.getRawParameterValue (ID::tape::freeze))
{
    for (auto* k : { &time, &feed, &volume, &mix, &speed })
    {
        k->onTurn = onTurn;
        addAndMakeVisible (*k);
    }
    speed.slider.detents = { -1.0, 0.5, 1.0 };
    speed.slider.detentWindow = 0.07;

    addAndMakeVisible (freeze);
    addAndMakeVisible (vu);
    setHint (vu, "VU: how loud the tape is.");

    for (int i = 0; i < 3; ++i)
    {
        const String letter = String::charToString ((juce_wchar) ('A' + i));
        loopLeds[(size_t) i] = std::make_unique<LoopLed> (p.apvts, i,
            "LOOP " + letter + ": click to arm or disarm recording. Lit = everything you play goes onto this loop.");
        addAndMakeVisible (*loopLeds[(size_t) i]);

        lengthReadouts[(size_t) i] = std::make_unique<LedReadout> (3, "Loop " + letter);
        addAndMakeVisible (*lengthReadouts[(size_t) i]);
    }

    erase.onPress = [this]
    {
        for (int i = 0; i < 3; ++i)
            if (auto* param = processor.apvts.getParameter (ID::loop::clear[i]))
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost (1.0f);
            }
    };
    erase.onRelease = [this]
    {
        for (int i = 0; i < 3; ++i)
            if (auto* param = processor.apvts.getParameter (ID::loop::clear[i]))
            {
                param->setValueNotifyingHost (0.0f);
                param->endChangeGesture();
            }
    };
    addAndMakeVisible (erase);
    addAndMakeVisible (modeSwitch);
    addAndMakeVisible (syncSwitch);
    addAndMakeVisible (cassette);

    using S = PianoKey::Symbol;
    const std::array<std::tuple<const char*, S, const char*>, 6> keyDefs { {
        { ID::xport::rec, S::rec, "REC: arm or disarm the loops (the A B C lights). Armed loops record everything you play." },
        { ID::xport::play, S::play, "PLAY: start the tape." },
        { ID::xport::rew, S::rewind, "REWIND: hold to run the tape backwards." },
        { ID::xport::ffwd, S::fastForward, "FAST FORWARD: hold to run the tape at double speed." },
        { ID::xport::stop, S::stop, "STOP: the tape slows down to a halt. Press PLAY to start it again." },
        { ID::xport::pause, S::pause, "PAUSE: quick stop. Press again and the tape lurches back up to speed." },
    } };
    for (size_t i = 0; i < keys.size(); ++i)
    {
        const auto& [id, symbol, hint] = keyDefs[i];
        keys[i] = std::make_unique<PianoKey> (p.apvts, id, symbol, hint);
        addAndMakeVisible (*keys[i]);
    }

    const auto& tape = p.getTape();
    using T = dust::TapeMachine::Transport;
    keys[0]->isLatched = [this]
    {
        const auto sel = (int) processor.apvts.getRawParameterValue (ID::tape::select)->load();
        for (int i = 0; i < 3; ++i)
            if ((sel == 3 || sel == i) && processor.apvts.getRawParameterValue (ID::loop::rec[i])->load() >= 0.5f)
                return true;
        return false;
    };
    keys[1]->isLatched = [&tape] { return tape.transportDisplay.load() == (int) T::playing; };
    keys[4]->isLatched = [&tape] { return tape.transportDisplay.load() == (int) T::stopped; };
    keys[5]->isLatched = [&tape] { return tape.transportDisplay.load() == (int) T::paused; };
}

void FrontPanel::resized()
{
    time.setBounds (60, 34, 100, 108);
    feed.setBounds (190, 34, 100, 108);
    volume.setBounds (320, 34, 100, 108);
    mix.setBounds (60, 158, 100, 108);
    freeze.setBounds (205, 168, 100, 100);
    speed.setBounds (speedBounds);

    vu.setBounds (684, 12, 296, 132);
    for (int i = 0; i < 3; ++i)
    {
        loopLeds[(size_t) i]->setBounds (694 + i * 50, 180, 40, 58);
        lengthReadouts[(size_t) i]->setBounds (694 + i * 96, 346, 84, 58);
    }
    erase.setBounds (862, 194, 104, 30);

    modeSwitch.setBounds (76, 334, 80, 112);
    syncSwitch.setBounds (186, 334, 80, 112);
    cassette.setBounds (320, 332, 360, 168);

    for (size_t i = 0; i < keys.size(); ++i)
        keys[i]->setBounds (212 + (int) i * 96, 512, 96, 54);
}

void FrontPanel::paint (Graphics& g)
{
    background.draw (g, getLocalBounds(), [this] (Image& image, float scale)
    {
        paintPanel (image, scale, getLocalBounds(), 0xdec0de);
        Graphics gi (image);
        gi.addTransform (AffineTransform::scale (scale));
        Art::drawPanelWear (gi, getLocalBounds().toFloat(), 0x5c2a7c);

        drawGrille (gi, { 20.0f, 12.0f, 644.0f, 268.0f });
        for (auto p : { Point<float> (30, 22), Point<float> (654, 22), Point<float> (30, 270), Point<float> (654, 270) })
            Art::drawScrew (gi, p, 4.5f, p.x * 0.01f);

        // Speed scale: ticks every 0.25x, numbers at the useful points, dots at the detents
        const auto c = speedBounds.toFloat().withTrimmedBottom (28.0f).getCentre();
        const auto knobR = 85.0f;
        for (int i = 0; i <= 16; ++i)
        {
            const auto v = -2.0f + 0.25f * (float) i;
            const auto a = rotaryAngle (v, -2.0f, 2.0f);
            const auto major = i % 4 == 0;
            gi.setColour (Art::Colours::silk.withAlpha (major ? 0.9f : 0.5f));
            gi.drawLine (Line<float> (polarPoint (c, knobR + 4.0f, a), polarPoint (c, knobR + (major ? 12.0f : 8.0f), a)), major ? 1.6f : 1.0f);
        }
        gi.setFont (Art::silk (13.0f));
        for (auto [v, text] : std::initializer_list<std::pair<float, const char*>> { { -2.0f, "-2" }, { -1.0f, "-1" }, { 0.0f, "0" },
                                                                                     { 0.5f, ".5" }, { 1.0f, "1" }, { 2.0f, "2" } })
        {
            const auto p = polarPoint (c, knobR + 22.0f, rotaryAngle (v, -2.0f, 2.0f));
            gi.setColour (v == 0.0f ? Art::Colours::ledRed.withAlpha (0.85f) : Art::Colours::silk);
            gi.drawText (text, Rectangle<float> (28.0f, 16.0f).withCentre (p), Justification::centred);
        }

        // Key bed
        const Rectangle<float> bed (204.0f, 506.0f, 592.0f, 62.0f);
        gi.setColour (Colour (0xff060606));
        gi.fillRoundedRectangle (bed, 4.0f);
        Art::drawRecess (gi, bed, 4.0f);

        drawDymoLabel (gi, { 740.0f, 165.0f }, "Loops", 18.0f, 41);
        drawDymoLabel (gi, { 838.0f, 425.0f }, "Length - seconds", 18.0f, 42);
    });
}

void FrontPanel::update (float frameSeconds)
{
    const auto& tape = processor.getTape();
    float tapeLevel = 0.0f;
    for (int i = 0; i < 3; ++i)
    {
        const auto level = tape.loopPeak[(size_t) i].load();
        loopLeds[(size_t) i]->setActivity (level);
        tapeLevel = std::max (tapeLevel, level);
    }
    vu.update (tapeLevel * 0.6f, frameSeconds);

    for (auto& k : keys)
        k->refresh();

    // Loop lengths in seconds (LOOP), or the three echo times (ECHO).
    const auto& state = processor.apvts;
    const auto echoMode = (int) state.getRawParameterValue (ID::tape::mode)->load() == 1;
    const auto sync = (int) state.getRawParameterValue (ID::tape::sync)->load() == 0;
    std::array<float, 3> lengths {};
    for (int i = 0; i < 3; ++i)
        lengths[(size_t) i] = sync ? state.getRawParameterValue (ID::loop::beats[i])->load()
                                   : state.getRawParameterValue (ID::loop::seconds[i])->load();
    const auto longest = std::max ({ lengths[0], lengths[1], lengths[2], 0.01f });
    const auto echoTime = (float) dust::TapeMachine::echoSeconds (state.getRawParameterValue (ID::tape::time)->load());

    for (int i = 0; i < 3; ++i)
    {
        const auto seconds = echoMode ? echoTime * lengths[(size_t) i] / longest : tape.loopSeconds[(size_t) i].load();
        lengthReadouts[(size_t) i]->setText (String (seconds, seconds < 10.0f ? 2 : 1));
        lengthReadouts[(size_t) i]->setCaption ((echoMode ? "Head " : "Loop ") + String::charToString ((juce_wchar) ('A' + i)));
    }
}

//==============================================================================
BackPanel::BackPanel (DustLoopsProcessor& p, std::function<void (Knob&)> onTurn) : processor (p)
{
    int seed = 100;
    const auto addKnob = [&] (const String& id, const String& label, const String& hint, Rectangle<int> bounds) -> Knob*
    {
        knobs.push_back (std::make_unique<Knob> (p.apvts, id, label, hint, Knob::Size::small, ++seed));
        auto* k = knobs.back().get();
        k->onTurn = onTurn;
        addAndMakeVisible (*k);
        layout.push_back ({ k, bounds });
        return k;
    };
    const auto addButton = [&] (const String& id, const String& label, SmallButton::Mode mode, int index, const String& hint,
                                Rectangle<int> bounds) -> SmallButton*
    {
        buttons.push_back (std::make_unique<SmallButton> (p.apvts, id, label, mode, index, hint));
        auto* b = buttons.back().get();
        addAndMakeVisible (*b);
        layout.push_back ({ b, bounds });
        return b;
    };

    // Loops: one column each
    for (int i = 0; i < 3; ++i)
    {
        const String letter = String::charToString ((juce_wchar) ('A' + i));
        const auto cx = 130 + i * 140;
        beatsKnobs[(size_t) i] = addKnob (ID::loop::beats[i], "Beats",
            "LOOP " + letter + " LENGTH in beats (SYNC). Different lengths make the loops drift against each other.", knobBounds (cx, 56));
        secondsKnobs[(size_t) i] = addKnob (ID::loop::seconds[i], "Seconds",
            "LOOP " + letter + " LENGTH in seconds (FREE). Different lengths make the loops drift against each other.", knobBounds (cx, 56));
        addKnob (ID::loop::level[i], "Level", "LOOP " + letter + " LEVEL.", knobBounds (cx, 160));
        addKnob (ID::loop::pan[i], "Pan", "LOOP " + letter + " PAN: where it sits left to right.", knobBounds (cx, 334));
        addKnob (ID::loop::speed[i], "Speed", "LOOP " + letter + " SPEED offset in semitones, on top of TAPE SPEED. 12 = an octave up.",
                 knobBounds (cx, 430));
        addButton (ID::loop::mute[i], "Mute", SmallButton::Mode::toggle, 0, "LOOP " + letter + " MUTE: silences it (it keeps running).",
                   { cx - 64, 532, 62, 26 });
        addButton (ID::loop::clear[i], "Clear", SmallButton::Mode::momentary, 0, "LOOP " + letter + " CLEAR: wipes this loop.",
                   { cx + 2, 532, 62, 26 });
    }

    // Tape character
    struct TypeDef { const char* name; float wow, flutter, hiss, sat, age, drop; };
    static constexpr TypeDef types[] { { "Ferric", 0.2f, 0.15f, 0.12f, 0.3f, 0.25f, 0.05f },
                                       { "Chrome", 0.08f, 0.06f, 0.05f, 0.15f, 0.12f, 0.02f },
                                       { "Dictaphone", 0.55f, 0.4f, 0.35f, 0.55f, 0.55f, 0.3f } };
    for (int t = 0; t < 3; ++t)
    {
        auto* b = addButton (ID::tape::type, types[t].name, SmallButton::Mode::choice, t,
            "TAPE TYPE: sets the tape's colour and a matching amount of wow, flutter, hiss, saturation, age and dropouts.",
            { 566 + t * 136, 36, 128, 26 });
        b->onClick = [this, t]
        {
            const auto& def = types[t];
            const std::pair<const char*, float> values[] { { ID::tape::wow, def.wow }, { ID::tape::flutter, def.flutter },
                                                           { ID::tape::hiss, def.hiss }, { ID::tape::saturation, def.sat },
                                                           { ID::tape::age, def.age }, { ID::tape::dropouts, def.drop } };
            for (auto [id, v] : values)
                if (auto* param = processor.apvts.getParameter (id))
                    param->setValueNotifyingHost (param->convertTo0to1 (v));
        };
    }

    const int cols[] { 600, 710, 820, 930 };
    addKnob (ID::tape::wow, "Wow", "WOW: slow, seasick pitch drift.", knobBounds (cols[0], 72));
    addKnob (ID::tape::flutter, "Flutter", "FLUTTER: fast, trembling pitch wobble.", knobBounds (cols[1], 72));
    addKnob (ID::tape::hiss, "Hiss", "HISS: tape noise. It builds up a little on every pass.", knobBounds (cols[2], 72));
    addKnob (ID::tape::saturation, "Saturate", "SATURATION: tape drive. Warm and squashed, then grimy on every pass.", knobBounds (cols[3], 72));
    addKnob (ID::tape::age, "Age", "AGE: how much treble the tape loses on every pass. High = loops go dark and muffled fast.",
             knobBounds (cols[0], 170));
    addKnob (ID::tape::dropouts, "Dropouts", "DROPOUTS: worn patches where the sound dips. They stay on the loop.", knobBounds (cols[1], 170));
    addKnob (ID::tape::inertia, "Inertia", "INERTIA: how slowly the tape follows the SPEED knob. High = heavy reels.", knobBounds (cols[2], 170));
    addKnob (ID::tape::brake, "Brake", "BRAKE: how long STOP takes to bring the tape to a halt.", knobBounds (cols[3], 170));

    const char* selectNames[] { "A", "B", "C", "All" };
    for (int i = 0; i < 4; ++i)
        addButton (ID::tape::select, selectNames[i], SmallButton::Mode::choice, i,
                   "Which loops the REC key arms and disarms.", { 566 + i * 102, 366, 94, 26 });

    addKnob (ID::global::master, "Master", "MASTER: the plugin's final output level.", knobBounds (cols[0], 420));
    if constexpr (! dust::kIsFx)
    {
        addKnob (ID::synth::attack, "Attack", "ATTACK: how slowly notes fade in.", knobBounds (cols[1], 420));
        addKnob (ID::synth::release, "Release", "RELEASE: how slowly notes fade out after you let go.", knobBounds (cols[2], 420));
    }
}

void BackPanel::resized()
{
    for (auto& [component, bounds] : layout)
        component->setBounds (bounds);
}

void BackPanel::update()
{
    const auto sync = (int) processor.apvts.getRawParameterValue (ID::tape::sync)->load() == 0;
    for (int i = 0; i < 3; ++i)
    {
        beatsKnobs[(size_t) i]->setVisible (sync);
        secondsKnobs[(size_t) i]->setVisible (! sync);
    }
}

void BackPanel::paint (Graphics& g)
{
    background.draw (g, getLocalBounds(), [this] (Image& image, float scale)
    {
        paintPanel (image, scale, getLocalBounds(), 0xbac4);
        Graphics gi (image);
        gi.addTransform (AffineTransform::scale (scale));
        Art::drawPanelWear (gi, getLocalBounds().toFloat(), 0x77aa11);

        // A printed rule between the two sections
        gi.setColour (Art::Colours::line.withAlpha (0.6f));
        gi.drawLine (535.0f, 16.0f, 535.0f, 554.0f, 1.0f);

        drawDymoLabel (gi, { 270.0f, 18.0f }, "Loops", 20.0f, 51);
        for (int i = 0; i < 3; ++i)
            drawDymoLabel (gi, { 130.0f + (float) i * 140.0f, 44.0f }, String::charToString ((juce_wchar) ('A' + i)), 18.0f, 60 + i);
        drawDymoLabel (gi, { 765.0f, 18.0f }, "Tape", 20.0f, 52);
        drawDymoLabel (gi, { 640.0f, 350.0f }, "Rec key arms", 16.0f, 53);
        drawDymoLabel (gi, { 640.0f, 410.0f }, dust::kIsFx ? "Output" : "Output + synth", 16.0f, 54);
    });
}

//==============================================================================
DeckContent::DeckContent (DustLoopsProcessor& p)
    : processor (p),
      front (p, [this] (Knob& k) { showHint (k.valueText()); }),
      back (p, [this] (Knob& k) { showHint (k.valueText()); }),
      browser (p),
      board (p, [this] (Knob& k) { showHint (k.valueText()); })
{
    defaultHint = defaultHintText;
    const auto onTurn = [this] (Knob& k) { showHint (k.valueText()); };

    addAndMakeVisible (presetDisplay);
    setHint (presetDisplay, "PRESET: click to browse the presets. The arrows step through them.");
    presetDisplay.onClick = [this] { showView (view == View::browser ? View::front : View::browser); };
    prevPreset.onPress = [this] { processor.presets->step (-1, browser.visibleOrder()); };
    nextPreset.onPress = [this] { processor.presets->step (1, browser.visibleOrder()); };
    addAndMakeVisible (prevPreset);
    addAndMakeVisible (nextPreset);
    addAndMakeVisible (speedReadout);
    setHint (speedReadout, "SPEED: the tape's actual speed right now (it glides, it doesn't jump).");
    addAndMakeVisible (tempoReadout);
    setHint (tempoReadout, "TEMPO: in SYNC the loops follow Logic's tempo. FREE ignores it.");

    inputKnob = std::make_unique<Knob> (p.apvts, ID::global::inputGain, "Input",
        dust::kIsFx ? "INPUT: level of the track's audio going onto the tape."
                    : "INPUT: level of the side-chained track (pick it in Logic's Side Chain menu at the top of this window).",
        Knob::Size::small, 7);
    inputKnob->onTurn = onTurn;
    addAndMakeVisible (*inputKnob);
    if constexpr (! dust::kIsFx)
    {
        synthKnob = std::make_unique<Knob> (p.apvts, ID::synth::level, "Synth", "SYNTH: level of the built-in synth.", Knob::Size::small, 8);
        synthKnob->onTurn = onTurn;
        addAndMakeVisible (*synthKnob);

        synthStrip = std::make_unique<SynthStrip> (p, onTurn);
        synthStrip->onEdit = [this] { showView (view == View::edit ? View::front : View::edit); };
        addAndMakeVisible (*synthStrip);
        synthEdit = std::make_unique<SynthEditPanel> (p, onTurn);
        addChildComponent (*synthEdit);
    }

    backButton.onPress = [this] { showView (view == View::back ? View::front : View::back); };
    addAndMakeVisible (backButton);
    addAndMakeVisible (front);
    addChildComponent (back);
    browser.onClose = [this] { showView (View::front); };
    browser.onHint = [this] (const String& text) { showHint (text); };
    addChildComponent (browser);
    addAndMakeVisible (board);
    addAndMakeVisible (hintStrip);
    hintStrip.setText (defaultHint);

    back.update();
    updatePresetDisplay();
    addMouseListener (this, true);
    setSize (logicalWidth, logicalHeight);
}

void DeckContent::showView (View v)
{
    view = v;
    front.setVisible (v == View::front);
    back.setVisible (v == View::back);
    browser.setVisible (v == View::browser);
    if (synthEdit != nullptr)
    {
        synthEdit->setVisible (v == View::edit);
        synthEdit->update();
    }
    if (synthStrip != nullptr)
        synthStrip->setEditShown (v == View::edit);
    backButton.setLabel (v == View::back ? "Front" : "Back");
    back.update();

    // The hint strip sits in the gap in the middle of the deck, or along the bottom of the other pages.
    const auto hintY = v == View::front || v == View::back ? deckTop + 292 : deckTop + frontHeight - 34;
    hintStrip.setBounds (20, hintY, 960, 30);
    hintStrip.toFront (false);
    defaultHint = v == View::browser ? String ("PRESETS: click one to hear it while you play, double-click to load it and close. "
                                               "The star marks a favourite.")
                : v == View::edit ? String ("SYNTH EDIT: every synth setting. Point at any control to see what it does. DECK goes back.")
                                  : String (defaultHintText);
    showHint (defaultHint);
}

void DeckContent::resized()
{
    presetDisplay.setBounds (226, 13, 276, 46);
    prevPreset.setBounds (508, 15, 32, 42);
    nextPreset.setBounds (542, 15, 32, 42);
    speedReadout.setBounds (584, 8, 86, 56);
    tempoReadout.setBounds (676, 8, 86, 56);
    inputKnob->setBounds (772, 2, 64, 68);
    if (synthKnob != nullptr)
        synthKnob->setBounds (840, 2, 64, 68);
    backButton.setBounds (918, 22, 66, 28);

    if (synthStrip != nullptr)
        synthStrip->setBounds (0, topHeight, logicalWidth, synthHeight);
    const Rectangle<int> deckArea (0, deckTop, logicalWidth, frontHeight);
    front.setBounds (deckArea);
    back.setBounds (deckArea);
    browser.setBounds (deckArea);
    if (synthEdit != nullptr)
        synthEdit->setBounds (deckArea);
    board.setBounds (0, deckHeight, logicalWidth, PedalBoardView::height);
    showView (view);
}

void DeckContent::updatePresetDisplay()
{
    const auto& pm = *processor.presets;
    const auto* preset = pm.currentPreset();
    String code = "--", name = "Init";
    if (preset != nullptr)
    {
        code = preset->isFactory ? String (pm.currentIndex() + 1).paddedLeft ('0', 2) : String ("US");
        name = preset->name;
    }
    if (pm.isModified())
        name << "*";
    presetDisplay.setProgram (code, name.toUpperCase());
}

void DeckContent::paint (Graphics& g)
{
    background.draw (g, { 0, 0, logicalWidth, topHeight }, [] (Image& image, float scale)
    {
        const Rectangle<int> strip (0, 0, logicalWidth, topHeight);
        paintPanel (image, scale, strip, 0x70b);
        Graphics gi (image);
        gi.addTransform (AffineTransform::scale (scale));
        Art::drawPanelWear (gi, strip.toFloat(), 0x1234);
        Art::drawPrintedImage (gi, Art::logoHorizontal(), { 20.0f, 18.0f, 190.0f, 32.0f }, Art::Colours::brandGold);
        gi.setColour (Colours::black.withAlpha (0.7f));
        gi.fillRect (0.0f, (float) topHeight - 2.0f, (float) logicalWidth, 2.0f);
    });
}

void DeckContent::update (float frameSeconds)
{
    const auto& tape = processor.getTape();
    const auto speedNow = tape.speedDisplay.load();
    speedReadout.setText (String (speedNow, 2));

    const auto sync = (int) processor.apvts.getRawParameterValue (ID::tape::sync)->load() == 0;
    tempoReadout.setText (sync ? String (roundToInt (processor.currentBpm.load())) : String ("FrEE"));

    bool anyArmed = false;
    for (int i = 0; i < 3; ++i)
        anyArmed = anyArmed || processor.apvts.getRawParameterValue (ID::loop::rec[i])->load() >= 0.5f;
    const auto frozen = processor.apvts.getRawParameterValue (ID::tape::freeze)->load() >= 0.5f;
    hintStrip.setRecording (anyArmed && ! frozen && std::abs (speedNow) > 0.05f);

    presetDisplay.advance (frameSeconds);
    updatePresetDisplay();
    if (front.isVisible()) front.update (frameSeconds);
    if (back.isVisible()) back.update();
    if (synthEdit != nullptr && synthEdit->isVisible()) synthEdit->update();

    // Presets loaded from elsewhere (Logic's preset menu, the arrows) show up in the browser's list.
    const auto presetState = std::make_pair (processor.presets->currentIndex(), (int) processor.presets->presets().size());
    if (presetState != lastPresetState)
    {
        lastPresetState = presetState;
        if (browser.isVisible()) browser.refresh();
    }
}

void DeckContent::showHint (const String& text)
{
    hintStrip.setText (text);
}

void DeckContent::mouseEnter (const MouseEvent& e)
{
    for (auto* c = e.eventComponent; c != nullptr && c != this; c = c->getParentComponent())
        if (c->getProperties().contains ("hint"))
        {
            showHint (c->getProperties()["hint"].toString());
            return;
        }
}

void DeckContent::mouseExit (const MouseEvent& e)
{
    if (e.eventComponent != this)
        showHint (defaultHint);
}
} // namespace deck
