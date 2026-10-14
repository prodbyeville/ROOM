#!/usr/bin/env bash
# Regenerates the built-in "Factory" ROOMS (.room) and STUDIOS (.studio) in
# plugin/Presets/Factory/.
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
rm -f "$OUT"/*.room "$OUT"/*.studio
A=examples/audio
TMP=$(mktemp -d)

# Each source sound is one factory ROOM, and the STUDIOS below are built on them.
room () {   # source  room-name  type  character  notes
    "$RENDER" "$A/$1.wav" "$A/melody.wav" "$TMP/out.wav" \
        --room-name "$2" --save-room "$OUT/$2.room" --room-type "$3" --room-styles "$4" --room-notes "$5" > /dev/null
    echo "  ROOM    $2"
}

studio () {   # source  room-name  name  type  styles  made-for  notes  [extra room_render options...]
    local src=$1 rname=$2 name=$3 type=$4 styles=$5 madefor=$6 notes=$7; shift 7
    "$RENDER" "$A/$src.wav" "$A/melody.wav" "$TMP/out.wav" --room-name "$rname" \
        --save-preset "$OUT/$name.studio" --name "$name" --type "$type" --styles "$styles" \
        --made-for "$madefor" --notes "$notes" "$@" > /dev/null
    echo "  STUDIO  $name"
}

room hall_clap        "Clap Hall"     Hall          "Ambient,Wide,Airy"        "A handclap in a big, open hall."
room small_room_clap  "Small Room"    "Small Space" "Tight,Clean"              "A clap in a small, fairly dead room."
room bottle_tap       "Glass Bottle"  Object        "Bright,Metallic"          "A tapped bottle: rings at its notes."
room metal_pipe       "Metal Pipe"    Object        "Metallic,Experimental"    "A struck pipe: inharmonic, long ringing."
room room_tone        "Room Tone"     Texture       "Warm,Dirty"               "Hum and air from a quiet room."
room drum_oneshot     "Drum Booth"    Room          "Tight,Dirty"              "A drum hit in a small booth."

studio hall_clap        "Clap Hall"    "Big Hall"        Hall          "Ambient,Wide,Airy"          "Vocals,Keys"        "A long, open hall."                       --mix 0.45 --space 1.3
studio hall_clap        "Clap Hall"    "Dark Hall"       Hall          "Dark,Ambient"               "Vocals,Guitar"      "The hall, through old tape."              --mix 0.45 --tape 0.5 --eq 5:8000:-4:0.71
studio hall_clap        "Clap Hall"    "Air Chamber"     Chamber       "Bright,Airy,Clean"          "Drums,Vocals"       "Shorter and brighter than the hall."      --intensity 0.6 --mix 0.4 --auto 0.4 --eq 5:8000:3:0.71
studio hall_clap        "Clap Hall"    "Glass Plate"     Plate         "Bright,Clean,Wide"          "Vocals,Synth"       "Smooth, bright, all space."               --intensity 0.8 --balance 0.9 --mix 0.4 --space 1.4
studio small_room_clap  "Small Room"   "Small Room"      "Small Space" "Tight,Clean"                "Drums,Guitar"       "A small, fairly dead room."               --mix 0.35
studio small_room_clap  "Small Room"   "Warm Booth"      "Small Space" "Warm,Tight"                 "Vocals,Bass"        "Close and warm."                          --mix 0.4 --tape 0.3 --eq 1:100:2:0.71
studio bottle_tap       "Glass Bottle" "Glass Bottle"    Object        "Bright,Metallic"            "FX,Keys"            "Rings at the bottle's notes."             --mix 0.5
studio bottle_tap       "Glass Bottle" "Bottle Lo-Fi"    Lo-Fi         "Vintage,Metallic"           "Drums,Keys"         "The bottle, on a worn tape."              --mix 0.5 --tape 0.7
studio metal_pipe       "Metal Pipe"   "Metal Pipe"      Object        "Metallic,Experimental"      "FX,Drums"           "Inharmonic, long ringing."                --mix 0.4
studio metal_pipe       "Metal Pipe"   "Pipe Space"      FX            "Metallic,Wide,Experimental" "FX,Synth"           "The pipe, stretched and wide."            --intensity 1.5 --mix 0.45 --space 1.6
studio room_tone        "Room Tone"    "Room Tone"       Texture       "Warm,Dirty"                 "Bus/Master,Keys"    "Hum and air from a quiet room."           --mix 0.4 --tape 0.4
studio room_tone        "Room Tone"    "Worn Tape Room"  Lo-Fi         "Vintage,Dirty"              "Bus/Master,Drums"   "Old tape, quiet room, tamed highs."       --mix 0.45 --tape 0.9 --auto 0.3

rm -rf "$TMP"
