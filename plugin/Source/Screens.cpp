#include "Screens.h"
#include "UiColours.h"
#include "EvilleLookAndFeel.h"

juce::Rectangle<float> Hw::drawScreen (juce::Graphics& g, juce::Rectangle<float> b)
{
    // ROOM 1.0: a recessed dark plot with a hairline edge (no glass, no bezel).
    g.setColour (juce::Colour (0xff0e0c0c));
    g.fillRoundedRectangle (b, 8.0f);
    g.setColour (Colours::moduleEdge);
    g.drawRoundedRectangle (b.reduced (0.5f), 8.0f, 1.0f);
    return b.reduced (2.0f);
}

void Hw::drawScreenOverlay (juce::Graphics&, juce::Rectangle<float>)
{
    // Nothing on top any more: the plots are flat.
}

void Hw::strokeGlow (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width, float)
{
    // No glows (WARP's rule): a plain hairline.
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Image Hw::asset (const char* fileName)
{
    juce::SharedResourcePointer<UiResources> res;   // see UiResources: no statics
    return res->image (fileName);
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

void Hw::drawKey (juce::Graphics& g, juce::Rectangle<float> r, bool, bool over, bool down)
{
    EvilleLookAndFeel::drawChip (g, r.reduced (1.0f), false, over, down);
}

void Hw::drawGlowKey (juce::Graphics& g, juce::Rectangle<float> r, bool lit, bool over, bool down)
{
    auto fill = lit ? Colours::crimson : Colours::chip;
    if (over) fill = fill.brighter (0.08f);
    if (down) fill = fill.darker (0.1f);
    g.setColour (fill);
    g.fillRoundedRectangle (r.reduced (1.0f), 6.0f);
}

void Hw::drawMiniKey (juce::Graphics& g, juce::Rectangle<float> r, bool lit, bool over, bool down)
{
    // A small chip with a dot that turns red while on (sync, retrig).
    EvilleLookAndFeel::drawChip (g, r.reduced (1.0f), lit, over, down);
    g.setColour (lit ? Colours::crimson.brighter (0.2f) : Colours::dustWhite.withAlpha (0.25f));
    g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (r.getCentre()));
}

juce::Rectangle<float> Hw::keyCap (juce::Rectangle<float> r)
{
    return r.reduced (2.0f);
}

void Hw::drawLed (juce::Graphics& g, juce::Point<float> c, bool on, float radius)
{
    // A flat dot: red while on, a dark chip while off.
    const auto r = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c);
    g.setColour (on ? Colours::crimson.brighter (0.2f) : Colours::chip);
    g.fillEllipse (r);
    if (! on)
    {
        g.setColour (Colours::chipEdge);
        g.drawEllipse (r, 1.0f);
    }
}

void Hw::drawScrew (juce::Graphics& g, juce::Point<float> c, bool onBlack, float size)
{
    const auto paperScrew = asset ("screw_paper.png");
    const auto blackScrew = asset ("screw_black.png");
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
    const auto img = asset ("toggle.png");
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
    const auto img = asset ("jack.png");
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

void Hw::drawGroove (juce::Graphics& g, juce::Point<float> a, juce::Point<float> b, bool)
{
    g.setColour (Colours::moduleEdge);
    g.drawLine ({ a, b }, 1.0f);
}

void Hw::drawGrooveRect (juce::Graphics& g, juce::Rectangle<float> r, float corner, bool)
{
    g.setColour (Colours::moduleEdge);
    g.drawRoundedRectangle (r, corner, 1.0f);
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
