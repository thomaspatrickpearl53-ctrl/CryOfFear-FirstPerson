"""Prints the rest-pose (reference skeleton) world positions of chosen bones of
a studio model, plus the bounding box of the vertices on those bones.
Usage: py test/kick_rig.py <model.mdl> "<bone>" ["<bone>" ...]
"""
import struct, sys
import numpy as np
sys.path.insert(0, __file__.rsplit('\\', 1)[0].rsplit('/', 1)[0])
from check_fists import load            # noqa: E402
from render_fists import quat, mat      # noqa: E402

m = load(sys.argv[1])
d, h, bones, seqs = m
parents = [struct.unpack_from('<i', d, h['boneindex'] + i * 112 + 32)[0] for i in range(len(bones))]
world = []
for i, (name, val, scale) in enumerate(bones):
    local = mat(quat(val[3:]), val[:3])
    world.append(local if parents[i] < 0 else world[parents[i]] @ local)

numbp, bpindex = struct.unpack_from('<ii', d, 0xcc)
verts = {}
for p in range(numbp):
    nmodels, base, modelindex = struct.unpack_from('<iii', d, bpindex + p * 76 + 64)
    for mi in range(nmodels):
        mo = modelindex + mi * 112
        name = d[mo:mo + 64].split(b'\0')[0].decode()
        numverts, vinfo, vindex = struct.unpack_from('<iii', d, mo + 80)
        for v in range(numverts):
            b = d[vinfo + v]
            p3 = np.array(struct.unpack_from('<3f', d, vindex + v * 12) + (1.0,))
            verts.setdefault(b, []).append((name, (world[b] @ p3)[:3]))

names = [b[0] for b in bones]
for want in sys.argv[2:]:
    i = names.index(want)
    pos = world[i][:3, 3]
    vs = verts.get(i, [])
    line = '%-16s #%-3d parent %-3d pos (%7.2f %7.2f %7.2f)' % (want, i, parents[i], *pos)
    if vs:
        pts = np.array([p for _, p in vs])
        models = sorted(set(n for n, _ in vs))
        line += '  %3d verts %s  min (%6.1f %6.1f %6.1f) max (%6.1f %6.1f %6.1f)' % (len(vs), models, *pts.min(0), *pts.max(0))
    print(line)
