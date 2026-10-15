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

namespace
{
    juce::Typeface::Ptr loadTypeface (const char* file)
    {
        int size = 0;
        if (auto* data = UiAssetFiles::get (file, size))
            return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
        return nullptr;
    }
}

juce::Typeface::Ptr Fonts::satoshi (Weight w)
{
    static juce::Typeface::Ptr faces[4] = { loadTypeface ("Satoshi-Regular.otf"), loadTypeface ("Satoshi-Medium.otf"),
                                            loadTypeface ("Satoshi-Bold.otf"), loadTypeface ("Satoshi-Black.otf") };
    return faces[(int) w];
}

juce::Font Fonts::sans (float height, Weight w)
{
    if (auto t = satoshi (w))
        return juce::Font (juce::FontOptions (t).withHeight (height));
    return juce::Font (juce::FontOptions (height, w >= bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font Fonts::serif (float height)
{
    static juce::Typeface::Ptr face = loadTypeface ("CormorantGaramond-MediumItalic.ttf");
    if (face != nullptr)
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
// Knobs: printed tick scale, black knurled body, white pointer

void EvilleLookAndFeel::drawRotarySlider (juce::Graphics& g, int, int, int, int, float pos,
                                          float startAngle, float endAngle, juce::Slider& s)
{
    const auto dark = onBlack (s);
    const auto kb = knobBounds (s);
    const auto c = kb.getCentre();
    const auto r = kb.getWidth() * 0.5f;
    const auto alpha = s.isEnabled() ? 1.0f : 0.8f;
    const auto printCol = (dark ? Colours::dim : Colours::ink).withMultipliedAlpha (alpha);
    auto pointAt = [&] (float angle, float radius)
    {
        return juce::Point<float> (c.x + radius * std::sin (angle), c.y - radius * std::cos (angle));
    };

    // Printed scale: 11 ticks, every fifth one longer.
    if (! s.getProperties()["noScale"])
    {
        for (int i = 0; i <= 10; ++i)
        {
            const auto a = startAngle + (endAngle - startAngle) * (float) i / 10.0f;
            const auto major = i % 5 == 0;
            g.setColour (printCol.withMultipliedAlpha (major ? 0.9f : 0.55f));
            g.drawLine ({ pointAt (a, r + 5.0f), pointAt (a, r + (major ? 11.0f : 8.5f)) }, major ? 1.3f : 1.0f);
        }
        g.setFont (Fonts::mono (std::max (8.5f, r * 0.24f)));
        const juce::Identifier ids[3] = { "scaleMin", "scaleMid", "scaleMax" };
        const float at[3] = { 0.0f, 0.5f, 1.0f };
        for (int i = 0; i < 3; ++i)
            if (auto t = s.getProperties()[ids[i]].toString(); t.isNotEmpty())
            {
                const auto a = startAngle + (endAngle - startAngle) * at[i];
                const auto p = pointAt (a, r + (i == 1 ? 21.0f : 22.0f));
                g.setColour (printCol.withMultipliedAlpha (0.85f));
                g.drawText (t, juce::Rectangle<float> (40.0f, 12.0f).withCentre (p.translated (i == 0 ? -6.0f : i == 2 ? 6.0f : 0.0f, 0.0f)),
                            juce::Justification::centred);
            }
    }

    // The knob itself: a photographed knurled cap (made in Higgsfield), one for
    // the white console and one for the black modules. The photo carries its own
    // contact shadow. It doesn't rotate (its light stays top-left); the pointer does.
    static const auto paperKnob = Hw::asset ("knob_paper.png");
    static const auto blackKnob = Hw::asset ("knob_black.png");
    if (const auto& knobImg = dark ? blackKnob : paperKnob; knobImg.isValid())
    {
        const auto half = r / (dark ? 0.92f : 0.875f);
        g.setOpacity (alpha);
        g.drawImage (knobImg, juce::Rectangle<float> (half * 2.0f, half * 2.0f).withCentre (c),
                     juce::RectanglePlacement::stretchToFit);
        if (s.isMouseOverOrDragging() && s.isEnabled())
        {
            g.setColour (juce::Colours::white.withAlpha (0.035f));
            g.fillEllipse (kb.reduced (r * 0.1f));
        }
        // Pointer: a white line inlaid in the cap, with a hairline of shadow under it.
        const auto a = startAngle + pos * (endAngle - startAngle);
        const auto w = std::max (1.8f, r * 0.075f);
        const juce::Line<float> line (pointAt (a, r * 0.30f), pointAt (a, r * 0.80f));
        g.setColour (juce::Colours::black.withAlpha (0.6f * alpha));
        g.drawLine (line.withShortenedStart (-0.5f).withShortenedEnd (-0.5f), w + 1.4f);
        g.setColour (juce::Colour (0xfff2efe8).withAlpha (0.96f * alpha));
        g.drawLine (line, w);
        g.setColour (juce::Colours::white.withAlpha (0.5f * alpha));
        g.drawLine ({ line.getStart().translated (-w * 0.2f, -w * 0.2f), line.getEnd().translated (-w * 0.2f, -w * 0.2f) }, w * 0.35f);
        return;
    }

    // Knurled skirt
    juce::ColourGradient skirt (juce::Colour (0xff2c2c2c), c.x - r, c.y - r, juce::Colour (0xff050505), c.x + r, c.y + r, false);
    g.setGradientFill (skirt);
    g.fillEllipse (kb);
    g.setColour (juce::Colour (0xff3a3a3a).withAlpha (0.55f * alpha));
    const int knurls = 56;
    for (int i = 0; i < knurls; ++i)
    {
        const auto a = juce::MathConstants<float>::twoPi * (float) i / (float) knurls;
        g.drawLine ({ pointAt (a, r * 0.86f), pointAt (a, r * 0.99f) }, 0.8f);
    }

    // Cap
    const auto cap = kb.reduced (r * 0.13f);
    juce::ColourGradient capGrad (juce::Colour (0xff2f2f2f), cap.getX(), cap.getY(),
                                  juce::Colour (0xff0b0b0b), cap.getRight(), cap.getBottom(), false);
    g.setGradientFill (capGrad);
    g.fillEllipse (cap);
    g.setColour (juce::Colours::white.withAlpha (0.07f * alpha));
    g.drawEllipse (cap.reduced (0.5f), 1.0f);
    juce::Path sheen;
    sheen.addCentredArc (c.x, c.y, r * 0.74f, r * 0.74f, 0.0f, -2.2f, -0.6f, true);
    g.setColour (juce::Colours::white.withAlpha (0.10f * alpha));
    g.strokePath (sheen, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Pointer
    const auto a = startAngle + pos * (endAngle - startAngle);
    g.setColour (Colours::dustWhite.withAlpha (0.95f * alpha));
    g.drawLine ({ pointAt (a, r * 0.18f), pointAt (a, r * 0.82f) }, std::max (2.0f, r * 0.07f));
}

void EvilleLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                          juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
        return;
    }
    // A slot cut into the panel, crimson up to the thumb, and a small black knob as the thumb.
    const auto thumb = std::min (22.0f, (float) h - 4.0f);
    const auto cy = (float) y + (float) h * 0.5f;
    const auto left = (float) x + thumb * 0.5f, right = (float) (x + w) - thumb * 0.5f;
    const auto slot = juce::Rectangle<float> (left - 3.0f, cy - 3.5f, right - left + 6.0f, 7.0f);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.fillRoundedRectangle (slot.translated (0.0f, 1.0f), 3.5f);
    g.setColour (juce::Colour (0xff050505));
    g.fillRoundedRectangle (slot, 3.5f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (slot.reduced (0.5f).translated (0.0f, 0.5f), 3.0f, 1.0f);
    const auto px = juce::jlimit (left, right, pos);
    g.setColour (Colours::crimson);
    g.fillRoundedRectangle (juce::Rectangle<float> (slot.getX() + 2.0f, cy - 1.5f, px - slot.getX() - 2.0f, 3.0f), 1.5f);

    const auto tb = juce::Rectangle<float> (thumb, thumb).withCentre ({ px, cy });
    static const auto knobImg = Hw::asset ("knob_black.png");
    if (knobImg.isValid())
    {
        g.setOpacity (1.0f);
        g.drawImage (knobImg, tb.expanded (thumb * 0.06f), juce::RectanglePlacement::stretchToFit);
    }
    else
    {
        g.setColour (juce::Colour (0xff1a1a1a));
        g.fillEllipse (tb);
    }
    g.setColour (juce::Colour (0xfff2efe8).withAlpha (s.isMouseOverOrDragging() ? 1.0f : 0.9f));
    g.drawLine (px, tb.getY() + thumb * 0.16f, px, tb.getY() + thumb * 0.46f, 1.8f);
}

//==============================================================================
// Menus, tooltips, value bubbles

namespace
{
    void glassPanel (juce::Graphics& g, juce::Rectangle<float> r, float corner, bool withShadow)
    {
        if (withShadow)   // soft shadow, falling down like every other shadow on the unit
            for (int i = 0; i < 6; ++i)
            {
                g.setColour (juce::Colours::black.withAlpha (0.055f));
                g.fillRoundedRectangle (r.translated (0.0f, 3.0f).expanded ((float) i * 1.4f), corner + (float) i * 1.4f);
            }
        juce::ColourGradient body (juce::Colour (0xff1d1d1d), 0.0f, r.getY(), juce::Colour (0xff0e0e0e), 0.0f, r.getBottom(), false);
        g.setGradientFill (body);
        g.fillRoundedRectangle (r, corner);
        g.setColour (juce::Colours::black);
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.10f));   // lit top lip
        g.drawLine (r.getX() + corner, r.getY() + 1.5f, r.getRight() - corner, r.getY() + 1.5f, 1.0f);
    }
}

void EvilleLookAndFeel::showMenu (juce::PopupMenu& m, juce::Component& target)
{
    m.setLookAndFeel (&target.getLookAndFeel());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target).withStandardItemHeight (30));
}

