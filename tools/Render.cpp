// Renders every factory preset playing a held chord (10 s) and its tail (5 s), and prints the levels.
//   DustRender                          print the report
//   DustRender --wav <dir>              also write each render as a WAV file
//   DustRender --trims <file.json>      update loudness trims (dB per preset) toward the target level;
//                                       tools/make_presets.py applies them to synth.level
#include <juce_audio_formats/juce_audio_formats.h>
#include "Plugin/PluginProcessor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (argv[i]);
    const auto argAfter = [&] (const char* flag) { const auto i = args.indexOf (flag); return i >= 0 ? args[i + 1] : juce::String(); };
    const juce::String wavDir = argAfter ("--wav"), trimsPath = argAfter ("--trims");
    constexpr double targetDb = -18.0, sr = 48000.0;
    constexpr int block = 512;

    DustLoopsProcessor processor;
    processor.setPlayConfigDetails (2, 2, sr, block);
    auto& pm = *processor.presets;

    juce::var trims (new juce::DynamicObject());
    if (trimsPath.isNotEmpty())
        if (auto parsed = juce::JSON::parse (juce::File::getCurrentWorkingDirectory().getChildFile (trimsPath)); parsed.isObject())
            trims = parsed;

    std::cout << juce::String ("preset").paddedRight (' ', 44) << "  loud dB  peak    dc      tail dB\n";
    int problems = 0;
    for (int i = 0; i < pm.numFactory(); ++i)
    {
        const auto& preset = pm.presets()[(size_t) i];
        pm.load (i);
        processor.prepareToPlay (sr, block);

        const int holdBlocks = (int) (10.0 * sr / block), tailBlocks = (int) (5.0 * sr / block);
        juce::AudioBuffer<float> out (2, (holdBlocks + tailBlocks) * block), buffer (2, block);
        juce::MidiBuffer midi;
        for (int note : { 48, 55, 60, 64 })
            midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.8f), 0);
        for (int b = 0; b < holdBlocks + tailBlocks; ++b)
        {
            if (b == holdBlocks)
                for (int note : { 48, 55, 60, 64 })
                    midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
            buffer.clear();
            processor.processBlock (buffer, midi);
            midi.clear();
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, b * block, buffer, ch, 0, block);
        }

        // Loudness = the loudest half second while the chord is held (fair to plucks and pads alike),
        // plus the tail level, peak, DC and NaN.
        double tailSum = 0.0, dc = 0.0, loudest = 0.0;
        bool finite = true;
        const auto holdEnd = holdBlocks * block, window = (int) (sr * 0.5);
        for (int w = 0; w + window <= holdEnd; w += window / 2)
        {
            double sum = 0.0;
            for (int ch = 0; ch < 2; ++ch)
                for (int s = w; s < w + window; ++s)
                    sum += (double) out.getSample (ch, s) * out.getSample (ch, s);
            loudest = std::max (loudest, sum / (2.0 * window));
        }
        for (int ch = 0; ch < 2; ++ch)
            for (int s = 0; s < out.getNumSamples(); ++s)
            {
                const auto x = out.getSample (ch, s);
                finite = finite && std::isfinite (x);
                if (s >= holdEnd) tailSum += (double) x * x;
                dc += x;
            }
        const auto rmsDb = juce::Decibels::gainToDecibels (std::sqrt (loudest), -120.0);
        const auto tailDb = juce::Decibels::gainToDecibels (std::sqrt (tailSum / (2.0 * tailBlocks * block)), -120.0);
        const auto peak = out.getMagnitude (0, out.getNumSamples());
        dc /= 2.0 * out.getNumSamples();

        const auto bad = ! finite || rmsDb < -40.0 || std::abs (dc) > 0.01;
        problems += bad ? 1 : 0;
        std::cout << (preset.bank.substring (0, 2) + " " + preset.name).paddedRight (' ', 44)
                  << juce::String (rmsDb, 1).paddedLeft (' ', 7) << juce::String (peak, 2).paddedLeft (' ', 7)
                  << juce::String (dc, 4).paddedLeft (' ', 8) << juce::String (tailDb, 1).paddedLeft (' ', 10)
                  << (bad ? "   <-- check" : "") << "\n";

        if (trimsPath.isNotEmpty())
        {
            const auto old = (double) trims.getProperty (preset.name, 0.0);
            const auto change = juce::jlimit (-12.0, 12.0, targetDb - rmsDb);
            if (std::abs (change) > 0.75)
                trims.getDynamicObject()->setProperty (preset.name, std::round ((old + change) * 10.0) / 10.0);
        }

        if (wavDir.isNotEmpty())
        {
            const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile (wavDir);
            dir.createDirectory();
            const auto file = dir.getChildFile (juce::File::createLegalFileName (juce::String (i + 1).paddedLeft ('0', 2) + " " + preset.name) + ".wav");
            file.deleteFile();
            juce::WavAudioFormat wav;
            if (auto stream = std::unique_ptr<juce::OutputStream> (file.createOutputStream()))
                if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (24)))
                    writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
        }
    }

    if (trimsPath.isNotEmpty())
        juce::File::getCurrentWorkingDirectory().getChildFile (trimsPath).replaceWithText (juce::JSON::toString (trims));
    std::cout << pm.numFactory() << " presets, " << problems << " to check\n";
    return problems == 0 ? 0 : 1;
}
