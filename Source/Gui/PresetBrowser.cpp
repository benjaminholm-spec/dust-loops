#include "PresetBrowser.h"
#include "Art.h"
#include "Plugin/PluginProcessor.h"
#include "Core/Identity.h"

namespace deck
{
using namespace juce;

namespace
{
const Rectangle<int> listArea { 212, 52, 772, 476 };

Path starPath (Point<float> c, float r)
{
    Path p;
    for (int i = 0; i < 10; ++i)
    {
        const auto angle = MathConstants<float>::pi * (float) i / 5.0f;
        const auto radius = i % 2 == 0 ? r : r * 0.45f;
        const Point<float> pt { c.x + radius * std::sin (angle), c.y - radius * std::cos (angle) };
        if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
    }
    p.closeSubPath();
    return p;
}

void styleEditor (TextEditor& e, const String& placeholder)
{
    e.setColour (TextEditor::backgroundColourId, Colour (0xff0a0a0a));
    e.setColour (TextEditor::textColourId, Art::Colours::silk);
    e.setColour (TextEditor::outlineColourId, Art::Colours::line);
    e.setColour (TextEditor::focusedOutlineColourId, Art::Colours::brandGold);
    e.setColour (TextEditor::highlightColourId, Art::Colours::brandGold.withAlpha (0.35f));
    e.setColour (CaretComponent::caretColourId, Art::Colours::silk);
    e.setFont (Art::silkLight (16.0f));
    e.setIndents (8, 6);
    e.setTextToShowWhenEmpty (placeholder, Art::Colours::silkDim);
}
} // namespace

PresetBrowser::PresetBrowser (DustLoopsProcessor& p) : presets (*p.presets)
{
    StringArray banks { "All", "Favourites" };
    banks.addArray (PresetManager::factoryBanks());
    banks.add (PresetManager::userBank);
    for (auto& name : banks)
    {
        const auto key = name == "All" ? String() : name;
        const auto hint = name == "All" ? String ("ALL: every preset.")
                        : name == "Favourites" ? String ("FAVOURITES: the presets you starred.")
                        : name == PresetManager::userBank ? String ("USER: presets you saved.")
                        : name.toUpperCase() + ": factory presets.";
        auto b = std::make_unique<ActionButton> (name, hint);
        b->onPress = [this, key] { setBank (key); };
        b->isLit = [this, key] { return bank == key; };
        addAndMakeVisible (*b);
        bankButtons.push_back (std::move (b));
    }

    styleEditor (search, "Search: rain, drone, bells...");
    setHint (search, "SEARCH: type a word to find presets by name, bank or description.");
    search.onTextChange = [this] { refresh(); };
    search.onEscapeKey = [this] { search.clear(); refresh(); };
    addAndMakeVisible (search);

    styleEditor (nameEditor, "Name for your preset");
    nameEditor.onReturnKey = [this] { finishSaveAs (true); };
    nameEditor.onEscapeKey = [this] { finishSaveAs (false); };
    addChildComponent (nameEditor);

    list.setRowHeight (26);
    list.setColour (ListBox::backgroundColourId, Colours::transparentBlack);
    list.setOutlineThickness (0);
    list.getViewport()->setScrollBarThickness (10);
    list.getVerticalScrollBar().setColour (ScrollBar::thumbColourId, Art::Colours::brandGold.withAlpha (0.55f));
    list.getVerticalScrollBar().setColour (ScrollBar::trackColourId, Colours::transparentBlack);
    list.addMouseListener (this, true);
    addAndMakeVisible (list);

    saveButton.onPress = [this] { save(); };
    saveAsButton.onPress = [this] { startSaveAs(); };
    deleteButton.onPress = [this] { deleteCurrent(); };
    folderButton.onPress = []
    {
        PresetManager::userFolder().createDirectory();
        PresetManager::userFolder().startAsProcess();
    };
    closeButton.onPress = [this] { if (onClose) onClose(); };
    okButton.onPress = [this] { finishSaveAs (true); };
    cancelButton.onPress = [this] { finishSaveAs (false); };
    for (auto* b : { &saveButton, &saveAsButton, &deleteButton, &folderButton, &closeButton })
        addAndMakeVisible (*b);
    deckButton.isLit = [this] { return presets.loadsDeck(); };
    deckButton.onPress = [this]
    {
        presets.setLoadsDeck (! presets.loadsDeck());
        deckButton.repaint();
        if (onHint)
            onHint (presets.loadsDeck() ? "TAPE + PEDALS on: presets now also set the tape and pedals (never REC)."
                                        : "TAPE + PEDALS off: presets only change the synth sound. The tape is yours.");
    };
    if constexpr (! dust::kIsFx)
        addAndMakeVisible (deckButton);
    addChildComponent (okButton);
    addChildComponent (cancelButton);

    refresh();
}

PresetBrowser::~PresetBrowser()
{
    list.removeMouseListener (this);
}

void PresetBrowser::setBank (const String& b)
{
    bank = b;
    for (auto& button : bankButtons)
        button->repaint();
    refresh();
}

void PresetBrowser::refresh()
{
    order = presets.filter (bank, search.getText());
    list.updateContent();

    const auto current = presets.currentIndex();
    const auto it = std::find (order.begin(), order.end(), current);
    syncingSelection = true;
    if (it != order.end())
        list.selectRow ((int) (it - order.begin()), false, true);
    else
        list.deselectAllRows();
    syncingSelection = false;

    deleteButton.setVisible (! savingAs && presets.canOverwrite());
    deleteArmedAt = 0.0;
    deleteButton.setLabel ("Delete");
    list.repaint();
}

void PresetBrowser::visibilityChanged()
{
    if (isVisible())
        refresh();
    else if (savingAs)
        finishSaveAs (false);
}

void PresetBrowser::paintListBoxItem (int row, Graphics& g, int width, int height, bool)
{
    if (row < 0 || row >= (int) order.size())
        return;
    const auto index = order[(size_t) row];
    const auto& preset = presets.presets()[(size_t) index];
    const auto isCurrent = index == presets.currentIndex();
    auto b = Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);