void EvilleLookAndFeel::drawPopupMenuBackgroundWithOptions (juce::Graphics& g, int w, int h, const juce::PopupMenu::Options&)
{
    g.fillAll (juce::Colours::transparentBlack);
    glassPanel (g, juce::Rectangle<float> ((float) w, (float) h).reduced ((float) menuMargin), 9.0f, true);
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
    idealHeight = 30;
    idealWidth = juce::GlyphArrangement::getStringWidthInt (getPopupMenuFont(), text) + 64;
}

void EvilleLookAndFeel::drawPopupMenuSectionHeaderWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area,
                                                               const juce::String& name, const juce::PopupMenu::Options&)
{
    g.setFont (Fonts::sans (11.0f, Fonts::bold).withExtraKerningFactor (0.12f));
    g.setColour (Colours::screenDim);
    g.drawText (name.toLowerCase(), area.toFloat().withTrimmedLeft (20.0f).withTrimmedTop (6.0f), juce::Justification::centredLeft);
}

void EvilleLookAndFeel::drawPopupMenuItemWithOptions (juce::Graphics& g, const juce::Rectangle<int>& area, bool highlighted,
                                                      const juce::PopupMenu::Item& item, const juce::PopupMenu::Options&)
{
    auto r = area.toFloat();
    if (item.isSeparator)
    {
        Hw::drawGroove (g, { r.getX() + 14.0f, r.getCentreY() }, { r.getRight() - 14.0f, r.getCentreY() }, true);
        return;
    }
    if (item.customComponent != nullptr)
        return;

    const auto enabled = item.isEnabled;
    if (highlighted && enabled)
    {
        const auto pill = r.reduced (5.0f, 2.0f);
        g.setColour (juce::Colour (0xff2a2a2a));
        g.fillRoundedRectangle (pill, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawLine (pill.getX() + 6.0f, pill.getY() + 0.8f, pill.getRight() - 6.0f, pill.getY() + 0.8f, 1.0f);
        g.setColour (Colours::crimson);
        g.fillRoundedRectangle (pill.getX() + 1.0f, pill.getY() + 6.0f, 2.5f, pill.getHeight() - 12.0f, 1.25f);
    }

    // Ticked: a lit crimson dot where a check would be
    const auto dot = juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ r.getX() + 19.0f, r.getCentreY() });
    if (item.isTicked)
    {
        g.setColour (Colours::crimson.withAlpha (0.3f));
        g.fillEllipse (dot.expanded (3.5f));
        g.setColour (Colours::crimson.brighter (0.2f));
        g.fillEllipse (dot);
    }
    else if (highlighted && enabled)
    {
        g.setColour (Colours::screenDim.withAlpha (0.5f));
        g.drawEllipse (dot.reduced (0.5f), 1.0f);
    }

    auto text = r.withTrimmedLeft (32.0f).withTrimmedRight (14.0f);
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

void EvilleLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg,
                                              bool over, bool down)
{
    const auto key = b.getProperties()["key"].toString();
    if (key.isNotEmpty())
    {
        Hw::drawKey (g, b.getLocalBounds().toFloat().reduced (1.0f), key == "light", over, down || b.getToggleState());
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
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

void EvilleLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool over, bool down)
{
    const auto key = b.getProperties()["key"].toString();
    if (key.isEmpty())
    {
        LookAndFeel_V4::drawButtonText (g, b, over, down);
        return;
    }
    g.setFont (Fonts::sans (13.0f));
    g.setColour (key == "light" ? Colours::ink : Colours::dustWhite);
    g.drawText (b.getButtonText(), b.getLocalBounds().translated (0, down ? 1 : 0), juce::Justification::centred);
}

void EvilleLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool down)
{
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
