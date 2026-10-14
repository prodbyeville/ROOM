# Room Synthesis Engine (Python prototype)

Upload any recording (room tone, a clap, a one-shot, found sound) and this tool analyzes it, derives a "room" from it, and applies that room to other audio. The processed sound picks up the decay and resonant character of wherever the original recording came from.

This is **not** convolution with the uploaded file. The recording is measured, and a brand new room is built from the measurements:

- a **synthetic impulse response** made from filtered noise, decaying band by band at the measured rates (the *spatial* path)
- a **tone-match EQ** that moves the target's overall tone toward the recording's (dark, bright, boxy, thin), plus a **resonant filter bank** tuned to the sharp peaks in the recording's spectrum (together, the *tonal* path)

A **balance score** from 0 (tonal) to 1 (spatial) is detected from the recording itself and decides how much of each path you hear.

This repo has two parts:

- **`plugin/`**: the real-time **VST3 plugin** (C++/JUCE). See [Plugin](#plugin) below.
- **Everything else**: the original offline Python prototype used to design and tune the sound by ear.

## Plugin

**Getting it:** every push builds the Windows VST3 on GitHub. Open the repo's **Actions** tab, click the latest green **Build Windows VST3** run, and download **ROOM-VST3-Windows** under *Artifacts*. Unzip it and follow `INSTALL.txt`.

**What it does:** drop any sound on the plugin. It analyzes it once (in the background) and builds the room. The analysis numbers are saved with your project; the audio file isn't needed afterwards.

**Knobs:** Intensity, Mix, Balance (with Auto = detected), Tail, Output.

**EQ + Tone section** (after the room): a live spectrum display with a 5-band EQ (drag the bands), a **Tape** knob (low and high cuts closing in, down to about 80 Hz to 9 kHz) and **Auto EQ**, a live soothe-style resonance suppressor focused on 2 to 10 kHz. Auto EQ adds one analysis window of latency (2048 samples at 44.1/48 kHz), reported to the DAW.

**Space:** stereo width (mid/side). 0% = mono, 100% = untouched, 200% = wide, with the side signal below about 120 Hz fading out as it widens.

Chain: `room (pre-delay, freeze, ducking) -> Tape -> EQ -> Auto EQ -> Space -> Output -> Bypass`.

**Layout:** a top bar (ROOMS, Advanced, Randomize, Dry/Wet, Out, Bypass) and three tiers: 1 load, 2 shape the room (decay + tone displays, Intensity, Balance, Tail, Space), 3 finish (EQ, Tape, Auto EQ). The **Advanced** button swaps tier 3 for Pre-delay, Size, Damping, Ducking + Release, Freeze and full Room Info. The window is resizable (fixed aspect; the whole UI scales). **LFOs:** two LFOs on the Advanced screen's MODULATION tab (sine, triangle, saws, square, S&H, smooth random; Hz or tempo-synced; retrigger). Drag an LFO onto any knob (or right-click a knob) to assign it, up to 8 targets each. Modulation is applied inside the plugin in 64-sample slices and never moves the parameter itself; Intensity/Tail/Size/Damping modulation rebuilds the room on the worker thread (about 20 times a second). Assignments are stored in the project state and in presets (`MODULATION`). **Record into ROOM:** the record button in tier 1 captures up to 30 s of the track's input (before processing) and analyzes it as the room.

**ROOMS (presets):** `.room` files (small XML: name, author, type, styles, notes, every parameter except Output, and the room analysis) in `Documents/Eville/ROOMS/<pack>/`. Favorites are kept in `ROOMS/favorites.xml`. The built-in **Factory** pack is generated from synthesized sources by `plugin/Tools/make_factory_presets.sh` and compiled into the plugin.

**How it differs from the Python prototype:**
- The tone-match EQ compares the room against a fixed "typical music" tilt (-6 dB/octave), not against the whole target file, because a live plugin can't hear the whole track ahead of time. The constant is `kReferenceTiltDbPerOct` in `plugin/Source/dsp/RoomSynth.cpp`.
- Levels are calibrated with fixed constants instead of per-file level matching.
- Everything else (analysis, balance formula, IR design, resonators) is a direct port and matches the prototype's numbers closely.

**Code map:**

| File | What |
|---|---|
| `plugin/Source/dsp/RoomAnalysis.*` | analysis stages 1 to 5 (port of `analysis.py`) |
| `plugin/Source/dsp/RoomSynth.*` | synthetic IR, tone EQ, resonators, intensity (port of `synthesis.py`) |
| `plugin/Source/dsp/RoomEngine.*` | real-time chain (port of `process.py`) |
| `plugin/Source/dsp/ToneShaper.*` | Tape filter and 5-band EQ |
| `plugin/Source/dsp/ResonanceSuppressor.*` | Auto EQ (live resonance suppression) |
| `plugin/Source/EqPanel.*` | EQ display: spectrum, draggable bands, Auto EQ cuts |
| `plugin/Source/AdvancedPanel.*` | ADVANCED screen: pre-delay, size, damping, ducking, freeze, room info |
| `plugin/Source/dsp/Lfo.h`, `plugin/Source/Modulation.h` | LFO shapes and the LFO -> knob assignment slots |
| `plugin/Source/ModSlider.*`, `plugin/Source/LfoPanel.*` | knobs that accept dropped LFOs (rings, right-click menu) and the LFO modules |
| `plugin/Source/dsp/StereoWidth.h` | Space (stereo width) |
| `plugin/Source/presets/PresetFile.*` | the `.room` format, type and style lists |
| `plugin/Source/presets/PresetLibrary.*` | ROOMS folder: scan, search, sort, favorites, save, edit, delete, import, export |
| `plugin/Source/presets/PresetBrowser.*` | preset bar, browser and save dialog |
| `plugin/Tools/RoomTests.cpp` | `room_tests`: presets, Space and state tests against the real processor |
| `plugin/Source/PluginProcessor.*` | parameters, background analysis thread, saving with the project |
| `plugin/Source/PluginEditor.*` | the simple UI |
| `plugin/Tools/RoomRender.cpp` | `room_render`: runs the plugin DSP on files, for testing |
| `plugin/Tools/HostTest.cpp` | `room_hosttest`: loads the built VST3 like a DAW and tests it |

**Building it yourself** (optional): `cmake -S plugin -B build && cmake --build build --config Release`. JUCE 8.0.15 is downloaded automatically.

## Python prototype

The prototype is offline, file-in / file-out, for judging the algorithm by ear.

## Setup (once)

```bash
pip install -r requirements.txt
```

## Quick start

```bash
# 1. Make some test sounds (a hall clap, a glass bottle tap, room tone, a drum hit, a melody)
python examples/make_test_audio.py

# 2. Put the drum in the "hall"
python room.py examples/audio/hall_clap.wav examples/audio/drum_oneshot.wav -o renders/drum_in_hall.wav

# 3. Put the melody "inside the bottle"
python room.py examples/audio/bottle_tap.wav examples/audio/melody.wav -o renders/melody_in_bottle.wav
```

With your own sounds:

```bash
python room.py path/to/your_recording.wav path/to/your_loop.wav -o renders/out.wav
```

(Create the `renders/` folder first, or point `-o` anywhere you like.)

## Knobs

| Option | Default | What it does |
|---|---|---|
| `--intensity` | 1.0 | How strongly the room's character is applied. 0 = none, 1 = as measured, 2 = exaggerated. Scales the EQ match, the resonances and the decay length together |
| `--mix` | 0.5 | Wet/dry. 0 = untouched, 1 = fully processed |
| `--balance` | auto | Force the blend. 0 = only resonances, 1 = only reverb tail. Leave out to use the detected value |
| `--tail` | 2.5 | Length of the synthetic tail in seconds (also the longest decay it will measure) |
| `--bands` | 16 | How many frequency bands to measure decay in. 12 to 24 is sensible |
| `--peaks` | 6 | Max number of resonances to pick up |
| `--seed` | 0 | Changes the random noise in the tail. Same seed = same sound every time |
| `--no-report` | off | Skip writing the report files |

All defaults are provisional prototype values, not final.

## What you get

For `-o renders/out.wav` the tool writes:

- `out.wav`: the processed audio (24-bit, stereo, tail included)
- `out_ir.wav`: the synthetic impulse response it built, so you can listen to the "room" on its own
- `out_room.png`: a picture of what it found (decay per band, spectrum with peaks, the IR, the balance)
- `out_room.json`: the same numbers in text form

The terminal also prints a short summary, for example:

```
  Decay (T60):  0.34s to 1.90s across 16/16 measurable bands (median 1.24s)
  Resonances:   1 peak(s)
                  1838.6 Hz   + 4.3 dB   Q  6.7
  Late energy:  51% arrives after 50 ms
  Balance:      0.70  [0 = tonal, 1 = spatial]  detected
```

## How it works

```
room source ──► ANALYSIS ──► SYNTHESIS ──► Room
                                             │
target audio ──► tone-match EQ ──► resonant filters ──► convolve with synthetic IR
                      │ (tonal)              │ (spatial)
                      └──── blend by balance ┘
                                 │
                       wet/dry against original ──► output
```

| File | Stage |
|---|---|
| `room_engine/analysis.py` | 1. split into log-spaced bands, 2. measure decay (T60) per band, 3. averaged spectrum, 4. find resonant peaks, 5. balance score |
| `room_engine/synthesis.py` | build the synthetic IR, the tone curve and the resonant filter bank; apply intensity |
| `room_engine/process.py` | the processing chain above |
| `room_engine/report.py` | summary, JSON and plot |
| `room.py` | command line |

Every stage is commented in the code.

### The balance formula (the main thing to tune)

Lives in `balance_score()` in `room_engine/analysis.py`. Right now:

- **Spatial evidence** rises with a long median decay, with decays that are similar across all bands (a real room rings everywhere, a bottle only at a few notes), and with lots of late energy.
- **Tonal evidence** rises with tall, narrow spectral peaks.
- `balance = spatial / (spatial + tonal)`

On the test sounds: hall clap = 0.70, room tone = 0.29, bottle tap = 0.12. On real sources: tape hiss = 0.50 (steady, neutral), 4-track stop = 0.22, a dry kick = 0.04.

### Known limits

- **Steady sounds** (tape hiss, room tone) have no decay to measure. The tool detects them, uses a short fallback tail (0.4 s) and starts the balance at a neutral 0.5; their color comes through the tone-match EQ and any resonances.
- **The tone-match EQ depends on the target.** It compares the target's tone to the room source's, so the same room EQs a bright synth and a dark pad differently. Strength is `EQ_MATCH_DEPTH` (0.6) and the limit is ±12 dB (`EQ_MAX_DB`), both in `synthesis.py`.
- **Very ringy sources** can measure 40+ dB resonances; boosts are capped at 18 dB (`MAX_RESONANCE_GAIN_DB` in `synthesis.py`) so the target stays recognizable.
- The lowest band (around 60 to 85 Hz) is the least reliable decay measurement on short files.
- The synthetic test sounds are only a sanity check. Real recordings are the real test.

## Not decided yet

Plugin name, final default values, the final balance/mix formula, and how this ships are all open.
