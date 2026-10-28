#pragma once

#include "DSP/Primitives.h"
#include "DSP/Safety.h"
#include <array>
#include <atomic>
#include <vector>

// The cassette deck: three tape loops (LOOP mode) or one tape with three playback heads (ECHO mode).
//
// The tape is modelled as cells moving past a head at the current tape speed. The head records and
// plays at the same speed, so varispeed, reverse and tape-stop all fall out of the motion itself:
// what was recorded at 0.5x plays an octave up at 1x, a stopping tape slides down in pitch and goes
// quiet. Every time a cell passes the head it is played, worn (age, saturation, dropouts) and scaled
// by FEED, so an unarmed loop can only stay the same or fade. Only an armed (recording) loop gets
// anything added: new input, record hiss and the pedals in the loop. A governor keeps what is
// re-recorded from the old tape no louder than it was, so the loop never builds up on its own.
//
// All memory is allocated in prepare(); process() never allocates, locks or does I/O.
namespace dust
{
struct TapeSettings
{
    enum Mode { loop = 0, echo = 1 };
    enum Type { ferric = 0, chrome = 1, dictaphone = 2 };

    int mode = loop;
    float time = 0.5f, feed = 0.7f, mix = 0.5f, volume = 1.0f; // volume is linear gain
    float speed = 1.0f, inertia = 0.4f, brakeSeconds = 0.8f;
    bool freeze = false;
    bool sync = true;
    double bpm = 120.0;
    int type = ferric;
    float wow = 0.2f, flutter = 0.15f, hiss = 0.1f, saturation = 0.25f, age = 0.25f, dropouts = 0.05f;
    bool rewindHeld = false, fastForwardHeld = false;

    struct Loop
    {
        float beats = 16.0f, seconds = 7.3f, gain = 0.7f, pan = 0.0f, speedSemis = 0.0f;
        bool mute = false, armed = true;
    };
    std::array<Loop, 3> loops;
};

class TapeMachine
{
public:
    static constexpr int numLoops = 3;
    static constexpr double maxLoopSeconds = 60.0;
    static constexpr double echoTapeSeconds = 8.0;
    enum class Transport { playing = 0, stopped = 1, paused = 2 };

    // Allocates the tape. Never erases it: when the sample rate changes (say the audio interface
    // changed), what's on the tape is resampled to the new rate so it keeps playing at the same pitch.
    void prepare (double newSampleRate)
    {
        if (newSampleRate <= 0.0)
            return;
        const auto firstTime = loops[0].data.empty();
        const auto rateChanged = ! firstTime && std::abs (newSampleRate - sampleRate) > 0.5;
        const auto oldRate = sampleRate;
        sampleRate = newSampleRate;
        const auto sr = (float) sampleRate;

        if (firstTime || rateChanged)
        {
            const auto oldCapacity = capacity, oldEchoSize = echoSize;
            capacity = (int) (maxLoopSeconds * sampleRate) + 4;
            echoSize = (int) (echoTapeSeconds * sampleRate);
            const auto ratio = rateChanged ? sampleRate / oldRate : 1.0;
            for (auto& l : loops)
            {
                std::vector<float> fresh ((size_t) capacity * 2, 0.0f);
                if (rateChanged)
                {
                    resample (l.data.data(), oldCapacity, fresh.data(), capacity, ratio);
                    l.length = std::clamp ((int) std::round (l.length * ratio), 1, capacity - 4);
                    l.position = std::fmod (l.position * ratio, (double) l.length);
                }
                else
                {
                    l.length = (int) (7.0 * sampleRate);
                    l.position = 0.0;
                }
                l.data = std::move (fresh);
            }
            std::vector<float> freshEcho ((size_t) echoSize * 2, 0.0f);
            if (rateChanged)
            {
                resample (echo.data(), oldEchoSize, freshEcho.data(), echoSize, ratio);
                echoWrite = std::fmod (echoWrite * ratio, (double) echoSize);
            }
            else
                echoWrite = 0.0;
            echo = std::move (freshEcho);
        }

        for (auto& l : loops)
        {
            l.limiter.prepare (sr);
            l.governor.prepare (sr);
            l.wear = {};
            l.clearCursor = -1;
        }
        echoLimiter.prepare (sr);
        echoWear = {};

        wowWalk.prepare (sr, 0.4f, 1.6f, 0.5f, 0x1234567u);
        flutterWalk.prepare (sr, 0.02f, 0.08f, 0.02f, 0x7654321u);
        for (auto& f : hissShape) f.setCoefficients (4000.0f, 0.5f, sr);
        for (auto& f : inputFilter) f.reset();
        echoDelaySmooth.setTime (0.12f, sr);
        echoDelaySmooth.value = -1.0f;
        motionSmooth.setTime (0.01f, sr);
        speedNow = speedVelocity = 0.0;
        startedFromRest = true;
        prevIn[0] = prevIn[1] = 0.0f;
        prevEchoWrite[0] = prevEchoWrite[1] = 0.0f;
    }

