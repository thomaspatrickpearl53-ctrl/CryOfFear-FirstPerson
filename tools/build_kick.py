"""Builds the kick leg viewmodel (cryoffear/models/fpbody/v_kick.mdl) from two
models already on your PC:

  * Brutal Half-Life's v_squeak.mdl: its 3-bone leg rig and the kick animations
    (FISTS_KICK, FISTS_KICKFIX, FISTS_STOMP)
  * Cry of Fear's models/cutscene/player.mdl: Simon's left leg and its textures

Simon's thigh, calf and foot are each fitted onto the matching Brutal
Half-Life bone (re-oriented and stretched to its length), so his leg plays the
kick. Nothing from either game ships with the mod; this runs on your own files.

Usage:
  py tools/build_kick.py [--bhl <v_squeak.mdl>] [--simon <player.mdl>] [--out <v_kick.mdl> ...]
Defaults use the standard Steam folders and write into Cry of Fear's folder.
"""
import argparse, math, os, struct, sys
import numpy as np

STEAM = r"C:\Program Files (x86)\Steam\steamapps\common"
DEF_BHL = os.path.join(STEAM, r"Half-Life\bhl_v3\models\v_squeak.mdl")
DEF_SIMON = os.path.join(STEAM, r"Cry of Fear\cryoffear\models\cutscene\player.mdl")
DEF_OUT = os.path.join(STEAM, r"Cry of Fear\cryoffear\models\fpbody\v_kick.mdl")

KICK_SEQS = ["FISTS_KICK", "FISTS_KICKFIX", "FISTS_STOMP"]
BHL_BONES = ["Hands biped", "joint76", "joint77", "joint78"]        # root, thigh, calf, foot
SIMON_LEG = ["Bip01 L Leg", "Bip01 L Leg1", "Bip01 L Foot", "Bip01 L Toe0", "Bip01 L Toe01"]
SEGMENT_OF = {0: 0, 1: 1, 2: 2, 3: 2, 4: 2}                       # Simon leg bone -> segment (thigh/calf/foot)

# ---------------------------------------------------------------------------
# Reading studio models (format version 10)
# ---------------------------------------------------------------------------

class Model:
    def __init__(self, path):
        self.d = d = open(path, "rb").read()
        if d[:4] != b"IDST" or struct.unpack_from("<i", d, 4)[0] != 10:
            raise SystemExit("%s is not a GoldSrc studio model" % path)
        (self.numbones, self.boneindex, _, _, _, _, self.numseq, self.seqindex, self.numseqgroups, self.seqgroupindex,
         self.numtextures, self.textureindex, self.texturedataindex, self.numskinref, self.numskinfamilies,
         self.skinindex, self.numbodyparts, self.bodypartindex) = struct.unpack_from("<18i", d, 0x8C)
        if self.numseqgroups != 1 or struct.unpack_from("<i", d, self.seqgroupindex + 100)[0] != 0:
            raise SystemExit("%s keeps animations in separate files; not supported" % path)
        self.bones = []
        for i in range(self.numbones):
            o = self.boneindex + i * 112
            name = d[o:o + 32].split(b"\0")[0].decode()
            parent = struct.unpack_from("<i", d, o + 32)[0]
            self.bones.append((name, parent, struct.unpack_from("<6f", d, o + 64), struct.unpack_from("<6f", d, o + 88), d[o:o + 112]))

    def bone(self, name):
        for i, b in enumerate(self.bones):
            if b[0] == name:
                return i
        raise SystemExit("bone %r not found" % name)

    def seq(self, name):
        for i in range(self.numseq):
            o = self.seqindex + i * 176
            if self.d[o:o + 32].split(b"\0")[0].decode().lower() == name.lower():
                return i, o
        raise SystemExit("sequence %r not found" % name)

    def anim_value(self, animindex, bone, ch, frame):
        o = animindex + bone * 12
        off = struct.unpack_from("<H", self.d, o + ch * 2)[0]
        if off == 0:
            return 0
        p, k = o + off, frame
        while True:
            valid, total = self.d[p], self.d[p + 1]
            if total == 0:
                raise SystemExit("bad animation data")
            if total > k:
                break
            k -= total
            p += (valid + 1) * 2
        return struct.unpack_from("<h", self.d, p + (k + 1) * 2 if valid > k else p + valid * 2)[0]

    def rest_world(self):
        world = []
        for name, parent, val, scale, raw in self.bones:
            local = matrix(val[3:], val[:3])
            world.append(local if parent < 0 else world[parent] @ local)
        return world


