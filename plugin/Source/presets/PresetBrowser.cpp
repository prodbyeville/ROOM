#include "PresetBrowser.h"
#include "../UiColours.h"
#include "../Screens.h"
#include "../EvilleLookAndFeel.h"

namespace room::presets
{

namespace
{
    enum Column { colName = 1, colType, colStyle, colPack, colFav, colDecay, colTone, colSource, colMadeFor, colRoom };

    juce::StringArray allAutoTags()
    {
        juce::StringArray a;
        a.addArray (allDecayTags());
        a.addArray (allToneTags());
        a.addArray (allSources());
        return a;
    }

    const juce::Colour rowAlt { 0xff1a1717 };
    const juce::Colour selectedRow { 0xff2a2424 };

    void drawHeart (juce::Graphics& g, juce::Rectangle<float> r, bool filled, juce::Colour c)
    {
        const auto s = std::min (r.getWidth(), r.getHeight());
        r = r.withSizeKeepingCentre (s, s * 0.9f);
        juce::Path p;
        const auto x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
        p.startNewSubPath (x + w * 0.5f, y + h);
        p.cubicTo (x - w * 0.05f, y + h * 0.55f, x + w * 0.05f, y - h * 0.05f, x + w * 0.5f, y + h * 0.25f);
        p.cubicTo (x + w * 0.95f, y - h * 0.05f, x + w * 1.05f, y + h * 0.55f, x + w * 0.5f, y + h);
        p.closeSubPath();
        g.setColour (c);
        if (filled) g.fillPath (p);
        else        g.strokePath (p, juce::PathStrokeType (1.3f));
    }

    juce::Font uiFont (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    }

    void styleTextEditor (juce::TextEditor& t, const juce::String& hint)
    {
        t.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff1a1717));
        t.setColour (juce::TextEditor::outlineColourId, Colours::chipEdge);
        t.setColour (juce::TextEditor::focusedOutlineColourId, Colours::crimson.brighter (0.15f));
        t.setColour (juce::TextEditor::textColourId, Colours::text);
        t.setColour (juce::CaretComponent::caretColourId, Colours::text);
        t.setTextToShowWhenEmpty (hint, Colours::dim);
        t.setFont (uiFont (14.0f));
        t.setIndents (8, 6);
    }

    void styleButton (juce::TextButton& b, bool primary = false)
    {
        b.setColour (juce::TextButton::buttonColourId, primary ? Colours::accent : Colours::chip);
        b.setColour (juce::TextButton::textColourOffId, Colours::text);
        b.setColour (juce::ComboBox::outlineColourId, Colours::chipEdge);
    }

    void styleLabel (juce::Label& l, const juce::String& text, float size = 11.0f, bool bold = true)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (uiFont (size, bold));
        l.setColour (juce::Label::textColourId, Colours::dim);
    }
}

//==============================================================================
// Chips

void Chip::paintButton (juce::Graphics& g, bool over, bool)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const auto on = getToggleState();
    g.setColour (on ? Colours::accent.withAlpha (0.18f) : (over ? Colours::panel.brighter (0.08f) : Colours::panel));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? Colours::accent : Colours::outline);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    g.setColour (on ? Colours::text : Colours::dim.brighter (0.2f));
    g.setFont (uiFont (12.0f, on));
    g.drawText (getButtonText(), getLocalBounds(), juce::Justification::centred);
}

int Chip::idealWidth() const
{
    return juce::GlyphArrangement::getStringWidthInt (uiFont (12.0f, true), getButtonText()) + 18;
}

ChipGroup::ChipGroup (const juce::StringArray& labels, bool singleSelect) : single (singleSelect)
{
    for (auto& l : labels)
    {
        auto* c = chips.add (new Chip (l));
        c->onClick = [this, c]
        {
            if (single && c->getToggleState())
                for (auto* other : chips)
                    if (other != c)
                        other->setToggleState (false, juce::dontSendNotification);
            if (onChange)
                onChange();
        };
        addAndMakeVisible (c);
    }
}

juce::StringArray ChipGroup::getSelected() const
{
    juce::StringArray s;
    for (auto* c : chips)
        if (c->getToggleState())
            s.add (c->getButtonText());
    return s;
}

void ChipGroup::setSelected (const juce::StringArray& sel)
{
    for (auto* c : chips)
        c->setToggleState (sel.contains (c->getButtonText()), juce::dontSendNotification);
}

int ChipGroup::heightForWidth (int width) const
{
    int x = 0, y = 0;
    for (auto* c : chips)
    {
        const auto w = c->idealWidth();
        if (x > 0 && x + w > width) { x = 0; y += 28; }
        x += w + 6;
    }
    return y + 24;
}

void ChipGroup::resized()
{
    int x = 0, y = 0;
    for (auto* c : chips)
    {
        const auto w = c->idealWidth();
        if (x > 0 && x + w > getWidth()) { x = 0; y += 28; }
        c->setBounds (x, y, w, 24);
        x += w + 6;
    }
}

//==============================================================================
// Save dialog

SaveDialog::SaveDialog (Kind k)
    : kind (k),
      types (k == Kind::room ? allTypes() : allMadeFor(), k == Kind::room),
      styles (allStyles(), false)
{
    setWantsKeyboardFocus (true);
    styleLabel (title, "Save", 16.0f);
    title.setColour (juce::Label::textColourId, Colours::text);
    styleLabel (nameLabel, "NAME");
    styleLabel (typeLabel, kind == Kind::room ? "TYPE" : "MADE FOR  (pick any)");
    styleLabel (styleTagLabel, kind == Kind::room ? "CHARACTER  (pick any)" : "STYLE  (pick any)");
    styleLabel (autoLabel, {}, 12.0f, false);
    styleLabel (packLabel, "PACK");
    styleLabel (notesLabel, "NOTES");
    styleLabel (error, {}, 12.0f, false);
    error.setColour (juce::Label::textColourId, Colours::reduction);

    styleTextEditor (name, kind == Kind::room ? "Name this room" : "Name this studio");
    styleTextEditor (notes, "Optional");
    styleTextEditor (newPackName, "New pack name");
    name.onReturnKey = [this] { submit(); };
    name.onTextChange = [this] { overwriteArmed = false; error.setText ({}, juce::dontSendNotification); };

    pack.setColour (juce::ComboBox::backgroundColourId, Colours::background);
    pack.setColour (juce::ComboBox::outlineColourId, Colours::outline);
    pack.setColour (juce::ComboBox::textColourId, Colours::text);
    pack.onChange = [this]
    {
        newPackName.setVisible (pack.getSelectedId() == 1);
        overwriteArmed = false;
    };

    styleButton (saveButton, true);
    styleButton (cancelButton);
    saveButton.onClick = [this] { submit(); };
    cancelButton.onClick = [this] { setVisible (false); };

    for (juce::Component* c : { (juce::Component*) &title, (juce::Component*) &nameLabel, (juce::Component*) &typeLabel,
                                (juce::Component*) &styleTagLabel, (juce::Component*) &packLabel, (juce::Component*) &notesLabel,
                                (juce::Component*) &error, (juce::Component*) &autoLabel, (juce::Component*) &name, (juce::Component*) &notes,
                                (juce::Component*) &types, (juce::Component*) &styles, (juce::Component*) &pack,
                                (juce::Component*) &newPackName, (juce::Component*) &saveButton, (juce::Component*) &cancelButton })
        addAndMakeVisible (c);
}

