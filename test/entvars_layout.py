"""Prints Cry of Fear's real entvars_t layout, read from the save/restore field
table (gEntvarsDescription) inside the game's own server dll, next to the
standard Half-Life SDK offsets.
Usage: py test/entvars_layout.py <hl_cof.dll>
"""
import struct, sys

d = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
optsize = struct.unpack_from('<H', d, pe + 20)[0]
base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
secs = [struct.unpack_from('<8sIIII', d, pe + 24 + optsize + i * 40) for i in range(nsec)]

def va_of(off):
    for _, vs, va, rs, raw in secs:
        if raw <= off < raw + rs:
            return base + va + off - raw

def off_of(v):
    r = v - base
    for _, vs, va, rs, raw in secs:
        if va <= r < va + rs:
            return raw + r - va

def cstr(v):
    o = off_of(v)
    if o is None:
        return None
    e = d.find(b'\0', o, o + 40)
    s = d[o:e]
    return s.decode() if e > o and all(32 < c < 127 for c in s) else None

# Find the TYPEDESCRIPTION entry for "groundentity": {int type; char *name; int offset; short size; short flags}
needle = d.find(b'groundentity\0')
while needle >= 0:
    name_va = va_of(needle)
    ref = d.find(struct.pack('<I', name_va))
    if ref >= 4:
        break
    needle = d.find(b'groundentity\0', needle + 1)
start = ref - 4
# walk back to the table start (entries are 16 bytes and every name is a string)
while True:
    t, n, o, sz, fl = struct.unpack_from('<IIIhh', d, start - 16)
    if t > 20 or cstr(n) is None or o > 0x400:
        break
    start -= 16

SDK = {}
try:
    for line in open(__file__.rsplit('\\', 1)[0].rsplit('/', 1)[0] + '/entvars_sdk.txt'):
        k, v = line.split()
        SDK[k] = int(v, 16)
except OSError:
    pass

p = start
while True:
    t, n, o, sz, fl = struct.unpack_from('<IIIhh', d, p)
    name = cstr(n)
    if t > 20 or name is None:
        break
    sdk = SDK.get(name)
    mark = '' if sdk is None else ('' if sdk == o else '   <-- SDK %X' % sdk)
    print('%-20s %4X  type %2d x%d%s' % (name, o, t, sz, mark))
    p += 16
