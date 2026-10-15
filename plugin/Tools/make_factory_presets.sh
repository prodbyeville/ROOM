#!/usr/bin/env bash
# Regenerates the built-in "Factory" ROOMS (.room) and STUDIOS (.studio) in
# plugin/Presets/Factory/.
# Every source here is synthesized by examples/make_test_audio.py. The ROOMS
# made from Eville's own recordings come from make_pack_rooms.sh and are kept.
#
#   bash plugin/Tools/make_factory_presets.sh path/to/room_render
set -euo pipefail
RENDER="${1:?usage: make_factory_presets.sh path/to/room_render}"
cd "$(dirname "$0")/../.."
python3 examples/make_test_audio.py > /dev/null
OUT=plugin/Presets/Factory
mkdir -p "$OUT"
PACK_ROOMS=$(sed -nE 's/^packroom +"[^"]*" +"([^"]+)".*/\1/p' plugin/Tools/make_pack_rooms.sh)
for f in "$OUT"/*.room "$OUT"/*.studio; do
    [ -e "$f" ] || continue
    [[ "$f" == *.room ]] && grep -qxF "$(basename "$f" .room)" <<< "$PACK_ROOMS" && continue
    rm -f "$f"
done
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
room stairwell        "Stairwell"       Chamber       "Bright,Wide"              "A clap in a concrete stairwell: tall, hard, a flutter between the walls."
room tiled_bathroom   "Tiled Bathroom"  "Small Space" "Bright,Tight"             "A small room of tiles: hard and bright."
room concrete_garage  "Concrete Garage" Room          "Wide,Dirty"               "A low, wide parking garage."
room cathedral        "Cathedral"       Hall          "Ambient,Dark,Wide"        "A stone nave: huge, slow, dark at the top."
room wooden_closet    "Wooden Closet"   "Small Space" "Warm,Tight"               "A cupboard of wood and coats."
room car_interior     "Car Interior"    "Small Space" "Tight,Dirty"              "Inside a parked car: tiny and boxy."
room tunnel           "Tunnel"          Outdoor       "Wide,Experimental"        "An underpass: slap echoes running away from you."
room empty_gym        "Empty Gym"       Hall          "Bright,Airy"              "A school gym after hours."
room live_room        "Live Room"       Room          "Clean,Warm"               "A good studio live room: even and open."
room vocal_booth      "Vocal Booth"     "Small Space" "Clean,Tight"              "A treated booth: almost nothing, a little wood."
room init_room        "Init Room"       Room          "Clean"                    "A plain, short room. A good place to start."
room singing_bowl     "Singing Bowl"    Object        "Metallic,Ambient"         "A struck singing bowl: slow beating, long ring."
room cardboard_box    "Cardboard Box"   Object        "Warm,Dirty"               "A big box, knocked: papery and quick."
room piano_body       "Piano Body"      Object        "Warm,Ambient"             "A thump on a piano with the pedal down."
room spring_tank      "Spring Tank"     FX            "Vintage,Metallic"         "A spring reverb tank, kicked."
room wine_glass       "Wine Glass"      Object        "Bright,Metallic"          "A flicked wine glass: clean, high, long."
room vinyl_room       "Vinyl Room"      Texture       "Vintage,Dirty"            "Crackle and hiss from a record's run-out groove."
room rain_window      "Rain Window"     Texture       "Ambient,Warm"             "Rain on a window, heard from inside."


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

studio init_room        "Init Room"       "Init Room"        Room          "Clean"                      "Vocals,Keys,Guitar" "A plain, short room. Start here."        --mix 0.3
studio stairwell        "Stairwell"       "Stairwell"        Chamber       "Bright,Wide"                "Drums,Vocals"       "Hard concrete, a flutter on every hit."   --mix 0.35
studio stairwell        "Stairwell"       "Fire Escape"      Chamber       "Dirty,Wide"                 "Drums,Guitar"       "The stairwell, crushed onto tape."        --mix 0.4 --tape 0.6 --param duck=0.35
studio tiled_bathroom   "Tiled Bathroom"  "Bathroom Vocal"   "Small Space" "Bright,Tight"               "Vocals"             "Singing in the shower, close."            --mix 0.3 --param preDelay=12
studio tiled_bathroom   "Tiled Bathroom"  "Tile Snare"       "Small Space" "Bright,Tight"               "Drums"              "Short and splashy for snares."            --mix 0.35 --tail 0.8
studio concrete_garage  "Concrete Garage" "Garage"           Room          "Wide,Dirty"                 "Drums,Guitar"       "Wide and rough."                          --mix 0.35
studio concrete_garage  "Concrete Garage" "Garage Band"      Room          "Dirty,Vintage"              "Guitar,Bass"        "The garage, after a few too many takes."  --mix 0.35 --tape 0.5 --param damping=0.3
studio cathedral        "Cathedral"       "Cathedral"        Hall          "Ambient,Dark,Wide"          "Vocals,Keys,Synth"  "Huge and slow."                           --mix 0.4 --param preDelay=40
studio cathedral        "Cathedral"       "Choir Loft"       Hall          "Ambient,Airy"               "Vocals"             "The nave, brighter and ducked."           --mix 0.4 --param preDelay=60,duck=0.4,damping=-0.3
studio cathedral        "Cathedral"       "Stone Pad"        FX            "Ambient,Experimental"       "Synth,FX"           "Everything turned into a pad."            --mix 0.45 --intensity 1.3 --space 1.3 --param size=1.6
studio wooden_closet    "Wooden Closet"   "Closet"           "Small Space" "Warm,Tight"                 "Vocals,Bass"        "Close, warm, a little boxy."              --mix 0.35
studio wooden_closet    "Wooden Closet"   "Acoustic Box"     "Small Space" "Warm,Clean"                 "Guitar,Keys"        "Body for dry acoustic parts."             --mix 0.3 --eq 1:180:2:0.71
studio car_interior     "Car Interior"    "Parked Car"       "Small Space" "Tight,Dirty"                "Drums,Bass"         "Boxy and close."                          --mix 0.35
studio car_interior     "Car Interior"    "Car Speakers"     Lo-Fi         "Dirty,Vintage"              "Bus/Master,Vocals"  "Played back through a car."               --mix 0.55 --tape 0.6 --eq 1:90:3:0.71,5:7000:-6:0.71
studio tunnel           "Tunnel"          "Underpass"        Outdoor       "Wide,Experimental"          "Drums,FX"           "Slaps that run away from you."            --mix 0.4
studio tunnel           "Tunnel"          "Subway"           Outdoor       "Dark,Wide"                  "Synth,FX"           "The tunnel, darker and further off."      --mix 0.45 --param preDelay=80,damping=0.5
studio empty_gym        "Empty Gym"       "Empty Gym"        Hall          "Bright,Airy"                "Drums,Vocals"       "Big, bright, ringing."                    --mix 0.35
studio empty_gym        "Empty Gym"       "Gym Claps"        Hall          "Bright,Wide"                "Drums"              "For claps and snaps."                     --mix 0.4 --space 1.5
studio live_room        "Live Room"       "Live Room"        Room          "Clean,Warm"                 "Drums,Guitar,Keys"  "Even and open."                          --mix 0.3
studio live_room        "Live Room"       "Drum Room"        Room          "Warm,Tight"                 "Drums"              "Room mics, pushed."                       --mix 0.45 --tape 0.3 --param duck=0.25
studio live_room        "Live Room"       "Piano Room"       Room          "Clean,Airy"                 "Keys"               "Air around a piano."                      --mix 0.3 --eq 5:9000:2:0.71
studio vocal_booth      "Vocal Booth"     "Vocal Booth"      "Small Space" "Clean,Tight"                "Vocals"             "Barely there. Just enough."               --mix 0.25
studio vocal_booth      "Vocal Booth"     "Rap Booth"        "Small Space" "Clean,Tight"                "Vocals"             "Tight, ducked, out of the way."           --mix 0.25 --param duck=0.5,duckRelease=150
studio singing_bowl     "Singing Bowl"    "Singing Bowl"     Object        "Metallic,Ambient"           "Keys,Synth,FX"      "Rings at the bowl's notes."               --mix 0.4
studio singing_bowl     "Singing Bowl"    "Bowl Drone"       FX            "Ambient,Experimental"       "FX,Synth"           "Held, slow beating."                      --mix 0.6 --intensity 1.4 --tail 6
studio cardboard_box    "Cardboard Box"   "Cardboard Box"    Object        "Warm,Dirty"                 "Drums,Bass"         "Papery thud around every hit."            --mix 0.4
studio piano_body       "Piano Body"      "Piano Body"       Object        "Warm,Ambient"               "Keys,Vocals"        "The strings answer back."                 --mix 0.35
studio piano_body       "Piano Body"      "Sympathy"         Object        "Ambient,Airy"               "Vocals,Guitar"      "Strings ringing under a voice."           --mix 0.4 --balance 0.2
studio spring_tank      "Spring Tank"     "Spring Tank"      FX            "Vintage,Metallic"           "Guitar,Drums"       "Boing, in the best way."                  --mix 0.35
studio spring_tank      "Spring Tank"     "Dub Spring"       FX            "Vintage,Dirty"              "Drums,FX"           "Spring and tape, sent hard."              --mix 0.5 --tape 0.6 --param preDelay=30
studio wine_glass       "Wine Glass"      "Wine Glass"       Object        "Bright,Metallic"            "Keys,Synth"         "Clean, high, long."                       --mix 0.35
studio vinyl_room       "Vinyl Room"      "Vinyl Room"       Texture       "Vintage,Dirty"              "Bus/Master,Keys"    "Crackle and hiss."                        --mix 0.35 --tape 0.5
studio rain_window      "Rain Window"     "Rain Window"      Texture       "Ambient,Warm"               "Keys,Vocals"        "A wet afternoon around your track."       --mix 0.35
studio bottle_tap       "Glass Bottle"    "Bottle Bass"      Object        "Warm,Metallic"              "Bass"               "The bottle's low ring under a bass."      --mix 0.3 --eq 5:6000:-6:0.71

rm -rf "$TMP"
