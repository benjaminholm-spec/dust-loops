#include "Params.h"

namespace Params
{
namespace
{
using Group = juce::AudioProcessorParameterGroup;
using Float = juce::AudioParameterFloat;
using FloatAttrs = juce::AudioParameterFloatAttributes;

constexpr int version = 1;

juce::String fmt (float v, int decimals, const char* unit)
{
    return juce::String (v, decimals) + unit;
}

// Unit-aware display strings so Logic's automation lanes read sensibly.
enum class Unit { plain, percent, db, hz, seconds, semis, cents, pan, speed };

FloatAttrs attrsFor (Unit unit)
{
    FloatAttrs a;
    switch (unit)
    {
        case Unit::percent: return a.withStringFromValueFunction ([] (float v, int) { return fmt (v * 100.0f, 0, " %"); });
        case Unit::db:      return a.withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return v <= -47.9f ? juce::String ("-inf") : fmt (v, 1, ""); });
        case Unit::hz:      return a.withLabel ("Hz").withStringFromValueFunction ([] (float v, int) { return v >= 1000.0f ? fmt (v / 1000.0f, 2, "k") : fmt (v, v < 10.0f ? 2 : 0, ""); });
        case Unit::seconds: return a.withLabel ("s").withStringFromValueFunction ([] (float v, int) { return v < 1.0f ? fmt (v * 1000.0f, 0, " ms") : fmt (v, 2, ""); });
        case Unit::semis:   return a.withLabel ("st").withStringFromValueFunction ([] (float v, int) { return fmt (v, 2, ""); });
        case Unit::cents:   return a.withLabel ("ct").withStringFromValueFunction ([] (float v, int) { return fmt (v, 1, ""); });
        case Unit::pan:     return a.withStringFromValueFunction ([] (float v, int) {
                                if (std::abs (v) < 0.005f) return juce::String ("C");
                                return fmt (std::abs (v) * 100.0f, 0, v < 0.0f ? " L" : " R"); });
        case Unit::speed:   return a.withStringFromValueFunction ([] (float v, int) { return fmt (v, 2, "x"); });
        case Unit::plain:   break;
    }
    return a;
}

struct Builder
{
    Group& group;

    void flt (const juce::String& id, const juce::String& name, float min, float max, float def,
              Unit unit = Unit::percent, float skewCentre = 0.0f)
    {
        juce::NormalisableRange<float> range (min, max);
        if (skewCentre > min && skewCentre < max)
            range.setSkewForCentre (skewCentre);
        group.addChild (std::make_unique<Float> (juce::ParameterID { id, version }, name, range, def, attrsFor (unit)));
    }

    void choice (const juce::String& id, const juce::String& name, const juce::StringArray& options, int def)
    {
        group.addChild (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, version }, name, options, def));
    }

    void boolean (const juce::String& id, const juce::String& name, bool def)
    {
        group.addChild (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, version }, name, def));
    }

    void integer (const juce::String& id, const juce::String& name, int min, int max, int def)
    {
        group.addChild (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { id, version }, name, min, max, def));
    }
};

std::unique_ptr<Group> makeGroup (const juce::String& id, const juce::String& name)
{
    return std::make_unique<Group> (id, name, " | ");
}

std::unique_ptr<Group> globalGroup()
{
    auto g = makeGroup ("global", "Global");
    Builder b { *g };
    b.flt (ID::global::master, "Master", -48.0f, 6.0f, 0.0f, Unit::db);
    b.flt (ID::global::inputGain, "Input Gain", -48.0f, 24.0f, 0.0f, Unit::db);
    return g;
}