void SaveDialog::open (Mode m, const PresetMeta& meta, const juce::StringArray& packs, const juce::String& currentPack,
                       const juce::String& autoInfo)
{
    mode = m;
    original = meta;
    overwriteArmed = false;
    const juce::String noun = kind == Kind::room ? "ROOM" : "STUDIO";
    title.setText ((m == Mode::save ? "Save " : "Edit ") + noun
                       + (kind == Kind::room ? "   (the room only: decay, tone, resonances)" : "   (room + every knob + LFOs)"),
                   juce::dontSendNotification);
    autoLabel.setText (autoInfo, juce::dontSendNotification);
    autoLabel.setVisible (autoInfo.isNotEmpty());
    saveButton.setButtonText (m == Mode::save ? "Save" : "Apply");
    name.setText (meta.name, juce::dontSendNotification);
    notes.setText (meta.notes, juce::dontSendNotification);
    types.setSelected (kind == Kind::room ? juce::StringArray (meta.type) : meta.madeFor);
    styles.setSelected (meta.styles);
    error.setText ({}, juce::dontSendNotification);

    pack.clear (juce::dontSendNotification);
    pack.addItem ("+ New pack...", 1);
    int id = 2, select = 0;
    for (auto& p : packs)
    {
        if (p == PresetLibrary::factoryPack)
            continue;
        pack.addItem (p, id);
        if (p == currentPack || (select == 0 && p == PresetLibrary::userPack))
            select = id;
        ++id;
    }
    pack.setSelectedId (select > 0 ? select : 2, juce::dontSendNotification);
    newPackName.clear();
    newPackName.setVisible (false);

    setVisible (true);
    toFront (true);
    name.grabKeyboardFocus();
    name.selectAll();
    resized();
}

void SaveDialog::submit()
{
    PresetMeta m = original;
    m.name = name.getText().trim();
    m.notes = notes.getText().trim();
    const auto t = types.getSelected();
    if (kind == Kind::room)
        m.type = t.isEmpty() ? juce::String() : t[0];
    else
        m.madeFor = t;
    m.styles = styles.getSelected();

    const auto targetPack = pack.getSelectedId() == 1 ? newPackName.getText().trim() : pack.getText();

    if (onSubmit == nullptr)
        return;
    const auto err = onSubmit (mode, m, targetPack, overwriteArmed);
    if (err.isEmpty())
    {
        setVisible (false);
        return;
    }
    // A name clash: a second click on Save overwrites.
    if (err.contains ("already exists") && mode == Mode::save)
    {
        overwriteArmed = true;
        error.setText (err + " Click Save again to replace it.", juce::dontSendNotification);
    }
    else
        error.setText (err, juce::dontSendNotification);
}

juce::Rectangle<int> SaveDialog::card() const
{
    return getLocalBounds().withSizeKeepingCentre (std::min (520, getWidth() - 40), std::min (480, getHeight() - 40));
}

void SaveDialog::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f));
    const auto c = card().toFloat();
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (c, 10.0f);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (c, 10.0f, 1.0f);
}

void SaveDialog::resized()
{
    auto r = card().reduced (22, 18);
    title.setBounds (r.removeFromTop (26));
    r.removeFromTop (8);
    nameLabel.setBounds (r.removeFromTop (16));
    name.setBounds (r.removeFromTop (32));
    r.removeFromTop (10);
    if (autoLabel.isVisible())
    {
        autoLabel.setBounds (r.removeFromTop (18));
        r.removeFromTop (6);
    }
    typeLabel.setBounds (r.removeFromTop (16));
    types.setBounds (r.removeFromTop (types.heightForWidth (r.getWidth())));
    r.removeFromTop (10);
    styleTagLabel.setBounds (r.removeFromTop (16));
    styles.setBounds (r.removeFromTop (styles.heightForWidth (r.getWidth())));
    r.removeFromTop (10);
    auto packRow = r.removeFromTop (16 + 30);
    auto packCol = packRow.removeFromLeft (packRow.getWidth() / 2 - 6);
    packLabel.setBounds (packCol.removeFromTop (16));
    pack.setBounds (packCol);
    packRow.removeFromLeft (12);
    packRow.removeFromTop (16);
    newPackName.setBounds (packRow);
    r.removeFromTop (10);
    notesLabel.setBounds (r.removeFromTop (16));
    notes.setBounds (r.removeFromTop (30));
    auto buttons = r.removeFromBottom (34);
    error.setBounds (r.removeFromTop (std::max (0, r.getHeight())));
    saveButton.setBounds (buttons.removeFromRight (110));
    buttons.removeFromRight (8);
    cancelButton.setBounds (buttons.removeFromRight (90));
}

void SaveDialog::mouseDown (const juce::MouseEvent& e)
{
    if (! card().contains (e.getPosition()))
        setVisible (false);
}

bool SaveDialog::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey)
    {
        setVisible (false);
        return true;
    }
    return false;
}

//==============================================================================
// Browser

