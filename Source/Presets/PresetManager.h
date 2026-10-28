#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Factory presets (JSON files in presets/<Bank>/, built into the plugin) and user presets
// (~/Music/<product>/Presets/*.json). A preset stores parameters in real units, choices by name, and
// the pedalboard order. Anything a preset leaves out goes back to its default, so presets always sound
// the same. Transport keys, REC arms, CLEAR, FREEZE and the input level are never stored or changed.
//
// By default loading a preset only changes the *sound* (synth.* and atmos.*): the tape, the loops and
// the pedals are left alone, so different sounds can be layered on one tape. With "load deck" on
// (always in the FX), presets set the tape and pedals too.
// Message thread only, apart from the modified flag.
class PresetManager : private juce::AudioProcessorValueTreeState::Listener
{
public:
    struct Preset
    {
        juce::String name, bank, mood, description; // bank = category (Pads, Keys, ...) or "User"
        juce::var data;
        juce::File file; // user presets only
        bool isFactory = true;
        juce::String key() const { return bank + "/" + name; }
    };

    static constexpr auto userBank = "User";
    static const juce::StringArray& factoryBanks(); // the instrument categories, in browser order

    PresetManager (juce::AudioProcessorValueTreeState&, std::function<juce::String()> getOrder,
                   std::function<void (const juce::String&)> setOrder);
    ~PresetManager() override;

    const std::vector<Preset>& presets() const noexcept { return list; }
    int numFactory() const noexcept { return factoryCount; }
    int currentIndex() const noexcept { return current; }
    const Preset* currentPreset() const noexcept;
    bool isModified() const noexcept { return modified.load(); }

    void load (int index);
    void applyPreset (const juce::var& data) { apply (data); }
    // Steps through `order` (e.g. the browser's filtered list), wrapping around.
    void step (int delta, const std::vector<int>& order);

    // Saves the current sound as a user preset (overwriting one with the same name). Returns the index.
    int saveUser (const juce::String& name, const juce::String& description);
    bool canOverwrite() const;           // the current preset is a user preset
    void deleteUser (int index);
    void rescanUser();
    static juce::File userFolder();

    bool isFavourite (int index) const;
    void toggleFavourite (int index);

    // Indices matching a category ("" = all, "Favourites", or a category / "User") and a search text.
    std::vector<int> filter (const juce::String& bank, const juce::String& search) const;

    // Remembers which preset a saved project was using.
    void writeState (juce::ValueTree& state) const;
    void readState (const juce::ValueTree& state);

    std::function<void()> onChange; // the list, the current preset or its modified state changed

    static bool isStored (const juce::String& paramId);
    static bool isSound (const juce::String& paramId);

    bool loadsDeck() const noexcept { return deckToo.load(); }
    void setLoadsDeck (bool, bool remember = true);
    juce::var capture (const juce::String& name, const juce::String& bank, const juce::String& description) const;

private:
    void parameterChanged (const juce::String&, float) override;
    void loadFactory();
    void apply (const juce::var& data);
    void notify() { if (onChange) onChange(); }

    juce::AudioProcessorValueTreeState& apvts;
    std::function<juce::String()> getOrder;
    std::function<void (const juce::String&)> setOrder;
    std::vector<Preset> list;
    int factoryCount = 0, current = -1;
    juce::StringArray favourites;
    std::atomic<bool> modified { false };
    std::atomic<bool> applying { false }, deckToo { false };
};