    // Transport keys (call from the audio thread, on the key's rising edge).
    void pressPlay() noexcept   { if (transport != Transport::playing) startedFromRest = true; transport = Transport::playing; }
    void pressStop() noexcept   { transport = Transport::stopped; }
    void pressPause() noexcept
    {
        if (transport == Transport::playing)     transport = Transport::paused;
        else if (transport == Transport::paused) { transport = Transport::playing; startedFromRest = true; }
    }

    // Wipes a loop over the next few blocks (a 60 s buffer is too big to clear in one go).
    void clearLoop (int i) noexcept { loops[(size_t) i].clearCursor = 0; }

    // Puts every head back at the start of its loop (used when Logic starts playing in SYNC).
    void realign() noexcept
    {
        for (auto& l : loops) l.position = 0.0;
    }

    // injectL/R (optional): extra signal re-recorded with the old tape on every pass of an armed loop (the
    // pedals in the loop), scaled by FEED. returnL/R (optional): receives what the heads play, averaged, before levels.
    void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples, const TapeSettings& s,
                  const float* injectL = nullptr, const float* injectR = nullptr,
                  float* returnL = nullptr, float* returnR = nullptr) noexcept
    {
        const auto sr = (float) sampleRate;
        updateLengths (s);
        progressClears();

        const TypeColour& colour = typeColours[(size_t) std::clamp (s.type, 0, 2)];
        const auto wear = makeWearCoeffs (s, colour);
        const auto feed = std::clamp (s.feed, 0.0f, 1.0f);
        const auto dryGain = std::min (1.0f, 2.0f * (1.0f - s.mix));
        const auto wetGain = std::min (1.0f, 2.0f * s.mix) * s.volume;
        const auto hissOut = s.hiss * s.hiss * 0.006f * colour.hiss;
        const auto wowDepth = s.wow * 0.007f, flutterDepth = s.flutter * 0.0018f;

        // Tape motion: the target speed and how the reels get there.
        auto target = transport == Transport::playing ? (double) s.speed : 0.0;
        if (s.rewindHeld)      target = -std::abs (target == 0.0 ? 1.0 : target);
        if (s.fastForwardHeld) target *= 2.0;
        double omega, zeta = 1.0;
        if (transport == Transport::stopped)     omega = 4.0 / std::max (0.05, (double) s.brakeSeconds); // the brake
        else if (transport == Transport::paused) omega = 30.0;                                           // pinch roller lifts
        else if (startedFromRest)                { omega = 14.0; zeta = 0.35; }                           // motor catches up, wobbles
        else                                     omega = 1.0 / (0.03 + 2.0 * (double) (s.inertia * s.inertia));
        const auto dt = 1.0 / sampleRate;

        // Recording at slow speed loses highs, like a real head: filter the input down with the speed.
        const auto inputCutoff = std::clamp ((float) std::abs (speedNow) * 0.42f * sr, 200.0f, 0.45f * sr);
        for (auto& f : inputFilter) f.setCoefficients (inputCutoff, 0.6f, sr);

        float blockPeak[numLoops] {};

        for (int n = 0; n < numSamples; ++n)
        {
            // Critically damped (or springy) glide towards the target speed.
            const auto accel = omega * omega * (target - speedNow) - 2.0 * zeta * omega * speedVelocity;
            speedVelocity += accel * dt;
            speedNow += speedVelocity * dt;
            if (startedFromRest && std::abs (target - speedNow) < 0.002 && std::abs (speedVelocity) < 0.01)
                startedFromRest = false;

            // Wow (slow, lopsided) and flutter (fast) from the capstan and pinch roller.
            wowPhase += 0.55f / sr;       if (wowPhase >= 1.0f) wowPhase -= 1.0f;
            flutterPhase1 += 6.3f / sr;   if (flutterPhase1 >= 1.0f) flutterPhase1 -= 1.0f;
            flutterPhase2 += 9.7f / sr;   if (flutterPhase2 >= 1.0f) flutterPhase2 -= 1.0f;
            const auto wowSine = std::sin (twoPi * wowPhase);
            const auto wobble = wowDepth * (0.6f * wowSine + 0.2f * wowSine * wowSine + 0.6f * wowWalk.process())
                              + flutterDepth * (std::sin (twoPi * flutterPhase1) + 0.5f * std::sin (twoPi * flutterPhase2)
                                                + 0.5f * flutterWalk.process());
            const auto v = speedNow * (1.0 + (double) wobble);

            // A moving head is what makes the sound: a stopped tape is silent.
            const auto motion = motionSmooth.process (std::min (1.0f, (float) std::abs (v) * 3.0f));

            const auto dryL = sanitize (inL[n]), dryR = sanitize (inR[n]);
            const auto inFL = inputFilter[0].process (dryL).low;
            const auto inFR = inputFilter[1].process (dryR).low;

            const auto injL = injectL != nullptr ? injectL[n] * feed : 0.0f;
            const auto injR = injectR != nullptr ? injectR[n] * feed : 0.0f;
            float wetL = 0.0f, wetR = 0.0f, retL = 0.0f, retR = 0.0f;
            if (s.mode == TapeSettings::loop)
                processLoopsSample (s, wear, feed, v, motion, inFL, inFR, injL, injR, wetL, wetR, retL, retR, blockPeak);
            else
                processEchoSample (s, wear, feed, v, motion, inFL, inFR, injL, injR, wetL, wetR, retL, retR);
            if (returnL != nullptr) { returnL[n] = retL; returnR[n] = retR; }

            prevIn[0] = inFL; prevIn[1] = inFR;

            if (hissOut > 0.0f)
            {
                wetL += hissShape[0].process (noise.bipolar()).band * hissOut * motion;
                wetR += hissShape[1].process (noise.bipolar()).band * hissOut * motion;
            }

            outL[n] = dryL * dryGain + wetL * wetGain;
            outR[n] = dryR * dryGain + wetR * wetGain;
        }

        // UI telemetry
        speedDisplay.store ((float) speedNow);
        transportDisplay.store ((int) transport);
        for (size_t i = 0; i < (size_t) numLoops; ++i)
        {
            const auto& l = loops[i];
            headPhase[i].store (s.mode == TapeSettings::loop ? (float) (l.position / (double) l.length)
                                                             : (float) (echoWrite / (double) echoSize));
            loopPeak[i].store (blockPeak[i]);
            loopSeconds[i].store ((float) ((double) l.length / sampleRate));
        }
    }

