#include "PresetLibrary.h"

namespace room::presets
{

PresetLibrary::PresetLibrary (juce::File rootFolder, std::vector<juce::String> factory, Kind k)
    : root (std::move (rootFolder)), kind (k), factoryXml (std::move (factory))
{
    rescan();
}

juce::File PresetLibrary::defaultBase()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Eville");
}

static juce::StringArray readFavoriteKeys (const juce::File& f)
{
    juce::StringArray keys;
    if (auto xml = juce::parseXML (f))
        for (auto* e : xml->getChildWithTagNameIterator ("FAVORITE"))
            keys.addIfNotAlreadyThere (e->getStringAttribute ("key"));
    return keys;
}

static void writeFavoriteKeys (const juce::File& f, const juce::StringArray& keys)
{
    juce::XmlElement xml ("ROOMS_FAVORITES");
    for (auto& k : keys)
        xml.createNewChildElement ("FAVORITE")->setAttribute ("key", k);
    f.getParentDirectory().createDirectory();
    juce::TemporaryFile temp (f);
    if (xml.writeTo (temp.getFile()))
        temp.overwriteTargetFileWithTemporary();
}

int PresetLibrary::migrateLegacy (const juce::File& roomsRoot, const juce::File& studiosRoot)
{
    if (! roomsRoot.isDirectory())
        return 0;
    const auto roomFavs = readFavoriteKeys (roomsRoot.getChildFile ("favorites.xml"));
    auto studioFavs = readFavoriteKeys (studiosRoot.getChildFile ("favorites.xml"));
    int converted = 0;

    for (const auto& dir : roomsRoot.findChildFiles (juce::File::findDirectories, false))
    {
        if (dir.isHidden() || dir.getFileName().startsWithChar ('.'))
            continue;
        for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, juce::String ("*") + roomExtension))
        {
            const auto preset = load (f, Kind::studio);   // only old-style full presets match
            if (! preset.isValid())
                continue;
            const auto key = dir.getFileName() + "/" + f.getFileNameWithoutExtension();
            const auto studio = studiosRoot.getChildFile (dir.getFileName())
                                    .getChildFile (f.getFileNameWithoutExtension() + studioExtension);
            if (! studio.existsAsFile() && ! presets::save (preset, studio))
                continue;   // leave the old file alone if the copy failed
            if (roomFavs.contains (key))
                studioFavs.addIfNotAlreadyThere (key);

            const auto room = roomFromPreset (preset, "File");
            if (room.isValid())
                presets::save (room, f);
            else
                f.deleteFile();   // a preset with no room: it lives on as a STUDIO only
            ++converted;
        }
    }
    if (converted > 0)
        writeFavoriteKeys (studiosRoot.getChildFile ("favorites.xml"), studioFavs);
    return converted;
}

bool PresetLibrary::isValidPackName (const juce::String& name)
{
    const auto n = name.trim();
    return n.isNotEmpty() && n.length() <= 60 && n == juce::File::createLegalFileName (n)
           && ! n.equalsIgnoreCase (factoryPack) && ! n.startsWithChar ('.');
}

//==============================================================================

