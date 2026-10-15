#!/usr/bin/env bash
# Makes the factory ROOMS that come from Eville's own recordings (Room Factory
# Pack 2) in plugin/Presets/Factory/. A .room keeps only the analysis, so the
# audio itself is not in this repo: pass the folder it lives in.
#
#   bash plugin/Tools/make_pack_rooms.sh path/to/room_render "path/to/Room Factory Pack"
#
# make_factory_presets.sh leaves these .room files alone.
set -euo pipefail
RENDER="$(realpath "${1:?usage: make_pack_rooms.sh path/to/room_render path/to/pack}")"
PACK="$(realpath "${2:?usage: make_pack_rooms.sh path/to/room_render path/to/pack}")"
cd "$(dirname "$0")/../.."
OUT=plugin/Presets/Factory
TMP=$(mktemp -d)

packroom () {   # file-pattern  room-name  type  character  notes
    local src
    src=$(find "$PACK" -type f -iname "$1" | head -n 1)
    [ -n "$src" ] || { echo "  missing $1" >&2; exit 1; }
    "$RENDER" "$src" examples/audio/melody.wav "$TMP/out.wav" \
        --room-name "$2" --save-room "$OUT/$2.room" --room-type "$3" --room-styles "$4" --room-notes "$5" > /dev/null
    echo "  ROOM    $2"
}

packroom "Key.wav"              "Dim Chapel"      Hall          "Dark,Warm,Ambient"        "Long and low. The top rolls off early."
packroom "*doubleshot*"         "Wood Hall"       Hall          "Warm"                     "A wooden hall: full low mids, a soft top."
packroom "*subthirty*"          "Sub Cellar"      Chamber       "Dark,Experimental"        "Hangs on low for two seconds, nothing up top."
packroom "*tomfill*"            "Wide Loft"       Room          "Wide,Warm"                "An open loft: very wide, about a second long."
packroom "*snare*lively*"       "Brick Room"      Room          "Warm,Dirty"               "Brick and a low ceiling: mids forward, top tucked in."
packroom "*tascam noise*tascam (*" "Cassette Hall" Lo-Fi        "Vintage,Dirty,Airy"       "Tape hiss as a room. The top end hangs on for seconds."
packroom "*caffeine4*"          "Glass Sheet"     Object        "Bright,Airy"              "Bright and thin. A few high notes ring on."
packroom "*crackle*"            "Dust Room"       Texture       "Dirty,Vintage,Wide"       "Crackle that dies away like a room does."
packroom "*chambers*"           "Back Chamber"    Chamber       "Bright,Dirty,Experimental" "A hard side chamber, hollow in the middle."
packroom "*depthpulse*"         "Basement Show"   Room          "Dirty,Warm,Wide"          "A packed basement: low, loud and wide."
packroom "*toxicstatic*"        "Back Alley"      Outdoor       "Bright,Dirty"             "Brick walls, open sky, a quick slap."
packroom "*ecofriendly*"        "Tin Shed"        "Small Space" "Bright,Metallic"          "Thin metal walls: bright and quick."
packroom "*como2*"              "Felt Room"       Room          "Warm,Clean"               "Soft walls, warm, close to a second long."
packroom "*ghosty*"             "Hallway"         "Small Space" "Warm,Tight"               "A narrow hallway: short and a little boomy."
packroom "*groundpulse*"        "Plaster Room"    Room          "Warm,Clean"               "Old plaster: warm, short, soft on top."
packroom "*disruptive*"         "Wide Shell"      "Small Space" "Wide,Experimental"        "Short and very wide, hollow in the low mids."
packroom "*coalvein*"           "Coal Room"       "Small Space" "Dark,Warm"                "Black walls, low ceiling. Dark and close."
packroom "*bloopersonics*"      "Deep Tank"       Texture       "Dark,Wide,Experimental"   "Something big and underwater, slowly moving."
packroom "*atmospheres*ac*b9*"  "After Hours"     Texture       "Warm,Ambient"             "An office at night with the air still running."
packroom "*atmospheres*ac*b2*"  "Hotel Room"      Texture       "Ambient,Dirty"            "A closed room and the hum of its air unit."
packroom "*atmospheres*room (*" "Empty Apartment" Texture       "Bright,Ambient"           "Bare walls that ring at a few low notes."
packroom "*greyshift*"          "Grey Room"       "Small Space" "Bright,Wide"              "Small and bright, scooped in the middle."
packroom "*bloom*"              "Moss Room"       "Small Space" "Dark,Warm"                "Soft and dark, a short bloom of low end."
packroom "GTR PHRASE 2.wav"     "Guitar Body"     Object        "Warm"                     "A hollow body that rings at the guitar's notes."
packroom "*bongo3*"             "Drum Shell"      Object        "Warm,Tight"               "Inside a hand drum: a round, short boom."

rm -rf "$TMP"
