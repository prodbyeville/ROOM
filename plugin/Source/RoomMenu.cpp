#include "RoomMenu.h"
#include "EvilleLookAndFeel.h"
#include "UiColours.h"

namespace
{
    // Metrics at scale 1
    constexpr float margin = 14.0f;      // the shadow lives here, outside the panel
    constexpr float padY = 6.0f;         // above the first and below the last row
    constexpr float inset = 14.0f;       // where every text starts, from the column's left
    constexpr float rightInset = 14.0f;  // ticks and chevrons sit this far from the right
    constexpr float rightSpace = 24.0f;  // kept for the tick / chevron on every row
    constexpr float itemH = 28.0f, headerH = 28.0f, headerGap = 8.0f, sepH = 11.0f;
    constexpr float minWidth = 190.0f;
    constexpr float corner = 9.0f;

    juce::Font itemFont (float s) { return Fonts::sans (14.0f * s); }
    juce::Font headerFont (float s) { return Fonts::sans (9.5f * s, Fonts::bold).withExtraKerningFactor (0.18f); }

    float textWidth (const juce::Font& f, const juce::String& t)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (f, t, 0.0f, 0.0f);
        return ga.getBoundingBox (0, -1, true).getWidth();
    }

    float maxColumnHeightFor (juce::Rectangle<int> anchor)
    {
        if (auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (anchor))
            return (float) d->userArea.getHeight() * 0.82f;
        return 640.0f;
    }

    float pendingMaxColumnHeight = 640.0f;   // read by the constructor (set just before)
}

//==============================================================================
void RoomMenu::show (const juce::PopupMenu& m, juce::Component& target)
{
    const auto mouse = juce::Desktop::getInstance().getMainMouseSource();
    const auto pos = mouse.getScreenPosition().roundToInt();
    const auto isKey = dynamic_cast<juce::Button*> (&target) != nullptr && ! mouse.getCurrentModifiers().isPopupMenu();
    const auto fromTarget = isKey || ! target.getScreenBounds().contains (pos);
    const auto anchor = fromTarget ? target.getScreenBounds() : juce::Rectangle<int> (pos.x, pos.y, 1, 1);

    const auto scale = juce::jlimit (0.6f, 2.0f, juce::Component::getApproximateScaleFactorForComponent (&target));
    pendingMaxColumnHeight = maxColumnHeightFor (anchor);
    auto* menu = new RoomMenu (m, scale, nullptr);
    menu->placeAt (anchor, false);
    menu->setAlwaysOnTop (true);
    menu->addToDesktop (juce::ComponentPeer::windowIsTemporary);
    menu->setVisible (true);
    menu->enterModalState (true, nullptr, true);   // deleted when dismissed
    menu->grabKeyboardFocus();
}

std::unique_ptr<RoomMenu> RoomMenu::createForTest (const juce::PopupMenu& m)
{
    pendingMaxColumnHeight = 640.0f;
    return std::unique_ptr<RoomMenu> (new RoomMenu (m, 1.0f, nullptr));
}

RoomMenu::RoomMenu (const juce::PopupMenu& m, float s, RoomMenu* parentMenu) : scale (s), parent (parentMenu)
{
    setOpaque (false);
    setWantsKeyboardFocus (parent == nullptr);
    setMouseClickGrabsKeyboardFocus (false);
    for (juce::PopupMenu::MenuItemIterator it (m); it.next();)
    {
        const auto& item = it.getItem();
        Entry e;
        if (item.isSeparator)
            e.kind = Entry::Kind::separator;
        else if (item.isSectionHeader)
            e.kind = Entry::Kind::header;
        e.text = item.text;
        e.shortcut = item.shortcutKeyDescription;
        e.enabled = item.isEnabled;
        e.ticked = item.isTicked;
        e.action = item.action;
        if (item.subMenu != nullptr)
            e.sub = std::make_shared<juce::PopupMenu> (*item.subMenu);
        entries.push_back (std::move (e));
    }
    layout();
}

RoomMenu::~RoomMenu()
{
    child.reset();
}

RoomMenu* RoomMenu::root()
{
    auto* r = this;
    while (r->parent != nullptr)
        r = r->parent;
    return r;
}

