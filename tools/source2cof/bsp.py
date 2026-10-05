"""Reads what the converter needs from a Source engine map (VBSP v19-v21):
brushes with their sides and texture placement, entities, which brushes
belong to which brush entity, and the embedded pakfile."""
import io, lzma, re, struct, zipfile

L_ENTITIES, L_PLANES, L_TEXDATA, L_NODES, L_TEXINFO, L_LEAFS, L_MODELS, L_LEAFBRUSHES, L_BRUSHES, \
    L_BRUSHSIDES, L_DISPINFO, L_GAME, L_PAKFILE, L_TEXSTR_DATA, L_TEXSTR_TABLE = \
    0, 1, 2, 5, 6, 10, 14, 17, 18, 19, 26, 35, 40, 43, 44

# brush contents
CONTENTS_SOLID, CONTENTS_WINDOW, CONTENTS_GRATE, CONTENTS_SLIME, CONTENTS_WATER = 0x1, 0x2, 0x8, 0x10, 0x20
CONTENTS_MOVEABLE, CONTENTS_AREAPORTAL, CONTENTS_PLAYERCLIP, CONTENTS_MONSTERCLIP = 0x4000, 0x8000, 0x10000, 0x20000
CONTENTS_ORIGIN, CONTENTS_DETAIL, CONTENTS_TRANSLUCENT, CONTENTS_LADDER = 0x1000000, 0x8000000, 0x10000000, 0x20000000

# texinfo flags
SURF_SKY2D, SURF_SKY, SURF_NODRAW, SURF_HINT, SURF_SKIP, SURF_TRIGGER = 0x2, 0x4, 0x80, 0x100, 0x200, 0x40


def _lzma(blob):
    """Valve's LZMA lump: 'LZMA', actual size, lzma size, 5 property bytes, data."""
    if blob[:4] != b"LZMA":
        return blob
    actual, packed = struct.unpack_from("<II", blob, 4)
    props = blob[12:17]
    alone = props + struct.pack("<Q", actual) + blob[17:17 + packed]
    return lzma.LZMADecompressor(lzma.FORMAT_ALONE).decompress(alone)[:actual]


def parse_entities(text):
    ents = []
    for block in re.finditer(r"\{(.*?)\}", text, re.S):
        e = []
        for k, v in re.findall(r'"([^"]*)"\s+"([^"]*)"', block.group(1)):
            e.append((k, v))
        ents.append(e)
    return ents