void PresetLibrary::rescan()
{
    entries.clear();
    unreadable = 0;
    root.getChildFile (userPack).createDirectory();
    loadFavorites();

    // Built-in presets
    for (auto& xml : factoryXml)
    {
        const auto tree = parse (xml, kind);
        if (! tree.isValid())
        {
            ++unreadable;
            continue;
        }
        Entry e;
        e.meta = readMeta (tree);
        if (kind == Kind::room)
            e.tags = autoTags (tree.getChildWithName ("ROOM_ANALYSIS"), "Factory");
        e.pack = factoryPack;
        e.key = juce::String (factoryPack) + "/" + toFileName (e.meta.name);
        e.factory = true;
        e.factoryXml = xml;
        entries.push_back (std::move (e));
    }

    // User packs: each sub-folder of the root
    for (const auto& dir : root.findChildFiles (juce::File::findDirectories, false))
    {
        if (dir.isHidden() || dir.getFileName().startsWithChar ('.'))
            continue;
        for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, juce::String ("*") + extensionFor (kind)))
        {
            const auto tree = load (f, kind);
            if (! tree.isValid())
            {
                ++unreadable;   // damaged or not the right kind of file: skip it, never crash
                continue;
            }
            Entry e;
            e.meta = readMeta (tree);
            if (kind == Kind::room)
                e.tags = autoTags (tree.getChildWithName ("ROOM_ANALYSIS"), "File");
            e.pack = dir.getFileName();
            e.key = e.pack + "/" + f.getFileNameWithoutExtension();
            e.file = f;
            entries.push_back (std::move (e));
        }
    }

    for (auto& e : entries)
        e.favorite = favoriteKeys.contains (e.key);
}

const PresetLibrary::Entry* PresetLibrary::find (const juce::String& key) const
{
    for (auto& e : entries)
        if (e.key == key)
            return &e;
    return nullptr;
}

//==============================================================================

std::vector<int> PresetLibrary::query (const Query& q) const
{
    const auto words = juce::StringArray::fromTokens (q.search.toLowerCase(), " ", "\"");

    std::vector<int> out;
    for (int i = 0; i < (int) entries.size(); ++i)
    {
        const auto& e = entries[(size_t) i];
        if (q.pack.isNotEmpty() && e.pack != q.pack)
            continue;
        if (q.favoritesOnly && ! e.favorite)
            continue;
        if (! q.types.isEmpty() && ! q.types.contains (e.meta.type))
            continue;

        bool stylesOk = true;
        for (auto& s : q.styles)
            stylesOk = stylesOk && e.meta.styles.contains (s);
        if (! stylesOk)
            continue;

        if (! q.madeFor.isEmpty())
        {
            bool any = false;
            for (auto& m : q.madeFor)
                any = any || e.meta.madeFor.contains (m);
            if (! any)
                continue;
        }

        // Auto tags: any of the picked ones within a group (decay / tone / source), every group.
        bool autoOk = true;
        for (auto* group : { &allDecayTags(), &allToneTags(), &allSources() })
        {
            juce::StringArray picked;
            for (auto& t : q.autoTags)
                if (group->contains (t))
                    picked.add (t);
            const auto value = group == &allDecayTags() ? e.tags.decay : group == &allToneTags() ? e.tags.tone : e.tags.source;
            autoOk = autoOk && (picked.isEmpty() || picked.contains (value));
        }
        if (! autoOk)
            continue;

        if (! words.isEmpty())
        {
            const auto haystack = (e.meta.name + " " + e.meta.author + " " + e.meta.type + " "
                                   + e.meta.styles.joinIntoString (" ") + " " + e.meta.madeFor.joinIntoString (" ") + " "
                                   + e.meta.roomName + " " + e.tags.decay + " " + e.tags.tone + " " + e.tags.source + " "
                                   + e.meta.notes + " " + e.pack)
                                      .toLowerCase();
            bool all = true;
            for (auto& w : words)
                all = all && (w.isEmpty() || haystack.contains (w));
            if (! all)
                continue;
        }
        out.push_back (i);
    }

    auto cmpText = [] (const juce::String& a, const juce::String& b) { return a.compareNatural (b); };
    std::stable_sort (out.begin(), out.end(), [&] (int ia, int ib)
    {
        const auto& a = entries[(size_t) ia];
        const auto& b = entries[(size_t) ib];
        int c = 0;
        switch (q.sortBy)
        {
            case SortColumn::name:     break;
            case SortColumn::type:     c = cmpText (a.meta.type, b.meta.type); break;
            case SortColumn::style:    c = cmpText (a.meta.styles.joinIntoString (","), b.meta.styles.joinIntoString (",")); break;
            case SortColumn::author:   c = cmpText (a.meta.author, b.meta.author); break;
            case SortColumn::pack:     c = cmpText (a.pack, b.pack); break;
            case SortColumn::date:     c = a.meta.created < b.meta.created ? -1 : (b.meta.created < a.meta.created ? 1 : 0); break;
            case SortColumn::favorite: c = (int) b.favorite - (int) a.favorite; break;   // hearted first
            case SortColumn::decay:    c = a.tags.medianT60 < b.tags.medianT60 ? -1 : (b.tags.medianT60 < a.tags.medianT60 ? 1 : 0); break;
            case SortColumn::tone:     c = allToneTags().indexOf (a.tags.tone) - allToneTags().indexOf (b.tags.tone); break;
            case SortColumn::source:   c = cmpText (a.tags.source, b.tags.source); break;
            case SortColumn::madeFor:  c = cmpText (a.meta.madeFor.joinIntoString (","), b.meta.madeFor.joinIntoString (",")); break;
            case SortColumn::room:     c = cmpText (a.meta.roomName, b.meta.roomName); break;
        }
        if (c == 0)
            c = cmpText (a.meta.name, b.meta.name);   // ties: by name
        if (c == 0)
            c = cmpText (a.pack, b.pack);
        return q.ascending ? c < 0 : c > 0;
    });
    return out;
}

