#include "EvilleLookAndFeel.h"
#include "Screens.h"
#include "UiColours.h"
#include "UiAssets.h"

//==============================================================================
// Assets and fonts

const char* UiAssetFiles::get (const char* fileName, int& sizeOut)
{
    sizeOut = 0;
    for (int i = 0; i < UiAssets::namedResourceListSize; ++i)
        if (juce::String (UiAssets::getNamedResourceOriginalFilename (UiAssets::namedResourceList[i])) == fileName)
            return UiAssets::getNamedResource (UiAssets::namedResourceList[i], sizeOut);
    return nullptr;
}

juce::Image UiResources::image (const char* fileName)
{
    const juce::ScopedLock sl (lock);
    auto& img = images[fileName];
    if (! img.isValid())
    {
        int size = 0;
        if (auto* data = UiAssetFiles::get (fileName, size))
            img = juce::ImageFileFormat::loadFrom (data, (size_t) size);
    }
    return img;
}

juce::Typeface::Ptr UiResources::typeface (const char* fileName)
{
    const juce::ScopedLock sl (lock);
    auto& face = faces[fileName];
    if (face == nullptr)
    {
        int size = 0;
        if (auto* data = UiAssetFiles::get (fileName, size))
            face = juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
    }
    return face;
}

// Images and typefaces live in one shared cache, owned by every open editor's
// look and feel. It goes away with the last editor, while JUCE is still up:
// statics would only be freed when the host unloads the plugin's DLL, after
// JUCE has shut down, which crashes some hosts (FL Studio on delete).
juce::Typeface::Ptr Fonts::satoshi (Weight w)
{
    static const char* files[4] = { "Satoshi-Regular.otf", "Satoshi-Medium.otf", "Satoshi-Bold.otf", "Satoshi-Black.otf" };
    juce::SharedResourcePointer<UiResources> res;
    return res->typeface (files[(int) w]);
}

juce::Font Fonts::sans (float height, Weight w)
{
    if (auto t = satoshi (w))
        return juce::Font (juce::FontOptions (t).withHeight (height));
    return juce::Font (juce::FontOptions (height, w >= bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font Fonts::serif (float height)
{
    juce::SharedResourcePointer<UiResources> res;
    if (auto face = res->typeface ("CormorantGaramond-MediumItalic.ttf"))
        return juce::Font (juce::FontOptions (face).withHeight (height));
    return juce::Font (juce::FontOptions (height, juce::Font::italic));
}

juce::Font Fonts::mono (float height)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
}

//==============================================================================

EvilleLookAndFeel::EvilleLookAndFeel()
{
    namespace C = juce::Colours;
    setColour (juce::Slider::textBoxTextColourId, Colours::ink);
    setColour (juce::Slider::textBoxOutlineColourId, C::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, C::transparentBlack);
    setColour (juce::Slider::rotarySliderFillColourId, Colours::crimson);
    setColour (juce::Slider::thumbColourId, Colours::dustWhite);
    setColour (juce::Label::textColourId, Colours::ink);
    setColour (juce::Label::outlineColourId, C::transparentBlack);
    setColour (juce::TextEditor::outlineColourId, C::transparentBlack);
    setColour (juce::TextEditor::focusedOutlineColourId, Colours::crimson);
    setColour (juce::ToggleButton::textColourId, Colours::ink);
    setColour (juce::ToggleButton::tickColourId, Colours::crimson);
    setColour (juce::TextButton::buttonColourId, Colours::panel);
    setColour (juce::TextButton::textColourOffId, Colours::text);
    setColour (juce::ComboBox::backgroundColourId, Colours::glass);
    setColour (juce::ComboBox::outlineColourId, Colours::outline);
    setColour (juce::ComboBox::textColourId, Colours::screenText);
    setColour (juce::ComboBox::arrowColourId, Colours::screenDim);
    setColour (juce::PopupMenu::backgroundColourId, C::transparentBlack);   // see-through: the menu draws its own rounded glass
    setColour (juce::PopupMenu::textColourId, Colours::dustWhite);
    setColour (juce::PopupMenu::headerTextColourId, Colours::screenDim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff262626));
    setColour (juce::PopupMenu::highlightedTextColourId, Colours::dustWhite);
    setColour (juce::BubbleComponent::backgroundColourId, Colours::evilleBlack);
    setColour (juce::BubbleComponent::outlineColourId, Colours::graphite);
    setColour (juce::TooltipWindow::backgroundColourId, Colours::evilleBlack);
    setColour (juce::TooltipWindow::textColourId, Colours::screenText);
    setColour (juce::TooltipWindow::outlineColourId, Colours::graphite);
    setColour (juce::ScrollBar::thumbColourId, Colours::outline);
    setColour (juce::AlertWindow::backgroundColourId, Colours::panel);
    setColour (juce::AlertWindow::textColourId, Colours::text);
    setColour (juce::AlertWindow::outlineColourId, Colours::outline);
}

juce::Typeface::Ptr EvilleLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    // Anything asking for the default sans gets Satoshi (bold -> Bold, else Medium).
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        if (auto t = Fonts::satoshi (f.isBold() ? Fonts::bold : Fonts::medium))
            return t;
    return LookAndFeel_V4::getTypefaceForFont (f);
}

