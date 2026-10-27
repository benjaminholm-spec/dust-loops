#include "VuMeter.h"
#include "Art.h"

using namespace juce;

namespace
{
constexpr float minDb = -20.0f, maxDb = 3.0f;
constexpr float referenceDbfs = -14.0f; // 0 VU
constexpr float halfSweep = 0.78f;      // radians either side of vertical
constexpr float imageScale = 2.0f;

Image makeLayer (Rectangle<int> size)
{
    return Image (Image::ARGB, jmax (1, (int) ((float) size.getWidth() * imageScale)),
                  jmax (1, (int) ((float) size.getHeight() * imageScale)), true);
}
} // namespace

VuMeter::VuMeter()
{
    setInterceptsMouseClicks (false, false);
    setOpaque (false);
}

float VuMeter::angleFor (float db)
{
    // Real VU scales are linear in voltage, which crowds the low end to the left.
    const auto lo = Decibels::decibelsToGain (minDb), hi = Decibels::decibelsToGain (maxDb);
    const auto norm = (Decibels::decibelsToGain (jlimit (minDb - 2.0f, maxDb + 1.0f, db)) - lo) / (hi - lo);
    return -halfSweep + 2.0f * halfSweep * norm;
}

Rectangle<float> VuMeter::faceArea() const    { return getLocalBounds().toFloat().reduced (11.0f, 10.0f); }
Point<float> VuMeter::pivot() const           { auto f = faceArea(); return { f.getCentreX(), f.getBottom() + f.getHeight() * 0.32f }; }
float VuMeter::needleLength() const           { return faceArea().getHeight() * 1.12f; }

void VuMeter::update (float rms, float frameSeconds)
{
    const auto target = jlimit (minDb - 2.0f, maxDb + 1.0f, Decibels::gainToDecibels (rms, -100.0f) - referenceDbfs);

    // Slightly under-damped mass/spring, like a real moving-coil movement (~300 ms rise, small overshoot).
    // Integrated in the angle domain so the motion looks natural on the nonlinear scale.
    constexpr int substeps = 8;
    const auto dt = frameSeconds / (float) substeps;
    auto angle = angleFor (position);
    const auto targetAngle = angleFor (target);
    for (int i = 0; i < substeps; ++i)
    {
        const auto accel = 170.0f * (targetAngle - angle) - 21.0f * velocity;
        velocity += accel * dt;
        angle += velocity * dt;
    }
    angle += (jitter.nextFloat() - 0.5f) * 0.004f; // worn pivot bearing

    // Back to dB via inverse mapping
    const auto lo = Decibels::decibelsToGain (minDb), hi = Decibels::decibelsToGain (maxDb);
    const auto norm = jlimit (0.0f, 1.08f, (angle + halfSweep) / (2.0f * halfSweep));
    position = Decibels::gainToDecibels (lo + norm * (hi - lo), minDb - 2.0f);
    repaint (faceArea().toNearestInt().expanded (2));
}

void VuMeter::resized()
{
    renderFace();
    renderGlass();
}

