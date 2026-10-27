# Changelog

## 0.3.0 — Pedalboard (2026-10-27)
Five stompboxes under the deck, joined by patch cables:
- **DIRT**: tape drive, then fuzz, then bitcrush, all on one BLEND knob. Also TONE and MIX.
- **WARBLE**: BBD chorus or vibrato, from gentle up to seasick. RATE, DEPTH, MIX and a VIB switch.
- **DRIFT FILTER**: resonant lowpass or bandpass with a very slow sweep (one cycle can take up to 2 minutes) and an envelope follower. FREQ, RESO, DRIFT, SWEEP, plus a BP switch and an ENV trim.
- **CLOUD**: granular pitch smear (octave down, fifth up, octave up, or up and down together) with regeneration, so the sound swells. PITCH, SMEAR, DENSITY and MIX.
- **HALL**: big, dark 8-line FDN reverb with up to 60 s of decay and a lossless FREEZE. DECAY, TONE, MIX and FRZ.

Every pedal has:
- a footswitch, which fades in and out instead of clicking
- an on LED
- a LOOP switch with a LOOP AMOUNT trim

LOOP runs a second copy of the pedal inside the tape's feedback path. The tape's return passes through all the in-loop pedals in board order, and only what they add is re-recorded, scaled by FEED, so the effect builds up on every pass. FREEZE bypasses it.

Drag a pedal (or the TAPE block) by its body to reorder the chain. Pedals left of TAPE go onto the tape; pedals right of it colour what comes off. The cables follow. The order is saved with the project. By default only HALL is on, in LOOP at 15 %.

Other changes:
- The window is now 1000 × 902 at 100 %, and it never opens taller than the screen.
- The chain is processed in fixed-size chunks, so oversized host blocks never allocate.

New tests:
- each pedal stays finite and bounded on loud noise
- HALL freeze holds its tail
- everything maxed inside the tape loop for 60 s keeps the tape below 1.0
- bypassed pedals plus a dry mix are bit-transparent
- pedal order survives text and saved state, and bad input falls back safely

## 0.2.0 — The deck: simple front panel + working tape (2026-10-27)
After feedback that the generic parameter list was impossible to use, the plan was reordered. The real front panel and the tape engine were built together, so every control on screen works.

**Front panel** (black, worn, modelled on a modded cassette player):
- TIME, FEED, MIX and VOLUME knobs on a speaker grille, with label-maker labels.
- The big TAPE SPEED knob, with a printed scale and detents at 1x, 0.5x and -1x.
- The yellow FREEZE button.
- VU meter.
- A, B and C loop REC lights (click to arm), and ERASE.
- LOOP/ECHO and SYNC/FREE toggle switches.
- A cassette with reels that turn at the real tape speed.
- Red 7-segment readouts for loop lengths, speed and tempo.
- Cassette piano keys that stay down while latched, like a real deck.
- A hint strip that explains whatever control the mouse is over.

**Back panel:** per-loop length, level, pan, speed, mute and clear; tape type plus the six wear controls; inertia, brake and master.

**Window:** resizable from 75 to 200 %. The size is remembered in `~/Music/Dust Loops/settings.json`.

**Tape engine:**
- Three loops (LOOP mode), or one tape with three heads (ECHO mode).
- The record and play head moves at the tape speed, so varispeed and reverse are real. A stopping tape slides down in pitch and goes silent.
- Each pass wears the tape: age (treble loss), saturation, hiss, and dropouts that stay on the loop. A soft limiter sits in every feedback path.
- Overdub is always on, and FREEZE is a perfect hold.
- STOP brakes the tape, PAUSE stops quickly and restarts with a wobble, ◀◀ reverses while held and ▶▶ doubles the speed while held.
- In SYNC, loop lengths follow Logic's tempo (16/13/11 beats) and realign when Logic starts playing.
- Pedals aren't in yet. They come next.

**Tests:**
- silence in gives silence out, in both modes
- hiss only when hiss is on
- maximum feedback stays bounded in both modes
- FREEZE holds with no decay
- loop lengths match the host tempo exactly
- heads advance exactly and realign on play
- 6,000 blocks of random automation with no NaN/inf and nothing over the ceiling

Also added the `DustSnapshot` tool, which renders the UI to PNG.

## 0.1.0 — Milestone 1: skeleton (2026-10-27)
- CMake + JUCE 9.0.4 project, arm64, macOS 14+, C++20. Three targets come from one codebase:
  - Dust Loops (AU instrument + Standalone)
  - Dust Loops FX (AU MIDI-controlled effect)
  - unit tests
- Product name, codes and bundle ID are kept in one block at the top of `CMakeLists.txt`.
- Instrument has a stereo **Sidechain** input bus that gets mixed into the signal path.
- The complete parameter set is in place from day one: 146 stable, sectioned IDs across Global, Synth, Atmos, Tape, Transport, Loop A/B/C and five pedals.
  - Parameters are grouped by section in Logic's automation menus.
  - Only Master, Input Gain, Synth Level and the amp envelope do anything yet.
- Temporary sine synth (8 voices).
- Output safety limiter (-0.3 dBFS ceiling) plus NaN/inf scrubbing.
- Functional generic editor with the Dust Planet Records logo.
- `build.sh` builds, runs the tests, installs, ad-hoc signs and runs `auval -strict` on both AUs.
- Tests: silence in gives silence out, a note stays finite and under the ceiling, the limiter holds its ceiling, and parameter IDs are unique.
