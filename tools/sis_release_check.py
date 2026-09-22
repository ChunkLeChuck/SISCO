r"""No instrument in what ships.

While the mod was being worked out it carried counters and hand-offs to a measuring tool, behind a SIS_DEV switch. The
tool has been retired and all of that is gone (git tag streamlimits-last has the last version that had it). This check
is what stops any of it coming back unnoticed: the shipping sources must contain no instrumentation construct at all,
and the built .asi must contain none of their names.

Usage: python sis_release_check.py <src\StreamingMemory folder>
"""
import os
import re
import sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SRC = [os.path.join(ROOT, "core", "sis_core.h"), os.path.join(ROOT, "SIS", "SIS.cpp")]
ASI = os.path.join(ROOT, "obj", "SISCO.asi")

# Constructs that only ever existed to feed the measuring tool.
BANNED = [
    (r"\bSIS_DEV\b", "the dev build switch"),
    (r"\bDEV\s*\(", "the DEV(...) macro"),
    (r"\bDev[A-Z]\w*\s*\(", "a Dev* hand-off to the recorder"),
    (r"\bg_(pushCalls|pushInCalls|pushInRet|spilled|spillDropped|queueCorrupt|refills|linkTake|linkFree|"
     r"linkFreeForeign|nativeInitCalls)\b", "a counter"),
]
NAMES = [b"DevOnSpill", b"DevDrainPre", b"DevDrainPost", b"DevOnLockNested", b"DevOnLockTaken", b"DevOnBrake",
         b"DevOnBrakeTick", b"DevOnBrakeStart", b"DevOnResize", b"DevOnQueueCorrupt", b"g_pushCalls", b"g_refills",
         b"g_spilled", b"g_queueCorrupt"]

bad = 0
for path in SRC:
    text = open(path, encoding="utf-8", errors="replace").read()
    for pat, what in BANNED:
        for m in re.finditer(pat, text):
            line = text[:m.start()].count("\n") + 1
            print(f"  FAIL  {os.path.basename(path)}:{line} carries {what}: {m.group(0)!r}")
            bad += 1
print(f"  {len(SRC)} shipping sources scanned for {len(BANNED)} kinds of instrument")

if not os.path.exists(ASI):
    print(f"  FAIL  no built SISCO.asi at {ASI}: the binary was not checked")
    bad += 1
else:
    blob = open(ASI, "rb").read()
    hits = [n.decode() for n in NAMES if n in blob]
    if hits:
        print(f"  FAIL  SISCO.asi carries instrument names: {hits}")
        bad += len(hits)
    else:
        print(f"  SISCO.asi {len(blob):,} bytes, {len(NAMES)} instrument names absent")

print(f"\n{'no instrument in what ships' if not bad else str(bad) + ' problems'}")
sys.exit(1 if bad else 0)
