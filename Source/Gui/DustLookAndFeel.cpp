#include "DustLookAndFeel.h"
#include "Art.h"

using namespace juce;

namespace
{
Point<float> polar (Point<float> c, float radius, float angle)
{
    return { c.x + radius * std::sin (angle), c.y - radius * std::cos (angle) };
}
} // namespace

DustLookAndFeel::DustLookAndFeel()
{
    setColour (TooltipWindow::textColourId, Art::Colours::silk);
    setColour (PopupMenu::backgroundColourId, Colour (0xff151515));
    setColour (PopupMenu::textColourId, Art::Colours::silk);
    setColour (PopupMenu::highlightedBackgroundColourId, Art::Colours::brandGold.withAlpha (0.3f));
    setColour (PopupMenu::highlightedTextColourId, Colours::white);
    setColour (PopupMenu::headerTextColourId, Art::Colours::brandGold);
}

void DustLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int width, int height, float pos,
                                        float startAngle, float endAngle, Slider& slider)
{
    const auto& props = slider.getProperties();
    const auto bounds = Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (2.0f);
    const auto angle = startAngle + pos * (endAngle - startAngle);
    const auto macro = props["style"].toString() == "macro";
    const auto accent = Colour::fromString (props.getWithDefault ("accent", Colours::transparentBlack.toString()).toString());

    drawKnob (g, bounds, angle, (int) props.getWithDefault ("seed", 1), accent, macro);
}

