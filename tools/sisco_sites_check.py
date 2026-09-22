"""Check SISCO's site table against a GTAIV.exe on disk, before anything is deployed.

Reads the "// SITE" lines and the #define blocks of the source and verifies, in the file:
  entry  <va> <bytes>     the prologue bytes the entry hook steals
  call   <va> -> <tgt>    an E8 rel32 whose target is tgt
  icall  <va> -> [<iat>]  FF 15 <iat>
  slot   <va> == <val>    a dword holding val
  bytes  <va> <bytes>     literal bytes
  import <va> <name>      an import slot whose hint/name entry names that function
plus: every SITE address has its #define, the build identity constants match, every D_ address lies in
.data, and no build anchor sits where a detour could land on it.
Usage: python sisco_sites_check.py <GTAIV.exe> [source ...]
"""
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from pe import PE  # noqa: E402   a small PE reader, kept beside this script so the check runs anywhere

exe = sys.argv[1]
srcs = sys.argv[2:] or [os.path.join(HERE, "..", "core", "sis_core.h")]
pe = PE(exe)
ib = pe.image_base
text = "\n".join(open(f, encoding="utf-8").read() for f in srcs)
ok = bad = 0


def res(cond, msg):
    global ok, bad
    if cond:
        ok += 1
        print("  ok   ", msg)
    else:
        bad += 1
        print("  FAIL ", msg)


def hexbytes(s):
    return bytes(int(x, 16) for x in s.split())


# Which build is this exe? The Complete Edition has its own site table (CE_ defines, CESITE lines), and only the code
# recovered from a dump can be checked: ce-merged.exe, never the encrypted file on disk.
_nt = struct.unpack_from("<I", pe.data, 0x3C)[0]
tds = struct.unpack_from("<I", pe.data, _nt + 8)[0]
soi = struct.unpack_from("<I", pe.data, _nt + 24 + 56)[0]
IS_CE = (soi, tds) == (0x1BE6400, 0x63D3E735)
pfx = "CE_" if IS_CE else ""
defines = {m.group(1)[len(pfx):]: int(m.group(2), 16)
           for m in re.finditer(r"#define\s+(%s[AD]_\w+)\s+0x([0-9A-Fa-f]+)u" % pfx, text)
           if pfx or not m.group(1).startswith("CE_")}
sites = re.findall(r"^// %sSITE (\w+)\s+(\w+)\s+0x([0-9A-Fa-f]+)\s+(.*)$" % ("CE" if IS_CE else ""), text, re.M)
print(f"{len(sites)} SITE lines, {len(defines)} address defines, exe {exe}")
for kind, name, va, rest in sites:
    va = int(va, 16)
    rest = rest.strip()
    res(va in defines.values(), f"{name}: 0x{va:X} has a #define")
    if kind in ("entry", "bytes"):
        want = hexbytes(rest)
        got = pe.read(va - ib, len(want))
        res(got == want, f"{name}: bytes at 0x{va:X} = {got.hex(' ').upper()} (want {want.hex(' ').upper()})")
    elif kind == "call":
        tgt = int(rest.split("->")[1].strip(), 16)
        b = pe.read(va - ib, 5)
        real = va + 5 + struct.unpack("<i", b[1:5])[0]
        res(b[0] == 0xE8 and real == tgt, f"{name}: E8 at 0x{va:X} -> 0x{real:X} (want 0x{tgt:X})")
    elif kind == "icall":
        iat = int(re.search(r"\[0x([0-9A-Fa-f]+)\]", rest).group(1), 16)
        b = pe.read(va - ib, 6)
        res(b == b"\xFF\x15" + struct.pack("<I", iat), f"{name}: FF 15 [0x{iat:X}] at 0x{va:X} = {b.hex(' ').upper()}")
    elif kind == "import":
        rva = pe.read_u32(va - ib)
        got = pe.read(rva + 2, len(rest) + 1)
        res(got == rest.encode() + b"\0", f"{name}: import slot 0x{va:X} names {rest}")
    elif kind == "slot":
        val = int(rest.split("==")[1].strip(), 16)
        got = pe.read_u32(va - ib)
        res(got == val, f"{name}: dword at 0x{va:X} = 0x{got:X} (want 0x{val:X})")
    else:
        res(False, f"{name}: unknown kind {kind}")

