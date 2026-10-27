#pragma once

#include "DSP/Primitives.h"

namespace dust
{
// Juno-style BBD chorus. One triangle LFO sweeps two delay lines in opposite directions (left and
// right), and the dark, slightly gritty wet signal is mixed 50/50 with the dry. Mode I is slow and
// wide, II faster, I+II a fast shallow shimmer.
struct BbdChorus
{
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        for (auto& d : lines)
            d.prepare ((int) (sampleRate * 0.02f));
        for (auto& f : tone)
            f.setCoefficients (7500.0f, 0.6f, sampleRate);
        amount.setTime (0.05f, sampleRate);
        amount.value = 0.0f;
        phase = 0.0f;
    }

    // mode: 0 off, 1 I, 2 II, 3 I+II
    void process (float* l, float* r, int n, int mode, float mix) noexcept
    {
        static constexpr float rates[] { 0.513f, 0.513f, 0.863f, 9.75f };
        static constexpr float depthsMs[] { 1.85f, 1.85f, 1.85f, 0.3f };
        if (mode != 0)
            current = std::clamp (mode, 1, 3);
        const auto target = mode == 0 ? 0.0f : mix;
        if (target <= 0.0f && amount.value < 1.0e-4f)
        {
            amount.value = 0.0f;
            return;
        }

        const auto inc = rates[current] / sr;
        const auto depth = depthsMs[current] * 0.001f * sr;
        const auto centre = 3.5f * 0.001f * sr;
        for (int i = 0; i < n; ++i)
        {
            const auto tri = 4.0f * std::abs (phase - 0.5f) - 1.0f;
            phase += inc;
            if (phase >= 1.0f) phase -= 1.0f;

            const auto m = amount.process (target);
            lines[0].push (l[i]);
            lines[1].push (r[i]);
            const auto wetL = fastTanh (tone[0].process (lines[0].read (centre + depth * tri)).low * 1.1f);
            const auto wetR = fastTanh (tone[1].process (lines[1].read (centre - depth * tri)).low * 1.1f);
            const auto dryGain = 1.0f - 0.5f * m, wetGain = 0.5f * m, makeUp = 1.0f + 0.25f * m;
            l[i] = (l[i] * dryGain + wetL * wetGain) * makeUp;
            r[i] = (r[i] * dryGain + wetR * wetGain) * makeUp;
        }
    }

    DelayLine lines[2];
    SVF tone[2];
    OnePole amount;
    float sr = 48000.0f, phase = 0.0f;
    int current = 1;
};

// String-machine ensemble: three delay taps per side, each swept by a slow and a fast sine 120 degrees
// apart. Turns plain saws into the thick, seasick string sound.
struct EnsembleChorus
{
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        for (auto& d : lines)
            d.prepare ((int) (sampleRate * 0.03f));
        amount.setTime (0.05f, sampleRate);
        amount.value = 0.0f;
    }

    void process (float* l, float* r, int n, float depth) noexcept
    {
        if (depth <= 0.0f && amount.value < 1.0e-4f)
        {
            amount.value = 0.0f;
            return;
        }

        const auto slowInc = 0.62f / sr, fastInc = 5.9f / sr;
        const auto ms = 0.001f * sr;
        for (int i = 0; i < n; ++i)
        {
            const auto d = amount.process (depth);
            slow += slowInc;
            fast += fastInc;
            if (slow >= 1.0f) slow -= 1.0f;
            if (fast >= 1.0f) fast -= 1.0f;

            lines[0].push (l[i]);
            lines[1].push (r[i]);
            float wet[2] {};
            for (int c = 0; c < 2; ++c)
                for (int k = 0; k < 3; ++k)
                {
                    const auto offset = (float) k / 3.0f + (float) c / 6.0f;
                    const auto delay = 6.0f * ms + d * ms * (2.4f * std::sin (twoPi * (slow + offset))
                                                             + 0.35f * std::sin (twoPi * (fast + offset)));
                    wet[c] += lines[c].read (delay) * (1.0f / 3.0f);
                }
            const auto dryGain = 1.0f - 0.7f * d, wetGain = 1.1f * d;
            l[i] = l[i] * dryGain + wet[0] * wetGain;
            r[i] = r[i] * dryGain + wet[1] * wetGain;
        }
    }

    DelayLine lines[2];
    OnePole amount;
    float sr = 48000.0f, slow = 0.0f, fast = 0.0f;
};
} // namespace dust
