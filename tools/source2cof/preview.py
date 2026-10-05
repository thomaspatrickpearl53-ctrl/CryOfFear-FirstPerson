"""Quick look at a compiled GoldSrc map (BSP v30) without the game: draws the
faces with each texture's average colour times its baked lighting, from the
player spawn and from above.

    py -m source2cof.preview <map.bsp> <out.png>   (run from tools/)
"""
import math, re, struct, sys
import numpy as np
from PIL import Image, ImageDraw


def wad_textures(path):
    """name -> miptex bytes from a WAD3 file (if there is one)."""
    try:
        w = open(path, "rb").read()
    except OSError:
        return {}
    n, o = struct.unpack_from("<ii", w, 4)
    out = {}
    for i in range(n):
        ofs, size, _, typ, _, _, name = struct.unpack_from("<iiiBBH16s", w, o + i * 32)
        out[name.split(b"\0")[0].decode("latin-1").lower()] = w[ofs:ofs + size]
    return out


def load(path):
    d = open(path, "rb").read()
    wadtex = wad_textures(path[:-4] + ".wad")
    if struct.unpack_from("<i", d, 0)[0] != 30:
        raise SystemExit("not a GoldSrc (v30) map")
    L = [struct.unpack_from("<ii", d, 4 + i * 8) for i in range(15)]
    lump = lambda i: d[L[i][0]:L[i][0] + L[i][1]]
    verts = np.frombuffer(lump(3), "<f4").reshape(-1, 3)
    edges = np.frombuffer(lump(12), "<u2").reshape(-1, 2)
    surf = np.frombuffer(lump(13), "<i4")
    tex = lump(2)
    ntex = struct.unpack_from("<i", tex, 0)[0]
    colors = []
    for i in range(ntex):
        o = struct.unpack_from("<i", tex, 4 + i * 4)[0]
        if o < 0:
            colors.append((128, 128, 128)); continue
        name = tex[o:o + 16].split(b"\0")[0].decode("latin-1").lower()
        w, h = struct.unpack_from("<II", tex, o + 16)
        offs = struct.unpack_from("<4I", tex, o + 24)
        src, base = tex, o
        if offs[0] == 0:
            if name not in wadtex:
                colors.append((128, 128, 128)); continue
            src, base = wadtex[name], 0
            offs = struct.unpack_from("<4I", src, 24)
        pix = np.frombuffer(src, np.uint8, w * h, base + offs[0])
        pal_o = base + offs[3] + (w // 8) * (h // 8) + 2
        pal = np.frombuffer(src, np.uint8, 768, pal_o).reshape(256, 3)
        px = pal[pix]
        if name.startswith("{"):
            px = px[pix != 255]
        if name.startswith("sky"):
            colors.append((90, 110, 150)); continue
        colors.append(tuple(px.reshape(-1, 3).mean(0)) if len(px) else (128, 128, 128))
    texinfo = lump(6)
    light = lump(8)
    faces = []
    fl = lump(7)
    for i in range(len(fl) // 20):
        plane, side, first, num, ti = struct.unpack_from("<HHiHH", fl, i * 20)
        styles = fl[i * 20 + 12:i * 20 + 16]
        lofs = struct.unpack_from("<i", fl, i * 20 + 16)[0]
        idx = []
        for k in range(num):
            e = surf[first + k]
            idx.append(edges[e][0] if e >= 0 else edges[-e][1])
        poly = verts[idx]
        miptex = struct.unpack_from("<i", texinfo, ti * 40 + 32)[0]
        c = np.array(colors[miptex] if 0 <= miptex < len(colors) else (128, 128, 128), np.float32)
        b = 1.0
        if lofs >= 0 and styles[0] != 255 and lofs + 3 <= len(light):
            sample = np.frombuffer(light, np.uint8, min(48, len(light) - lofs) // 3 * 3, lofs).reshape(-1, 3)
            b = sample.mean() / 128.0
        faces.append((poly, np.clip(c * b, 0, 255)))
    ents = lump(0).decode("latin-1", "replace")
    m = re.search(r'"classname" "info_player_start"[^}]*', ents) or re.search(r'\{[^}]*"classname" "info_player_start"[^}]*\}', ents)
    spawn = np.zeros(3); yaw = 0.0
    block = re.search(r'\{[^{}]*"info_player_start"[^{}]*\}', ents)
    if block:
        o = re.search(r'"origin" "([^"]+)"', block.group(0))
        a = re.search(r'"angles" "([^"]+)"', block.group(0))
        if o:
            spawn = np.array([float(x) for x in o.group(1).split()[:3]])
        if a:
            yaw = float(a.group(1).split()[1])
    return faces, spawn, yaw


def render(faces, eye, yaw, pitch, size=(640, 400), fov=90.0):
    W, H = size
    cy, sy = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    cp, sp = math.cos(math.radians(pitch)), math.sin(math.radians(pitch))
    fwd = np.array([cy * cp, sy * cp, -sp])
    right = np.array([sy, -cy, 0.0])
    up = np.cross(right, fwd)
    f = (W / 2) / math.tan(math.radians(fov / 2))
    polys = []
    for poly, col in faces:
        rel = poly - eye
        cam = np.stack([rel @ right, rel @ up, rel @ fwd], 1)          # x right, y up, z forward
        # clip against the near plane
        out = []
        n = len(cam)
        for i in range(n):
            a, b = cam[i], cam[(i + 1) % n]
            if a[2] > 1:
                out.append(a)
            if (a[2] > 1) != (b[2] > 1):
                t = (1 - a[2]) / (b[2] - a[2])
                out.append(a + (b - a) * t)
        if len(out) < 3:
            continue
        out = np.array(out)
        scr = [(W / 2 + p[0] * f / p[2], H / 2 - p[1] * f / p[2]) for p in out]
        polys.append((out[:, 2].mean(), scr, tuple(int(x) for x in col)))
    img = Image.new("RGB", size, (0, 0, 0))
    dr = ImageDraw.Draw(img)
    for _, scr, col in sorted(polys, key=lambda p: -p[0]):
        dr.polygon(scr, fill=col)
    return img


def main():
    faces, spawn, yaw = load(sys.argv[1])
    eye = spawn + np.array([0, 0, 28.0])
    views = [render(faces, eye, yaw + a, 5) for a in (0, 90, 180, 270)]
    allp = np.concatenate([p for p, _ in faces])
    lo, hi = allp.min(0), allp.max(0)
    centre = (lo + hi) / 2
    top_eye = np.array([centre[0], centre[1], hi[2] + max(hi[0] - lo[0], hi[1] - lo[1]) * 0.9])
    top = render(faces, top_eye, 90, 89.9, fov=70)
    sheet = Image.new("RGB", (1280, 1200))
    for i, v in enumerate(views):
        sheet.paste(v, ((i % 2) * 640, (i // 2) * 400))
    sheet.paste(top.resize((640, 400)), (320, 800))
    sheet.save(sys.argv[2])
    print("spawn", spawn, "yaw", yaw, "faces", len(faces))


if __name__ == "__main__":
    main()
