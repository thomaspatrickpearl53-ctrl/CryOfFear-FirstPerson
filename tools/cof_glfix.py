#!/usr/bin/env python3
"""Cry of Fear graphics fix for Wine / Proton (and some newer Windows PCs).

Cry of Fear ships its own opengl32.dll (the "Paranoia" renderer wrapper). Under
Wine/Proton, and with some newer drivers, that name clashes with the system's
opengl32 and the wrapper gives up when the driver lacks the old
wglGetDefaultProcAddress, so the game fails to start or renders black.

The community fix: copy the wrapper as opengp32.dll with that one check
skipped, and point the engine (hw.dll) and the game client at the new name.
This script makes those same 14 byte changes to YOUR OWN game files, so no
game files are shipped. Every file is checked against the exact original first
and against the known fixed result afterwards; anything that doesn't match is
left alone.

    python3 tools/cof_glfix.py "<Cry of Fear folder>"            apply
    python3 tools/cof_glfix.py "<Cry of Fear folder>" --undo     put the originals back
    python3 tools/cof_glfix.py "<Cry of Fear folder>" --check    just report

The game client is patched where the mod keeps it: cryoffear/cl_dlls/client_cof.dll
when the first-person mod is installed, otherwise cryoffear/cl_dlls/client.dll.
"""
import hashlib, os, shutil, sys

# file: (original sha256, fixed sha256, [(offset, original byte, fixed byte)])
HW = ("9dd34e536c4bb7cda3bc1bc4f0f5f6163687566a16f35c0ea0be59f93da62875",
      "31e320322b0c70ac876febcfa78a5cf8bd9848b3083144305e40f26fd34351e7",
      [(0x1BAF01, 0x6C, 0x70), (0x1BAF49, 0x6C, 0x70), (0x1C2D69, 0x6C, 0x70), (0x1CF7E1, 0x6C, 0x70), (0x1CF8BD, 0x6C, 0x70)])
GL = ("26732d7a9aeb8079f2d0c0920f7e433521a2fcc570017c7dafd45bdd30cdbeab",
      "48847a6402a5bab0b761d4af0cda0d63d83adb1a2c34b1b2171a4f1ba379167f",
      [(0x6776, 0x75, 0xEB)])
CLIENT = ("d2a04641b301804f6f449aa68265042b13adc360925b80033d417ec9f38c9c00",
          "5151e085b2dc0ecec7d9c53beadda6c6d68a286e6c8bed992b9edcce4d9d23cc",
          [(0x149C72, 0x6C, 0x70), (0x149F3D, 0x6C, 0x70), (0x149F65, 0x6C, 0x70), (0x149F90, 0x6C, 0x70),
           (0x149FBB, 0x6C, 0x70), (0x149FEC, 0x6C, 0x70), (0x14A00E, 0x6C, 0x70), (0x14B3B0, 0x6C, 0x70)])


def sha(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def find(folder, *parts):
    """Case-insensitive path lookup (Linux file systems are case-sensitive)."""
    cur = folder
    for p in parts:
        if not os.path.isdir(cur):
            return os.path.join(cur, *parts[parts.index(p):])
        hit = next((n for n in os.listdir(cur) if n.lower() == p.lower()), p)
        cur = os.path.join(cur, hit)
    return cur


def patched(data, spec):
    data = bytearray(data)
    for off, old, new in spec[2]:
        if off >= len(data) or data[off] != old:
            return None
        data[off] = new
    return bytes(data)


def fix_file(src, dst, spec, label, backup):
    """Write the fixed version of src to dst. True if dst is (now) fixed."""
    if os.path.isfile(dst) and sha(dst) == spec[1]:
        print("  %s: already fixed" % label)
        return True
    if not os.path.isfile(src):
        print("  %s: %s not found" % (label, src))
        return False
    h = sha(src)
    if h != spec[0]:
        print("  %s: %s isn't the version this fix is for; left alone" % (label, os.path.basename(src)))
        return False
    with open(src, "rb") as f:
        out = patched(f.read(), spec)
    if out is None or hashlib.sha256(out).hexdigest() != spec[1]:
        print("  %s: unexpected contents; left alone" % label)
        return False
    if backup and os.path.normcase(src) == os.path.normcase(dst) and not os.path.exists(backup):
        shutil.copy2(src, backup)
    with open(dst + ".tmp", "wb") as f:
        f.write(out)
    os.replace(dst + ".tmp", dst)
    print("  %s: fixed" % label)
    return True


def client_path(game):
    cl = find(game, "cryoffear", "cl_dlls")
    cof = find(cl, "client_cof.dll")
    return cof if os.path.isfile(cof) else find(cl, "client.dll")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = [a for a in sys.argv[1:] if a.startswith("--")]
    if not args:
        print(__doc__)
        return 2
    game = args[0]
    if not os.path.isdir(find(game, "cryoffear")):
        print("%s doesn't look like a Cry of Fear folder" % game)
        return 1
    hw, gl_src, gl_dst = find(game, "hw.dll"), find(game, "opengl32.dll"), find(game, "opengp32.dll")
    client = client_path(game)

    if "--check" in flags:
        for label, path, spec in (("engine (hw.dll)", hw, HW), ("renderer (opengp32.dll)", gl_dst, GL), ("client", client, CLIENT)):
            state = "missing" if not os.path.isfile(path) else (
                "fixed" if sha(path) == spec[1] else "original" if sha(path) == spec[0] else "other version")
            print("  %-26s %s" % (label, state))
        return 0

    if "--undo" in flags:
        for path in (hw, client):
            bak = path + ".beforeglfix"
            if os.path.isfile(bak):
                os.replace(bak, path)
                print("  restored %s" % os.path.basename(path))
        if os.path.isfile(gl_dst) and sha(gl_dst) == GL[1]:
            os.remove(gl_dst)
            print("  removed opengp32.dll")
        return 0

    print("Cry of Fear graphics fix (Wine/Proton):")
    ok = fix_file(gl_src, gl_dst, GL, "renderer (opengp32.dll)", None)
    # The engine and client only switch to opengp32.dll once it exists.
    ok = ok and fix_file(hw, hw, HW, "engine (hw.dll)", hw + ".beforeglfix")
    ok = ok and fix_file(client, client, CLIENT, "client", client + ".beforeglfix")
    if not ok:
        print("The fix wasn't (fully) applied; nothing that didn't match was changed.")
        return 1
    print("Done. Originals kept as *.beforeglfix; undo with --undo.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