//==============================================================================
void RoomMenu::layout()
{
    const auto s = scale;
    auto heightOf = [s] (const Entry& e, bool firstInColumn)
    {
        switch (e.kind)
        {
            case Entry::Kind::separator: return sepH * s;
            case Entry::Kind::header:    return (headerH + (firstInColumn ? 0.0f : headerGap)) * s;
            case Entry::Kind::item:      break;
        }
        return itemH * s;
    };
    auto widthOf = [s] (const Entry& e)
    {
        if (e.kind == Entry::Kind::header)
            return (inset * 2.0f) * s + textWidth (headerFont (s), e.text.toUpperCase());
        if (e.kind == Entry::Kind::separator)
            return 0.0f;
        auto w = inset * s + textWidth (itemFont (s), e.text) + (rightSpace + rightInset) * s;
        if (e.shortcut.isNotEmpty())
            w += textWidth (itemFont (s * 0.85f), e.shortcut) + 10.0f * s;
        return w;
    };

    // Sections: a header and what follows it, up to the next header.
    std::vector<std::pair<size_t, size_t>> sections;   // [first, end)
    for (size_t i = 0; i < entries.size(); ++i)
        if (i == 0 || entries[i].kind == Entry::Kind::header)
            sections.push_back ({ i, i + 1 });
        else
            sections.back().second = i + 1;

    auto sectionHeight = [&] (std::pair<size_t, size_t> sec, bool first)
    {
        float h = 0.0f;
        for (auto i = sec.first; i < sec.second; ++i)
            h += heightOf (entries[i], first && i == sec.first);
        return h;
    };
    float total = 0.0f;
    for (size_t k = 0; k < sections.size(); ++k)
        total += sectionHeight (sections[k], k == 0);

    // Columns break only between sections, balanced by height.
    const auto maxH = std::max (200.0f * s, pendingMaxColumnHeight - 2.0f * (margin + padY) * s);
    std::vector<std::vector<size_t>> cols (1);   // section indices per column
    if (total > maxH)
    {
        const auto numCols = (int) std::ceil (total / maxH);
        const auto target = total / (float) numCols;
        float h = 0.0f;
        for (size_t k = 0; k < sections.size(); ++k)
        {
            const auto sh = sectionHeight (sections[k], cols.back().empty());
            if (! cols.back().empty() && h + sh * 0.5f > target && (int) cols.size() < numCols)
            {
                cols.emplace_back();
                h = 0.0f;
            }
            cols.back().push_back (k);
            h += sectionHeight (sections[k], cols.back().size() == 1);
        }
    }
    else
        for (size_t k = 0; k < sections.size(); ++k)
            cols.back().push_back (k);

    // Place the rows
    columns.clear();
    float x = margin * s, tallest = 0.0f;
    for (const auto& col : cols)
    {
        float w = minWidth * s;
        for (auto k : col)
            for (auto i = sections[k].first; i < sections[k].second; ++i)
                w = std::max (w, widthOf (entries[i]));
        w = std::ceil (w);
        float y = (margin + padY) * s;
        bool first = true;
        for (auto k : col)
            for (auto i = sections[k].first; i < sections[k].second; ++i)
            {
                const auto h = heightOf (entries[i], first);
                entries[i].row = { x, y, w, h };
                y += h;
                first = false;
            }
        columns.push_back ({ x, margin * s, w, 0.0f });
        tallest = std::max (tallest, y - margin * s);
        x += w + 1.0f;
    }
    const auto panelW = x - 1.0f - margin * s;
    panel = { margin * s, margin * s, panelW, tallest + padY * s };
    for (auto& c : columns)
        c.setHeight (panel.getHeight());
    setSize ((int) std::ceil (panel.getRight() + margin * s), (int) std::ceil (panel.getBottom() + margin * s));
}

void RoomMenu::setHoverForTest (int nth)
{
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].selectable() && nth-- == 0)
        {
            hover = (int) i;
            return;
        }
}

juce::Rectangle<float> RoomMenu::highlightFor (const Entry& e) const
{
    return e.row.reduced (5.0f * scale, 1.0f * scale);
}

