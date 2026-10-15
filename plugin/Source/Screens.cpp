#include "Screens.h"
#include "UiColours.h"
#include "EvilleLookAndFeel.h"

juce::Rectangle<float> Hw::drawScreen (juce::Graphics& g, juce::Rectangle<float> b)
{
    // A window cut into the panel: light catches the lower lip of the cut,
    // the top of the recess is in shadow.
    g.setColour (juce::Colours::white.withAlpha (0.28f));
    g.fillRoundedRectangle (b.translated (0.0f, 1.0f), 7.5f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (b.translated (0.0f, -0.6f), 7.5f);

    // Bezel: black anodised frame, brighter at the top edge
    juce::ColourGradient bezel (juce::Colour (0xff2e2e2e), 0.0f, b.getY(), juce::Colour (0xff0c0c0c), 0.0f, b.getBottom(), false);
    g.setGradientFill (bezel);
    g.fillRoundedRectangle (b, 7.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawLine (b.getX() + 7.0f, b.getY() + 1.0f, b.getRight() - 7.0f, b.getY() + 1.0f, 1.0f);

    // Glass, sunk into the bezel
    const auto glass = b.reduced (4.5f);
    g.setColour (Colours::glass);
    g.fillRoundedRectangle (glass, 4.5f);
    juce::ColourGradient vig (juce::Colour (0xff181818), glass.getCentreX(), glass.getCentreY(),
                              Colours::glass.withAlpha (0.0f), glass.getX(), glass.getY(), true);
    g.setGradientFill (vig);
    g.fillRoundedRectangle (glass, 4.5f);
    return glass;
}

void Hw::drawScreenOverlay (juce::Graphics& g, juce::Rectangle<float> glass)
{
    g.saveState();
    g.reduceClipRegion (glass.toNearestInt());
    g.setColour (juce::Colours::black.withAlpha (0.12f));
    for (auto y = glass.getY(); y < glass.getBottom(); y += 3.0f)
        g.fillRect (glass.getX(), y, glass.getWidth(), 1.0f);

    // A broad, soft reflection across the glass from the top left
    juce::Path sheen;
    sheen.startNewSubPath (glass.getX(), glass.getY());
    sheen.lineTo (glass.getX() + glass.getWidth() * 0.62f, glass.getY());
    sheen.lineTo (glass.getX() + glass.getWidth() * 0.30f, glass.getBottom());
    sheen.lineTo (glass.getX(), glass.getBottom());
    sheen.closeSubPath();
    juce::ColourGradient refl (juce::Colours::white.withAlpha (0.045f), glass.getX(), glass.getY(),
                               juce::Colours::white.withAlpha (0.0f), glass.getX() + glass.getWidth() * 0.4f, glass.getBottom(), false);
    g.setGradientFill (refl);
    g.fillPath (sheen);

    // Inner shadow: the bezel's edge throws a little shade onto the glass.
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.32f - 0.07f * (float) i));
        g.drawRoundedRectangle (glass.reduced (0.5f + (float) i).translated (0.0f, (float) i * 0.5f), 4.5f, 1.0f);
    }
    g.restoreState();
}