PresetBrowser::PresetBrowser (RoomAudioProcessor& p, Kind k)
    : proc (p), kind (k),
      typeChips (k == Kind::room ? allTypes() : allMadeFor(), false),
      styleChips (allStyles(), false),
      autoChips (allAutoTags(), false)
{
    setWantsKeyboardFocus (true);

    styleTextEditor (search, kind == Kind::room ? "Search names, types, character, decay, tone..."
                                                 : "Search names, made for, styles, rooms...");
    search.onTextChange = [this] { query.search = search.getText(); updateRows(); };
    search.onEscapeKey = [this] { if (onClose) onClose(); };
    addAndMakeVisible (search);

    styleLabel (countLabel, {}, 12.0f, false);
    countLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (countLabel);

    styleButton (closeButton);
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);
    styleButton (soundButton);
    soundButton.setTooltip ("analyze any sound file and make it the room");
    soundButton.onClick = [this] { if (onOpenSoundFile) onOpenSoundFile(); };
    if (kind == Kind::room)
        addAndMakeVisible (soundButton);

    // Sidebar
    styleLabel (packsLabel, "PACKS");
    styleLabel (typesLabel, kind == Kind::room ? "TYPE" : "MADE FOR");
    styleLabel (stylesLabel, kind == Kind::room ? "CHARACTER" : "STYLE");
    styleLabel (autoTagsLabel, juce::String::fromUTF8 ("DECAY  \xc2\xb7  TONE  \xc2\xb7  SOURCE"));
    for (auto* b : { &newPackButton, &importButton, &exportButton, &clearFilters })
    {
        styleButton (*b);
        b->setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        b->setColour (juce::TextButton::textColourOffId, Colours::dim);
        addAndMakeVisible (b);
    }
    newPackButton.onClick = [this] { newPack(); };
    importButton.onClick = [this] { importPresets(); };
    exportButton.onClick = [this] { exportPack(); };
    clearFilters.onClick = [this]
    {
        typeChips.setSelected ({});
        styleChips.setSelected ({});
        autoChips.setSelected ({});
        query.types.clear();
        query.madeFor.clear();
        query.styles.clear();
        query.autoTags.clear();
        updateRows();
    };
    typeChips.onChange = [this]
    {
        if (kind == Kind::room) query.types = typeChips.getSelected();
        else                    query.madeFor = typeChips.getSelected();
        updateRows();
    };
    styleChips.onChange = [this] { query.styles = styleChips.getSelected(); updateRows(); };
    autoChips.onChange = [this] { query.autoTags = autoChips.getSelected(); updateRows(); };
    for (auto* c : { (juce::Component*) &packsLabel, (juce::Component*) &typesLabel, (juce::Component*) &stylesLabel,
                     (juce::Component*) &typeChips, (juce::Component*) &styleChips })
        addAndMakeVisible (c);
    if (kind == Kind::room)
    {
        addAndMakeVisible (autoTagsLabel);
        addAndMakeVisible (autoChips);
    }

    // WARP's layout: filters live behind a chip next to search, the details in a
    // right-click menu; the old sidebar chips and detail buttons only keep state.
    for (auto* c : std::initializer_list<juce::Component*> { &packsLabel, &typesLabel, &stylesLabel, &autoTagsLabel,
                                                             &typeChips, &styleChips, &autoChips, &clearFilters, &countLabel })
        c->setVisible (false);
    for (auto* b : { &filtersButton, &folderButton, &importButton, &exportButton, &soundButton })
        b->getProperties().set ("key", "dark");
    pathButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    pathButton.setColour (juce::TextButton::textColourOffId, Colours::textDim);
    pathButton.setTooltip ("open this folder");
    pathButton.onClick = [this] { lib().getRoot().startAsProcess(); };
    folderButton.setTooltip ("open this pack's folder");
    folderButton.onClick = [this]
    {
        const auto& pr = packRows[(size_t) juce::jlimit (0, (int) packRows.size() - 1, selectedPackRow)];
        (pr.pack.isNotEmpty() ? lib().getRoot().getChildFile (pr.pack) : lib().getRoot()).startAsProcess();
    };
    filtersButton.setTooltip ("filter by tags");
    filtersButton.onClick = [this] { showFiltersMenu(); };
    closeButton.setButtonText ({});
    closeButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    closeButton.setTooltip ("close");
    newPackButton.setButtonText ("+ new pack");
    newPackButton.setColour (juce::TextButton::textColourOffId, Colours::redLight);
    importButton.setButtonText ("import...");
    exportButton.setButtonText ("export...");
    for (auto* b : { &filtersButton, &folderButton, &pathButton })
        addAndMakeVisible (b);

    // Table
    table.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    table.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    table.setRowHeight (38);
    auto& h = table.getHeader();
    const auto colFlags = juce::TableHeaderComponent::defaultFlags;
    if (kind == Kind::room)
    {
        h.addColumn ("Name", colName, 120, 80, 400, colFlags);
        h.addColumn ("Type", colType, 92, 50, 160, colFlags);
        h.addColumn ("Character", colStyle, 120, 60, 260, colFlags);
        h.addColumn ("Decay", colDecay, 58, 44, 100, colFlags);
        h.addColumn ("Tone", colTone, 62, 44, 100, colFlags);
    }
    else
    {
        h.addColumn ("Name", colName, 140, 80, 400, colFlags);
        h.addColumn ("Made for", colMadeFor, 90, 60, 200, colFlags);
        h.addColumn ("Style", colStyle, 100, 60, 260, colFlags);
        h.addColumn ("Room", colRoom, 90, 60, 200, colFlags);
        h.addColumn ("Pack", colPack, 60, 44, 160, colFlags);
    }
    h.addColumn ({}, colFav, 30, 30, 30, juce::TableHeaderComponent::notResizableOrSortable | juce::TableHeaderComponent::sortable);
    h.setStretchToFitActive (true);
    h.setSortColumnId (colName, true);
    h.setColour (juce::TableHeaderComponent::backgroundColourId, juce::Colour (0xff0e0c0c));
    h.setColour (juce::TableHeaderComponent::textColourId, Colours::textDim);
    h.setColour (juce::TableHeaderComponent::outlineColourId, juce::Colours::transparentBlack);
    h.setColour (juce::TableHeaderComponent::highlightColourId, Colours::chip);
    table.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, Colours::outline);
    addAndMakeVisible (table);

    // Details
    styleButton (loadButton, true);
    for (auto* b : { &favButton, &editButton, &deleteButton })
        styleButton (*b);
    deleteButton.setColour (juce::TextButton::textColourOffId, Colours::reduction);
    loadButton.onClick = [this] { if (auto* e = selectedEntry()) if (onLoad) onLoad (e->key); };
    favButton.onClick = [this]
    {
        if (auto* e = selectedEntry())
        {
            lib().setFavorite (e->key, ! e->favorite);
            updateRows();
        }
    };
    editButton.onClick = [this] { if (auto* e = selectedEntry()) if (onEdit) onEdit (e->key, e->meta, e->pack); };
    deleteButton.onClick = [this] { deleteSelected(); };
    for (auto* b : { &loadButton, &favButton, &editButton, &deleteButton })
        addChildComponent (b);   // their actions are in the row's right-click menu

    refresh();
}