    if (isCurrent)
    {
        g.setColour (Colour (0xff2a1512));
        g.fillRect (b.reduced (0.0f, 1.0f));
    }
    else if (row % 2 == 1)
    {
        g.setColour (Colours::white.withAlpha (0.025f));
        g.fillRect (b);
    }

    // Favourite star
    const auto star = starPath ({ 18.0f, b.getCentreY() }, 7.5f);
    if (presets.isFavourite (index))
    {
        g.setColour (Art::Colours::brandGold);
        g.fillPath (star);
    }
    else
    {
        g.setColour (Art::Colours::silkDim.withAlpha (0.5f));
        g.strokePath (star, PathStrokeType (1.0f));
    }

    auto text = b.withTrimmedLeft (36.0f).withTrimmedRight (12.0f);
    g.setFont (Art::silkLight (12.0f));
    g.setColour (Art::Colours::silkDim);
    g.drawText (preset.bank.toUpperCase(), text.removeFromRight (150.0f), Justification::centredRight, false);

    g.setFont (Art::silk (16.0f));
    g.setColour (isCurrent ? Art::Colours::ledRed : Art::Colours::silk);
    auto name = preset.name;
    if (isCurrent && presets.isModified())
        name << "  (edited)";
    g.drawText (name, text, Justification::centredLeft, true);
}

void PresetBrowser::listBoxItemClicked (int row, const MouseEvent& e)
{
    if (row < 0 || row >= (int) order.size())
        return;
    const auto index = order[(size_t) row];
    if (e.x < 34)
        presets.toggleFavourite (index);
    else
        presets.load (index);
    refresh();
}

void PresetBrowser::listBoxItemDoubleClicked (int row, const MouseEvent& e)
{
    if (row >= 0 && row < (int) order.size() && e.x >= 34)
    {
        presets.load (order[(size_t) row]);
        if (onClose) onClose();
    }
}

void PresetBrowser::selectedRowsChanged (int lastRowSelected)
{
    // Arrow keys in the list load as they move; clicks are handled in listBoxItemClicked.
    if (syncingSelection || ModifierKeys::currentModifiers.isAnyMouseButtonDown())
        return;
    if (lastRowSelected >= 0 && lastRowSelected < (int) order.size() && order[(size_t) lastRowSelected] != presets.currentIndex())
    {
        presets.load (order[(size_t) lastRowSelected]);
        list.repaint();
        showDescription (lastRowSelected);
    }
}

void PresetBrowser::showDescription (int row)
{
    if (onHint == nullptr || row < 0 || row >= (int) order.size())
        return;
    const auto& preset = presets.presets()[(size_t) order[(size_t) row]];
    onHint (preset.name.toUpperCase() + ": " + (preset.description.isNotEmpty() ? preset.description : String ("your preset.")));
}