void Hw::strokeGlow (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width, float glow)
{
    if (glow > 0.0f)
    {
        g.setColour (c.withMultipliedAlpha (0.10f * glow));
        g.strokePath (p, juce::PathStrokeType (width * 5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c.withMultipliedAlpha (0.22f * glow));
        g.strokePath (p, juce::PathStrokeType (width * 2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Image Hw::asset (const char* fileName)
{
    int size = 0;
    if (auto* data = UiAssetFiles::get (fileName, size))
        return juce::ImageCache::getFromMemory (data, size);
    return {};
}

namespace
{
    // Draws a photographed part so that `partSize` (the part itself) lands on `area`;
    // fraction = how much of the photo the part takes up.
    void drawPart (juce::Graphics& g, const juce::Image& img, juce::Point<float> centre, float partSize, float fraction,
                   bool flipVertically = false)
    {
        const auto s = partSize / fraction;
        const auto r = juce::Rectangle<float> (s, s).withCentre (centre);
        g.setOpacity (1.0f);
        auto t = juce::AffineTransform::scale (s / (float) img.getWidth(), s / (float) img.getHeight());
        if (flipVertically)
            t = juce::AffineTransform::scale (1.0f, -1.0f).translated (0.0f, (float) img.getHeight()).followedBy (t);
        g.drawImageTransformed (img, t.translated (r.getX(), r.getY()), false);
    }
}

void Hw::drawKey (juce::Graphics& g, juce::Rectangle<float> r, bool light, bool over, bool down)
{
    // Photographed key caps set into the panel (made in Higgsfield). The photo
    // carries the key well and the cap's own contact shadow.
    static const auto paperKey = asset ("key_paper.png");
    static const auto blackKey = asset ("key_black.png");
    const auto& img = light ? paperKey : blackKey;
    if (img.isValid())
    {
        const auto s = std::min (r.getWidth(), r.getHeight());
        const auto cap = juce::Rectangle<float> (s * 0.78f, s * 0.78f).withCentre (r.getCentre());
        if (r.getWidth() > r.getHeight() * 1.15f || r.getHeight() > r.getWidth() * 1.15f)
        {
            // A wide key: stretch the photo by slicing it into nine pieces.
            const auto iw = img.getWidth(), ih = img.getHeight(), e = iw * 3 / 10;
            const auto scale = s / (float) ih, ed = e * scale;
            const float xs[] = { r.getX(), r.getX() + ed, r.getRight() - ed, r.getRight() };
            const float ys[] = { r.getY(), r.getY() + ed, r.getBottom() - ed, r.getBottom() };
            const int sx[] = { 0, e, iw - e, iw }, sy[] = { 0, e, ih - e, ih };
            g.setOpacity (1.0f);
            for (int yi = 0; yi < 3; ++yi)
                for (int xi = 0; xi < 3; ++xi)
                    g.drawImage (img, xs[xi], ys[yi], xs[xi + 1] - xs[xi], ys[yi + 1] - ys[yi],
                                 sx[xi], sy[yi], sx[xi + 1] - sx[xi], sy[yi + 1] - sy[yi]);
            const auto wide = keyCap (r);
            if (over || down)
            {
                g.setColour ((down ? juce::Colours::black : juce::Colours::white).withAlpha (down ? (light ? 0.10f : 0.25f) : (light ? 0.10f : 0.05f)));
                g.fillRoundedRectangle (wide.reduced (s * 0.05f), s * 0.08f);
            }
            return;
        }

        drawPart (g, img, r.getCentre(), s, 1.0f);
        if (over || down)
        {
            // pressed: the cap sinks a touch and catches less light
            g.setColour ((down ? juce::Colours::black : juce::Colours::white).withAlpha (down ? (light ? 0.10f : 0.25f) : (light ? 0.10f : 0.05f)));
            g.fillRoundedRectangle (cap.reduced (s * 0.03f), s * 0.08f);
        }
        return;
    }

    const auto lift = down ? 0.8f : 2.2f;
    g.setColour (juce::Colours::black.withAlpha (light ? 0.22f : 0.5f));
    g.fillRoundedRectangle (r.translated (0.0f, lift), 6.0f);
    juce::ColourGradient grad (light ? juce::Colour (0xfff6f4ef) : juce::Colour (0xff2a2a2a), 0.0f, r.getY(),
                               light ? juce::Colour (0xffe2ded5) : juce::Colour (0xff141414), 0.0f, r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r.withTrimmedBottom (lift), 6.0f);
}

void Hw::drawGlowKey (juce::Graphics& g, juce::Rectangle<float> r, bool lit, bool over, bool down)
{
    // A translucent crimson key that lights from inside (made in Higgsfield).
    static const auto onImg = asset ("key_glow_on.png");
    static const auto offImg = asset ("key_glow_off.png");
    const auto s = std::min (r.getWidth(), r.getHeight());
    const auto cap = juce::Rectangle<float> (s * 0.78f, s * 0.78f).withCentre (r.getCentre());
    if (const auto& img = lit ? onImg : offImg; img.isValid())
        drawPart (g, img, r.getCentre(), s, 1.0f);
    else
    {
        g.setColour (lit ? Colours::crimson : juce::Colour (0xff3a1414));
        g.fillRoundedRectangle (cap, s * 0.1f);
    }
    if (over || down)
    {
        g.setColour ((down ? juce::Colours::black : juce::Colours::white).withAlpha (down ? 0.18f : 0.07f));
        g.fillRoundedRectangle (cap.reduced (s * 0.03f), s * 0.08f);
    }
}

void Hw::drawMiniKey (juce::Graphics& g, juce::Rectangle<float> r, bool lit, bool over, bool down)
{
    // A small black key with an LED in the middle of its cap; lit in code so
    // on and off are exactly the same key.
    static const auto img = asset ("minikey_off.png");
    const auto s = std::min (r.getWidth(), r.getHeight());
    const auto c = r.getCentre();
    if (img.isValid())
        drawPart (g, img, c, s, 1.0f);
    else
    {
        g.setColour (juce::Colour (0xff1c1c1c));
        g.fillRoundedRectangle (juce::Rectangle<float> (s * 0.8f, s * 0.8f).withCentre (c), s * 0.12f);
    }
    const auto lens = juce::Rectangle<float> (s * 0.34f, s * 0.34f).withCentre (c);
    if (lit)
    {
        juce::ColourGradient bloom (Colours::crimson.withAlpha (0.55f), c.x, c.y, Colours::crimson.withAlpha (0.0f), c.x + s * 0.42f, c.y, true);
        g.setGradientFill (bloom);
        g.fillEllipse (lens.expanded (s * 0.24f));
        juce::ColourGradient led (juce::Colour (0xffffb0a4), c.x - s * 0.04f, c.y - s * 0.05f, Colours::crimson, c.x + s * 0.15f, c.y + s * 0.15f, true);
        g.setGradientFill (led);
        g.fillEllipse (lens.reduced (s * 0.02f));
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.fillEllipse (juce::Rectangle<float> (s * 0.08f, s * 0.06f).withCentre ({ c.x - s * 0.04f, c.y - s * 0.07f }));
    }
    if (over || down)
    {
        g.setColour ((down ? juce::Colours::black : juce::Colours::white).withAlpha (down ? 0.2f : 0.05f));
        g.fillRoundedRectangle (juce::Rectangle<float> (s * 0.72f, s * 0.72f).withCentre (c), s * 0.12f);
    }
}

juce::Rectangle<float> Hw::keyCap (juce::Rectangle<float> r)
{
    const auto s = std::min (r.getWidth(), r.getHeight());
    return juce::Rectangle<float> (r.getWidth() - s * 0.22f, r.getHeight() - s * 0.22f).withCentre (r.getCentre());
}

void Hw::drawLed (juce::Graphics& g, juce::Point<float> c, bool on, float radius)
{
    static const auto ledOn = asset ("led_on.png");
    static const auto ledOff = asset ("led_off.png");
    if (const auto& img = on ? ledOn : ledOff; img.isValid())
    {
        if (on)
        {
            // light spilling onto the panel around the lens
            juce::ColourGradient spill (Colours::crimson.withAlpha (0.3f), c.x, c.y,
                                        Colours::crimson.withAlpha (0.0f), c.x + radius * 2.4f, c.y, true);
            g.setGradientFill (spill);
            g.fillEllipse (juce::Rectangle<float> (radius * 4.8f, radius * 4.8f).withCentre (c));
        }
        drawPart (g, img, c, radius * 2.0f, 0.62f);
        return;
    }
    const auto r = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c);
    g.setColour (on ? Colours::crimson : juce::Colour (0xff3a1a18));
    g.fillEllipse (r);
}

void Hw::drawScrew (juce::Graphics& g, juce::Point<float> c, bool onBlack, float size)
{
    static const auto paperScrew = asset ("screw_paper.png");
    static const auto blackScrew = asset ("screw_black.png");
    if (const auto& img = onBlack ? blackScrew : paperScrew; img.isValid())
    {
        // Not rotated: the photo's light has to agree with the rest of the unit.
        drawPart (g, img, c, size, 0.8f);
        return;
    }
    const auto r = juce::Rectangle<float> (size, size).withCentre (c);
    g.setColour (juce::Colour (onBlack ? 0xff2a2a2a : 0xff8d8a84));
    g.fillEllipse (r);
}

void Hw::drawToggle (juce::Graphics& g, juce::Point<float> c, bool up, float size)
{
    static const auto img = asset ("toggle.png");
    if (img.isValid())
    {
        // The photo is lit from above; flipped, the lever points down (bypassed).
        drawPart (g, img, c, size, 1.0f, ! up);
        return;
    }
    g.setColour (juce::Colour (0xff9a9690));
    g.fillEllipse (juce::Rectangle<float> (size * 0.4f, size * 0.4f).withCentre (c));
    g.drawLine (c.x, c.y, c.x, c.y + (up ? -size * 0.4f : size * 0.4f), size * 0.1f);
}

void Hw::drawJack (juce::Graphics& g, juce::Point<float> c, float size)
{
    static const auto img = asset ("jack.png");
    if (img.isValid())
    {
        drawPart (g, img, c, size, 0.77f);
        return;
    }
    g.setColour (juce::Colour (0xff3a3a3a));
    g.fillEllipse (juce::Rectangle<float> (size, size).withCentre (c));
    g.setColour (juce::Colours::black);
    g.fillEllipse (juce::Rectangle<float> (size * 0.45f, size * 0.45f).withCentre (c));
}

void Hw::drawGroove (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, bool onBlack)
{
    // A line pressed into the metal: a dark cut with a lit lip on its far side.
    const auto horizontal = std::abs (b.y - a.y) < std::abs (b.x - a.x);
    const juce::Point<float> off = horizontal ? juce::Point<float> (0.0f, 1.0f) : juce::Point<float> (1.0f, 0.0f);
    g.setColour (juce::Colours::black.withAlpha (onBlack ? 0.55f : 0.16f));
    g.drawLine ({ a - off * 0.5f, b - off * 0.5f }, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (onBlack ? 0.35f : 0.07f));
    g.drawLine ({ a - off * 1.5f, b - off * 1.5f }, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (onBlack ? 0.07f : 0.75f));
    g.drawLine ({ a + off * 0.6f, b + off * 0.6f }, 1.0f);
}

void Hw::drawGrooveRect (juce::Graphics& g, juce::Rectangle<float> r, float corner, bool onBlack)
{
    // Inset frame: shadow on the top/left inner edge, light on the bottom/right.
    g.setColour (juce::Colours::black.withAlpha (onBlack ? 0.55f : 0.15f));
    g.drawRoundedRectangle (r, corner, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (onBlack ? 0.25f : 0.06f));
    g.drawRoundedRectangle (r.reduced (1.0f).translated (0.5f, 0.5f), corner, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (onBlack ? 0.06f : 0.7f));
    g.drawRoundedRectangle (r.expanded (1.0f).translated (0.5f, 0.8f), corner + 1.0f, 0.8f);
}

void Hw::drawDottedPath (juce::Graphics& g, const juce::Path& p, juce::Colour c, float spacing, float dotRadius)
{
    juce::PathFlatteningIterator it (p, juce::AffineTransform(), 0.5f);
    float carry = 0.0f;
    g.setColour (c);
    while (it.next())
    {
        const juce::Line<float> seg (it.x1, it.y1, it.x2, it.y2);
        const auto len = seg.getLength();
        auto d = carry;
        while (d < len)
        {
            const auto pt = seg.getPointAlongLine (d);
            g.fillEllipse (pt.x - dotRadius, pt.y - dotRadius, dotRadius * 2.0f, dotRadius * 2.0f);
            d += spacing;
        }
        carry = d - len;
    }
}

juce::Image Hw::grain (int w, int h, bool dark, int seed)
{
    juce::Image img (juce::Image::ARGB, std::max (1, w), std::max (1, h), true);
    juce::Random rng (seed);
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const auto n = rng.nextFloat();
            // Mostly tiny specks, a few darker flecks: painted steel / paper.
            float a = 0.0f;
            if (n > 0.985f) a = dark ? 0.06f : 0.12f;
            else if (n > 0.6f) a = (n - 0.6f) * (dark ? 0.05f : 0.07f);
            const auto v = dark ? juce::Colours::white : juce::Colours::black;
            bd.setPixelColour (x, y, v.withAlpha (a));
        }
    return img;
}
