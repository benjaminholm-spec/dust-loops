#include "PresetManager.h"
#include "Core/GlobalSettings.h"
#include "Core/Identity.h"
#include "PresetData.h"

namespace
{
const juce::Identifier favouritesKey { "favourites" }, loadDeckKey { "presetsLoadTapeAndPedals" };
const juce::Identifier presetKeyProp { "presetKey" }, presetModifiedProp { "presetModified" };

int bankRank (const juce::String& bank)
{
    const auto i = PresetManager::factoryBanks().indexOf (bank);
    return i < 0 ? 99 : i;
}

juce::var valueFor (const juce::RangedAudioParameter& p)
{
    const auto real = p.convertFrom0to1 (p.getValue());
    if (dynamic_cast<const juce::AudioParameterBool*> (&p) != nullptr)
        return real >= 0.5f;
    if (auto* c = dynamic_cast<const juce::AudioParameterChoice*> (&p))
        return c->choices[c->getIndex()];
    if (dynamic_cast<const juce::AudioParameterInt*> (&p) != nullptr)
        return juce::roundToInt (real);
    return std::round (real * 10000.0f) / 10000.0f;
}

// Normalised value for a stored entry: a number in real units, a bool, or a choice name.
std::optional<float> normalisedFrom (const juce::RangedAudioParameter& p, const juce::var& v)
{
    if (v.isBool())
        return (bool) v ? 1.0f : 0.0f;
    if (v.isString())
    {
        if (auto* c = dynamic_cast<const juce::AudioParameterChoice*> (&p))
        {
            const auto i = c->choices.indexOf (v.toString(), true);
            if (i < 0) return std::nullopt;
            return p.convertTo0to1 ((float) i);
        }
        return std::nullopt;
    }
    if (v.isInt() || v.isInt64() || v.isDouble())
    {
        const auto range = p.getNormalisableRange();
        return p.convertTo0to1 (juce::jlimit (range.start, range.end, (float) (double) v));
    }
    return std::nullopt;
}
} // namespace

const juce::StringArray& PresetManager::factoryBanks()
{
    static const juce::StringArray banks { "Pads", "Keys", "Plucks", "Bells", "Leads", "Bass", "Strings & Choirs",
                                           "Drones", "Textures", "Utility" };
    return banks;
}

bool PresetManager::isStored (const juce::String& id)
{
    // Never stored: transport keys, CLEAR, which loops record (the arms and the REC key's A/B/C/All
    // choice belong to the player, not the preset), FREEZE and the input level.
    return ! (id.startsWith ("xport.") || id.endsWith (".clear") || id.endsWith (".rec") || id == "tape.select" || id == "tape.freeze"
              || id == "pedal.hall.freeze" || id == "global.inputGain");
}

bool PresetManager::isSound (const juce::String& id)
{
    return id.startsWith ("synth.") || id.startsWith ("atmos.");
}

void PresetManager::setLoadsDeck (bool shouldLoad, bool remember)
{
    deckToo.store (dust::kIsFx || shouldLoad); // the FX has no synth: its presets are all deck
    if (remember && ! dust::kIsFx)
        dust::settings::set (loadDeckKey, shouldLoad);
    notify();
}

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state, std::function<juce::String()> getOrderFn,
                              std::function<void (const juce::String&)> setOrderFn)
    : apvts (state), getOrder (std::move (getOrderFn)), setOrder (std::move (setOrderFn))
{
    deckToo.store (dust::kIsFx || (bool) dust::settings::get (loadDeckKey, false));
    loadFactory();
    rescanUser();
    if (auto* arr = dust::settings::get (favouritesKey, {}).getArray())
        for (auto& f : *arr)
            favourites.add (f.toString());

    for (auto* p : apvts.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (isStored (ranged->getParameterID()))
                apvts.addParameterListener (ranged->getParameterID(), this);
}

PresetManager::~PresetManager()
{
    for (auto* p : apvts.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            apvts.removeParameterListener (ranged->getParameterID(), this);
}

void PresetManager::loadFactory()
{
    std::vector<std::pair<juce::String, Preset>> found; // sorted by bank, then file name
    for (int i = 0; i < PresetData::namedResourceListSize; ++i)
    {
        const juce::String file = PresetData::originalFilenames[i];
        if (! file.endsWithIgnoreCase (".json"))
            continue;
        int size = 0;
        const auto* bytes = PresetData::getNamedResource (PresetData::namedResourceList[i], size);
        const auto parsed = juce::JSON::parse (juce::String::fromUTF8 (bytes, size));
        if (! parsed.isObject())
            continue;
        Preset p;
        p.name = parsed["name"].toString();
        p.bank = parsed.hasProperty ("category") ? parsed["category"].toString() : parsed["bank"].toString();
        p.mood = parsed["mood"].toString();
        p.description = parsed["description"].toString();
        p.data = parsed;
        const auto sortKey = juce::String (bankRank (p.bank)).paddedLeft ('0', 2) + file;
        found.emplace_back (sortKey, std::move (p));
    }
    std::sort (found.begin(), found.end(), [] (auto& a, auto& b) { return a.first < b.first; });
    for (auto& [k, p] : found)
        list.push_back (std::move (p));
    factoryCount = (int) list.size();
}

juce::File PresetManager::userFolder()
{
    return dust::settings::folder().getChildFile ("Presets");
}

void PresetManager::rescanUser()
{
    const auto currentKey = currentPreset() != nullptr ? currentPreset()->key() : juce::String();
    list.resize ((size_t) factoryCount);
    auto files = userFolder().findChildFiles (juce::File::findFiles, true, "*.json");
    files.sort();
    for (auto& f : files)
    {
        const auto parsed = juce::JSON::parse (f);
        if (! parsed.isObject())
            continue;
        Preset p;
        p.name = parsed["name"].toString().isNotEmpty() ? parsed["name"].toString() : f.getFileNameWithoutExtension();
        p.bank = userBank;
        p.description = parsed["description"].toString();
        p.data = parsed;
        p.file = f;
        p.isFactory = false;
        list.push_back (std::move (p));
    }
    current = -1;
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].key() == currentKey)
            current = (int) i;
    notify();
}

