#!/usr/bin/env python3
"""Helpers for tools/claude_preview/build.sh.

  make_png.py thumb SPRITE.bin OUT.bin   gallery thumbnail blob of a PMD sprite
                                          (same format as /mons/thumbs.bin entries)
  make_png.py png BUILD_DIR OUT_DIR      every BUILD_DIR/*.rgb565 (466x466) -> PNG
                                          with the round panel mask and a bezel,
                                          plus overview.png with all of them
"""

import struct
import sys
from pathlib import Path

W = H = 466
BEZEL = 14


def cmd_thumb(src, out):
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
    import make_thumbs  # noqa: E402  (tools/make_thumbs.py)

    w, h, pal, data = make_thumbs.read_pmd_idle_frame0(src)
    nw, nh, npal, ndata = make_thumbs.shrink(w, h, pal, data)
    blob = struct.pack("<3B", nw, nh, len(npal)) + struct.pack(f"<{len(npal)}H", *npal) + ndata
    Path(out).write_bytes(blob)


def rgb565_to_image(raw):
    from PIL import Image

    px = struct.unpack(f"<{W * H}H", raw)
    rgb = bytearray()
    for v in px:
        r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
        rgb += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
    return Image.frombytes("RGB", (W, H), bytes(rgb))


def framed(img):
    """The round panel as it looks in the case: circle mask + dark bezel."""
    from PIL import Image, ImageDraw

    size = W + 2 * BEZEL
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(out)
    d.ellipse((0, 0, size - 1, size - 1), fill=(38, 38, 42, 255))
    d.ellipse((4, 4, size - 5, size - 5), fill=(18, 18, 20, 255))
    mask = Image.new("L", (W, H), 0)
    ImageDraw.Draw(mask).ellipse((0, 0, W - 1, H - 1), fill=255)
    out.paste(img, (BEZEL, BEZEL), mask)
    return out


def cmd_png(build, out):
    from PIL import Image

    build, out = Path(build), Path(out)
    out.mkdir(parents=True, exist_ok=True)
    frames = []
    for raw in sorted(build.glob("*.rgb565")):
        img = framed(rgb565_to_image(raw.read_bytes()))
        img.save(out / (raw.stem + ".png"), optimize=True)
        frames.append((raw.stem, img))
        print(f"  {out / (raw.stem + '.png')}")
    # overview: the main screens side by side, half size
    pick = ["context-working", "limits", "session", "context-needs-you"]
    chosen = [img for name, img in frames if name in pick]
    if chosen:
        cell = chosen[0].width // 2
        sheet = Image.new("RGBA", (cell * len(chosen) + 16 * (len(chosen) - 1), cell), (0, 0, 0, 0))
        for i, img in enumerate(chosen):
            sheet.paste(img.resize((cell, cell), Image.LANCZOS), (i * (cell + 16), 0))
        sheet.save(out / "overview.png", optimize=True)
        print(f"  {out / 'overview.png'}")


def main(argv):
    if len(argv) == 4 and argv[1] == "thumb":
        cmd_thumb(argv[2], argv[3])
    elif len(argv) == 4 and argv[1] == "png":
        cmd_png(argv[2], argv[3])
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
