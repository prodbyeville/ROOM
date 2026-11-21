import sys
from PIL import Image, ImageFilter, ImageDraw
base, edit, out, scale, cx, cy, ex, ey = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4]), *map(int, sys.argv[5:9])
b = Image.open(base).convert('RGBA'); e = Image.open(edit).convert('RGBA')
w, h = 130, 120                       # crop around the AI knob (incl. arc + shadow)
patch = e.crop((ex - w // 2, ey - h // 2, ex + w // 2, ey + h // 2))
pw, ph = round(w * scale), round(h * scale)
patch = patch.resize((pw, ph), Image.LANCZOS)
m = Image.new('L', (pw, ph), 0)
ImageDraw.Draw(m).ellipse((6, 6, pw - 6, ph - 6), fill=255)
m = m.filter(ImageFilter.GaussianBlur(4))
b.paste(patch, (cx - pw // 2, cy - ph // 2), m)
b.save(out)
