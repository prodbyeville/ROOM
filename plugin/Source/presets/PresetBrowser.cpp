#include "PresetBrowser.h"
#include "../UiColours.h"

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

    const juce::Colour rowAlt { 0xff19191d };
    const juce::Colour selectedRow { 0xff2a2a33 };

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
        t.setColour (juce::TextEditor::backgroundColourId, Colours::background);
        t.setColour (juce::TextEditor::outlineColourId, Colours::outline);
        t.setColour (juce::TextEditor::focusedOutlineColourId, Colours::accent);
        t.setColour (juce::TextEditor::textColourId, Colours::text);
        t.setColour (juce::CaretComponent::caretColourId, Colours::text);
        t.setTextToShowWhenEmpty (hint, Colours::dim);
        t.setFont (uiFont (14.0f));
        t.setIndents (8, 6);
    }

    void styleButton (juce::TextButton& b, bool primary = false)
    {
        b.setColour (juce::TextButton::buttonColourId, primary ? Colours::accent : Colours::panel);
        b.setColour (juce::TextButton::textColourOffId, primary ? Colours::background : Colours::text);
        b.setColour (juce::ComboBox::outlineColourId, Colours::outline);
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

    // Table
    table.setColour (juce::ListBox::backgroundColourId, Colours::background);
    table.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    table.setRowHeight (30);
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
    h.setColour (juce::TableHeaderComponent::backgroundColourId, Colours::background);
    h.setColour (juce::TableHeaderComponent::textColourId, Colours::dim);
    h.setColour (juce::TableHeaderComponent::outlineColourId, Colours::outline);
    h.setColour (juce::TableHeaderComponent::highlightColourId, Colours::panel);
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
        addAndMakeVisible (b);

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
    g.fillAll (selected ? selectedRow : (row % 2 ? rowAlt : Colours::background));
    if (row >= 0 && row < (int) rows.size()
        && lib().getEntries()[(size_t) rows[(size_t) row]].key == currentKey())
    {
        g.setColour (Colours::accent);
        g.fillRect (0, 0, 3, h);
    }
    juce::ignoreUnused (w);
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
        case colName:  text = e.meta.name; colour = current ? Colours::accent : Colours::text; break;
        case colType:  text = e.meta.type; colour = Colours::accent2; break;
        case colStyle: text = e.meta.styles.joinIntoString (" \xc2\xb7 "); break;
        case colDecay: text = e.tags.decay.isEmpty() ? juce::String() : e.tags.medianT60 <= 0.0f ? juce::String ("steady") : juce::String (e.tags.medianT60, 1) + " s"; break;
        case colTone:  text = e.tags.tone; break;
        case colSource: text = e.tags.source; break;
        case colMadeFor: text = e.meta.madeFor.joinIntoString (" \xc2\xb7 "); colour = Colours::accent2; break;
        case colRoom:  text = e.meta.roomName; break;
        case colPack:  text = e.pack; break;
        case colFav:
            drawHeart (g, juce::Rectangle<float> ((float) w, (float) h).reduced (8.0f, 9.0f), e.favorite,
                       e.favorite ? Colours::reduction : Colours::outline.brighter (0.3f));
            return;
        default: break;
    }
    g.setColour (colour);
    g.setFont (uiFont (13.0f, col == colName && (selected || current)));
    g.drawText (text, 8, 0, w - 12, h, juce::Justification::centredLeft, true);
}

void PresetBrowser::cellClicked (int row, int col, const juce::MouseEvent&)
{
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
    auto* w = new juce::AlertWindow ("New pack", juce::String ("Name the new pack (a folder in ") + (kind == Kind::room ? "ROOMS" : "STUDIOS") + ").", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", {}, "Pack name");
    w->addButton ("Create", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int result)
    {
        if (result == 1)
        {
            juce::String err;
            if (lib().createPack (w->getTextEditorContents ("name"), err))
                refresh();
            else
                juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("New pack").withMessage (err).withButton ("OK"), nullptr);
        }
    }), true);
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
                              juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Import")
                                                                .withMessage ("Imported " + juce::String (n) + " of "
                                                                              + juce::String (files.size()) + " into " + target + ".")
                                                                .withButton ("OK"), nullptr);
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
                              juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Export")
                                                                .withMessage (ok ? "Exported to " + dir.getFullPathName()
                                                                                 : "Some presets couldn't be exported.")
                                                                .withButton ("OK"), nullptr);
                          });
}

