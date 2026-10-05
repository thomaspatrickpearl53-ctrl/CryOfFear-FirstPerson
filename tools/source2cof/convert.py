"""Source map (.bsp) -> Cry of Fear map.

    py tools/source2cof.py <source map .bsp> [--name NAME] [--cof <cryoffear folder>]

1. Reads the Source map's brushes (Source keeps them in the .bsp), entities,
   texture placement and embedded files.
2. Converts the textures it uses (from the map and the game's VPKs) to 8-bit
   GoldSrc textures, and writes a Half-Life .map plus a .wad.
3. Compiles it with Valve's Half-Life SDK tools (qcsg, qbsp2, vis, qrad) and
   copies the result into cryoffear/maps.

Lossy by nature: displacement terrain becomes the flat brush underneath it,
props (prop_static etc.) and most game logic (inputs/outputs) are dropped, and
textures drop to 256 colours. Small, mostly-brush maps convert best.
"""
import argparse, ctypes, math, os, re, shutil, struct, subprocess, sys, time, zlib
import numpy as np

from . import bsp as B
from .vpk import GameFiles, search_dirs_for
from . import vtf, wad

STEAM = r"C:\Program Files (x86)\Steam\steamapps\common"
DEF_COF = os.path.join(STEAM, r"Cry of Fear\cryoffear")
DEF_TOOLS = os.path.join(STEAM, r"Half-Life SDK\Map Tools")
DEF_RAD = os.path.join(STEAM, r"Half-Life SDK\Hammer Editor\tools\lights.rad")
LIMIT = 4096.0                       # GoldSrc's world bounds


def log(*a):
    print(*a, flush=True)


def short_path(p):
    """8.3 path without spaces: the 1998 compilers can't handle spaces."""
    os.makedirs(p, exist_ok=True)
    buf = ctypes.create_unicode_buffer(1024)
    if ctypes.windll.kernel32.GetShortPathNameW(p, buf, 1024):
        return buf.value
    return p


# ---------------------------------------------------------------------------
# Materials
# ---------------------------------------------------------------------------

def parse_keyvalues(text):
    """Tiny KeyValues reader -> (root name, nested dict with lower-case keys)."""
    toks = re.findall(r'"([^"]*)"|(\{)|(\})|([^\s{}"]+)', re.sub(r"//[^\n]*", "", text))
    toks = [t[0] if t[0] or (not t[1] and not t[2] and not t[3]) else (t[1] or t[2] or t[3]) for t in toks]
    pos = 0

    def block():
        nonlocal pos
        out = {}
        while pos < len(toks):
            t = toks[pos]
            if t == "}":
                pos += 1
                return out
            key = t.lower()
            pos += 1
            if pos >= len(toks):
                break
            if toks[pos] == "{":
                pos += 1
                out[key] = block()
            else:
                out[key] = toks[pos]
                pos += 1
        return out

    if not toks:
        return "", {}
    name = toks[0]
    pos = 1
    if pos < len(toks) and toks[pos] == "{":
        pos += 1
        return name.lower(), block()
    return name.lower(), {}


class Materials:
    TOOLS = {
        "tools/toolsnodraw": "nodraw", "tools/toolsclip": "clip", "tools/toolsplayerclip": "clip",
        "tools/toolsskybox": "sky", "tools/toolsskybox2d": "sky", "tools/toolstrigger": "trigger",
        "tools/toolsblack": "black", "tools/toolsorigin": "origin", "tools/toolsinvisibleladder": "ladder",
    }

    def __init__(self, files):
        self.files = files
        self.cache = {}

    def vmt(self, name, depth=0):
        data = self.files.read("materials/%s.vmt" % name.lower().replace("\\", "/"))
        if data is None or depth > 4:
            return None, {}
        shader, keys = parse_keyvalues(data.decode("latin-1", "replace"))
        if shader == "patch" and "include" in keys:
            inc = keys["include"].lower().replace("\\", "/")
            inc = inc[len("materials/"):] if inc.startswith("materials/") else inc
            inc = inc[:-4] if inc.endswith(".vmt") else inc
            base_shader, base = self.vmt(inc, depth + 1)
            merged = dict(base)
            for sect in ("insert", "replace"):
                if isinstance(keys.get(sect), dict):
                    merged.update({k: v for k, v in keys[sect].items() if not isinstance(v, dict)})
            return base_shader, merged
        return shader, keys

    def info(self, name):
        """-> dict(kind, rgba or None, masked, translucent, color)."""
        key = name.lower().replace("\\", "/")
        if key in self.cache:
            return self.cache[key]
        r = {"kind": "normal", "rgba": None, "masked": False, "translucent": False, "color": None}
        if key.startswith("tools/"):
            r["kind"] = self.TOOLS.get(key, "drop")
        else:
            shader, keys = self.vmt(key)
            flat = {k: v for k, v in keys.items() if not isinstance(v, dict)}
            if shader in ("water", "lightmappedgeneric_water") or "$fogcolor" in flat and "water" in key:
                r["kind"] = "water"
                m = re.findall(r"[-\d.]+", flat.get("$fogcolor", ""))
                if len(m) >= 3:
                    c = [float(x) for x in m[:3]]
                    if max(c) <= 1.0:
                        c = [x * 255 for x in c]
                    r["color"] = [min(255, max(30, int(x))) for x in c]
                else:
                    r["color"] = [40, 60, 70]
            elif shader in ("sky", "unlitgeneric") and key.startswith("skybox/"):
                r["kind"] = "sky"
            else:
                r["masked"] = flat.get("$alphatest", "0") not in ("0", "")
                r["translucent"] = flat.get("$translucent", "0") not in ("0", "") and not r["masked"]
                base = flat.get("$basetexture") or flat.get("$basetexture2")
                if base:
                    base = base.lower().replace("\\", "/")
                    base = base[len("materials/"):] if base.startswith("materials/") else base
                    base = base[:-4] if base.endswith(".vtf") else base
                    data = self.files.read("materials/%s.vtf" % base)
                    if data:
                        try:
                            r["rgba"], _ = vtf.decode(data)
                        except (ValueError, struct.error, IndexError) as e:
                            log("  texture %s: %s" % (base, e))
        self.cache[key] = r
        return r