juce::Font EvilleLookAndFeel::getTextButtonFont (juce::TextButton&, int height)
{
    return Fonts::sans (std::min (14.0f, (float) height * 0.55f));
}

bool EvilleLookAndFeel::onBlack (const juce::Component& c)
{
    return c.getProperties()["surface"].toString() == "black";
}

juce::Rectangle<float> EvilleLookAndFeel::knobBounds (const juce::Slider& s)
{
    const auto b = s.getLocalBounds().toFloat();
    const auto r = s.getProperties().contains ("knobRadius")
                       ? (float) s.getProperties()["knobRadius"]
                       : std::min (b.getWidth(), b.getHeight()) * 0.5f - 10.0f;
    return juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (b.getCentre());
}

juce::Label* EvilleLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (Fonts::mono (s.getProperties()["bigReadout"] ? 14.0f : 12.0f));
    return l;
}

//==============================================================================
// Knobs (ROOM 1.0): a ring of fine ticks, a red value arc over it ending in a
// white dot, and a dark body with a white pointer. No glow. Line widths follow
// the knob's size so small knobs stay delicate.

void EvilleLookAndFeel::drawRotarySlider (juce::Graphics& g, int, int, int, int, float pos,
                                          float startAngle, float endAngle, juce::Slider& s)
{
    const auto kb = knobBounds (s);
    const auto c = kb.getCentre();
    const auto r = kb.getWidth() * 0.5f;
    const auto alpha = s.isEnabled() ? 1.0f : 0.45f;
    const auto small = r < 20.0f;
    auto pointAt = [&] (float angle, float radius)
    {
        return juce::Point<float> (c.x + radius * std::sin (angle), c.y - radius * std::cos (angle));
    };
    const auto a = startAngle + pos * (endAngle - startAngle);
    const auto ringR = r * 0.98f;

    if (small)
    {
        // Small knobs (wet, out): just the ring and the arc, WARP's knob A.
        juce::Path track;
        track.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, startAngle, endAngle, true);
        g.setColour (Colours::dustWhite.withAlpha (0.28f * alpha));
        g.strokePath (track, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else
    {
        // The tick ring
        const int ticks = 32;
        for (int i = 0; i <= ticks; ++i)
        {
            const auto t = startAngle + (endAngle - startAngle) * (float) i / (float) ticks;
            g.setColour (Colours::dustWhite.withAlpha ((i % 8 == 0 ? 0.42f : 0.24f) * alpha));
            g.drawLine ({ pointAt (t, ringR - r * 0.10f), pointAt (t, ringR) }, i % 8 == 0 ? 1.1f : 0.8f);
        }
        // The body: a dark disc with a soft top light and a hairline rim
        const auto body = juce::Rectangle<float> (r * 1.42f, r * 1.42f).withCentre (c);
        juce::ColourGradient grad (juce::Colour (0xff2c2828).withMultipliedAlpha (alpha), body.getX(), body.getY(),
                                   juce::Colour (0xff121010).withMultipliedAlpha (alpha), body.getRight(), body.getBottom(), false);
        g.setGradientFill (grad);
        g.fillEllipse (body);
        g.setColour (juce::Colours::white.withAlpha (0.08f * alpha));
        g.drawEllipse (body.reduced (0.5f), 1.0f);
        if (s.isMouseOverOrDragging() && s.isEnabled())
        {
            g.setColour (juce::Colours::white.withAlpha (0.04f));
            g.fillEllipse (body);
        }
        // The pointer
        g.setColour (Colours::dustWhite.withAlpha (0.92f * alpha));
        g.drawLine ({ pointAt (a, r * 0.08f), pointAt (a, r * 0.62f) }, std::max (1.6f, r * 0.045f));
    }

    // The value arc, from the start (or the centre, for bipolar knobs) to the value
    const auto bipolar = (bool) s.getProperties()["bipolar"];
    const auto from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    if (std::abs (a - from) > 0.01f)
    {
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, std::min (from, a), std::max (from, a), true);
        g.setColour ((small ? Colours::redLight : Colours::crimson.brighter (0.15f)).withAlpha (alpha));
        g.strokePath (arc, juce::PathStrokeType (small ? 2.2f : std::max (2.2f, r * 0.055f),
                                                 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    // The dot at the value
    const auto dot = small ? 3.2f : std::max (3.0f, r * 0.075f);
    g.setColour (Colours::dustWhite.withAlpha (alpha));
    g.fillEllipse (juce::Rectangle<float> (dot * 2.0f, dot * 2.0f).withCentre (pointAt (a, ringR)));
}

void EvilleLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                          juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
        return;
    }
    // A hairline track, red up to the thumb, a white dot as the thumb.
    const auto thumb = std::min (14.0f, (float) h - 4.0f);
    const auto cy = (float) y + (float) h * 0.5f;
    const auto left = (float) x + thumb * 0.5f, right = (float) (x + w) - thumb * 0.5f;
    g.setColour (Colours::dustWhite.withAlpha (0.18f));
    g.fillRoundedRectangle (juce::Rectangle<float> (left, cy - 1.0f, right - left, 2.0f), 1.0f);
    const auto px = juce::jlimit (left, right, pos);
    g.setColour (Colours::crimson.brighter (0.15f));
    g.fillRoundedRectangle (juce::Rectangle<float> (left, cy - 1.25f, px - left, 2.5f), 1.25f);
    g.setColour (Colours::dustWhite.withAlpha (s.isMouseOverOrDragging() ? 1.0f : 0.92f));
    g.fillEllipse (juce::Rectangle<float> (thumb, thumb).withCentre ({ px, cy }));
}

//==============================================================================
// Menus, tooltips, value bubbles

namespace
{
    void glassPanel (juce::Graphics& g, juce::Rectangle<float> r, float corner, bool withShadow)
    {
        // WARP's menus and dialogs: a flat dark panel with a white hairline edge.
        if (withShadow)
        {
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (r.translated (0.0f, 3.0f).expanded (2.0f), corner + 2.0f);
        }
        g.setColour (juce::Colour (0xff161414));
        g.fillRoundedRectangle (r, corner);
        g.setColour (juce::Colours::white.withAlpha (0.14f));
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    }
}

void EvilleLookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& w, const juce::Rectangle<int>& textArea,
                                      juce::TextLayout& layout)
{
    g.fillAll (juce::Colours::transparentBlack);
    glassPanel (g, w.getLocalBounds().toFloat().reduced (6.0f), 10.0f, true);
    layout.draw (g, textArea.toFloat());
}