PresetLibrary& PresetBrowser::lib() const
{
    return kind == Kind::room ? proc.getRoomLibrary() : proc.getLibrary();
}

juce::String PresetBrowser::currentKey() const
{
    return kind == Kind::room ? proc.getCurrentRoomKey() : proc.getCurrentPresetKey();
}

void PresetBrowser::visibilityChanged()
{
    if (isVisible())
    {
        refresh();
        search.grabKeyboardFocus();
    }
}

void PresetBrowser::refresh()
{
    auto& lib = this->lib();
    lib.rescan();

    const auto previousPack = selectedPackRow < (int) packRows.size() ? packRows[(size_t) selectedPackRow] : PackRow {};
    packRows.clear();
    packRows.push_back ({ "All", {}, false, lib.countInPack ({}) });
    packRows.push_back ({ "Favorites", {}, true, lib.countFavorites() });
    for (auto& p : lib.getPacks())
        packRows.push_back ({ p, p, false, lib.countInPack (p) });

    selectedPackRow = 0;
    for (int i = 0; i < (int) packRows.size(); ++i)
        if (packRows[(size_t) i].label == previousPack.label && packRows[(size_t) i].favorites == previousPack.favorites)
            selectedPackRow = i;

    packButtons.clear();
    for (int i = 0; i < (int) packRows.size(); ++i)
    {
        auto* b = packButtons.add (new juce::TextButton());
        b->setButtonText (packRows[(size_t) i].label);
        b->setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        b->setColour (juce::TextButton::buttonOnColourId, Colours::panel);
        b->setColour (juce::TextButton::textColourOffId, Colours::dim);
        b->setColour (juce::TextButton::textColourOnId, Colours::text);
        b->setClickingTogglesState (false);
        b->setToggleState (i == selectedPackRow, juce::dontSendNotification);
        b->setAlpha (0.0f);   // painted by the browser; the button only catches clicks
        b->onClick = [this, i]
        {
            selectedPackRow = i;
            for (int k = 0; k < packButtons.size(); ++k)
                packButtons[k]->setToggleState (k == i, juce::dontSendNotification);
            updateRows();
        };
        addAndMakeVisible (b);
    }

    updateRows();
    resized();
}

void PresetBrowser::updateRows()
{
    auto& lib = this->lib();
    const auto& pr = packRows[(size_t) juce::jlimit (0, (int) packRows.size() - 1, selectedPackRow)];
    query.pack = pr.pack;
    query.favoritesOnly = pr.favorites;
    rows = lib.query (query);

    // Keep the counts in the sidebar current (favorites change them).
    if (packRows.size() > 1)
        packRows[1].count = lib.countFavorites();

    countLabel.setText (juce::String ((int) rows.size()) + " of " + juce::String ((int) lib.getEntries().size()) + (kind == Kind::room ? " ROOMS" : " STUDIOS")
                            + (lib.getUnreadableCount() > 0 ? "  (" + juce::String (lib.getUnreadableCount()) + " unreadable)" : juce::String()),
                        juce::dontSendNotification);
    table.updateContent();

    // Keep the selection on the same preset if it's still in the list.
    int sel = -1;
    for (int i = 0; i < (int) rows.size(); ++i)
        if (lib.getEntries()[(size_t) rows[(size_t) i]].key == selectedKey)
            sel = i;
    if (sel < 0 && selectedKey.isEmpty())
        for (int i = 0; i < (int) rows.size(); ++i)
            if (lib.getEntries()[(size_t) rows[(size_t) i]].key == currentKey())
                sel = i;
    filtersButton.setButtonText (activeFilterCount() > 0 ? "filters  " + juce::String (activeFilterCount()) : juce::String ("filters"));
    filtersButton.setToggleState (activeFilterCount() > 0, juce::dontSendNotification);
    if (sel >= 0)
        table.selectRow (sel, false, true);
    else
        table.deselectAllRows();
    showDetailsFor (selectedEntry());
    repaint();
}

std::vector<int> PresetBrowser::currentList() const
{
    return lib().query (query);
}

const PresetLibrary::Entry* PresetBrowser::selectedEntry() const
{
    return selectedKey.isEmpty() ? nullptr : lib().find (selectedKey);
}

void PresetBrowser::showDetailsFor (const PresetLibrary::Entry* e)
{
    const auto has = e != nullptr;
    loadButton.setEnabled (has);
    favButton.setEnabled (has);
    favButton.setButtonText (has && e->favorite ? "Unfavorite" : "Favorite");
    editButton.setVisible (has && ! e->factory);
    deleteButton.setVisible (has && ! e->factory);
    repaint (detailArea);
}

//==============================================================================
// Table

void PresetBrowser::paintRowBackground (juce::Graphics& g, int row, int w, int h, bool selected)
{
    // WARP's rows: a soft pill per preset, the selected one lighter, the loaded one edged red.
    const auto r = juce::Rectangle<float> ((float) w, (float) h).reduced (0.0f, 2.0f);
    g.setColour (selected ? selectedRow : rowAlt);
    g.fillRect (r);
    if (row >= 0 && row < (int) rows.size()
        && lib().getEntries()[(size_t) rows[(size_t) row]].key == currentKey())
    {
        g.setColour (Colours::crimson.brighter (0.15f));
        g.fillRect (r.withHeight (1.2f));
        g.fillRect (r.withTop (r.getBottom() - 1.2f));
    }
}

void PresetBrowser::paintCell (juce::Graphics& g, int row, int col, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) rows.size())
        return;
    const auto& e = lib().getEntries()[(size_t) rows[(size_t) row]];
    const auto current = e.key == currentKey();
    juce::String text;
    auto colour = Colours::dim.brighter (0.25f);
    switch (col)
    {
        case colName:  text = e.meta.name; colour = current ? Colours::redLight : Colours::text; break;
        case colType:  text = e.meta.type; colour = Colours::textDim; break;
        case colStyle: text = e.meta.styles.joinIntoString (" \xc2\xb7 "); break;
        case colDecay: text = e.tags.decay.isEmpty() ? juce::String() : e.tags.medianT60 <= 0.0f ? juce::String ("steady") : juce::String (e.tags.medianT60, 1) + " s"; break;
        case colTone:  text = e.tags.tone; break;
        case colSource: text = e.tags.source; break;
        case colMadeFor: text = e.meta.madeFor.joinIntoString (" \xc2\xb7 "); colour = Colours::textDim; break;
        case colRoom:  text = e.meta.roomName; break;
        case colPack:  text = e.pack; break;
        case colFav:
            drawHeart (g, juce::Rectangle<float> ((float) w, (float) h).reduced (8.0f, 12.0f), e.favorite,
                       e.favorite ? Colours::crimson.brighter (0.2f) : Colours::dustWhite.withAlpha (0.35f));
            return;
        default: break;
    }
    g.setColour (colour);
    juce::ignoreUnused (selected);
    g.setFont (Fonts::sans (col == colName ? 14.0f : 12.5f, col == colName ? Fonts::bold : Fonts::medium));
    g.drawText (text, 8, 0, w - 12, h, juce::Justification::centredLeft, true);
}