    // For the UI and tests
    std::atomic<float> speedDisplay { 0.0f };
    std::atomic<int> transportDisplay { 0 };
    std::array<std::atomic<float>, numLoops> headPhase {}, loopPeak {}, loopSeconds {};

    Transport getTransport() const noexcept { return transport; }
    double getHeadPosition (int i) const noexcept { return loops[(size_t) i].position; }
    int getLoopLength (int i) const noexcept { return loops[(size_t) i].length; }
    double getSampleRate() const noexcept { return sampleRate; }

    // ---- Saving the tape with the project ------------------------------------------------------
    // Copies loop i's recorded cells (interleaved stereo) and its head position. Runs on the message
    // thread while the tape may be playing: a torn sample here is harmless, and nothing allocates on
    // the audio thread.
    void copyLoop (int i, std::vector<float>& out, double& position) const
    {
        const auto& l = loops[(size_t) i];
        out.clear();
        if (l.data.empty())
            return;
        out.assign (l.data.begin(), l.data.begin() + (std::ptrdiff_t) l.length * 2);
        position = l.position;
    }

    // Puts saved cells (recorded at `fromRate`) back on loop i, resampled to the current rate.
    // The audio thread must be locked out while this runs.
    void restoreLoop (int i, const std::vector<float>& cells, double position, double fromRate)
    {
        auto& l = loops[(size_t) i];
        if (l.data.empty() || fromRate <= 0.0)
            return;
        const auto srcCells = (int) (cells.size() / 2);
        const auto ratio = sampleRate / fromRate;
        const auto cellsHere = std::clamp ((int) std::round (srcCells * ratio), 1, capacity - 4);
        std::fill (l.data.begin(), l.data.end(), 0.0f);
        resample (cells.data(), srcCells, l.data.data(), cellsHere, ratio);
        l.length = cellsHere;
        l.position = std::fmod (std::max (0.0, position * ratio), (double) cellsHere);
        l.clearCursor = -1;
    }

