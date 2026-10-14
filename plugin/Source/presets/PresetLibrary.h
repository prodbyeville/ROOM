#pragma once

#include "PresetFile.h"
#include <juce_core/juce_core.h>
#include <vector>

// A library of ROOMS (rooms only) or STUDIOS (full presets). Same layout for both:
//
//   Documents/Eville/ROOMS/              Documents/Eville/STUDIOS/
//       favorites.xml                        favorites.xml    which are hearted (factory too)
//       User/Dark Hall.room                  User/Vox Hall.studio   each sub-folder is a "pack"
//       Eville/...                           Eville/...
//
// Plus the built-in "Factory" pack, which is read-only.
//
// No UI in here, so it can be tested on its own. Use from the message thread.

namespace room::presets
{

class PresetLibrary
{
public:
    static constexpr const char* factoryPack = "Factory";
    static constexpr const char* userPack = "User";

    struct Entry
    {
        juce::String key;          // "Pack/File name" (stable id, used for favorites)
        juce::String pack;
        PresetMeta meta;
        juce::File file;           // empty for factory presets
        juce::String factoryXml;   // factory presets: the preset itself
        bool factory = false;
        bool favorite = false;
        AutoTags tags;             // ROOMS: decay / tone / source, worked out from the room
    };

    enum class SortColumn { name, type, style, author, pack, date, favorite, decay, tone, source, madeFor, room };

    struct Query
    {
        juce::String search;          // matches name, author, type, styles, notes, pack
        juce::String pack;            // "" = all packs
        bool favoritesOnly = false;
        juce::StringArray types;      // any of these (empty = all)
        juce::StringArray styles;     // must have all of these (empty = all)
        juce::StringArray madeFor;    // STUDIOS: any of these
        juce::StringArray autoTags;   // ROOMS: decay / tone / source tags; any within a group, all groups
        SortColumn sortBy = SortColumn::name;
        bool ascending = true;
    };

    /** factory: the built-in files' XML (ROOM files for a room library). */
    PresetLibrary (juce::File rootFolder, std::vector<juce::String> factoryXml, Kind = Kind::studio);

    /** Documents/Eville: ROOMS and STUDIOS live in here. */
    static juce::File defaultBase();
    static juce::File rootFor (const juce::File& base, Kind k) { return base.getChildFile (k == Kind::room ? "ROOMS" : "STUDIOS"); }

    juce::File getRoot() const { return root; }
    Kind getKind() const { return kind; }
    const char* noun() const { return kind == Kind::room ? "ROOM" : "STUDIO"; }

    /** Before STUDIOS existed, full presets were saved as .room files in ROOMS.
        Copies each into STUDIOS (as a .studio) and rewrites the .room as a
        room-only file. Favorites carry over. Safe to run every time. Returns how
        many were converted. */
    static int migrateLegacy (const juce::File& roomsRoot, const juce::File& studiosRoot);

    /** Re-reads the folder. Call after files may have changed outside the plugin. */
    void rescan();

    const std::vector<Entry>& getEntries() const { return entries; }
    const Entry* find (const juce::String& key) const;

    /** Indices into getEntries(), filtered and sorted. */
    std::vector<int> query (const Query&) const;

    /** Packs in display order: Factory, User, then the rest A-Z. */
    juce::StringArray getPacks() const;
    int countInPack (const juce::String& pack) const;
    int countFavorites() const;
    int getUnreadableCount() const { return unreadable; }

    juce::ValueTree loadPreset (const Entry&) const;

    /** Saves a new user preset. Fails (with a message) if the name is taken
        and overwrite is false, or if the pack is the factory pack. */
    bool save (const juce::ValueTree& preset, const juce::String& pack, bool overwrite,
               juce::String& keyOut, juce::String& errorOut);

    /** Change name/tags/notes and/or move it to another pack (user presets only). */
    bool updateMeta (const juce::String& key, const PresetMeta&, const juce::String& newPack,
                     juce::String& newKeyOut, juce::String& errorOut);

    bool remove (const juce::String& key);
    void setFavorite (const juce::String& key, bool);
    bool createPack (const juce::String& name, juce::String& errorOut);

    /** Copies .room files into a pack. Returns how many were imported. */
    int importFiles (const juce::Array<juce::File>&, const juce::String& pack);

    /** Copies a pack (or, for "", all user packs) into destinationFolder. */
    bool exportPack (const juce::String& pack, const juce::File& destinationFolder) const;

    bool nameExists (const juce::String& pack, const juce::String& name) const;
    static bool isValidPackName (const juce::String&);

private:
    juce::File packFolder (const juce::String& pack) const { return root.getChildFile (pack); }
    juce::File favoritesFile() const { return root.getChildFile ("favorites.xml"); }
    void loadFavorites();
    void saveFavorites() const;

    juce::File root;
    Kind kind;
    std::vector<juce::String> factoryXml;
    std::vector<Entry> entries;
    juce::StringArray favoriteKeys;
    int unreadable = 0;
};

} // namespace room::presets
