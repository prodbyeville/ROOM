import sys
from PIL import Image
import numpy as np
base, edit, out = sys.argv[1:4]
a = np.array(Image.open(base).convert('RGBA')).astype(float)
e = np.array(Image.open(edit).convert('RGBA')).astype(float)
H, W = a.shape[:2]
yy, xx = np.mgrid[0:H, 0:W]

def heal(cx, cy, r, iters=400):
    m = (xx - cx) ** 2 + (yy - cy) ** 2 <= r * r
    ys, xs = np.where(m)
    y0, y1, x0, x1 = ys.min() - 2, ys.max() + 3, xs.min() - 2, xs.max() + 3
    sub = a[y0:y1, x0:x1, :3]; mm = m[y0:y1, x0:x1]
    sub[mm] = sub[~mm].mean(0)
    for _ in range(iters):
        avg = (np.roll(sub, 1, 0) + np.roll(sub, -1, 0) + np.roll(sub, 1, 1) + np.roll(sub, -1, 1)) / 4
        sub[mm] = avg[mm]
    a[y0:y1, x0:x1, :3] = sub

def dot(cx, cy, r=2.2, col=(245, 243, 239), ss=8):
    d = np.sqrt((xx[int(cy)-6:int(cy)+7, int(cx)-6:int(cx)+7] - cx) ** 2 + (yy[int(cy)-6:int(cy)+7, int(cx)-6:int(cx)+7] - cy) ** 2)
    k = np.clip(r + 0.5 - d, 0, 1)                 # anti-aliased disc
    g = np.exp(-(d / (r * 1.8)) ** 2) * 0.25       # faint glow, like the originals
    k = np.clip(k + g, 0, 1)[..., None]
    p = a[int(cy)-6:int(cy)+7, int(cx)-6:int(cx)+7, :3]
    a[int(cy)-6:int(cy)+7, int(cx)-6:int(cx)+7, :3] = p * (1 - k) + np.array(col) * k

# SMOOTH: old dot on far left -> move to end of red arc (~11:30)
heal(720, 1107, 4.5)
dot(730.0, 1091.0)
# IMPRINT: remove both dots, place one at end of red arc
heal(927.2, 1089.6, 4.5)
heal(928.1, 1096.0, 4.5)
dot(934.0, 1088.0)

# SYNC: take the clean label from the AI edit, feathered paste
x0, y0, x1, y1 = 1770, 1066, 1880, 1102
mask = np.zeros((H, W))
mask[y0:y1, x0:x1] = 1
for _ in range(4):
    mask = (mask + np.roll(mask, 1, 0) + np.roll(mask, -1, 0) + np.roll(mask, 1, 1) + np.roll(mask, -1, 1)) / 5
mask = mask[..., None]
a[..., :3] = a[..., :3] * (1 - mask) + e[..., :3] * mask
Image.fromarray(a.clip(0, 255).astype('uint8'), 'RGBA').save(out)
