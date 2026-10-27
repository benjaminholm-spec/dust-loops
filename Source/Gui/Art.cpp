#include "Art.h"
#include "BinaryData.h"

namespace Art
{
using namespace juce;

namespace
{
Font fontWithFallback (std::initializer_list<std::pair<const char*, const char*>> candidates, float height)
{
    static const auto available = Font::findAllTypefaceNames();
    for (const auto& [name, style] : candidates)
        if (available.contains (name))
            return Font (FontOptions().withName (name).withStyle (style).withHeight (height));
    return Font (FontOptions().withHeight (height));
}

// Irregular polygon, for paint chips
Path makeChip (Point<float> c, float size, Random& r)
{
    Path p;
    const auto n = 5 + r.nextInt (4);
    for (int i = 0; i < n; ++i)
    {
        const auto a = MathConstants<float>::twoPi * (float) i / (float) n + (r.nextFloat() - 0.5f) * 0.6f;
        const auto rr = size * (0.4f + r.nextFloat() * 0.6f);
        const Point<float> pt { c.x + std::cos (a) * rr, c.y + std::sin (a) * rr * (0.5f + r.nextFloat() * 0.5f) };
        if (i == 0) p.startNewSubPath (pt);
        else        p.lineTo (pt);
    }
    p.closeSubPath();
    return p;
}
} // namespace

Font silk (float height)      { return fontWithFallback ({ { "Helvetica Neue", "Condensed Bold" }, { "DIN Condensed", "Bold" } }, height); }
Font silkLight (float height) { return fontWithFallback ({ { "Helvetica Neue", "Medium" }, { "DIN Alternate", "Bold" } }, height); }
Font brand (float height)     { return fontWithFallback ({ { "Avenir Next", "Heavy" }, { "Futura", "Bold" } }, height); }

Image logoHorizontal()
{
    return ImageCache::getFromMemory (BinaryData::dpr_logo_horizontal_png, BinaryData::dpr_logo_horizontal_pngSize);
}

Image logoMonogram()
{
    // The monogram is the left part of the horizontal lockup (the wordmark starts at x = 535 of 1600).
    const auto full = logoHorizontal();
    return full.getClippedImage ({ 0, 0, full.getWidth() * 507 / 1600, full.getHeight() });
}

void drawPrintedImage (Graphics& g, const Image& image, Rectangle<float> area, Colour ink)
{
    Graphics::ScopedSaveState save (g);
    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    g.setColour (ink);
    g.drawImage (image, area, RectanglePlacement::centred, true);
}

//==============================================================================
void drawBlackPanel (Image& target, Rectangle<int> area, float scale, uint32 seed)
{
    Random r ((int64) seed);

    {
        Graphics g (target);
        g.addTransform (AffineTransform::scale (scale));

        // Overhead studio light: slightly lifted toward the top, falling off to the front edge
        ColourGradient base (Colours::panelLift, (float) area.getCentreX(), (float) area.getY(),
                             Colours::panel.darker (0.35f), (float) area.getCentreX(), (float) area.getBottom(), false);
        base.addColour (0.35, Colours::panel);
        g.setGradientFill (base);
        g.fillRect (area);

        // Large, faint mottling in the paint
        for (int i = 0; i < 18; ++i)
        {
            const Point<float> c ((float) area.getX() + r.nextFloat() * (float) area.getWidth(),
                                  (float) area.getY() + r.nextFloat() * (float) area.getHeight());
            const auto rad = 80.0f + r.nextFloat() * 220.0f;
            const auto tone = r.nextBool() ? Colours::panelLift.brighter (0.2f) : Colours::panel.darker (0.5f);
            ColourGradient blot (tone.withAlpha (0.10f + r.nextFloat() * 0.08f), c.x, c.y, tone.withAlpha (0.0f), c.x + rad, c.y, true);
            g.setGradientFill (blot);
            g.fillEllipse (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f);
        }
    }

    // Powder-coat grain
    const auto px = (area.toFloat() * scale).getSmallestIntegerContainer().getIntersection (target.getBounds());
    Image::BitmapData data (target, px.getX(), px.getY(), px.getWidth(), px.getHeight(), Image::BitmapData::readWrite);
    for (int y = 0; y < data.height; ++y)
        for (int x = 0; x < data.width; ++x)
        {
            const auto n = (r.nextFloat() - 0.5f) * 0.11f;
            const auto c = data.getPixelColour (x, y);
            data.setPixelColour (x, y, c.withBrightness (jlimit (0.0f, 1.0f, c.getBrightness() * (1.0f + n) + n * 0.012f)));
        }
}

void drawPanelWear (Graphics& g, Rectangle<float> area, uint32 seed)
{
    Random r ((int64) seed);
    auto randomPoint = [&] { return Point<float> (area.getX() + r.nextFloat() * area.getWidth(), area.getY() + r.nextFloat() * area.getHeight()); };

    // Greasy finger smudges where hands rest and reach
    for (int i = 0; i < 22; ++i)
    {
        auto c = randomPoint();
        c.y = area.getY() + area.getHeight() * (0.35f + 0.65f * std::sqrt (r.nextFloat()));
        const auto rx = 10.0f + r.nextFloat() * 22.0f, ry = rx * (0.6f + r.nextFloat() * 0.3f);
        ColourGradient smudge (Colour (0xff9a968c).withAlpha (0.035f + r.nextFloat() * 0.03f), c.x, c.y,
                               Colour (0x009a968c), c.x + rx, c.y, true);
        g.setGradientFill (smudge);
        g.fillEllipse (c.x - rx, c.y - ry, rx * 2.0f, ry * 2.0f);
    }

    // Hairline scratches: mostly paint-deep (grey), a few through to bright metal
    for (int i = 0; i < 160; ++i)
    {
        const auto start = randomPoint();
        const auto len = 4.0f + std::pow (r.nextFloat(), 2.0f) * 90.0f;
        const auto angle = r.nextFloat() * MathConstants<float>::pi;
        const auto bend = (r.nextFloat() - 0.5f) * len * 0.15f;
        const Point<float> end { start.x + std::cos (angle) * len, start.y + std::sin (angle) * len };
        Path s;
        s.startNewSubPath (start);
        s.quadraticTo ((start.x + end.x) * 0.5f - std::sin (angle) * bend, (start.y + end.y) * 0.5f + std::cos (angle) * bend, end.x, end.y);
        const auto deep = r.nextFloat() < 0.12f;
        g.setColour ((deep ? Colours::bareMetal : Colour (0xff6c6a64)).withAlpha (deep ? 0.35f + r.nextFloat() * 0.3f : 0.05f + r.nextFloat() * 0.12f));
        g.strokePath (s, PathStrokeType (deep ? 0.6f : 0.45f));
    }

    // Swirl scuffs (something slid across the panel)
    for (int i = 0; i < 6; ++i)
    {
        const auto c = randomPoint();
        for (int k = 0; k < 14; ++k)
        {
            const auto rad = 15.0f + r.nextFloat() * 40.0f;
            const auto a0 = r.nextFloat() * MathConstants<float>::twoPi;
            Path arc;
            arc.addCentredArc (c.x, c.y, rad, rad * 0.6f, 0.3f, a0, a0 + 0.2f + r.nextFloat() * 0.6f, true);
            g.setColour (Colour (0xff77746d).withAlpha (0.04f + r.nextFloat() * 0.06f));
            g.strokePath (arc, PathStrokeType (0.5f));
        }
    }

    // Paint chips exposing bare metal, concentrated along the front and back edges
    for (int i = 0; i < 28; ++i)
    {
        auto c = randomPoint();
        if (r.nextFloat() < 0.6f)
            c.y = r.nextBool() ? area.getY() + r.nextFloat() * 10.0f : area.getBottom() - r.nextFloat() * 10.0f;
        const auto size = 0.8f + std::pow (r.nextFloat(), 3.0f) * 5.0f;
        auto chip = makeChip (c, size, r);
        g.setColour (Colour (0xff050505).withAlpha (0.6f));
        g.strokePath (chip, PathStrokeType (0.8f));
        ColourGradient metal (Colours::bareMetal.brighter (0.2f), c.x - size, c.y - size, Colours::bareMetal.darker (0.4f), c.x + size, c.y + size, false);
        g.setGradientFill (metal);
        g.fillPath (chip);
    }

    // Rubbed-through front and back edges
    for (auto edgeY : { area.getY(), area.getBottom() - 5.0f })
        for (int i = 0; i < 60; ++i)
        {
            const auto x = area.getX() + r.nextFloat() * area.getWidth();
            const auto w = 4.0f + r.nextFloat() * 30.0f;
            g.setColour (Colour (0xff4a4844).withAlpha (0.05f + r.nextFloat() * 0.12f));
            g.fillRoundedRectangle (x, edgeY + r.nextFloat() * 3.0f, w, 1.0f + r.nextFloat() * 2.0f, 1.0f);
        }

    // Dust and lint: black gear shows every speck
    for (int i = 0; i < 700; ++i)
    {
        const auto p = randomPoint();
        const auto s = 0.35f + std::pow (r.nextFloat(), 3.0f) * 1.6f;
        g.setColour (Colour (0xffcfc9bd).withAlpha (0.06f + r.nextFloat() * 0.22f));
        g.fillEllipse (p.x, p.y, s, s);
    }
    for (int i = 0; i < 9; ++i)
    {
        const auto p = randomPoint();
        Path fibre;
        fibre.startNewSubPath (p);
        fibre.cubicTo (p.x + (r.nextFloat() - 0.5f) * 12.0f, p.y + (r.nextFloat() - 0.5f) * 12.0f,
                       p.x + (r.nextFloat() - 0.5f) * 16.0f, p.y + (r.nextFloat() - 0.5f) * 16.0f,
                       p.x + (r.nextFloat() - 0.5f) * 14.0f, p.y + (r.nextFloat() - 0.5f) * 14.0f);
        g.setColour (Colour (0xffb9b3a6).withAlpha (0.18f));
        g.strokePath (fibre, PathStrokeType (0.4f));
    }
}

void wearPrint (Graphics& g, Rectangle<float> area, int count, uint32 seed)
{
    Random r ((int64) seed);
    for (int i = 0; i < count; ++i)
    {
        const auto x = area.getX() + r.nextFloat() * area.getWidth(), y = area.getY() + r.nextFloat() * area.getHeight();
        const auto s = 0.5f + r.nextFloat() * 1.6f;
        g.setColour (Colours::panel.withAlpha (0.5f + r.nextFloat() * 0.5f));
        g.fillEllipse (x, y, s, s * (0.4f + r.nextFloat()));
    }
}

//==============================================================================
void drawRail (Graphics& g, Rectangle<float> area, uint32 seed)
{
    Random r ((int64) seed);

    // Extruded aluminium, black anodised: a soft cylindrical sheen across its width
    ColourGradient sheen (Colour (0xff0d0d0d), area.getX(), 0.0f, Colour (0xff070707), area.getRight(), 0.0f, false);
    sheen.addColour (0.3, Colour (0xff2a2a2a));
    sheen.addColour (0.45, Colour (0xff1d1d1d));
    g.setGradientFill (sheen);
    g.fillRect (area);

    // Fine vertical brushing
    for (int i = 0; i < 40; ++i)
    {
        const auto x = area.getX() + r.nextFloat() * area.getWidth();
        g.setColour (juce::Colours::white.withAlpha (0.01f + r.nextFloat() * 0.025f));
        g.drawLine (x, area.getY(), x, area.getBottom(), 0.5f);
    }

    // Anodising worn off at the corners (bright aluminium shows through)
    for (auto y : { area.getY() + 1.0f, area.getBottom() - 7.0f })
    {
        ColourGradient worn (Colours::bareMetal.withAlpha (0.5f), area.getCentreX(), y + 3.0f,
                             Colours::bareMetal.withAlpha (0.0f), area.getCentreX() + area.getWidth() * 0.6f, y + 3.0f, true);
        g.setGradientFill (worn);
        g.fillRect (area.getX(), y, area.getWidth(), 6.0f);
    }

    for (int i = 0; i < 5; ++i)
    {
        const auto y = area.getY() + r.nextFloat() * area.getHeight();
        g.setColour (Colours::bareMetal.withAlpha (0.25f + r.nextFloat() * 0.3f));
        g.drawLine (area.getX() + r.nextFloat() * area.getWidth(), y, area.getX() + r.nextFloat() * area.getWidth(), y + 2.0f + r.nextFloat() * 10.0f, 0.5f);
    }

    g.setColour (juce::Colours::black);
    g.drawVerticalLine ((int) area.getRight() - 1, area.getY(), area.getBottom());

    const auto cx = area.getCentreX();
    for (auto y : { area.getY() + 26.0f, area.getBottom() - 26.0f })
        drawScrew (g, { cx, y }, 4.0f, r.nextFloat() * 3.0f);
}

void drawScrew (Graphics& g, Point<float> c, float radius, float angle)
{
    // Countersunk hole shadow
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.fillEllipse (c.x - radius - 1.0f, c.y - radius - 0.5f, radius * 2.0f + 2.0f, radius * 2.0f + 2.0f);

    // Black-oxide steel head, worn shiny on the high spots
    ColourGradient head (Colour (0xff5a5a58), c.x - radius * 0.6f, c.y - radius * 0.8f, Colour (0xff0b0b0b), c.x + radius, c.y + radius, true);
    g.setGradientFill (head);
    g.fillEllipse (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f);

    // Phillips cross
    const auto d = radius * 0.72f, w = radius * 0.28f;
    Path slot;
    slot.addRectangle (-d, -w * 0.5f, d * 2.0f, w);
    slot.addRectangle (-w * 0.5f, -d, w, d * 2.0f);
    slot.applyTransform (AffineTransform::rotation (angle).translated (c));
    g.setColour (Colour (0xff030303));
    g.fillPath (slot);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.strokePath (slot, PathStrokeType (0.4f), AffineTransform::translation (0.3f, 0.5f));
}

void drawRecess (Graphics& g, Rectangle<float> area, float corner)
{
    // Lip highlight below, shadow above: the window sits below the panel surface
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawRoundedRectangle (area.expanded (1.5f).translated (0.0f, 0.8f), corner + 1.5f, 1.0f);
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (area.expanded (1.0f), corner + 1.0f);
    for (int i = 0; i < 5; ++i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.25f - (float) i * 0.045f));
        g.drawRoundedRectangle (area.reduced ((float) i * 0.8f).translated (0.0f, (float) i * 0.6f), corner, 1.2f);
    }
}
} // namespace Art