void RoomMenu::placeAt (juce::Rectangle<int> anchor, bool beside)
{
    const auto m = (int) (margin * scale);
    juce::Rectangle<int> area (0, 0, 4000, 4000);
    if (auto* d = juce::Desktop::getInstance().getDisplays().getDisplayForRect (anchor))
        area = d->userArea;
    int x, y;
    if (beside)
    {
        x = anchor.getRight() - m + 2;
        y = anchor.getY() - m - (int) (padY * scale);
        if (x + getWidth() > area.getRight())
            x = anchor.getX() - getWidth() + m - 2;
    }
    else
    {
        x = anchor.getX() - m;
        y = anchor.getBottom() - m + (anchor.getHeight() > 1 ? 4 : 0);
        if (y + getHeight() > area.getBottom())   // no room below: open above
            y = anchor.getY() - getHeight() + m - (anchor.getHeight() > 1 ? 4 : 0);
    }
    x = juce::jlimit (area.getX() - m, std::max (area.getX() - m, area.getRight() - getWidth() + m), x);
    y = juce::jlimit (area.getY() - m, std::max (area.getY() - m, area.getBottom() - getHeight() + m), y);
    setTopLeftPosition (x, y);
}

//==============================================================================
void RoomMenu::paint (juce::Graphics& g)
{
    const auto s = scale;
    // the shadow, inside our own margin
    for (int i = 6; i >= 1; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.07f));
        g.fillRoundedRectangle (panel.expanded ((float) i * 1.4f * s).translated (0.0f, (float) i * 0.6f * s), (corner + (float) i) * s);
    }
    // the panel
    juce::ColourGradient fill (juce::Colour (0xff171515), panel.getX(), panel.getY(), juce::Colour (0xff0f0e0e), panel.getX(), panel.getBottom(), false);
    g.setGradientFill (fill);
    g.fillRoundedRectangle (panel, corner * s);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (panel.reduced (0.5f), corner * s, 1.0f);

    // column separators
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    for (size_t c = 1; c < columns.size(); ++c)
        g.fillRect (juce::Rectangle<float> (columns[c].getX() - 1.0f, panel.getY() + 10.0f * s, 1.0f, panel.getHeight() - 20.0f * s));

    for (size_t i = 0; i < entries.size(); ++i)
    {
        const auto& e = entries[i];
        const auto r = e.row;
        if (e.kind == Entry::Kind::separator)
        {
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.fillRect (juce::Rectangle<float> (r.getX() + inset * s, r.getCentreY(), r.getWidth() - (inset + rightInset) * s, 1.0f));
            continue;
        }
        if (e.kind == Entry::Kind::header)
        {
            auto t = r.withTrimmedLeft (inset * s).withTrimmedRight (rightInset * s);
            t = t.withTrimmedTop (t.getHeight() - headerH * s);   // the gap above is empty
            g.setFont (headerFont (s));
            g.setColour (Colours::redLight);
            g.drawText (e.text.toUpperCase(), t.withTrimmedBottom (3.0f * s), juce::Justification::centredLeft, true);
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.fillRect (juce::Rectangle<float> (t.getX(), t.getBottom() - 3.0f * s, t.getWidth(), 1.0f));
            continue;
        }
        const auto isInfo = e.action == nullptr && e.sub == nullptr;
        if ((int) i == hover && e.selectable())
        {
            g.setColour (juce::Colour (0xff2b2626));
            g.fillRoundedRectangle (highlightFor (e), 5.0f * s);
        }
        auto t = r.withTrimmedLeft (inset * s).withTrimmedRight ((rightSpace + rightInset) * s);
        if (e.shortcut.isNotEmpty())
        {
            g.setFont (itemFont (s * 0.85f));
            g.setColour (Colours::dustWhite.withAlpha (0.4f));
            g.drawText (e.shortcut, t, juce::Justification::centredRight, true);
            t.removeFromRight (textWidth (itemFont (s * 0.85f), e.shortcut) + 10.0f * s);
        }
        g.setFont (itemFont (s));
        g.setColour (Colours::dustWhite.withAlpha (! e.enabled ? (isInfo ? 0.45f : 0.32f) : isInfo ? 0.55f : 0.93f));
        g.drawText (e.text, t, juce::Justification::centredLeft, true);

        const auto rx = r.getRight() - rightInset * s;   // the right-hand marker's centre
        if (e.sub != nullptr)
        {
            juce::Path chevron;
            chevron.startNewSubPath (rx - 2.5f * s, r.getCentreY() - 4.5f * s);
            chevron.lineTo (rx + 2.0f * s, r.getCentreY());
            chevron.lineTo (rx - 2.5f * s, r.getCentreY() + 4.5f * s);
            g.setColour (Colours::dustWhite.withAlpha (0.7f));
            g.strokePath (chevron, juce::PathStrokeType (1.4f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else if (e.ticked)
        {
            g.setColour (Colours::redLight);
            g.fillEllipse (juce::Rectangle<float> (6.0f * s, 6.0f * s).withCentre ({ rx, r.getCentreY() }));
        }
    }
}

//==============================================================================
int RoomMenu::entryAt (juce::Point<float> p) const
{
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].selectable() && entries[i].row.contains (p))
            return (int) i;
    return -1;
}