juce::StringArray PresetLibrary::getPacks() const
{
    juce::StringArray others;
    for (const auto& dir : root.findChildFiles (juce::File::findDirectories, false))
        if (! dir.isHidden() && ! dir.getFileName().startsWithChar ('.')
            && dir.getFileName() != userPack && ! dir.getFileName().equalsIgnoreCase (factoryPack))
            others.add (dir.getFileName());
    others.sortNatural();

    juce::StringArray packs;
    if (! factoryXml.empty())
        packs.add (factoryPack);
    packs.add (userPack);
    packs.addArray (others);
    return packs;
}

int PresetLibrary::countInPack (const juce::String& pack) const
{
    return (int) std::count_if (entries.begin(), entries.end(), [&] (auto& e) { return pack.isEmpty() || e.pack == pack; });
}

int PresetLibrary::countFavorites() const
{
    return (int) std::count_if (entries.begin(), entries.end(), [] (auto& e) { return e.favorite; });
}

juce::ValueTree PresetLibrary::loadPreset (const Entry& e) const
{
    return e.factory ? parse (e.factoryXml, kind) : load (e.file, kind);
}

bool PresetLibrary::nameExists (const juce::String& pack, const juce::String& name) const
{
    return find (pack + "/" + toFileName (name)) != nullptr;
}

//==============================================================================
// Changes

bool PresetLibrary::save (const juce::ValueTree& preset, const juce::String& pack, bool overwrite,
                          juce::String& keyOut, juce::String& errorOut)
{
    const auto meta = readMeta (preset);
    if (meta.name.trim().isEmpty())
    {
        errorOut = juce::String ("Give the ") + noun() + " a name.";
        return false;
    }
    if (! isValidPackName (pack))
    {
        errorOut = "Choose a pack you can save into.";
        return false;
    }

    const auto file = packFolder (pack).getChildFile (toFileName (meta.name) + extensionFor (kind));
    if (file.existsAsFile() && ! overwrite)
    {
        errorOut = juce::String ("A ") + noun() + " called \"" + meta.name + "\" already exists in " + pack + ".";
        return false;
    }
    if (! presets::save (preset, file))
    {
        errorOut = "Couldn't write " + file.getFullPathName();
        return false;
    }

    keyOut = pack + "/" + file.getFileNameWithoutExtension();
    rescan();
    return true;
}

