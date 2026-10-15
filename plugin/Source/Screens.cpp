#include "Screens.h"
#include "UiColours.h"
#include "EvilleLookAndFeel.h"

juce::Rectangle<float> Hw::drawScreen (juce::Graphics& g, juce::Rectangle<float> b)
{
    // Bezel: a dark frame set into the panel, lit from above.
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.fillRoundedRectangle (b.translated (0.0f, 1.5f).expanded (1.0f), 7.0f);
    juce::ColourGradient bezel (juce::Colour (0xff2b2b2b), 0.0f, b.getY(), juce::Colour (0xff0d0d0d), 0.0f, b.getBottom(), false);
    g.setGradientFill (bezel);
    g.fillRoundedRectangle (b, 7.0f);
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawRoundedRectangle (b.reduced (0.5f), 7.0f, 1.0f);

    // Glass
    const auto glass = b.reduced (4.0f);
    g.setColour (Colours::glass);
    g.fillRoundedRectangle (glass, 4.5f);
    // Vignette: the centre of the tube is a touch lighter.
    juce::ColourGradient vig (juce::Colour (0xff1a1a1a), glass.getCentreX(), glass.getCentreY(),
                              Colours::glass.withAlpha (0.0f), glass.getX(), glass.getY(), true);
    g.setGradientFill (vig);
    g.fillRoundedRectangle (glass, 4.5f);
    return glass;
}

