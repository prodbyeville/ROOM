#include "PresetLibrary.h"

namespace room::presets
{

PresetLibrary::PresetLibrary (juce::File rootFolder, std::vector<juce::String> factory)
    : root (std::move (rootFolder)), factoryXml (std::move (factory))
{
    rescan();
}

juce::File PresetLibrary::defaultRoot()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Eville").getChildFile ("ROOMS");
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
        const auto tree = parse (xml);
        if (! tree.isValid())
        {
            ++unreadable;
            continue;
        }
        Entry e;
        e.meta = readMeta (tree);
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
        for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension))
        {
            const auto tree = load (f);
            if (! tree.isValid())
            {
                ++unreadable;   // damaged or not a ROOM preset: skip it, never crash
                continue;
            }
            Entry e;
            e.meta = readMeta (tree);
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

        if (! words.isEmpty())
        {
            const auto haystack = (e.meta.name + " " + e.meta.author + " " + e.meta.type + " "
                                   + e.meta.styles.joinIntoString (" ") + " " + e.meta.notes + " " + e.pack)
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
    return e.factory ? parse (e.factoryXml) : load (e.file);
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
        errorOut = "Give the preset a name.";
        return false;
    }
    if (! isValidPackName (pack))
    {
        errorOut = "Choose a pack you can save into.";
        return false;
    }

    const auto file = packFolder (pack).getChildFile (toFileName (meta.name) + fileExtension);
    if (file.existsAsFile() && ! overwrite)
    {
        errorOut = "A preset called \"" + meta.name + "\" already exists in " + pack + ".";
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
        errorOut = "Factory presets can't be changed. Save a copy instead.";
        return false;
    }
    if (meta.name.trim().isEmpty() || ! isValidPackName (newPack))
    {
        errorOut = "Give the preset a name and a pack.";
        return false;
    }

    auto tree = load (e->file);
    if (! tree.isValid())
    {
        errorOut = "Couldn't read the preset file.";
        return false;
    }

    auto m = meta;
    m.created = e->meta.created;
    writeMeta (tree, m);

    const auto oldFile = e->file;
    const auto newFile = packFolder (newPack).getChildFile (toFileName (m.name) + fileExtension);
    const auto moving = newFile != oldFile;
    if (moving && newFile.existsAsFile())
    {
        errorOut = "A preset called \"" + m.name + "\" already exists in " + newPack + ".";
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
        const auto tree = load (f);
        if (! tree.isValid())
            continue;
        // Never overwrite on import: add " 2", " 3"... if the name is taken.
        auto dest = packFolder (pack).getChildFile (f.getFileName());
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
        const auto target = folder.getChildFile (toFileName (e.meta.name) + fileExtension);
        ok = (e.factory ? presets::save (parse (e.factoryXml), target) : e.file.copyFileTo (target)) && ok;
    }
    return ok;
}

//==============================================================================

void PresetLibrary::loadFavorites()
{
    favoriteKeys.clear();
    if (auto xml = juce::parseXML (favoritesFile()))
        for (auto* e : xml->getChildWithTagNameIterator ("FAVORITE"))
            favoriteKeys.addIfNotAlreadyThere (e->getStringAttribute ("key"));
}

void PresetLibrary::saveFavorites() const
{
    juce::XmlElement xml ("ROOMS_FAVORITES");
    for (auto& k : favoriteKeys)
        xml.createNewChildElement ("FAVORITE")->setAttribute ("key", k);
    root.createDirectory();
    juce::TemporaryFile temp (favoritesFile());
    if (xml.writeTo (temp.getFile()))
        temp.overwriteTargetFileWithTemporary();
}

} // namespace room::presets