void PresetBrowser::mouseMove (const MouseEvent& e)
{
    if (e.eventComponent != this && list.isParentOf (e.eventComponent))
    {
        const auto pos = e.getEventRelativeTo (&list).getPosition();
        const auto row = list.getRowContainingPosition (pos.x, pos.y);
        if (row >= 0 && pos.x < 34)
            onHint ? onHint ("STAR: mark or unmark this preset as a favourite.") : void();
        else
            showDescription (row);
    }
}

void PresetBrowser::save()
{
    if (! presets.canOverwrite())
    {
        startSaveAs();
        return;
    }
    const auto* p = presets.currentPreset();
    presets.saveUser (p->name, p->description);
    refresh();
    if (onHint) onHint ("SAVED: " + p->name);
}

void PresetBrowser::startSaveAs()
{
    savingAs = true;
    const auto* p = presets.currentPreset();
    nameEditor.setText (p != nullptr && ! p->isFactory ? p->name : String(), false);
    search.setVisible (false);
    nameEditor.setVisible (true);
    for (auto* b : { &saveButton, &saveAsButton, &deleteButton, &folderButton })
        b->setVisible (false);
    okButton.setVisible (true);
    cancelButton.setVisible (true);
    nameEditor.grabKeyboardFocus();
    if (onHint) onHint ("SAVE AS: type a name and press Return (or SAVE). A user preset with the same name is replaced.");
}

void PresetBrowser::finishSaveAs (bool keep)
{
    if (! savingAs)
        return;
    savingAs = false;
    const auto name = nameEditor.getText().trim();
    nameEditor.setVisible (false);
    okButton.setVisible (false);
    cancelButton.setVisible (false);
    search.setVisible (true);
    for (auto* b : { &saveButton, &saveAsButton, &folderButton })
        b->setVisible (true);

    if (keep && name.isNotEmpty())
    {
        if (presets.saveUser (name, {}) >= 0)
        {
            setBank (PresetManager::userBank);
            if (onHint) onHint ("SAVED: " + name + " (in the User bank)");
            return;
        }
        if (onHint) onHint ("Could not save to " + PresetManager::userFolder().getFullPathName());
    }
    refresh();
}

void PresetBrowser::deleteCurrent()
{
    if (! presets.canOverwrite())
        return;
    // Two presses within three seconds: the first one arms it.
    const auto now = Time::getMillisecondCounterHiRes();
    if (now - deleteArmedAt > 3000.0)
    {
        deleteArmedAt = now;
        deleteButton.setLabel ("Sure?");
        if (onHint) onHint ("DELETE: press again to delete " + presets.currentPreset()->name + ".");
        return;
    }
    const auto name = presets.currentPreset()->name;
    presets.deleteUser (presets.currentIndex());
    refresh();
    if (onHint) onHint ("DELETED: " + name);
}

void PresetBrowser::resized()
{
    for (size_t i = 0; i < bankButtons.size(); ++i)
        bankButtons[i]->setBounds (16, 56 + (int) i * 36, 180, 28);
    deckButton.setBounds (16, 492, 180, 28);
    search.setBounds (212, 12, 380, 30);
    nameEditor.setBounds (search.getBounds());
    saveButton.setBounds (604, 13, 70, 28);
    saveAsButton.setBounds (680, 13, 86, 28);
    deleteButton.setBounds (772, 13, 76, 28);
    folderButton.setBounds (854, 13, 72, 28);
    closeButton.setBounds (932, 13, 56, 28);
    okButton.setBounds (604, 13, 70, 28);
    cancelButton.setBounds (680, 13, 86, 28);
    list.setBounds (listArea.reduced (4));
}

void PresetBrowser::paint (Graphics& g)
{
    background.draw (g, getLocalBounds(), [this] (Image& image, float scale)
    {
        Art::drawBlackPanel (image, getLocalBounds(), scale, 0xb40);
        Graphics gi (image);
        gi.addTransform (AffineTransform::scale (scale));
        Art::drawPanelWear (gi, getLocalBounds().toFloat(), 0xb41);
        drawDymoLabel (gi, { 106.0f, 26.0f }, "Presets", 20.0f, 91);
        if constexpr (! dust::kIsFx)
        {
            gi.setFont (Art::silk (11.0f));
            gi.setColour (Art::Colours::silkDim);
            gi.drawText ("PRESETS ALSO LOAD:", Rectangle<float> (16.0f, 470.0f, 180.0f, 16.0f), Justification::centred, false);
        }
        Art::drawRecess (gi, listArea.toFloat(), 4.0f);
    });
}
} // namespace deck