void Hw::drawScreenOverlay (juce::Graphics& g, juce::Rectangle<float> glass)
{
    g.saveState();
    g.reduceClipRegion (glass.toNearestInt());
    g.setColour (juce::Colours::black.withAlpha (0.16f));
    for (auto y = glass.getY(); y < glass.getBottom(); y += 3.0f)
        g.fillRect (glass.getX(), y, glass.getWidth(), 1.0f);
    // A soft reflection across the top of the glass.
    juce::ColourGradient refl (juce::Colours::white.withAlpha (0.045f), 0.0f, glass.getY(),
                               juce::Colours::white.withAlpha (0.0f), 0.0f, glass.getY() + glass.getHeight() * 0.45f, false);
    g.setGradientFill (refl);
    g.fillRect (glass.withHeight (glass.getHeight() * 0.45f));
    // Inner shadow at the edges
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (glass.reduced (0.5f), 4.5f, 1.5f);
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

void Hw::drawKey (juce::Graphics& g, juce::Rectangle<float> r, bool light, bool over, bool down)
{
    // Photographed key caps (made in Higgsfield), with a drawn shadow under them.
    if (auto img = asset (light ? "key_light.png" : "key_dark.png"); img.isValid())
    {
        const auto s = std::min (r.getWidth(), r.getHeight()) + 4.0f;
        auto cap = juce::Rectangle<float> (s, s).withCentre (r.getCentre()).translated (0.0f, down ? 1.2f : 0.0f);
        g.setColour (juce::Colours::black.withAlpha (light ? (down ? 0.10f : 0.2f) : 0.45f));
        g.fillRoundedRectangle (r.reduced (2.0f).translated (0.0f, down ? 1.0f : 2.5f), 6.0f);
        g.setOpacity (1.0f);
        g.drawImage (img, cap, juce::RectanglePlacement::stretchToFit);
        if (over || down)
        {
            g.setColour ((down ? juce::Colours::black : juce::Colours::white).withAlpha (down ? 0.08f : 0.06f));
            g.fillRoundedRectangle (cap.reduced (s * 0.08f), 6.0f);
        }
        return;
    }

    const auto lift = down ? 0.8f : 2.2f;
    // Shadow and the key's side
    g.setColour (juce::Colours::black.withAlpha (light ? 0.22f : 0.5f));
    g.fillRoundedRectangle (r.translated (0.0f, lift), 6.0f);
    g.setColour (light ? juce::Colour (0xffb9b3a7) : juce::Colour (0xff050505));
    g.fillRoundedRectangle (r.translated (0.0f, lift * 0.5f), 6.0f);
    // Top
    auto top = r.withTrimmedBottom (lift * 0.5f).translated (0.0f, down ? 0.8f : 0.0f);
    juce::ColourGradient grad (light ? juce::Colour (over ? 0xfffbfaf6 : 0xfff6f4ef) : juce::Colour (over ? 0xff333333 : 0xff2a2a2a),
                               0.0f, top.getY(),
                               light ? juce::Colour (0xffe2ded5) : juce::Colour (0xff141414), 0.0f, top.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (top, 6.0f);
    g.setColour (light ? juce::Colour (0xffa8a296) : juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (top.reduced (0.5f), 6.0f, 1.0f);
}

void Hw::drawLed (juce::Graphics& g, juce::Point<float> c, bool on, float radius)
{
    const auto r = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c);
    if (on)
    {
        g.setColour (Colours::crimson.withAlpha (0.25f));
        g.fillEllipse (r.expanded (radius * 1.4f));
        g.setColour (Colours::crimson.withAlpha (0.35f));
        g.fillEllipse (r.expanded (radius * 0.6f));
    }
    juce::ColourGradient led (on ? juce::Colour (0xffff6b5e) : juce::Colour (0xff4a2522), c.x - radius * 0.4f, c.y - radius * 0.4f,
                              on ? Colours::crimson : juce::Colour (0xff1c0e0d), c.x + radius, c.y + radius, true);
    g.setGradientFill (led);
    g.fillEllipse (r);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawEllipse (r, 0.8f);
}

void Hw::drawScrew (juce::Graphics& g, juce::Point<float> c, bool onBlack)
{
    const auto r = juce::Rectangle<float> (11.0f, 11.0f).withCentre (c);
    g.setColour (juce::Colours::black.withAlpha (onBlack ? 0.6f : 0.25f));
    g.fillEllipse (r.translated (0.0f, 1.0f).expanded (0.8f));
    juce::ColourGradient grad (juce::Colour (onBlack ? 0xff4a4a4a : 0xff8d8a84), r.getX(), r.getY(),
                               juce::Colour (onBlack ? 0xff151515 : 0xff3c3a36), r.getRight(), r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillEllipse (r);
    g.setColour (juce::Colours::black.withAlpha (0.65f));
    g.drawLine ({ c.translated (-3.3f, -3.3f), c.translated (3.3f, 3.3f) }, 1.4f);
    g.drawLine ({ c.translated (-3.3f, 3.3f), c.translated (3.3f, -3.3f) }, 1.4f);
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

void Hw::drawTinyRoom (juce::Graphics& g, juce::Point<float> c, float s, juce::Colour col)
{
    // Isometric box: front face, a receding top and side, and a doorway.
    const auto w = s, h = s * 1.1f, d = s * 0.45f;
    const juce::Point<float> f0 (c.x - w * 0.5f, c.y - h * 0.4f), f1 (f0.x + w, f0.y),
                             f2 (f1.x, f0.y + h), f3 (f0.x, f2.y);
    const juce::Point<float> off (d, -d * 0.6f);
    juce::Path p;
    p.addLineSegment ({ f0, f1 }, 0.0f); p.addLineSegment ({ f1, f2 }, 0.0f);
    p.addLineSegment ({ f2, f3 }, 0.0f); p.addLineSegment ({ f3, f0 }, 0.0f);
    p.addLineSegment ({ f0, f0 + off }, 0.0f); p.addLineSegment ({ f1, f1 + off }, 0.0f);
    p.addLineSegment ({ f2, f2 + off }, 0.0f);
    p.addLineSegment ({ f0 + off, f1 + off }, 0.0f); p.addLineSegment ({ f1 + off, f2 + off }, 0.0f);
    // Inner back wall lines, seen through the open front
    p.addLineSegment ({ f3 + off * 0.6f, f0 + off * 0.6f }, 0.0f);
    // Doorway
    const auto dw = w * 0.22f, dh = h * 0.45f;
    const juce::Point<float> dx (c.x + w * 0.05f, f2.y);
    p.addLineSegment ({ dx, dx.translated (0.0f, -dh) }, 0.0f);
    p.addLineSegment ({ dx.translated (0.0f, -dh), dx.translated (dw, -dh) }, 0.0f);
    p.addLineSegment ({ dx.translated (dw, -dh), dx.translated (dw, 0.0f) }, 0.0f);
    g.setColour (col);
    g.strokePath (p, juce::PathStrokeType (0.8f));
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