std::unique_ptr<Group> synthGroup()
{
    using namespace ID::synth;
    auto g = makeGroup ("synth", "Synth");
    Builder b { *g };
    b.choice (model, "Synth Model", { "DCO-Poly", "VCO-Poly", "Ensemble" }, 0);
    b.choice (voiceMode, "Synth Voice Mode", { "Poly", "Mono", "Legato" }, 0);
    b.flt (glide, "Synth Glide", 0.0f, 2.0f, 0.0f, Unit::seconds, 0.3f);
    b.integer (bendRange, "Synth Bend Range", 0, 12, 2);
    b.flt (level, "Synth Level", -48.0f, 6.0f, -6.0f, Unit::db);
    b.flt (slop, "Synth Slop", 0.0f, 1.0f, 0.2f);
    // Macros rest at 0 (no effect); the mod wheel pushes MOTION up.
    static constexpr const char* macroNames[] { "Bloom", "Motion", "Warp", "Haze" };
    for (int i = 0; i < 4; ++i)
        b.flt (macro[i], macroNames[i], 0.0f, 1.0f, 0.0f);

    b.flt (saw, "DCO Saw", 0.0f, 1.0f, 1.0f);
    b.flt (pulse, "DCO Pulse", 0.0f, 1.0f, 0.0f);
    b.flt (pw, "Pulse Width", 0.05f, 0.95f, 0.5f);
    b.flt (pwm, "DCO PWM", 0.0f, 1.0f, 0.0f);
    b.flt (sub, "DCO Sub", 0.0f, 1.0f, 0.0f);
    b.flt (noise, "DCO Noise", 0.0f, 1.0f, 0.0f);

    b.flt (osc1Wave, "VCO 1 Wave", 0.0f, 1.0f, 0.0f);
    b.flt (osc2Wave, "VCO 2 Wave", 0.0f, 1.0f, 0.0f);
    b.integer (osc2Semi, "VCO 2 Semitones", -24, 24, 0);
    b.flt (osc2Detune, "VCO 2 Detune", -50.0f, 50.0f, 7.0f, Unit::cents);
    b.flt (osc2Level, "VCO 2 Level", 0.0f, 1.0f, 0.8f);
    b.boolean (sync, "VCO Sync", false);
    b.flt (polyModPitch, "Poly-Mod Pitch", 0.0f, 1.0f, 0.0f);
    b.flt (polyModPw, "Poly-Mod PW", 0.0f, 1.0f, 0.0f);
    b.flt (drive, "VCO Filter Drive", 0.0f, 1.0f, 0.3f);

    b.choice (ensFeet, "Ensemble Registers", { "8'", "8' + 4'", "16' + 8'", "16' + 8' + 4'" }, 1);
    b.flt (ensBright, "Ensemble Brightness", 0.0f, 1.0f, 0.5f);
    b.flt (ensEnsemble, "Ensemble Depth", 0.0f, 1.0f, 0.8f);

    b.flt (cutoff, "Cutoff", 20.0f, 20000.0f, 3000.0f, Unit::hz, 1000.0f);
    b.flt (resonance, "Resonance", 0.0f, 1.0f, 0.15f);
    b.flt (hpf, "High-Pass", 20.0f, 2000.0f, 20.0f, Unit::hz, 200.0f);
    b.flt (filterEnv, "Filter Env Amount", -1.0f, 1.0f, 0.25f);
    b.flt (keyTrack, "Filter Key Track", 0.0f, 1.0f, 0.5f);
    b.flt (velToFilter, "Velocity > Filter", 0.0f, 1.0f, 0.3f);
    b.flt (atToFilter, "Aftertouch > Filter", 0.0f, 1.0f, 0.4f);
    b.flt (fAttack, "Filter Attack", 0.001f, 10.0f, 0.01f, Unit::seconds, 0.5f);
    b.flt (fDecay, "Filter Decay", 0.001f, 10.0f, 0.6f, Unit::seconds, 0.8f);
    b.flt (fSustain, "Filter Sustain", 0.0f, 1.0f, 0.4f);
    b.flt (fRelease, "Filter Release", 0.001f, 15.0f, 0.8f, Unit::seconds, 1.0f);

    b.flt (attack, "Attack", 0.001f, 10.0f, 0.01f, Unit::seconds, 0.5f);
    b.flt (decay, "Decay", 0.001f, 10.0f, 0.5f, Unit::seconds, 0.8f);
    b.flt (sustain, "Sustain", 0.0f, 1.0f, 0.8f);
    b.flt (release, "Release", 0.001f, 15.0f, 1.2f, Unit::seconds, 1.0f);
    b.flt (velToAmp, "Velocity > Amp", 0.0f, 1.0f, 0.4f);

    b.flt (lfoRate, "LFO Rate", 0.02f, 20.0f, 0.6f, Unit::hz, 1.5f);
    b.flt (lfoDelay, "LFO Delay", 0.0f, 5.0f, 0.0f, Unit::seconds, 1.0f);
    b.choice (lfoShape, "LFO Shape", { "Triangle", "Sine", "Square", "Random" }, 0);
    b.flt (lfoPitch, "LFO > Pitch", 0.0f, 1.0f, 0.0f);
    b.flt (lfoFilter, "LFO > Filter", 0.0f, 1.0f, 0.0f);
    b.flt (lfoPwm, "LFO > PWM", 0.0f, 1.0f, 0.0f);

    b.choice (chorus, "Chorus", { "Off", "I", "II", "I+II" }, 1);
    b.flt (chorusMix, "Chorus Mix", 0.0f, 1.0f, 0.8f);
    return g;
}

