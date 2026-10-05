"""Reads a DLL's export name table directly and compares it with src/server/hl_exports.txt.
Usage: python check_exports.py path\\to\\hl.dll"""
import os, struct, sys

d = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
opt = pe + 24
exp_rva, exp_size = struct.unpack_from('<II', d, opt + 96)
nsec = struct.unpack_from('<H', d, pe + 6)[0]
optsize = struct.unpack_from('<H', d, pe + 20)[0]
sections = [struct.unpack_from('<8sIIII', d, pe + 24 + optsize + i * 40) for i in range(nsec)]


def off(rva):
    for _, vsize, va, rsize, raw in sections:
        if va <= rva < va + max(vsize, rsize):
            return rva - va + raw
    raise ValueError(rva)


o = off(exp_rva)
count = struct.unpack_from('<I', d, o + 24)[0]
names_rva = struct.unpack_from('<I', d, o + 32)[0]
names = []
for i in range(count):
    r = struct.unpack_from('<I', d, off(names_rva) + 4 * i)[0]
    names.append(d[off(r):off(r) + 256].split(b'\0')[0].decode())

ref = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'src', 'server', 'hl_exports.txt')
orig = open(ref).read().split()
print('exports:', len(names), '| original:', len(orig), '| identical names:', sorted(names) == sorted(orig))
missing = sorted(set(orig) - set(names))
extra = sorted(set(names) - set(orig))
if missing: print('missing:', missing[:10])
if extra: print('extra:', extra[:10])
