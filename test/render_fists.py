"""Renders points of a viewmodel's hands at chosen sequence frames, seen from
the player's eye (x forward, y left, z up), to a PNG contact sheet.
Usage: py test/render_fists.py <model.mdl> <out.png> seq:frame [seq:frame ...]
"""
import math, struct, sys
import numpy as np
from PIL import Image, ImageDraw
sys.path.insert(0, __file__.rsplit('\\', 1)[0].rsplit('/', 1)[0])
from check_fists import load, frame_values  # noqa: E402  (module also runs checks when given argv)

def quat(a):
    # GoldSrc AngleQuaternion (roll=x, pitch=y, yaw=z radians)
    sr, cr = math.sin(a[0] * .5), math.cos(a[0] * .5)
    sp, cp = math.sin(a[1] * .5), math.cos(a[1] * .5)
    sy, cy = math.sin(a[2] * .5), math.cos(a[2] * .5)
    return (sr * cp * cy - cr * sp * sy, cr * sp * cy + sr * cp * sy, cr * cp * sy - sr * sp * cy, cr * cp * cy + sr * sp * sy)

def mat(q, p):
    x, y, z, w = q
    m = np.eye(4)
    m[0, :3] = [1 - 2 * y * y - 2 * z * z, 2 * x * y - 2 * w * z, 2 * x * z + 2 * w * y]
    m[1, :3] = [2 * x * y + 2 * w * z, 1 - 2 * x * x - 2 * z * z, 2 * y * z - 2 * w * x]
    m[2, :3] = [2 * x * z - 2 * w * y, 2 * y * z + 2 * w * x, 1 - 2 * x * x - 2 * y * y]
    m[:3, 3] = p
    return m

def verts(m):
    d, h, bones, seqs = m
    numbp, bpindex = struct.unpack_from('<ii', d, 0xcc)
    out = []
    for p in range(numbp):
        po = bpindex + p * 76
        nmodels, base, modelindex = struct.unpack_from('<iii', d, po + 64)
        mo = modelindex  # first model of each part
        numverts, vinfo, vindex = struct.unpack_from('<iii', d, mo + 80)
        for v in range(numverts):
            out.append((d[vinfo + v], struct.unpack_from('<3f', d, vindex + v * 12)))
    return out

def pose(m, seq, frame):
    vals = frame_values(m, seq, frame)
    bones = m[2]
    mats = []
    parents = [struct.unpack_from('<i', m[0], m[1]['boneindex'] + i * 112 + 32)[0] for i in range(len(bones))]
    for i, (name, _, _) in enumerate(bones):
        v = vals[name]
        local = mat(quat(v[3:]), v[:3])
        mats.append(local if parents[i] < 0 else mats[parents[i]] @ local)
    return mats

if __name__ == '__main__':
    m = load(sys.argv[1])
    vs = verts(m)
    shots = sys.argv[3:]
    W, H = 320, 240
    sheet = Image.new('RGB', (W * len(shots), H + 16), (20, 20, 20))
    draw = ImageDraw.Draw(sheet)
    for i, shot in enumerate(shots):
        seq, frame = map(int, shot.split(':'))
        mats = pose(m, seq, frame)
        pts = np.array([(mats[b] @ np.array([*p, 1.0]))[:3] for b, p in vs])
        pts = pts[pts[:, 0] > 0.5]
        # perspective from the eye at the origin, ~90 degree horizontal fov
        sx = (-pts[:, 1] / pts[:, 0] + 1) * W / 2
        sy = (-pts[:, 2] / pts[:, 0]) * W / 2 + H / 2
        for x, y in zip(sx, sy):
            if 0 <= x < W and 0 <= y < H:
                sheet.putpixel((int(x) + i * W, int(y) + 16), (220, 200, 170))
        draw.rectangle([i * W, 16, i * W + W - 1, H + 15], outline=(90, 90, 90))
        draw.text((i * W + 4, 2), '%s f%d' % (m[3][seq][0], frame), fill=(255, 255, 255))
    sheet.save(sys.argv[2])
