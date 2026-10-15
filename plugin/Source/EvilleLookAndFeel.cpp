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
    setColour (juce::PopupMenu::backgroundColourId, Colours::panel);
    setColour (juce::PopupMenu::textColourId, Colours::text);
    setColour (juce::PopupMenu::headerTextColourId, Colours::dim);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::crimson.withAlpha (0.6f));
    setColour (juce::PopupMenu::highlightedTextColourId, Colours::dustWhite);
    setColour (juce::BubbleComponent::backgroundColourId, Colours::evilleBlack);
    setColour (juce::BubbleComponent::outlineColourId, Colours::graphite);
    setColour (juce::TooltipWindow::backgroundColourId, Colours::evilleBlack);
    setColour (juce::TooltipWindow::textColourId, Colours::dustWhite);
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
    l->setFont (Fonts::mono (12.0f));
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
    const auto cy = (float) y + (float) h * 0.5f;
    g.setColour (juce::Colour (0xff3c3c3c));
    g.fillRoundedRectangle ((float) x, cy - 1.5f, (float) w, 3.0f, 1.5f);
    g.setColour (Colours::crimson);
    g.fillRoundedRectangle ((float) x, cy - 1.5f, pos - (float) x, 3.0f, 1.5f);
    g.setColour (Colours::dustWhite);
    g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre ({ pos, cy }));
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