juce::Array<int> EvilleLookAndFeel::getWidthsForTextButtons (juce::AlertWindow&, const juce::Array<juce::TextButton*>& buttons)
{
    juce::Array<int> widths;
    for (auto* b : buttons)
        widths.add (std::max (96, juce::GlyphArrangement::getStringWidthInt (Fonts::sans (13.0f), b->getButtonText()) + 40));
    return widths;
}

namespace
{
    // A dialog drawn inside the plugin: dims the window, black glass panel in the middle.
    class RoomDialog : public juce::Component
    {
    public:
        RoomDialog (const juce::String& t, const juce::String& m, const juce::StringArray& buttonNames, const juce::String& hint)
            : title (t), message (m)
        {
            setWantsKeyboardFocus (true);
            if (hint.isNotEmpty())
            {
                field = std::make_unique<juce::TextEditor>();
                field->setTextToShowWhenEmpty (hint, Colours::screenDim);
                field->setFont (Fonts::sans (14.0f));
                field->setColour (juce::TextEditor::backgroundColourId, Colours::glass);
                field->setColour (juce::TextEditor::textColourId, Colours::screenText);
                field->setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff050505));
                field->setColour (juce::TextEditor::focusedOutlineColourId, Colours::crimson);
                field->setColour (juce::CaretComponent::caretColourId, Colours::crimson);
                field->setIndents (10, 8);
                field->onReturnKey = [this] { exitModalState (1); };
                field->onEscapeKey = [this] { exitModalState (0); };
                addAndMakeVisible (*field);
            }
            for (int i = 0; i < buttonNames.size(); ++i)
            {
                auto* b = buttons.add (new juce::TextButton (buttonNames[i]));
                b->getProperties().set ("key", "dark");
                b->onClick = [this, i] { exitModalState (i + 1); };
                addAndMakeVisible (b);
            }
        }

        juce::String getText() const { return field != nullptr ? field->getText() : juce::String(); }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colours::black.withAlpha (0.45f));
            glassPanel (g, panel.toFloat(), 10.0f, true);
            auto r = panel.reduced (28, 24);
            g.setColour (Colours::dustWhite);
            g.setFont (Fonts::sans (19.0f, Fonts::bold));
            g.drawText (title, r.removeFromTop (26), juce::Justification::centredLeft);
            r.removeFromTop (10);
            layout.draw (g, r.removeFromTop ((int) std::ceil (layout.getHeight())).toFloat());
        }

        void resized() override
        {
            const int w = std::min (440, getWidth() - 40);
            juce::AttributedString text;
            text.append (message, Fonts::sans (14.5f), Colours::dustWhite.withAlpha (0.86f));
            text.setLineSpacing (3.0f);
            layout.createLayout (text, (float) (w - 56));
            const int h = 24 + 26 + 10 + (int) std::ceil (layout.getHeight()) + (field != nullptr ? 14 + 38 : 0) + 24 + 38 + 24;
            panel = juce::Rectangle<int> (w, h).withCentre (getLocalBounds().getCentre());
            auto r = panel.reduced (28, 24);
            r.removeFromTop (26 + 10 + (int) std::ceil (layout.getHeight()));
            if (field != nullptr)
            {
                r.removeFromTop (14);
                field->setBounds (r.removeFromTop (38));
            }
            auto row = r.removeFromBottom (38);
            for (int i = buttons.size(); --i >= 0;)   // right aligned, first (default) button on the right
            {
                const auto bw = std::max (96, juce::GlyphArrangement::getStringWidthInt (Fonts::sans (13.0f), buttons[i]->getButtonText()) + 40);
                buttons[buttons.size() - 1 - i]->setBounds (row.removeFromRight (bw));
                row.removeFromRight (10);
            }
        }

        bool keyPressed (const juce::KeyPress& k) override
        {
            if (k == juce::KeyPress::escapeKey) { exitModalState (0); return true; }
            if (k == juce::KeyPress::returnKey) { exitModalState (1); return true; }
            return true;   // modal: nothing leaks through
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (! panel.contains (e.getPosition()))   // click outside: cancel
                exitModalState (0);
        }

    private:
        juce::String title, message;
        std::unique_ptr<juce::TextEditor> field;
        juce::OwnedArray<juce::TextButton> buttons;
        juce::TextLayout layout;
        juce::Rectangle<int> panel;
    };
}

