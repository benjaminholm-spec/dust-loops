#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Core/Identity.h"
#include "DSP/Safety.h"
#include "Synth/SineVoice.h"
#include "Tape/TapeMachine.h"

// One processor class for both plugins. The FX build (DUST_LOOPS_FX=1) has no synth: its main input
// feeds the tape. The instrument's input bus is a sidechain that Logic can route another track into.
class DustLoopsProcessor : public juce::AudioProcessor,
                           private juce::Timer
{
public:
    DustLoopsProcessor();
    ~DustLoopsProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; } // the FX is a MIDI-controlled effect, for CC control
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Init"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    const dust::TapeMachine& getTape() const noexcept { return tape; }

    // Post-limiter peak per channel, for the meters.
    std::atomic<float> outputPeak[2] { 0.0f, 0.0f };
    // Tempo the tape is following (host tempo, or 120 when there is none).
    std::atomic<float> currentBpm { 120.0f };

    // Not an automatable parameter: the pedalboard chain order, including the TAPE block.
    static constexpr auto defaultPedalOrder = "dirt,warble,drift,tape,cloud,hall";

private:
    static constexpr int numVoices = 8;
    static BusesProperties makeBuses();

    void timerCallback() override;
    void readTapeSettings();
    void handleKeys();

    juce::Synthesiser synth;
    std::array<dust::SineVoice*, numVoices> voices {};
    juce::AudioBuffer<float> inputScratch, tapeIn;
    dust::TapeMachine tape;
    dust::TapeSettings tapeSettings;
    dust::OutputLimiter outputLimiter;
    juce::SmoothedValue<float> masterGain, inputGain, synthGain;

    // Parameter pointers, cached once
    std::atomic<float>* masterParam = nullptr;
    std::atomic<float>* inputGainParam = nullptr;
    std::atomic<float>* synthLevelParam = nullptr;
    std::atomic<float> *attackParam = nullptr, *decayParam = nullptr, *sustainParam = nullptr, *releaseParam = nullptr;

    struct TapeParams
    {
        std::atomic<float> *mode, *time, *feed, *mix, *volume, *speed, *inertia, *freeze, *sync, *select, *brake, *type,
                           *wow, *flutter, *hiss, *saturation, *age, *dropouts;
        std::atomic<float> *keyRec, *keyPlay, *keyRew, *keyFfwd, *keyStop, *keyPause;
        std::array<std::atomic<float>*, 3> beats, seconds, level, pan, speedOffset, mute, rec, clear;
    } tp {};

    // Rising-edge detection for the momentary keys
    bool lastPlay = false, lastStop = false, lastPause = false, lastRec = false;
    std::array<bool, 3> lastClear {};
    bool hostWasPlaying = false;

    // REC changes loop arm parameters, which must happen on the message thread.
    std::atomic<bool> recKeyPressed { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DustLoopsProcessor)
};
