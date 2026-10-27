# Dust Loops

A tape-loop drone instrument and effect for Logic Pro, from Dust Planet Records. It's a modded cassette
player (QF-XL style: speed, time, feedback, mix) with three phasing tape loops and a small pedalboard.

| Target | Format | AU type / codes | Use it on |
|---|---|---|---|
| **Dust Loops** | AU instrument + Standalone | `aumu Dlp1 Dplp` | Software instrument tracks (synth → tape → pedals; sidechain input feeds the tape) |
| **Dust Loops FX** | AU MIDI-controlled effect | `aumf Dlx1 Dplp` | Audio tracks and buses (input → tape → pedals) |

## Requirements
- Apple Silicon Mac, macOS 14 or newer. Developed on macOS 14.3.1 with Logic Pro 11.0.1.
- Xcode Command Line Tools (`xcode-select --install`), CMake 3.22 or newer, and Ninja (`brew install cmake ninja`).
- JUCE 9.0.4. `build.sh` clones it into `JUCE/` automatically if that folder is missing. It isn't committed to git.

## Build, install and validate
```bash
./build.sh
```
This does the following:
1. Configures and builds the instrument, the FX and the Standalone app (Release, Ninja) into `build/`.
2. Runs the unit tests.
3. Copies both components to `~/Library/Audio/Plug-Ins/Components` and ad-hoc signs them.
4. Restarts the AU registrar and runs `auval -strict` on both AUs.

The script stops on the first failure. The full auval logs are in `build/auval-*.log`.

Variations:
```bash
SKIP_AUVAL=1 ./build.sh       # quicker iteration
CONFIG=Debug ./build.sh       # debug build in build-debug/
open "build/DustLoops_artefacts/Release/Standalone/Dust Loops.app"
```

If Logic doesn't show a new build: quit Logic, run `./build.sh`, and reopen Logic. If it still doesn't appear, go to **Logic Pro → Settings → Plug-in Manager**, select the Dust Loops entries and click **Reset & Rescan Selection**.

## Using it in Logic

### Instrument
Create a Software Instrument track, then **Instrument slot → AU Instruments → Dust Planet Records → Dust Loops**.

### Feeding another track into the tape (sidechain)
1. Put Dust Loops on a Software Instrument track.
2. In the plugin window's header bar, open the **Side Chain** menu and choose the track or bus you want to record. For example, a guitar track or a bus with your drums.
3. That audio now goes into Dust Loops on top of the synth, through the pre-tape pedals and onto the tape. **Input Gain** sets its level.

The source track still plays through its own channel as normal. Turn it down or mute its output if you only want to hear it through the tape.

### Effect
On an audio track or bus: **Audio FX slot → Audio Units → Dust Planet Records → Dust Loops FX**.

Dust Loops FX is a *MIDI-controlled effect*. To control REC arm, FREEZE and so on from a MIDI keyboard, pick the keyboard's track in the plugin header's **Side Chain** menu. In Logic, that menu also lists MIDI sources for MIDI-controlled effects.

## Project layout
```
CMakeLists.txt      targets, plus the identity block at the top (name, codes, bundle ID)
build.sh            build → test → install → sign → auval
Source/Core/        Identity.h (product name constant), Params.{h,cpp} (every parameter ID)
Source/DSP/         allocation-free DSP building blocks (Safety.h: NaN scrub, limiters)
Source/Synth/       synth engine
Source/Plugin/      processor + editor (one class serves both plugins; DUST_LOOPS_FX switches the synth out)
tests/              unit tests (run by build.sh, or: build/DustLoopsTests_artefacts/Release/DustLoopsTests)
assets/             Dust Planet Records logo
presets/            factory presets as JSON (from milestone 2)
```

## Renaming
Change `DL_PRODUCT_NAME` (and the codes and bundle ID, if you like) at the top of `CMakeLists.txt`, then delete `build/` and run `./build.sh`. Saved projects keep working, because the internal state ID doesn't depend on the name. If you change the codes, though, Logic will see it as a new plugin.

## Parameter IDs
Every parameter ID is sectioned (`synth.cutoff`, `tape.feed`, `loopB.level`, `pedal.hall.mix`, ...) and versioned. IDs are never renamed or reused once they ship, so Logic automation and saved projects stay valid.