void EvilleLookAndFeel::showDialog (juce::Component& parent, const juce::String& title, const juce::String& message,
                                    const juce::StringArray& buttons, const juce::String& textHint,
                                    std::function<void (int, const juce::String&)> callback)
{
    auto* d = new RoomDialog (title, message, buttons, textHint);
    d->setBounds (parent.getLocalBounds());
    parent.addAndMakeVisible (d);
    d->toFront (true);
    d->enterModalState (true, juce::ModalCallbackFunction::create ([d, callback] (int result)
    {
        if (callback)
            callback (result, d->getText());
    }), true);
    for (auto* c : d->getChildren())
        if (auto* ed = dynamic_cast<juce::TextEditor*> (c))
            ed->grabKeyboardFocus();
}

void EvilleLookAndFeel::showMenu (juce::PopupMenu& m, juce::Component& target)
{
    m.setLookAndFeel (&target.getLookAndFeel());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target).withStandardItemHeight (30));
}

void EvilleLookAndFeel::drawPopupMenuBackgroundWithOptions (juce::Graphics& g, int w, int h, const juce::PopupMenu::Options&)
{
    g.fillAll (juce::Colours::transparentBlack);
    glassPanel (g, juce::Rectangle<float> ((float) w, (float) h).reduced ((float) menuMargin), 5.0f, true);
}

