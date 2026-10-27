#pragma once

#include "DSP/Primitives.h"
#include "DSP/Safety.h"
#include <array>
#include <vector>

// The five stompboxes. Each works in place on a stereo block and is allocation-free after prepare().
namespace dust
{
inline void dcBlock (float& x, float& xPrev, float& yPrev, float coef) noexcept
{
    const auto y = x - xPrev + coef * yPrev;
    xPrev = x;
    yPrev = y;
    x = y;
}

//==============================================================================
// DIRT: tape drive -> fuzz -> bitcrush / sample-rate reduction, morphed by one BLEND knob.
class Dirt
{
public:
    struct Settings { float drive = 0.4f, blend = 0.0f, tone = 0.5f, mix = 1.0f; };

    void prepare (float sr)
    {
        sampleRate = sr;
        dcCoef = 1.0f - twoPi * 10.0f / sr;
        reset();
    }

    void reset()
    {
        for (auto& f : toneFilter) f.reset();
        for (auto& v : held) v = 0.0f;
        holdCounter = 0.0f;
        dcX[0] = dcX[1] = dcY[0] = dcY[1] = 0.0f;
    }

    void process (float* left, float* right, int numSamples, const Settings& s) noexcept
    {
        const auto tapeGain = 1.0f + 6.0f * s.drive;
        const auto tapeMakeup = 1.0f / std::pow (tapeGain, 0.6f);
        const auto fuzzGain = 2.0f + 40.0f * s.drive * s.drive;
        const auto toFuzz = std::clamp (s.blend * 2.0f, 0.0f, 1.0f);
        const auto crush = std::clamp (s.blend * 2.0f - 1.0f, 0.0f, 1.0f);
        const auto holdStep = 1.0f / (1.0f + crush * crush * 30.0f); // down to ~1/31 of the sample rate
        const auto levels = std::exp2 (15.0f - 11.0f * crush);       // 15 bits down to 4
        constexpr float bias = 0.18f;
        const auto biasOut = shapeFuzz (bias);
        for (auto& f : toneFilter) f.setCoefficients (600.0f * std::exp2 (s.tone * 4.6f), 0.6f, sampleRate);

        float* ch[] { left, right };
        for (int i = 0; i < numSamples; ++i)
        {
            holdCounter += holdStep;
            const auto sampleNow = holdCounter >= 1.0f;
            if (sampleNow) holdCounter -= 1.0f;

            for (int c = 0; c < 2; ++c)
            {
                const auto x = ch[c][i];
                const auto tape = fastTanh (x * tapeGain) * tapeMakeup;
                auto y = tape;
                if (toFuzz > 0.0f)
                {
                    const auto fuzz = (shapeFuzz (x * fuzzGain + bias) - biasOut) * 0.45f;
                    y += toFuzz * (fuzz - tape);
                }
                if (crush > 0.0f)
                {
                    if (sampleNow) held[c] = std::round (y * levels) / levels;
                    y += crush * (held[c] - y);
                }
                y = toneFilter[c].process (y).low;
                dcBlock (y, dcX[c], dcY[c], dcCoef);
                ch[c][i] = x + s.mix * (y - x);
            }
        }
    }

private:
    // Asymmetric transistor-ish clipper
    static float shapeFuzz (float v) noexcept { return v > 0.0f ? fastTanh (v * 1.4f) : 0.8f * fastTanh (v * 0.9f); }

    float sampleRate = 44100.0f, holdCounter = 0.0f, dcCoef = 0.999f;
    float held[2] {}, dcX[2] {}, dcY[2] {};
    SVF toneFilter[2];
};

//==============================================================================
// WARBLE: BBD chorus / vibrato, from gentle shimmer to seasick.
class Warble
{
public:
    struct Settings { float rate = 0.6f, depth = 0.3f, mix = 0.5f; int mode = 0; }; // mode 0 chorus, 1 vibrato

    void prepare (float sr)
    {
        sampleRate = sr;
        for (auto& d : delays) d.prepare ((int) (0.04f * sr));
        for (auto& f : bbdFilter) f.setCoefficients (6500.0f, 0.6f, sr);
        drift.prepare (sr, 0.3f, 1.2f, 0.4f, 0xabcdefu);
        depthSmooth.setTime (0.05f, sr);
        reset();
    }

    void reset()
    {
        for (auto& d : delays) d.reset();
        for (auto& f : bbdFilter) f.reset();
        phase = 0.0f;
    }

