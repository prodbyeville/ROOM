#pragma once

#include "../PluginProcessor.h"

// Library UI, used for both ROOMS (rooms only) and STUDIOS (full presets):
//   PresetBar     - in the header: < [STUDIOS | name*] >  heart  Save
//   PresetBrowser - full-window overlay: packs, filters, sortable list, details
//   SaveDialog    - name, tags, pack, notes (also used to edit tags)
//
// Tags: ROOMS have Type + Character, plus Decay / Tone / Source that ROOM works
// out itself. STUDIOS have Made for + Style.

namespace room::presets
{

//==============================================================================
/** Toggleable tag chip (types, styles). */
class Chip : public juce::Button
{
public:
    explicit Chip (const juce::String& label) : juce::Button (label) { setClickingTogglesState (true); }
    void paintButton (juce::Graphics&, bool over, bool down) override;
    int idealWidth() const;
};

/** A row of chips that wraps onto several lines. */
class ChipGroup : public juce::Component
{
public:
    ChipGroup (const juce::StringArray& labels, bool singleSelect);

    juce::StringArray getSelected() const;
    void setSelected (const juce::StringArray&);
    int heightForWidth (int width) const;
    void resized() override;

    std::function<void()> onChange;

private:
    juce::OwnedArray<Chip> chips;
    bool single;
};

//==============================================================================
class SaveDialog : public juce::Component
{
public:
    enum class Mode { save, edit };

    explicit SaveDialog (Kind);

    /** Fill and show. For edit mode, pass the current meta and pack.
        autoInfo: a read-only line (ROOMS: the auto tags). */
    void open (Mode, const PresetMeta&, const juce::StringArray& packs, const juce::String& pack,
               const juce::String& autoInfo = {});

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;   // click outside the card = cancel
    bool keyPressed (const juce::KeyPress&) override;

    /** Return an error message to show, or empty on success (dialog closes). */
    std::function<juce::String (Mode, const PresetMeta&, const juce::String& pack, bool overwrite)> onSubmit;

private:
    void submit();
    juce::Rectangle<int> card() const;

    const Kind kind;
    Mode mode = Mode::save;
    PresetMeta original;
    bool overwriteArmed = false;

    juce::Label title, nameLabel, typeLabel, styleTagLabel, packLabel, notesLabel, error, autoLabel;
    juce::TextEditor name, notes, newPackName;
    ChipGroup types;    // ROOMS: Type (pick one)   STUDIOS: Made for (pick any)
    ChipGroup styles;   // ROOMS: Character         STUDIOS: Style
    juce::ComboBox pack;
    juce::TextButton saveButton { "Save" }, cancelButton { "Cancel" };
};

//==============================================================================
class PresetBrowser : public juce::Component,
                      private juce::TableListBoxModel
{
public:
    PresetBrowser (RoomAudioProcessor&, Kind);
    Kind getKind() const { return kind; }

    /** Re-read the library and refresh everything. */
    void refresh();

    /** The current list (after filters and sorting), for the < > buttons. */
    std::vector<int> currentList() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override;

    std::function<void()> onClose;
    std::function<void (const juce::String& key)> onLoad;
    std::function<void (const juce::String& key, const PresetMeta&, const juce::String& pack)> onEdit;

private:
    // TableListBoxModel
    int getNumRows() override { return (int) rows.size(); }
    void paintRowBackground (juce::Graphics&, int row, int w, int h, bool selected) override;
    void paintCell (juce::Graphics&, int row, int col, int w, int h, bool selected) override;
    void cellClicked (int row, int col, const juce::MouseEvent&) override;
    void cellDoubleClicked (int row, int col, const juce::MouseEvent&) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void sortOrderChanged (int columnId, bool forwards) override;
    void returnKeyPressed (int row) override;

    void updateRows();
    void layoutSidebar (juce::Rectangle<int>);
    const PresetLibrary::Entry* selectedEntry() const;
    void showDetailsFor (const PresetLibrary::Entry*);
    void newPack();
    void importPresets();
    void exportPack();
    void deleteSelected();
    PresetLibrary& lib() const;
    juce::String currentKey() const;

    struct PackRow { juce::String label, pack; bool favorites = false; int count = 0; };

    RoomAudioProcessor& proc;
    const Kind kind;
    PresetLibrary::Query query;

    juce::TextEditor search;
    juce::Label countLabel;
    juce::TextButton closeButton { "Close" };

    // Sidebar
    juce::OwnedArray<juce::TextButton> packButtons;
    std::vector<PackRow> packRows;
    int selectedPackRow = 0;
    juce::TextButton newPackButton { "+ New pack" }, importButton { "Import..." }, exportButton { "Export..." };
    juce::Label typesLabel, stylesLabel, autoTagsLabel, packsLabel;
    ChipGroup typeChips;    // ROOMS: Type     STUDIOS: Made for
    ChipGroup styleChips;   // ROOMS: Character  STUDIOS: Style
    ChipGroup autoChips;    // ROOMS: Decay / Tone / Source
    juce::TextButton clearFilters { "Clear filters" };
    juce::Rectangle<int> sidebarArea, detailArea;

    // List
    juce::TableListBox table { "LIBRARY", this };
    std::vector<int> rows;

    // Details
    juce::String selectedKey;
    juce::TextButton loadButton { "Load" }, favButton { "Favorite" }, editButton { "Edit" }, deleteButton { "Delete" };

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBrowser)
};

//==============================================================================
class PresetBar : public juce::Component
{
public:
    explicit PresetBar (const juce::String& label);
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    void setPreset (const juce::String& name, bool modified, bool favorite, bool canFavorite);

    std::function<void()> onOpen, onPrev, onNext, onToggleFavorite, onSave;

private:
    juce::Rectangle<int> nameArea() const;

    juce::String label, name;
    bool modified = false, favorite = false, canFavorite = false;
    juce::TextButton prev { "<" }, next { ">" }, heart, save { "Save" };
};

} // namespace room::presets
