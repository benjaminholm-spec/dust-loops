#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Procedural drawing helpers for a worn black 70s/80s instrument. Everything is drawn in code;
// the only bitmap is the Dust Planet Records logo.
namespace Art
{
namespace Colours
{
inline const juce::Colour panel      { 0xff141414 }; // black powder-coated steel
inline const juce::Colour panelLift  { 0xff1c1c1c };
inline const juce::Colour silk       { 0xffd8d4cb }; // off-white silkscreen
inline const juce::Colour silkDim    { 0xff8e8a83 };
inline const juce::Colour line       { 0xff5a5852 }; // printed rules
inline const juce::Colour bareMetal  { 0xff8d8c87 };
inline const juce::Colour ledOrange  { 0xffff6a1c };
inline const juce::Colour ledRed     { 0xffff2a1a }; // old red LED displays

// Dust Planet Records brand palette
inline const juce::Colour brandGold   { 0xffcdaa5f };
inline const juce::Colour brandBlue   { 0xff5f82cd };
inline const juce::Colour brandPurple { 0xff6a5d81 };
inline const juce::Colour copper      { 0xffb4674a };
} // namespace Colours

juce::Font silk (float height);       // condensed bold sans, like 80s panel legends
juce::Font silkLight (float height);  // medium weight for small print
juce::Font brand (float height);      // heavy geometric sans, close to the Dust Planet wordmark

// The Dust Planet Records logo: the "DR" monogram on its own, and the full horizontal lockup.
// Both are black on transparent; drawPrintedImage tints them.
juce::Image logoMonogram();
juce::Image logoHorizontal();
void drawPrintedImage (juce::Graphics&, const juce::Image&, juce::Rectangle<float> area, juce::Colour ink);

// `area` is in logical coordinates; `scale` is the image's pixels-per-unit (2 on Retina).
void drawBlackPanel (juce::Image& target, juce::Rectangle<int> area, float scale, juce::uint32 seed);
// Scratches, paint chips down to bare metal, rubbed edges, finger smudges and dust.
void drawPanelWear (juce::Graphics&, juce::Rectangle<float> area, juce::uint32 seed);
// Black anodised aluminium end rail.
void drawRail (juce::Graphics&, juce::Rectangle<float> area, juce::uint32 seed);
void drawScrew (juce::Graphics&, juce::Point<float> centre, float radius, float angle);
// A recessed window (display or meter): dark lip and inner shadow.
void drawRecess (juce::Graphics&, juce::Rectangle<float> area, float cornerSize);
// Rub off parts of the silkscreen with panel-coloured specks.
void wearPrint (juce::Graphics&, juce::Rectangle<float> area, int count, juce::uint32 seed);
} // namespace Art