void PresetBrowser::cellClicked (int row, int col, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        table.selectRow (row);
        showRowMenu (row);
        return;
    }
    // WARP: a click loads it and the browser stays open, so you can keep browsing.
    if (col != colFav && row >= 0 && row < (int) rows.size() && onLoad)
    {
        onLoad (lib().getEntries()[(size_t) rows[(size_t) row]].key);
        table.repaint();
        return;
    }
    if (col == colFav && row >= 0 && row < (int) rows.size())
    {
        const auto& e = lib().getEntries()[(size_t) rows[(size_t) row]];
        lib().setFavorite (e.key, ! e.favorite);
        updateRows();
    }
}

void PresetBrowser::cellDoubleClicked (int row, int, const juce::MouseEvent&)
{
    returnKeyPressed (row);
    if (onClose)
        onClose();   // double-click: load it and close
}

void PresetBrowser::returnKeyPressed (int row)
{
    if (row >= 0 && row < (int) rows.size() && onLoad)
        onLoad (lib().getEntries()[(size_t) rows[(size_t) row]].key);
}

void PresetBrowser::selectedRowsChanged (int row)
{
    selectedKey = row >= 0 && row < (int) rows.size()
                      ? lib().getEntries()[(size_t) rows[(size_t) row]].key
                      : juce::String();
    showDetailsFor (selectedEntry());
}

void PresetBrowser::sortOrderChanged (int columnId, bool forwards)
{
    using S = PresetLibrary::SortColumn;
    switch (columnId)
    {
        case colType:  query.sortBy = S::type; break;
        case colStyle: query.sortBy = S::style; break;
        case colPack:  query.sortBy = S::pack; break;
        case colDecay: query.sortBy = S::decay; break;
        case colTone:  query.sortBy = S::tone; break;
        case colSource: query.sortBy = S::source; break;
        case colMadeFor: query.sortBy = S::madeFor; break;
        case colRoom:  query.sortBy = S::room; break;
        case colFav:   query.sortBy = S::favorite; break;
        default:       query.sortBy = S::name; break;
    }
    query.ascending = forwards;
    updateRows();
}

//==============================================================================
// Pack actions

void PresetBrowser::newPack()
{
    EvilleLookAndFeel::showDialog (*this, "new pack",
                                   juce::String ("name the new pack (a folder in ") + (kind == Kind::room ? "ROOMS" : "STUDIOS") + ").",
                                   { "create", "cancel" }, "pack name",
                                   [this] (int result, const juce::String& name)
                                   {
                                       if (result != 1)
                                           return;
                                       juce::String err;
                                       if (lib().createPack (name, err))
                                           refresh();
                                       else
                                           EvilleLookAndFeel::showDialog (*this, "new pack", err.toLowerCase(), { "ok" });
                                   });
}

void PresetBrowser::importPresets()
{
    const auto& pr = packRows[(size_t) selectedPackRow];
    const auto target = PresetLibrary::isValidPackName (pr.pack) ? pr.pack : juce::String (PresetLibrary::userPack);
    chooser = std::make_unique<juce::FileChooser> (juce::String ("Import ") + (kind == Kind::room ? "ROOMS" : "STUDIOS") + " into \"" + target + "\"",
                                                   juce::File(), kind == Kind::room ? "*.room" : "*.studio;*.room");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this, target] (const juce::FileChooser& fc)
                          {
                              const auto files = fc.getResults();
                              if (files.isEmpty())
                                  return;
                              const auto n = lib().importFiles (files, target);
                              refresh();
                              EvilleLookAndFeel::showDialog (*this, "import", "imported " + juce::String (n) + " of "
                                                                 + juce::String (files.size()) + " into " + target + ".", { "ok" });
                          });
}

void PresetBrowser::exportPack()
{
    const auto& pr = packRows[(size_t) selectedPackRow];
    chooser = std::make_unique<juce::FileChooser> ("Choose a folder to export \"" + pr.label + "\" to");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this, pack = pr.pack] (const juce::FileChooser& fc)
                          {
                              const auto dir = fc.getResult();
                              if (dir == juce::File())
                                  return;
                              const auto ok = lib().exportPack (pack, dir);
                              EvilleLookAndFeel::showDialog (*this, "export", ok ? "exported to " + dir.getFullPathName()
                                                                                 : juce::String ("some presets couldn't be exported."), { "ok" });
                          });
}

void PresetBrowser::deleteSelected()
{
    const auto* e = selectedEntry();
    if (e == nullptr || e->factory)
        return;
    const auto key = e->key;
    EvilleLookAndFeel::showDialog (*this, "delete " + juce::String (lib().noun()).toLowerCase(),
                                   "delete \"" + e->meta.name + "\"? this can't be undone.", { "delete", "cancel" }, {},
                                   [this, key] (int result, const juce::String&)
                                  {
                                      if (result != 1)
                                          return;
                                      lib().remove (key);
                                      selectedKey.clear();
                                      refresh();
                                  });
}

//==============================================================================

bool PresetBrowser::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey)
    {
        if (onClose) onClose();
        return true;
    }
    return false;
}

// WARP's browser, in ROOM's colours: a card over the window, the packs as
// textured banners on the left, search and the list on the right, and the
// section, the count and import / export / folder along the bottom.

juce::Rectangle<int> PresetBrowser::cardArea() const
{
    return getLocalBounds().withSizeKeepingCentre (std::min (840, getWidth() - 40), std::min (700, getHeight() - 40));
}
juce::Rectangle<int> PresetBrowser::footerArea() const { return cardArea().reduced (22, 0).removeFromBottom (58); }
juce::Rectangle<int> PresetBrowser::sectionPanel() const
{
    return cardArea().reduced (20, 0).withTrimmedTop (64 + 10).withTrimmedBottom (58).withWidth (250);
}
juce::Rectangle<int> PresetBrowser::listPanel() const
{
    return cardArea().reduced (20, 0).withTrimmedTop (64 + 10).withTrimmedBottom (58).withTrimmedLeft (250 + 12);
}