std::unique_ptr<Group> atmosGroup()
{
    using namespace ID::atmos;
    auto g = makeGroup ("atmos", "Atmos");
    Builder b { *g };
    b.flt (level, "Atmos Level", 0.0f, 1.0f, 0.0f);
    b.flt (crackle, "Atmos Crackle", 0.0f, 1.0f, 0.6f);
    b.flt (rain, "Atmos Rain", 0.0f, 1.0f, 0.3f);
    b.flt (hum, "Atmos Hum", 0.0f, 1.0f, 0.2f);
    b.choice (humFreq, "Atmos Hum Freq", { "50 Hz", "60 Hz" }, 0);
    b.flt (tone, "Atmos Tone", 0.0f, 1.0f, 0.5f);
    b.boolean (always, "Atmos Always On", false);
    return g;
}

std::unique_ptr<Group> tapeGroup()
{
    using namespace ID::tape;
    auto g = makeGroup ("tape", "Tape");
    Builder b { *g };
    b.choice (mode, "Tape Mode", { "Loop", "Echo" }, 0);
    b.flt (ID::tape::time, "Tape Time", 0.0f, 1.0f, 0.5f);
    b.flt (feed, "Tape Feed", 0.0f, 1.0f, 0.7f);
    b.flt (mix, "Tape Mix", 0.0f, 1.0f, 0.5f);
    b.flt (volume, "Tape Volume", -24.0f, 12.0f, 0.0f, Unit::db);
    b.flt (speed, "Tape Speed", -2.0f, 2.0f, 1.0f, Unit::speed);
    b.flt (inertia, "Tape Inertia", 0.0f, 1.0f, 0.4f);
    b.boolean (freeze, "Tape Freeze", false);
    b.choice (sync, "Tape Sync", { "Sync", "Free" }, 0);
    b.choice (select, "Tape Selected Loop", { "A", "B", "C", "All" }, 3);
    b.flt (brake, "Tape Brake Time", 0.05f, 5.0f, 0.8f, Unit::seconds, 1.0f);
    b.choice (type, "Tape Type", { "Ferric", "Chrome", "Worn Dictaphone" }, 0);
    b.flt (wow, "Tape Wow", 0.0f, 1.0f, 0.2f);
    b.flt (flutter, "Tape Flutter", 0.0f, 1.0f, 0.15f);
    b.flt (hiss, "Tape Hiss", 0.0f, 1.0f, 0.1f);
    b.flt (saturation, "Tape Saturation", 0.0f, 1.0f, 0.25f);
    b.flt (age, "Tape Age", 0.0f, 1.0f, 0.25f);
    b.flt (dropouts, "Tape Dropouts", 0.0f, 1.0f, 0.05f);
    return g;
}

std::unique_ptr<Group> transportGroup()
{
    using namespace ID::xport;
    auto g = makeGroup ("xport", "Transport");
    Builder b { *g };
    b.boolean (rec, "Key REC", false);
    b.boolean (play, "Key PLAY", false);
    b.boolean (rew, "Key REWIND (reverse)", false);
    b.boolean (ffwd, "Key FAST FWD (double)", false);
    b.boolean (stop, "Key STOP", false);
    b.boolean (pause, "Key PAUSE", false);
    return g;
}

std::unique_ptr<Group> loopGroup (int i)
{
    static constexpr float defaultBeats[] { 16.0f, 13.0f, 11.0f };
    static constexpr float defaultSeconds[] { 7.3f, 11.1f, 17.9f };
    static constexpr float defaultPan[] { -0.5f, 0.0f, 0.5f };

    const juce::String letter = juce::String::charToString ((juce::juce_wchar) ('A' + i));
    auto g = makeGroup ("loop" + letter, "Loop " + letter);
    Builder b { *g };
    const auto n = "Loop " + letter + " ";
    b.integer (ID::loop::beats[i], n + "Beats", 1, 64, (int) defaultBeats[i]);
    b.flt (ID::loop::seconds[i], n + "Seconds", 0.5f, 60.0f, defaultSeconds[i], Unit::seconds, 10.0f);
    b.flt (ID::loop::level[i], n + "Level", -48.0f, 6.0f, -3.0f, Unit::db);
    b.flt (ID::loop::pan[i], n + "Pan", -1.0f, 1.0f, defaultPan[i], Unit::pan);
    b.flt (ID::loop::speed[i], n + "Speed Offset", -12.0f, 12.0f, 0.0f, Unit::semis);
    b.boolean (ID::loop::mute[i], n + "Mute", false);
    b.boolean (ID::loop::rec[i], n + "Rec Arm", true);
    b.boolean (ID::loop::clear[i], n + "Clear", false);
    return g;
}

