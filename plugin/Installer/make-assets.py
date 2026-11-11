"""Draws ROOM's installer art (red, from the faceplate's wordmark):
   assets/wizard.png + @2x   the tall panel on the Windows Welcome / Finish pages
   assets/small.png + @2x    the mark top right on the other Windows pages
   assets/room.ico           the setup / uninstall icon
   mac-resources/background.png   bottom left of the macOS installer
Run from anywhere: python plugin/Installer/make-assets.py (needs Pillow)."""
from pathlib import Path
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
LOGO = HERE / "logo_from_svg.png"   # Resources/Ui/room_logo.svg rendered (letters in the alpha)
RED, DEEP, BLACK, WHITE = (186, 39, 39), (120, 18, 18), (10, 10, 10), (245, 243, 239)


def wordmark(width, colour):
    """The faceplate's 'room' wordmark, recoloured, `width` pixels wide."""
    logo = Image.open(LOGO).convert("RGBA")
    # The letters are the alpha channel.
    alpha = logo.getchannel("A")
    alpha = alpha.crop(alpha.point(lambda v: 255 if v > 40 else 0).getbbox())
    mark = Image.new("RGBA", alpha.size, colour + (255,))
    mark.putalpha(alpha)
    h = round(mark.height * width / mark.width)
    return mark.resize((width, h), Image.LANCZOS)


def room_mark(img, cx, cy, size, colour, width, fill=None, inner=None):
    """The ROOM mark (Design/logos/final/room-mark.svg): a rounded square, the
    back wall and the four corner lines. `size` is the outer square's side.
    With `fill`, the square is filled and the inner lines drawn in `inner`.
    Drawn at 4x and scaled down so the thin lines stay smooth."""
    ss = 4
    w, h = img.size
    layer = Image.new("RGBA", (w * ss, h * ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    k = size / 384.0 * ss
    P = lambda x, y: ((cx * ss) + (x - 256) * k, (cy * ss) + (y - 256) * k)
    lw = max(1, round(width * ss))
    frame = [P(64, 64), P(448, 448)]
    if fill is not None:
        d.rounded_rectangle(frame, radius=50 * k, fill=fill)
    else:
        d.rounded_rectangle(frame, radius=50 * k, outline=colour, width=lw)
    line = inner if inner is not None else colour
    d.rectangle([P(174, 164), P(338, 308)], outline=line, width=lw)
    for a_, b_ in [((78.6, 78.6), (174, 164)), ((433.4, 78.6), (338, 164)), ((78.6, 433.4), (174, 308)), ((433.4, 433.4), (338, 308))]:
        d.line([P(*a_), P(*b_)], fill=line, width=lw)
    img.alpha_composite(layer.resize((w, h), Image.LANCZOS))


def vertical_gradient(size, top, bottom):
    w, h = size
    img = Image.new("RGBA", size)
    d = ImageDraw.Draw(img)
    for y in range(h):
        t = y / max(1, h - 1)
        d.line([0, y, w, y], fill=tuple(round(top[i] + (bottom[i] - top[i]) * t) for i in range(3)) + (255,))
    return img


def wizard(scale):
    w, h = 164 * scale, 314 * scale
    img = vertical_gradient((w, h), RED, DEEP)
    d = ImageDraw.Draw(img)
    room_mark(img, w / 2, h * 0.40, w * 0.62, WHITE, 1.4 * scale)
    mark = wordmark(round(w * 0.62), WHITE)
    img.alpha_composite(mark, (round((w - mark.width) / 2), round(h * 0.78)))
    return img


def small(scale):
    # The mark filled red with off-white inner lines (reads at 55 px).
    s = 55 * scale
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    room_mark(img, s / 2, s / 2, s * 0.92, RED, 1.5 * scale, fill=RED + (255,), inner=WHITE)
    return img


def icon(s=256):
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    room_mark(img, s / 2, s / 2, s * 0.94, RED, max(1.0, s / 64), fill=RED + (255,), inner=WHITE)
    return img


def mac_background():
    w, h = 620, 418
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    room_mark(img, 92, h - 118, 100, RED, 2.0)
    mark = wordmark(96, RED)
    img.alpha_composite(mark, (44, h - 26 - mark.height))
    return img


if __name__ == "__main__":
    out = HERE / "assets"
    out.mkdir(exist_ok=True)
    wizard(1).save(out / "wizard.png"); wizard(2).save(out / "wizard@2x.png")
    small(1).save(out / "small.png"); small(2).save(out / "small@2x.png")
    icon().save(out / "room.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    icon(1024).save(out / "icon_1024.png")
    (HERE / "mac-resources").mkdir(exist_ok=True)
    mac_background().save(HERE / "mac-resources" / "background.png")
    print("ok")