void PresetBrowser::mouseDown (const juce::MouseEvent& e)
{
    if (! cardArea().contains (e.getPosition()) && onClose)
        onClose();
}

int PresetBrowser::activeFilterCount() const
{
    return query.types.size() + query.madeFor.size() + query.styles.size() + query.autoTags.size();
}

void PresetBrowser::showFiltersMenu()
{
    juce::PopupMenu m;
    auto group = [&] (const juce::String& title, ChipGroup& chips, const juce::StringArray& options)
    {
        m.addSectionHeader (title);
        const auto selected = chips.getSelected();
        for (auto& o : options)
            m.addItem (o.toLowerCase(), true, selected.contains (o), [this, &chips, o]
            {
                auto sel = chips.getSelected();
                if (sel.contains (o)) sel.removeString (o); else sel.add (o);
                chips.setSelected (sel);
                if (chips.onChange) chips.onChange();
                showFiltersMenu();   // stay open so several can be ticked in one go
            });
    };
    group (kind == Kind::room ? "type" : "made for", typeChips, kind == Kind::room ? allTypes() : allMadeFor());
    group (kind == Kind::room ? "character" : "style", styleChips, allStyles());
    if (kind == Kind::room)
    {
        juce::StringArray a;
        a.addArray (allDecayTags()); a.addArray (allToneTags()); a.addArray (allSources());
        group (juce::String::fromUTF8 ("decay \xc2\xb7 tone \xc2\xb7 source"), autoChips, a);
    }
    m.addSeparator();
    m.addItem ("clear filters", activeFilterCount() > 0, false, [this] { clearFilters.onClick(); });
    EvilleLookAndFeel::showMenu (m, filtersButton);
}

void PresetBrowser::showRowMenu (int row)
{
    if (row < 0 || row >= (int) rows.size())
        return;
    const auto& e = lib().getEntries()[(size_t) rows[(size_t) row]];
    const auto key = e.key;
    juce::PopupMenu m;
    m.addSectionHeader (e.meta.name.toLowerCase());
    m.addItem ("load", [this, key] { if (onLoad) onLoad (key); table.repaint(); });
    m.addItem (e.favorite ? "unfavourite" : "favourite", [this, key]
    {
        if (auto* x = lib().find (key)) { lib().setFavorite (key, ! x->favorite); updateRows(); }
    });
    if (! e.factory)
    {
        m.addItem (juce::String::fromUTF8 ("edit\xe2\x80\xa6"), [this, key]
        {
            if (auto* x = lib().find (key)) if (onEdit) onEdit (x->key, x->meta, x->pack);
        });
        m.addItem (juce::String::fromUTF8 ("delete\xe2\x80\xa6"), [this, key] { selectedKey = key; deleteSelected(); });
        if (e.file.existsAsFile())
            m.addItem ("show in folder", [file = e.file] { file.revealToUser(); });
    }
    m.addSeparator();
    juce::String info = e.meta.type.isNotEmpty() ? e.meta.type : e.meta.madeFor.joinIntoString (", ");
    if (kind == Kind::room && e.tags.decay.isNotEmpty())
        info << juce::String::fromUTF8 ("  \xc2\xb7  ") << e.tags.decay << juce::String::fromUTF8 ("  \xc2\xb7  ") << e.tags.tone;
    if (info.isNotEmpty())
        m.addItem (info.toLowerCase(), false, false, nullptr);
    if (e.meta.notes.isNotEmpty())
        m.addItem (e.meta.notes.toLowerCase(), false, false, nullptr);
    m.addItem (e.pack.toLowerCase() + (e.factory ? "  (built in)" : ""), false, false, nullptr);
    EvilleLookAndFeel::showMenu (m, table);
}

