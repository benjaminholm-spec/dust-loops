#pragma once

#include <juce_core/juce_core.h>
#include "Identity.h"

// Settings shared by every instance (not saved per project or preset): window size now; favourites
// and MIDI Learn mappings later. Stored as JSON in ~/Music/<product>/settings.json.
// Message thread only.
namespace dust::settings
{
inline juce::File folder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (kProductName);
}

inline juce::File file() { return folder().getChildFile ("settings.json"); }

inline juce::var load()
{
    const auto parsed = juce::JSON::parse (file());
    return parsed.isObject() ? parsed : juce::var (new juce::DynamicObject());
}

inline void set (const juce::Identifier& key, const juce::var& value)
{
    auto all = load();
    if (auto* obj = all.getDynamicObject())
    {
        obj->setProperty (key, value);
        folder().createDirectory();
        file().replaceWithText (juce::JSON::toString (all));
    }
}

inline juce::var get (const juce::Identifier& key, const juce::var& fallback)
{
    const auto all = load();
    return all.hasProperty (key) ? all[key] : fallback;
}
} // namespace dust::settings