# ---------------------------------------------------------------------------
# Geometry
# ---------------------------------------------------------------------------

def base_winding(n, d):
    n = np.asarray(n, np.float64)
    ax = np.argmax(np.abs(n))
    up = np.array([0.0, 0.0, 1.0]) if ax != 2 else np.array([1.0, 0.0, 0.0])
    up -= n * np.dot(up, n)
    up /= np.linalg.norm(up)
    right = np.cross(up, n)
    org = n * d
    s = 65536.0
    return [org - right * s + up * s, org + right * s + up * s, org + right * s - up * s, org - right * s - up * s]


def clip(w, n, d, eps=0.01):
    """Keep the part of winding w behind plane (n, d)."""
    out = []
    k = len(w)
    for i in range(k):
        p, q = w[i], w[(i + 1) % k]
        dp, dq = np.dot(p, n) - d, np.dot(q, n) - d
        if dp <= eps:
            out.append(p)
        if (dp > eps and dq < -eps) or (dp < -eps and dq > eps):
            t = dp / (dp - dq)
            out.append(p + (q - p) * t)
    return out


def plane_points(w, n, d):
    """Three integer points on the plane, ordered so the compiler gets the outward normal."""
    pts = np.array(w)
    ri = np.round(pts)
    if np.all(np.abs(pts - ri) < 0.02) and len(pts) >= 3:
        cand = ri
    else:
        # Off-grid face: widely spaced lattice points, solved along the dominant axis.
        ax = int(np.argmax(np.abs(n)))
        u, v = [i for i in range(3) if i != ax]
        c = pts.mean(0)
        cand = []
        for du, dv in ((0, 0), (2048, 0), (0, 2048)):
            p = np.zeros(3)
            p[u], p[v] = round(c[u]) + du, round(c[v]) + dv
            p[ax] = round((d - n[u] * p[u] - n[v] * p[v]) / n[ax])
            cand.append(p)
        cand = np.array(cand)
    # the three points spanning the largest triangle
    best, tri = -1.0, None
    m = len(cand)
    for i in range(m):
        for j in range(i + 1, m):
            for k in range(j + 1, m):
                a = np.linalg.norm(np.cross(cand[j] - cand[i], cand[k] - cand[i]))
                if a > best:
                    best, tri = a, (cand[i], cand[j], cand[k])
    if tri is None or best < 1e-3:
        return None
    p0, p1, p2 = tri
    nrm = np.cross(p0 - p1, p2 - p1)
    if np.dot(nrm, n) < 0:
        p0, p2 = p2, p0
    return p0, p1, p2


# ---------------------------------------------------------------------------
# Conversion
# ---------------------------------------------------------------------------

