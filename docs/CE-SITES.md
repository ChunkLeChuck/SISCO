# SISCO on the Complete Edition: the sites

CE is 1.2.0.59 (timestamp 0x63D3E735, SizeOfImage 0x1BE6400, ASLR on). Its first megabyte of code (0x401000 to
0x4FB000) ships encrypted and is decrypted in memory at startup, so those addresses can only be read from a runtime
dump: `CEDump.asi` wrote one on 2026-09-22 (base 0x00F80000, 100% of the 20,663 relocation sites in the range pointing
into the image, 10,952,704 bytes of .text), and `tools/ce_merge.py` put it back into a copy of the executable,
`CEDUMP\ce-merged.exe`, which matches the file on disk byte for byte outside that range. Every address below was read
from that copy. The dump and the copy stay on the owner's machine: they are the game's code.

**What the dump also proved:** at the moment a plugin loads on CE, the streamer, the resource cache, the VehicleStruct
pool, the population object, the param list, the video-memory figure and the pool mode are all still zero, and the
budget table already holds its stock values (row 0 = 210 MB, row 15 High = 800 MB, the same layout as 1.0.8.0). So a
plugin is early enough on CE to set everything, exactly as on 1.0.8.0. CE's stock car and ped budgets are the same
40,000,000 and 50,000,000 bytes.

## Read by the lead (2026-09-22)

| site | 1.0.8.0 | CE | bytes | note |
|---|---|---|---|---|
| ArenaSize | 0x40108C | **0x4010D6** | `BF 00 80 02 00` | `mov edi, 0x28000` (KB), the same instruction as 1.0.8.0 |
| ArenaSize (low RAM) | none | **0x4010E9** | `BF 00 08 02 00` | CE only: `mov edi, 0x20800` (129.5 MiB) when a 64-bit RAM figure is under 0x59999980 (about 1.4 GiB). The raise patches the first one; a low-memory machine keeps the game's own. |
| arena init guard | 0x19D1B04 bit 2 | **0x1BB6900 bit 4** | data | the same dword also guards the earlier steps with bits 1 and 2; bit 4 is set once the arena's memory is allocated |
| arena object | 0x19CF920 | **0x1BB8A08** (vtable 0xFC99C4) | data | built by 0x4021E0; its field layout is NOT 1.0.8.0's (there: vtable, +0xC usable, +0x10 block). Only used for logging and for "is it built yet", and on CE the guard bit answers that. |
| VStructName | 0xA4A2AB | **0xA7B107** | `68 60 FD E9 00` | pushes "VehicleStruct" (0xE9FD60) |
| VStructSize | 0xA4A2B0 | **0xA7B10C** | `6A 32 8B C8` | `push 0x32` then `mov ecx, eax`, then the pool constructor 0xC6C5F0, then the pool pointer to 0x12FA84C |
| VehicleStruct pool | 0x1401BCC | **0x12FA84C** | data | confirmed by the store after the constructor |
| NativeInit | 0x626A80 | **0x86FC70** | `64 A1 2C 00 00 00` | the same prologue, so the same 5-byte jump fits |
| NativeSizeSt | 0x626A9D | **0x86FC94** | `89 1D 20 AF B4 01` | the size stored BEFORE the allocation: crash 1 is on CE too |
| NativePtrSt | 0x626AB3 | **0x86FCA8** | `A3 1C AF B4 01` | the pointer stored after the allocation at 0x86FCA3 |
| NativeCountSt | 0x626AD4 | **0x86FCC6** | `C7 05 28 AF B4 01 00 00 00 00` | the count; the capacity-not-positive path writes it at 0x86FCD6 instead |
| native table pointer | 0x190FDD0 | **0x1B4AF1C** | data | |
| native table size | 0x190FDD4 | **0x1B4AF20** | data | the lazy caller 0x86FCE0 reads this one first, as 0x626B40 does on 1.0.8.0 |
| native table count | 0x190FDDC | **0x1B4AF28** | data | |

**The one difference that changes code:** 1.0.8.0's native-table init takes the capacity in esi and returns with `ret`;
CE's takes it as a stack argument (`[esp+4]` at entry) and returns with `ret 4`, and leaves the table pointer in eax.
The replacement needs its own thunk on CE: read the argument, call the same replacement, then `ret 4`.

## Read by the site fan-out (2026-09-22)

Six agents read one group each from `ce-merged.exe` and the 1.0.8.0 executable side by side, reaching every site from an
anchor that survives a recompile (a unique immediate, an import, a call graph from an address already known) and then
matching the CE function against 1.0.8.0's instruction by instruction. 91 of the 95 things they reported carry the bytes
they read; every one of those was re-checked against `ce-merged.exe` afterwards and all of them hold. The three that
carry no bytes are a struct offset, the PE header and a site that does not exist on CE. Everything below is now in
`core/sis_core.h` as the `CE_` table, and `tools/sl_sites_check.py` re-checks all of it against `ce-merged.exe` on every
build (62 checks, part of `build.ps1`).