void EvilleLookAndFeel::getIdealPopupMenuItemSizeWithOptions (const juce::String& text, bool isSeparator, int,
                                                              int& idealWidth, int& idealHeight, const juce::PopupMenu::Options&)
{
    if (isSeparator)
    {
        idealWidth = 60;
        idealHeight = 11;
        return;
    }
    idealHeight = 26;
    idealWidth = juce::GlyphArrangement::getStringWidthInt (getPopupMenuFont(), text) + 56;
}

void EvilleLookAndFeel::drawPopupMenuSectionHeaderWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area,
                                                               const juce::String& name, const juce::PopupMenu::Options&)
{
    g.setFont (Fonts::sans (11.0f, Fonts::bold).withExtraKerningFactor (0.12f));
    g.setColour (Colours::screenDim);
    g.drawText (name.toLowerCase(), area.toFloat().withTrimmedLeft (16.0f).withTrimmedTop (6.0f), juce::Justification::centredLeft);   // in line with the items
}

void EvilleLookAndFeel::drawPopupMenuItemWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area, bool highlighted,
                                                      const juce::PopupMenu::Item& item, const juce::PopupMenu::Options&)
{
    auto r = area.toFloat();
    if (item.isSeparator)
    {
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRect (juce::Rectangle<float> (r.getX() + 8.0f, r.getCentreY(), r.getWidth() - 16.0f, 1.0f));
        return;
    }
    if (item.customComponent != nullptr)
        return;

    const auto enabled = item.isEnabled;
    if (highlighted && enabled)
    {
        g.setColour (juce::Colour (0xff262222));
        g.fillRoundedRectangle (r.reduced (6.0f, 1.0f), 4.0f);
    }

    // Ticked: a small red dot at the right (WARP's menus keep the text flush left).
    const auto dot = juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ r.getRight() - 16.0f, r.getCentreY() });
    if (item.isTicked)
    {
        g.setColour (Colours::crimson.brighter (0.2f));
        g.fillEllipse (dot);
    }
    auto text = r.withTrimmedLeft (16.0f).withTrimmedRight (item.isTicked ? 26.0f : 14.0f);
    if (item.subMenu != nullptr)
    {
        const auto c = juce::Point<float> (text.getRight() - 4.0f, r.getCentreY());
        juce::Path chevron;
        chevron.startNewSubPath (c.x - 3.0f, c.y - 4.5f);
        chevron.lineTo (c.x + 1.5f, c.y);
        chevron.lineTo (c.x - 3.0f, c.y + 4.5f);
        g.setColour (Colours::screenDim.withAlpha (enabled ? 1.0f : 0.4f));
        g.strokePath (chevron, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        text.removeFromRight (14.0f);
    }
    if (item.shortcutKeyDescription.isNotEmpty())
    {
        g.setFont (Fonts::mono (11.5f));
        g.setColour (Colours::screenDim);
        g.drawText (item.shortcutKeyDescription, text, juce::Justification::centredRight);
    }
    g.setFont (getPopupMenuFont());
    g.setColour ((item.colour.isTransparent() ? Colours::dustWhite : item.colour)
                     .withAlpha (! enabled ? 0.35f : highlighted ? 1.0f : 0.86f));
    g.drawFittedText (item.text, text.toNearestInt(), juce::Justification::centredLeft, 1);
}

