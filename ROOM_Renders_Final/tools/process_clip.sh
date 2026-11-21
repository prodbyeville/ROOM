#!/usr/bin/env bash
# Usage: process_clip.sh <name> <result_url>
# Downloads a Kling clip, conforms to 1920x1080 24fps (no audio), applies the
# per-clip focus pass if a config exists, and writes a 3-frame check strip.
set -euo pipefail
NAME="$1"; URL="$2"
V=/home/user/ROOM/ROOM_Renders_Final/04_Video
T=/home/user/ROOM/ROOM_Renders_Final/tools
S=/tmp/claude-0/-home-user-ROOM/862feb5a-312a-5fcb-b6f4-d0c74ca69c6a/scratchpad
curl -sS -o "$V/raw/${NAME}_kling_raw.mp4" "$URL"
CONF="$S/${NAME}_conform.mp4"
ffmpeg -v error -y -i "$V/raw/${NAME}_kling_raw.mp4" -vf "scale=1936:1080:flags=lanczos,crop=1920:1080,fps=24" \
  -frames:v 120 -an -c:v libx264 -preset slow -crf 14 -pix_fmt yuv420p -movflags +faststart "$CONF"
if [ -f "$T/configs/${NAME}_dofpass.json" ]; then
  python3 -I "$T/dof_pass.py" "$T/configs/${NAME}_dofpass.json" >/dev/null
else
  cp "$CONF" "$V/${NAME}_final.mp4"
fi
for t in 0 2.5 4.95; do ffmpeg -v error -y -ss $t -i "$V/${NAME}_final.mp4" -frames:v 1 "$S/strip_${NAME}_$t.png"; done
python3 -c "
from PIL import Image
S='$S'; n='$NAME'
ims=[Image.open(f'{S}/strip_{n}_{t}.png').resize((640,360)) for t in ['0','2.5','4.95']]
c=Image.new('RGB',(1920,360)); [c.paste(im,(i*640,0)) for i,im in enumerate(ims)]; c.save(f'{S}/strip_{n}.png')"
echo "$NAME ok: $(ffprobe -v error -show_entries stream=codec_type,width,height,nb_frames -of csv=p=0 "$V/${NAME}_final.mp4" | tr '\n' ' ')"
