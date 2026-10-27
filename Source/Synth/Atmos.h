#pragma once

#include <array>
#include "DSP/Primitives.h"

namespace dust
{
struct AtmosSettings
{
    float level = 0.0f, crackle = 0.6f, rain = 0.3f, hum = 0.2f, tone = 0.5f;
    bool hum60 = false, always = false;
};

// The room the synth lives in: vinyl crackle and pops, rain on a window, mains hum. It fades in while
// notes are playing and drifts away after (or runs all the time with "always").
class Atmos
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        activity.setTime (1.5f, sampleRate);
        for (auto& f : surface) f.setCoefficients (3200.0f, 0.5f, sampleRate);
        for (auto& f : rainBand) f.setCoefficients (2600.0f, 0.4f, sampleRate);
        for (auto& f : toneFilter) f.setCoefficients (8000.0f, 0.6f, sampleRate);
        rainSwell.prepare (sampleRate / 64.0f, 1.5f, 6.0f, 2.5f, 0xa7a7u);
        for (auto& c : clicks) c = {};
        for (auto& d : drops) d = {};
        humPhase = 0.0f;
    }

    // Adds into l/r. `notesActive` drives the fade.
    void process (float* l, float* r, int n, const AtmosSettings& s, bool notesActive) noexcept
    {
        const auto targetActivity = s.always || notesActive ? 1.0f : 0.0f;
        activity.coef = 1.0f - std::exp (-1.0f / ((targetActivity > activity.value ? 1.5f : 6.0f) * sr));
        if (s.level <= 0.0f || (targetActivity == 0.0f && activity.value < 1.0e-4f))
        {
            activity.value = 0.0f;
            return;
        }

        const auto toneHz = 1200.0f * std::exp2 (s.tone * 4.0f); // 1.2k .. 19k
        for (auto& f : toneFilter) f.setCoefficients (toneHz, 0.6f, sr);

        const auto gain = s.level * s.level * 0.35f;
        const auto clickChance = s.crackle * s.crackle * 40.0f / sr;
        const auto dropChance = s.rain * s.rain * 45.0f / sr;
        const auto humInc = (s.hum60 ? 60.0f : 50.0f) / sr;
        for (int i = 0; i < n; ++i)
        {
            if ((i & 63) == 0) swell = 0.6f + 0.4f * rainSwell.process();
            const auto a = activity.process (targetActivity) * gain;

            // Crackle: quiet surface noise plus random clicks; now and then a big pop.
            float crackL = surface[0].process (rng.bipolar()).band * 0.04f * s.crackle;
            float crackR = surface[1].process (rng.bipolar()).band * 0.04f * s.crackle;
            if (rng.uniform() < clickChance)
                spawnClick();
            for (auto& c : clicks)
                if (std::abs (c.level) > 1.0e-4f)
                {
                    crackL += c.level * c.left;
                    crackR += c.level * c.right;
                    c.level *= c.decay;
                }

            // Rain: banded noise that swells, plus droplets ticking on the glass.
            float rainL = rainBand[0].process (rng.bipolar()).band * 0.22f * s.rain * swell;
            float rainR = rainBand[1].process (rng.bipolar()).band * 0.22f * s.rain * swell;
            if (rng.uniform() < dropChance)
                spawnDrop();
            for (auto& d : drops)
                if (d.level > 1.0e-4f)
                {
                    const auto v = d.level * std::sin (twoPi * d.phase);
                    d.phase += d.inc;
                    if (d.phase >= 1.0f) d.phase -= 1.0f;
                    rainL += v * d.left;
                    rainR += v * d.right;
                    d.level *= d.decay;
                }

            // Hum: the mains fundamental and a few harmonics, slightly buzzy.
            humPhase += humInc;
            if (humPhase >= 1.0f) humPhase -= 1.0f;
            const auto h = (std::sin (twoPi * humPhase) + 0.5f * std::sin (2.0f * twoPi * humPhase)
                            + 0.25f * std::sin (3.0f * twoPi * humPhase) + 0.12f * std::sin (5.0f * twoPi * humPhase)) * 0.12f * s.hum;

            l[i] += toneFilter[0].process (crackL + rainL + h).low * a;
            r[i] += toneFilter[1].process (crackR + rainR + h).low * a;
        }
    }

private:
    struct Click { float level = 0.0f, decay = 0.0f, left = 0.0f, right = 0.0f; };
    struct Drop { float level = 0.0f, decay = 0.0f, phase = 0.0f, inc = 0.0f, left = 0.0f, right = 0.0f; };

    void spawnClick() noexcept
    {
        auto& c = clicks[nextClick++ % clicks.size()];
        const auto big = rng.uniform() < 0.06f;
        c.level = (big ? 0.5f + 0.4f * rng.uniform() : 0.05f + 0.2f * rng.uniform() * rng.uniform()) * (rng.uniform() < 0.5f ? -1.0f : 1.0f);
        c.decay = std::exp (-1.0f / ((big ? 0.0012f : 0.00025f) * sr));
        const auto pan = rng.uniform();
        c.left = std::sqrt (1.0f - pan);
        c.right = std::sqrt (pan);
    }

    void spawnDrop() noexcept
    {
        auto& d = drops[nextDrop++ % drops.size()];
        d.level = 0.02f + 0.08f * rng.uniform();
        d.decay = std::exp (-1.0f / ((0.002f + 0.006f * rng.uniform()) * sr));
        d.inc = (1800.0f + 3500.0f * rng.uniform()) / sr;
        d.phase = 0.0f;
        const auto pan = rng.uniform();
        d.left = std::sqrt (1.0f - pan);
        d.right = std::sqrt (pan);
    }

    float sr = 48000.0f, humPhase = 0.0f, swell = 1.0f;
    Random rng { 0x5eed1u };
    OnePole activity;
    RandomWalk rainSwell;
    SVF surface[2], rainBand[2], toneFilter[2];
    std::array<Click, 16> clicks {};
    std::array<Drop, 12> drops {};
    size_t nextClick = 0, nextDrop = 0;
};
} // namespace dust
