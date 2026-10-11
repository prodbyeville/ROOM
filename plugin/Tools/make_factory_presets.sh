#!/usr/bin/env bash
# Regenerates the built-in "Factory" ROOMS in plugin/Presets/Factory/.
# Every source is synthesized by examples/make_test_audio.py, so no
# third-party audio ends up inside the plugin.
#
#   bash plugin/Tools/make_factory_presets.sh path/to/room_render
set -euo pipefail
RENDER="${1:?usage: make_factory_presets.sh path/to/room_render}"
cd "$(dirname "$0")/../.."
python3 examples/make_test_audio.py > /dev/null
OUT=plugin/Presets/Factory
mkdir -p "$OUT"
rm -f "$OUT"/*.room
A=examples/audio
TMP=$(mktemp -d)

preset () {   # source  name  type  styles  notes  [extra room_render options...]
    local src=$1 name=$2 type=$3 styles=$4 notes=$5; shift 5
    "$RENDER" "$A/$src.wav" "$A/melody.wav" "$TMP/out.wav" \
        --save-preset "$OUT/$name.room" --name "$name" --type "$type" --styles "$styles" \
        --notes "$notes" "$@" > /dev/null
    echo "  $name"
}

preset hall_clap        "Big Hall"        Hall          "Ambient,Wide,Airy"        "A long, open hall."                       --mix 0.45 --space 1.3
preset hall_clap        "Dark Hall"       Hall          "Dark,Ambient"             "The hall, through old tape."              --mix 0.45 --tape 0.5 --eq 5:8000:-4:0.71
preset hall_clap        "Air Chamber"     Chamber       "Bright,Airy,Clean"        "Shorter and brighter than the hall."      --intensity 0.6 --mix 0.4 --auto 0.4 --eq 5:8000:3:0.71
preset hall_clap        "Glass Plate"     Plate         "Bright,Clean,Wide"        "Smooth, bright, all space."               --intensity 0.8 --balance 0.9 --mix 0.4 --space 1.4
preset small_room_clap  "Small Room"      "Small Space" "Tight,Clean"              "A small, fairly dead room."               --mix 0.35
preset small_room_clap  "Warm Booth"      "Small Space" "Warm,Tight"               "Close and warm."                          --mix 0.4 --tape 0.3 --eq 1:100:2:0.71
preset bottle_tap       "Glass Bottle"    Object        "Bright,Metallic"          "Rings at the bottle's notes."             --mix 0.5
preset bottle_tap       "Bottle Lo-Fi"    Lo-Fi         "Vintage,Metallic"         "The bottle, on a worn tape."              --mix 0.5 --tape 0.7
preset metal_pipe       "Metal Pipe"      Object        "Metallic,Experimental"    "Inharmonic, long ringing."                --mix 0.4
preset metal_pipe       "Pipe Space"      FX            "Metallic,Wide,Experimental" "The pipe, stretched and wide."          --intensity 1.5 --mix 0.45 --space 1.6
preset room_tone        "Room Tone"       Texture       "Warm,Dirty"               "Hum and air from a quiet room."           --mix 0.4 --tape 0.4
preset room_tone        "Worn Tape Room"  Lo-Fi         "Vintage,Dirty"            "Old tape, quiet room, tamed highs."       --mix 0.45 --tape 0.9 --auto 0.3

rm -rf "$TMP"
