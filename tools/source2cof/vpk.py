"""Read-only access to a Source game's files: loose files and VPK archives
(version 1 and 2), searched in order."""
import os, struct


class Vpk:
    def __init__(self, dir_path):
        self.dir_path = dir_path
        self.prefix = dir_path[:-len("_dir.vpk")]
        self.files = {}
        with open(dir_path, "rb") as f:
            data = f.read()
        sig, ver, tree_size = struct.unpack_from("<III", data, 0)
        if sig != 0x55AA1234:
            raise ValueError("not a VPK: " + dir_path)
        header = 12 if ver == 1 else 28
        p = header
        end = header + tree_size

        def cstr():
            nonlocal p
            e = data.index(b"\0", p)
            s = data[p:e].decode("latin-1")
            p = e + 1
            return s

        while p < end:
            ext = cstr()
            if not ext:
                break
            while True:
                path = cstr()
                if not path:
                    break
                while True:
                    name = cstr()
                    if not name:
                        break
                    crc, preload, archive, offset, length, term = struct.unpack_from("<IHHIIH", data, p)
                    p += 18
                    pre = data[p:p + preload]
                    p += preload
                    full = (name if path == " " else path + "/" + name) + ("." + ext if ext != " " else "")
                    self.files[full.lower()] = (archive, offset, length, pre)
        self.tree_end = end

    def read(self, rel):
        e = self.files.get(rel.lower())
        if not e:
            return None
        archive, offset, length, pre = e
        if length == 0:
            return pre
        if archive == 0x7FFF:
            with open(self.dir_path, "rb") as f:
                f.seek(self.tree_end + offset)
                return pre + f.read(length)
        with open("%s_%03d.vpk" % (self.prefix, archive), "rb") as f:
            f.seek(offset)
            return pre + f.read(length)


class GameFiles:
    """Search path: each game folder's loose files, then its VPKs."""

    def __init__(self, game_dirs, extra=None):
        self.dirs = []
        self.vpks = []
        self.extra = extra or {}          # e.g. files from the map's own pakfile, searched first
        for d in game_dirs:
            if not os.path.isdir(d):
                continue
            self.dirs.append(d)
            for n in sorted(os.listdir(d)):
                if n.lower().endswith("_dir.vpk"):
                    try:
                        self.vpks.append(Vpk(os.path.join(d, n)))
                    except (OSError, ValueError, struct.error):
                        pass

    def read(self, rel):
        rel = rel.replace("\\", "/").lower()
        if rel in self.extra:
            return self.extra[rel]
        for d in self.dirs:
            p = os.path.join(d, rel)
            if os.path.isfile(p):
                with open(p, "rb") as f:
                    return f.read()
        for v in self.vpks:
            data = v.read(rel)
            if data is not None:
                return data
        return None


def search_dirs_for(bsp_path):
    """<root>/<game>/maps/x.bsp -> [<root>/<game>, other game folders under <root>...]."""
    game = os.path.dirname(os.path.dirname(os.path.abspath(bsp_path)))
    root = os.path.dirname(game)
    dirs = [game]
    order = ["hl1", "episodic", "ep2", "hl2", "platform"]
    others = [os.path.join(root, d) for d in os.listdir(root) if os.path.isdir(os.path.join(root, d))]
    others = [d for d in others if os.path.normcase(d) != os.path.normcase(game)]
    others.sort(key=lambda d: order.index(os.path.basename(d).lower()) if os.path.basename(d).lower() in order else 99)
    return dirs + others