class Bsp:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.data = d = f.read()
        if d[:4] != b"VBSP":
            raise SystemExit("%s is not a Source map (VBSP); GoldSrc maps don't need converting" % path)
        self.version = struct.unpack_from("<i", d, 4)[0]
        if not 19 <= self.version <= 21:
            raise SystemExit("unsupported VBSP version %d (supported: 19-21)" % self.version)
        self.lumps = [struct.unpack_from("<iiii", d, 8 + i * 16) for i in range(64)]   # ofs, len, ver, fourcc

        self.planes = [struct.unpack_from("<4f", b, i * 20) for b in [self.lump(L_PLANES)] for i in range(len(b) // 20)]
        strdata = self.lump(L_TEXSTR_DATA)
        table = self.lump(L_TEXSTR_TABLE)
        names = []
        for i in range(len(table) // 4):
            o = struct.unpack_from("<i", table, i * 4)[0]
            names.append(strdata[o:strdata.index(b"\0", o)].decode("latin-1"))
        td = self.lump(L_TEXDATA)
        self.texdata = []                    # (material name, width, height)
        for i in range(len(td) // 32):
            nid, w, h = struct.unpack_from("<iii", td, i * 32 + 12)
            self.texdata.append((names[nid] if 0 <= nid < len(names) else "", w, h))
        ti = self.lump(L_TEXINFO)
        self.texinfo = []                    # (s vec4, t vec4, flags, texdata)
        for i in range(len(ti) // 72):
            v = struct.unpack_from("<16f", ti, i * 72)
            flags, tdi = struct.unpack_from("<ii", ti, i * 72 + 64)
            self.texinfo.append((v[0:4], v[4:8], flags, tdi))
        br = self.lump(L_BRUSHES)
        self.brushes = [struct.unpack_from("<iii", br, i * 12) for i in range(len(br) // 12)]     # firstside, numsides, contents
        bs = self.lump(L_BRUSHSIDES)
        self.sides = []                      # (plane, texinfo, dispinfo, bevel)
        for i in range(len(bs) // 8):
            plane, tex, disp = struct.unpack_from("<Hhh", bs, i * 8)
            bevel = bs[i * 8 + 6]
            self.sides.append((plane, tex, disp, bevel))
        md = self.lump(L_MODELS)
        self.models = [struct.unpack_from("<9fiii", md, i * 48) for i in range(len(md) // 48)]   # mins, maxs, origin, headnode, ...
        self.entities = parse_entities(self.lump(L_ENTITIES).decode("latin-1", "replace"))
        self._model_brushes = None

    def lump(self, i):
        ofs, length, ver, fourcc = self.lumps[i]
        blob = self.data[ofs:ofs + length]
        return _lzma(blob)

    def pakfile(self):
        """Files embedded in the map (custom and cubemap-patched materials), lower-case path -> bytes."""
        blob = self.lump(L_PAKFILE)
        out = {}
        if not blob:
            return out
        try:
            with zipfile.ZipFile(io.BytesIO(blob)) as z:
                for n in z.namelist():
                    if not n.endswith("/"):
                        out[n.replace("\\", "/").lower()] = z.read(n)
        except zipfile.BadZipFile:
            pass
        return out

    def _tree(self):
        if getattr(self, "_tree_cache", None):
            return self._tree_cache
        nodes_b = self.lump(L_NODES)
        nodes = [struct.unpack_from("<iii", nodes_b, i * 32) for i in range(len(nodes_b) // 32)]   # plane, child0, child1
        leaf_size = 56 if self.version == 19 else 32
        leafs_b = self.lump(L_LEAFS)
        leafs = []
        for i in range(len(leafs_b) // leaf_size):
            contents, cluster, area_flags = struct.unpack_from("<ihh", leafs_b, i * leaf_size)
            first, num = struct.unpack_from("<HH", leafs_b, i * leaf_size + 24)
            leafs.append((first, num, area_flags & 0x1FF))
        lb = self.lump(L_LEAFBRUSHES)
        leafbrushes = struct.unpack_from("<%dH" % (len(lb) // 2), lb, 0)
        self._tree_cache = (nodes, leafs, leafbrushes)
        return self._tree_cache

    def leaf_at(self, point):
        nodes, leafs, _ = self._tree()
        n = self.models[0][9]
        while n >= 0:
            pl, c0, c1 = nodes[n]
            nx, ny, nz, d = self.planes[pl]
            n = c0 if nx * point[0] + ny * point[1] + nz * point[2] - d >= 0 else c1
        return -1 - n

    def brush_areas(self):
        """Areas (Source's sealed regions) each world brush touches."""
        nodes, leafs, leafbrushes = self._tree()
        areas = {}
        for first, num, area in leafs:
            for b in leafbrushes[first:first + num]:
                areas.setdefault(b, set()).add(area)
        return areas, leafs

    def model_brushes(self):
        """Brush indices per model (0 = world), from each model's node tree."""
        if self._model_brushes is not None:
            return self._model_brushes
        nodes_b = self.lump(L_NODES)
        nodes = [struct.unpack_from("<iii", nodes_b, i * 32) for i in range(len(nodes_b) // 32)]   # plane, child0, child1
        leaf_size = 56 if self.version == 19 else 32
        leafs_b = self.lump(L_LEAFS)
        leafs = [struct.unpack_from("<HH", leafs_b, i * leaf_size + 24) for i in range(len(leafs_b) // leaf_size)]  # firstleafbrush, num
        lb = self.lump(L_LEAFBRUSHES)
        leafbrushes = struct.unpack_from("<%dH" % (len(lb) // 2), lb, 0)
        result = []
        for m in self.models:
            head = m[9]
            seen = set()
            stack = [head]
            while stack:
                n = stack.pop()
                if n < 0:
                    leaf = -1 - n
                    if leaf < len(leafs):
                        first, num = leafs[leaf]
                        seen.update(leafbrushes[first:first + num])
                    continue
                _, c0, c1 = nodes[n]
                stack.append(c0)
                stack.append(c1)
            result.append(seen)
        # a brush can only belong to one model; brush entities win over the world
        for i in range(1, len(result)):
            result[0] -= result[i]
        self._model_brushes = result
        return result
