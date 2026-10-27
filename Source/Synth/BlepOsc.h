#pragma once

#include <algorithm>
#include <cmath>

namespace dust
{
// Band-limited oscillator: saw, pulse and a square sub an octave down, all on one phase, plus hard
// sync. Every edge is smoothed with a two-sided polyBLEP residual. The side before an edge needs one
// sample of lookahead, so the output is one sample late; that doesn't matter inside a synth voice.
struct BlepOsc
{
    struct Levels
    {
        float saw = 1.0f, pulse = 0.0f, sub = 0.0f;
    };

    float phase = 0.0f;
    float wrapTau = -1.0f; // where in the last sample the phase wrapped (0..1], or -1: drives a synced oscillator

    void reset (float startPhase) noexcept
    {
        phase = startPhase;
        held = 0.0f;
        subSign = 1.0f;
    }

    // syncTau: a master oscillator wrapped at this point of the sample (0..1]; <= 0 = no sync this sample.
    float process (float inc, float pw, const Levels& l, float syncTau = -1.0f) noexcept
    {
        inc = std::clamp (inc, 1.0e-7f, 0.45f);
        pw = std::clamp (pw, 0.02f, 0.98f);
        before = after = 0.0f;
        wrapTau = -1.0f;

        if (syncTau > 0.0f && syncTau <= 1.0f)
        {
            run (0.0f, syncTau, inc, pw, l);
            // Hard reset to phase 0; the sub divider keeps its state.
            const auto jump = value (0.0f, pw, l) - value (phase, pw, l);
            step (jump, syncTau);
            phase = 0.0f;
            run (syncTau, 1.0f, inc, pw, l);
        }
        else
        {
            run (0.0f, 1.0f, inc, pw, l);
        }

        const auto out = held + before;
        held = value (phase, pw, l) - l.pulse * (2.0f * pw - 1.0f) + after; // pulse DC removed
        return out;
    }

private:
    float value (float p, float pw, const Levels& l) const noexcept
    {
        return l.saw * (2.0f * p - 1.0f) + l.pulse * (p < pw ? 1.0f : -1.0f) + l.sub * subSign;
    }

    // A jump of height h at fraction tau of the way from the held sample to the new one.
    void step (float h, float tau) noexcept
    {
        const auto y = 1.0f - tau;
        before += 0.5f * h * y * y;
        after -= 0.5f * h * tau * tau;
    }

    // Moves the phase from t0 to t1 (fractions of this sample), correcting every edge on the way.
    void run (float t0, float t1, float inc, float pw, const Levels& l) noexcept
    {
        auto t = t0;
        for (int guard = 0; guard < 4; ++guard)
        {
            const auto end = phase + inc * (t1 - t);
            if (phase < pw && end >= pw)
            {
                t += (pw - phase) / inc;
                step (-2.0f * l.pulse, t);
                phase = pw;
                continue;
            }
            if (end >= 1.0f)
            {
                t += (1.0f - phase) / inc;
                step (-2.0f * l.saw + 2.0f * l.pulse - 2.0f * subSign * l.sub, t);
                subSign = -subSign;
                phase = 0.0f;
                wrapTau = std::clamp (t, 1.0e-6f, 1.0f);
                continue;
            }
            phase = end;
            return;
        }
        phase = std::fmod (phase, 1.0f);
    }

    float held = 0.0f, before = 0.0f, after = 0.0f, subSign = 1.0f;
};
} // namespace dust
