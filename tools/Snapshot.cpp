// Renders the editor (front and back panel) to PNG files after playing a chord into the tape for a
// few seconds, so the UI can be checked without opening a DAW.
//   cmake --build build --target DustSnapshot && build/DustSnapshot_artefacts/Release/DustSnapshot <out-dir>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Plugin/PluginProcessor.h"
#include "Plugin/PluginEditor.h"
#include "Gui/Panels.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();

    DustLoopsProcessor processor;
    processor.presets->load (argc > 2 ? juce::String (argv[2]).getIntValue() : 0);
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
    editor->setSize (deck::logicalWidth, deck::logicalHeight);
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

    // Click a control by finding it through its hint text.
    std::function<juce::Component* (juce::Component&, const juce::String&)> find = [&] (juce::Component& c, const juce::String& prefix) -> juce::Component*
    {
        for (auto* child : c.getChildren())
        {
            if (child->isVisible() && child->getProperties()["hint"].toString().startsWith (prefix)) return child;
            if (auto* found = find (*child, prefix)) return found;
        }
        return nullptr;
    };
    const auto click = [&] (const juce::String& prefix)
    {
        if (auto* target = find (*editor, prefix))
        {
            const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), {}, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                      target, target, juce::Time::getCurrentTime(), {}, juce::Time::getCurrentTime(), 1, false);
            target->mouseDown (e);
            target->mouseUp (e);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
            return true;
        }
        std::cout << "not found: " << prefix << std::endl;
        return false;
    };
    if (click ("BACK PANEL")) { save ("back.png"); click ("BACK PANEL"); }
    if (click ("EDIT:"))      { save ("edit.png"); click ("EDIT:"); }
    if (click ("PRESET:"))    save ("browser.png");
    editor.reset();
    return 0;
}
