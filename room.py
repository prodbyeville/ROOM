#!/usr/bin/env python3
"""
Room Synthesis Engine: command-line tool.

Usage:
    python room.py ROOM_SOURCE TARGET [-o OUTPUT.wav] [options]

Example:
    python room.py my_kitchen_clap.wav snare.wav -o snare_in_kitchen.wav --mix 0.6

ROOM_SOURCE   any recording to derive a room from (room tone, clap, one-shot, found sound)
TARGET        the audio to process (a one-shot or a melody)

All defaults are provisional prototype values, not final.
"""

import argparse
import os
import sys

from room_engine.audio_io import load_audio, save_audio, DEFAULT_SR
from room_engine.analysis import analyze_room
from room_engine.synthesis import synthesize_room
from room_engine.process import apply_room
from room_engine import report


def main(argv=None):
    p = argparse.ArgumentParser(
        description="Derive a room from one recording and apply it to another.")
    p.add_argument("source", help="room source recording")
    p.add_argument("target", help="audio to process")
    p.add_argument("-o", "--output", help="output WAV (default: <target>_in_<source>.wav)")
    p.add_argument("--bands", type=int, default=16,
                   help="number of analysis bands, 12-24 is a good range (default 16)")
    p.add_argument("--tail", type=float, default=2.5,
                   help="synthetic IR tail length in seconds (default 2.5)")
    p.add_argument("--peaks", type=int, default=6,
                   help="max number of resonant peaks to track (default 6)")
    p.add_argument("--balance", type=float, default=None,
                   help="override the balance: 0 = fully tonal, 1 = fully spatial "
                        "(default: detected from the source)")
    p.add_argument("--intensity", type=float, default=1.0,
                   help="how strongly the room's character is applied: 0 = none, "
                        "1 = as measured, 2 = exaggerated. Scales EQ, resonances "
                        "and decay length together (default 1.0)")
    p.add_argument("--mix", type=float, default=0.5,
                   help="wet/dry: 0 = dry, 1 = fully processed (default 0.5)")
    p.add_argument("--seed", type=int, default=0,
                   help="random seed for the IR noise (change for a different-sounding tail)")
    p.add_argument("--sr", type=int, default=DEFAULT_SR, help="processing sample rate")
    p.add_argument("--no-report", action="store_true",
                   help="don't write the JSON + PNG analysis report")
    args = p.parse_args(argv)

    if args.output is None:
        s = os.path.splitext(os.path.basename(args.source))[0]
        t = os.path.splitext(args.target)[0]
        args.output = f"{t}_in_{s}.wav"

    print(f"Analyzing room source: {args.source}")
    source, sr = load_audio(args.source, sr=args.sr)
    analysis = analyze_room(source, sr, n_bands=args.bands, n_peaks=args.peaks,
                            max_t60=args.tail)

    room = synthesize_room(analysis, tail_s=args.tail,
                           balance_override=args.balance, seed=args.seed,
                           intensity=args.intensity)
    print(report.summary_text(analysis, room.balance))
    print(f"  Intensity:    {room.intensity:.2f}   Mix: {args.mix:.2f}")

    print(f"Processing: {args.target}")
    target, _ = load_audio(args.target, sr=sr)
    out = apply_room(target, room, mix=args.mix)
    save_audio(args.output, out, sr)
    print(f"Wrote {args.output}  ({out.shape[1] / sr:.2f}s, {out.shape[0]} ch)")

    if not args.no_report:
        base = os.path.splitext(args.output)[0]
        settings = {k: v for k, v in vars(args).items()}
        report.write_json(base + "_room.json", analysis, room.balance, settings)
        save_audio(base + "_ir.wav", room.ir, sr)
        made_png = report.write_plot(base + "_room.png", room)
        extras = "_room.json, _ir.wav" + (", _room.png" if made_png else "")
        print(f"Report: {base}{{{extras}}}")


if __name__ == "__main__":
    sys.exit(main())