class Converter:
    def __init__(self, bsp_path, name, maxtex, light_scale):
        self.path = bsp_path
        self.name = name
        self.maxtex = maxtex
        self.light_scale = light_scale
        log("reading %s" % bsp_path)
        self.bsp = B.Bsp(bsp_path)
        pak = self.bsp.pakfile()
        self.files = GameFiles(search_dirs_for(bsp_path), extra=pak)
        log("  VBSP v%d, %d brushes, %d entities, %d embedded files, %d VPKs searched" % (
            self.bsp.version, len(self.bsp.brushes), len(self.bsp.entities), len(pak), len(self.files.vpks)))
        self.mats = Materials(self.files)
        self.tex_names = {}          # material -> goldsrc name
        self.tex_data = {}           # goldsrc name -> (miptex bytes, (w, h))
        self.tex_scale = {}          # goldsrc name -> (sx, sy) new size / Source size
        self.T = np.zeros(3)         # whole-map move that centres it in GoldSrc's +-4096 box
        self.sky_area = None         # Source area of the 3D skybox, removed
        self.clipped_at_edge = False
        self.box_lo = np.full(3, -(LIMIT - 16))   # everything is cut off at this box (set by centre())
        self.box_hi = np.full(3, LIMIT - 16)
        self.stats = {"brushes": 0, "dropped": 0, "outside": 0, "skybox": 0,
                      "displacement_faces": len(self.bsp.lump(B.L_DISPINFO)) // 176}

    # --- textures -------------------------------------------------------------

    def unique(self, base):
        base = re.sub(r"[^a-z0-9_]", "", base.lower()) or "tex"
        if base[0] in "+-!{~" or base.startswith(("sky", "clip", "origin", "aaatrigger", "scroll", "null", "hint", "skip")):
            base = "x" + base
        name = base[:15]
        i = 1
        while name.upper() in (n.upper() for n in self.tex_data):
            suffix = "%d" % i
            name = base[:15 - len(suffix)] + suffix
            i += 1
        return name

    def special(self, name, color):
        if name not in self.tex_data:
            size = (16, 16)
            self.tex_data[name] = (wad.miptex(name, None, size, solid_color=color), size)
            self.tex_scale[name] = (1.0, 1.0)
        return name

    def texture_for(self, texinfo_index):
        """-> (goldsrc texture name, kind, material info) for a brush side."""
        if texinfo_index < 0:
            return self.special("nodraw", (0, 0, 0)), "nodraw", None
        s, t, flags, tdi = self.bsp.texinfo[texinfo_index]
        mat, tw, th = self.bsp.texdata[tdi] if 0 <= tdi < len(self.bsp.texdata) else ("", 64, 64)
        info = self.mats.info(mat)
        kind = info["kind"]
        if flags & (B.SURF_SKY | B.SURF_SKY2D):
            kind = "sky"
        elif flags & B.SURF_NODRAW and kind == "normal":
            kind = "nodraw"
        if kind == "sky":
            return self.special("sky", (60, 70, 90)), kind, info
        if kind in ("nodraw", "black"):
            return self.special("nodraw" if kind == "nodraw" else "black", (0, 0, 0)), kind, info
        if kind == "clip":
            return self.special("clip", (128, 0, 128)), kind, info
        if kind == "origin":
            return self.special("origin", (255, 128, 0)), kind, info
        if kind in ("trigger", "ladder"):
            return self.special("aaatrigger", (255, 128, 0)), kind, info
        if kind == "drop":
            return None, kind, info
        key = mat.lower()
        if key in self.tex_names:
            return self.tex_names[key], kind, info
        if kind == "water":
            base = re.sub(r"[^a-z0-9_]", "", key.split("/")[-1])[:13] or "water"
            name = "!" + base
            i = 1
            while name.upper() in (n.upper() for n in self.tex_data):
                name = ("!" + base)[:15 - len(str(i))] + str(i)
                i += 1
            size = (64, 64)
            self.tex_data[name] = (wad.miptex(name, None, size, solid_color=info["color"]), size)
            self.tex_scale[name] = (size[0] / max(1, tw), size[1] / max(1, th))
        else:
            rgba = info["rgba"]
            if rgba is None:
                log("  missing texture for material %s (grey placeholder)" % mat)
                rgba = np.full((64, 64, 4), 128, np.uint8)
                rgba[::16, :, :3] = 90
                rgba[:, ::16, :3] = 90
            masked = info["masked"]
            base = key.split("/")[-1]
            name = self.unique(base)
            if masked:
                name = "{" + name[:14]
            size = wad.fit_size(rgba.shape[1], rgba.shape[0], self.maxtex)
            self.tex_data[name] = (wad.miptex(name, rgba, size, masked=masked), size)
            self.tex_scale[name] = (size[0] / max(1, tw), size[1] / max(1, th))
        self.tex_names[key] = name
        return name, kind, info

    # --- brushes ---------------------------------------------------------------

    def brush_faces(self, bi, offset):
        """-> (list of (points, texture, s, t, kind)), contents, kinds) or None."""
        first, num, contents = self.bsp.brushes[bi]
        sides = self.bsp.sides[first:first + num]
        planes = []
        for plane_i, tex_i, disp, bevel in sides:
            if bevel:
                continue
            nx, ny, nz, d = self.bsp.planes[plane_i]
            n = np.array([nx, ny, nz], np.float64)
            d = d + float(np.dot(n, offset + self.T))
            planes.append((n, d, tex_i, disp))
        # GoldSrc's world ends at +-4096: cut brushes off there (sealed by a nodraw face)
        # instead of dropping them, so huge outer walls still close the map.
        for axis in range(3):
            for sign in (1.0, -1.0):
                n = np.zeros(3)
                n[axis] = sign
                planes.append((n, self.box_hi[axis] if sign > 0 else -self.box_lo[axis], -3, -1))
        faces = []
        for i, (n, d, tex_i, disp) in enumerate(planes):
            w = base_winding(n, d)
            for j, (n2, d2, _, _) in enumerate(planes):
                if j != i and w:
                    w = clip(w, n2, d2)
            if len(w) < 3:
                continue
            pts = plane_points(w, n, d)
            if pts is None:
                continue
            if tex_i == -3:
                self.clipped_at_edge = True
                faces.append((pts, self.special("nodraw", (0, 0, 0)), -1, "nodraw", None, w))
                continue
            tex, kind, info = self.texture_for(tex_i)
            faces.append((pts, tex, tex_i, kind, info, w))
        if len(faces) < 4:
            return None
        return self.agree(faces), contents

    def agree(self, faces):
        """GoldSrc takes a brush's contents (sky, clip, water...) from its textures,
        and every side must agree. Source mixes them freely (one sky side and
        nodraw on the rest, a clip side on a wall...)."""
        kinds = [f[3] for f in faces]
        if "sky" in kinds:
            if all(k in ("sky", "nodraw", "black") for k in kinds):
                sky = self.special("sky", (60, 70, 90))
                return [(p, sky, -1, "sky", info, w) for p, tex, ti, k, info, w in faces]
            nod = self.special("nodraw", (0, 0, 0))
            faces = [(p, nod, -1, "nodraw", info, w) if k == "sky" else (p, tex, ti, k, info, w)
                     for p, tex, ti, k, info, w in faces]
            kinds = [f[3] for f in faces]
        special = ("clip", "origin", "trigger", "ladder")
        if any(k in special for k in kinds) and not all(k in special + ("nodraw", "black", "drop") for k in kinds):
            nod = self.special("nodraw", (0, 0, 0))
            faces = [(p, nod, -1, "nodraw", info, w) if k in special else (p, tex, ti, k, info, w)
                     for p, tex, ti, k, info, w in faces]
        return faces

    def side_line(self, pts, tex, tex_i):
        p = " ".join("( %d %d %d )" % tuple(int(round(c)) for c in q) for q in pts)
        n = np.cross(pts[0] - pts[1], pts[2] - pts[1])
        n = n / (np.linalg.norm(n) or 1)
        ax = int(np.argmax(np.abs(n)))
        fallback_u = np.array([0.0, 1, 0] if ax == 0 else [1.0, 0, 0])
        fallback_v = np.array([0.0, 0, -1] if ax != 2 else [0.0, -1, 0])
        if tex_i < 0 or tex not in self.tex_scale or self.bsp.texinfo[tex_i][3] < 0:
            return "%s %s [ %d %d %d 0 ] [ %d %d %d 0 ] 0 1 1" % (p, tex.upper(), *fallback_u, *fallback_v)
        s, t, flags, tdi = self.bsp.texinfo[tex_i]
        sx, sy = self.tex_scale[tex]
        out = []
        for vec, f in ((s, sx), (t, sy)):
            axis = np.array(vec[:3], np.float64)
            length = np.linalg.norm(axis)
            if length < 1e-9:
                axis, length = np.array([1.0, 0, 0]), 1.0
            out.append((axis / length, 1.0 / (length * f), vec[3] * f))
        (ua, us, ush), (va, vs, vsh) = out
        # The whole map was moved by T: move the texture with it.
        ush -= float(np.dot(self.T, ua)) / us
        vsh -= float(np.dot(self.T, va)) / vs
        # Source keeps whatever axes a hidden side had; GoldSrc's lighting refuses
        # axes that lie edge-on to the face. Project those straight on instead.
        cross = np.cross(ua, va)
        if abs(np.dot(cross / (np.linalg.norm(cross) or 1), n)) < 0.1:
            ua, va = fallback_u, fallback_v
        return "%s %s [ %.6g %.6g %.6g %.6g ] [ %.6g %.6g %.6g %.6g ] 0 %.6g %.6g" % (
            p, tex.upper(), ua[0], ua[1], ua[2], ush, va[0], va[1], va[2], vsh, us, vs)

    def brush_text(self, faces, force_tex=None):
        lines = []
        for pts, tex, tex_i, kind, info, w in faces:
            t = force_tex or tex or self.special("nodraw", (0, 0, 0))
            lines.append(self.side_line(pts, t, tex_i if t == tex else -1))
        return "{\n" + "\n".join(lines) + "\n}\n"

    def classify(self, faces, contents):
        """-> 'drop', 'clip', 'water', 'ladder', 'trigger', 'masked', 'glass' or 'solid'."""
        kinds = [f[3] for f in faces]
        visible = [k for k in kinds if k not in ("nodraw", "drop")]
        if contents & (B.CONTENTS_AREAPORTAL | B.CONTENTS_ORIGIN):
            return "drop"
        if contents & (B.CONTENTS_WATER | B.CONTENTS_SLIME) or "water" in kinds:
            return "water"
        if contents & B.CONTENTS_LADDER or "ladder" in kinds:
            return "ladder"
        if not contents & (B.CONTENTS_SOLID | B.CONTENTS_WINDOW | B.CONTENTS_GRATE | B.CONTENTS_MOVEABLE):
            if contents & B.CONTENTS_PLAYERCLIP:
                return "clip"
            if "trigger" in kinds:
                return "trigger"
            if not visible:
                return "drop"
        if visible and all(k == "clip" for k in visible):
            return "clip"
        if visible and all(k == "trigger" for k in visible):
            return "trigger"
        if not visible:
            return "drop" if "drop" in kinds else "solid"          # all-nodraw blockers stay solid
        if any(f[4] and f[4]["masked"] for f in faces):
            return "masked"
        if any(f[4] and f[4]["translucent"] for f in faces):
            return "glass"
        return "solid"

    # --- entities --------------------------------------------------------------

    BRUSH_CLASSES = {
        "func_door": "func_door", "func_door_rotating": "func_door_rotating", "func_button": "func_button",
        "func_breakable": "func_breakable", "func_breakable_surf": "func_breakable", "func_wall": "func_wall",
        "func_brush": "func_wall", "func_illusionary": "func_illusionary", "func_wall_toggle": "func_wall_toggle",
        "func_rotating": "func_rotating", "func_movelinear": "func_wall", "func_physbox": "func_wall",
        "func_lod": "func_wall", "func_tracktrain": "func_wall", "func_train": "func_wall",
        "func_conveyor": "func_conveyor", "func_rot_button": "func_rot_button", "func_pendulum": "func_wall",
        "func_monitor": "func_wall", "func_reflective_glass": "func_wall", "func_water_analog": "func_wall",
        "trigger_teleport": "trigger_teleport", "trigger_hurt": "trigger_hurt", "trigger_push": "trigger_push",
        "func_ladder": "func_ladder", "func_detail": None,
    }
    KEEP_KEYS = {
        "func_door": ("targetname", "speed", "wait", "lip", "angles", "spawnflags", "dmg", "health"),
        "func_door_rotating": ("targetname", "speed", "wait", "distance", "angles", "spawnflags", "dmg"),
        "func_button": ("targetname", "target", "speed", "wait", "lip", "angles", "spawnflags", "health"),
        "func_breakable": ("targetname", "health", "material", "spawnflags", "explodemagnitude"),
        "func_rotating": ("targetname", "speed", "spawnflags", "angles"),
        "trigger_teleport": ("targetname", "target", "spawnflags"),
        "trigger_hurt": ("targetname", "damage", "dmg", "spawnflags", "damagetype"),
        "trigger_push": ("targetname", "speed", "pushdir", "spawnflags"),
    }

    def convert(self):
        ents = []
        world_brushes = []
        extra = {"masked": [], "glass": [], "ladder": []}
        model_brushes = self.bsp.model_brushes()
        spawn_done = False
        out_points = []
        skyname = None
        self.find_skybox()
        self.centre(model_brushes)

        for e in self.bsp.entities:
            kv = dict(e)
            cls = kv.get("classname", "")
            if cls == "worldspawn":
                skyname = kv.get("skyname")
                continue
            if self.in_skybox(kv):
                self.stats["skybox"] += 1
                continue
            model = kv.get("model", "")
            if model.startswith("*"):
                mi = int(model[1:])
                if mi >= len(model_brushes):
                    continue
                target = self.BRUSH_CLASSES.get(cls, "func_wall")
                offset = np.zeros(3)
                if target is None:                         # func_detail: plain world brushes
                    world_brushes.extend((bi, offset) for bi in model_brushes[mi])
                    continue
                origin = np.array((kv.get("origin", "0 0 0").split() + ["0", "0", "0"])[:3], np.float64)
                offset = self.model_offset(mi, origin)
                brushes = []
                for bi in model_brushes[mi]:
                    r = self.brush_faces(bi, offset)
                    if not r:
                        continue
                    faces, contents = r
                    kind = self.classify(faces, contents)
                    if kind == "drop" or (kind == "trigger" and not target.startswith("trigger")):
                        self.stats["dropped"] += 1
                        continue
                    force = "aaatrigger" if target.startswith("trigger") or target == "func_ladder" else None
                    brushes.append(self.brush_text(faces, force_tex=self.special("aaatrigger", (255, 128, 0)) if force else None))
                    out_points.extend(np.array(f[5]) for f in faces)
                if not brushes:
                    continue
                keys = [("classname", target)]
                for k in self.KEEP_KEYS.get(target, ("targetname",)):
                    if k in kv:
                        keys.append((("dmg" if k == "damage" else k), kv[k]))
                rm = int(kv.get("rendermode", "0") or 0)
                if rm or any("{" in b for b in brushes):
                    keys += [("rendermode", "4" if any("{" in b for b in brushes) else str(min(rm, 5))),
                             ("renderamt", kv.get("renderamt", "255"))]
                ents.append((keys, brushes))
                continue
            pe = self.point_entity(cls, kv, spawn_done)
            if pe:
                if pe[0][1] == "info_player_start":
                    spawn_done = True
                ents.append((pe, []))

        for bi in model_brushes[0]:
            world_brushes.append((bi, np.zeros(3)))
        world_text = []
        for bi, offset in world_brushes:
            if self.skybox_brush(bi):
                self.stats["skybox"] += 1
                continue
            r = self.brush_faces(bi, offset)
            if not r:
                continue
            faces, contents = r
            kind = self.classify(faces, contents)
            if kind in ("drop", "trigger"):
                self.stats["dropped"] += 1
                continue
            pts = np.concatenate([np.array(f[5]) for f in faces])
            if np.any(np.abs(pts) > LIMIT - 1):
                self.stats["outside"] += 1
                continue
            out_points.append(pts)
            if kind == "clip":
                text = self.brush_text(faces, force_tex=self.special("clip", (128, 0, 128)))
            elif kind == "water":
                wt = next((f[1] for f in faces if f[1] and f[1].startswith("!")), None) or \
                    self.special("!water", (40, 60, 70))
                text = self.brush_text(faces, force_tex=wt)
            elif kind in extra:
                extra[kind].append(self.brush_text(faces, force_tex=self.special("aaatrigger", (255, 128, 0)) if kind == "ladder" else None))
                continue
            else:
                text = self.brush_text(faces)
            world_text.append(text)
            self.stats["brushes"] += 1

        world_text.extend(self.sky_shell())
        if extra["masked"]:
            ents.append(([("classname", "func_wall"), ("rendermode", "4"), ("renderamt", "255")], extra["masked"]))
        if extra["glass"]:
            ents.append(([("classname", "func_wall"), ("rendermode", "2"), ("renderamt", "130")], extra["glass"]))
        if extra["ladder"]:
            ents.append(([("classname", "func_ladder")], extra["ladder"]))
        if not spawn_done:
            log("  WARNING: no player spawn found; add one with 'origin' near the map's centre")
        self.skyname = skyname
        self.world_text = world_text
        self.ents = ents
        if out_points:
            allp = np.concatenate([np.atleast_2d(p) for p in out_points])
            self.bounds = (allp.min(0), allp.max(0))
        else:
            self.bounds = (np.zeros(3), np.zeros(3))

    def sky_shell(self):
        """Six thin sky brushes just outside the cut-off box: whatever was cut off
        there (or never sealed in GoldSrc's terms) can't leak to the void."""
        sky = self.special("sky", (60, 70, 90)).upper()
        lo, hi, t = self.box_lo, self.box_hi, 16.0
        out = []

        def box(a, b):
            x0, y0, z0 = a
            x1, y1, z1 = b
            faces = [
                ((x0, 0, 0), (x0, 1, 0), (x0, 0, 1), "0 1 0", "0 0 -1"), ((x1, 0, 0), (x1, 0, 1), (x1, 1, 0), "0 1 0", "0 0 -1"),
                ((0, y0, 0), (0, y0, 1), (1, y0, 0), "1 0 0", "0 0 -1"), ((0, y1, 0), (1, y1, 0), (0, y1, 1), "1 0 0", "0 0 -1"),
                ((0, 0, z0), (1, 0, z0), (0, 1, z0), "1 0 0", "0 -1 0"), ((0, 0, z1), (0, 1, z1), (1, 0, z1), "1 0 0", "0 -1 0")]
            return "{\n" + "".join("( %d %d %d ) ( %d %d %d ) ( %d %d %d ) %s [ %s 0 ] [ %s 0 ] 0 1 1\n" % (
                *p0, *p1, *p2, sky, u, v) for p0, p1, p2, u, v in faces) + "}\n"

        L, H = lo - t, hi + t
        for axis in range(3):
            for side in (0, 1):
                a, b = L.copy(), H.copy()
                if side == 0:
                    b[axis] = lo[axis]
                else:
                    a[axis] = hi[axis]
                out.append(box(a, b))
        return out

    def find_skybox(self):
        """Source's 3D skybox is a small sealed room drawn scaled-up around the map;
        GoldSrc has no such thing, so its brushes and entities are left out."""
        cam = next((dict(e) for e in self.bsp.entities if dict(e).get("classname") == "sky_camera"), None)
        if not cam or "origin" not in cam:
            return
        self._brush_areas, leafs = self.bsp.brush_areas()
        leaf = self.bsp.leaf_at([float(x) for x in cam["origin"].split()[:3]])
        if 0 <= leaf < len(leafs) and leafs[leaf][2] > 0:
            self.sky_area = leafs[leaf][2]

    def skybox_brush(self, bi):
        if self.sky_area is None:
            return False
        areas = self._brush_areas.get(bi, set()) - {0}
        return bool(areas) and areas <= {self.sky_area}

    def in_skybox(self, kv):
        if self.sky_area is None or "origin" not in kv:
            return False
        if kv.get("model", "").startswith("*"):
            mb = self.bsp.model_brushes()[int(kv["model"][1:])] if kv["model"][1:].isdigit() else set()
            return bool(mb) and all(self.skybox_brush(b) for b in mb)
        leaf = self.bsp.leaf_at([float(x) for x in (kv["origin"].split() + ["0", "0", "0"])[:3]])
        leafs = self.bsp._tree()[1]
        return 0 <= leaf < len(leafs) and leafs[leaf][2] == self.sky_area

    def centre(self, model_brushes):
        """Move the map so the playable part's bounding box is centred on the origin."""
        lo, hi = np.full(3, 1e9), np.full(3, -1e9)
        for bi in model_brushes[0]:
            if self.skybox_brush(bi):
                continue
            first, num, contents = self.bsp.brushes[bi]
            sides = [s for s in self.bsp.sides[first:first + num] if not s[3]]
            # sky and nodraw shells around the map don't count towards its size
            if all(s[1] < 0 or self.bsp.texinfo[s[1]][2] & (B.SURF_SKY | B.SURF_SKY2D | B.SURF_NODRAW) for s in sides):
                continue
            planes = [self.bsp.planes[p] for p, tex, disp, bevel in sides]
            for i, (nx, ny, nz, d) in enumerate(planes):
                n = np.array([nx, ny, nz])
                w = base_winding(n, d)
                for j, (mx, my, mz, e) in enumerate(planes):
                    if j != i and w:
                        w = clip(w, np.array([mx, my, mz]), e)
                if len(w) >= 3:
                    w = np.array(w)
                    lo, hi = np.minimum(lo, w.min(0)), np.maximum(hi, w.max(0))
        if lo[0] > hi[0]:
            return
        self.T = -np.round((lo + hi) / 2 / 16.0) * 16.0
        if np.any(np.abs(self.T) > 0):
            log("  moving the map by (%d %d %d) to centre it" % tuple(self.T))
        # Cut everything (sky shells included) off a little outside the playable part.
        margin = 512.0
        self.box_lo = np.maximum(np.floor((lo + self.T - margin) / 16) * 16, -(LIMIT - 16))
        self.box_hi = np.minimum(np.ceil((hi + self.T + margin) / 16) * 16, LIMIT - 16)
        if np.any(hi - lo > 2 * (LIMIT - 16)):
            log("  WARNING: the playable part is %.0f x %.0f x %.0f, bigger than GoldSrc's 8192 box; the edges are cut off" % tuple(hi - lo))

    def model_offset(self, mi, origin):
        """Brushes of a brush entity are stored around its origin when it has one."""
        if not np.any(origin):
            return np.zeros(3)
        m = self.bsp.models[mi]
        mins, maxs = np.array(m[0:3]), np.array(m[3:6])
        # If the model's bounds already contain the origin's area, the brushes are in world space.
        centre = (mins + maxs) / 2
        if np.all(np.abs(centre) < np.abs(centre - origin) - 1):
            return origin
        return np.zeros(3)

    def point_entity(self, cls, kv, spawn_done):
        o = kv.get("origin")
        if o is None:
            return None
        try:
            o = "%g %g %g" % tuple(np.array((o.split() + ["0", "0", "0"])[:3], np.float64) + self.T)
        except ValueError:
            return None
        angles = kv.get("angles", "0 0 0")
        spawn_classes = ("info_player_start", "info_player_deathmatch", "info_player_terrorist",
                         "info_player_counterterrorist", "info_player_combine", "info_player_rebel",
                         "info_player_teamspawn")
        if cls in spawn_classes:
            return [("classname", "info_player_deathmatch" if spawn_done else "info_player_start"),
                    ("origin", o), ("angles", angles)]
        if cls in ("light", "light_spot", "light_environment"):
            light = kv.get("_light", "255 255 255 200").split()
            while len(light) < 4:
                light.append("200")
            try:
                light[3] = "%d" % max(1, round(float(light[3]) * self.light_scale))
            except ValueError:
                pass
            keys = [("classname", cls), ("origin", o), ("_light", " ".join(light[:4]))]
            for k in ("style", "pitch", "targetname", "spawnflags"):
                if k in kv:
                    keys.append((k, kv[k]))
            if "angles" in kv:
                keys.append(("angles", kv["angles"]))
            if cls == "light_spot":
                keys.append(("_cone", kv.get("_inner_cone", "30")))
                keys.append(("_cone2", kv.get("_cone", "45")))
            return keys
        if cls == "info_teleport_destination":
            return [("classname", cls), ("origin", o), ("angles", angles), ("targetname", kv.get("targetname", ""))]
        return None

    # --- output ----------------------------------------------------------------

    def write(self, work):
        # The real textures, and 16x16 stand-ins in their average colour for the
        # compile: the 1998 qcsg copies every texture's pixels into the map and
        # stops at 2 MB. patch_textures() swaps the real ones in afterwards.
        wad.write_wad(os.path.join(work, self.name + ".wad"), [(n, mt) for n, (mt, size) in self.tex_data.items()])
        stub_path = os.path.join(work, self.name + "_stub.wad")
        wad.write_wad(stub_path, [(n, wad.stub(n, mt)) for n, (mt, size) in self.tex_data.items()])
        lines = ['{', '"classname" "worldspawn"', '"mapversion" "220"', '"wad" "%s"' % stub_path.replace("\\", "/")]
        if self.skyname:
            lines.append('"skyname" "%s"' % self.skyname)
        lines.append('"message" "%s (converted from Source)"' % self.name)
        text = "\n".join(lines) + "\n" + "".join(self.world_text) + "}\n"
        for keys, brushes in self.ents:
            text += "{\n" + "".join('"%s" "%s"\n' % (k, v) for k, v in keys) + "".join(brushes) + "}\n"
        map_path = os.path.join(work, self.name + ".map")
        with open(map_path, "w", newline="\n") as f:
            f.write(text)
        return map_path

    def sky(self, cof_dir):
        """Source sky boxes are six VTFs; GoldSrc wants gfx/env/<name><side>.tga."""
        if not self.skyname:
            return
        env = os.path.join(cof_dir, "gfx", "env")
        os.makedirs(env, exist_ok=True)
        from PIL import Image
        done = 0
        for side in ("rt", "lf", "ft", "bk", "up", "dn"):
            mat = "skybox/%s%s" % (self.skyname, side)
            shader, keys = self.mats.vmt(mat)
            base = keys.get("$basetexture", mat) if isinstance(keys, dict) else mat
            base = str(base).lower().replace("\\", "/")
            data = self.files.read("materials/%s.vtf" % base)
            if not data:
                continue
            try:
                rgba, _ = vtf.decode(data)
            except (ValueError, struct.error, IndexError):
                continue
            img = Image.fromarray(rgba[:, :, :3].copy(), "RGB").resize((256, 256), Image.LANCZOS)
            img.save(os.path.join(env, "%s%s.tga" % (self.skyname, side)))
            done += 1
        log("  sky %s: %d/6 sides written to gfx/env" % (self.skyname, done))


def patch_textures(bsp_path, tex_data, name, embed):
    """Swap the compile's stand-in textures for the real ones: inside the map when
    they fit, otherwise name + real size only (the engine then loads the pixels
    from <name>.wad in the game folder, like Half-Life's own maps)."""
    d = bytearray(open(bsp_path, "rb").read())
    lumps = [list(struct.unpack_from("<ii", d, 4 + i * 8)) for i in range(15)]
    tex = bytes(d[lumps[2][0]:lumps[2][0] + lumps[2][1]])
    real = {n.upper(): mt for n, (mt, size) in tex_data.items()}
    count = struct.unpack_from("<i", tex, 0)[0]
    entries = []
    for i in range(count):
        o = struct.unpack_from("<i", tex, 4 + i * 4)[0]
        tname = tex[o:o + 16].split(b"\0")[0].decode("latin-1").upper()
        mt = real.get(tname)
        if mt is None:
            entries.append(tex[o:o + 40])
        elif embed:
            entries.append(mt)
        else:
            entries.append(mt[:24] + b"\0" * 16)            # name, width, height; no pixels
    new_tex = wad.texture_lump(entries)
    ents = bytes(d[lumps[0][0]:lumps[0][0] + lumps[0][1]]).decode("latin-1")
    ents = re.sub(r'"wad" "[^"]*"', '"wad" "%s"' % ("" if embed else "\\\\cryoffear\\\\%s.wad" % name), ents, count=1)
    new_ents = ents.encode("latin-1")
    if not new_ents.endswith(b"\0"):
        new_ents += b"\0"
    blobs = [bytes(d[o:o + l]) for o, l in lumps]
    blobs[0], blobs[2] = new_ents, new_tex
    order = sorted(range(15), key=lambda i: lumps[i][0])
    out = bytearray(d[:4] + b"\0" * 120)
    for i in order:
        while len(out) % 4:
            out.append(0)
        struct.pack_into("<ii", out, 4 + i * 8, len(out), len(blobs[i]))
        out += blobs[i]
    with open(bsp_path, "wb") as f:
        f.write(out)


def compile_map(map_path, tools, rad_file, fast, log_path, embed=True):
    work = os.path.dirname(map_path)
    base = os.path.splitext(os.path.basename(map_path))[0]
    tdir = short_path(os.path.join(work, "tools"))
    for exe in ("qcsg.exe", "qbsp2.exe", "vis.exe", "qrad.exe"):
        shutil.copy2(os.path.join(tools, exe), os.path.join(tdir, exe))
    if os.path.isfile(rad_file):
        shutil.copy2(rad_file, os.path.join(tdir, "lights.rad"))
    else:
        open(os.path.join(tdir, "lights.rad"), "w").close()
    steps = [("qcsg.exe", ["-nowadtextures"] if embed else []), ("qbsp2.exe", []), ("vis.exe", ["-fast"] if fast else []), ("qrad.exe", [])]
    with open(log_path, "w") as lf:
        for exe, args in steps:
            log("  %s ..." % exe[:-4])
            t = time.time()
            p = subprocess.run([os.path.join(tdir, exe)] + args + [base], cwd=work, capture_output=True, text=True, errors="replace")
            out = p.stdout + p.stderr
            lf.write("===== %s %s\n%s\n" % (exe, " ".join(args), out))
            warn = [l for l in out.splitlines() if re.search(r"error|WARNING|leak|exceeded|MAX_", l, re.I)]
            for l in warn[:8]:
                log("    " + l.strip())
            if len(warn) > 8:
                log("    ... %d more lines in %s" % (len(warn) - 8, log_path))
            log("    %.0f s" % (time.time() - t))
            if exe == "qbsp2.exe" and os.path.isfile(os.path.join(work, base + ".pts")) and os.path.getsize(os.path.join(work, base + ".pts")):
                log("    the map LEAKS (a hole to the void); vis and lighting will be poor")
            if exe == "qrad.exe" and "MAX_PATCHES" in out:
                for chop in ("128", "256", "512"):
                    log("    too many light patches; retrying with -chop %s" % chop)
                    p = subprocess.run([os.path.join(tdir, exe), "-chop", chop, base], cwd=work,
                                       capture_output=True, text=True, errors="replace")
                    out = p.stdout + p.stderr
                    lf.write("===== %s -chop %s\n%s\n" % (exe, chop, out))
                    if "ERROR" not in out:
                        break
            if "ERROR" in out and exe == "qrad.exe":
                log("    lighting failed (the map is too big for the 2000-era qrad); it will be full-bright")
                continue
            if "ERROR" in out:
                log("    %s failed" % exe[:-4])
                if "MAX_LEAF_FACES" in out or "MAX_MAP" in out:
                    log("    the map is too big or detailed for the 1998 compilers")
                return False
    return os.path.isfile(os.path.join(work, base + ".bsp"))


def main(argv=None):
    ap = argparse.ArgumentParser(description="Convert a Source engine map (.bsp) into a Cry of Fear map.")
    ap.add_argument("bsp", help="Source map .bsp (inside its game's maps folder, so its textures can be found)")
    ap.add_argument("--name", help="output map name (default: s_<source name>)")
    ap.add_argument("--cof", action="append", help="Cry of Fear's cryoffear folder (repeatable; default: the Steam install)")
    ap.add_argument("--tools", default=DEF_TOOLS, help="folder with qcsg/qbsp2/vis/qrad")
    ap.add_argument("--maxtex", type=int, default=256, help="largest texture size (default 256)")
    ap.add_argument("--light-scale", type=float, default=1.0, help="multiply light brightness")
    ap.add_argument("--fullvis", action="store_true", help="full vis instead of -fast (slow)")
    ap.add_argument("--work", help="working folder (default: tools/source2cof_work/<name>)")
    a = ap.parse_args(argv)
    cofs = a.cof or [DEF_COF]
    name = (a.name or "s_" + os.path.splitext(os.path.basename(a.bsp))[0]).lower()
    name = re.sub(r"[^a-z0-9_]", "_", name)[:30]
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    work = short_path(a.work or os.path.join(here, "source2cof_work", name))

    t0 = time.time()
    c = Converter(a.bsp, name, a.maxtex, a.light_scale)
    c.convert()
    s = c.stats
    log("  %d world brushes, %d dropped (tools/triggers/hint), %d in the 3D skybox (left out), %d outside GoldSrc's +-%d limit, %d displacements flattened" % (
        s["brushes"], s["dropped"], s["skybox"], s["outside"], LIMIT, s["displacement_faces"]))
    log("  %d brush/point entities kept, %d textures" % (len(c.ents), len(c.tex_data)))
    lo, hi = c.bounds
    log("  size: %.0f x %.0f x %.0f units" % tuple(hi - lo))
    map_path = c.write(work)
    log("wrote %s" % map_path)
    tex_bytes = sum(len(mt) for mt, size in c.tex_data.values())
    embed = tex_bytes < 1800000            # the old qcsg holds at most 2 MB of textures in a map
    log("  textures: %.1f MB, %s" % (tex_bytes / 1e6, "inside the map" if embed else "in %s.wad next to it" % name))
    ok = compile_map(map_path, a.tools, DEF_RAD, not a.fullvis, os.path.join(work, name + "_compile.log"))
    bsp_out = os.path.join(work, name + ".bsp")
    if not ok:
        log("compile failed; see %s" % os.path.join(work, name + "_compile.log"))
        return 1
    patch_textures(bsp_out, c.tex_data, name, embed)
    for cof in cofs:
        maps = os.path.join(cof, "maps")
        os.makedirs(maps, exist_ok=True)
        shutil.copy2(bsp_out, os.path.join(maps, name + ".bsp"))
        if not embed:
            shutil.copy2(os.path.join(work, name + ".wad"), os.path.join(cof, name + ".wad"))
        c.sky(cof)
        log("installed %s" % os.path.join(maps, name + ".bsp"))
    log("done in %.0f s. In Cry of Fear's console:  map %s" % (time.time() - t0, name))
    return 0
