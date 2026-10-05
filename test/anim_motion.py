"""How much each bone moves in a sequence of a GoldSrc model (decodes the RLE
animation data). Usage: python anim_motion.py model.mdl sequence_name"""
import struct, sys

d = open(sys.argv[1], 'rb').read()
want = sys.argv[2]
v = struct.unpack_from('<19i', d, 136)
nb, bi, nseq, seqi = v[1], v[2], v[7], v[8]
bones = []
for i in range(nb):
    o = bi + i * 112
    name = d[o:o + 32].split(b'\0')[0].decode()
    scale = struct.unpack_from('<6f', d, o + 88)
    bones.append((name, scale))

for s in range(nseq):
    o = seqi + s * 176
    if d[o:o + 32].split(b'\0')[0].decode() == want:
        break
else:
    sys.exit('no such sequence')
numframes = struct.unpack_from('<i', d, o + 56)[0]
animindex = struct.unpack_from('<i', d, o + 124)[0]


def channel(anim_off, frame):
    """Value of one animation channel at a frame (RLE: [valid, total] then values)."""
    p = anim_off
    k = frame
    while True:
        valid, total = d[p], d[p + 1]
        if total == 0:
            return 0
        if k < total:
            idx = k if k < valid else valid - 1
            return struct.unpack_from('<h', d, p + 2 + idx * 2)[0]
        k -= total
        p += 2 + valid * 2


rows = []
for b, (name, scale) in enumerate(bones):
    a = animindex + b * 12
    offs = struct.unpack_from('<6H', d, a)
    motion = 0.0
    for ch in range(3, 6):                       # rotation channels
        if offs[ch]:
            vals = [channel(a + offs[ch], f) * scale[ch] for f in range(numframes)]
            motion += max(vals) - min(vals)
    rows.append((motion, name))

for side in ('l_', 'r_'):
    total = sum(m for m, n in rows if n.startswith(side))
    print(f'{side}* arm total rotation range: {total:.2f} rad')
print('most moving bones:', ', '.join(f'{n} {m:.2f}' for m, n in sorted(rows, reverse=True)[:6]))
