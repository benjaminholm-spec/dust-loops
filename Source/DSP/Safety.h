#pragma once

#include <algorithm>
#include <cmath>

// Last lines of defence: NaN/inf scrubbing, a soft limiter for feedback paths and a peak limiter for
// the output. Allocation-free and branch-light; safe on the audio thread.
namespace dust
{
// Replaces NaN/inf with silence and flushes denormal-range values to zero.
inline float sanitize (float x) noexcept
{
    if (! std::isfinite (x)) return 0.0f;
    return std::abs (x) < 1.0e-15f ? 0.0f : x;
}

inline void sanitize (float* data, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
        data[i] = sanitize (data[i]);
}

// Smooth saturating curve: linear below `knee`, then bends towards `ceiling` and never exceeds it.
inline float softClip (float x, float knee = 0.6f, float ceiling = 1.0f) noexcept
{
    const auto a = std::abs (x);
    if (a <= knee) return x;
    const auto range = ceiling - knee;
    const auto over = (a - knee) / range;
    const auto shaped = knee + range * over / (1.0f + over); // rational curve, approaches ceiling asymptotically
    return std::copysign (shaped, x);
}

//==============================================================================
// Gentle limiter for the inside of feedback loops: a slow gain rider that keeps the level near `target`,
// followed by a soft clip. Feedback can pile up and get dense but never grows without bound.
class FeedbackLimiter
{
public:
    void prepare (float sampleRate) noexcept
    {
        attackCoef = std::exp (-1.0f / (0.005f * sampleRate));
        releaseCoef = std::exp (-1.0f / (0.4f * sampleRate));
        reset();
    }

    void reset() noexcept { envelope = 0.0f; }

    // Process a stereo frame in place.
    void process (float& left, float& right, float target = 0.7f) noexcept
    {
        left = sanitize (left);
        right = sanitize (right);
        const auto peak = std::max (std::abs (left), std::abs (right));
        const auto coef = peak > envelope ? attackCoef : releaseCoef;
        envelope = peak + coef * (envelope - peak);
        const auto gain = envelope > target ? target / envelope : 1.0f;
        left = softClip (left * gain, 0.7f, 0.98f);
        right = softClip (right * gain, 0.7f, 0.98f);
    }

private:
    float envelope = 0.0f, attackCoef = 0.0f, releaseCoef = 0.0f;
};

//==============================================================================
// Output safety: instant-attack peak limiter at -0.3 dBFS with a smooth release, then a hard ceiling.
// Transparent at sane levels; only there to catch accidents.
class OutputLimiter
{
public:
    static constexpr float ceiling = 0.966f; // -0.3 dBFS

    void prepare (float sampleRate) noexcept
    {
        releaseCoef = std::exp (-1.0f / (0.15f * sampleRate));
        reset();
    }

    void reset() noexcept { gain = 1.0f; }

    void process (float* left, float* right, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            auto l = sanitize (left[i]);
            auto r = right != nullptr ? sanitize (right[i]) : l;
            const auto peak = std::max (std::abs (l), std::abs (r));
            const auto wanted = peak * gain > ceiling ? ceiling / peak : 1.0f;
            gain = wanted < gain ? wanted : wanted + releaseCoef * (gain - wanted);
            l = std::clamp (l * gain, -ceiling, ceiling);
            r = std::clamp (r * gain, -ceiling, ceiling);
            left[i] = l;
            if (right != nullptr) right[i] = r;
        }
    }

    float currentGain() const noexcept { return gain; }

private:
    float gain = 1.0f, releaseCoef = 0.0f;
};
} // namespace dust