    // Linear-interpolated stereo resampling of interleaved cells.
    static void resample (const float* src, int srcCells, float* dst, int dstCells, double ratio) noexcept
    {
        if (src == nullptr || srcCells < 2)
            return;
        for (int i = 0; i < dstCells; ++i)
        {
            const auto pos = (double) i / ratio;
            const auto i0 = (int) pos;
            if (i0 >= srcCells - 1)
                break;
            const auto f = (float) (pos - i0);
            for (int c = 0; c < 2; ++c)
                dst[i * 2 + c] = src[i0 * 2 + c] + f * (src[(i0 + 1) * 2 + c] - src[i0 * 2 + c]);
        }
    }

    // Loop length scaling from TIME in LOOP mode: 0 -> x0.25, 0.5 -> x1, 1 -> x2.
    static double timeScale (float time) noexcept
    {
        return time < 0.5f ? std::exp2 (-4.0 * (0.5 - (double) time)) : std::exp2 (2.0 * ((double) time - 0.5));
    }

    // Echo time from TIME in ECHO mode: 30 ms .. 2 s, exponential.
    static double echoSeconds (float time) noexcept { return 0.03 * std::pow (2.0 / 0.03, (double) time); }

private:
    struct WearState
    {
        float lp[2] {}, hp[2] {};
        float dropoutPhase = 1.0f, dropoutRate = 0.0f, dropoutDepth = 0.0f;
    };

    struct Loop
    {
        std::vector<float> data; // interleaved stereo cells
        int length = 1;
        double position = 0.0;
        FeedbackLimiter limiter;
        LoopGainGovernor governor;
        WearState wear;
        int clearCursor = -1; // >= 0 while a clear is in progress
    };

    struct TypeColour { float lowpassHz, highpassHz, hiss, saturation, width; };
    static constexpr std::array<TypeColour, 3> typeColours { {
        { 16000.0f, 20.0f, 1.0f, 1.0f, 1.0f },  // Ferric: warm, the default
        { 19000.0f, 15.0f, 0.5f, 0.7f, 1.0f },  // Chrome: brighter, quieter
        { 6000.0f, 110.0f, 2.2f, 1.4f, 0.3f },  // Worn dictaphone: narrow, boxy, nearly mono
    } };

    struct WearCoeffs { float lpCoef, hpCoef, drive, hissRec, dropoutChance, width; };

    WearCoeffs makeWearCoeffs (const TapeSettings& s, const TypeColour& c) const noexcept
    {
        const auto sr = (float) sampleRate;
        const auto lpHz = std::min (0.45f * sr, c.lowpassHz * std::exp2 (-2.5f * s.age));
        const auto onePole = [sr] (float hz) { return 1.0f - std::exp (-twoPi * hz / sr); };
        return {
            onePole (lpHz),
            onePole (c.highpassHz),
            1.0f + 3.0f * s.saturation * c.saturation,
            s.hiss * s.hiss * 0.004f * c.hiss,
            s.dropouts * s.dropouts * 0.8f / sr, // dropouts per second at full: ~0.8
            c.width
        };
    }

    // One pass of wear on a stereo cell: tape loses highs and lows, saturates and drops out. The record
    // head adds hiss, so only a recording pass hisses.
    void wearCell (WearState& w, float& l, float& r, const WearCoeffs& c, bool recording) noexcept
    {
        float* ch[] { &l, &r };
        for (int i = 0; i < 2; ++i)
        {
            auto& x = *ch[i];
            w.lp[i] += c.lpCoef * (x - w.lp[i]);
            x = w.lp[i];
            w.hp[i] += c.hpCoef * (x - w.hp[i]);
            x -= w.hp[i];
            if (c.drive > 1.001f)
                x = fastTanh (x * c.drive) / c.drive;
            if (recording && c.hissRec > 0.0f)
                x += noise.bipolar() * c.hissRec;
        }

        if (c.width < 1.0f)
        {
            const auto mid = 0.5f * (l + r), side = 0.5f * (l - r) * c.width;
            l = mid + side; r = mid - side;
        }

        // Dropouts get burned into the tape: the next pass carries the hole.
        if (w.dropoutPhase >= 1.0f && c.dropoutChance > 0.0f && noise.uniform() < c.dropoutChance)
        {
            w.dropoutPhase = 0.0f;
            w.dropoutDepth = 0.3f + 0.6f * noise.uniform();
            w.dropoutRate = 1.0f / ((0.03f + 0.22f * noise.uniform()) * (float) sampleRate);
        }
        if (w.dropoutPhase < 1.0f)
        {
            const auto g = 1.0f - w.dropoutDepth * 0.5f * (1.0f - std::cos (twoPi * w.dropoutPhase));
            l *= g; r *= g;
            w.dropoutPhase += w.dropoutRate;
        }
    }

