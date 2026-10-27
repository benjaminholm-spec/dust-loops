# Changelog

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