void PresetBrowser::deleteSelected()
{
    const auto* e = selectedEntry();
    if (e == nullptr || e->factory)
        return;
    const auto key = e->key;
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle (juce::String ("Delete ") + lib().noun())
                                      .withMessage ("Delete \"" + e->meta.name + "\"? This can't be undone.")
                                      .withButton ("Delete").withButton ("Cancel")
                                      .withAssociatedComponent (this),
                                  [this, key] (int result)
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

void PresetBrowser::paint (juce::Graphics& g)
{
    g.fillAll (Colours::background);

    // Title
    g.setColour (Colours::text);
    g.setFont (uiFont (20.0f, true));
    g.drawText (kind == Kind::room ? "ROOMS" : "STUDIOS", 20, 0, 120, 56, juce::Justification::centredLeft);

    // Panels
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (sidebarArea.toFloat(), 8.0f);
    g.fillRoundedRectangle (detailArea.toFloat(), 8.0f);

    // Pack rows (painted; transparent buttons on top catch the clicks)
    for (int i = 0; i < packButtons.size() && i < (int) packRows.size(); ++i)
    {
        const auto r = packButtons[i]->getBounds();
        const auto sel = i == selectedPackRow;
        if (sel)
        {
            g.setColour (selectedRow);
            g.fillRoundedRectangle (r.toFloat(), 5.0f);
            g.setColour (Colours::accent);
            g.fillRect (r.getX(), r.getY() + 5, 2, r.getHeight() - 10);
        }
        const auto& pr = packRows[(size_t) i];
        g.setColour (sel ? Colours::text : Colours::dim.brighter (0.2f));
        g.setFont (uiFont (13.0f, sel));
        auto tr = r.reduced (10, 0);
        if (pr.favorites)
        {
            drawHeart (g, tr.removeFromLeft (14).toFloat().reduced (0.0f, 8.0f), true, Colours::reduction);
            tr.removeFromLeft (6);
        }
        g.drawText (pr.label, tr, juce::Justification::centredLeft);
        g.setColour (Colours::dim);
        g.setFont (uiFont (12.0f));
        g.drawText (juce::String (pr.count), tr, juce::Justification::centredRight);
    }

    // Details
    auto d = detailArea.reduced (16, 14);
    const auto* e = selectedEntry();
    if (e == nullptr)
    {
        g.setColour (Colours::dim);
        g.setFont (uiFont (13.0f));
        g.drawFittedText (juce::String ("Select a ") + lib().noun() + " to see its details.\nDouble-click to load it."
                              + (kind == Kind::room ? "\n\nLoading a ROOM only swaps the room. Your knobs and LFOs stay." : ""),
                          d.withHeight (110),
                          juce::Justification::topLeft, 3);
        return;
    }

    g.setColour (Colours::text);
    g.setFont (uiFont (18.0f, true));
    g.drawFittedText (e->meta.name, d.removeFromTop (48), juce::Justification::topLeft, 2);
    g.setColour (Colours::dim);
    g.setFont (uiFont (12.0f));
    g.drawText (e->meta.author.isNotEmpty() ? e->meta.author : juce::String ("User"), d.removeFromTop (18), juce::Justification::topLeft);
    d.removeFromTop (8);

    auto line = [&] (const juce::String& k, const juce::String& v)
    {
        auto row = d.removeFromTop (20);
        g.setColour (Colours::dim);
        g.setFont (uiFont (12.0f));
        g.drawText (k, row.removeFromLeft (76), juce::Justification::centredLeft);
        g.setColour (Colours::text);
        g.drawText (v.isNotEmpty() ? v : juce::String ("-"), row, juce::Justification::centredLeft, true);
    };
    if (kind == Kind::room)
    {
        line ("Type", e->meta.type);
        line ("Character", e->meta.styles.joinIntoString (", "));
        line ("Decay", e->tags.decay);
        line ("Tone", e->tags.tone);
        line ("Source", e->tags.source);
    }
    else
    {
        line ("Made for", e->meta.madeFor.joinIntoString (", "));
        line ("Style", e->meta.styles.joinIntoString (", "));
        line ("Room", e->meta.roomName);
    }
    line ("Pack", e->pack + (e->factory ? "  (built in)" : ""));
    if (e->meta.created.toMilliseconds() > 0)
        line ("Saved", e->meta.created.formatted ("%d %b %Y"));

    // Room summary, read from the preset itself
    const auto tree = lib().loadPreset (*e);
    const auto a = RoomAnalysis::fromValueTree (tree.getChildWithName ("ROOM_ANALYSIS"));
    d.removeFromTop (8);
    g.setColour (Colours::outline);
    g.drawHorizontalLine (d.getY(), (float) d.getX(), (float) d.getRight());
    d.removeFromTop (10);
    if (a.valid)
    {
        line ("Balance", juce::String (a.balance, 2) + (a.balance > 0.6f ? "  spatial" : a.balance < 0.4f ? "  tonal" : "  in between"));
        line ("Decay time", a.steady ? juce::String ("steady sound") : juce::String (a.medianT60, 2) + " s");
        line ("Rings", juce::String ((int) a.peaks.size()) + " resonance" + (a.peaks.size() == 1 ? "" : "s"));
    }
    else
        line ("Room", "none (knobs only)");

    if (e->meta.notes.isNotEmpty())
    {
        d.removeFromTop (8);
        g.setColour (Colours::dim);
        g.setFont (uiFont (12.0f));
        g.drawFittedText (e->meta.notes, d.removeFromTop (60), juce::Justification::topLeft, 4);
    }
}

void PresetBrowser::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (56).reduced (16, 12);
    top.removeFromLeft (110);
    closeButton.setBounds (top.removeFromRight (70));
    top.removeFromRight (10);
    countLabel.setBounds (top.removeFromRight (190));
    top.removeFromRight (10);
    search.setBounds (top);

    r = r.reduced (16, 0).withTrimmedBottom (16);
    sidebarArea = r.removeFromLeft (200);
    r.removeFromLeft (12);
    detailArea = r.removeFromRight (kind == Kind::room ? 200 : 230);
    r.removeFromRight (12);
    table.setBounds (r);

    layoutSidebar (sidebarArea.reduced (10, 12));

    auto d = detailArea.reduced (16, 14);
    auto buttons = d.removeFromBottom (32);
    loadButton.setBounds (buttons.removeFromRight (90));
    buttons.removeFromRight (6);
    favButton.setBounds (buttons);
    auto row2 = d.removeFromBottom (40).withTrimmedBottom (8);
    editButton.setBounds (row2.removeFromLeft (row2.getWidth() / 2 - 3));
    row2.removeFromLeft (6);
    deleteButton.setBounds (row2);
}