void PresetBrowser::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.72f));
    const auto card = cardArea().toFloat();
    g.setColour (juce::Colour (0xff121010));
    g.fillRoundedRectangle (card, 22.0f);
    g.setColour (Colours::crimson.withAlpha (0.85f));
    g.drawRoundedRectangle (card.reduced (0.75f), 22.0f, 1.5f);

    // Header: the title, a hairline under it
    const auto header = cardArea().removeFromTop (64);
    EvilleLookAndFeel::drawCaps (g, kind == Kind::room ? "rooms" : "studios", header.withTrimmedLeft (28).toFloat(), 17.0f, Colours::dustWhite);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.fillRect (card.getX() + 20.0f, (float) header.getBottom(), card.getWidth() - 40.0f, 1.0f);

    // The close X
    {
        const auto c = closeButton.getBounds().toFloat().getCentre();
        g.setColour (Colours::dustWhite.withAlpha (closeButton.isMouseOver() ? 1.0f : 0.8f));
        g.drawLine (c.x - 7.0f, c.y - 7.0f, c.x + 7.0f, c.y + 7.0f, 1.8f);
        g.drawLine (c.x - 7.0f, c.y + 7.0f, c.x + 7.0f, c.y - 7.0f, 1.8f);
    }

    // The panels behind the sections and the list
    for (auto r : { sectionPanel(), listPanel() })
    {
        g.setColour (juce::Colour (0xff0e0c0c));
        g.fillRoundedRectangle (r.toFloat(), 12.0f);
    }

    // The sections as banners: a crop of a texture darkened to the left, caps
    // name, the count and a chevron; the chosen one edged red.
    static const char* textures[] = { "card_plum.jpg", "card_red.jpg", "card_charcoal.jpg", "card_rose.jpg", "card_plaster.jpg" };
    for (int i = 0; i < packButtons.size() && i < (int) packRows.size(); ++i)
    {
        const auto r = packButtons[i]->getBounds().toFloat().reduced (0.0f, 3.0f);
        const auto& pr = packRows[(size_t) i];
        const auto sel = i == selectedPackRow;
        const auto texName = pr.favorites ? textures[1] : pr.pack.isEmpty() ? textures[2]
                           : textures[(size_t) (pr.pack.hashCode() & 0x7fffffff) % 5];
        {
            juce::Graphics::ScopedSaveState save (g);
            juce::Path shape;
            shape.addRoundedRectangle (r, 10.0f);
            g.reduceClipRegion (shape);
            if (auto tex = Hw::asset (texName); tex.isValid())
            {
                const auto seed = (int) (pr.label.hashCode() & 0xffff);
                g.drawImage (tex, r.getX(), r.getY(), r.getWidth(), r.getHeight(),
                             seed % (tex.getWidth() / 2), (seed / 3) % (tex.getHeight() / 2), tex.getWidth() / 2, tex.getHeight() / 3);
            }
            juce::ColourGradient dark (juce::Colours::black.withAlpha (0.78f), r.getX(), 0.0f,
                                       juce::Colours::black.withAlpha (0.15f), r.getRight(), 0.0f, false);
            g.setGradientFill (dark);
            g.fillRect (r);
        }
        if (sel)
        {
            g.setColour (Colours::crimson.brighter (0.15f));
            g.drawRoundedRectangle (r.reduced (0.75f), 10.0f, 1.5f);
        }
        auto tr = r.reduced (16.0f, 0.0f);
        if (pr.favorites)
        {
            drawHeart (g, tr.removeFromLeft (14.0f).reduced (0.0f, (r.getHeight() - 13.0f) * 0.5f), true, Colours::dustWhite);
            tr.removeFromLeft (10.0f);
        }
        if (! pr.favorites && pr.pack.isNotEmpty() && pr.pack.equalsIgnoreCase ("factory"))
            EvilleLookAndFeel::drawCaps (g, "built in", tr.withHeight (14.0f).translated (0.0f, r.getHeight() * 0.18f), 8.5f,
                                         Colours::dustWhite.withAlpha (0.6f));
        EvilleLookAndFeel::drawCaps (g, pr.favorites ? "favourites" : pr.label, tr, 12.0f,
                                     sel ? Colours::redLight.brighter (0.2f) : Colours::dustWhite);
        // count and chevron
        auto right = tr.removeFromRight (52.0f);
        const auto chev = right.removeFromRight (10.0f).getCentre();
        right.removeFromRight (10.0f);
        juce::Path p;
        p.startNewSubPath (chev.x - 3.0f, chev.y - 6.0f);
        p.lineTo (chev.x + 3.0f, chev.y);
        p.lineTo (chev.x - 3.0f, chev.y + 6.0f);
        g.setColour (Colours::dustWhite.withAlpha (0.85f));
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setFont (Fonts::sans (11.5f, Fonts::bold));
        g.drawText (juce::String (pr.count), right, juce::Justification::centredRight);
    }


    // Footer: the section, a hairline, the count
    auto footer = footerArea().toFloat();
    footer.removeFromRight ((float) (footerArea().getRight() - importButton.getX()) + 14.0f);
    g.setColour (Colours::crimson.brighter (0.15f));
    g.fillRoundedRectangle (footer.removeFromLeft (10.0f).withSizeKeepingCentre (8.0f, 8.0f), 2.0f);
    footer.removeFromLeft (10.0f);
    const auto& pr = packRows[(size_t) juce::jlimit (0, (int) packRows.size() - 1, selectedPackRow)];
    const auto section = (pr.favorites ? juce::String ("favourites") : pr.label).toUpperCase();
    const auto sectionWidth = std::min (footer.getWidth() * 0.4f,
                                        juce::GlyphArrangement::getStringWidth (Fonts::sans (11.5f, Fonts::bold).withExtraKerningFactor (0.18f), section) + 10.0f);
    EvilleLookAndFeel::drawCaps (g, section, footer.removeFromLeft (sectionWidth), 11.5f, Colours::dustWhite);
    juce::String count = juce::String ((int) rows.size()) + " " + (kind == Kind::room ? "rooms" : "studios");
    if (lib().getUnreadableCount() > 0)
        count << "  (" << lib().getUnreadableCount() << " unreadable)";
    const auto countWidth = juce::GlyphArrangement::getStringWidth (Fonts::sans (11.0f, Fonts::bold).withExtraKerningFactor (0.18f), count.toUpperCase()) + 24.0f;
    EvilleLookAndFeel::drawCaps (g, count, footer.removeFromRight (countWidth), 11.0f, Colours::textDim, juce::Justification::centredRight);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.fillRect (footer.reduced (12.0f, 0.0f).withSizeKeepingCentre (std::max (0.0f, footer.getWidth() - 24.0f), 1.0f));

    if (rows.empty())
    {
        g.setColour (Colours::textDim);
        g.setFont (Fonts::sans (14.0f));
        g.drawFittedText (search.isEmpty() && activeFilterCount() == 0
                              ? (pr.favorites ? juce::String ("heart a ") + juce::String (lib().noun()).toLowerCase() + " to keep it here."
                                              : juce::String ("nothing here yet."))
                              : juce::String ("nothing matches."),
                          table.getBounds().reduced (30), juce::Justification::centred, 3);
    }
}