def matrix(ang, pos):
    """GoldSrc AngleQuaternion + QuaternionMatrix: angles are (roll x, pitch y, yaw z) radians."""
    sr, cr = math.sin(ang[0] * .5), math.cos(ang[0] * .5)
    sp, cp = math.sin(ang[1] * .5), math.cos(ang[1] * .5)
    sy, cy = math.sin(ang[2] * .5), math.cos(ang[2] * .5)
    x = sr * cp * cy - cr * sp * sy
    y = cr * sp * cy + sr * cp * sy
    z = cr * cp * sy - sr * sp * cy
    w = cr * cp * cy + sr * sp * sy
    m = np.eye(4)
    m[0, :3] = [1 - 2 * y * y - 2 * z * z, 2 * x * y - 2 * w * z, 2 * x * z + 2 * w * y]
    m[1, :3] = [2 * x * y + 2 * w * z, 1 - 2 * x * x - 2 * z * z, 2 * y * z - 2 * w * x]
    m[2, :3] = [2 * x * z - 2 * w * y, 2 * y * z + 2 * w * x, 1 - 2 * x * x - 2 * y * y]
    m[:3, 3] = pos
    return m


def frame_axes(x, hint):
    x = x / np.linalg.norm(x)
    z = np.cross(x, hint)
    z /= np.linalg.norm(z)
    return np.column_stack([x, np.cross(z, x), z])


def triangles(m, model_off):
    """All triangles of a studio submodel as (mesh skinref, [(vert, norm, s, t) x3])."""
    d = m.d
    nummesh, meshindex = struct.unpack_from("<ii", d, model_off + 72)
    out = []
    for k in range(nummesh):
        numtris, triindex, skinref, numnorms, normindex = struct.unpack_from("<5i", d, meshindex + k * 20)
        p = triindex
        while True:
            count = struct.unpack_from("<h", d, p)[0]
            p += 2
            if count == 0:
                break
            n = abs(count)
            v = [struct.unpack_from("<4h", d, p + i * 8) for i in range(n)]
            p += n * 8
            for i in range(2, n):
                if count < 0:                      # fan
                    tri = (v[0], v[i - 1], v[i])
                elif i % 2:                        # strip, odd: flipped
                    tri = (v[i - 1], v[i - 2], v[i])
                else:
                    tri = (v[i - 2], v[i - 1], v[i])
                out.append((skinref, tri))
    return out

# ---------------------------------------------------------------------------
# Animation encoding (run-length value spans, one span per <=255 frames)
# ---------------------------------------------------------------------------

def encode_anims(frames_vals, rest, scales):
    """frames_vals[f][b][c] floats -> bytes of mstudioanim_t[nb] + value spans."""
    nb = len(rest)
    nf = len(frames_vals)
    table = [[0] * 6 for _ in range(nb)]
    values = []
    tbytes = nb * 12
    for b in range(nb):
        for c in range(6):
            raw = [int(math.floor((frames_vals[f][b][c] - rest[b][c]) / scales[b][c] + 0.5)) for f in range(nf)]
            if not any(raw):
                continue
            off = tbytes + len(values) * 2 - b * 12
            if off > 0xFFFF:
                raise SystemExit("animation too big")
            table[b][c] = off
            for f in range(0, nf, 255):
                chunk = raw[f:f + 255]
                values.append(len(chunk) | (len(chunk) << 8))     # span header: valid, total (bytes)
                values.extend(max(-32768, min(32767, v)) for v in chunk)
    out = b"".join(struct.pack("<6H", *t) for t in table)
    return out + b"".join(struct.pack("<H", v & 0xFFFF) for v in values)

# ---------------------------------------------------------------------------

