"""Renders viewmodels through their real triangle lists (flat-shaded with each
texture's average colour) from the player's eye, to check what is drawn.
Usage: py test/render_vm.py <out.png> seq:frame <model.mdl> [<model.mdl> ...]"""
import struct, sys
import numpy as np
from PIL import Image, ImageDraw
sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])
from check_fists import load
from render_fists import pose


def tex_colors(d):
    nt, ti = struct.unpack_from("<ii", d, 0xB4)
    out = []
    for i in range(nt):
        w, h, idx = struct.unpack_from("<iii", d, ti + i * 80 + 68)
        pix = np.frombuffer(d, np.uint8, w * h, idx)
        pal = np.frombuffer(d, np.uint8, 768, idx + w * h).reshape(256, 3)
        out.append(tuple(int(c) for c in pal[pix].mean(0)))
    return out


def render(path, seq, frame, W=360, H=270):
    m = load(path)
    d = m[0]
    frame = min(frame, max(1, m[3][seq][1]) - 1)
    mats = pose(m, seq, frame)
    cols = tex_colors(d)
    nsr, nsf, si = struct.unpack_from("<iii", d, 0xC0)
    skins = struct.unpack_from("<%dh" % nsr, d, si)
    npart, pi = struct.unpack_from("<ii", d, 0xCC)
    polys = []
    for p in range(npart):
        nm, base, mi = struct.unpack_from("<iii", d, pi + p * 76 + 64)
        mo = mi                                            # first model of each body part
        nmesh, meshi, nverts, vinfo, vindex = struct.unpack_from("<iiiii", d, mo + 72)
        vb = d[vinfo:vinfo + nverts]
        V = np.frombuffer(d, "<f4", nverts * 3, vindex).reshape(-1, 3)
        world = np.array([(mats[vb[i]] @ np.append(V[i], 1.0))[:3] for i in range(nverts)])
        for k in range(nmesh):
            numtris, triindex, skinref = struct.unpack_from("<iii", d, meshi + k * 20)
            col = cols[skins[skinref]] if skinref < len(skins) and skins[skinref] < len(cols) else (255, 0, 255)
            q = triindex
            while True:
                c = struct.unpack_from("<h", d, q)[0]; q += 2
                if c == 0:
                    break
                n = abs(c)
                idx = [struct.unpack_from("<h", d, q + j * 8)[0] for j in range(n)]
                q += n * 8
                for j in range(2, n):
                    tri = (idx[0], idx[j - 1], idx[j]) if c < 0 else ((idx[j - 2], idx[j - 1], idx[j]) if j % 2 == 0 else (idx[j - 1], idx[j - 2], idx[j]))
                    pts = world[list(tri)]
                    if np.any(pts[:, 0] < 1):
                        continue
                    polys.append((pts[:, 0].mean(), [(W / 2 - p_[1] / p_[0] * W / 2, H / 2 - p_[2] / p_[0] * W / 2) for p_ in pts], col))
    img = Image.new("RGB", (W, H), (25, 25, 28))
    dr = ImageDraw.Draw(img)
    for _, pts, col in sorted(polys, key=lambda x: -x[0]):
        dr.polygon(pts, fill=col)
    return img


out = sys.argv[1]
seq, frame = map(int, sys.argv[2].split(":"))
imgs = [render(p, seq, frame) for p in sys.argv[3:]]
sheet = Image.new("RGB", (360 * len(imgs), 270))
for i, im in enumerate(imgs):
    sheet.paste(im, (i * 360, 0))
sheet.save(out)
