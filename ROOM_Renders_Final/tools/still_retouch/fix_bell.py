import sys
from PIL import Image, ImageDraw, ImageFont
import numpy as np
src, dst = sys.argv[1], sys.argv[2]
FONT = '/home/user/ROOM/plugin/Resources/Ui/Satoshi-Bold.otf'
a = np.array(Image.open(src).convert('RGBA')).astype(float)
# glyph colour from the next letter (E)
ink = a[631:643, 1067:1076, :3].reshape(-1, 3); col = ink[ink.sum(1).argmax()]
x0, y0, x1, y1 = 1051, 628, 1065, 646
T = a[y0 - 1, x0:x1 + 1, :3].copy(); B = a[y1 + 1, x0:x1 + 1, :3].copy(); n = y1 - y0 + 1
for i in range(n):
    t = (i + 1) / (n + 1); a[y0 + i, x0:x1 + 1, :3] = T * (1 - t) + B * t
f = ImageFont.truetype(FONT, 400); l, t, r, b = f.getbbox('B')
m = Image.new('L', (r - l + 40, b - t + 40), 0); ImageDraw.Draw(m).text((20 - l, 20 - t), 'B', font=f, fill=255); m = m.crop(m.getbbox())
h = 12; gw = 9
m = m.resize((gw * 8, h * 8), Image.LANCZOS).resize((gw, h), Image.LANCZOS)
al = np.array(m).astype(float)[..., None] / 255.0
reg = a[631:631 + h, 1054:1054 + gw, :3]
a[631:631 + h, 1054:1054 + gw, :3] = reg * (1 - al) + col * al
Image.fromarray(a.clip(0, 255).astype('uint8'), 'RGBA').save(dst)
