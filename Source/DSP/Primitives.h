#pragma once

#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>

// Small, allocation-free DSP building blocks used by the voices and the effects.
namespace dust
{
constexpr float twoPi = 6.28318530717958647692f;

inline float fastTanh (float x) noexcept
{
    // Padé approximant, accurate enough for saturation and cheap enough to run per sample per voice.
    x = std::clamp (x, -3.0f, 3.0f);
    const auto x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

//==============================================================================
// Tiny xorshift RNG: deterministic, lock-free and safe to use on the audio thread.
struct Random
{
    explicit Random (uint32_t seed = 0x9e3779b9u) : state (seed ? seed : 1u) {}

    uint32_t next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    float uniform() noexcept  { return (float) (next() >> 8) * (1.0f / 16777216.0f); } // [0, 1)
    float bipolar() noexcept  { return uniform() * 2.0f - 1.0f; }                      // [-1, 1)

    uint32_t state;
};

//==============================================================================
inline float polyBlep (float t, float dt) noexcept
{
    if (t < dt)        { t /= dt;               return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt;   return t * t + t + t + 1.0f; }
    return 0.0f;
}

// Band-limited oscillator that morphs from saw (shape 0) to pulse (shape 1).
struct MorphOscillator
{
    float phase = 0.0f;

    float process (float inc, float shape, float pulseWidth) noexcept
    {
        inc = std::min (inc, 0.45f);

        float saw = 2.0f * phase - 1.0f;
        saw -= polyBlep (phase, inc);

        float pulse = 0.0f;
        if (shape > 0.0f)
        {
            pulse = phase < pulseWidth ? 1.0f : -1.0f;
            pulse += polyBlep (phase, inc);
            float t2 = phase + 1.0f - pulseWidth;
            t2 -= (float) (int) t2;
            pulse -= polyBlep (t2, inc);
            pulse -= 2.0f * pulseWidth - 1.0f; // remove DC for narrow pulses
        }

        phase += inc;
        if (phase >= 1.0f) phase -= 1.0f;

        return saw + shape * (pulse - saw);
    }
};

struct SineOscillator
{
    float phase = 0.0f;

    float process (float inc) noexcept
    {
        const auto out = std::sin (twoPi * phase);
        phase += inc;
        if (phase >= 1.0f) phase -= 1.0f;
        return out;
    }
};

//==============================================================================
// Analog-style exponential ADSR. Attack aims past 1.0 so it has the familiar convex RC curve;
// decay and release times are measured to -60 dB.
struct Envelope
{
    enum class Stage { idle, attack, decay, release };

    void setSampleRate (double sr) noexcept { sampleRate = (float) sr; }

    void setParameters (float attackS, float decayS, float sustainLevel, float releaseS) noexcept
    {
        constexpr float attackTarget = 1.3f;
        const auto attackTau = std::max (attackS, 0.0005f) / std::log (attackTarget / (attackTarget - 1.0f));
        attackCoef  = std::exp (-1.0f / (attackTau * sampleRate));
        decayCoef   = std::exp (-6.9078f / (std::max (decayS, 0.001f) * sampleRate));
        releaseCoef = std::exp (-6.9078f / (std::max (releaseS, 0.001f) * sampleRate));
        sustain = sustainLevel;
    }

    void noteOn() noexcept   { stage = Stage::attack; }  // retriggers from the current level: no clicks on steal
    void noteOff() noexcept  { if (stage != Stage::idle) stage = Stage::release; }
    void reset() noexcept    { stage = Stage::idle; level = 0.0f; }
    bool isActive() const noexcept { return stage != Stage::idle; }

    float process() noexcept
    {
        switch (stage)
        {
            case Stage::attack:
                level = 1.3f + (level - 1.3f) * attackCoef;
                if (level >= 1.0f) { level = 1.0f; stage = Stage::decay; }
                break;
            case Stage::decay:
                level = sustain + (level - sustain) * decayCoef;
                break;
            case Stage::release:
                level *= releaseCoef;
                if (level < 1.0e-4f) reset();
                break;
            case Stage::idle:
                break;
        }
        return level;
    }

    float sampleRate = 44100.0f, attackCoef = 0.0f, decayCoef = 0.0f, releaseCoef = 0.0f, sustain = 1.0f, level = 0.0f;
    Stage stage = Stage::idle;
};

//==============================================================================
// Zero-delay-feedback 4-pole ladder low-pass with soft saturation in the feedback path.
struct LadderFilter
{
    void reset() noexcept { s[0] = s[1] = s[2] = s[3] = 0.0f; }

    // g = tan(pi * fc / fs), k = feedback amount (0..~4)
    float process (float x, float g, float k) noexcept
    {
        const auto G = g / (1.0f + g);
        const auto beta = 1.0f / (1.0f + g);
        const auto G2 = G * G, G3 = G2 * G, G4 = G3 * G;

        const auto S = G3 * beta * s[0] + G2 * beta * s[1] + G * beta * s[2] + beta * s[3];
        const auto y4Estimate = (G4 * x + S) / (1.0f + k * G4);

        // Partial passband gain compensation keeps the low end warm as resonance goes up.
        auto u = fastTanh ((x - k * y4Estimate) * (1.0f + 0.35f * k));

        for (auto& state : s)
        {
            const auto v = (u - state) * G;
            const auto y = v + state;
            state = y + v;
            u = y;
        }
        return u;
    }

    float s[4] {};
};

//==============================================================================
// Topology-preserving state variable filter (Zavalishin). Used for tape tone, BBD darkening, hiss shaping.
struct SVF
{
    void setCoefficients (float cutoffHz, float q, float sampleRate) noexcept
    {
        cutoffHz = std::clamp (cutoffHz, 10.0f, sampleRate * 0.49f);
        g = std::tan (3.14159265f * cutoffHz / sampleRate);
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void reset() noexcept { ic1 = ic2 = 0.0f; }

    struct Out { float low, band, high; };

    Out process (float x) noexcept
    {
        const auto v3 = x - ic2;
        const auto v1 = a1 * ic1 + a2 * v3;
        const auto v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return { v2, v1, x - k * v1 - v2 };
    }

    float g = 0.0f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f, ic1 = 0.0f, ic2 = 0.0f;
};

//==============================================================================
// Slowly wandering random value in [-1, 1]: picks a new target at random intervals and glides to it.
struct RandomWalk
{
    void prepare (float controlRateHz, float minIntervalS, float maxIntervalS, float glideS, uint32_t seed) noexcept
    {
        rng = Random (seed);
        rate = controlRateHz;
        minInterval = minIntervalS;
        maxInterval = maxIntervalS;
        coef = 1.0f - std::exp (-1.0f / (glideS * controlRateHz));
        value = target = rng.bipolar() * 0.5f;
        countdown = 0;
    }

    float process() noexcept
    {
        if (--countdown <= 0)
        {
            target = rng.bipolar();
            countdown = (int) ((minInterval + rng.uniform() * (maxInterval - minInterval)) * rate);
        }
        value += (target - value) * coef;
        return value;
    }

    Random rng;
    float rate = 1000.0f, minInterval = 0.2f, maxInterval = 0.6f, coef = 0.01f, value = 0.0f, target = 0.0f;
    int countdown = 0;
};

//==============================================================================
// Circular delay line with cubic (Hermite) interpolated reads. Storage is allocated in prepare() only.
struct DelayLine
{
    void prepare (int maxSamples)
    {
        size = 1;
        while (size < maxSamples + 4) size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        writePos = 0;
    }

    void reset() noexcept { std::fill (buffer.begin(), buffer.end(), 0.0f); }

    void push (float x) noexcept
    {
        buffer[(size_t) writePos] = x;
        writePos = (writePos + 1) & mask;
    }

    // delaySamples >= 1, measured back from the most recently pushed sample
    float read (float delaySamples) const noexcept
    {
        const auto pos = (float) writePos - 1.0f - delaySamples + (float) size;
        const auto i = (int) pos;
        const auto f = pos - (float) i;
        const auto xm1 = buffer[(size_t) ((i - 1) & mask)];
        const auto x0  = buffer[(size_t) (i & mask)];
        const auto x1  = buffer[(size_t) ((i + 1) & mask)];
        const auto x2  = buffer[(size_t) ((i + 2) & mask)];
        const auto c1 = 0.5f * (x1 - xm1);
        const auto c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const auto c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    std::vector<float> buffer;
    int size = 0, mask = 0, writePos = 0;
};

//==============================================================================
struct OnePole
{
    void setTime (float seconds, float sampleRate) noexcept { coef = 1.0f - std::exp (-1.0f / (std::max (seconds, 1.0e-5f) * sampleRate)); }
    float process (float target) noexcept { value += (target - value) * coef; return value; }
    float coef = 1.0f, value = 0.0f;
};
} // namespace dust
