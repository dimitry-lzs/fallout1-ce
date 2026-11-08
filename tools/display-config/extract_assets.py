#!/usr/bin/env python3
"""Extracts preview images for the display settings page from a Fallout install.

Usage: python3 extract_assets.py /path/to/Fallout

Reads master.dat and the data folder (the Hi-Res Patch art shipped with the
GOG release lives there) and writes PNGs into ./assets next to this script.
The game art stays on your machine; nothing here is redistributed.

Images that cannot be extracted (an in-game map view and a movie frame) are
optional: drop your own `world.png` and `movie.png` into ./assets to replace
the drawn placeholders. Python standard library only.
"""

import os
import struct
import sys
import zlib

ASSETS = {
    # output name: path inside the game data
    "bar": "art/intrface/iface.frm",
    "menu_bg": "art/intrface/mainmenu.frm",
    "menu_button": "art/intrface/menuup.frm",
    "menu_hires_bg": "art/intrface/hr_mainmenu.frm",
    "menu_hires_panel": "art/intrface/hr_menu_bg.frm",
    "side_lft1": "art/intrface/hr_ifacelft1.frm",
    "side_rht1": "art/intrface/hr_ifacerht1.frm",
    "side_lft2": "art/intrface/hr_ifacelft2.frm",
    "side_rht2": "art/intrface/hr_ifacerht2.frm",
}


def find(directory, name):
    """Case-insensitive lookup of `name` (may contain /) under `directory`."""
    path = directory
    for part in name.split("/"):
        if not os.path.isdir(path):
            return None
        match = next((e for e in os.listdir(path) if e.lower() == part.lower()), None)
        if match is None:
            return None
        path = os.path.join(path, match)
    return path


def lzss(src, size):
    out = bytearray()
    pos = 0
    while pos < len(src) and len(out) < size:
        (n,) = struct.unpack(">h", src[pos : pos + 2])
        pos += 2
        if n == 0:
            break
        if n < 0:
            out += src[pos : pos - n]
            pos -= n
            continue
        end = pos + n
        ring = bytearray(b" " * 4096)
        r = 4078
        while pos < end:
            flags = src[pos]
            pos += 1
            for bit in range(8):
                if pos >= end:
                    break
                if flags & (1 << bit):
                    c = src[pos]
                    pos += 1
                    out.append(c)
                    ring[r] = c
                    r = (r + 1) & 0xFFF
                else:
                    lo, hi = src[pos], src[pos + 1]
                    pos += 2
                    offset = lo | ((hi & 0xF0) << 4)
                    for k in range((hi & 0x0F) + 3):
                        c = ring[(offset + k) & 0xFFF]
                        out.append(c)
                        ring[r] = c
                        r = (r + 1) & 0xFFF
    return bytes(out[:size])


class Dat:
    """Fallout 1 DAT archive (big-endian, LZSS compressed entries)."""

    def __init__(self, path):
        self.file = open(path, "rb")
        self.entries = {}
        f = self.file
        (count,) = struct.unpack(">I", f.read(4))
        f.read(12)
        dirs = [f.read(f.read(1)[0]).decode("ascii") for _ in range(count)]
        for d in dirs:
            (files,) = struct.unpack(">I", f.read(4))
            f.read(12)
            for _ in range(files):
                name = f.read(f.read(1)[0]).decode("ascii")
                key = (name if d == "." else d + "\\" + name).lower().replace("\\", "/")
                self.entries[key] = struct.unpack(">IIII", f.read(16))

    def read(self, name):
        entry = self.entries.get(name.lower())
        if entry is None:
            return None
        attr, offset, size, packed = entry
        self.file.seek(offset)
        if attr == 0x40:
            return lzss(self.file.read(packed), size)
        return self.file.read(size)


def png(path, width, height, rgba):
    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    rows = b"".join(b"\0" + rgba[y * width * 4 : (y + 1) * width * 4] for y in range(height))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(rows, 9)))
        f.write(chunk(b"IEND", b""))


def frm_to_rgba(data, palette):
    """First frame of an FRM; palette index 0 becomes transparent."""
    width, height = struct.unpack(">HH", data[0x3E:0x42])
    pixels = data[0x4A : 0x4A + width * height]
    out = bytearray(width * height * 4)
    for i, index in enumerate(pixels):
        if index:
            out[i * 4 : i * 4 + 4] = palette[index] + b"\xff"
    return width, height, bytes(out)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    game = sys.argv[1]
    master = find(game, "master.dat")
    if master is None:
        sys.exit(f"master.dat not found in {game}")
    data_dir = find(game, "data")
    dat = Dat(master)

    def load(name):
        local = find(data_dir, name) if data_dir else None
        if local:
            with open(local, "rb") as f:
                return f.read()
        return dat.read(name)

    raw = load("color.pal")
    palette = [bytes(min(255, c * 4) for c in raw[i * 3 : i * 3 + 3]) for i in range(256)]

    out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "assets")
    os.makedirs(out_dir, exist_ok=True)
    for out, name in ASSETS.items():
        data = load(name)
        if data is None:
            print(f"skipped {out}: {name} not found (Hi-Res Patch art is optional)")
            continue
        width, height, rgba = frm_to_rgba(data, palette)
        png(os.path.join(out_dir, out + ".png"), width, height, rgba)
        print(f"wrote {out}.png ({width}x{height})")


if __name__ == "__main__":
    main()
