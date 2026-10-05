"""Finds code that references a string in a 32-bit PE (push imm32 / mov reg,imm32
/ any 4-byte occurrence of the string's address in .text) and prints the VAs.
Usage: py test/xref.py <dll> "<exact string>" [...]
"""
import struct, sys

d = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
n = struct.unpack_from('<H', d, pe + 6)[0]
o = struct.unpack_from('<H', d, pe + 20)[0]
base = struct.unpack_from('<I', d, pe + 52)[0]
secs = [struct.unpack_from('<8sIIII', d, pe + 24 + o + i * 40) for i in range(n)]

def va(off):
    for name, vs, rva, rs, raw in secs:
        if raw <= off < raw + rs:
            return base + rva + off - raw

text = [s for s in secs if s[0].startswith(b'.text')][0]
for s in sys.argv[2:]:
    pos = d.find(s.encode() + b'\0')
    if pos < 0:
        print(s, 'not found')
        continue
    sva = va(pos)
    refs = []
    p = text[4]
    needle = struct.pack('<I', sva)
    while True:
        p = d.find(needle, p, text[4] + text[3])
        if p < 0:
            break
        refs.append(hex(va(p) - 1))     # instruction usually starts 1 byte before (push/mov)
        p += 1
    print(s, hex(sva), refs)
