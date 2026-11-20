# Synthetic depth-of-field: blur grows with distance from a focal depth on an approximate depth plane.
import sys
from PIL import Image, ImageFilter
import numpy as np
src, dst, fx, fy = sys.argv[1], sys.argv[2], float(sys.argv[3]), float(sys.argv[4])
kx = float(sys.argv[5]) if len(sys.argv) > 5 else 0.5
maxr = float(sys.argv[6]) if len(sys.argv) > 6 else 22
im = Image.open(src).convert('RGB'); W, H = im.size
yy, xx = np.mgrid[0:H, 0:W]
depth = kx * xx / W + (1 - yy / H)          # farther = up and right in this low view
d0 = kx * fx + (1 - fy)                     # focal depth at the focus point (normalised coords)
r = np.clip(np.abs(depth - d0) * maxr * 4, 0, maxr)
levels = [0, 2, 4, 7, 11, 16, 22]
stack = [np.array(im).astype(float) if l == 0 else np.array(im.filter(ImageFilter.GaussianBlur(l))).astype(float) for l in levels]
out = np.zeros_like(stack[0])
for i in range(len(levels) - 1):
    lo, hi = levels[i], levels[i + 1]
    m = (r >= lo) & (r <= hi)
    t = ((r - lo) / (hi - lo))[..., None]
    out[m] = (stack[i] * (1 - t) + stack[i + 1] * t)[m]
out[r > levels[-1]] = stack[-1][r > levels[-1]]
Image.fromarray(out.clip(0, 255).astype('uint8')).save(dst)
