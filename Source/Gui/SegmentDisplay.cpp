#include "SegmentDisplay.h"
#include "Art.h"

using namespace juce;

namespace
{
// Segment bits: A top, B upper right, C lower right, D bottom, E lower left, F upper left,
// G1/G2 middle halves, H/K upper diagonals, J/M centre verticals, L/N lower diagonals.
enum Seg : uint16 { A = 1 << 0, B = 1 << 1, C = 1 << 2, D = 1 << 3, E = 1 << 4, F = 1 << 5, G1 = 1 << 6, G2 = 1 << 7,
                    H = 1 << 8, J = 1 << 9, K = 1 << 10, L = 1 << 11, M = 1 << 12, N = 1 << 13 };
constexpr int numSegments = 14;

uint16 glyph (juce_wchar ch)
{
    switch (CharacterFunctions::toUpperCase (ch))
    {
        case '0': return A | B | C | D | E | F | K | L;
        case '1': return B | C;
        case '2': return A | B | G1 | G2 | E | D;
        case '3': return A | B | G2 | C | D;
        case '4': return F | G1 | G2 | B | C;
        case '5': return A | F | G1 | G2 | C | D;
        case '6': return A | F | E | D | C | G1 | G2;
        case '7': return A | B | C;
        case '8': return A | B | C | D | E | F | G1 | G2;
        case '9': return A | B | C | D | F | G1 | G2;
        case 'A': return A | B | C | E | F | G1 | G2;
        case 'B': return A | B | C | D | G2 | J | M;
        case 'C': return A | D | E | F;
        case 'D': return A | B | C | D | J | M;
        case 'E': return A | D | E | F | G1;
        case 'F': return A | E | F | G1;
        case 'G': return A | C | D | E | F | G2;
        case 'H': return B | C | E | F | G1 | G2;
        case 'I': return A | D | J | M;
        case 'J': return B | C | D | E;
        case 'K': return E | F | G1 | K | N;
        case 'L': return D | E | F;
        case 'M': return B | C | E | F | H | K;
        case 'N': return B | C | E | F | H | N;
        case 'O': return A | B | C | D | E | F;
        case 'P': return A | B | E | F | G1 | G2;
        case 'Q': return A | B | C | D | E | F | N;
        case 'R': return A | B | E | F | G1 | G2 | N;
        case 'S': return A | F | G1 | G2 | C | D;
        case 'T': return A | J | M;
        case 'U': return B | C | D | E | F;
        case 'V': return E | F | L | K;
        case 'W': return B | C | E | F | L | N;
        case 'X': return H | K | L | N;
        case 'Y': return H | K | M;
        case 'Z': return A | D | K | L;
        case '-': return G1 | G2;
        case '+': return G1 | G2 | J | M;
        case '/': return K | L;
        case '\'': return J;
        default:  return 0;
    }
}

// The display's faults, by cell
struct Fault { int cell; uint16 dead; uint16 loose; uint16 stuck; float brightness; };
constexpr Fault faults[] = {
    { 1, 0, 0, 0, 0.8f },          // program digit a little tired
    { 5, G2, 0, 0, 1.0f },         // dead middle-right segment
    { 8, 0, B | C, 0, 1.0f },      // loose contact: right side flickers
    { 11, 0, 0, 0, 0.42f },        // this tube has aged
    { 13, D, 0, 0, 1.0f },         // dead bottom segment
    { 17, A, 0, J, 1.0f },         // dead top, centre stroke stuck faintly on
};

const Fault* faultFor (int cell)
{
    for (const auto& f : faults)
        if (f.cell == cell) return &f;
    return nullptr;
}

const Colour neon = Art::Colours::ledRed;
const Colour neonHot (0xffffcf9e);
constexpr float skew = 0.12f;
} // namespace

SegmentDisplay::SegmentDisplay()
{
    setMouseCursor (MouseCursor::PointingHandCursor);
    setTitle ("Preset display");
}

void SegmentDisplay::setProgram (int number, const String& newName)
{
    programText = String (number).paddedLeft ('0', 2);
    if (newName != name)
    {
        name = newName;
        marqueeOffset = 0.0f;
        holdTime = 0.0f;
        setDescription (name);
    }
    repaint();
}

void SegmentDisplay::advance (float dt)
{
    time += dt;

    // Marquee: hold at the start, then scroll the name through at ~4 characters per second
    if (name.length() > nameCells)
    {
        holdTime += dt;
        if (holdTime > 1.6f)
        {
            marqueeOffset += dt * 4.0f;
            const auto loopLength = (float) (name.length() + 4);
            if (marqueeOffset >= loopLength)
            {
                marqueeOffset = 0.0f;
                holdTime = 0.0f;
            }
        }
    }

    // Multiplex shimmer, the odd brown-out, and a contact that can't decide
    shimmer = 0.92f + 0.08f * rng.nextFloat();
    if (rng.nextFloat() < 0.012f) shimmer *= 0.55f;
    if (rng.nextFloat() < (looseContactOn ? 0.08f : 0.25f)) looseContactOn = ! looseContactOn;

    repaint();
}