def build(bhl_path, simon_path):
    bhl, simon = Model(bhl_path), Model(simon_path)

    # --- skeleton: BHL root + leg chain
    bidx = [bhl.bone(n) for n in BHL_BONES]
    bones_out = []
    for i, bi in enumerate(bidx):
        raw = bytearray(bhl.bones[bi][4])
        struct.pack_into("<i", raw, 32, i - 1)                        # parent: previous in chain
        struct.pack_into("<6i", raw, 40, -1, -1, -1, -1, -1, -1)       # no bone controllers
        bones_out.append(raw)
    rest = [list(bhl.bones[bi][2]) for bi in bidx]

    # --- animations
    seqs = []
    for name in KICK_SEQS:
        si, so = bhl.seq(name)
        numframes = max(1, struct.unpack_from("<i", bhl.d, so + 56)[0])
        animindex = struct.unpack_from("<i", bhl.d, so + 124)[0]
        frames = []
        for f in range(numframes):
            frames.append([[bhl.bones[bi][2][c] + bhl.anim_value(animindex, bi, c, f) * bhl.bones[bi][3][c] for c in range(6)] for bi in bidx])
        seqs.append((so, frames))
    scales = []
    for b in range(len(bidx)):
        row = []
        for c in range(6):
            dev = max(abs(fr[b][c] - rest[b][c]) for _, frames in seqs for fr in frames)
            row.append(dev / 32000.0 if dev > 1e-6 else 1.0)
        scales.append(row)
    for b, raw in enumerate(bones_out):
        struct.pack_into("<6f", raw, 88, *scales[b])

    # --- fit Simon's leg onto the BHL leg bones
    WB, WS = bhl.rest_world(), simon.rest_world()
    J = [WB[bi][:3, 3] for bi in bidx]                                 # root, hip, knee, ankle
    sidx = [simon.bone(n) for n in SIMON_LEG]
    S = [WS[i][:3, 3] for i in sidx]                                   # hip, knee, ankle, toe0, toe tip

    # BHL foot direction: from the ankle toward its farthest foot vertex.
    foot_pts = []
    for p in range(bhl.numbodyparts):
        nmodels, _, modelindex = struct.unpack_from("<iii", bhl.d, bhl.bodypartindex + p * 76 + 64)
        for mi in range(nmodels):
            mo = modelindex + mi * 112
            numverts, vinfo, vindex = struct.unpack_from("<iii", bhl.d, mo + 80)
            for v in range(numverts):
                if bhl.d[vinfo + v] == bidx[3]:
                    foot_pts.append((WB[bidx[3]] @ np.array(struct.unpack_from("<3f", bhl.d, vindex + v * 12) + (1.0,)))[:3])
    bhl_toe = max(foot_pts, key=lambda q: np.linalg.norm(q - J[3]))

    s_toe_dir = S[4] - S[2]
    b_toe_dir = bhl_toe - J[3]
    seg_S = [(S[0], frame_axes(S[1] - S[0], s_toe_dir), np.linalg.norm(S[1] - S[0])),
             (S[1], frame_axes(S[2] - S[1], s_toe_dir), np.linalg.norm(S[2] - S[1])),
             (S[2], frame_axes(s_toe_dir, S[1] - S[2]), np.linalg.norm(s_toe_dir))]
    seg_B = [(J[1], frame_axes(J[2] - J[1], b_toe_dir), np.linalg.norm(J[2] - J[1])),
             (J[2], frame_axes(J[3] - J[2], b_toe_dir), np.linalg.norm(J[3] - J[2])),
             (J[3], frame_axes(b_toe_dir, J[2] - J[3]), np.linalg.norm(b_toe_dir))]
    cross_scale = seg_B[1][2] / seg_S[1][2]                            # calf ratio keeps the leg's thickness
    seg_bone = [1, 2, 3]                                               # output bone of each segment

    def fit_point(seg, p):
        (oS, rS, lS), (oB, rB, lB) = seg_S[seg], seg_B[seg]
        a, b, c = rS.T @ (p - oS)
        return oB + rB @ np.array([a * lB / lS, b * cross_scale, c * cross_scale])

    def fit_normal(seg, n):
        return seg_B[seg][1] @ (seg_S[seg][1].T @ n)

    # Simon's submodel and the triangles fully on the leg
    nmodels, _, modelindex = struct.unpack_from("<iii", simon.d, simon.bodypartindex + 64)
    mo = modelindex
    numverts, vinfo, vindex, numnorms, ninfo, nindex = struct.unpack_from("<6i", simon.d, mo + 80)
    vbone = simon.d[vinfo:vinfo + numverts]
    nbone = simon.d[ninfo:ninfo + numnorms]
    leg_of = {bi: k for k, bi in enumerate(sidx)}
    tris = [(sk, t) for sk, t in triangles(simon, mo) if all(vbone[v[0]] in leg_of for v in t)]
    if not tris:
        raise SystemExit("no leg triangles found on Simon's model")

    skins = struct.unpack_from("<%dh" % simon.numskinref, simon.d, simon.skinindex)   # family 0
    tex_used = sorted({skins[sk] for sk, _ in tris})
    tex_new = {t: i for i, t in enumerate(tex_used)}

    # vertices (shared) and normals (contiguous per mesh, as the renderer lights them in mesh order)
    vert_new, verts_out, vbone_out = {}, [], []
    for _, t in tris:
        for v in t:
            if v[0] not in vert_new:
                seg = SEGMENT_OF[leg_of[vbone[v[0]]]]
                p = WS[vbone[v[0]]] @ np.array(struct.unpack_from("<3f", simon.d, vindex + v[0] * 12) + (1.0,))
                q = fit_point(seg, p[:3])
                ob = seg_bone[seg]
                local = np.linalg.inv(WB[bidx[ob]]) @ np.append(q, 1.0)
                vert_new[v[0]] = len(verts_out)
                verts_out.append(local[:3])
                vbone_out.append(ob)
    meshes = []                      # (skinref, [tri as (v, n, s, t)], norm list)
    norms_out, nbone_out = [], []
    for tex in tex_used:
        mt = [t for sk, t in tris if skins[sk] == tex]
        nmap = {}
        start = len(norms_out)
        out_tris = []
        for t in mt:
            nt = []
            for v in t:
                if v[1] not in nmap:
                    b = nbone[v[1]] if nbone[v[1]] in leg_of else vbone[v[0]]
                    seg = SEGMENT_OF[leg_of[b]]
                    n = WS[b][:3, :3] @ np.array(struct.unpack_from("<3f", simon.d, nindex + v[1] * 12))
                    n = fit_normal(seg, n)
                    ob = seg_bone[seg]
                    n = WB[bidx[ob]][:3, :3].T @ n
                    nmap[v[1]] = len(norms_out)
                    norms_out.append(n / np.linalg.norm(n))
                    nbone_out.append(ob)
                nt.append((vert_new[v[0]], nmap[v[1]], v[2], v[3]))
            out_tris.append(nt)
        meshes.append((tex_new[tex], out_tris, start, len(norms_out) - start))

    # ---------------------------------------------------------------------------
    # Write the file. Everything except texture pixels comes before
    # texturedataindex: GoldSrc drops what follows it after uploading textures.
    # ---------------------------------------------------------------------------
    out = bytearray(244)
    def here():
        while len(out) % 4:
            out.append(0)
        return len(out)

    boneindex = here()
    for raw in bones_out:
        out += raw
    bonecontrollerindex = hitboxindex = attachmentindex = here()

    # sequences: descriptors first, animation data later
    seqindex = here()
    seq_desc_offs = []
    for so, frames in seqs:
        desc = bytearray(bhl.d[so:so + 176])
        struct.pack_into("<ii", desc, 48, 0, 0)            # no events (they reference Half-Life sounds)
        struct.pack_into("<ii", desc, 60, 0, 0)            # no pivots
        struct.pack_into("<ii", desc, 68, 0, 0)            # no motion extraction
        struct.pack_into("<3f", desc, 76, 0, 0, 0)
        struct.pack_into("<i", desc, 120, 1)               # one blend
        struct.pack_into("<i", desc, 156, 0)               # seqgroup 0
        struct.pack_into("<4i", desc, 160, 0, 0, 0, 0)     # entry/exit nodes, nodeflags, nextseq
        seq_desc_offs.append(len(out))
        out += desc
    seqgroupindex = here()
    out += b"default".ljust(32, b"\0") + b"".ljust(64, b"\0") + struct.pack("<ii", 0, 0)

    bodypartindex = here()
    bp_off = len(out); out += bytes(76)
    modelindex = here()
    model_off = len(out); out += bytes(112)
    meshindex = here()
    mesh_off = len(out); out += bytes(20 * len(meshes))
    vinfo_out = here(); out += bytes(vbone_out)
    ninfo_out = here(); out += bytes(nbone_out)
    vindex_out = here()
    for v in verts_out:
        out += struct.pack("<3f", *v)
    nindex_out = here()
    for n in norms_out:
        out += struct.pack("<3f", *n)
    for k, (skinref, mtris, nstart, ncount) in enumerate(meshes):
        tri_off = here()
        for t in mtris:
            out += struct.pack("<h", 3)
            for v in t:
                out += struct.pack("<4h", *v)
        out += struct.pack("<h", 0)
        struct.pack_into("<5i", out, mesh_off + k * 20, len(mtris), tri_off, skinref, ncount, nindex_out + nstart * 12)
    pts = np.array([(np.array(WB_out) @ np.append(v, 1.0))[:3] for v, WB_out in zip(verts_out, [WB[bidx[b]] for b in vbone_out])])
    radius = float(np.max(np.linalg.norm(pts, axis=1)))
    struct.pack_into("<64sif10i", out, model_off, b"simon_leg", 0, radius, len(meshes), mesh_off,
                     len(verts_out), vinfo_out, vindex_out, len(norms_out), ninfo_out, nindex_out, 0, 0)
    struct.pack_into("<64siii", out, bp_off, b"leg", 1, 1, model_off)

    for k, (so, frames) in enumerate(seqs):
        a = here()
        out += encode_anims(frames, rest, scales)
        struct.pack_into("<i", out, seq_desc_offs[k] + 124, a)

    # textures: headers, skin table, then pixel data (last)
    textureindex = here()
    tex_hdr_off = len(out); out += bytes(80 * len(tex_used))
    skinindex = here()
    for i in range(len(tex_used)):
        out += struct.pack("<h", i)
    texturedataindex = here()
    for i, t in enumerate(tex_used):
        to = simon.textureindex + t * 80
        name, flags, w, h, index = struct.unpack_from("<64siiii", simon.d, to)
        pix = simon.d[index:index + w * h + 768]
        pos = len(out)
        out += pix
        struct.pack_into("<64siiii", out, tex_hdr_off + i * 80, name, flags, w, h, pos)

    mins = pts.min(0) - 2
    maxs = pts.max(0) + 2
    struct.pack_into("<4si64si3f3f3f3f3fi", out, 0, b"IDST", 10, b"fpbody/v_kick.mdl", len(out),
                     0, 0, 0, *mins, *maxs, *mins, *maxs, 0)
    struct.pack_into("<26i", out, 0x8C,
                     len(bones_out), boneindex, 0, bonecontrollerindex, 0, hitboxindex,
                     len(seqs), seqindex, 1, seqgroupindex,
                     len(tex_used), textureindex, texturedataindex,
                     len(tex_used), 1, skinindex,
                     1, bodypartindex, 0, attachmentindex,
                     0, 0, 0, 0, 0, 0)
    struct.pack_into("<i", out, 0x48, len(out))
    info = "%d triangles, %d vertices, %d textures (%s), %d sequences, thigh x%.2f calf x%.2f foot x%.2f" % (
        len(tris), len(verts_out), len(tex_used),
        ", ".join(simon.d[simon.textureindex + t * 80:simon.textureindex + t * 80 + 64].split(b"\0")[0].decode() for t in tex_used),
        len(seqs), seg_B[0][2] / seg_S[0][2], seg_B[1][2] / seg_S[1][2], seg_B[2][2] / seg_S[2][2])
    return bytes(out), info


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--bhl", default=DEF_BHL)
    ap.add_argument("--simon", default=DEF_SIMON)
    ap.add_argument("--out", action="append")
    a = ap.parse_args()
    data, info = build(a.bhl, a.simon)
    for path in a.out or [DEF_OUT]:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        open(path, "wb").write(data)
        print("wrote", path, len(data), "bytes")
    print(info)


if __name__ == "__main__":
    main()
