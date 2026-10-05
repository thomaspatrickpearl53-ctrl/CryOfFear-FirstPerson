"""Patches the real C++ export names into the built server wrapper.

The linker can't write names like '?Think@CEnvModel@@EAEXXZ', so the wrapper is
linked with same-length placeholders (see gen_server_exports.py). This replaces
each placeholder string in place and re-sorts the export name table, which the
Windows loader binary-searches in GetProcAddress.

Usage: python fix_exports.py build\\hl.dll build\\hl_export_map.txt
"""
import struct, sys

dll_path, map_path = sys.argv[1], sys.argv[2]
mapping = dict(line.split() for line in open(map_path) if line.strip())
d = bytearray(open(dll_path, 'rb').read())

pe = struct.unpack_from('<I', d, 0x3c)[0]
opt = pe + 24
exp_rva = struct.unpack_from('<I', d, opt + 96)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
optsize = struct.unpack_from('<H', d, pe + 20)[0]
sections = [struct.unpack_from('<8sIIII', d, pe + 24 + optsize + i * 40) for i in range(nsec)]


def off(rva):
    for _, vsize, va, rsize, raw in sections:
        if va <= rva < va + max(vsize, rsize):
            return rva - va + raw
    raise ValueError(hex(rva))


e = off(exp_rva)
count = struct.unpack_from('<I', d, e + 24)[0]
names_off = off(struct.unpack_from('<I', d, e + 32)[0])
ords_off = off(struct.unpack_from('<I', d, e + 36)[0])

entries = []
patched = 0
for i in range(count):
    rva = struct.unpack_from('<I', d, names_off + 4 * i)[0]
    o = off(rva)
    end = d.index(b'\0', o)
    name = d[o:end].decode()
    real = mapping.get(name)
    if real is not None:
        assert len(real) == len(name), (name, real)
        d[o:end] = real.encode()
        name = real
        patched += 1
    ordinal = struct.unpack_from('<H', d, ords_off + 2 * i)[0]
    entries.append((name.encode(), rva, ordinal))

entries.sort(key=lambda t: t[0])
for i, (_, rva, ordinal) in enumerate(entries):
    struct.pack_into('<I', d, names_off + 4 * i, rva)
    struct.pack_into('<H', d, ords_off + 2 * i, ordinal)

open(dll_path, 'wb').write(d)
print(f'patched {patched} of {len(mapping)} names, {count} exports re-sorted')