String SegmentDisplay::visibleText() const
{
    if (name.length() <= nameCells)
        return name.paddedRight (' ', nameCells);

    const auto loop = name + "    ";
    const auto start = (int) marqueeOffset;
    String out;
    for (int i = 0; i < nameCells; ++i)
        out << loop[(start + i) % loop.length()];
    return out;
}

Rectangle<float> SegmentDisplay::cellArea (int cell) const
{
    const auto inner = getLocalBounds().toFloat().reduced (14.0f, 12.0f);
    const auto cw = inner.getWidth() / (float) totalCells;
    return { inner.getX() + cw * (float) cell, inner.getY(), cw, inner.getHeight() };
}

void SegmentDisplay::drawCell (Graphics& g, int cell, uint16 mask, Rectangle<float> area) const
{
    const auto cw = area.getWidth() * 0.78f;
    const auto t = cw * 0.15f;
    const auto x0 = area.getX() + t * 0.5f, x1 = area.getX() + cw - t * 0.5f, xm = (x0 + x1) * 0.5f;
    const auto y0 = area.getY() + t * 0.5f, y1 = area.getBottom() - t * 0.5f, ym = (y0 + y1) * 0.5f;
    const auto gap = t * 0.55f;

    auto skewed = [&] (Point<float> p) { return Point<float> (p.x + (area.getBottom() - p.y) * skew, p.y); };
    auto seg = [&] (Point<float> a, Point<float> b)
    {
        const Line<float> l (a, b);
        const auto len = l.getLength();
        return Line<float> (skewed (l.getPointAlongLine (gap)), skewed (l.getPointAlongLine (len - gap)));
    };

    const Line<float> lines[numSegments] = {
        seg ({ x0, y0 }, { x1, y0 }), seg ({ x1, y0 }, { x1, ym }), seg ({ x1, ym }, { x1, y1 }), seg ({ x0, y1 }, { x1, y1 }),
        seg ({ x0, ym }, { x0, y1 }), seg ({ x0, y0 }, { x0, ym }), seg ({ x0, ym }, { xm, ym }), seg ({ xm, ym }, { x1, ym }),
        seg ({ x0, y0 }, { xm, ym }), seg ({ xm, y0 }, { xm, ym }), seg ({ x1, y0 }, { xm, ym }), seg ({ xm, ym }, { x0, y1 }),
        seg ({ xm, ym }, { xm, y1 }), seg ({ xm, ym }, { x1, y1 })
    };

    const auto* fault = faultFor (cell);
    const auto cellBrightness = (fault != nullptr ? fault->brightness : 1.0f) * shimmer;
    const PathStrokeType::EndCapStyle cap = PathStrokeType::rounded;

    for (int s = 0; s < numSegments; ++s)
    {
        const auto bit = (uint16) (1 << s);
        auto level = (mask & bit) != 0 ? 1.0f : 0.0f;
        if (fault != nullptr)
        {
            if ((fault->dead & bit) != 0)                       level = 0.0f;
            if ((fault->loose & bit) != 0 && ! looseContactOn)  level = 0.0f;
            if ((fault->stuck & bit) != 0)                      level = jmax (level, 0.3f);
        }

        Path p;
        p.startNewSubPath (lines[s].getStart());
        p.lineTo (lines[s].getEnd());

        if (level <= 0.0f)
        {
            // Unlit segments are still faintly visible through the lens
            g.setColour (neon.withAlpha (0.07f));
            g.strokePath (p, PathStrokeType (t, PathStrokeType::curved, cap));
            continue;
        }

        const auto b = level * cellBrightness;
        g.setColour (neon.withAlpha (0.07f * b));
        g.strokePath (p, PathStrokeType (t * 3.4f, PathStrokeType::curved, cap));
        g.setColour (neon.withAlpha (0.2f * b));
        g.strokePath (p, PathStrokeType (t * 1.9f, PathStrokeType::curved, cap));
        g.setColour (neon.withAlpha (0.95f * b));
        g.strokePath (p, PathStrokeType (t, PathStrokeType::curved, cap));
        g.setColour (neonHot.withAlpha (0.7f * b));
        g.strokePath (p, PathStrokeType (t * 0.3f, PathStrokeType::curved, cap));
    }
}

