Noise profiles for the bottom module, made by Tools/noise/make_noise_loops.py from Ethan's
recordings (not kept here: they're 230 MB):

    python3 -I Tools/noise/make_noise_loops.py <folder of source WAVs> Resources/Noise

Each recording becomes "NN name.ogg", a seamless loop (its end flows into its start through
a crossfade at the point where the two match best; the script checks the seam before and
after encoding and fails if it clicks), and a line in noise.txt: number, menu name, menu
section and the noise screen's look. 1-4 (vinyl, tape hiss, air, room tone) are made in
Source/dsp/NoiseBank.cpp.

The number is the noise type parameter's index + 1 and is saved in projects: never
renumber or reuse one, only add new ones at the end (in the script's LOOPS list).
