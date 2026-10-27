#pragma once

#include <array>
#include <juce_audio_basics/juce_audio_basics.h>
#include "DSP/Primitives.h"
#include "Synth/BlepOsc.h"
#include "Synth/Chorus.h"
#include "Synth/Atmos.h"

// The built-in synth: 8 voices (or one, in Mono/Legato) in three models, with a shared LFO, high-pass,
// chorus and the Atmos layer.
//   DCO-Poly  Juno-style: one rock-steady oscillator with saw, pulse/PWM, sub and noise, plus BBD chorus.
//   VCO-Poly  Prophet-style: two drifting oscillators, hard sync, poly-mod and a driven filter.
//   Ensemble  string machine: divide-down saw registers, brightness and an ensemble chorus.
// The four macros reach across many settings at once: BLOOM (slow swells), MOTION (movement; the mod
// wheel pushes it up), WARP (detune and wobble), HAZE (darker, softer, further away).
namespace dust
{
// Raw parameter values, read once per block.
struct SynthSettings
{
    int model = 0, voiceMode = 0, ensFeet = 1, lfoShape = 0, chorus = 1;
    float glide = 0.0f, bendRange = 2.0f, slop = 0.2f;
    std::array<float, 4> macro {};
    float saw = 1.0f, pulse = 0.0f, pw = 0.5f, pwm = 0.0f, sub = 0.0f, noise = 0.0f;
    float osc1Wave = 0.0f, osc2Wave = 0.0f, osc2Semi = 0.0f, osc2Detune = 7.0f, osc2Level = 0.8f;
    float polyModPitch = 0.0f, polyModPw = 0.0f, drive = 0.3f;
    bool sync = false;
    float ensBright = 0.5f, ensEnsemble = 0.8f;
    float cutoff = 3000.0f, resonance = 0.15f, hpf = 20.0f, filterEnv = 0.25f, keyTrack = 0.5f, velToFilter = 0.3f, atToFilter = 0.4f;
    float fAttack = 0.01f, fDecay = 0.6f, fSustain = 0.4f, fRelease = 0.8f;
    float attack = 0.01f, decay = 0.5f, sustain = 0.8f, release = 1.2f, velToAmp = 0.4f;
    float lfoRate = 0.6f, lfoDelay = 0.0f, lfoPitch = 0.0f, lfoFilter = 0.0f, lfoPwm = 0.0f;
    float chorusMix = 0.8f;
    AtmosSettings atmos;
};

enum class Model { dco, vco, ensemble };

// Block-constant values shared by every voice, with the macros already applied.
struct VoiceFrame
{
    Model model = Model::dco;
    BlepOsc::Levels dco, vco1, vco2;
    float dcoNoise = 0.0f, pw = 0.5f, osc2Offset = 0.0f, osc2Level = 0.8f, polyModPitch = 0.0f, polyModPw = 0.0f, drive = 0.0f;
    bool sync = false;
    std::array<float, 3> ensGains {};
    float ensBrightHz = 3000.0f;
    float mixGain = 1.0f, hazeNoise = 0.0f;
    float cutoffOct = 11.0f, k = 0.0f, envOct = 0.0f, keyTrack = 0.5f, velOct = 0.0f, pressureOct = 0.0f;
    float fA = 0.01f, fD = 0.5f, fS = 0.5f, fR = 1.0f, aA = 0.01f, aD = 0.5f, aS = 1.0f, aR = 1.0f;
    float velToAmp = 0.4f, lfoDelay = 0.0f, glideTime = 0.0f, driftCents = 0.0f, slop = 0.0f, warpCents = 0.0f;
};

// Values that change every control tick (32 samples).
struct ControlFrame
{
    float lfo = 0.0f, vibSemis = 0.0f, lfoFilterOct = 0.0f, pwmDco = 0.0f, pwmVco = 0.0f;
    float wobbleSemis = 0.0f, bendSemis = 0.0f, pressure = 0.0f;
};

class Voice
{
public:
    void prepare (float sampleRate, uint32_t seed, float panPosition)
    {
        sr = sampleRate;
        rng = Random (seed);
        const auto controlRate = sampleRate / 32.0f;
        drift1.prepare (controlRate, 0.3f, 1.4f, 0.4f, rng.next());
        drift2.prepare (controlRate, 0.3f, 1.4f, 0.4f, rng.next());
        ampEnv.setSampleRate (sampleRate);
        filterEnv.setSampleRate (sampleRate);
        cutoffSmooth.setTime (0.004f, sampleRate);
        const auto angle = (panPosition + 1.0f) * 0.25f * 3.14159265f;
        panL = std::cos (angle) * 1.41421356f;
        panR = std::sin (angle) * 1.41421356f;
        kill();
    }

