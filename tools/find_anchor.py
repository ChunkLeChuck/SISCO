"""Find build-identity anchors: runs of code that name one build and nothing else, and that no other mod is likely to
have rewritten by the time SIS looks at them.

An anchor has to be
  - inside the range that ships encrypted on the Complete Edition (0x401000..0x4FB000), so that reading it correctly
    also proves the code has been decrypted,
  - free of relocated dwords, because the loader rewrites those wherever the image lands,
  - well past the start of its function, because a detour is written AT a function's entry: SIS's first CE anchor was
    the entry of the one function FusionFix hooks, which would have made SIS refuse to load,
  - not a site SIS itself patches, and not a site any mod has a reason to patch.

Usage: python find_anchor.py <exe> [lo] [hi] [minlen]
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "tools", "binhunt"))
from pe import PE  # noqa: E402

exe = sys.argv[1]
LO = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x401000
HI = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x4FB000
MINLEN = int(sys.argv[4]) if len(sys.argv) > 4 else 20
pe = PE(exe)
ib = pe.image_base
d = pe.data

# every relocation target, as a VA
relocs = set()
nt = struct.unpack_from("<I", d, 0x3C)[0]
rva, size = struct.unpack_from("<II", d, nt + 24 + 96 + 5 * 8)
off = next(s["rawptr"] + (rva - s["vaddr"]) for s in pe.sections if s["vaddr"] <= rva < s["vaddr"] + s["rawsize"])
end = off + size
while off < end:
    page, blk = struct.unpack_from("<II", d, off)
    if blk < 8:
        break
    for i in range((blk - 8) // 2):
        e, = struct.unpack_from("<H", d, off + 8 + 2 * i)
        if (e >> 12) == 3:
            relocs.add(ib + page + (e & 0xFFF))
    off += blk
print(f"{len(relocs):,} relocation targets in the image")

text = next(s for s in pe.sections if s["name"] == ".text")
base_off = text["rawptr"]


def va_of(o):
    return ib + text["vaddr"] + (o - base_off)


# Walk the range. A run breaks on a relocated dword (or the four bytes before one) and on int3 padding, and the first
# 48 bytes after any padding are dropped: that is a function entry and its first instructions.
lo_off = base_off + (LO - ib - text["vaddr"])
hi_off = base_off + (HI - ib - text["vaddr"])
runs = []
start = None
since_pad = 0
i = lo_off
while i < hi_off:
    va = va_of(i)
    is_pad = d[i] == 0xCC
    # a relocated dword covers four bytes ending at its target + 3
    is_reloc = any((va - k) in relocs for k in range(4))
    if is_pad:
        since_pad = 0
        if start is not None:
            runs.append((start, i))
            start = None
    elif is_reloc:
        if start is not None:
            runs.append((start, i))
            start = None
        since_pad += 1
    else:
        since_pad += 1
        if start is None and since_pad > 48:
            start = i
    i += 1
if start is not None:
    runs.append((start, hi_off))

runs = [(a, b) for a, b in runs if b - a >= MINLEN]
runs.sort(key=lambda r: r[1] - r[0], reverse=True)
print(f"{len(runs)} runs of {MINLEN}+ bytes that are reloc-free and at least 48 bytes into their function\n")
for a, b in runs[:12]:
    va = va_of(a)
    n = min(b - a, 24)
    print(f"  {va:#010x}  {b - a:>4} bytes free, first {n}: {d[a:a + n].hex(' ').upper()}")