void VuMeter::renderFace()
{
    faceLayer = makeLayer (getLocalBounds());
    Graphics g (faceLayer);
    g.addTransform (AffineTransform::scale (imageScale));

    const auto b = getLocalBounds().toFloat();
    Random r (99);

    // Black metal bezel
    g.setColour (Colours::black.withAlpha (0.25f));
    g.fillRoundedRectangle (b.translated (1.5f, 2.5f).reduced (0.5f), 7.0f);
    ColourGradient housing (Colour (0xff2c2c2c), b.getX(), b.getY(), Colour (0xff050505), b.getRight(), b.getBottom(), false);
    g.setGradientFill (housing);
    g.fillRoundedRectangle (b.reduced (0.5f), 7.0f);
    g.setColour (Colour (0x30ffffff));
    g.drawRoundedRectangle (b.reduced (1.5f), 6.0f, 1.0f);

    // Yellowed face, lit from behind by a warm incandescent bulb that falls off toward the corners
    const auto face = faceArea();
    ColourGradient paper (Colour (0xfff3d79a), face.getCentreX(), face.getBottom(), Colour (0xff8a6a3c), face.getX(), face.getY(), true);
    paper.addColour (0.55, Colour (0xffd9b674));
    g.setGradientFill (paper);
    g.fillRoundedRectangle (face, 3.0f);

    Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (face.toNearestInt());

    // Water damage creeping in from one corner, plus foxing spots
    for (int i = 0; i < 3; ++i)
    {
        const auto cx = face.getRight() - r.nextFloat() * face.getWidth() * 0.3f, cy = face.getY() + r.nextFloat() * face.getHeight() * 0.4f;
        const auto rad = face.getHeight() * (0.3f + r.nextFloat() * 0.35f);
        ColourGradient stain (Colour (0x00a07a3a), cx, cy, Colour (0x30a07a3a), cx + rad, cy, true);
        stain.addColour (0.92, Colour (0x55835a26));
        stain.addColour (0.97, Colour (0x00835a26));
        g.setGradientFill (stain);
        g.fillEllipse (cx - rad, cy - rad, rad * 2.0f, rad * 2.0f);
    }
    for (int i = 0; i < 18; ++i)
    {
        const auto s = 0.8f + r.nextFloat() * 2.2f;
        g.setColour (Colour (0xff8a6532).withAlpha (0.12f + r.nextFloat() * 0.2f));
        g.fillEllipse (face.getX() + r.nextFloat() * face.getWidth(), face.getY() + r.nextFloat() * face.getHeight(), s, s);
    }

    // Scale
    const auto p = pivot();
    const auto arcR = needleLength() * 0.8f;
    auto pt = [p] (float radius, float a) { return Point<float> (p.x + radius * std::sin (a), p.y - radius * std::cos (a)); };

    Path blackArc, redArc;
    blackArc.addCentredArc (p.x, p.y, arcR, arcR, 0.0f, angleFor (minDb), angleFor (0.0f), true);
    redArc.addCentredArc (p.x, p.y, arcR, arcR, 0.0f, angleFor (0.0f), angleFor (maxDb), true);
    g.setColour (Colour (0xff2b2420).withAlpha (0.85f));
    g.strokePath (blackArc, PathStrokeType (1.1f));
    g.setColour (Colour (0xffb8432e).withAlpha (0.85f));
    g.strokePath (redArc, PathStrokeType (3.2f));

    const float marks[] = { -20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3 };
    g.setFont (Art::silk (10.5f));
    for (auto db : marks)
    {
        const auto a = angleFor (db);
        g.setColour (db > 0 ? Colour (0xffb8432e) : Colour (0xff2b2420));
        g.drawLine (Line<float> (pt (arcR, a), pt (arcR + 5.5f, a)), 1.1f);
        const auto label = (db > 0 ? "+" : "") + String ((int) std::abs (db));
        const auto lp = pt (arcR + 11.0f, a);
        g.drawText (label, Rectangle<float> (lp.x - 12.0f, lp.y - 6.0f, 24.0f, 12.0f), Justification::centred);
    }
    for (float db = -18.0f; db < 3.0f; db += 1.0f)
    {
        g.setColour (Colour (0xff2b2420).withAlpha (0.5f));
        const auto a = angleFor (db);
        g.drawLine (Line<float> (pt (arcR, a), pt (arcR + 2.5f, a)), 0.6f);
    }

    g.setColour (Colour (0xff2b2420).withAlpha (0.85f));
    g.setFont (Art::silk (17.0f));
    g.drawText ("VU", Rectangle<float> (face.getCentreX() - 20.0f, face.getBottom() - 30.0f, 40.0f, 18.0f), Justification::centred);
    g.setFont (Art::silkLight (7.0f));
    g.setColour (Colour (0xff2b2420).withAlpha (0.55f));
    g.drawText ("DUST PLANET", Rectangle<float> (face.getX() + 5.0f, face.getY() + 3.0f, face.getWidth() * 0.5f, 9.0f), Justification::left);

    // Housing screws
    for (auto sp : { b.getTopLeft() + Point<float> (5.5f, 5.0f), b.getTopRight() + Point<float> (-5.5f, 5.0f),
                     b.getBottomLeft() + Point<float> (5.5f, -5.0f), b.getBottomRight() + Point<float> (-5.5f, -5.0f) })
        Art::drawScrew (g, sp, 2.6f, r.nextFloat() * 3.0f);
}

void VuMeter::renderGlass()
{
    glassLayer = makeLayer (getLocalBounds());
    Graphics g (glassLayer);
    g.addTransform (AffineTransform::scale (imageScale));
    const auto face = faceArea();
    Random r (7);

    // Pivot shroud hides the needle root
    g.setColour (Colour (0xff1e1814));
    Path shroud;
    shroud.addRoundedRectangle (face.getCentreX() - face.getWidth() * 0.22f, face.getBottom() - 9.0f, face.getWidth() * 0.44f, 12.0f, 3.0f);
    g.saveState();
    g.reduceClipRegion (face.toNearestInt());
    g.fillPath (shroud);
    g.restoreState();

    // Inner shadow from the bezel
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (Colours::black.withAlpha (0.12f - 0.025f * (float) i));
        g.drawRoundedRectangle (face.reduced ((float) i), 3.0f, 1.2f);
    }

    // Glass glare: a diagonal band, a little hazy, plus hairline scratches
    Path glare;
    glare.startNewSubPath (face.getX(), face.getY());
    glare.lineTo (face.getX() + face.getWidth() * 0.55f, face.getY());
    glare.lineTo (face.getX() + face.getWidth() * 0.25f, face.getBottom());
    glare.lineTo (face.getX(), face.getBottom());
    glare.closeSubPath();
    ColourGradient glareGrad (Colour (0x34ffffff), face.getX(), face.getY(), Colour (0x00ffffff), face.getX() + face.getWidth() * 0.45f, face.getBottom(), false);
    g.setGradientFill (glareGrad);
    g.fillPath (glare);
    for (int i = 0; i < 9; ++i)
    {
        const auto x = face.getX() + r.nextFloat() * face.getWidth(), y = face.getY() + r.nextFloat() * face.getHeight();
        g.setColour (Colours::white.withAlpha (0.12f + r.nextFloat() * 0.12f));
        g.drawLine (x, y, x + (r.nextFloat() - 0.5f) * 30.0f, y + (r.nextFloat() - 0.5f) * 8.0f, 0.5f);
    }
}

void VuMeter::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.drawImage (faceLayer, b);

    // Needle
    {
        Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (faceArea().toNearestInt());
        const auto a = angleFor (position);
        const auto p = pivot();
        const auto len = needleLength();
        const Point<float> tip { p.x + len * std::sin (a), p.y - len * std::cos (a) };
        g.setColour (Colours::black.withAlpha (0.18f));
        g.drawLine (Line<float> (p.translated (1.5f, 1.0f), tip.translated (1.5f, 1.0f)), 1.3f);
        g.setColour (Colour (0xff1c1612));
        g.drawLine (Line<float> (p, tip), 1.2f);
    }

    g.drawImage (glassLayer, b);
}
