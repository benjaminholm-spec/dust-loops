// Renders the editor (front and back panel) to PNG files after playing a chord into the tape for a
// few seconds, so the UI can be checked without opening a DAW.
//   cmake --build build --target DustSnapshot && build/DustSnapshot_artefacts/Release/DustSnapshot <out-dir>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Plugin/PluginProcessor.h"
#include "Plugin/PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();

    DustLoopsProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    for (int note : { 48, 55, 60, 64 })
        midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.8f), 0);
    for (int b = 0; b < 48000 * 6 / 512; ++b)
    {
        buffer.clear();
        processor.processBlock (buffer, midi);
        midi.clear();
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setSize (1000, 640);
    for (int i = 0; i < 20; ++i) // let timers run so meters and readouts settle
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        buffer.clear();
        processor.processBlock (buffer, midi);
    }

    const auto save = [&] (const juce::String& name)
    {
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
        juce::PNGImageFormat png;
        auto file = outDir.getChildFile (name);
        file.deleteFile();
        juce::FileOutputStream out (file);
        png.writeImageToStream (image, out);
        std::cout << file.getFullPathName() << std::endl;
    };
    save ("front.png");

    // Click BACK by finding the button with that label in the hierarchy
    std::function<juce::Component* (juce::Component&)> findBack = [&] (juce::Component& c) -> juce::Component*
    {
        for (auto* child : c.getChildren())
        {
            if (child->getProperties()["hint"].toString().startsWith ("BACK PANEL")) return child;
            if (auto* found = findBack (*child)) return found;
        }
        return nullptr;
    };
    if (auto* back = findBack (*editor))
    {
        back->mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), {}, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                           back, back, juce::Time::getCurrentTime(), {}, juce::Time::getCurrentTime(), 1, false));
        save ("back.png");
    }
    editor.reset();
    return 0;
}