std::unique_ptr<Group> pedalGroup (int i)
{
    using namespace ID::pedal;
    static constexpr const char* titles[] { "Dirt", "Warble", "Drift Filter", "Cloud", "Hall" };
    // Default board: only HALL is on, and it leaks a little into the tape feedback.
    static constexpr bool defaultOn[] { false, false, false, false, true };
    static constexpr bool defaultLoop[] { false, false, false, false, true };
    static constexpr float defaultLoopAmt[] { 0.3f, 0.3f, 0.3f, 0.3f, 0.15f };

    const juce::String title = titles[i];
    auto g = makeGroup (juce::String ("pedal.") + names[i], "Pedal " + title);
    Builder b { *g };
    const auto n = title + " ";
    b.boolean (id (i, on), n + "On", defaultOn[i]);
    b.boolean (id (i, loop), n + "In Loop", defaultLoop[i]);
    b.flt (id (i, loopAmt), n + "Loop Amount", 0.0f, 1.0f, defaultLoopAmt[i]);

    switch (i)
    {
        case 0:
            b.flt (dirt::drive, n + "Drive", 0.0f, 1.0f, 0.4f);
            b.flt (dirt::blend, n + "Tape > Fuzz > Crush", 0.0f, 1.0f, 0.0f);
            b.flt (dirt::tone, n + "Tone", 0.0f, 1.0f, 0.5f);
            b.flt (dirt::mix, n + "Mix", 0.0f, 1.0f, 1.0f);
            break;
        case 1:
            b.flt (warble::rate, n + "Rate", 0.05f, 12.0f, 0.6f, Unit::hz, 1.0f);
            b.flt (warble::depth, n + "Depth", 0.0f, 1.0f, 0.3f);
            b.choice (warble::mode, n + "Mode", { "Chorus", "Vibrato" }, 0);
            b.flt (warble::mix, n + "Mix", 0.0f, 1.0f, 0.5f);
            break;
        case 2:
            b.flt (drift::freq, n + "Frequency", 40.0f, 12000.0f, 1200.0f, Unit::hz, 800.0f);
            b.flt (drift::reso, n + "Resonance", 0.0f, 1.0f, 0.3f);
            b.choice (drift::mode, n + "Mode", { "Lowpass", "Bandpass" }, 0);
            // 1/120 Hz = one sweep every two minutes
            b.flt (drift::lfoRate, n + "LFO Rate", 1.0f / 120.0f, 2.0f, 0.05f, Unit::hz, 0.1f);
            b.flt (drift::lfoDepth, n + "LFO Depth", 0.0f, 1.0f, 0.4f);
            b.flt (drift::envAmt, n + "Envelope", -1.0f, 1.0f, 0.0f);
            break;
        case 3:
            b.choice (cloud::pitch, n + "Pitch", { "Octave Down", "Fifth Up", "Octave Up", "Octave Up + Down" }, 2);
            b.flt (cloud::smear, n + "Smear", 0.0f, 1.0f, 0.5f);
            b.flt (cloud::density, n + "Density", 0.0f, 1.0f, 0.5f);
            b.flt (cloud::mix, n + "Mix", 0.0f, 1.0f, 0.4f);
            break;
        case 4:
            b.flt (hall::decay, n + "Decay", 0.5f, 60.0f, 8.0f, Unit::seconds, 8.0f);
            b.flt (hall::tone, n + "Tone", 0.0f, 1.0f, 0.35f);
            b.flt (hall::mix, n + "Mix", 0.0f, 1.0f, 0.3f);
            b.boolean (hall::freeze, n + "Freeze", false);
            break;
        default: jassertfalse;
    }
    return g;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createLayout (bool includeSynth)
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (globalGroup());
    if (includeSynth)
        layout.add (synthGroup());
    if (includeSynth)
        layout.add (atmosGroup());
    layout.add (tapeGroup());
    layout.add (transportGroup());
    for (int i = 0; i < ID::loop::count; ++i)
        layout.add (loopGroup (i));
    for (int i = 0; i < ID::pedal::count; ++i)
        layout.add (pedalGroup (i));
    return layout;
}
} // namespace Params