    bool isActive() const noexcept { return ampEnv.isActive(); }
    float currentPitch() const noexcept { return pitchNow; }

    // glideFrom < 0: start at the note's pitch.
    void start (int midiNote, float vel, float glideFrom, uint32_t order)
    {
        if (! isActive())
        {
            filter.reset();
            cutoffSmooth.value = -100.0f; // jump to the first target
            for (auto& o : osc)
                o.reset (rng.uniform());
            brightState = 0.0f;
            lastOsc2 = 0.0f;
        }
        note = midiNote;
        velocity = vel;
        gate = true;
        sustained = false;
        startOrder = order;
        polyPressure = 0.0f;
        timeOn = 0.0f;
        pitchNow = glideFrom >= 0.0f ? glideFrom : (float) midiNote;

        // Slop: no two notes land quite the same.
        noteCents1 = rng.bipolar() * 6.0f;
        noteCents2 = rng.bipolar() * 6.0f;
        noteCutoff = rng.bipolar() * 0.3f;
        envScale = 1.0f + rng.bipolar() * 0.18f;
        warpRandom = rng.bipolar();

        ampEnv.noteOn();
        filterEnv.noteOn();
    }

    void glideTo (int midiNote, bool glide) noexcept
    {
        note = midiNote;
        if (! glide)
            pitchNow = (float) midiNote;
    }

    void release() noexcept
    {
        gate = false;
        sustained = false;
        ampEnv.noteOff();
        filterEnv.noteOff();
    }

    void kill() noexcept
    {
        ampEnv.reset();
        filterEnv.reset();
        note = -1;
        gate = sustained = false;
    }