void RoomMenu::setHover (int index, bool openSub)
{
    if (index != hover)
    {
        hover = index;
        repaint();
    }
    if (index >= 0 && entries[(size_t) index].sub != nullptr)
    {
        if (index != subIndex)
        {
            pendingSub = index;
            if (openSub)
                openSubmenu (index);
            else
                startTimer (140);
        }
    }
    else if (index >= 0 && subIndex >= 0)
        closeSubmenu();
}

void RoomMenu::mouseMove (const juce::MouseEvent& e)
{
    setHover (entryAt (e.position), false);
}

void RoomMenu::mouseExit (const juce::MouseEvent&)
{
    if (subIndex < 0 && hover >= 0)
    {
        hover = -1;
        repaint();
    }
}

void RoomMenu::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasDraggedSinceMouseDown() && e.getDistanceFromDragStart() > 4 && ! getLocalBounds().contains (e.getPosition()))
        return;
    const auto i = entryAt (e.position);
    if (i >= 0)
        trigger (i);
}

void RoomMenu::timerCallback()
{
    stopTimer();
    if (pendingSub >= 0 && pendingSub == hover)
        openSubmenu (pendingSub);
}

void RoomMenu::openSubmenu (int index)
{
    closeSubmenu();
    const auto& e = entries[(size_t) index];
    if (e.sub == nullptr)
        return;
    child.reset (new RoomMenu (*e.sub, scale, this));
    subIndex = index;
    const auto rowOnScreen = localAreaToGlobal (e.row.toNearestInt());
    child->placeAt (rowOnScreen, true);
    child->setAlwaysOnTop (true);
    child->addToDesktop (juce::ComponentPeer::windowIsTemporary);
    child->setVisible (true);
}

void RoomMenu::closeSubmenu()
{
    child.reset();
    subIndex = -1;
}

void RoomMenu::trigger (int index)
{
    const auto& e = entries[(size_t) index];
    if (e.sub != nullptr)
    {
        if (subIndex != index)
            openSubmenu (index);
        return;
    }
    auto action = e.action;
    root()->dismissAll();
    if (action != nullptr)
        juce::MessageManager::callAsync (action);
}

void RoomMenu::dismissAll()
{
    if (parent != nullptr)
    {
        root()->dismissAll();
        return;
    }
    if (dismissed)
        return;
    dismissed = true;
    stopTimer();
    closeSubmenu();
    setVisible (false);
    if (isCurrentlyModal (false))
        exitModalState (0);   // the modal manager deletes us
}

void RoomMenu::inputAttemptWhenModal()
{
    dismissAll();
}

bool RoomMenu::canModalEventBeSentToComponent (const juce::Component* c)
{
    for (auto* m = child.get(); m != nullptr; m = m->child.get())
        if (c == m || m->isParentOf (c))
            return true;
    return false;
}

bool RoomMenu::keyPressed (const juce::KeyPress& k)
{
    // keys go to the deepest open menu
    auto* m = this;
    while (m->child != nullptr)
        m = m->child.get();
    auto step = [m] (int dir)
    {
        const auto n = (int) m->entries.size();
        auto i = m->hover;
        for (int tries = 0; tries < n; ++tries)
        {
            i = (i + dir + n) % n;
            if (m->entries[(size_t) i].selectable())
            {
                m->setHover (i, false);
                return;
            }
        }
    };
    if (k == juce::KeyPress::escapeKey)
    {
        if (m != this)
            m->parent->closeSubmenu();
        else
            dismissAll();
        return true;
    }
    if (k == juce::KeyPress::downKey)  { step (1); return true; }
    if (k == juce::KeyPress::upKey)    { step (-1); return true; }
    if (k == juce::KeyPress::rightKey && m->hover >= 0 && m->entries[(size_t) m->hover].sub != nullptr)
    {
        m->openSubmenu (m->hover);
        return true;
    }
    if (k == juce::KeyPress::leftKey && m != this)
    {
        m->parent->closeSubmenu();
        return true;
    }
    if (k == juce::KeyPress::returnKey && m->hover >= 0)
    {
        m->trigger (m->hover);
        return true;
    }
    return true;
}
