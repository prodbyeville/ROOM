import sys
from PIL import Image, ImageDraw, ImageFont
import numpy as np
src, dst = sys.argv[1], sys.argv[2]
FONT = '/home/user/ROOM/plugin/Resources/Ui/Satoshi-Medium.otf'
a = np.array(Image.open(src).convert('RGBA')).astype(float)

def erase_h(x0, y0, x1, y1):
    n = x1 - x0 + 1
    for y in range(y0, y1 + 1):
        L = a[y, x0 - 1, :3].copy(); R = a[y, x1 + 1, :3].copy()
        for i in range(n):
            t = (i + 1) / (n + 1); a[y, x0 + i, :3] = L * (1 - t) + R * t

def erase_v(x0, y0, x1, y1):
    T = a[y0 - 1, x0:x1 + 1, :3].copy(); B = a[y1 + 1, x0:x1 + 1, :3].copy(); n = y1 - y0 + 1
    for i in range(n):
        t = (i + 1) / (n + 1); a[y0 + i, x0:x1 + 1, :3] = T * (1 - t) + B * t

def glyph(ch, bx0, by0, bx1, by1, color, ss=8, font=FONT):
    w, h = bx1 - bx0 + 1, by1 - by0 + 1
    f = ImageFont.truetype(font, 400); l, t, r, b = f.getbbox(ch)
    m = Image.new('L', (r - l + 40, b - t + 40), 0)
    ImageDraw.Draw(m).text((20 - l, 20 - t), ch, font=f, fill=255); m = m.crop(m.getbbox())
    gw = max(1, round(m.width * h / m.height))
    m = m.resize((gw * ss, h * ss), Image.LANCZOS).resize((gw, h), Image.LANCZOS)
    al = np.array(m).astype(float) / 255.0; ox = bx0 + (w - gw) // 2
    reg = a[by0:by0 + h, ox:ox + gw, :3]
    a[by0:by0 + h, ox:ox + gw, :3] = reg * (1 - al[..., None]) + np.array(color) * al[..., None]

# TAIL "2.5 5" -> "2.5 S"
ink = a[823:840, 1784:1797, :3].reshape(-1, 3); col = ink[ink.sum(1).argmin()]
erase_h(1804, 819, 1824, 839)
glyph('S', 1808, 823, 1821, 839, col)
# GLITCH "SYNO" -> "SYNC"
ink = a[1074:1086, 1868:1877, :3].reshape(-1, 3); col = ink[ink.sum(1).argmax()]
erase_v(1904, 1070, 1918, 1090)
glyph('C', 1906, 1074, 1916, 1086, col)
Image.fromarray(a.clip(0, 255).astype('uint8'), 'RGBA').save(dst)
