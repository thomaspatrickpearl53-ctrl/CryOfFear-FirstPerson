"""Checks models/fpbody/v_fists.mdl against the models it was built from.

Decodes every animation frame of every sequence and compares bone values:
the nightstick sequences against v_nightstick.mdl, the punches against
v_actions.mdl (positions scaled by the size ratio).
Usage: py test/check_fists.py "<...>/cryoffear"
"""
import os, struct, sys

def load(path):
    d = open(path, 'rb').read()
    h = {}
    h['numbones'], h['boneindex'] = struct.unpack_from('<ii', d, 0x8c)
    h['numseq'], h['seqindex'] = struct.unpack_from('<ii', d, 0xa4)
    h['length'] = struct.unpack_from('<i', d, 0x48)[0]
    bones = []
    for i in range(h['numbones']):
        o = h['boneindex'] + i * 112
        bones.append((d[o:o+32].split(b'\0')[0].decode(), struct.unpack_from('<6f', d, o + 64), struct.unpack_from('<6f', d, o + 88)))
    seqs = []
    for i in range(h['numseq']):
        o = h['seqindex'] + i * 176
        name = d[o:o+32].split(b'\0')[0].decode()
        numframes = struct.unpack_from('<i', d, o + 56)[0]
        animindex = struct.unpack_from('<i', d, o + 124)[0]
        seqs.append((name, numframes, animindex))
    return d, h, bones, seqs

def value(d, animbase, bone, ch, frame):
    o = animbase + bone * 12
    off = struct.unpack_from('<H', d, o + ch * 2)[0]
    if off == 0:
        return 0
    p = o + off
    k = frame
    while True:
        valid, total = d[p], d[p + 1]
        if total == 0:
            raise ValueError('bad run')
        if total > k:
            break
        k -= total
        p += (valid + 1) * 2
    if valid > k:
        return struct.unpack_from('<h', d, p + (k + 1) * 2)[0]
    return struct.unpack_from('<h', d, p + valid * 2)[0]

def frame_values(m, seq, frame):
    d, h, bones, seqs = m
    out = {}
    for b, (name, val, scale) in enumerate(bones):
        out[name] = [val[c] + value(d, seqs[seq][2], b, c, frame) * scale[c] for c in range(6)]
    return out

def compare(src, sseq, fists, fseq, scale):
    assert src[3][sseq][1] == fists[3][fseq][1], 'frame count'
    worst = 0.0
    for f in range(src[3][sseq][1]):
        a = frame_values(src, sseq, f)
        b = frame_values(fists, fseq, f)
        for name, vals in b.items():
            if name not in a:
                continue
            for c in range(6):
                want = a[name][c] * (scale if c < 3 else 1.0)
                worst = max(worst, abs(want - vals[c]))
    return worst

def main():
    game = sys.argv[1]
    stick = load(os.path.join(game, 'models/weapons/nightstick/v_nightstick.mdl'))
    actions = load(os.path.join(game, 'models/weapons/v_actions.mdl'))
    fists = load(os.path.join(game, 'models/fpbody/v_fists.mdl'))
    assert fists[1]['length'] == len(fists[0]), 'length mismatch'
    ratio = dict((b[0], b) for b in stick[2])['r_wr'][1][0] / dict((b[0], b) for b in actions[2])['r_wr'][1][0]
    print('ratio', ratio, 'sequences', [s[0] for s in fists[3]])
    worst = 0.0
    for i in range(stick[1]['numseq']):
        worst = max(worst, compare(stick, i, fists, i, 1.0))
    names = [s[0] for s in actions[3]]
    worst = max(worst, compare(actions, names.index('punch1'), fists, stick[1]['numseq'], ratio))
    worst = max(worst, compare(actions, names.index('punch2'), fists, stick[1]['numseq'] + 1, ratio))
    print('worst error', worst)
    print('OK' if worst < 0.01 else 'FAIL')
    return worst < 0.01

if __name__ == '__main__':
    sys.exit(0 if main() else 1)