void DustLookAndFeel::drawKnob (Graphics& g, Rectangle<float> bounds, float angle, int seed, Colour accent, bool macro)
{
    Random r (seed);
    const auto size = jmin (bounds.getWidth(), bounds.getHeight());
    const auto c = bounds.getCentre();
    const auto rad = size * 0.5f;

    // Contact shadow, light from above
    for (int i = 0; i < 6; ++i)
    {
        const auto sr = rad * (1.0f + 0.03f * (float) i);
        g.setColour (Colours::black.withAlpha (0.16f - 0.022f * (float) i));
        g.fillEllipse (c.x - sr, c.y - sr + rad * 0.1f + (float) i * 0.6f, sr * 2.0f, sr * 2.0f);
    }

    // Skirt: black plastic, lit from above
    ColourGradient skirt (Colour (0xff2c2c2c), c.x, c.y - rad, Colour (0xff060606), c.x, c.y + rad, false);
    g.setGradientFill (skirt);
    g.fillEllipse (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f);

    // Knurled grip (rotates with the knob); dust collects in the grooves
    const auto ridges = macro ? 72 : 44;
    for (int i = 0; i < ridges; ++i)
    {
        const auto a = angle + MathConstants<float>::twoPi * (float) i / (float) ridges;
        const auto lit = 0.5f - 0.5f * std::cos (a); // brighter on top
        g.setColour (Colours::white.withAlpha (0.03f + 0.07f * (1.0f - lit)));
        g.drawLine (Line<float> (polar (c, rad * 0.84f, a), polar (c, rad * 0.985f, a)), 0.7f);
        g.setColour (Colours::black.withAlpha (0.5f));
        g.drawLine (Line<float> (polar (c, rad * 0.84f, a + 0.03f), polar (c, rad * 0.985f, a + 0.03f)), 0.6f);
        if (r.nextFloat() < 0.25f)
        {
            const auto p = polar (c, rad * (0.86f + r.nextFloat() * 0.12f), a + 0.015f);
            g.setColour (Colour (0xffcfc9bd).withAlpha (0.18f + r.nextFloat() * 0.2f));
            g.fillEllipse (p.x - 0.4f, p.y - 0.4f, 0.8f, 0.8f);
        }
    }
    g.setColour (Colours::white.withAlpha (0.10f));
    Path rim;
    rim.addCentredArc (c.x, c.y, rad * 0.99f, rad * 0.99f, 0.0f, -1.2f, 1.2f, true);
    g.strokePath (rim, PathStrokeType (0.8f));

    // Top face: matte black with a soft sheen
    const auto faceR = rad * 0.8f;
    ColourGradient face (Colour (0xff2a2a2a), c.x - faceR * 0.4f, c.y - faceR, Colour (0xff0b0b0b), c.x + faceR * 0.3f, c.y + faceR, false);
    g.setGradientFill (face);
    g.fillEllipse (c.x - faceR, c.y - faceR, faceR * 2.0f, faceR * 2.0f);
    g.setColour (Colours::black.withAlpha (0.7f));
    g.drawEllipse (c.x - faceR, c.y - faceR, faceR * 2.0f, faceR * 2.0f, 1.0f);
    Path faceRim;
    faceRim.addCentredArc (c.x, c.y, faceR - 0.6f, faceR - 0.6f, 0.0f, -1.4f, 1.4f, true);
    g.setColour (Colours::white.withAlpha (0.13f));
    g.strokePath (faceRim, PathStrokeType (0.8f));

    // Macro knobs carry a painted colour ring, rubbed away in places
    if (macro && accent.getAlpha() > 0)
    {
        const auto ringR = rad * 0.69f;
        for (int i = 0; i < 48; ++i)
        {
            const auto a0 = MathConstants<float>::twoPi * (float) i / 48.0f;
            const auto worn = r.nextFloat();
            if (worn < 0.12f) continue;
            Path seg;
            seg.addCentredArc (c.x, c.y, ringR, ringR, 0.0f, angle + a0, angle + a0 + MathConstants<float>::twoPi / 48.0f + 0.01f, true);
            g.setColour (accent.withMultipliedSaturation (0.8f).withAlpha (worn < 0.3f ? 0.35f : 0.75f));
            g.strokePath (seg, PathStrokeType (rad * 0.05f));
        }
    }

    // Spun-aluminium cap
    const auto capR = rad * (macro ? 0.48f : 0.42f);
    ColourGradient cap (Colour (0xffbdbcb6), c.x - capR * 0.3f, c.y - capR * 0.4f, Colour (0xff5c5b57), c.x + capR, c.y + capR, true);
    g.setGradientFill (cap);
    g.fillEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f);
    for (int i = 0; i < (macro ? 60 : 30); ++i)
    {
        const auto rr = capR * r.nextFloat();
        g.setColour ((r.nextBool() ? Colours::white : Colours::black).withAlpha (0.03f + r.nextFloat() * 0.06f));
        g.drawEllipse (c.x - rr, c.y - rr, rr * 2.0f, rr * 2.0f, 0.5f);
    }
    // Anisotropic highlight: brushed metal catches the light in a bow-tie
    for (auto axis : { -MathConstants<float>::pi * 0.25f, MathConstants<float>::pi * 0.75f })
    {
        Path wedge;
        wedge.addPieSegment (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f, axis - 0.35f, axis + 0.35f, 0.0f);
        ColourGradient sheen (Colours::white.withAlpha (0.22f), c.x, c.y, Colours::white.withAlpha (0.0f), polar (c, capR, axis).x, polar (c, capR, axis).y, false);
        g.setGradientFill (sheen);
        g.fillPath (wedge);
    }
    g.setColour (Colours::black.withAlpha (0.55f));
    g.drawEllipse (c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f, 0.9f);

    // Engraved line in the cap, painted line on the black face
    g.setColour (Colour (0xff1a1a1a).withAlpha (0.85f));
    g.drawLine (Line<float> (polar (c, capR * 0.2f, angle), polar (c, capR * 0.92f, angle)), macro ? 2.0f : 1.4f);
    const auto p0 = polar (c, capR + rad * 0.05f, angle), p1 = polar (c, faceR * 0.95f, angle);
    g.setColour (Colour (0xffe9e5dc).withAlpha (0.88f));
    g.drawLine (Line<float> (p0, p1), macro ? 2.6f : 1.8f);
    if (r.nextFloat() < 0.6f)
    {
        const auto chip = Line<float> (p0, p1).getPointAlongLineProportionally (0.3f + r.nextFloat() * 0.5f);
        g.setColour (Colour (0xff121212));
        g.fillEllipse (chip.x - 0.8f, chip.y - 0.8f, 1.6f, 1.6f);
    }

    // A scratch or two on the cap
    for (int i = 0; i < 2; ++i)
    {
        const auto a = r.nextFloat() * MathConstants<float>::twoPi;
        const auto p = polar (c, capR * r.nextFloat() * 0.7f, a);
        g.setColour (Colours::white.withAlpha (0.25f));
        g.drawLine (p.x, p.y, p.x + (r.nextFloat() - 0.5f) * capR * 0.8f, p.y + (r.nextFloat() - 0.5f) * capR * 0.5f, 0.4f);
    }
}

//==============================================================================
Font DustLookAndFeel::getSliderPopupFont (Slider&)       { return Art::silk (14.0f); }
int DustLookAndFeel::getSliderPopupPlacement (Slider&)   { return BubbleComponent::above; }

void DustLookAndFeel::drawBubble (Graphics& g, BubbleComponent&, const Point<float>&, const Rectangle<float>& body)
{
    const auto b = body.reduced (2.0f, 3.0f);
    g.setColour (Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (b.translated (0.0f, 1.5f), 2.0f);
    g.setColour (Colour (0xff0d0d0d));
    g.fillRoundedRectangle (b, 2.0f);
    g.setColour (Art::Colours::line);
    g.drawRoundedRectangle (b, 2.0f, 0.8f);
}

Font DustLookAndFeel::getPopupMenuFont()
{
    return Art::silk (16.0f);
}

void DustLookAndFeel::drawPopupMenuBackground (Graphics& g, int width, int height)
{
    g.fillAll (findColour (PopupMenu::backgroundColourId));
    g.setColour (Art::Colours::line);
    g.drawRect (0, 0, width, height);
}