    void render (float* outL, float* outR, int n, const VoiceFrame& f, const ControlFrame& c) noexcept
    {
        if (! isActive())
            return;

        // ---- control rate ----
        timeOn += (float) n / sr;
        if (f.glideTime > 0.001f)
            pitchNow += ((float) note - pitchNow) * (1.0f - std::exp (-(float) n / (f.glideTime * sr)));
        else
            pitchNow = (float) note;

        const auto lfoFade = f.lfoDelay > 0.001f ? std::min (1.0f, timeOn / f.lfoDelay) : 1.0f;
        const auto lfo = c.lfo * lfoFade;
        const auto slop = f.slop;
        const auto d1 = (drift1.process() * f.driftCents + noteCents1 * slop) * 0.01f;
        const auto d2 = (drift2.process() * f.driftCents + noteCents2 * slop) * 0.01f;
        const auto pitch = pitchNow + c.bendSemis + lfo * c.vibSemis + c.wobbleSemis + warpRandom * f.warpCents * 0.01f;
        const auto hzOf = [] (float semis) { return 440.0f * std::exp2 ((semis - 69.0f) / 12.0f); };

        const auto es = 1.0f + (envScale - 1.0f) * slop;
        ampEnv.setParameters (f.aA * es, f.aD * es, f.aS, f.aR * es);
        filterEnv.setParameters (f.fA * es, f.fD * es, f.fS, f.fR * es);

        const auto pressure = std::max (c.pressure, polyPressure);
        const auto baseOct = f.cutoffOct + f.keyTrack * ((float) note - 60.0f) / 12.0f + f.velOct * (velocity - 0.6f)
                           + f.pressureOct * pressure + lfo * c.lfoFilterOct + noteCutoff * slop;
        const auto maxOct = std::log2 (std::min (sr * 0.45f, 20000.0f));
        const auto velGain = 1.0f - f.velToAmp + f.velToAmp * velocity;
        const auto outGain = velGain * 0.5f * f.mixGain;

        float inc[3] {};
        float pw1 = f.pw, pw2 = f.pw;
        switch (f.model)
        {
            case Model::dco:
                inc[0] = hzOf (pitch + d1 * 0.3f) / sr;
                pw1 = f.pw + c.pwmDco * lfo;
                break;
            case Model::vco:
                inc[0] = hzOf (pitch + d1) / sr;
                inc[1] = hzOf (pitch + f.osc2Offset + d2) / sr;
                pw1 = f.pw + c.pwmVco * lfo;
                pw2 = f.pw - c.pwmVco * lfo;
                break;
            case Model::ensemble:
                inc[1] = hzOf (pitch + d1 * 0.5f) / sr;
                inc[0] = inc[1] * 0.5f;
                inc[2] = inc[1] * 2.0f;
                break;
        }
        const auto brightCoef = 1.0f - std::exp (-twoPi * std::min (f.ensBrightHz, sr * 0.45f) / sr);

        // ---- audio rate ----
        for (int i = 0; i < n; ++i)
        {
            const auto fEnv = filterEnv.process();
            const auto aEnv = ampEnv.process();

            float x = 0.0f;
            switch (f.model)
            {
                case Model::dco:
                    x = osc[0].process (inc[0], pw1, f.dco) + f.dcoNoise * rng.bipolar();
                    break;
                case Model::vco:
                {
                    const auto pmPw = f.polyModPw * fEnv * 0.45f;
                    const auto o2 = osc[1].process (inc[1], pw2 + pmPw, f.vco2);
                    auto inc1 = inc[0];
                    if (f.polyModPitch > 0.0f)
                        inc1 *= std::exp2 (f.polyModPitch * fEnv * 2.0f) * (1.0f + f.polyModPitch * 0.6f * lastOsc2);
                    const auto o1 = osc[0].process (inc1, pw1 + pmPw, f.vco1, f.sync ? osc[1].wrapTau : -1.0f);
                    lastOsc2 = o2;
                    x = o1 + o2 * f.osc2Level;
                    break;
                }
                case Model::ensemble:
                {
                    for (size_t k = 0; k < 3; ++k)
                        if (f.ensGains[k] > 0.0f)
                            x += f.ensGains[k] * osc[k].process (inc[k], 0.5f, {});
                    brightState += (x - brightState) * brightCoef;
                    x = brightState;
                    break;
                }
            }
            x += f.hazeNoise * rng.bipolar();
            if (f.drive > 0.0f)
                x = fastTanh (x * (1.0f + 3.0f * f.drive)) * (1.6f - 0.5f * f.drive);

            const auto targetOct = std::clamp (baseOct + f.envOct * fEnv, 4.6f, maxOct);
            if (cutoffSmooth.value < 0.0f) cutoffSmooth.value = targetOct;
            const auto g = std::tan (3.14159265f * std::exp2 (cutoffSmooth.process (targetOct)) / sr);
            const auto y = filter.process (x * 0.45f, g, f.k) * aEnv * outGain;
            outL[i] += y * panL;
            outR[i] += y * panR;

            if (! ampEnv.isActive())
            {
                kill();
                break;
            }
        }
    }

    int note = -1;
    bool gate = false, sustained = false;
    uint32_t startOrder = 0;
    float polyPressure = 0.0f;

private:
    float sr = 48000.0f;
    Random rng;
    std::array<BlepOsc, 3> osc;
    LadderFilter filter;
    Envelope ampEnv, filterEnv;
    RandomWalk drift1, drift2;
    OnePole cutoffSmooth;
    float velocity = 1.0f, pitchNow = 60.0f, timeOn = 0.0f, panL = 1.0f, panR = 1.0f;
    float noteCents1 = 0.0f, noteCents2 = 0.0f, noteCutoff = 0.0f, envScale = 1.0f, warpRandom = 0.0f;
    float brightState = 0.0f, lastOsc2 = 0.0f;
};

class SynthEngine
{
public:
    static constexpr int numVoices = 8, tick = 32;

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        static constexpr float pans[numVoices] { -0.3f, 0.3f, -0.12f, 0.12f, -0.45f, 0.45f, -0.22f, 0.22f };
        for (size_t i = 0; i < voices.size(); ++i)
            voices[i].prepare (sr, 0x51a7u + (uint32_t) i * 7919u, pans[i]);
        chorus.prepare (sr);
        ensemble.prepare (sr);
        atmos.prepare (sr);
        wobble.prepare (sr / (float) tick, 0.6f, 2.2f, 0.7f, 0xb0bu);
        for (auto& f : highPass) f.reset();
        heldCount = 0;
        sustainDown = false;
        modWheel = modWheelTarget = pressure = pressureTarget = 0.0f;
        bendNorm = 0.0f;
        lastPitch = -1.0f;
    }