bool PresetLibrary::updateMeta (const juce::String& key, const PresetMeta& meta, const juce::String& newPack,
                                juce::String& newKeyOut, juce::String& errorOut)
{
    const auto* e = find (key);
    if (e == nullptr || e->factory)
    {
        errorOut = "Factory ones can't be changed. Save a copy instead.";
        return false;
    }
    if (meta.name.trim().isEmpty() || ! isValidPackName (newPack))
    {
        errorOut = "Give it a name and a pack.";
        return false;
    }

    auto tree = load (e->file, kind);
    if (! tree.isValid())
    {
        errorOut = "Couldn't read the file.";
        return false;
    }

    auto m = meta;
    m.created = e->meta.created;
    writeMeta (tree, m);

    const auto oldFile = e->file;
    const auto newFile = packFolder (newPack).getChildFile (toFileName (m.name) + extensionFor (kind));
    const auto moving = newFile != oldFile;
    if (moving && newFile.existsAsFile())
    {
        errorOut = juce::String ("A ") + noun() + " called \"" + m.name + "\" already exists in " + newPack + ".";
        return false;
    }
    if (! presets::save (tree, newFile))
    {
        errorOut = "Couldn't write " + newFile.getFullPathName();
        return false;
    }
    if (moving)
        oldFile.deleteFile();

    newKeyOut = newPack + "/" + newFile.getFileNameWithoutExtension();
    if (moving && favoriteKeys.contains (key))
    {
        favoriteKeys.removeString (key);
        favoriteKeys.addIfNotAlreadyThere (newKeyOut);
        saveFavorites();
    }
    rescan();
    return true;
}

bool PresetLibrary::remove (const juce::String& key)
{
    const auto* e = find (key);
    if (e == nullptr || e->factory)
        return false;
    const auto ok = e->file.deleteFile();
    if (favoriteKeys.contains (key))
    {
        favoriteKeys.removeString (key);
        saveFavorites();
    }
    rescan();
    return ok;
}

void PresetLibrary::setFavorite (const juce::String& key, bool fav)
{
    if (fav) favoriteKeys.addIfNotAlreadyThere (key);
    else     favoriteKeys.removeString (key);
    saveFavorites();
    for (auto& e : entries)
        if (e.key == key)
            e.favorite = fav;
}

bool PresetLibrary::createPack (const juce::String& name, juce::String& errorOut)
{
    if (! isValidPackName (name))
    {
        errorOut = "That pack name can't be used.";
        return false;
    }
    if (! packFolder (name.trim()).createDirectory())
    {
        errorOut = "Couldn't create the pack folder.";
        return false;
    }
    return true;
}

int PresetLibrary::importFiles (const juce::Array<juce::File>& files, const juce::String& pack)
{
    if (! isValidPackName (pack))
        return 0;
    int count = 0;
    for (auto& f : files)
    {
        auto tree = load (f, kind);
        if (! tree.isValid() && kind == Kind::room)
            tree = roomFromPreset (load (f, Kind::studio), "File");   // an old-style full preset: take its room
        if (! tree.isValid())
            continue;
        // Never overwrite on import: add " 2", " 3"... if the name is taken.
        auto dest = packFolder (pack).getChildFile (f.getFileNameWithoutExtension() + extensionFor (kind));
        if (dest.existsAsFile())
            dest = dest.getNonexistentSibling (true);
        if (presets::save (tree, dest))
            ++count;
    }
    rescan();
    return count;
}

bool PresetLibrary::exportPack (const juce::String& pack, const juce::File& destination) const
{
    bool ok = destination.createDirectory();
    for (auto& e : entries)
    {
        if (pack.isNotEmpty() && e.pack != pack)
            continue;
        const auto folder = destination.getChildFile (e.pack);
        ok = folder.createDirectory() && ok;
        const auto target = folder.getChildFile (toFileName (e.meta.name) + extensionFor (kind));
        ok = (e.factory ? presets::save (parse (e.factoryXml, kind), target) : e.file.copyFileTo (target)) && ok;
    }
    return ok;
}

//==============================================================================

void PresetLibrary::loadFavorites()
{
    favoriteKeys = readFavoriteKeys (favoritesFile());
}

void PresetLibrary::saveFavorites() const
{
    writeFavoriteKeys (favoritesFile(), favoriteKeys);
}

} // namespace room::presets