void PresetBrowser::paintOverChildren (juce::Graphics& g)
{
    // Search: the magnifier inside the field
    {
        const auto mb = search.getBounds().toFloat().removeFromLeft (34.0f).withSizeKeepingCentre (14.0f, 14.0f);
        g.setColour (Colours::textDim);
        g.drawEllipse (mb.withTrimmedRight (3.0f).withTrimmedBottom (3.0f), 1.6f);
        g.drawLine (mb.getRight() - 4.0f, mb.getBottom() - 4.0f, mb.getRight(), mb.getBottom(), 1.8f);
    }

    // The folder glyph on the folder key
    const auto b = folderButton.getBounds().toFloat().withSizeKeepingCentre (18.0f, 14.0f);
    juce::Path p;
    p.startNewSubPath (b.getX(), b.getBottom());
    p.lineTo (b.getX(), b.getY());
    p.lineTo (b.getX() + b.getWidth() * 0.4f, b.getY());
    p.lineTo (b.getX() + b.getWidth() * 0.5f, b.getY() + 3.0f);
    p.lineTo (b.getRight(), b.getY() + 3.0f);
    p.lineTo (b.getRight(), b.getBottom());
    p.closeSubPath();
    g.setColour (Colours::dustWhite.withAlpha (folderButton.isMouseOver() ? 1.0f : 0.85f));
    g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void PresetBrowser::resized()
{
    auto header = cardArea().removeFromTop (64).reduced (18, 14);
    closeButton.setBounds (header.removeFromRight (36));
    header.removeFromRight (10);
    if (kind == Kind::room)
    {
        soundButton.setBounds (header.removeFromRight (180));
        header.removeFromRight (10);
    }
    auto path = lib().getRoot().getFullPathName();
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();
    if (path.startsWith (home))
        path = "~" + path.substring (home.length());
    pathButton.setButtonText (path);
    pathButton.setBounds (header.removeFromRight (std::min (300, header.getWidth() - 140)));

    auto left = sectionPanel().reduced (8);
    newPackButton.setBounds (left.removeFromBottom (32));
    left.removeFromBottom (4);
    layoutSidebar (left);

    auto right = listPanel().reduced (8);
    auto top = right.removeFromTop (40);
    filtersButton.setBounds (top.removeFromRight (110));
    top.removeFromRight (8);
    search.setBounds (top);
    search.setIndents (34, 12);
    right.removeFromTop (8);
    table.setBounds (right);

    auto footer = footerArea().reduced (0, 10);
    folderButton.setBounds (footer.removeFromRight (38));
    footer.removeFromRight (8);
    exportButton.setBounds (footer.removeFromRight (100));
    footer.removeFromRight (8);
    importButton.setBounds (footer.removeFromRight (100));
}

void PresetBrowser::layoutSidebar (juce::Rectangle<int> s)
{
    for (auto* b : packButtons)
        b->setBounds (s.removeFromTop (54));
}

//==============================================================================
// Header bar

PresetBar::PresetBar (const juce::String& l) : label (l)
{
    for (auto* b : { &prev, &next, &heart })
    {
        b->setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        b->setColour (juce::TextButton::textColourOffId, Colours::screenText);
        b->setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        b->setButtonText ({});
        b->onStateChange = [this] { repaint(); };   // their glyphs are drawn on the glass
        addAndMakeVisible (b);
    }
    save.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    save.setButtonText ({});
    addAndMakeVisible (save);
    save.setTooltip ("save this sound as a STUDIO (room + every knob + lfos)");
    heart.setTooltip ("favorite");
    prev.setTooltip ("previous studio");
    next.setTooltip ("next studio");
    prev.onClick = [this] { if (onPrev) onPrev(); };
    next.onClick = [this] { if (onNext) onNext(); };
    save.onClick = [this] { if (onSave) onSave(); };
    heart.onClick = [this] { if (onToggleFavorite) onToggleFavorite(); };
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void PresetBar::setPreset (const juce::String& n, bool mod, bool fav, bool canFav)
{
    if (n == name && mod == modified && fav == favorite && canFav == canFavorite)
        return;
    name = n;
    modified = mod;
    favorite = fav;
    canFavorite = canFav;
    heart.setEnabled (canFavorite);
    repaint();
}

juce::Rectangle<int> PresetBar::nameArea() const
{
    auto r = getLocalBounds();
    r.removeFromRight (getHeight() + 8);
    return r;
}

void PresetBar::paint (juce::Graphics& g)
{
    // WARP's preset box: one dark box with the name in caps, then the heart, a
    // divider and the arrows; save sits outside it as a bare icon.
    const auto box = nameArea().toFloat().reduced (0.5f);
    g.setColour (juce::Colour (0xff141212));
    g.fillRoundedRectangle (box, 6.0f);
    g.setColour (Colours::moduleEdge);
    g.drawRoundedRectangle (box, 6.0f, 1.0f);
    if (isMouseOver (true) && nameArea().contains (getMouseXYRelative()))
    {
        g.setColour (juce::Colours::white.withAlpha (0.03f));
        g.fillRoundedRectangle (box, 6.0f);
    }

    const auto title = (name.isEmpty() ? juce::String ("no preset") : name) + (modified && name.isNotEmpty() ? " *" : "");
    auto text = box.withRight ((float) heart.getX() - 6.0f).withTrimmedLeft (12.0f);
    g.setFont (Fonts::sans (13.5f, Fonts::bold).withExtraKerningFactor (0.18f));
    g.setColour (Colours::dustWhite.withAlpha (name.isEmpty() ? 0.6f : 1.0f));
    g.drawFittedText (title.toUpperCase(), text.toNearestInt(), juce::Justification::centred, 1, 0.8f);

    const auto hb = heart.getBounds().toFloat().withSizeKeepingCentre (15.0f, 13.0f);
    const auto hc = ! canFavorite ? Colours::dustWhite.withAlpha (0.25f)
                  : favorite      ? Colours::crimson.brighter (0.2f)
                  : Colours::dustWhite.withAlpha (heart.isMouseOver() ? 1.0f : 0.75f);
    drawHeart (g, hb, favorite, hc);

    g.setColour (Colours::moduleEdge);
    g.drawVerticalLine (prev.getX() - 3, box.getY() + 9.0f, box.getBottom() - 9.0f);
    for (auto* b : { &prev, &next })
    {
        const auto c = b->getBounds().toFloat().getCentre();
        const auto d = b == &prev ? -1.0f : 1.0f;
        juce::Path chevron;
        chevron.startNewSubPath (c.x - d * 2.5f, c.y - 5.0f);
        chevron.lineTo (c.x + d * 2.5f, c.y);
        chevron.lineTo (c.x - d * 2.5f, c.y + 5.0f);
        g.setColour (Colours::dustWhite.withAlpha (b->isMouseOver() ? 1.0f : 0.75f));
        g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void PresetBar::paintOverChildren (juce::Graphics& g)
{
    // The save icon: a floppy, drawn bare
    const auto sb = save.getBounds().toFloat().withSizeKeepingCentre (20.0f, 20.0f).translated (0.0f, save.isDown() ? 0.6f : 0.0f);
    g.setColour (Colours::dustWhite.withAlpha (save.isMouseOver() ? 1.0f : 0.85f));
    g.drawRoundedRectangle (sb, 2.5f, 1.6f);
    g.fillRect (sb.getX() + sb.getWidth() * 0.27f, sb.getY(), sb.getWidth() * 0.46f, sb.getHeight() * 0.32f);
    g.drawRect (sb.getX() + sb.getWidth() * 0.22f, sb.getY() + sb.getHeight() * 0.56f, sb.getWidth() * 0.56f, sb.getHeight() * 0.44f, 1.3f);
}

void PresetBar::resized()
{
    auto r = getLocalBounds();
    save.setBounds (r.removeFromRight (getHeight()));
    r.removeFromRight (8);
    auto inner = r.reduced (6, 6);
    next.setBounds (inner.removeFromRight (24));
    prev.setBounds (inner.removeFromRight (24));
    inner.removeFromRight (8);
    heart.setBounds (inner.removeFromRight (26));
}

void PresetBar::mouseUp (const juce::MouseEvent& e)
{
    if (nameArea().contains (e.getPosition()) && onOpen)
        onOpen();
}

} // namespace room::presets