void SegmentDisplay::paint (Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    Art::drawRecess (g, b.reduced (2.0f), 3.0f);

    // Smoked lens with a warm cast from the tubes behind it
    const auto lens = b.reduced (4.0f);
    ColourGradient smoke (Colour (0xff1c0606), lens.getCentreX(), lens.getCentreY(), Colour (0xff070302), lens.getX(), lens.getY(), true);
    g.setGradientFill (smoke);
    g.fillRoundedRectangle (lens, 2.0f);

    // Program number, a separator, then the name
    drawCell (g, 0, glyph (programText[0]), cellArea (0));
    drawCell (g, 1, glyph (programText[1]), cellArea (1));
    drawCell (g, 2, 0, cellArea (2));
    const auto text = visibleText();
    for (int i = 0; i < nameCells; ++i)
        drawCell (g, 3 + i, glyph (text[i]), cellArea (3 + i));

    g.drawImage (glass, b);
}

void SegmentDisplay::resized()
{
    renderGlass();
}

void SegmentDisplay::renderGlass()
{
    constexpr float scale = 2.0f;
    glass = Image (Image::ARGB, jmax (1, getWidth() * 2), jmax (1, getHeight() * 2), true);
    Graphics g (glass);
    g.addTransform (AffineTransform::scale (scale));
    const auto lens = getLocalBounds().toFloat().reduced (4.0f);
    Random r (31);

    // Glare from the studio lights
    ColourGradient glare (Colours::white.withAlpha (0.10f), lens.getX(), lens.getY(), Colours::white.withAlpha (0.0f), lens.getX(), lens.getCentreY(), false);
    g.setGradientFill (glare);
    g.fillRoundedRectangle (lens.withHeight (lens.getHeight() * 0.5f), 2.0f);

    // A crack from a knock on the top edge, with a couple of branches
    Path crack;
    Point<float> p { lens.getX() + lens.getWidth() * 0.63f, lens.getY() };
    crack.startNewSubPath (p);
    std::vector<Point<float>> joints;
    while (p.y < lens.getBottom())
    {
        p += { (r.nextFloat() - 0.62f) * 9.0f, 3.0f + r.nextFloat() * 6.0f };
        p.y = jmin (p.y, lens.getBottom());
        crack.lineTo (p);
        joints.push_back (p);
    }
    for (int i = 0; i < 2; ++i)
    {
        auto q = joints[(size_t) r.nextInt ((int) joints.size())];
        Path branch;
        branch.startNewSubPath (q);
        for (int k = 0; k < 3; ++k)
        {
            q += { (r.nextBool() ? 1.0f : -1.0f) * (3.0f + r.nextFloat() * 6.0f), (r.nextFloat() - 0.3f) * 5.0f };
            branch.lineTo (q);
        }
        crack.addPath (branch);
    }
    g.setColour (Colours::black.withAlpha (0.55f));
    g.strokePath (crack, PathStrokeType (1.4f));
    g.setColour (Colours::white.withAlpha (0.28f));
    g.strokePath (crack, PathStrokeType (0.5f), AffineTransform::translation (0.6f, 0.0f));

    // Fine scratches and a fingerprint smear on the acrylic
    for (int i = 0; i < 26; ++i)
    {
        const auto x = lens.getX() + r.nextFloat() * lens.getWidth(), y = lens.getY() + r.nextFloat() * lens.getHeight();
        g.setColour (Colours::white.withAlpha (0.05f + r.nextFloat() * 0.1f));
        g.drawLine (x, y, x + (r.nextFloat() - 0.5f) * 40.0f, y + (r.nextFloat() - 0.5f) * 6.0f, 0.4f);
    }
    const auto fx = lens.getX() + lens.getWidth() * 0.22f, fy = lens.getCentreY() + 4.0f;
    for (int i = 0; i < 9; ++i)
    {
        const auto rr = 4.0f + (float) i * 2.2f;
        Path whorl;
        whorl.addCentredArc (fx, fy, rr, rr * 0.75f, 0.2f, 0.4f, 5.2f, true);
        g.setColour (Colours::white.withAlpha (0.035f));
        g.strokePath (whorl, PathStrokeType (0.8f));
    }

    // Dust caught in the bezel edge
    for (int i = 0; i < 40; ++i)
    {
        const auto onTop = r.nextBool();
        const auto x = lens.getX() + r.nextFloat() * lens.getWidth();
        const auto y = onTop ? lens.getY() + r.nextFloat() * 2.0f : lens.getBottom() - r.nextFloat() * 2.0f;
        g.setColour (Colour (0xffcfc9bd).withAlpha (0.15f + r.nextFloat() * 0.25f));
        g.fillEllipse (x, y, 0.8f, 0.8f);
    }
}

void SegmentDisplay::mouseUp (const MouseEvent& e)
{
    if (! e.mouseWasDraggedSinceMouseDown() && onClick != nullptr)
        onClick();
}