juce::Rectangle<int> EvilleLookAndFeel::getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    const auto font = Fonts::sans (13.0f);
    const auto w = std::min (320, juce::GlyphArrangement::getStringWidthInt (font, text) + 26);
    const auto lines = juce::GlyphArrangement::getStringWidthInt (font, text) + 26 > 320 ? 2 : 1;
    const auto h = 14 + lines * 17;
    auto r = juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 12,
                                   pos.y > parent.getCentreY() ? pos.y - (h + 8) : pos.y + 14, w, h);
    return r.constrainedWithin (parent);
}

void EvilleLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.fillAll (juce::Colour (0xff121212));
    glassPanel (g, juce::Rectangle<float> ((float) w, (float) h), 4.0f, false);
    g.setFont (Fonts::sans (13.0f));
    g.setColour (Colours::screenText);
    g.drawFittedText (text, juce::Rectangle<int> (w, h).reduced (12, 5), juce::Justification::centredLeft, 2);
}

void EvilleLookAndFeel::drawBubble (juce::Graphics& g, juce::BubbleComponent&, const juce::Point<float>& tip,
                                    const juce::Rectangle<float>& body)
{
    // The value readout above a knob: a small black glass tag with a pointer.
    juce::Path p;
    p.addBubble (body.reduced (0.5f), body.getUnion (juce::Rectangle<float> (tip.x, tip.y, 1.0f, 1.0f)), tip, 5.0f,
                 std::min (12.0f, body.getWidth() * 0.4f));
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillPath (p, juce::AffineTransform::translation (0.0f, 2.0f));
    g.setColour (juce::Colour (0xff121212));
    g.fillPath (p);
    g.setColour (juce::Colours::black);
    g.strokePath (p, juce::PathStrokeType (1.0f));
    g.setColour (juce::Colours::white.withAlpha (0.09f));
    g.drawLine (body.getX() + 5.0f, body.getY() + 1.5f, body.getRight() - 5.0f, body.getY() + 1.5f, 1.0f);
}

//==============================================================================
// Keys and toggles