    void process (float* left, float* right, int numSamples, const Settings& s) noexcept
    {
        const auto msToSamples = sampleRate * 0.001f;
        float* ch[] { left, right };
        for (int i = 0; i < numSamples; ++i)
        {
            phase += s.rate / sampleRate;
            if (phase >= 1.0f) phase -= 1.0f;
            const auto d = depthSmooth.process (s.depth);
            const auto depthMs = 0.15f + d * d * 9.0f;   // up to +/-9 ms: properly seasick
            const auto wander = d * d * 2.5f * drift.process();
            const auto centreMs = depthMs + 2.0f + std::abs (wander);

            for (int c = 0; c < 2; ++c)
            {
                const auto x = ch[c][i];
                const auto p = phase + (c == 0 ? 0.0f : 0.25f);
                const auto tri = 4.0f * std::abs (p - std::floor (p) - 0.5f) - 1.0f;
                delays[c].push (x);
                const auto wet = bbdFilter[c].process (delays[c].read ((centreMs + depthMs * tri + wander) * msToSamples)).low;
                ch[c][i] = s.mode == 0 ? (x + s.mix * wet) / (1.0f + 0.5f * s.mix) // chorus: dry + wet
                                       : x + s.mix * (wet - x);                     // vibrato: wet only at full mix
            }
        }
    }

private:
    float sampleRate = 44100.0f, phase = 0.0f;
    DelayLine delays[2];
    SVF bbdFilter[2];
    RandomWalk drift;
    OnePole depthSmooth;
};

//==============================================================================
// DRIFT FILTER: resonant lowpass / bandpass with a very slow LFO (down to one sweep in two minutes)
// and an envelope follower.
class DriftFilter
{
public:
    struct Settings { float freq = 1200.0f, reso = 0.3f, lfoRate = 0.05f, lfoDepth = 0.4f, envAmt = 0.0f; int mode = 0; };

    void prepare (float sr)
    {
        sampleRate = sr;
        envelope.setTime (0.08f, sr);
        cutoffSmooth.setTime (0.03f, sr / (float) controlInterval);
        reset();
    }

    void reset()
    {
        for (auto& f : filters) f.reset();
        envelope.value = 0.0f;
        cutoffSmooth.value = -1.0f;
        counter = 0;
    }

    float currentCutoff() const noexcept { return cutoffSmooth.value; }

    void process (float* left, float* right, int numSamples, const Settings& s) noexcept
    {
        const auto q = 0.6f + s.reso * s.reso * 14.0f;
        float* ch[] { left, right };
        for (int i = 0; i < numSamples; ++i)
        {
            const auto env = envelope.process (std::max (std::abs (left[i]), std::abs (right[i])));
            if (counter-- <= 0)
            {
                counter = controlInterval - 1;
                phase += s.lfoRate * (float) controlInterval / sampleRate;
                if (phase >= 1.0f) phase -= 1.0f;
                const auto octaves = s.lfoDepth * 3.0f * std::sin (twoPi * phase) + s.envAmt * 5.0f * std::min (1.0f, env * 2.0f);
                const auto target = std::clamp (s.freq * std::exp2 (octaves), 30.0f, 0.45f * sampleRate);
                if (cutoffSmooth.value < 0.0f) cutoffSmooth.value = target;
                const auto hz = cutoffSmooth.process (target);
                for (auto& f : filters) f.setCoefficients (hz, q, sampleRate);
            }
            for (int c = 0; c < 2; ++c)
            {
                const auto out = filters[c].process (softClip (ch[c][i] * 0.8f, 0.8f, 1.5f));
                const auto y = s.mode == 0 ? out.low : out.band * filters[c].k * 1.6f; // bandpass normalised to ~unity peak
                ch[c][i] = softClip (y * 1.25f, 0.8f, 1.6f);
            }
        }
    }

private:
    static constexpr int controlInterval = 16;
    float sampleRate = 44100.0f, phase = 0.0f;
    int counter = 0;
    SVF filters[2];
    OnePole envelope, cutoffSmooth;
};

//==============================================================================
// CLOUD: granular pitch smear. Overlapping grains, each pitched (octave down, fifth, octave up, or up
// and down), scattered in time and space, with a little regeneration so textures swell.
class Cloud
{
public:
    struct Settings { int pitch = 2; float smear = 0.5f, density = 0.5f, mix = 0.4f; };

    void prepare (float sr)
    {
        sampleRate = sr;
        size = 1;
        while (size < (int) (2.6f * sr)) size <<= 1;
        mask = size - 1;
        buffer.assign ((size_t) size * 2, 0.0f);
        reset();
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        for (auto& g : grains) g.active = false;
        writePos = 0;
        spawnCountdown = 0.0f;
        lastWet[0] = lastWet[1] = 0.0f;
    }