    void updateLengths (const TapeSettings& s) noexcept
    {
        const auto bpm = std::clamp (s.bpm, 20.0, 400.0);
        const auto scale = timeScale (s.time);
        for (size_t i = 0; i < loops.size(); ++i)
        {
            const auto& ls = s.loops[i];
            const auto seconds = (s.sync ? (double) ls.beats * 60.0 / bpm : (double) ls.seconds) * scale;
            auto& l = loops[i];
            l.length = (int) std::clamp (std::round (seconds * sampleRate), 0.05 * sampleRate, (double) capacity - 4.0);
            if (l.position >= (double) l.length)
                l.position = std::fmod (l.position, (double) l.length);
        }
    }

    void progressClears() noexcept
    {
        constexpr int chunk = 1 << 16;
        for (auto& l : loops)
        {
            if (l.clearCursor < 0)
                continue;
            const auto end = std::min (capacity, l.clearCursor + chunk);
            std::fill (l.data.begin() + (ptrdiff_t) l.clearCursor * 2, l.data.begin() + (ptrdiff_t) end * 2, 0.0f);
            l.clearCursor = end >= capacity ? -1 : end;
            if (l.clearCursor < 0)
                l.wear = {};
        }
    }

    static int wrap (int k, int length) noexcept
    {
        k %= length;
        return k < 0 ? k + length : k;
    }