    bool anyActive() const noexcept { return numActiveVoices() > 0; }

    int numActiveVoices() const noexcept
    {
        int n = 0;
        for (auto& v : voices)
            n += v.isActive() ? 1 : 0;
        return n;
    }

    // Writes (does not add) the synth into l/r.
    void process (float* l, float* r, int n, const juce::MidiBuffer& midi, const SynthSettings& s) noexcept
    {
        std::fill (l, l + n, 0.0f);
        std::fill (r, r + n, 0.0f);
        settings = &s;
        buildFrame (s);

        if (s.voiceMode != lastVoiceMode)
        {
            for (auto& v : voices) v.release();
            heldCount = 0;
            lastVoiceMode = s.voiceMode;
        }

        int pos = 0;
        for (const auto metadata : midi)
        {
            const auto at = std::clamp (metadata.samplePosition, 0, n);
            if (at > pos)
            {
                renderVoices (l + pos, r + pos, at - pos);
                pos = at;
            }
            handle (metadata.getMessage());
        }
        if (pos < n)
            renderVoices (l + pos, r + pos, n - pos);

        for (int ch = 0; ch < 2; ++ch)
        {
            auto* x = ch == 0 ? l : r;
            auto& f = highPass[(size_t) ch];
            f.setCoefficients (s.hpf, 0.707f, sr);
            for (int i = 0; i < n; ++i)
                x[i] = f.process (x[i]).high;
        }
        ensemble.process (l, r, n, frame.model == Model::ensemble ? s.ensEnsemble : 0.0f);
        chorus.process (l, r, n, s.chorus, s.chorusMix);
        atmos.process (l, r, n, s.atmos, anyActive());
    }

private:
    void buildFrame (const SynthSettings& s) noexcept
    {
        auto& f = frame;
        const auto bloom = s.macro[0], warp = s.macro[2], haze = s.macro[3];
        f.model = (Model) std::clamp (s.model, 0, 2);

        f.dco = { s.saw, s.pulse, s.sub * 0.8f };
        f.dcoNoise = s.noise * 0.7f;
        f.vco1 = { 1.0f - s.osc1Wave, s.osc1Wave, 0.0f };
        f.vco2 = { 1.0f - s.osc2Wave, s.osc2Wave, 0.0f };
        f.pw = s.pw;
        f.osc2Offset = s.osc2Semi + (s.osc2Detune + warp * 14.0f) * 0.01f;
        f.osc2Level = s.osc2Level;
        f.sync = s.sync;
        f.polyModPitch = s.polyModPitch;
        f.polyModPw = s.polyModPw;
        f.drive = f.model == Model::vco ? s.drive : 0.0f;

        static constexpr float feet[4][3] { { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.65f }, { 0.8f, 1.0f, 0.0f }, { 0.7f, 1.0f, 0.55f } };
        const auto& reg = feet[std::clamp (s.ensFeet, 0, 3)];
        const auto regSum = reg[0] + reg[1] + reg[2];
        for (size_t k = 0; k < 3; ++k)
            f.ensGains[k] = reg[k] / regSum * 1.3f;
        f.ensBrightHz = 700.0f * std::exp2 (s.ensBright * 4.2f);

        switch (f.model)
        {
            case Model::dco:      f.mixGain = 1.6f / std::max (1.0f, s.saw + s.pulse + 0.8f * s.sub + 0.5f * s.noise); break;
            case Model::vco:      f.mixGain = 1.6f / std::max (1.0f, 1.0f + s.osc2Level); break;
            case Model::ensemble: f.mixGain = 1.5f; break;
        }
        f.hazeNoise = 0.05f * haze;

        // Filter: HAZE pulls it down and softens the resonance; BLOOM adds a slow envelope swell.
        f.cutoffOct = std::log2 (std::max (20.0f, s.cutoff)) - 2.5f * haze - 0.5f * bloom;
        f.k = s.resonance * 3.95f * (1.0f - 0.5f * haze);
        f.envOct = s.filterEnv * 6.0f + 1.5f * bloom;
        f.keyTrack = s.keyTrack;
        f.velOct = s.velToFilter * 4.0f * (1.0f - haze);
        f.pressureOct = s.atToFilter * 3.0f;

        // BLOOM stretches short envelope times toward a slow pad (short settings move furthest).
        const auto stretch = [bloom] (float seconds, float target)
        {
            return seconds < target ? seconds * std::pow (target / std::max (seconds, 0.001f), bloom) : seconds;
        };
        const auto softAttack = 0.12f * haze;
        f.aA = std::max (softAttack, stretch (s.attack, 2.5f));
        f.aD = stretch (s.decay, 4.0f);
        f.aS = s.sustain + (1.0f - s.sustain) * bloom * 0.85f;
        f.aR = stretch (s.release, 6.0f);
        f.fA = std::max (softAttack, stretch (s.fAttack, 3.0f));
        f.fD = stretch (s.fDecay, 5.0f);
        f.fS = s.fSustain + (0.7f - s.fSustain) * bloom * 0.5f;
        f.fR = stretch (s.fRelease, 6.0f);
        f.velToAmp = s.velToAmp * (1.0f - 0.6f * haze);

        f.lfoDelay = s.lfoDelay;
        f.glideTime = s.glide;
        static constexpr float modelDrift[3] { 2.0f, 9.0f, 3.0f }; // cents of wander at full slop
        const auto slop = std::min (1.0f, s.slop + 0.5f * warp);
        f.slop = slop;
        f.driftCents = modelDrift[(size_t) f.model] * slop + 6.0f * warp;
        f.warpCents = 22.0f * warp;
    }

