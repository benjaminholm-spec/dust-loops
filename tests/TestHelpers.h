#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Plugin/PluginProcessor.h"

namespace test
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;

inline void prepare (DustLoopsProcessor& p)
{
    p.setPlayConfigDetails (2, 2, sampleRate, blockSize);
    p.prepareToPlay (sampleRate, blockSize);
}

inline void setParam (DustLoopsProcessor& p, const juce::String& id, float realValue)
{
    auto* param = p.apvts.getParameter (id);
    jassert (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (realValue));
}

inline bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (ch, i)))
                return false;
    return true;
}

// A host stand-in for tempo and transport.
struct FakePlayHead : juce::AudioPlayHead
{
    double bpm = 120.0;
    bool playing = false;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setIsPlaying (playing);
        return info;
    }
};
} // namespace test
