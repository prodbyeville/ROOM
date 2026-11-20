#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// ROOM's own menu window. Every menu in the plugin is still built as a juce::PopupMenu
// (items with actions, section headers, separators, ticks, info lines, submenus); this
// draws and runs it, so it looks and behaves the same everywhere:
//
//   - the dark panel fills the window exactly (its shadow is drawn inside the window's own
//     margin), and every highlight sits inside the panel with an even inset
//   - one text inset for headers, items and info lines; ticks and submenu chevrons at a
//     fixed right inset
//   - long menus split into columns only at section headers, balanced by height
//   - submenus open beside their item on hover; escape, a click outside, up / down /
//     return all work; the menu opens at the mouse (right-click) or under its button and
//     always stays on screen
//
// Use RoomMenu::show (menu, target) (EvilleLookAndFeel::showMenu calls it).
class RoomMenu : public juce::Component,
                 private juce::Timer
{
public:
    /** Opens the menu for `target` (at the mouse, or under the target when it was opened from a key). */
    static void show (const juce::PopupMenu&, juce::Component& target);

    struct Entry
    {
        enum class Kind { item, header, separator };
        Kind kind = Kind::item;
        juce::String text, shortcut;
        bool enabled = true, ticked = false;
        std::function<void()> action;
        std::shared_ptr<juce::PopupMenu> sub;
        juce::Rectangle<float> row;   // in the menu's coordinates
        bool selectable() const { return kind == Kind::item && enabled && (action != nullptr || sub != nullptr); }
    };

    /** Layout, for tests: the panel and the entries (with their rows) at scale 1. */
    static std::unique_ptr<RoomMenu> createForTest (const juce::PopupMenu&);
    juce::Rectangle<float> panelBounds() const { return panel; }
    const std::vector<Entry>& getEntries() const { return entries; }
    juce::Rectangle<float> highlightFor (const Entry& e) const;
    int numColumns() const { return (int) columns.size(); }
    /** Tests / screenshots: highlight the n-th selectable item. */
    void setHoverForTest (int nthSelectable);

    ~RoomMenu() override;
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent& e) override { mouseMove (e); }
    bool keyPressed (const juce::KeyPress&) override;
    void inputAttemptWhenModal() override;
    bool canModalEventBeSentToComponent (const juce::Component*) override;

private:
    RoomMenu (const juce::PopupMenu&, float scale, RoomMenu* parentMenu);
    void layout();
    void timerCallback() override;
    int entryAt (juce::Point<float>) const;
    void setHover (int index, bool openSubmenu);
    void openSubmenu (int index);
    void closeSubmenu();
    void trigger (int index);
    void dismissAll();
    RoomMenu* root();
    void placeAt (juce::Rectangle<int> anchor, bool beside);

    std::vector<Entry> entries;
    std::vector<juce::Rectangle<float>> columns;   // column areas inside the panel
    juce::Rectangle<float> panel;
    float scale = 1.0f;
    int hover = -1, pendingSub = -1, subIndex = -1;
    RoomMenu* parent = nullptr;
    std::unique_ptr<RoomMenu> child;
    bool dismissed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoomMenu)
};

/** A ComboBox whose list opens as a RoomMenu (the same look as every other menu), not
    JUCE's own popup. Drop-in: items, ids, attachments and onChange all work as usual. */
class RoomComboBox : public juce::ComboBox
{
public:
    using juce::ComboBox::ComboBox;
    void showPopup() override
    {
        juce::PopupMenu m;
        const auto current = getSelectedId();
        for (int i = 0; i < getNumItems(); ++i)
        {
            const auto id = getItemId (i);
            if (id == 0)
                continue;
            m.addItem (getItemText (i), isItemEnabled (id), id == current,
                       [safe = juce::Component::SafePointer<juce::ComboBox> (this), id]
                       {
                           if (safe != nullptr)
                               safe->setSelectedId (id, juce::sendNotificationAsync);
                       });
        }
        RoomMenu::show (m, *this);
    }
};