    ControlFrame controlTick (int n) noexcept
    {
        const auto& s = *settings;
        const auto smooth = 1.0f - std::exp (-(float) n / (0.03f * sr));
        modWheel += (modWheelTarget - modWheel) * smooth;
        pressure += (pressureTarget - pressure) * smooth;
        const auto motion = std::min (1.0f, s.macro[1] + modWheel);

        // LFO (value at the start of the tick)
        float value = 0.0f;
        switch (s.lfoShape)
        {
            case 1:  value = std::sin (twoPi * lfoPhase); break;
            case 2:  value = lfoPhase < 0.5f ? 1.0f : -1.0f; break;
            case 3:  randomSmoothed += (randomHeld - randomSmoothed) * 0.35f; value = randomSmoothed; break;
            default: value = 4.0f * std::abs (lfoPhase - 0.5f) - 1.0f; break;
        }
        lfoPhase += s.lfoRate * (float) n / sr;
        if (lfoPhase >= 1.0f)
        {
            lfoPhase -= std::floor (lfoPhase);
            randomHeld = lfoRandom.bipolar();
        }

        wobblePhase += 0.31f * (float) n / sr;
        if (wobblePhase >= 1.0f) wobblePhase -= 1.0f;
        const auto warp = s.macro[2];

        ControlFrame c;
        c.lfo = value;
        c.vibSemis = s.lfoPitch * s.lfoPitch * 2.0f + motion * 0.2f;
        c.lfoFilterOct = s.lfoFilter * 3.0f + motion * 0.9f;
        c.pwmDco = s.pwm * 0.42f + motion * 0.15f;
        c.pwmVco = s.lfoPwm * 0.42f + motion * 0.15f;
        c.wobbleSemis = warp * 0.2f * (0.65f * wobble.process() + 0.35f * std::sin (twoPi * wobblePhase));
        c.bendSemis = bendNorm * s.bendRange;
        c.pressure = pressure;
        return c;
    }

    void renderVoices (float* l, float* r, int n) noexcept
    {
        while (n > 0)
        {
            const auto m = std::min (n, tick);
            const auto c = controlTick (m);
            for (auto& v : voices)
                v.render (l, r, m, frame, c);
            l += m;
            r += m;
            n -= m;
        }
    }

    bool isMono() const noexcept { return settings->voiceMode != 0; }
    float glideFrom() const noexcept { return settings->glide > 0.001f ? lastPitch : -1.0f; }

