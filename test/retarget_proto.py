"""Prototype of the world-space retarget used by src/fpchars.cpp: renders a
character walking with Simon's walk, side view, next to Simon.
Usage: py test/retarget_proto.py <simon.mdl> <out.png> <character.mdl> [...]"""
import math, struct, sys
import numpy as np
from PIL import Image, ImageDraw
sys.path.insert(0, __file__.rsplit("\\", 1)[0].rsplit("/", 1)[0])
from check_fists import load, frame_values
from render_fists import quat, mat, verts


def rot(ang):
    return mat(quat(ang), (0, 0, 0))[:3, :3]


def angles(m):
    """Inverse of GoldSrc AngleQuaternion's rotation (Rz(yaw) Ry(pitch) Rx(roll))."""
    pitch = math.asin(max(-1.0, min(1.0, -m[2, 0])))
    roll = math.atan2(m[2, 1], m[2, 2])
    yaw = math.atan2(m[1, 0], m[0, 0])
    return roll, pitch, yaw


def parents(m):
    return [struct.unpack_from("<i", m[0], m[1]["boneindex"] + i * 112 + 32)[0] for i in range(len(m[2]))]


def globals_rot(local_rots, par):
    g = []
    for i, r in enumerate(local_rots):
        g.append(r if par[i] < 0 else g[par[i]] @ r)
    return g


def retarget_frame(S, C, seq, f, k):
    sp, cp = parents(S), parents(C)
    snames = [b[0].lower() for b in S[2]]
    srest = globals_rot([rot(b[1][3:]) for b in S[2]], sp)
    crest = globals_rot([rot(b[1][3:]) for b in C[2]], cp)
    fv = frame_values(S, seq, f)
    sframe = globals_rot([rot(fv[b[0]][3:]) for b in S[2]], sp)
    out_local = []
    cglob = []
    for i, (name, val, scale) in enumerate(C[2]):
        s = snames.index(name.lower()) if name.lower() in snames else -1
        if s >= 0:
            g = sframe[s] @ srest[s].T @ crest[i]
        else:
            g = (cglob[cp[i]] if cp[i] >= 0 else np.eye(3)) @ rot(val[3:])
        cglob.append(g)
        local = g if cp[i] < 0 else cglob[cp[i]].T @ g
        pos = list(val[:3])
        if cp[i] < 0 and s >= 0:
            sv = fv[S[2][s][0]]
            pos = [val[c] + (sv[c] - S[2][s][1][c]) * k for c in range(3)]
        out_local.append((angles(local), pos))
    return out_local


def draw(m, local, W, H, img, ox, oy):
    par = parents(m)
    mats = []
    for i, (ang, pos) in enumerate(local):
        L = mat(quat(ang), pos)
        mats.append(L if par[i] < 0 else mats[par[i]] @ L)
    vs = verts(m)
    pts = np.array([(mats[b] @ np.array([*p, 1.0]))[:3] for b, p in vs])
    sx = W / 2 + pts[:, 0] * 2.0
    sy = H - 20 - (pts[:, 2] - pts[:, 2].min()) * 2.0
    for x, y in zip(sx, sy):
        if 0 <= x < W and 0 <= y < H:
            img.putpixel((int(x) + ox, int(y) + oy), (220, 200, 170))


S = load(sys.argv[1])
seq = [s[0].lower() for s in S[3]].index("walk")
frames = S[3][seq][1]
W, H = 220, 300
chars = sys.argv[3:]
img = Image.new("RGB", (W * 4, H * len(chars)), (20, 20, 20))
dr = ImageDraw.Draw(img)
for row, p in enumerate(chars):
    C = load(p)
    for col in range(4):
        f = int(col * frames / 4)
        draw(C, retarget_frame(S, C, seq, f, 1.0), W, H, img, col * W, row * H)
        dr.text((col * W + 4, row * H + 4), "%s f%d" % (p.split("\\")[-1][:22], f), fill=(255, 255, 255))
img.save(sys.argv[2])
