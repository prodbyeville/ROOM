# Room Synthesis Engine (Python prototype)

Upload any recording (room tone, a clap, a one-shot, found sound) and this tool analyzes it, derives a "room" from it, and applies that room to other audio. The processed sound picks up the decay and resonant character of wherever the original recording came from.

This is **not** convolution with the uploaded file. The recording is measured, and a brand new room is built from the measurements:

- a **synthetic impulse response** made from filtered noise, decaying band by band at the measured rates (the *spatial* path)
- a **tone-match EQ** that moves the target's overall tone toward the recording's (dark, bright, boxy, thin), plus a **resonant filter bank** tuned to the sharp peaks in the recording's spectrum (together, the *tonal* path)

A **balance score** from 0 (tonal) to 1 (spatial) is detected from the recording itself and decides how much of each path you hear.

This phase is an offline, file-in / file-out prototype for judging the algorithm by ear. No plugin, no UI, no real-time.

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