    void process (float* left, float* right, int numSamples, const Settings& s) noexcept
    {
        const auto grainLength = (0.06f + 0.34f * s.smear) * sampleRate;
        const auto overlap = 1.0f + 5.0f * s.density;
        const auto spawnInterval = grainLength / overlap;
        const auto norm = 1.6f / overlap;
        const auto regen = 0.45f * s.density;

        for (int i = 0; i < numSamples; ++i)
        {
            if ((spawnCountdown -= 1.0f) <= 0.0f)
            {
                spawnCountdown += spawnInterval * (0.7f + 0.6f * rng.uniform());
                spawn (grainLength, s);
            }

            float wetL = 0.0f, wetR = 0.0f;
            for (auto& g : grains)
            {
                if (! g.active) continue;
                const auto w = 0.5f - 0.5f * std::cos (twoPi * g.age / g.length);
                const auto idx = (int) std::floor (g.position);
                const auto f = (float) (g.position - (double) idx);
                const auto a = (size_t) (idx & mask) * 2, b = (size_t) ((idx + 1) & mask) * 2;
                const auto l = buffer[a] + f * (buffer[b] - buffer[a]);
                const auto r = buffer[a + 1] + f * (buffer[b + 1] - buffer[a + 1]);
                wetL += w * (l * g.gainL);
                wetR += w * (r * g.gainR);
                g.position += g.ratio;
                if ((g.age += 1.0f) >= g.length) g.active = false;
            }
            wetL *= norm; wetR *= norm;

            const auto x0 = left[i], x1 = right[i];
            const auto cell = (size_t) writePos * 2;
            buffer[cell] = softClip (x0 + regen * lastWet[0], 0.7f, 1.0f);
            buffer[cell + 1] = softClip (x1 + regen * lastWet[1], 0.7f, 1.0f);
            writePos = (writePos + 1) & mask;
            lastWet[0] = wetL; lastWet[1] = wetR;

            left[i] = x0 * (1.0f - 0.5f * s.mix) + wetL * s.mix;
            right[i] = x1 * (1.0f - 0.5f * s.mix) + wetR * s.mix;
        }
    }

private:
    struct Grain
    {
        bool active = false;
        double position = 0.0;
        float ratio = 1.0f, age = 0.0f, length = 1.0f, gainL = 1.0f, gainR = 1.0f;
    };

    void spawn (float grainLength, const Settings& s) noexcept
    {
        for (auto& g : grains)
        {
            if (g.active) continue;
            float ratio;
            switch (s.pitch)
            {
                case 0:  ratio = 0.5f; break;
                case 1:  ratio = 1.5f; break;
                case 3:  ratio = (alternate = ! alternate) ? 2.0f : 0.5f; break;
                default: ratio = 2.0f; break;
            }
            ratio *= 1.0f + s.smear * 0.012f * rng.bipolar();
            // Start far enough back that a fast grain never overtakes the write head.
            const auto scatter = s.smear * 0.6f * sampleRate * rng.uniform();
            const auto delay = grainLength * std::max (0.0f, ratio - 1.0f) + 64.0f + scatter;
            g.position = (double) writePos - (double) delay + (double) size;
            g.ratio = ratio;
            g.age = 0.0f;
            g.length = grainLength;
            const auto pan = s.smear * 0.8f * rng.bipolar();
            g.gainL = std::min (1.0f, 1.0f - pan);
            g.gainR = std::min (1.0f, 1.0f + pan);
            g.active = true;
            return;
        }
    }

    float sampleRate = 44100.0f, spawnCountdown = 0.0f;
    int size = 1, mask = 0, writePos = 0;
    bool alternate = false;
    std::vector<float> buffer;
    std::array<Grain, 14> grains;
    float lastWet[2] {};
    Random rng { 0xc10du };
};

//==============================================================================
// HALL: huge dark reverb (8-line feedback delay network), decay up to 60 s, with its own freeze.
class Hall
{
public:
    struct Settings { float decay = 8.0f, tone = 0.35f, mix = 0.3f; bool freeze = false; };