const PresetManager::Preset* PresetManager::currentPreset() const noexcept
{
    return current >= 0 && current < (int) list.size() ? &list[(size_t) current] : nullptr;
}

void PresetManager::apply (const juce::var& data)
{
    const auto params = data["params"];
    applying = true;
    for (auto* p : apvts.processor.getParameters())
    {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (ranged == nullptr || ! isStored (ranged->getParameterID()))
            continue;
        if (! deckToo.load() && ! isSound (ranged->getParameterID()))
            continue;
        const auto stored = params[juce::Identifier (ranged->getParameterID())];
        const auto value = stored.isVoid() ? std::optional<float> (ranged->getDefaultValue()) : normalisedFrom (*ranged, stored);
        if (value && std::abs (*value - ranged->getValue()) > 1.0e-6f)
        {
            ranged->beginChangeGesture();
            ranged->setValueNotifyingHost (*value);
            ranged->endChangeGesture();
        }
    }
    if (setOrder && deckToo.load())
        setOrder (data["pedalOrder"].toString()); // empty = default order
    applying = false;
}

void PresetManager::load (int index)
{
    if (index < 0 || index >= (int) list.size())
        return;
    apply (list[(size_t) index].data);
    current = index;
    modified.store (false);
    notify();
}

void PresetManager::step (int delta, const std::vector<int>& order)
{
    if (order.empty())
        return;
    auto pos = (int) (std::find (order.begin(), order.end(), current) - order.begin());
    if (pos >= (int) order.size())
        pos = delta > 0 ? -1 : 0;
    const auto n = (int) order.size();
    load (order[(size_t) (((pos + delta) % n + n) % n)]);
}

juce::var PresetManager::capture (const juce::String& name, const juce::String& bank, const juce::String& description) const
{
    auto* params = new juce::DynamicObject();
    for (auto* p : apvts.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (isStored (ranged->getParameterID()))
                params->setProperty (ranged->getParameterID(), valueFor (*ranged));

    auto* root = new juce::DynamicObject();
    root->setProperty ("name", name);
    root->setProperty ("category", bank);
    root->setProperty ("description", description);
    root->setProperty ("pedalOrder", getOrder ? getOrder() : juce::String());
    root->setProperty ("params", juce::var (params));
    return juce::var (root);
}

int PresetManager::saveUser (const juce::String& rawName, const juce::String& description)
{
    const auto name = rawName.trim().isNotEmpty() ? rawName.trim() : juce::String ("Untitled");
    const auto file = userFolder().getChildFile (juce::File::createLegalFileName (name) + ".json");
    userFolder().createDirectory();
    if (! file.replaceWithText (juce::JSON::toString (capture (name, userBank, description))))
        return -1;

    rescanUser();
    for (size_t i = 0; i < list.size(); ++i)
        if (! list[i].isFactory && list[i].file == file)
            current = (int) i;
    modified.store (false);
    notify();
    return current;
}

bool PresetManager::canOverwrite() const
{
    const auto* p = currentPreset();
    return p != nullptr && ! p->isFactory;
}

void PresetManager::deleteUser (int index)
{
    if (index < factoryCount || index >= (int) list.size())
        return;
    list[(size_t) index].file.deleteFile();
    if (index == current)
        current = -1;
    rescanUser();
}

bool PresetManager::isFavourite (int index) const
{
    return index >= 0 && index < (int) list.size() && favourites.contains (list[(size_t) index].key());
}

void PresetManager::toggleFavourite (int index)
{
    if (index < 0 || index >= (int) list.size())
        return;
    const auto key = list[(size_t) index].key();
    if (favourites.contains (key))
        favourites.removeString (key);
    else
        favourites.add (key);
    juce::Array<juce::var> arr;
    for (auto& f : favourites)
        arr.add (f);
    dust::settings::set (favouritesKey, arr);
    notify();
}

std::vector<int> PresetManager::filter (const juce::String& bank, const juce::String& search) const
{
    std::vector<int> result;
    const auto words = juce::StringArray::fromTokens (search.trim(), " ", "");
    for (size_t i = 0; i < list.size(); ++i)
    {
        const auto& p = list[i];
        if (bank == "Favourites" ? ! isFavourite ((int) i) : (bank.isNotEmpty() && p.bank != bank))
            continue;
        const auto haystack = p.name + " " + p.bank + " " + p.mood + " " + p.description;
        bool match = true;
        for (auto& w : words)
            match = match && haystack.containsIgnoreCase (w);
        if (match)
            result.push_back ((int) i);
    }
    return result;
}

void PresetManager::writeState (juce::ValueTree& state) const
{
    const auto* p = currentPreset();
    state.setProperty (presetKeyProp, p != nullptr ? p->key() : juce::String(), nullptr);
    state.setProperty (presetModifiedProp, isModified(), nullptr);
}

void PresetManager::readState (const juce::ValueTree& state)
{
    const auto key = state.getProperty (presetKeyProp).toString();
    current = -1;
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].key() == key)
            current = (int) i;
    modified.store ((bool) state.getProperty (presetModifiedProp, current < 0));
    notify();
}

void PresetManager::parameterChanged (const juce::String& id, float)
{
    // Only what a load would restore counts as editing the preset.
    if (! applying && (deckToo.load() || isSound (id)))
        modified.store (true);
}