    void handle (const juce::MidiMessage& m) noexcept
    {
        if (m.isNoteOn())
            noteOn (m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff())
            noteOff (m.getNoteNumber());
        else if (m.isPitchWheel())
            bendNorm = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
        else if (m.isChannelPressure())
            pressureTarget = (float) m.getChannelPressureValue() / 127.0f;
        else if (m.isAftertouch())
        {
            for (auto& v : voices)
                if (v.note == m.getNoteNumber())
                    v.polyPressure = (float) m.getAfterTouchValue() / 127.0f;
        }
        else if (m.isSustainPedalOn())
            sustainDown = true;
        else if (m.isSustainPedalOff())
        {
            sustainDown = false;
            for (auto& v : voices)
                if (v.sustained) v.release();
        }
        else if (m.isController() && m.getControllerNumber() == 1)
            modWheelTarget = (float) m.getControllerValue() / 127.0f;
        else if (m.isAllSoundOff())
        {
            for (auto& v : voices) v.kill();
            heldCount = 0;
        }
        else if (m.isAllNotesOff() || m.isResetAllControllers())
        {
            for (auto& v : voices) v.release();
            heldCount = 0;
            sustainDown = false;
        }
    }

    void noteOn (int note, float velocity) noexcept
    {
        if (isMono())
        {
            monoNoteOn (note, velocity);
            return;
        }

        // Same note again (repeated or held by the pedal) reuses its voice; otherwise the longest-idle
        // free voice, then the oldest released one, then the oldest of all.
        Voice* chosen = nullptr;
        for (auto& v : voices)
            if (v.isActive() && v.note == note) chosen = &v;
        const auto oldest = [this] (auto&& accept) -> Voice*
        {
            Voice* best = nullptr;
            for (auto& v : voices)
                if (accept (v) && (best == nullptr || v.startOrder < best->startOrder))
                    best = &v;
            return best;
        };
        if (chosen == nullptr) chosen = oldest ([] (const Voice& v) { return ! v.isActive(); });
        if (chosen == nullptr) chosen = oldest ([] (const Voice& v) { return ! v.gate; });
        if (chosen == nullptr) chosen = oldest ([] (const Voice&) { return true; });
        chosen->start (note, velocity, glideFrom(), ++order);
        lastPitch = (float) note;
    }

    void noteOff (int note) noexcept
    {
        if (isMono())
        {
            monoNoteOff (note);
            return;
        }
        for (auto& v : voices)
            if (v.gate && v.note == note)
            {
                if (sustainDown)
                {
                    v.gate = false;
                    v.sustained = true;
                }
                else
                    v.release();
            }
    }

    void removeHeld (int note) noexcept
    {
        int w = 0;
        for (int i = 0; i < heldCount; ++i)
            if (held[(size_t) i] != note)
                held[(size_t) w++] = held[(size_t) i];
        heldCount = w;
    }

    // Mono: every note retriggers and glides. Legato: overlapping notes glide without retriggering.
    void monoNoteOn (int note, float velocity) noexcept
    {
        removeHeld (note);
        const auto overlapping = heldCount > 0;
        if (heldCount == (int) held.size())
            removeHeld (held[0]);
        held[(size_t) heldCount++] = note;

        auto& v = voices[0];
        const auto glide = settings->glide > 0.001f;
        if (settings->voiceMode == 2 && overlapping && v.isActive())
            v.glideTo (note, glide);
        else
            v.start (note, velocity, glide && v.isActive() && settings->voiceMode == 1 ? v.currentPitch() : -1.0f, ++order);
        lastPitch = (float) note;
    }

    void monoNoteOff (int note) noexcept
    {
        const auto wasTop = heldCount > 0 && held[(size_t) heldCount - 1] == note;
        removeHeld (note);
        auto& v = voices[0];
        if (heldCount == 0)
        {
            if (sustainDown)
            {
                v.gate = false;
                v.sustained = true;
            }
            else
                v.release();
        }
        else if (wasTop)
            v.glideTo (held[(size_t) heldCount - 1], settings->glide > 0.001f);
    }

    float sr = 48000.0f;
    const SynthSettings* settings = nullptr;
    VoiceFrame frame;
    std::array<Voice, numVoices> voices;
    std::array<int, 16> held {};
    int heldCount = 0, lastVoiceMode = 0;
    uint32_t order = 0;
    bool sustainDown = false;
    float modWheel = 0.0f, modWheelTarget = 0.0f, pressure = 0.0f, pressureTarget = 0.0f, bendNorm = 0.0f, lastPitch = -1.0f;
    float lfoPhase = 0.0f, randomHeld = 0.0f, randomSmoothed = 0.0f, wobblePhase = 0.0f;
    Random lfoRandom { 0x1f0u };
    RandomWalk wobble;
    std::array<SVF, 2> highPass;
    BbdChorus chorus;
    EnsembleChorus ensemble;
    Atmos atmos;
};
} // namespace dust