void EvilleLookAndFeel::drawChip (juce::Graphics& g, juce::Rectangle<float> r, bool on, bool over, bool down)
{
    // WARP's style A: a soft dark key. Chosen: its edge turns red (the text too,
    // drawn by the caller); nothing moves.
    const auto corner = std::min (8.0f, r.getHeight() * 0.3f);
    auto fill = Colours::chip;
    if (over) fill = fill.brighter (0.06f);
    if (down) fill = fill.darker (0.12f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, corner);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawLine (r.getX() + corner, r.getY() + 1.0f, r.getRight() - corner, r.getY() + 1.0f, 1.0f);
    g.setColour (on ? Colours::crimson.brighter (0.1f) : Colours::chipEdge.withAlpha (0.8f));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, on ? 1.3f : 1.0f);
}

void EvilleLookAndFeel::drawCaps (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, float height,
                                  juce::Colour colour, juce::Justification just, Fonts::Weight weight)
{
    // Small caps titles, letterspaced about 0.18 em (WARP).
    g.setFont (Fonts::sans (height, weight).withExtraKerningFactor (0.18f));
    g.setColour (colour);
    g.drawText (text.toUpperCase(), r, just, false);
}

void EvilleLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg,
                                              bool over, bool down)
{
    const auto key = b.getProperties()["key"].toString();
    if (key.isNotEmpty())
    {
        drawChip (g, b.getLocalBounds().toFloat().reduced (1.0f), b.getToggleState(), over, down);
        return;
    }
    if (bg.isTransparent() && ! b.getToggleState())
        return;   // a bare glyph button (e.g. < > in the preset window)
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto col = bg;
    if (b.getToggleState())
        col = b.findColour (juce::TextButton::buttonOnColourId);
    if (over) col = col.brighter (0.08f);
    if (down) col = col.darker (0.1f);
    g.setColour (col);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (Colours::chipEdge);
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

void EvilleLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool down)
{
    const auto key = b.getProperties()["key"].toString();
    if (key.isEmpty())
    {
        LookAndFeel_V4::drawButtonText (g, b, over, down);
        return;
    }
    const auto on = b.getToggleState();
    drawCaps (g, b.getButtonText(), b.getLocalBounds().toFloat(), 12.0f,
              (on ? Colours::redLight : Colours::dustWhite).withAlpha (b.isEnabled() ? (over || on ? 1.0f : 0.9f) : 0.4f),
              juce::Justification::centred, Fonts::bold);
}

void EvilleLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool down)
{
    if (b.getProperties()["style"].toString() == "lightkey" || b.getProperties()["style"].toString() == "chip")
    {
        // A chip that turns red while on (auto).
        const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        drawChip (g, r, b.getToggleState(), over, down);
        drawCaps (g, b.getButtonText(), r, 11.0f, b.getToggleState() ? Colours::redLight : Colours::dustWhite.withAlpha (0.85f),
                  juce::Justification::centred, Fonts::bold);
        return;
    }
    if (b.getProperties()["style"].toString() == "minikey")
    {
        auto r = b.getLocalBounds().toFloat();
        const auto key = r.removeFromLeft (std::min (r.getHeight(), 30.0f)).withSizeKeepingCentre (r.getHeight(), r.getHeight());
        Hw::drawMiniKey (g, key, b.getToggleState(), over, down);
        g.setFont (Fonts::sans (13.5f));
        g.setColour (Colours::dustWhite.withAlpha (over || b.getToggleState() ? 1.0f : 0.82f));
        g.drawText (b.getButtonText(), r.withTrimmedLeft (7.0f), juce::Justification::centredLeft);
        return;
    }
    if (b.getProperties()["style"].toString() != "led")
    {
        LookAndFeel_V4::drawToggleButton (g, b, over, down);
        return;
    }
    const auto dark = onBlack (b);
    auto r = b.getLocalBounds().toFloat();
    const auto ledArea = r.removeFromLeft (14.0f);
    Hw::drawLed (g, ledArea.getCentre(), b.getToggleState(), 3.5f);
    g.setFont (Fonts::sans (12.5f));
    g.setColour ((dark ? Colours::dustWhite : Colours::ink).withAlpha (over ? 1.0f : 0.8f));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (3.0f), juce::Justification::centredLeft);
}
