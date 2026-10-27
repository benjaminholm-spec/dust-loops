#pragma once

#include "DeckParts.h"

class DustLoopsProcessor;
class PresetManager;

// The preset browser, shown in place of the deck: banks on the left, a search box, and the list.
// One click loads a preset (so you can audition while you play), the star marks a favourite, a double
// click loads it and closes the browser.
namespace deck
{
class PresetBrowser : public juce::Component, private juce::ListBoxModel
{
public:
    explicit PresetBrowser (DustLoopsProcessor&);
    ~PresetBrowser() override;

    std::function<void()> onClose;
    std::function<void (const juce::String&)> onHint;

    // The filtered list, also used by the arrows next to the preset display.
    const std::vector<int>& visibleOrder() const noexcept { return order; }
    void refresh(); // after the preset list or the current preset changed

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    int getNumRows() override { return (int) order.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void selectedRowsChanged (int lastRowSelected) override;

    void setBank (const juce::String&);
    void save();
    void startSaveAs();
    void finishSaveAs (bool keep);
    void deleteCurrent();
    void showDescription (int row);

    PresetManager& presets;
    juce::String bank; // "" = all
    std::vector<int> order;
    std::vector<std::unique_ptr<ActionButton>> bankButtons;
    juce::TextEditor search, nameEditor;
    juce::ListBox list { "Presets", this };
    ActionButton saveButton { "Save", "SAVE: store your changes in this user preset. On a factory preset it asks for a new name." },
                 saveAsButton { "Save As", "SAVE AS: store the current sound as a new user preset." },
                 deleteButton { "Delete", "DELETE: removes the selected user preset (factory presets can't be deleted)." },
                 folderButton { "Folder", "FOLDER: opens your user preset folder in Finder." },
                 closeButton { "Close", "CLOSE: back to the deck. (Clicking the preset display does this too.)" },
                 okButton { "Save", "SAVE: store it under the name you typed." },
                 cancelButton { "Cancel", "CANCEL: don't save." },
                 deckButton { "Tape + Pedals",
                              "TAPE + PEDALS: off (normal) = presets only change the synth sound; the tape, loops and pedals stay as they are, "
                              "so you can layer different sounds on one tape. Lit = presets also set the tape and pedals. REC is never changed." };
    bool syncingSelection = false, savingAs = false;
    double deleteArmedAt = 0.0;
    CachedBackground background;
};
} // namespace deck
