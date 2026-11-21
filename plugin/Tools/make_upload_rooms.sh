#!/usr/bin/env bash
# Makes the factory ROOMS that come from Eville's own synth and melodic sounds
# in plugin/Presets/Factory/. A .room keeps only the analysis, so the audio itself is not in
# this repo: pass the folder it lives in.
#
#   bash plugin/Tools/make_upload_rooms.sh path/to/room_render path/to/melody.wav "path/to/sounds"
#
# make_factory_presets.sh leaves these .room files alone.
set -euo pipefail
RENDER="$(realpath "${1:?usage: make_upload_rooms.sh room_render melody.wav sounds-folder}")"
TARGET="$(realpath "${2:?usage: make_upload_rooms.sh room_render melody.wav sounds-folder}")"
PACK="$(realpath "${3:?usage: make_upload_rooms.sh room_render melody.wav sounds-folder}")"
cd "$(dirname "$0")/../.."
OUT=plugin/Presets/Factory
TMP=$(mktemp -d)

uproom () {   # file-pattern  room-name  type  character  notes
    local src
    src=$(find "$PACK" -type f -iname "$1" | head -n 1)
    [ -n "$src" ] || { echo "  missing $1" >&2; exit 1; }
    "$RENDER" "$src" "$TARGET" "$TMP/out.wav" \
        --room-name "$2" --save-room "$OUT/$2.room" --room-type "$3" --room-styles "$4" --room-notes "$5" > /dev/null
    echo "  ROOM    $2"
}

uproom "*positivevibrations*"  "Sunlit Glass"   Object   "Bright,Airy"                 "A glassy chime that hangs on in the mids. Rings in F# minor."
uproom "*reframenarrative*"    "Fifth Tower"    FX       "Warm,Ambient"                "Open fifths on F#, with a low end that lingers."
uproom "*moros*"               "Black Choir"    Texture  "Dark,Ambient"                "A dark choir on B and F#. Short tail, deep colour."
uproom "*mindmelody*"          "Dream Vault"    Hall     "Ambient,Warm,Wide"           "Huge and slow: six seconds of C minor in the lows."
uproom "*keres*"               "Ember Room"     Texture  "Warm,Dark"                   "Glowing E and B, close and short."
uproom "*keysforthelife*"      "Old Keys"       Object   "Vintage,Warm"                "An old keyboard's body: C# and G# ringing under everything."
uproom "*energyshapes*"        "Static Garden"  FX       "Airy,Wide,Experimental"      "Highs that hang for seconds over a quick, dry low end."
uproom "*hecate*"              "Moon Chamber"   Chamber  "Dark,Ambient"                "Octaves and fifths of C, four and a half seconds deep."
uproom "*bellsandthecause*"    "Bell Loft"      Object   "Bright,Metallic"             "Bells in the rafters. The top keeps ringing after the rest has gone."
uproom "*worldofsynths*"       "Neon Hall"      FX       "Bright,Experimental"         "Loud E major resonances. Push it past 100% and it sings."
uproom "*masseffect*"          "Orbit"          FX       "Wide,Ambient"                "Wide and floating, with D and G at the centre."
uproom "*wormholestructure*"   "Wormhole"       FX       "Experimental,Airy"           "Short lows, long highs: the top end falls away for seconds."
uproom "*jamrecord*"           "Tape Jam"       Lo-Fi    "Vintage,Dirty"               "A tape-room colour on F and C#. Short and dusty."
uproom "*orchestratorlife*"    "String Loft"    Room     "Warm,Wide"                   "A warm loft that rings in A and E, longest in the lows."
uproom "*planetary*"           "Planetarium"    Hall     "Ambient,Airy"                "G and C hanging overhead in a domed room."
