#include "R5Parts.h"
#include "EvilleLookAndFeel.h"
#include "UiColours.h"
#include "Screens.h"

namespace R5
{
namespace
{
    juce::Image mip (const char* name, float minWidthPx)
    {
        juce::SharedResourcePointer<UiResources> res;   // see UiResources: no statics
        return res->mip (name, juce::roundToInt (std::max (1.0f, minWidthPx)));
    }

    float physicalScale (juce::Graphics& g)
    {
        return std::max (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    }

    /** Draws `src` (a part of an image) exactly onto `dst`. */
    void drawSection (juce::Graphics& g, const juce::Image& img, juce::Rectangle<int> src, juce::Rectangle<float> dst)
    {
        if (src.isEmpty() || dst.isEmpty())
            return;
        const auto part = img.getClippedImage (src);
        g.drawImageTransformed (part, juce::AffineTransform::scale (dst.getWidth() / (float) src.getWidth(),
                                                                    dst.getHeight() / (float) src.getHeight())
                                          .translated (dst.getX(), dst.getY()));
    }

    const juce::Colour caseColour { 0xff101010 };
    const juce::Colour glowRed { 0xffd23232 };

    /** A soft elliptical glow: radial falloff in both directions, so it has no visible edge. */
    void softGlow (juce::Graphics& g, juce::Point<float> c, float rx, float ry, float alpha)
    {
        juce::Graphics::ScopedSaveState save (g);
        g.addTransform (juce::AffineTransform::scale (1.0f, ry / rx, c.x, c.y));
        juce::ColourGradient glow (glowRed.withAlpha (alpha), c.x, c.y, glowRed.withAlpha (0.0f), c.x + rx, c.y, true);
        glow.addColour (0.45, glowRed.withAlpha (alpha * 0.45f));
        g.setGradientFill (glow);
        g.fillEllipse (juce::Rectangle<float> (rx * 2.0f, rx * 2.0f).withCentre (c));
    }
    const juce::Colour lineWhite = juce::Colour (0xfff5f3ef).withAlpha (0.74f);
    const juce::Colour lineRed = juce::Colour (0xffd23232);
}

void drawCase (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (caseColour);
    g.fillRect (area);
    if (auto tex = Hw::asset ("r5_panel.jpg"); tex.isValid())
    {
        // the brushed finish, tiled at its own size, then darkened towards black
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (area.toNearestInt());
        const auto w = (float) tex.getWidth() * 0.5f, h = (float) tex.getHeight() * 0.5f;
        for (auto y = area.getY(); y < area.getBottom(); y += h)
            for (auto x = area.getX(); x < area.getRight(); x += w)
                g.drawImage (tex, juce::Rectangle<float> (x, y, w, h));
        g.setColour (juce::Colours::black.withAlpha (0.38f));
        g.fillRect (area);
    }
}

void drawCell (juce::Graphics& g, juce::Rectangle<float> r)
{
    constexpr float corner = 9.0f;
    // depth: a soft shadow under the pocket's lip and a faint light on its lower edge
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.16f));
        g.fillRoundedRectangle (r.expanded ((float) i * 0.8f).translated (0.0f, (float) i * 0.7f), corner + (float) i);
    }
    juce::ColourGradient fill (juce::Colour (0xff121212), r.getX(), r.getY(), juce::Colour (0xff0c0c0c), r.getX(), r.getBottom(), false);
    g.setGradientFill (fill);
    g.fillRoundedRectangle (r, corner);

    {
        // an inner shadow along the top, as if the pocket is sunk into the case
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle (r, corner);
        g.reduceClipRegion (clip);
        juce::ColourGradient top (juce::Colours::black.withAlpha (0.45f), r.getX(), r.getY(),
                                  juce::Colours::transparentBlack, r.getX(), r.getY() + 10.0f, false);
        g.setGradientFill (top);
        g.fillRect (r.withHeight (10.0f));
    }
    // the rim: brighter at the bottom (catching light), darker at the top
    juce::ColourGradient rim (juce::Colours::white.withAlpha (0.05f), r.getX(), r.getY(),
                              juce::Colours::white.withAlpha (0.13f), r.getX(), r.getBottom(), false);
    g.setGradientFill (rim);
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
}