void PresetBrowser::layoutSidebar (juce::Rectangle<int> s)
{
    packsLabel.setBounds (s.removeFromTop (18));
    for (auto* b : packButtons)
        b->setBounds (s.removeFromTop (26));
    s.removeFromTop (4);
    auto actions = s.removeFromTop (24);
    const auto w3 = actions.getWidth() / 3;
    newPackButton.setBounds (actions.removeFromLeft (w3 + 8));
    importButton.setBounds (actions.removeFromLeft (w3 - 4));
    exportButton.setBounds (actions);
    s.removeFromTop (12);

    typesLabel.setBounds (s.removeFromTop (18));
    typeChips.setBounds (s.removeFromTop (typeChips.heightForWidth (s.getWidth())));
    s.removeFromTop (10);
    stylesLabel.setBounds (s.removeFromTop (18));
    styleChips.setBounds (s.removeFromTop (styleChips.heightForWidth (s.getWidth())));
    if (kind == Kind::room)
    {
        s.removeFromTop (10);
        autoTagsLabel.setBounds (s.removeFromTop (18));
        autoChips.setBounds (s.removeFromTop (autoChips.heightForWidth (s.getWidth())));
    }
    s.removeFromTop (6);
    clearFilters.setBounds (s.removeFromTop (24).removeFromLeft (100));
}

//==============================================================================
// Header bar

PresetBar::PresetBar (const juce::String& l) : label (l)
{
    for (auto* b : { &prev, &next, &save })
    {
        styleButton (*b);
        addAndMakeVisible (b);
    }
    heart.setColour (juce::TextButton::buttonColourId, Colours::panel);
    addAndMakeVisible (heart);
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
    r.removeFromLeft (30);
    r.removeFromRight (30 + 6 + 34 + 6 + 64);
    return r.reduced (4, 0);
}

void PresetBar::paint (juce::Graphics& g)
{
    const auto r = nameArea().toFloat();
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

    auto tr = nameArea().reduced (10, 0);
    g.setColour (Colours::accent);
    g.setFont (uiFont (11.0f, true));
    const auto lw = juce::GlyphArrangement::getStringWidthInt (uiFont (11.0f, true), label) + 8;
    g.drawText (label, tr.removeFromLeft (lw), juce::Justification::centredLeft);
    g.setColour (Colours::outline);
    g.drawVerticalLine (tr.getX(), r.getY() + 7.0f, r.getBottom() - 7.0f);
    tr.removeFromLeft (10);
    g.setColour (name.isEmpty() ? Colours::dim : Colours::text);
    g.setFont (uiFont (14.0f, name.isNotEmpty()));
    g.drawText ((name.isEmpty() ? juce::String ("Init") : name) + (modified && name.isNotEmpty() ? " *" : ""), tr.withTrimmedRight (16),
                juce::Justification::centredLeft, true);
    // little "open" chevron
    juce::Path chev;
    const auto cx = (float) tr.getRight() - 8.0f, cy = r.getCentreY();
    chev.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
    g.setColour (Colours::dim);
    g.fillPath (chev);
}

void PresetBar::paintOverChildren (juce::Graphics& g)
{
    // heart on top of its button
    const auto hb = heart.getBounds().toFloat().reduced (9.0f, 8.0f);
    drawHeart (g, hb, favorite, ! canFavorite ? Colours::outline : favorite ? Colours::reduction : Colours::dim);
}

void PresetBar::resized()
{
    auto r = getLocalBounds();
    prev.setBounds (r.removeFromLeft (30));
    save.setBounds (r.removeFromRight (64));
    r.removeFromRight (6);
    heart.setBounds (r.removeFromRight (34));
    r.removeFromRight (6);
    next.setBounds (r.removeFromRight (30));
}

void PresetBar::mouseUp (const juce::MouseEvent& e)
{
    if (nameArea().contains (e.getPosition()) && onOpen)
        onOpen();
}

} // namespace room::presets
