"""Draws ROOM's installer art (red, from the faceplate's wordmark):
   assets/wizard.png + @2x   the tall panel on the Windows Welcome / Finish pages
   assets/small.png + @2x    the mark top right on the other Windows pages
   assets/room.ico           the setup / uninstall icon
   mac-resources/background.png   bottom left of the macOS installer
Run from anywhere: python plugin/Installer/make-assets.py (needs Pillow)."""
from pathlib import Path
from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
LOGO = HERE.parent / "Resources" / "Ui" / "logo.png"
RED, DEEP, BLACK, WHITE = (186, 39, 39), (120, 18, 18), (10, 10, 10), (245, 243, 239)


def wordmark(width, colour):
    """The faceplate's 'room' wordmark, recoloured, `width` pixels wide."""
    logo = Image.open(LOGO).convert("RGBA")
    # The letters are the alpha channel; the 3 px frame around the file isn't part of them.
    alpha = logo.getchannel("A").crop((3, 3, logo.width - 3, logo.height - 3))
    alpha = alpha.crop(alpha.point(lambda v: 255 if v > 40 else 0).getbbox())
    mark = Image.new("RGBA", alpha.size, colour + (255,))
    mark.putalpha(alpha)
    h = round(mark.height * width / mark.width)
    return mark.resize((width, h), Image.LANCZOS)


def room_box(draw, cx, cy, size, colour, width):
    """A room seen from the doorway: the back wall and the four edges to the corners."""
    front, back = size, size * 0.42
    f = [cx - front, cy - front * 0.78, cx + front, cy + front * 0.78]
    b = [cx - back, cy - back * 0.78, cx + back, cy + back * 0.78]
    draw.rectangle(f, outline=colour, width=width)
    draw.rectangle(b, outline=colour, width=width)
    for (x0, y0), (x1, y1) in [((f[0], f[1]), (b[0], b[1])), ((f[2], f[1]), (b[2], b[1])),
                               ((f[0], f[3]), (b[0], b[3])), ((f[2], f[3]), (b[2], b[3]))]:
        draw.line([x0, y0, x1, y1], fill=colour, width=width)


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
    room_box(d, w / 2, h * 0.40, w * 0.36, (232, 120, 112), max(1, round(1.5 * scale)))
    mark = wordmark(round(w * 0.62), WHITE)
    img.alpha_composite(mark, (round((w - mark.width) / 2), round(h * 0.78)))
    return img


def small(scale):
    s = 55 * scale
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, s - 1, s - 1], radius=round(10 * scale), fill=RED + (255,))
    room_box(d, s / 2, s / 2, s * 0.32, WHITE, max(1, round(1.6 * scale)))
    return img


def icon():
    s = 256
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([8, 8, s - 9, s - 9], radius=48, fill=RED + (255,))
    room_box(d, s / 2, s / 2, s * 0.30, WHITE, 9)
    return img


def mac_background():
    w, h = 620, 418
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    room_box(d, 92, h - 118, 52, RED, 3)
    mark = wordmark(96, RED)
    img.alpha_composite(mark, (44, h - 26 - mark.height))
    return img


if __name__ == "__main__":
    out = HERE / "assets"
    out.mkdir(exist_ok=True)
    wizard(1).save(out / "wizard.png"); wizard(2).save(out / "wizard@2x.png")
    small(1).save(out / "small.png"); small(2).save(out / "small@2x.png")
    icon().save(out / "room.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    (HERE / "mac-resources").mkdir(exist_ok=True)
    mac_background().save(HERE / "mac-resources" / "background.png")
    print("ok")