# IsKnownBuild names each build by a (SizeOfImage, TimeDateStamp) pair: one of them must be this exe's.
pairs = [(int(a, 16), int(b, 16)) for a, b in
         re.findall(r"soi == 0x([0-9A-Fa-f]+)u && tds == 0x([0-9A-Fa-f]+)u", text)]
res((soi, tds) in pairs, f"IsKnownBuild names this build: SizeOfImage 0x{soi:X}, TimeDateStamp 0x{tds:X} (knows {[(hex(s), hex(t)) for s, t in pairs]})")
if not IS_CE:
    m3 = re.search(r"slot0 != \(uint32_t\)VA\(0x([0-9A-Fa-f]+)\)", text)
    res(m3 and pe.read_u32(defines["A_ARENA_VTBL"] - ib) == int(m3.group(1), 16), "IsKnownBuild arena vtable slot 0")

# the core's stock values: the budget row the game uses (row 15 by texture quality) and the car and ped budgets
mq = re.search(r"quality <= 0 \? (\d+) : quality == 1 \? (\d+) : (\d+);", text)
row15 = [struct.unpack("<Q", pe.read(defines["D_TABLE"] - ib + 8 * (45 + q), 8))[0] >> 20 for q in range(3)]
res(mq and [int(x) for x in mq.groups()] == row15, f"StockBudgetMb equals the table's row 15: {row15}")
for name, addr in (("CAR_STOCK", "D_VEH_BUDGET"), ("PED_STOCK", "D_PED_BUDGET")):
    mc = re.search(r"#define %s (\d+)u" % name, text)
    got = pe.read_u32(defines[addr] - ib)
    res(mc and int(mc.group(1)) == got, f"{name} equals the file's initial {addr} ({got:,})")
mv = re.search(r"#define %sA_VSTRUCT_STR\s+0x([0-9A-Fa-f]+)u" % pfx, text)
res(mv and pe.read(int(mv.group(1), 16) - ib, 14) == b"VehicleStruct\0", "A_VSTRUCT_STR is the string 'VehicleStruct'")

# A build anchor must not sit where anything writes a detour. The first Complete Edition anchor was the entry of the
# credit function, which is exactly where FusionFix's ExtraStreamingMemory writes its own hook, so with that setting on
# the mod would have refused to load and blamed the decryption. An anchor is bad if it starts inside the first 16 bytes
# of any address the mod patches, or of the credit function, which is the one entry another mod is known to take.
anchors = [(n, int(va, 16)) for k, n, va, _ in sites if n.startswith("Anchor")]
hooked = {k: v for k, v in defines.items() if k.startswith("A_") and "ANCHOR" not in k}
for aname, ava in anchors:
    clash = [k for k, v in hooked.items() if v <= ava < v + 16]
    res(not clash, f"{aname} at 0x{ava:X} is clear of every site a detour could land on ({clash or 'clear'})")
if IS_CE:      # 1.0.8.0 is named by its RESC10 string and its arena vtable instead, neither of which anything hooks
    res(len(anchors) >= 2, f"the Complete Edition declares {len(anchors)} build anchors (two, in different functions)")

# data addresses live in .data (their contents are runtime state, so only the section is checkable)
for k, v in sorted(defines.items()):
    if k.startswith("D_"):
        res(pe.section_of(v - ib) == ".data", f"{k} 0x{v:X} is in .data")

print(f"\n{ok} ok, {bad} FAIL")
sys.exit(1 if bad else 0)