| what | 1.0.8.0 | CE |
|---|---|---|
| release queue push | 0x411450 | **0x426CA0** |
| its inline copies | 0x407A27, 0x407A91, 0x421FB0 | **0x4231C2, 0x4236C2, 0x424082, 0x4396E2** |
| the drain | 0x4114A0 | **0x42A1F0** |
| its call sites | 0x40E2D8, 0x40FCCA, 0x40FEF5, 0x410496 | **0x422DD8, 0x4289B4, 0x428B9C, 0x42917C, 0x426ECA** |
| resource cache | 0x1A72030 | **0x1C58BB0** (+0x148, +0x40148, +0x40760, +0x40268 all unchanged) |
| link pool / its lock | 0x1676188 / 0x1676180 | **0x15F8B48 / 0x15F8B40** |
| link size, takes, releases | 0xB72130, 0xB7214C, 0xB721D9, 0xB721AB, 0xB721B9, 0xB721FC | **0xAF48F0, 0xAF4719, 0xAF4829, 0xAF4780, 0xAF4746, 0xAF484C** |
| the spinlock | 0x456320 | **0x403C10** |
| drawable-slot size / pool | 0xA09860 / 0x121B9E0 | **0xA8ABD0 / 0x1173758** |
| Direct3DCreate9 call / thunk / import | 0x4067D8 / 0xD169AC / 0xD6B5EC | **0x424423 / none / 0xE73554** |
| the three option readers | 0x406A3F, 0x406A52, 0x402A65 | **0x424686, 0x424699, 0x420583** |
| their value slots | 0x10A98E0, 0x10A98F8, 0x10A9730 | **0x110DDA0, 0x110DCBC, 0x110DBD4** |
| credit (GetAvailableFreeMemory) | 0x40EA20 | **0x4276B0** |
| budget table / quality / render targets | 0x1098DD8 / 0x109802C / 0x18CADD0 | **0x1064580 / 0x106B56C / 0x17F5980** |
| streamer / loading byte / CPlayerInfo[] | 0xF493F0 / 0x18CB06C / 0x1100498 | **0x103E8D0 / 0x18B6F2E / 0x11A8808** |
| car and ped budgets | 0xF49B98 / 0xF49B94 | **0x10496DC / 0x10496D8** |

## The seven things that are not just another address

These are why the port is more than a table. Each one is proven in the harness on code shaped the way CE's compiler
left it (`core/sis_core_tests.h`, the "Complete Edition's shapes" block: 20 checks, run in both compiles).

1. **The push takes the cache in ecx**, not esi, **returns 0**, not 1, and **drops a null value** before it takes the
   lock. Its own hook, `HkPushCe`.
2. **The drain takes the cache in ecx**, not eax, and saves ebx as well. Its own wrapper, `ThunkDrainCe`.
3. **Four inline copies of the push, of 20 bytes**, not three of 22 (`inc eax` where 1.0.8.0 has `add eax, 1`), and two
   of the four hold the value in **edi** instead of esi. The fourth has no 1.0.8.0 counterpart at all: CE inlined a
   helper that 1.0.8.0 calls.
4. **A fifth drain call site**: 1.0.8.0's single shutdown flush is two places on CE, one inlined and one in the
   standalone function. Both have to be wrapped or the flush is bounded on one path only.
5. **The three lock releases have different encodings and lengths** (10, 5, 10 bytes: `mov [lock], 0`,
   `mov [lock], eax`, `mov [lock], 0`), and the two in the take function are **swapped in address order**, so they are
   mapped by role, not by address. The 5-byte one stores eax, which is also the function's return value and provably 0
   there; the stub keeps eax.
6. **The native-table init takes its capacity as a stack argument, returns the table's pointer and ends `ret 4`**
   (1.0.8.0: esi, returns the capacity, `ret`). Its own thunk, `ThunkNativeInitCe`. The size is still stored before the
   allocation, so crash 1 is on CE too.
7. **CPlayerInfo -> the player's ped is +0x598**, not +0x58C. ped +0x20 (the matrix) and matrix +0x30 (the position) are
   unchanged. This is the only struct offset that moved.

Smaller ones, all handled: the two flag readers compare against **ecx** instead of esi; there is **no jmp-thunk** before
the import slot, so the call site is checked as `FF 15 [slot]`; the arena's init guard uses **bits 2 and 4** (the block
records, then the arena memory), so the "too late" test is mask 6; the native size store is `mov [size], ebx` instead of
esi; and CE has a second arena size below about 1.4 GiB of RAM (`mov edi, 0x20800` at 0x4010E9) which is left alone, so
a low-memory machine keeps the game's own.

## What does not carry over

- **FusionFix's VehicleBudget and PedBudget patterns do not match on CE** (the budget is loaded with `imul` from memory,
  not `mov ecx`), so nothing else will have written those two globals: SIS carries the addresses itself and whatever it
  writes stands.
- **FusionFix's link-pool pattern does match** (0xAF48F0 byte for byte), so if it runs it still raises 13,000 to 20,000
  before SIS loads, exactly as on 1.0.8.0. SIS accepts whatever value it finds and raises it further.
- **The "RESC10" string is not a build check on CE**: it lives elsewhere and sits in a block of shader names. The build
  is named by its PE header and two reloc-free code anchors instead.
- The credit function's bytes are not 1.0.8.0's (`movss` where 1.0.8.0 has `fld`), but the first six are the same, so
  the check for "has FusionFix hooked it" behaves identically.
- Direct3DCreate9 has **two callers on both builds** (the second is an adapter helper that makes a temporary
  IDirect3D9). The hook is on the import slot, so it sees both, and it acts on the first call only, as it always did.

## Still open

- Nothing here was verified with the game running. The next step is a run on CE, in IV and in an episode.
- Whether CE's drawable-slot pool actually fills at 10,000 the way 1.0.8.0's does was not measured; the code is the
  same, so the freeze's precondition is the same.