    static float cubic (float xm1, float x0, float x1, float x2, float f) noexcept
    {
        const auto c1 = 0.5f * (x1 - xm1);
        const auto c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const auto c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    static void readStereo (const std::vector<float>& data, int length, double position, float& l, float& r) noexcept
    {
        const auto i = (int) std::floor (position);
        const auto f = (float) (position - (double) i);
        const auto a = (size_t) wrap (i - 1, length) * 2, b = (size_t) wrap (i, length) * 2,
                   c = (size_t) wrap (i + 1, length) * 2, d = (size_t) wrap (i + 2, length) * 2;
        l = cubic (data[a], data[b], data[c], data[d], f);
        r = cubic (data[a + 1], data[b + 1], data[c + 1], data[d + 1], f);
    }

    void processLoopsSample (const TapeSettings& s, const WearCoeffs& wear, float feed, double v, float motion,
                             float inL, float inR, float injL, float injR, float& wetL, float& wetR,
                             float& retL, float& retR, float* peaks) noexcept
    {
        for (size_t i = 0; i < loops.size(); ++i)
        {
            auto& l = loops[i];
            const auto& ls = s.loops[i];
            const auto len = l.length;
            const auto p0 = l.position;
            const auto p1 = p0 + v * std::exp2 ((double) ls.speedSemis / 12.0);

            // Play what's on the tape under the head (before this pass overwrites it).
            float pl, pr;
            readStereo (l.data, len, p1, pl, pr);
            if (l.clearCursor >= 0)
                pl = pr = 0.0f;

            // Rewrite every cell the head crossed during this sample.
            if (! s.freeze && l.clearCursor < 0)
            {
                const auto k0 = (int) std::floor (p0), k1 = (int) std::floor (p1);
                const auto steps = std::abs (k1 - k0);
                const auto dir = k1 > k0 ? 1 : -1;
                for (int j = 1; j <= steps; ++j)
                {
                    const auto cell = (size_t) wrap (dir > 0 ? k0 + j : k0 - j + 1, len) * 2;
                    const auto frac = (float) j / (float) steps;
                    const auto oldL = l.data[cell], oldR = l.data[cell + 1];
                    auto cl = oldL, cr = oldR;
                    wearCell (l.wear, cl, cr, wear, ls.armed);
                    cl *= feed;
                    cr *= feed;
                    if (ls.armed)
                    {
                        // The pedals' copy goes on with the old tape, never louder than it was.
                        cl += injL;
                        cr += injR;
                        const auto g = l.governor.process (oldL, oldR, cl, cr);
                        cl = cl * g + prevIn[0] + frac * (inL - prevIn[0]);
                        cr = cr * g + prevIn[1] + frac * (inR - prevIn[1]);
                    }
                    l.limiter.process (cl, cr);
                    l.data[cell] = cl;
                    l.data[cell + 1] = cr;
                }
            }

            auto np = std::fmod (p1, (double) len);
            if (np < 0.0) np += (double) len;
            l.position = np;

            if (! ls.mute)
                addPanned (pl * motion, pr * motion, ls.gain, ls.pan, wetL, wetR);
            retL += pl * motion * (1.0f / 3.0f);
            retR += pr * motion * (1.0f / 3.0f);
            peaks[i] = std::max (peaks[i], std::max (std::abs (pl), std::abs (pr)) * motion);
        }
    }

    // ECHO: one tape loop, a record head and three playback heads behind it (A, B and C), spaced by the
    // ratio of their loop lengths: a multi-head tape echo.
    void processEchoSample (const TapeSettings& s, const WearCoeffs& wear, float feed, double v, float motion,
                            float inL, float inR, float injL, float injR, float& wetL, float& wetR,
                            float& retL, float& retR) noexcept
    {
        const auto lengthOf = [&s] (size_t i) { return s.sync ? s.loops[i].beats : s.loops[i].seconds; };
        const auto longest = std::max ({ lengthOf (0), lengthOf (1), lengthOf (2), 0.01f });
        const auto delayTarget = (float) (echoSeconds (s.time) * sampleRate);
        if (echoDelaySmooth.value < 0.0f) echoDelaySmooth.value = delayTarget;
        const auto delay = echoDelaySmooth.process (delayTarget); // moving the heads glides the pitch, like a real echo

        const auto w0 = echoWrite;
        const auto w1 = w0 + v;

        float fbL = 0.0f, fbR = 0.0f;
        for (size_t i = 0; i < loops.size(); ++i)
        {
            const auto& ls = s.loops[i];
            const auto ratio = lengthOf (i) / longest;
            float hl, hr;
            readStereo (echo, echoSize, w1 - (double) (delay * ratio) - 2.0, hl, hr);
            hl *= motion; hr *= motion;
            fbL += hl; fbR += hr;
            if (! ls.mute)
                addPanned (hl, hr, ls.gain, ls.pan, wetL, wetR);
        }

        fbL *= (1.0f / 3.0f); fbR *= (1.0f / 3.0f);
        retL = fbL; retR = fbR;
        if (! s.freeze)
        {
            wearCell (echoWear, fbL, fbR, wear, true);
            auto wl = inL + fbL * feed * 1.15f + injL; // a little extra so FEED at max can self-oscillate into the limiter
            auto wr = inR + fbR * feed * 1.15f + injR;
            echoLimiter.process (wl, wr);

            const auto k0 = (int) std::floor (w0), k1 = (int) std::floor (w1);
            const auto steps = std::abs (k1 - k0);
            const auto dir = k1 > k0 ? 1 : -1;
            for (int j = 1; j <= steps; ++j)
            {
                const auto cell = (size_t) wrap (dir > 0 ? k0 + j : k0 - j + 1, echoSize) * 2;
                const auto frac = (float) j / (float) steps;
                echo[cell] = prevEchoWrite[0] + frac * (wl - prevEchoWrite[0]);
                echo[cell + 1] = prevEchoWrite[1] + frac * (wr - prevEchoWrite[1]);
            }
            prevEchoWrite[0] = wl; prevEchoWrite[1] = wr;
        }

        auto nw = std::fmod (w1, (double) echoSize);
        if (nw < 0.0) nw += (double) echoSize;
        echoWrite = nw;
    }

    static void addPanned (float l, float r, float gain, float pan, float& outL, float& outR) noexcept
    {
        outL += l * gain * std::min (1.0f, 1.0f - pan);
        outR += r * gain * std::min (1.0f, 1.0f + pan);
    }

    double sampleRate = 0.0;
    int capacity = 0, echoSize = 1;
    std::array<Loop, numLoops> loops;
    std::vector<float> echo;
    double echoWrite = 0.0;
    float prevEchoWrite[2] {};
    WearState echoWear;
    FeedbackLimiter echoLimiter;
    OnePole echoDelaySmooth, motionSmooth;

    Transport transport = Transport::playing;
    double speedNow = 0.0, speedVelocity = 0.0;
    bool startedFromRest = true;

    float wowPhase = 0.0f, flutterPhase1 = 0.3f, flutterPhase2 = 0.7f;
    RandomWalk wowWalk, flutterWalk;
    SVF hissShape[2], inputFilter[2];
    float prevIn[2] {};
    Random noise { 0x5eedf00du };
};
} // namespace dust