    void prepare (float sr)
    {
        sampleRate = sr;
        const auto scale = sr / 44100.0f;
        preDelay.prepare ((int) (0.05f * sr));
        for (size_t i = 0; i < 4; ++i)
        {
            diffusers[i].length = (int) ((float) diffuserLengths[i] * scale);
            diffusers[i].line.prepare (diffusers[i].length + 8);
        }
        for (size_t i = 0; i < numLines; ++i)
            lines[i].prepare ((int) (lineMs[i] * lengthScale * 0.001f * sr) + 128);
        inputGain.setTime (0.05f, sr);
        modDepth.setTime (0.3f, sr);
        reset();
    }

    void reset()
    {
        modDepth.value = 1.0f;
        preDelay.reset();
        for (auto& d : diffusers) d.line.reset();
        for (auto& l : lines) l.reset();
        for (auto& v : damp) v = 0.0f;
        inputGain.value = 1.0f;
    }

    void process (float* left, float* right, int numSamples, const Settings& s) noexcept
    {
        // Frozen: no damping, (almost) no loss, no new input. The tail hangs indefinitely.
        const auto dampHz = 1200.0f * std::exp2 (s.tone * 3.8f);
        const auto dampCoef = s.freeze ? 0.0f : std::exp (-twoPi * dampHz / sampleRate);
        float gains[numLines];
        for (size_t l = 0; l < numLines; ++l)
        {
            const auto lenSeconds = lineMs[l] * 0.001f * lengthScale;
            gains[l] = s.freeze ? 0.99999f : std::pow (10.0f, -3.0f * lenSeconds / std::max (0.5f, s.decay));
        }

        for (int i = 0; i < numSamples; ++i)
        {
            const auto inL = left[i], inR = right[i];
            const auto gin = inputGain.process (s.freeze ? 0.0f : 1.0f);

            preDelay.push (0.5f * (inL + inR) * gin);
            auto x = preDelay.read (0.025f * sampleRate);
            for (auto& d : diffusers)
            {
                const auto delayed = d.line.read ((float) d.length);
                const auto v = x + 0.65f * delayed;
                d.line.push (v);
                x = delayed - 0.65f * v;
            }

            modPhase += 0.23f / sampleRate;
            if (modPhase >= 1.0f) modPhase -= 1.0f;
            // Modulation fades out while frozen: whole-sample delays loop the tail without any loss.
            const auto mod = std::sin (twoPi * modPhase) * 0.0016f * sampleRate * modDepth.process (s.freeze ? 0.0f : 1.0f);

            float outs[numLines];
            for (size_t l = 0; l < numLines; ++l)
            {
                auto len = std::round (lineMs[l] * 0.001f * sampleRate * lengthScale);
                if (l == 1 || l == 4) len += mod;
                if (l == 6 || l == 3) len -= mod;
                const auto y = lines[l].read (std::max (4.0f, len));
                damp[l] = y + (damp[l] - y) * dampCoef;
                outs[l] = damp[l] * gains[l];
            }

            for (size_t h = 1; h < numLines; h <<= 1)
                for (size_t a = 0; a < numLines; a += h << 1)
                    for (size_t b = a; b < a + h; ++b)
                    {
                        const auto u = outs[b], v = outs[b + h];
                        outs[b] = u + v;
                        outs[b + h] = u - v;
                    }

            constexpr float norm = 0.35355339f; // 1/sqrt(8): the mix matrix is lossless
            for (size_t l = 0; l < numLines; ++l)
                lines[l].push (sanitize (outs[l] * norm + x * (l % 2 == 0 ? 0.5f : -0.5f)));

            const auto wetL = (damp[0] - damp[2] + damp[4] - damp[6]) * 0.5f;
            const auto wetR = (damp[1] - damp[3] + damp[5] - damp[7]) * 0.5f;
            left[i] = inL * (1.0f - 0.3f * s.mix) + wetL * s.mix;
            right[i] = inR * (1.0f - 0.3f * s.mix) + wetR * s.mix;
        }
    }

private:
    static constexpr size_t numLines = 8;
    static constexpr float lengthScale = 1.7f; // a big room
    static constexpr float lineMs[numLines] = { 37.1f, 41.3f, 47.9f, 53.3f, 59.9f, 67.7f, 73.1f, 79.3f };
    static constexpr int diffuserLengths[4] = { 142, 107, 379, 277 };

    struct Diffuser { DelayLine line; int length = 100; };

