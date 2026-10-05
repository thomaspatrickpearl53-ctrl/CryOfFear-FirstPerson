"""GoldSrc textures: RGBA -> 8-bit miptex (4 mip levels, 256-colour palette)
and WAD3 files."""
import struct
import numpy as np
from PIL import Image


def fit_size(w, h, max_size):
    """GoldSrc sizes: multiples of 16, at most max_size, keeping the aspect ratio."""
    s = min(1.0, max_size / max(w, h))
    nw = max(16, int(round(w * s / 16.0)) * 16)
    nh = max(16, int(round(h * s / 16.0)) * 16)
    return min(nw, max_size), min(nh, max_size)


def miptex(name, rgba, size, masked=False, solid_color=None):
    """Returns the miptex bytes (as stored in a WAD or BSP)."""
    w, h = size
    if solid_color is not None:
        img = Image.new("RGB", (w, h), tuple(int(c) for c in solid_color))
        alpha = None
    else:
        im = Image.fromarray(rgba, "RGBA").resize((w, h), Image.LANCZOS)
        arr = np.asarray(im)
        alpha = arr[:, :, 3] < 128 if masked else None
        img = Image.fromarray(arr[:, :, :3].copy(), "RGB")
    colors = 255 if masked else 256
    q = img.quantize(colors=colors, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = q.getpalette()[:colors * 3]
    pal += [0] * (768 - len(pal))
    if masked:
        pal[255 * 3:256 * 3] = [0, 0, 255]                      # index 255 = see-through
    levels = []
    for m in range(4):
        mw, mh = max(1, w >> m), max(1, h >> m)
        if m == 0:
            idx = np.asarray(q, np.uint8).copy()
        else:
            small = img.resize((mw, mh), Image.LANCZOS)
            pimg = Image.new("P", (1, 1))
            pimg.putpalette(pal[:colors * 3] + [0] * (768 - colors * 3))
            idx = np.asarray(small.quantize(palette=pimg, dither=Image.Dither.NONE), np.uint8).copy()
            if masked:
                idx[idx == 255] = 0
        if masked and alpha is not None:
            a = alpha if m == 0 else np.asarray(Image.fromarray(alpha.astype(np.uint8) * 255).resize((mw, mh), Image.NEAREST)) > 127
            idx[a] = 255
        levels.append(idx.tobytes())
    head = struct.pack("<16sII", name.upper().encode("ascii")[:15].ljust(16, b"\0"), w, h)
    offs, pos = [], 40
    for lv in levels:
        offs.append(pos)
        pos += len(lv)
    out = head + struct.pack("<4I", *offs) + b"".join(levels)
    out += struct.pack("<H", 256) + bytes(pal[:768]) + b"\0\0"
    return out


def stub(name, mt):
    """16x16 stand-in for a miptex in its average colour (for the compiler's lighting bounce)."""
    w, h = struct.unpack_from("<II", mt, 16)
    o0, o3 = struct.unpack_from("<I", mt, 24)[0], struct.unpack_from("<I", mt, 36)[0]
    pix = np.frombuffer(mt, np.uint8, w * h, o0)
    pal = np.frombuffer(mt, np.uint8, 768, o3 + (w // 8) * (h // 8) + 2).reshape(256, 3)
    used = pix[pix != 255] if name.startswith("{") else pix
    color = pal[used].mean(0) if len(used) else (128, 128, 128)
    return miptex(name, None, (16, 16), solid_color=color)


def texture_lump(entries):
    """BSP texture lump from miptex bytes (full textures, or 40-byte headers that
    tell the engine to load the pixels from a WAD)."""
    n = len(entries)
    out = bytearray(struct.pack("<i", n) + b"\0" * 4 * n)
    for i, mt in enumerate(entries):
        while len(out) % 4:
            out.append(0)
        struct.pack_into("<i", out, 4 + i * 4, len(out))
        out += mt
    return bytes(out)


def write_wad(path, textures):
    """textures: list of (name, miptex bytes)."""
    data = bytearray(b"WAD3" + struct.pack("<II", len(textures), 0))
    entries = []
    for name, mt in textures:
        while len(data) % 4:
            data.append(0)
        entries.append((len(data), len(mt), name))
        data += mt
    while len(data) % 4:
        data.append(0)
    dir_ofs = len(data)
    for ofs, size, name in entries:
        data += struct.pack("<iiiBBH16s", ofs, size, size, 0x43, 0, 0, name.upper().encode("ascii")[:15].ljust(16, b"\0"))
    struct.pack_into("<I", data, 8, dir_ofs)
    with open(path, "wb") as f:
        f.write(data)