void drawKnob (juce::Graphics& g, juce::Point<float> c, float ring, float startAngle, float endAngle,
               float angle, float arcFrom, bool enabled, bool hover, bool ticks, bool light)
{
    const auto alpha = enabled ? 1.0f : 0.45f;
    const auto body = ring * 0.80f;
    auto pointAt = [&] (float a, float radius) { return juce::Point<float> (c.x + radius * std::sin (a), c.y - radius * std::cos (a)); };

    // a soft shadow under the knob
    {
        juce::ColourGradient sh (juce::Colours::black.withAlpha ((light ? 0.38f : 0.6f) * alpha), c.x, c.y + 2.0f,
                                 juce::Colours::transparentBlack, c.x + body * 1.25f, c.y + 2.0f, true);
        g.setGradientFill (sh);
        g.fillEllipse (juce::Rectangle<float> (body * 2.5f, body * 2.5f).withCentre (c.translated (0.0f, 2.0f)));
    }
    // the tick ring
    if (ticks)
    {
        const int n = 28;
        for (int i = 0; i <= n; ++i)
        {
            const auto t = startAngle + (endAngle - startAngle) * (float) i / (float) n;
            const auto major = i % 7 == 0;
            g.setColour ((light ? juce::Colour (0xff1b1a18) : juce::Colour (0xfff5f3ef)).withAlpha ((major ? (light ? 0.75f : 0.55f) : (light ? 0.45f : 0.32f)) * alpha));
            g.drawLine ({ pointAt (t, ring * (major ? 0.83f : 0.86f)), pointAt (t, ring * 0.95f) }, major ? 1.1f : 0.9f);
        }
    }
    // the value arc
    if (std::abs (angle - arcFrom) > 0.01f)
    {
        juce::Path arc;
        arc.addCentredArc (c.x, c.y, ring, ring, 0.0f, std::min (arcFrom, angle), std::max (arcFrom, angle), true);
        g.setColour (lineRed.withAlpha (alpha));
        g.strokePath (arc, juce::PathStrokeType (std::max (2.0f, ring * 0.075f), juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    }
    // the body (the photographed knob)
    const auto bodyRect = juce::Rectangle<float> (body * 2.0f, body * 2.0f).withCentre (c);
    if (auto img = mip ("r5_knob.png", bodyRect.getWidth() * physicalScale (g)); img.isValid())
    {
        g.setOpacity (alpha);
        g.drawImageTransformed (img, juce::AffineTransform::scale (bodyRect.getWidth() / (float) img.getWidth(),
                                                                   bodyRect.getHeight() / (float) img.getHeight())
                                         .translated (bodyRect.getX(), bodyRect.getY()));
        g.setOpacity (1.0f);
    }
    else
    {
        g.setColour (juce::Colour (0xff1a1a1a));
        g.fillEllipse (bodyRect);
    }
    if (hover && enabled)
    {
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillEllipse (bodyRect);
    }
    // the pointer: a white dot near the face's edge
    const auto dot = std::max (1.6f, ring * 0.065f);
    g.setColour (juce::Colour (0xfff5f3ef).withAlpha (alpha));
    g.fillEllipse (juce::Rectangle<float> (dot * 2.0f, dot * 2.0f).withCentre (pointAt (angle, body * 0.72f)));
}

void drawKey (juce::Graphics& g, juce::Rectangle<float> b, KeyKind kind, const juce::String& label,
              bool on, bool over, bool down, bool led)
{
    const auto name = kind == KeyKind::cream ? "r5_key_cream.png" : kind == KeyKind::black ? "r5_key_black.png" : "r5_key_square.png";
    auto full = Hw::asset (name);
    const auto key = b.translated (0.0f, down ? 1.0f : 0.0f);

    // lit: one soft red glow spilling out from under the key, fading to nothing well inside
    // the button's own bounds (the caller leaves `glowRoom` below the key), so it never clips
    if (on)
        softGlow (g, { key.getCentreX(), key.getBottom() - 1.0f }, key.getWidth() * 0.42f, 7.0f, 0.5f);

    if (full.isValid())
    {
        const auto phys = physicalScale (g);
        const auto img = mip (name, (float) full.getWidth() * key.getHeight() * phys / (float) full.getHeight());
        const auto iw = img.getWidth(), ih = img.getHeight();
        const auto s = key.getHeight() / (float) ih;
        const auto cap = (int) ((float) ih * 0.48f);   // the rounded ends, kept at their own shape
        if (kind == KeyKind::square || key.getWidth() <= 2.0f * (float) cap * s + 2.0f)
            drawSection (g, img, img.getBounds(), key);
        else
        {
            const auto capW = (float) cap * s;
            drawSection (g, img, { 0, 0, cap, ih }, key.withWidth (capW + 0.5f));
            drawSection (g, img, { cap, 0, iw - 2 * cap, ih }, key.withTrimmedLeft (capW).withTrimmedRight (capW).expanded (0.5f, 0.0f));
            drawSection (g, img, { iw - cap, 0, cap, ih }, key.withTrimmedLeft (key.getWidth() - capW - 0.5f));
        }
    }
    else
    {
        g.setColour (kind == KeyKind::cream ? juce::Colour (0xffe9e2d3) : juce::Colour (0xff1a1a1a));
        g.fillRoundedRectangle (key, 5.0f);
    }
    if (over)
    {
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRoundedRectangle (key.reduced (2.0f), 4.0f);
    }
    if (on)
    {
        // a faint red tint on the lower bevel, as if lit from below
        juce::Graphics::ScopedSaveState save (g);
        juce::Path shape;
        shape.addRoundedRectangle (key.reduced (1.0f), 5.0f);
        g.reduceClipRegion (shape);
        juce::ColourGradient tint (lineRed.withAlpha (0.0f), key.getX(), key.getBottom() - key.getHeight() * 0.3f,
                                   lineRed.withAlpha (0.22f), key.getX(), key.getBottom(), false);
        g.setGradientFill (tint);
        g.fillRect (key.withTrimmedTop (key.getHeight() * 0.7f));
    }

    // the label, on the key's top face (a little above the middle: the bevel is below)
    const auto face = key.withTrimmedBottom (key.getHeight() * 0.16f);
    const auto ink = kind == KeyKind::cream ? juce::Colour (0xff3b3732) : juce::Colour (0xfff5f3ef).withAlpha (on || over ? 1.0f : 0.88f);
    if (led)
    {
        const auto ledC = juce::Point<float> (face.getCentreX(), face.getY() + face.getHeight() * 0.3f);
        if (on)
        {
            juce::ColourGradient halo (lineRed.withAlpha (0.8f), ledC.x, ledC.y, lineRed.withAlpha (0.0f), ledC.x + 6.0f, ledC.y, true);
            g.setGradientFill (halo);
            g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (ledC));
        }
        g.setColour (on ? juce::Colour (0xffff5a4e) : juce::Colour (0xff3a1c1c));
        g.fillEllipse (juce::Rectangle<float> (3.6f, 3.6f).withCentre (ledC));
        g.setFont (Fonts::sans (std::min (13.0f, face.getHeight() * 0.42f), Fonts::medium));
        g.setColour (ink);
        g.drawText (label, face.withTrimmedTop (face.getHeight() * 0.42f), juce::Justification::centred, false);
    }
    else if (label.isNotEmpty())
    {
        g.setFont (Fonts::sans (std::min (10.5f, face.getHeight() * 0.42f), Fonts::bold).withExtraKerningFactor (0.14f));
        g.setColour (ink);
        g.drawText (label.toUpperCase(), face, juce::Justification::centred, false);
    }
}

void drawRoute (juce::Graphics& g, std::initializer_list<juce::Point<float>> pts, Stroke stroke, End startEnd, End finishEnd)
{
    if (pts.size() < 2)
        return;
    juce::Path p;
    auto it = pts.begin();
    p.startNewSubPath (*it);
    for (++it; it != pts.end(); ++it)
        p.lineTo (*it);
    p = p.createPathWithRoundedCorners (4.0f);

    const auto colour = stroke == Stroke::dottedRed ? lineRed : lineWhite;
    g.setColour (colour);
    if (stroke == Stroke::solid)
        g.strokePath (p, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    else
    {
        // dots placed by distance along the whole route, so corners keep the same spacing
        const auto gap = 4.2f;
        const auto d = stroke == Stroke::dottedRed ? 2.0f : 1.7f;
        const auto length = p.getLength();
        const auto n = std::max (1, (int) std::round (length / gap));
        const auto step = length / (float) n;
        for (int k = 0; k <= n; ++k)
            g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (p.getPointAlongPath ((float) k * step)));
    }

    auto drawEnd = [&] (End e, juce::Point<float> at)
    {
        if (e == End::dot)
        {
            g.setColour (stroke == Stroke::dottedRed ? lineRed : juce::Colour (0xfff5f3ef).withAlpha (0.85f));
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (at));
        }
        else if (e == End::ring)
        {
            const auto r = juce::Rectangle<float> (6.5f, 6.5f).withCentre (at);
            g.setColour (caseColour);
            g.fillEllipse (r);
            g.setColour (stroke == Stroke::dottedRed ? lineRed : juce::Colour (0xfff5f3ef).withAlpha (0.85f));
            g.drawEllipse (r.reduced (0.6f), 1.2f);
        }
    };
    drawEnd (startEnd, *pts.begin());
    drawEnd (finishEnd, *(pts.end() - 1));
}

void drawLabel (juce::Graphics& g, const juce::String& text, juce::Point<float> c, float height, float alpha, bool ink)
{
    g.setFont (Fonts::sans (height, Fonts::bold).withExtraKerningFactor (0.24f));
    g.setColour ((ink ? juce::Colour (0xff1b1a18) : juce::Colour (0xfff5f3ef)).withAlpha (alpha));
    // the extra kerning trails the last letter: shift left by half of it so the word sits centred
    const auto trail = height * 0.24f * 0.5f;
    g.drawText (text.toUpperCase(), juce::Rectangle<float> (200.0f, height * 1.6f).withCentre (c.translated (trail, 0.0f)),
                juce::Justification::centred, false);
}

void drawValue (juce::Graphics& g, const juce::String& text, juce::Point<float> c, float height, float alpha, bool ink)
{
    g.setFont (Fonts::sans (height, Fonts::medium).withExtraKerningFactor (0.03f));
    g.setColour ((ink ? juce::Colour (0xff1b1a18) : juce::Colour (0xfff5f3ef)).withAlpha (alpha));
    g.drawText (text, juce::Rectangle<float> (200.0f, height * 1.5f).withCentre (c), juce::Justification::centred, false);
}
}