    float sampleRate = 44100.0f, modPhase = 0.0f;
    DelayLine preDelay;
    Diffuser diffusers[4];
    DelayLine lines[numLines];
    float damp[numLines] {};
    OnePole inputGain, modDepth;
};

//==============================================================================
// The board: a main copy of every pedal for the signal chain, and a second copy for the tape's
// feedback path (LOOP on). Order is set by the processor; TAPE is a slot in that order.
struct PedalBoardSettings
{
    static constexpr int numPedals = 5;
    std::array<bool, numPedals> on {}, inLoop {};
    std::array<float, numPedals> loopAmount {};
    Dirt::Settings dirt;
    Warble::Settings warble;
    DriftFilter::Settings drift;
    Cloud::Settings cloud;
    Hall::Settings hall;
};

class PedalBoard
{
public:
    static constexpr int numPedals = PedalBoardSettings::numPedals;
    static constexpr int tapeSlot = numPedals; // the TAPE block's id in the chain order
    using Order = std::array<int, numPedals + 1>;

    void prepare (float sr, int maxBlock)
    {
        for (int copy = 0; copy < 2; ++copy)
        {
            dirt[copy].prepare (sr); warble[copy].prepare (sr); drift[copy].prepare (sr);
            cloud[copy].prepare (sr); hall[copy].prepare (sr);
        }
        for (auto& b : scratch) b.assign ((size_t) maxBlock, 0.0f);
        for (auto& b : loopBus) b.assign ((size_t) maxBlock, 0.0f);
        const auto rampStep = 1.0f / (0.02f * sr);
        step = rampStep;
        mainMix.fill (0.0f);
        loopMix.fill (0.0f);
    }

    int maxBlock() const noexcept { return (int) scratch[0].size(); }

    // Main chain: one pedal, in place. Fades in and out on the footswitch instead of clicking.
    void processMain (int pedal, float* left, float* right, int numSamples, const PedalBoardSettings& s) noexcept
    {
        processWithRamp (0, pedal, mainMix[(size_t) pedal], s.on[(size_t) pedal] ? 1.0f : 0.0f, left, right, numSamples, s);
    }

    // Feedback path: the tape's return goes through every pedal that has LOOP on (in board order), and
    // only what the pedals add is handed back for re-recording, so its effect compounds on each pass.
    void processLoop (const Order& order, const float* returnL, const float* returnR, float* injectL, float* injectR,
                      int numSamples, const PedalBoardSettings& s) noexcept
    {
        auto* xl = loopBus[0].data();
        auto* xr = loopBus[1].data();
        std::copy (returnL, returnL + numSamples, xl);
        std::copy (returnR, returnR + numSamples, xr);
        for (auto pedal : order)
        {
            if (pedal == tapeSlot) continue;
            const auto target = s.on[(size_t) pedal] && s.inLoop[(size_t) pedal] ? s.loopAmount[(size_t) pedal] : 0.0f;
            processWithRamp (1, pedal, loopMix[(size_t) pedal], target, xl, xr, numSamples, s);
        }
        for (int i = 0; i < numSamples; ++i)
        {
            injectL[i] = softClip (sanitize (xl[i] - returnL[i]), 0.6f, 1.0f);
            injectR[i] = softClip (sanitize (xr[i] - returnR[i]), 0.6f, 1.0f);
        }
    }

    float driftCutoff() const noexcept { return drift[0].currentCutoff(); }

private:
    void processWithRamp (int copy, int pedal, float& mix, float target, float* left, float* right, int numSamples,
                          const PedalBoardSettings& s) noexcept
    {
        if (mix <= 0.0f && target <= 0.0f)
            return;

        auto* wl = scratch[0].data();
        auto* wr = scratch[1].data();
        std::copy (left, left + numSamples, wl);
        std::copy (right, right + numSamples, wr);
        switch (pedal)
        {
            case 0: dirt[copy].process (wl, wr, numSamples, s.dirt); break;
            case 1: warble[copy].process (wl, wr, numSamples, s.warble); break;
            case 2: drift[copy].process (wl, wr, numSamples, s.drift); break;
            case 3: cloud[copy].process (wl, wr, numSamples, s.cloud); break;
            case 4: hall[copy].process (wl, wr, numSamples, s.hall); break;
            default: return;
        }
        for (int i = 0; i < numSamples; ++i)
        {
            mix = target > mix ? std::min (target, mix + step) : std::max (target, mix - step);
            left[i] += mix * (wl[i] - left[i]);
            right[i] += mix * (wr[i] - right[i]);
        }
    }

    Dirt dirt[2];
    Warble warble[2];
    DriftFilter drift[2];
    Cloud cloud[2];
    Hall hall[2];
    std::array<std::vector<float>, 2> scratch, loopBus;
    std::array<float, numPedals> mainMix {}, loopMix {};
    float step = 0.001f;
};
} // namespace dust
