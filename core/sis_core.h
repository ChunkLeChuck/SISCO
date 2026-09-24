// sis_core.h: SISCO, the Streaming Issue Solver: Crash Override, for GTA IV 1.0.8.0 and the Complete Edition.
//
// Everything the mod does is in this file. SIS.cpp builds it into the .asi and adds a log and a once-a-second worker;
// sis_core_tests.h is the harness, and it is compiled into the same build, so what the tests exercise is the code that
// ships. One translation unit: this header holds definitions, not just declarations.
//
// What it does:
//  - the release-queue fix: the game's 65,536-entry release queue has no bounds check;
//  - the link-pool lock fix: the game's own eviction takes a spinlock it already holds, which is the freeze;
//  - the native-table race fix, which is FusionFix's startup crash;
//  - the drawable-slot and link pools raised before the game builds them, because a link pool that runs dry crashes;
//  - the VehicleStruct pool from 50 to 100;
//  - with DXVK only: the streaming arena to 400 MiB, -managed and the card's real video memory given to the game, the
//    budget sized to the card, the car and ped budgets raised, and the address-space brake.
// Without DXVK the game keeps its own budget, its own arena and its own traffic, and only the list above it applies.
// That path has not been measured: everything here was worked out and run under DXVK.
//
// A map of this file: the addresses and what was read at each, then the small primitives that patch and read them,
// then the three fixes, the launch-time sizes, the budget and the brake, the once-a-second tick, and the install.
// Quantities, used throughout: T the streaming budget, X the render targets, O everything else the card holds,
// B the card's budget for this process, V the video memory the game is holding, L the largest free block of the
// 32-bit address space. All in MB unless a name says otherwise.
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <dxgi1_4.h>

#ifndef SISCO_BUILD
#define SISCO_BUILD "dev"
#endif
#define SIS_VERSION "1.0.1"

// ---------------------------------------------------------------------------------------------
// Addresses (1.0.8.0 VA, image base 0x400000), rebased at run time. SITE lines are checked against GTAIV.exe on disk
// by tools/sisco_sites_check.py before every deploy. Bytes that hold absolute addresses are the file's values; at run
// time they are compared rebased.
// ---------------------------------------------------------------------------------------------
// The release queue:
// SITE entry  QueuePush     0x411450  83 BE 60 07 04 00 00
// SITE bytes  QueuePushIn1  0x407A27  A1 78 21 AB 01 8D 0C 85 78 21 A7 01 83 C0 01 A3 78 21 AB 01 89 31
// SITE bytes  QueuePushIn2  0x407A91  A1 78 21 AB 01 8D 0C 85 78 21 A7 01 83 C0 01 A3 78 21 AB 01 89 31
// SITE bytes  QueuePushIn3  0x421FB0  A1 78 21 AB 01 8D 0C 85 78 21 A7 01 83 C0 01 A3 78 21 AB 01 89 31
// SITE call   DrainFlush    0x40E2D8  -> 0x4114A0
// SITE call   DrainCreate   0x40FCCA  -> 0x4114A0
// SITE call   DrainOver1    0x40FEF5  -> 0x4114A0
// SITE call   DrainOver2    0x410496  -> 0x4114A0
// The link pool and its spinlock. 0xB72130 builds the link pool
// (push 0x32C8, 13,000 x 20-byte nodes, pointer-linked) at 0x1676188. 0xB72140 takes the spinlock [0x1676180] and then a
// link; when none is free it evicts through 0xA099F0 still holding the lock. Both evictions (that one and the
// drawable-slot steal in 0xA09880) free the evicted entities' links through 0xB721D0, which takes the same spinlock,
// and 0x456320 keeps no owner: the holder waits for itself. The LOCK WORD at 0x1676180 has exactly five users, the
// two takes and three releases below; 0x456320 itself is a shared acquire helper with many callers, so only those two
// call sites are retargeted and no other lock in the game is affected. No branch lands inside any of these sites.
// SITE bytes  LinkSize      0xB72130  68 C8 32 00 00
// SITE call   LinkTake1     0xB7214C  -> 0x456320
// SITE call   LinkTake2     0xB721D9  -> 0x456320
// SITE bytes  LinkFree1     0xB721AB  89 1D 80 61 67 01
// SITE bytes  LinkFree2     0xB721B9  89 1D 80 61 67 01
// SITE bytes  LinkFree3     0xB721FC  C7 05 80 61 67 01 00 00 00 00
// The drawable-slot pool: 0xA09860 builds 10,000 12-byte nodes (push 0x2710) at 0x121B9E0.
// SITE bytes  SlotSize      0xA09860  68 10 27 00 00
// The streaming arena's size in KB (mov edi, 0x28000), read once by 0x401060 when it builds the arena.
// SITE bytes  ArenaSize     0x40108C  BF 00 80 02 00
// The native-table init, which is the startup crash: the size is stored before the allocation, the pointer after it,
// the keys alone are zeroed. Two callers (0x625515, and the lazy 0x626B57 when the size is still 0).
// SITE bytes  NativeInit    0x626A80  64 A1 2C 00 00 00
// SITE bytes  NativeSizeSt  0x626A9D  89 35 D4 FD 90 01
// SITE bytes  NativePtrSt   0x626AB3  A3 D0 FD 90 01
// SITE bytes  NativeCountSt 0x626AD4  C7 05 DC FD 90 01 00 00 00 00
// The VehicleStruct pool: push "VehicleStruct", push 50 (imm8), then the pool constructor 0x7F3DB0.
// SITE bytes  VStructName   0xA4A2AB  68 34 EE DA 00
// SITE bytes  VStructSize   0xA4A2B0  6A 32 8B C8
// Direct3DCreate9: called at 0x4067D8 through the thunk 0xD169AC and the import slot 0xD6B5EC, and a second time
// at 0x40222B by an adapter helper that makes a temporary object of its own. The hook is on the import slot, so it
// sees both, and it acts on the first of them,
// after the command line is parsed and before the pool mode and the video-memory figure are decided.
// SITE call   D3DCreateCall  0x4067D8  -> 0xD169AC
// SITE bytes  D3DCreateThunk 0xD169AC  FF 25 EC B5 D6 00
// SITE import D3DCreateIat   0xD6B5EC  Direct3DCreate9
// The readers of the three launch options SISCO sets: -unmanaged is tested before -managed; the
// video-memory figure is atoi(-availablevidmem) MiB.
// SITE bytes  UnmanagedRead 0x406A3F  39 35 E0 98 0A 01
// SITE bytes  ManagedRead   0x406A52  39 35 F8 98 0A 01
// SITE bytes  PoolModeStore 0x406A4C  89 35 E0 AC 8C 01
// SITE bytes  MemRestrictTest 0x40F152  83 3D 78 AB 0A 01 00 0F 85 58 01 00 00
// SITE bytes  VidmemRead    0x402A65  A1 30 97 0A 01
// The budget credit, grcResourceCache::GetAvailableFreeMemory: FusionFix's ExtraStreamingMemory hooks its entry and
// raises the 0.9 multiplier up to 1.5 (memory.ixx), which the sizing must then assume.
// SITE bytes  CreditEntry   0x40EA20  55 8B EC 83 E4 F8 D9 45 08
// Build identity:
// SITE bytes  Resc10Str     0xEBDD00  52 45 53 43 31 30 00

#define A_PUSH          0x411450u   // release-queue push: esi = cache, one stack arg, al = 1, ret 4; no bounds check
#define A_PUSH_INL1     0x407A27u   // inline copy of the push in device shutdown (value in esi, lock held)
#define A_PUSH_INL2     0x407A91u   // the second one there
#define A_PUSH_INL3     0x421FB0u   // inline copy in the texture factory's device-lost loop
#define A_DRAIN         0x4114A0u   // release-queue drain: eax = cache, al = 1; saves ebp/esi/edi
#define A_DRAIN_SITE1   0x40E2D8u   // device-lost / shutdown flush
#define A_DRAIN_SITE2   0x40FCCAu   // every resource create
#define A_DRAIN_SITE3   0x40FEF5u   // over-budget create path
#define A_DRAIN_SITE4   0x410496u   // over-budget create path
#define A_LINK_SIZE     0xB72130u   // push 0x32C8: the link pool's node count, read once by 0xB72060
#define A_LINK_TAKE1    0xB7214Cu   // call 0x456320 in 0xB72140 (hand out a link)
#define A_LINK_TAKE2    0xB721D9u   // call 0x456320 in 0xB721D0 (free a link)
#define A_LINK_FREE1    0xB721ABu   // mov [0x1676180], ebx (ebx = 0): 0xB72140's release when it found a link
#define A_LINK_FREE2    0xB721B9u   // mov [0x1676180], ebx (ebx = 0): 0xB72140's release when it found none
#define A_LINK_FREE3    0xB721FCu   // mov [0x1676180], 0: 0xB721D0's release
#define A_SPIN_ACQUIRE  0x456320u   // cdecl (lock): InterlockedCompareExchange(lock, 1, 0) and a back-off loop; no owner kept
#define A_SLOT_SIZE     0xA09860u   // push 0x2710: the drawable-slot pool's node count, read once by the streaming init
#define A_ARENA_SIZE    0x40108Cu   // mov edi, 0x28000: the arena size in KB, read once by 0x401060
#define A_ARENA_VTBL    0xEC0824u
#define A_RESC10_STR    0xEBDD00u
#define A_NATIVE_INIT   0x626A80u   // the native-table init: esi = capacity, ret
#define A_NATIVE_SIZE_ST 0x626A9Du
#define A_NATIVE_PTR_ST 0x626AB3u
#define A_NATIVE_CNT_ST 0x626AD4u
#define A_VSTRUCT_NAME  0xA4A2ABu
#define A_VSTRUCT_SIZE  0xA4A2B0u   // push 0x32 (imm8, signed: at most 0x7F)
#define A_D3D_CALL      0x4067D8u
#define A_D3D_THUNK     0xD169ACu
#define A_D3D_IAT       0xD6B5ECu
#define A_RD_UNMANAGED  0x406A3Fu
#define A_RD_MANAGED    0x406A52u
#define A_POOL_STORE    0x406A4Cu   // mov [0x18CACE0], esi: proves the pool-mode address on this build
#define A_RD_VIDMEM     0x402A65u
#define A_GAFM          0x40EA20u   // grcResourceCache::GetAvailableFreeMemory
#define D_CACHE         0x1A72030u  // grcResourceCache object
#define C_V             0x40268u    // u64: video bytes held (V), cache + 0x40268
#define D_LINK_POOL     0x1676188u  // the link pool: node array [+0x50], free list [+0x38] (first) to the sentinel +0x3C
#define D_LINK_LOCK     0x1676180u  // its spinlock
#define D_SLOT_POOL     0x121B9E0u  // the drawable-slot pool: free list [+0x20] (first) to the sentinel +0x24
#define D_ARENA         0x19CF920u  // the arena object (vtable 0 until it is built)
#define D_INIT_GUARD    0x19D1B04u  // bit 2 is set by 0x401060 once the arena is allocated
#define D_NATIVE_PTR    0x190FDD0u  // the native table: pointer, size, capacity for the lazy init, count
#define D_NATIVE_SIZE   0x190FDD4u
#define D_NATIVE_COUNT  0x190FDDCu
#define D_VSTRUCT_POOL  0x1401BCCu  // CPool* VehicleStruct (0 until the pool is built)
#define A_VSTRUCT_STR   0xDAEE34u   // "VehicleStruct"
#define D_SET_VIEW      0x10FC304u  // the graphics sliders the player runs at, logged so a report carries them
#define D_SET_DETAIL    0x10FC308u
#define D_SET_CARS      0x10FC30Cu
#define D_FRAME         0x10FCC30u  // the game's frame counter: read for the build's frame rate on the load line
#define A_FRAME_INC     0x7CC1DCu   // add dword ptr [D_FRAME], 1: the counter is read only while this is what advances it
#define D_POOL_MODE     0x18CACE0u  // 0 unmanaged, 1 D3D-runtime managed: the block at 0x406A3F, last write wins
#define A_MEMRESTRICT_TEST 0x40F152u  // cmp [nomemrestrict], 0 / jne row15: FusionFix forces this branch (ExtraStreamingMemory off)
#define D_PARAM_NOMEMRESTRICT 0x10AAB78u   // the -nomemrestrict slot the test reads
#define D_PARAM_MEMRESTRICT 0x10AAB90u     // the -memrestrict N slot: with it set the game never reads the table, so the budget is held
#define D_PARAM_UNMANAGED 0x10A98E0u  // value slots of the game's own launch options (record + 8): non-NULL = given
#define D_PARAM_MANAGED 0x10A98F8u
#define D_PARAM_VIDMEM  0x10A9730u
#define D_TABLE         0x1098DD8u  // the budget table: 16 rows x 3 texture qualities, u64 bytes
#define D_QUALITY       0x109802Cu  // texture quality 0..2
#define D_RT0           0x18CADD0u  // u64 render-target bytes (X)
#define D_PLAYERINFO    0x1100498u  // CPlayerInfo*[32]; +0x58C the player's ped; ped +0x20 matrix; matrix +0x30 position
#define D_STREAMER      0xF493F0u   // +4: the streamer's N, 0 until the streaming system is up
#define D_LOADING       0x18CB06Cu  // byte: the loading screen is up
#define D_VEH_BUDGET    0xF49B98u   // car model budget, bytes; read live by the traffic code (0xA41FED, 0xA41310)
#define D_PED_BUDGET    0xF49B94u   // ped model budget, bytes; read live (0xA42014)
// The engine-sound slots, read and never written. 0x825490 fills a table by asking the audio config for
// STREAM_ENGINE_1, _2 and so on until one is not declared, at most the 25 in its own loop; the game then plays one
// looping engine sound per distinct vehicle model near the player, out of those slots. Stock GTA IV declares 15 of them
// in pc\audio\config\waveslots.xml, so that is the real ceiling, and a raised car budget is what makes it reachable.
// SITE bytes  AudioSlotCap  0x8254D2  83 F8 19
// SITE bytes  AudioHeap     0x7BC4EC  BE 00 00 E0 07
// SITE bytes  FrameInc      0x7CC1DC  83 05 30 CC 0F 01 01
#define A_AUDIO_SLOT_CAP 0x8254D2u  // cmp eax, 0x19: the loop's own limit, the imm8 at +2
#define A_AUDIO_HEAP    0x7BC4ECu   // mov esi, 0x7E00000: the audio heap the slots are carved from, the imm32 at +1
#define D_AUDIO_SLOTS   0x117B200u  // how many the loop resolved

// ---------------------------------------------------------------------------------------------
// The Complete Edition (1.2.0.59, TimeDateStamp 0x63D3E735). Same game, recompiled: every address moved and a few
// instructions have a different shape, so each one below was read from that build's own code and byte-checked.
// CE ships 0x401000..0x4FB000 encrypted and decrypts it in memory before a plugin loads, so the addresses in that range
// can only be read from a runtime dump; the two build anchors live there, which is also what proves the code is decrypted
// by the time SISCO runs. CESITE lines are checked by sisco_sites_check.py against the recovered executable.
// ---------------------------------------------------------------------------------------------
// CESITE entry  QueuePush     0x426CA0  53 8B 5C 24 08
// The four inline copies, the three lock releases, the native init's three stores and the three option readers carry
// the addresses the loader rebases, so SISCO builds their expected bytes at run time; here they are as the file has them,
// which is what says the shapes in the table (which register, which encoding, 20 bytes not 22) are the right ones.
// CESITE bytes  QueuePushIn1  0x4231C2  A1 F8 8C C9 01 8D 0C 85 F8 8C C5 01 40 A3 F8 8C C9 01 89 39
// CESITE bytes  QueuePushIn2  0x4236C2  A1 F8 8C C9 01 8D 0C 85 F8 8C C5 01 40 A3 F8 8C C9 01 89 31
// CESITE bytes  QueuePushIn3  0x424082  A1 F8 8C C9 01 8D 0C 85 F8 8C C5 01 40 A3 F8 8C C9 01 89 31
// CESITE bytes  QueuePushIn4  0x4396E2  A1 F8 8C C9 01 8D 0C 85 F8 8C C5 01 40 A3 F8 8C C9 01 89 39
// CESITE bytes  LinkFree1     0xAF4780  C7 05 40 8B 5F 01 00 00 00 00
// CESITE bytes  LinkFree2     0xAF4746  A3 40 8B 5F 01
// CESITE bytes  LinkFree3     0xAF484C  C7 05 40 8B 5F 01 00 00 00 00
// CESITE bytes  NativeSizeSt  0x86FC94  89 1D 20 AF B4 01
// CESITE bytes  NativePtrSt   0x86FCA8  A3 1C AF B4 01
// CESITE bytes  NativeCountSt 0x86FCC6  C7 05 28 AF B4 01 00 00 00 00
// CESITE bytes  UnmanagedRead 0x424686  39 0D A0 DD 10 01
// CESITE bytes  ManagedRead   0x424699  39 0D BC DC 10 01
// CESITE bytes  PoolModeStore 0x4246AB  A3 44 D9 7E 01
// CESITE bytes  MemRestrictTest 0x427E5A  83 3D 48 DE 10 01 00 0F 85 62 01 00 00
// CESITE bytes  VidmemRead    0x420583  A1 D4 DB 10 01
// CESITE call   DrainFlushA   0x422DD8  -> 0x42A1F0
// CESITE call   DrainFlushB   0x426ECA  -> 0x42A1F0
// CESITE call   DrainCreate   0x4289B4  -> 0x42A1F0
// CESITE call   DrainOver1    0x428B9C  -> 0x42A1F0
// CESITE call   DrainOver2    0x42917C  -> 0x42A1F0
// CESITE call   LinkTake1     0xAF4719  -> 0x403C10
// CESITE call   LinkTake2     0xAF4829  -> 0x403C10
// CESITE bytes  LinkSize      0xAF48F0  68 C8 32 00 00
// CESITE bytes  SlotSize      0xA8ABD0  68 10 27 00 00
// CESITE bytes  ArenaSize     0x4010D6  BF 00 80 02 00
// CESITE bytes  NativeInit    0x86FC70  64 A1 2C 00 00 00
// CESITE bytes  VStructName   0xA7B107  68 60 FD E9 00
// CESITE bytes  VStructSize   0xA7B10C  6A 32 8B C8
// CESITE icall  D3DCreateCall 0x424423  -> [0xE73554]
// CESITE import D3DCreateIat  0xE73554  Direct3DCreate9
// The build anchors. They are read before anything is installed, and reading them correctly also proves the Complete
// Edition has decrypted its own code, because both live inside the range that ships encrypted. They are deliberately
// NOT at a function's entry and NOT in any function a mod has a reason to hook: the first version of this check used
// the entry of the credit function, which is exactly where FusionFix's ExtraStreamingMemory writes its detour, so with
// that setting on SISCO would have refused to load and blamed the decryption. One is vector maths, the other float
// setup; both are leaves, both are reloc-free, and they sit 340 KB apart.
// CESITE bytes  AnchorA       0x466C40  0F 58 C8 F3 0F 11 0A F3 0F 10 49 04 F3 0F 59 48 14 F3 0F 10
// CESITE bytes  AnchorB       0x4B4015  6A 10 51 8D 44 24 30 C7 04 24 00 00 80 3F 50 8B CE E8 D5 76
// CESITE bytes  CacheOffset   0x427719  2B 83 68 02 04 00
#define CE_A_PUSH          0x426CA0u   // release-queue push: ecx = cache, [esp+4] = value, drops a null value, eax = 0, ret 4
#define CE_A_PUSH_INL1     0x4231C2u   // four inline copies of it, 20 bytes each (inc eax where 1.0.8.0 has add eax,1)
#define CE_A_PUSH_INL2     0x4236C2u   // the value is in edi at the first and the fourth, in esi at these two
#define CE_A_PUSH_INL3     0x424082u
#define CE_A_PUSH_INL4     0x4396E2u
#define CE_A_DRAIN         0x42A1F0u   // drain: ecx = cache; saves ebp/ebx/esi/edi
#define CE_A_DRAIN_SITE1   0x422DD8u   // the shutdown flush, inlined into the shutdown tail here
#define CE_A_DRAIN_SITE2   0x4289B4u   // every resource create
#define CE_A_DRAIN_SITE3   0x428B9Cu   // over-budget create path
#define CE_A_DRAIN_SITE4   0x42917Cu   // over-budget create path
#define CE_A_DRAIN_SITE5   0x426ECAu   // the same flush again in the standalone function: 1.0.8.0 has one site for the two
#define CE_A_LINK_SIZE     0xAF48F0u
#define CE_A_LINK_TAKE1    0xAF4719u
#define CE_A_LINK_TAKE2    0xAF4829u
#define CE_A_LINK_FREE1    0xAF4780u   // mov [0x15F8B40], 0: the release when it found a link (the higher address here)
#define CE_A_LINK_FREE2    0xAF4746u   // mov [0x15F8B40], eax (eax = 0): the release when it found none
#define CE_A_LINK_FREE3    0xAF484Cu   // mov [0x15F8B40], 0
#define CE_A_SPIN_ACQUIRE  0x403C10u
#define CE_A_SLOT_SIZE     0xA8ABD0u
#define CE_A_ARENA_SIZE    0x4010D6u   // and 0x4010E9, the game's own smaller arena under about 1.4 GiB of RAM: left alone
#define CE_A_NATIVE_INIT   0x86FC70u   // the native-table init: [esp+4] = capacity, returns the table pointer, ret 4
#define CE_A_NATIVE_SIZE_ST 0x86FC94u  // mov [size], ebx (1.0.8.0: esi), still before the allocation: crash 1 is here too
#define CE_A_NATIVE_PTR_ST 0x86FCA8u
#define CE_A_NATIVE_CNT_ST 0x86FCC6u
#define CE_A_VSTRUCT_NAME  0xA7B107u
#define CE_A_VSTRUCT_SIZE  0xA7B10Cu
#define CE_A_VSTRUCT_STR   0xE9FD60u
#define CE_A_D3D_CALL      0x424423u   // CE calls the import slot directly: there is no jmp-thunk to check
#define CE_A_D3D_IAT       0xE73554u
#define CE_A_RD_UNMANAGED  0x424686u   // cmp [slot], ecx (1.0.8.0: esi)
#define CE_A_RD_MANAGED    0x424699u
#define CE_A_POOL_STORE    0x4246ABu   // mov [0x17ED944], eax: proves the pool-mode address on this build
#define CE_A_RD_VIDMEM     0x420583u
#define CE_A_GAFM          0x4276B0u   // FusionFix's ExtraStreamingMemory detours this entry: never read it for identity
#define CE_A_ANCHOR_A      0x466C40u   // build anchor: inside a vector-maths leaf, 48+ bytes past its entry
#define CE_A_ANCHOR_B      0x4B4015u   // build anchor: float setup in an unrelated function, 340 KB from anchor A
#define CE_A_CACHE_OFF     0x427719u   // sub eax, [ebx+0x40268]: the cache offset the budget is credited against
#define CE_D_CACHE         0x1C58BB0u  // +0x148 array, +0x40148 count, +0x40760 lock, +0x40268 V: all unchanged
#define CE_D_LINK_POOL     0x15F8B48u
#define CE_D_LINK_LOCK     0x15F8B40u
#define CE_D_SLOT_POOL     0x1173758u
#define CE_D_ARENA         0x1BB8A08u
#define CE_D_INIT_GUARD    0x1BB6900u  // bit 1 the first step, 2 the block records, 4 the arena memory: 6 means too late
#define CE_D_NATIVE_PTR    0x1B4AF1Cu
#define CE_D_NATIVE_SIZE   0x1B4AF20u
#define CE_D_NATIVE_COUNT  0x1B4AF28u
#define CE_D_VSTRUCT_POOL  0x12FA84Cu
#define CE_D_SET_VIEW      0x1160E94u
#define CE_D_SET_DETAIL    0x1160E98u
#define CE_D_SET_CARS      0x1160E9Cu
#define CE_D_POOL_MODE     0x17ED944u  // the same, from the block at 0x424686 (CE uses cmovne where 1.0.8.0 branches)
#define CE_A_MEMRESTRICT_TEST 0x427E5Au  // the same test, the same shape
#define CE_D_PARAM_NOMEMRESTRICT 0x110DE48u
#define CE_D_PARAM_MEMRESTRICT 0x110DE18u
#define CE_D_PARAM_UNMANAGED 0x110DDA0u
#define CE_D_PARAM_MANAGED 0x110DCBCu
#define CE_D_PARAM_VIDMEM  0x110DBD4u
#define CE_D_TABLE         0x1064580u
#define CE_D_QUALITY       0x106B56Cu
#define CE_D_RT0           0x17F5980u
#define CE_D_PLAYERINFO    0x11A8808u  // the player's ped is at +0x598 here, not +0x58C
#define CE_D_STREAMER      0x103E8D0u
#define CE_D_LOADING       0x18B6F2Eu
#define CE_D_VEH_BUDGET    0x10496DCu
#define CE_D_PED_BUDGET    0x10496D8u
// CESITE bytes  AudioSlotCap  0x98D50D  83 FF 19
// CESITE bytes  AudioHeap     0x8C158B  BE 00 00 E0 07
#define CE_A_AUDIO_SLOT_CAP 0x98D50Du   // cmp edi, 0x19: the same loop, counting in another register
#define CE_A_AUDIO_HEAP    0x8C158Bu
#define CE_D_AUDIO_SLOTS   0x1283298u

// ---------------------------------------------------------------------------------------------
// Basics
// ---------------------------------------------------------------------------------------------
static uintptr_t g_base;          // the real base of GTAIV.exe
static LARGE_INTEGER g_qpf, g_qpc0;
static inline uintptr_t VA(uint32_t va) { return g_base + (va - 0x400000u); }
static int64_t NowMs() {
    LARGE_INTEGER q; QueryPerformanceCounter(&q);
    return (q.QuadPart - g_qpc0.QuadPart) * 1000 / g_qpf.QuadPart;
}

// Defined by whoever includes this: a line of SISCO.log in the release, a printed line in the tests.
static void SisLog(const char* key, const char* fmt, ...);

// Safe reads. SISCO's own thread never takes a lock that belongs to the game, and every pointer chain into the
// game's memory is guarded: the structures it walks may not exist yet.
static bool RdU32(uintptr_t a, uint32_t* o) { __try { *o = *(volatile uint32_t*)a; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
static bool RdF32(uintptr_t a, float* o) { __try { *o = *(volatile float*)a; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
// A u64 the game updates with add/adc, so a single read can catch it half written: read the pair twice and accept
// only when the two agree, up to eight attempts.
static bool RdU64(uintptr_t a, uint64_t* o) {
    __try {
        for (int i = 0; i < 8; i++) {
            uint32_t lo1 = *(volatile uint32_t*)a, hi1 = *(volatile uint32_t*)(a + 4);
            uint32_t lo2 = *(volatile uint32_t*)a, hi2 = *(volatile uint32_t*)(a + 4);
            if (lo1 == lo2 && hi1 == hi2) { *o = ((uint64_t)hi1 << 32) | lo1; return true; }
        }
        return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool BytesEq(uintptr_t a, const uint8_t* b, size_t n) {
    __try { return memcmp((const void*)a, b, n) == 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool WriteCode(uintptr_t at, const void* src, size_t n) {
    DWORD old;
    if (!VirtualProtect((void*)at, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy((void*)at, src, n);
    DWORD o2; VirtualProtect((void*)at, n, old, &o2);
    FlushInstructionCache(GetCurrentProcess(), (void*)at, n);
    return true;
}
// An aligned 32-bit store into the game's data, from any thread.
static bool WrU32(uintptr_t a, uint32_t v) {
    __try { InterlockedExchange((volatile LONG*)a, (LONG)v); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ---------------------------------------------------------------------------------------------
// Sites: each is byte-checked before it is written. A site that does not match is left alone and its feature stays
// off; the log says which.
// ---------------------------------------------------------------------------------------------
// ST_ABSENT is for a site the running build simply does not have (the Complete Edition has one more inline push and one
// more drain call site than 1.0.8.0): nothing failed, so the log does not mention it.
enum { ST_NOTTRIED = 0, ST_ON, ST_OFF_MISMATCH, ST_OFF_DISABLED, ST_OFF_ERROR, ST_ABSENT };
static const char* StName(int s) {
    switch (s) { case ST_ON: return "ON"; case ST_OFF_MISMATCH: return "OFF_MISMATCH";
    case ST_OFF_DISABLED: return "OFF_DISABLED"; case ST_OFF_ERROR: return "OFF_ERROR";
    case ST_ABSENT: return "NOT_ON_THIS_BUILD"; default: return "NOT_TRIED"; }
}
enum { S_PUSH, S_PUSH_IN1, S_PUSH_IN2, S_PUSH_IN3, S_PUSH_IN4, S_DRAIN1, S_DRAIN2, S_DRAIN3, S_DRAIN4, S_DRAIN5,
       S_LINK_TAKE1, S_LINK_TAKE2, S_LINK_FREE1, S_LINK_FREE2, S_LINK_FREE3, S_NATIVE_INIT, S_VSTRUCT, S_ARENA_SIZE,
       S_SLOT_SIZE, S_LINK_SIZE, S_D3D_IAT, S_COUNT };
// The fourth inline push and the fifth drain site exist only on the Complete Edition; on 1.0.8.0 they are marked
// ST_ABSENT, so the log does not mention them.
static const char* const kSiteName[S_COUNT] = { "QueuePush", "QueuePushIn1", "QueuePushIn2", "QueuePushIn3", "QueuePushIn4",
    "DrainFlush", "DrainCreate", "DrainOver1", "DrainOver2", "DrainFlush2", "LinkTake1", "LinkTake2", "LinkFree1", "LinkFree2",
    "LinkFree3", "NativeInit", "VehicleStruct", "ArenaSize", "SlotSize", "LinkSize", "D3DCreate9" };
struct Site { int state; char note[112]; };
static Site g_site[S_COUNT];
static uint32_t g_raised[S_COUNT];   // what each size raise left at its site, read back at sizing
static void SiteSet(int s, int state, const char* fmt, ...) {
    g_site[s].state = state;
    va_list ap; va_start(ap, fmt); _vsnprintf_s(g_site[s].note, sizeof(g_site[s].note), _TRUNCATE, fmt, ap); va_end(ap);
}
// E8 rel32 call site: verify the opcode and the current target, then retarget.
static bool PatchCall(int s, uintptr_t at, uintptr_t expectTarget, void* hook) {
    uint8_t op = 0; int32_t rel = 0;
    __try { op = *(uint8_t*)at; rel = *(int32_t*)(at + 1); } __except (EXCEPTION_EXECUTE_HANDLER) { SiteSet(s, ST_OFF_ERROR, "unreadable"); return false; }
    if (op != 0xE8 || at + 5 + rel != expectTarget) { SiteSet(s, ST_OFF_MISMATCH, "site target %08X", (unsigned)(at + 5 + rel)); return false; }
    int32_t nrel = (int32_t)((uintptr_t)hook - (at + 5));
    if (!WriteCode(at + 1, &nrel, 4)) { SiteSet(s, ST_OFF_ERROR, "VirtualProtect failed"); return false; }
    SiteSet(s, ST_ON, ""); return true;
}
// n verified bytes become E8 (call a stub that ends in ret) or E9 (jump to a stub that ends in the function's own
// branches), padded with NOPs. Only for sites where no branch lands inside the n bytes.
static bool PatchBytes(int s, uintptr_t at, const uint8_t* expect, size_t n, void* stub, uint8_t op) {
    if (n < 5 || n > 16 || (op != 0xE8 && op != 0xE9)) { SiteSet(s, ST_OFF_ERROR, "bad patch"); return false; }
    if (!BytesEq(at, expect, n)) { SiteSet(s, ST_OFF_MISMATCH, "site bytes differ at %08X", (unsigned)at); return false; }
    uint8_t nb[16]; nb[0] = op; int32_t rel = (int32_t)((uintptr_t)stub - (at + 5)); memcpy(nb + 1, &rel, 4);
    for (size_t i = 5; i < n; i++) nb[i] = 0x90;
    if (!WriteCode(at, nb, n)) { SiteSet(s, ST_OFF_ERROR, "VirtualProtect failed"); return false; }
    SiteSet(s, ST_ON, ""); return true;
}

// The two builds. Everything SISCO patches is the same code, but the Complete Edition's compiler picked other registers
// and other encodings for some of it, so the shapes that differ are carried in the site table.
enum { BUILD_1080 = 0, BUILD_CE = 1 };
// One instruction that touches an absolute address, as each build emitted it. The address is the loader's, so the
// expected bytes are built at run time and a site is only ever written when it matches them exactly.
enum { F_MOV_ESI, F_MOV_EBX, F_MOV_EAX, F_MOV_IMM0, F_CMP_ESI, F_CMP_ECX, F_LOAD_EAX };
static size_t InsnForm(uint8_t* out, int form, uint32_t addr) {
    switch (form) {
    case F_MOV_ESI:  out[0] = 0x89; out[1] = 0x35; memcpy(out + 2, &addr, 4); return 6;   // mov [addr], esi
    case F_MOV_EBX:  out[0] = 0x89; out[1] = 0x1D; memcpy(out + 2, &addr, 4); return 6;   // mov [addr], ebx
    case F_MOV_EAX:  out[0] = 0xA3; memcpy(out + 1, &addr, 4); return 5;                  // mov [addr], eax
    case F_MOV_IMM0: out[0] = 0xC7; out[1] = 0x05; memcpy(out + 2, &addr, 4); memset(out + 6, 0, 4); return 10;
    case F_CMP_ESI:  out[0] = 0x39; out[1] = 0x35; memcpy(out + 2, &addr, 4); return 6;   // cmp [addr], esi
    case F_CMP_ECX:  out[0] = 0x39; out[1] = 0x0D; memcpy(out + 2, &addr, 4); return 6;   // cmp [addr], ecx
    case F_LOAD_EAX: out[0] = 0xA1; memcpy(out + 1, &addr, 4); return 5;                  // mov eax, [addr]
    }
    return 0;
}
static bool FormEq(uintptr_t at, int form, uint32_t addr) {
    uint8_t want[10]; size_t n = InsnForm(want, form, addr);
    return n != 0 && BytesEq(at, want, n);
}

// ---------------------------------------------------------------------------------------------
// The release-queue fix. The game's array stays exactly as it
// is; when it is full, a push goes to a spill list SISCO owns, and every drain feeds the spill back in and drains again
// until it is empty. Nothing is released earlier than the game would, and no new thread or lock order is added:
// spill writers hold the queue lock (+0x40760) when it exists, and the spill lock is a leaf taken after it. Never
// freed and never unpatched: the game still pushes and drains during shutdown.
// ---------------------------------------------------------------------------------------------
// Four times the game's own queue. Nothing measured has come close: the largest burst ever recorded was 66,133
// entries, at the moment of quitting a game loaded to its limit. A push past this is dropped, which leaks that one
// object rather than writing past the array.
#define SPILL_CAP 262144
static uint32_t g_spill[SPILL_CAP];
static volatile LONG g_spillN;
static CRITICAL_SECTION g_spillCs;
static uintptr_t g_cacheAddr, g_oDrain;
static void* g_pEnterCs = (void*)EnterCriticalSection;
static void* g_pLeaveCs = (void*)LeaveCriticalSection;
static int g_fixInstalled;
// The count at which the fix treats the queue as full: the game's own, and the only value it ever has.
#define QUEUE_CAP 0x10000u
static __declspec(thread) uint32_t t_drainCache, t_drainLocked;
extern "C" void __cdecl SisPushCore(uint32_t cache, uint32_t value) {
    volatile uint32_t* cnt = (volatile uint32_t*)(uintptr_t)(cache + 0x40148);
    uint32_t c = *cnt;
    if (c < QUEUE_CAP) { ((volatile uint32_t*)(uintptr_t)(cache + 0x148))[c] = value; *cnt = c + 1; return; }
    EnterCriticalSection(&g_spillCs);
    if (g_spillN < SPILL_CAP) g_spill[g_spillN++] = value;   // past that one object leaks; nothing is overwritten
    LeaveCriticalSection(&g_spillCs);
}
extern "C" void __cdecl SisInlinePush(uint32_t value) {
    SisPushCore((uint32_t)g_cacheAddr, value);
}
// 0x411450 entry -> jmp: the game's own push, bounded. esi = cache, [esp+4] = value, al = 1, ret 4, edi kept.
__declspec(naked) static void HkPush() {
    __asm {
        push edi
        lea edi, [esi + 0x40760]
        cmp dword ptr [edi], 0
        je nolock
        push edi
        call dword ptr [g_pEnterCs]
    nolock:
        mov eax, [esp + 8]
        push eax
        push esi
        call SisPushCore
        add esp, 8
    }
    __asm {
        cmp dword ptr [edi], 0
        je nounlock
        push edi
        call dword ptr [g_pLeaveCs]
    nounlock:
        mov al, 1
        pop edi
        ret 4
    }
}
// The Complete Edition's push (0x426CA0): ecx = cache, [esp+4] = value, a null value is dropped before the lock is
// taken, and it returns 0 (1.0.8.0 returns 1). ebx and edi are the game's to keep, so they are saved as its own code
// saves them; the cache stays in ebx because EnterCriticalSection may clobber ecx.
__declspec(naked) static void HkPushCe() {
    __asm {
        push ebx
        push edi
        mov ebx, ecx
        mov edi, [esp + 12]             // the value: esp+0 edi, +4 ebx, +8 the return address, +12 the argument
        test edi, edi
        je done
        cmp dword ptr [ebx + 0x40760], 0
        je nolock
        lea eax, [ebx + 0x40760]
        push eax
        call dword ptr [g_pEnterCs]
    nolock:
        push edi
        push ebx
        call SisPushCore
        add esp, 8
    }
    __asm {
        cmp dword ptr [ebx + 0x40760], 0
        je done
        lea eax, [ebx + 0x40760]
        push eax
        call dword ptr [g_pLeaveCs]
    done:
        xor eax, eax
        pop edi
        pop ebx
        ret 4
    }
}
// The inline copies of the push, 22 bytes on 1.0.8.0 and 20 on the Complete Edition -> call + jmp over the rest.
// The value is in esi here and in edi in HkInlinePushEdi; the caller holds the lock. Every register, the flags and
// the x87/SSE state are kept, because the compiler did not expect a call at this point.
__declspec(naked) static void HkInlinePush() {
    __asm {
        pushad
        pushfd
        mov ebp, esp
        sub esp, 512
        and esp, 0xFFFFFFF0
        fxsave [esp]
        push esi
        call SisInlinePush
        add esp, 4
        fxrstor [esp]
        mov esp, ebp
        popfd
        popad
        ret
    }
}
// Two of the Complete Edition's four inline copies hold the value in edi instead of esi.
__declspec(naked) static void HkInlinePushEdi() {
    __asm {
        pushad
        pushfd
        mov ebp, esp
        sub esp, 512
        and esp, 0xFFFFFFF0
        fxsave [esp]
        push edi
        call SisInlinePush
        add esp, 4
        fxrstor [esp]
        mov esp, ebp
        popfd
        popad
        ret
    }
}
// The drain as a cdecl function (eax = cache; it keeps esi/edi/ebp and ebx), for the refill loop.
__declspec(naked) static void __cdecl CallDrain1080(uint32_t) {
    __asm {
        mov eax, [esp + 4]
        call dword ptr [g_oDrain]
        ret
    }
}
// The Complete Edition's drain takes the cache in ecx.
__declspec(naked) static void __cdecl CallDrainCe(uint32_t) {
    __asm {
        mov ecx, [esp + 4]
        call dword ptr [g_oDrain]
        ret
    }
}
static void (__cdecl* g_callDrain)(uint32_t) = CallDrain1080;
static void CallDrain(uint32_t cache) { g_callDrain(cache); }
static void SpillRefill(uint32_t cache) {
    for (int guard = 0; guard < 64; guard++) {
        EnterCriticalSection(&g_spillCs);
        volatile uint32_t* cnt = (volatile uint32_t*)(uintptr_t)(cache + 0x40148);
        uint32_t c = *cnt, room = c < QUEUE_CAP ? QUEUE_CAP - c : 0;
        uint32_t n = (uint32_t)g_spillN < room ? (uint32_t)g_spillN : room;
        volatile uint32_t* arr = (volatile uint32_t*)(uintptr_t)(cache + 0x148);
        for (uint32_t i = 0; i < n; i++) arr[c + i] = g_spill[g_spillN - n + i];
        *cnt = c + n; g_spillN -= n;
        LeaveCriticalSection(&g_spillCs);
        if (!n) return;
        CallDrain(cache);
    }
}
// Around every drain: the queue lock (recursive; the drain takes it again on this thread) is held from before the
// drain until the spill has been fed back, so no push can slip between the drain and the refill.
extern "C" void __cdecl SisDrainPre(uint32_t cache) {
    uint32_t cs = 0; RdU32(cache + 0x40760, &cs);
    t_drainLocked = cs != 0;
    if (t_drainLocked) EnterCriticalSection((LPCRITICAL_SECTION)(uintptr_t)(cache + 0x40760));
    t_drainCache = cache;
}
extern "C" void __cdecl SisDrainPost() {
    if (g_fixInstalled && g_spillN > 0) SpillRefill(t_drainCache);   // still under the queue lock taken in Pre
    if (t_drainLocked) LeaveCriticalSection((LPCRITICAL_SECTION)(uintptr_t)(t_drainCache + 0x40760));
}
// E8 -> the drain at each of its call sites: the cache comes in ecx on the Complete Edition, eax on 1.0.8.0; al out,
// ebx/esi/edi/ebp preserved (pushad keeps ebx, which the Complete Edition's drain also uses).
__declspec(naked) static void ThunkDrainCe() {
    __asm {
        pushad
        push ecx
        call SisDrainPre
        add esp, 4
        popad
        call dword ptr [g_oDrain]
        pushfd
        pushad
        call SisDrainPost
        popad
        popfd
        ret
    }
}
__declspec(naked) static void ThunkDrain() {
    __asm {
        pushad
        push eax
        call SisDrainPre
        add esp, 4
        popad
        call dword ptr [g_oDrain]
        pushfd
        pushad
        call SisDrainPost
        popad
        popfd
        ret
    }
}

// ---------------------------------------------------------------------------------------------
// The lock fix. Measured: 2.2 million nested takes went through it with no freeze, and the same test without it
// froze on the first one. The spinlock's two takes come to LinkLockTake: the thread that already holds the lock goes
// through one level deeper, every other thread takes it exactly as before (the game's own 0x456320). The three releases
// come to StubLinkFree1..3: only the outermost one frees it. Owner and depth are written only by the thread that holds
// the lock, so another thread can never read its own id there.
// ---------------------------------------------------------------------------------------------
typedef void (__cdecl* SpinAcquire_t)(volatile LONG*);
static uintptr_t g_spinAcquire, g_linkLockAddr;
static volatile DWORD g_linkLockOwner;
static LONG g_linkLockDepth;
static int g_linkFixInstalled;
static void __cdecl LinkLockTake(volatile LONG* lock) {
    DWORD me = GetCurrentThreadId();
    if (g_linkLockOwner == me) {
        g_linkLockDepth++;
        return;
    }
    ((SpinAcquire_t)g_spinAcquire)(lock);
    g_linkLockOwner = me; g_linkLockDepth = 1;
}
static void LinkLockGive() {
    if (g_linkLockOwner != GetCurrentThreadId()) {     // never expected: release it as the game did
        InterlockedExchange((volatile LONG*)g_linkLockAddr, 0);
        return;
    }
    if (--g_linkLockDepth > 0) return;
    g_linkLockDepth = 0; g_linkLockOwner = 0;
    InterlockedExchange((volatile LONG*)g_linkLockAddr, 0);
}
__declspec(naked) static void ThunkLinkTake1() { __asm { jmp LinkLockTake } }
// Called in place of the release (E8 + NOPs): every register and the flags come back as they were.
__declspec(naked) static void StubLinkFree1() {
    __asm { pushfd } __asm { pushad } __asm { call LinkLockGive } __asm { popad } __asm { popfd } __asm { ret }
}

// ---------------------------------------------------------------------------------------------
// The native-table race fix, the startup crash. FusionFix's frame limiter looks a native up from the loading-screen
// thread while the main thread is inside 0x626A80, which stores the table's size before it allocates the table and its
// pointer after: FusionFix reads a NULL pointer with a valid size. The replacement allocates the same way (the game's
// TLS allocator, vtable slot 2, 16-byte aligned), zeroes the whole table (the handler slots too: the registrar writes
// the key before the handler), sets the count to 0, publishes the pointer, and stores the size last; no size when the
// allocation failed (the game's own code would crash there). It returns what the game's did.
// ---------------------------------------------------------------------------------------------
typedef void* (__thiscall* TlsAlloc_t)(void* self, uint32_t size, uint32_t align, uint32_t flags);
static uintptr_t g_nativePtr, g_nativeSize, g_nativeCount;   // the three globals, rebased
extern "C" uint32_t __cdecl SisNativeTableInit(int32_t cap) {
    uintptr_t tls = *(uintptr_t*)(uintptr_t)__readfsdword(0x2C);   // the exe's TLS block (index 0), as the game reads it
    void* alloc = *(void**)(tls + 8);
    uint64_t want = (uint64_t)(uint32_t)cap * 8;
    uint32_t bytes = want > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)want;   // the game's seto/neg/or: overflow asks for 4 GB
    void* p = ((TlsAlloc_t)((*(uintptr_t**)alloc)[2]))(alloc, bytes, 0x10, 0);
    if (p && cap > 0) memset(p, 0, (size_t)cap * 8);
    *(volatile uint32_t*)g_nativeCount = 0;
    MemoryBarrier();
    *(volatile uint32_t*)g_nativePtr = (uint32_t)(uintptr_t)p;
    MemoryBarrier();
    if (p) *(volatile uint32_t*)g_nativeSize = (uint32_t)cap;
    return cap > 0 ? (uint32_t)cap : 0;
}
// jmp from 0x626A80: esi = capacity. ebx, esi, edi and ebp are kept (cdecl); eax, ecx, edx are the game's scratch too.
__declspec(naked) static void ThunkNativeInit() {
    __asm {
        push esi
        call SisNativeTableInit
        add esp, 4
        ret
    }
}
// The Complete Edition's takes the capacity as a stack argument, returns the table's pointer and ends with ret 4.
__declspec(naked) static void ThunkNativeInitCe() {
    __asm {
        push dword ptr [esp + 4]
        call SisNativeTableInit
        add esp, 4
        mov eax, dword ptr [g_nativePtr]
        mov eax, dword ptr [eax]
        ret 4
    }
}

// ---------------------------------------------------------------------------------------------
// Launch-time settings, each written once before the game reads it
// ---------------------------------------------------------------------------------------------
struct CoreSites {                                                    // rebased; the harness fills it with mocks
    int build;                                                        // BUILD_1080 or BUILD_CE: the shapes that differ
    uintptr_t cache, push, pushIn[4], drain, drainSite[5];            // the release queue
    int pushInN, pushInLen, drainSiteN;                               // 3, 22, 4 on 1.0.8.0; 4, 20, 5 on the CE
    uint8_t pushInEdi[4];                                             // 1 where the inline copy holds the value in edi
    uintptr_t linkLock, spinAcquire, linkTake[2], linkFree[3];        // the lock fix
    uint8_t linkFreeForm[3];                                          // how each release writes the lock (InsnForm)
    uintptr_t slotSize, slotPool, linkSize, linkPool;                 // the two pools
    uintptr_t arenaSize, arenaObj, initGuard;                         // the arena
    uint32_t initGuardMask;                                           // the guard bits that say the size was already read
    uintptr_t nativeInit, nativeSizeSt, nativePtrSt, nativeCountSt, nativePtr, nativeSize, nativeCount;
    uint8_t nativeSizeForm;                                           // the size store: esi on 1.0.8.0, ebx on the CE
    uintptr_t vstructName, vstructSize, vstructPool, vstructNameStr;  // VehicleStruct
    uintptr_t d3dIat, d3dThunk, d3dCall, rdUnmanaged, rdManaged, rdVidmem, prmUnmanaged, prmManaged, prmVidmem;
    uint8_t paramCmpForm;                                             // the two flag readers: cmp against esi or ecx
    uintptr_t table, quality, rt0, cacheV, streamerN, loading, vehBudget, pedBudget, gafm, playerInfo;   // read and written in play
    uintptr_t poolMode;            // what the game DECIDED about managed resources, not what SISCO asked for
    uintptr_t memRestrictTest, prmNoMemRestrict, prmMemRestrict;   // which budget row the game runs on, asked its way
    uintptr_t setView, setDetail, setCars;   // the player's View Distance, Detail Distance and Car Density: logged, never written
    uint32_t playerPedOff;                                            // CPlayerInfo -> the player's ped: 0x58C, 0x598 on the CE
    uintptr_t audioSlotCap, audioHeap, audioSlots;                    // the engine-sound slots: read, never written
    uintptr_t frameCounter, frameInc;                                 // the game's frame counter and the add that advances it, for the load line; 0 = not on this build
    uint8_t audioCapModRm;                                            // the loop counts in eax (0xF8) or, on the CE, edi (0xFF)
};
static CoreSites g_cs;

// The shapes, apart from the addresses: what each build's compiler emitted where SISCO patches. Set on a site table
// before it is used, by the two real tables below and by the harness's mocks.
static void SitesShape1080(CoreSites& c) {
    c.build = BUILD_1080;
    c.pushInN = 3; c.pushInLen = 22; c.drainSiteN = 4;
    for (int i = 0; i < 4; i++) c.pushInEdi[i] = 0;
    c.linkFreeForm[0] = F_MOV_EBX; c.linkFreeForm[1] = F_MOV_EBX; c.linkFreeForm[2] = F_MOV_IMM0;
    c.initGuardMask = 4; c.nativeSizeForm = F_MOV_ESI; c.paramCmpForm = F_CMP_ESI; c.playerPedOff = 0x58Cu;
    c.audioCapModRm = 0xF8;
}
static void SitesShapeCe(CoreSites& c) {
    c.build = BUILD_CE;
    c.pushInN = 4; c.pushInLen = 20; c.drainSiteN = 5;
    c.pushInEdi[0] = 1; c.pushInEdi[1] = 0; c.pushInEdi[2] = 0; c.pushInEdi[3] = 1;   // the first and the fourth store edi
    c.linkFreeForm[0] = F_MOV_IMM0; c.linkFreeForm[1] = F_MOV_EAX; c.linkFreeForm[2] = F_MOV_IMM0;
    c.initGuardMask = 6; c.nativeSizeForm = F_MOV_EBX; c.paramCmpForm = F_CMP_ECX; c.playerPedOff = 0x598u;
    c.audioCapModRm = 0xFF;
}

// A pool or arena size written as an imm32 (op xx xx xx xx) that the game reads once when it builds that pool. Raised to
// at least `want`, never lowered: another mod's larger value stands. Only while `built` says the game has not read it.
static bool RaiseImm32(int s, uintptr_t at, uint8_t op, uint32_t want, uint32_t lo, uint32_t hi, bool built) {
    uint8_t o = 0; uint32_t v = 0;
    __try { o = *(uint8_t*)at; v = *(uint32_t*)(at + 1); } __except (EXCEPTION_EXECUTE_HANDLER) { SiteSet(s, ST_OFF_ERROR, "unreadable"); return false; }
    if (built) { SiteSet(s, ST_OFF_ERROR, "too late: the game already built it"); return false; }
    if (o != op || v < lo || v > hi) { SiteSet(s, ST_OFF_MISMATCH, "not the size instruction (%02X %u)", o, v); return false; }
    if (v >= want) { g_raised[s] = v; SiteSet(s, ST_ON, "kept %u (already at least %u)", v, want); return true; }
    if (!WriteCode(at + 1, &want, 4)) { SiteSet(s, ST_OFF_ERROR, "VirtualProtect failed"); return false; }
    g_raised[s] = want; SiteSet(s, ST_ON, "%u -> %u", v, want); return true;
}
static bool RaisePools(const CoreSites& c, uint32_t slots, uint32_t links) {
    uint32_t head = 1, nodes = 1, first = 1;
    RdU32(c.slotPool + 0x20, &head); RdU32(c.linkPool + 0x50, &nodes); RdU32(c.linkPool + 0x38, &first);
    bool a = RaiseImm32(S_SLOT_SIZE, c.slotSize, 0x68, slots, 1000, 200000, head != 0);
    bool b = RaiseImm32(S_LINK_SIZE, c.linkSize, 0x68, links, 1000, 200000, nodes != 0 || first != 0);
    return a && b;
}
static bool RaiseArena(const CoreSites& c, uint32_t mib) {
    uint32_t vt = 1, guard = 1;
    RdU32(c.arenaObj, &vt); RdU32(c.initGuard, &guard);
    return RaiseImm32(S_ARENA_SIZE, c.arenaSize, 0xBF, mib * 1024u, 32u * 1024u, 1000u * 1024u,
                      vt != 0 || (guard & c.initGuardMask) != 0);
}
// VehicleStruct: push imm8 (signed, at most 0x7F), right after the push of the pool's name.
static bool RaiseVehicleStruct(const CoreSites& c, uint8_t want) {
    uint8_t name[5] = { 0x68 }; uint32_t np = (uint32_t)c.vstructNameStr; memcpy(name + 1, &np, 4);
    uint8_t b[4] = { 0 }; uint32_t pool = 1;
    RdU32(c.vstructPool, &pool);
    __try { memcpy(b, (const void*)c.vstructSize, 4); } __except (EXCEPTION_EXECUTE_HANDLER) { SiteSet(S_VSTRUCT, ST_OFF_ERROR, "unreadable"); return false; }
    if (!BytesEq(c.vstructName, name, 5) || b[0] != 0x6A || b[2] != 0x8B || b[3] != 0xC8 || b[1] > 0x7F) {
        SiteSet(S_VSTRUCT, ST_OFF_MISMATCH, "site bytes differ at %08X", (unsigned)c.vstructSize); return false;
    }
    if (pool != 0) { SiteSet(S_VSTRUCT, ST_OFF_ERROR, "too late: the pool exists"); return false; }
    if (b[1] >= want) { g_raised[S_VSTRUCT] = b[1]; SiteSet(S_VSTRUCT, ST_ON, "kept %u", b[1]); return true; }
    if (!WriteCode(c.vstructSize + 1, &want, 1)) { SiteSet(S_VSTRUCT, ST_OFF_ERROR, "VirtualProtect failed"); return false; }
    g_raised[S_VSTRUCT] = want; SiteSet(S_VSTRUCT, ST_ON, "%u -> %u", b[1], want); return true;
}
// The four raises are writes SISCO trusted took, and three things undo them with nothing to see at the write: a
// plugin that loads after SISCO and writes the same site (FusionFix sets the link pool to 20,000 and IVTweaker's
// MaxGameHeap the arena, each without looking), a plugin that hooks the VehicleStruct constructor call and passes
// its own count (IVTweaker's MaxVehicleStruct), and a load order that puts either after SISCO. So at sizing, with
// every plugin loaded and every pool built, each site is read back: the size instruction the game read, and for
// VehicleStruct the count the pool was built with (CPool +8 on both builds) and the constructor call itself. A site
// that no longer holds at least what SISCO left is marked, and the budget then stays the game's own exactly as if
// the raise had been refused at load. A larger value is another plugin's and stands.
static const uint8_t kVsCtorCall1080[5] = { 0xE8, 0xF7, 0x9A, 0xDA, 0xFF };   // call 0x7F3DB0 at 0xA4A2B4
static const uint8_t kVsCtorCallCe[5]   = { 0xE8, 0xDB, 0x14, 0x1F, 0x00 };   // call 0xC6C5F0 at 0xA7B110
static char g_builtNote[200] = "";
static void ReadBackRaises(const CoreSites& c) {
    const int s3[3] = { S_SLOT_SIZE, S_LINK_SIZE, S_ARENA_SIZE };
    const uintptr_t at3[3] = { c.slotSize, c.linkSize, c.arenaSize };
    uint32_t v3[3] = { 0, 0, 0 };
    for (int i = 0; i < 3; i++) {
        if (g_site[s3[i]].state != ST_ON) continue;
        if (!RdU32(at3[i] + 1, &v3[i])) SiteSet(s3[i], ST_OFF_ERROR, "unreadable at sizing");
        else if (v3[i] < g_raised[s3[i]])
            SiteSet(s3[i], ST_OFF_MISMATCH, "read back %u at sizing, not the %u SISCO left: another plugin set it after SISCO", v3[i], g_raised[s3[i]]);
        if (g_site[s3[i]].state != ST_ON) SisLog("site", "%s %s (%s)", kSiteName[s3[i]], StName(g_site[s3[i]].state), g_site[s3[i]].note);
    }
    uint32_t pool = 0, built = 0;
    if (g_site[S_VSTRUCT].state == ST_ON) {
        if (!RdU32(c.vstructPool, &pool) || !pool || !RdU32(pool + 8, &built)) SiteSet(S_VSTRUCT, ST_OFF_ERROR, "the pool cannot be read at sizing");
        else if (built < g_raised[S_VSTRUCT])
            SiteSet(S_VSTRUCT, ST_OFF_MISMATCH, "built with %u, not the %u SISCO left: %s", built, g_raised[S_VSTRUCT],
                    BytesEq(c.vstructSize + 4, c.build == BUILD_CE ? kVsCtorCallCe : kVsCtorCall1080, 5)
                        ? "another plugin set the size after SISCO" : "the constructor call is hooked (IVTweaker's MaxVehicleStruct)");
        if (g_site[S_VSTRUCT].state != ST_ON) SisLog("site", "%s %s (%s)", kSiteName[S_VSTRUCT], StName(g_site[S_VSTRUCT].state), g_site[S_VSTRUCT].note);
    }
    _snprintf_s(g_builtNote, sizeof(g_builtNote), _TRUNCATE, "drawable slots %u, links %u, arena %u KB, VehicleStruct pool %u (read back at sizing; 0 where a raise was not on)",
                v3[0], v3[1], v3[2], built);
}
// The race fix goes in only when the whole function is the one read: its entry and its three stores.
static bool InstallRaceFix(const CoreSites& c) {
    static const uint8_t entry[6] = { 0x64, 0xA1, 0x2C, 0x00, 0x00, 0x00 };   // mov eax, fs:[0x2C]: both builds
    if (!FormEq(c.nativeSizeSt, c.nativeSizeForm, (uint32_t)c.nativeSize) ||
        !FormEq(c.nativePtrSt, F_MOV_EAX, (uint32_t)c.nativePtr) ||
        !FormEq(c.nativeCountSt, F_MOV_IMM0, (uint32_t)c.nativeCount)) {
        SiteSet(S_NATIVE_INIT, ST_OFF_MISMATCH, "the function's stores differ"); return false;
    }
    g_nativePtr = c.nativePtr; g_nativeSize = c.nativeSize; g_nativeCount = c.nativeCount;
    void* thunk = c.build == BUILD_CE ? (void*)ThunkNativeInitCe : (void*)ThunkNativeInit;
    return PatchBytes(S_NATIVE_INIT, c.nativeInit, entry, 6, thunk, 0xE9);
}

// ---------------------------------------------------------------------------------------------
// The card's video memory: the system dxgi.dll by full path, the adapter with the most dedicated memory,
// IDXGIAdapter3::QueryVideoMemoryInfo(LOCAL): this process's budget. MEASURED: equal to Vulkan's budget through DXVK
// (measured equal to Vulkan's own budget through DXVK on a 24 GB card). DedicatedVideoMemory is capped at 3 GB for
// 32-bit callers; the budget is not.
// ---------------------------------------------------------------------------------------------
static IDXGIAdapter3* g_dxgiAd; static int g_dxgiState;    // 0 untried, 1 ok, -1 failed
typedef HRESULT (WINAPI* CreateDXGIFactory1_t)(REFIID, void**);
static void DxgiInit() {
    g_dxgiState = -1;
    wchar_t p[MAX_PATH]; UINT n = GetSystemDirectoryW(p, MAX_PATH);
    if (!n || n > MAX_PATH - 12) return;
    wcscat_s(p, L"\\dxgi.dll");
    HMODULE m = LoadLibraryW(p); if (!m) { SisLog("vram.dxgi", "system dxgi.dll did not load (%lu)", GetLastError()); return; }
    CreateDXGIFactory1_t create = (CreateDXGIFactory1_t)GetProcAddress(m, "CreateDXGIFactory1");
    IDXGIFactory1* fac = NULL;
    if (!create || FAILED(create(__uuidof(IDXGIFactory1), (void**)&fac)) || !fac) { SisLog("vram.dxgi", "CreateDXGIFactory1 failed"); return; }
    IDXGIAdapter1* best = NULL; SIZE_T bestMem = 0; DXGI_ADAPTER_DESC1 bestDesc = {};
    for (UINT i = 0; i < 16; i++) {
        IDXGIAdapter1* a = NULL; if (fac->EnumAdapters1(i, &a) != S_OK || !a) break;
        DXGI_ADAPTER_DESC1 d = {}; a->GetDesc1(&d);
        if (!(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && d.DedicatedVideoMemory > bestMem) {
            if (best) best->Release();
            best = a; bestMem = d.DedicatedVideoMemory; bestDesc = d;
        } else a->Release();
    }
    fac->Release();
    if (!best) { SisLog("vram.dxgi", "no hardware adapter"); return; }
    IDXGIAdapter3* a3 = NULL;
    HRESULT hr = best->QueryInterface(__uuidof(IDXGIAdapter3), (void**)&a3);
    best->Release();
    if (FAILED(hr) || !a3) { SisLog("vram.dxgi", "IDXGIAdapter3 not available (hr %08X)", (unsigned)hr); return; }
    g_dxgiAd = a3; g_dxgiState = 1;
    // The description is 128 wide characters, so its UTF-8 form can need more than 128 bytes; on failure the call
    // leaves the buffer without a terminator, and the log line would read off the end of it.
    char name[256] = "?";
    if (!WideCharToMultiByte(CP_UTF8, 0, bestDesc.Description, -1, name, sizeof(name), NULL, NULL)) strcpy_s(name, "?");
    SisLog("vram.dxgi", "%s, LUID %08X:%08X", name, (unsigned)bestDesc.AdapterLuid.HighPart, (unsigned)bestDesc.AdapterLuid.LowPart);
}
static bool DxgiPoll(int64_t* budget) {
    if (g_dxgiState != 1) return false;
    DXGI_QUERY_VIDEO_MEMORY_INFO i = {};
    if (FAILED(g_dxgiAd->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &i))) return false;
    *budget = (int64_t)i.Budget; return true;
}

// ---------------------------------------------------------------------------------------------
// At the game's Direct3DCreate9: detect DXVK, read the card's budget, and with DXVK give the game
// -managed (unless the player passed -managed or -unmanaged) and -availablevidmem = the budget in MiB (unless the
// player passed it; at most 8192). The option slots are written, not the globals: the game re-reads the slot at every device reset,
// keeps its own precedence rules, and the player's commandline.txt is still read (a process argument would skip it).
// ---------------------------------------------------------------------------------------------
static const GUID kIID_D3D9VkInteropInterface = { 0x3461a81b, 0xce41, 0x485b, { 0xb6, 0xb5, 0xfc, 0xf0, 0x8b, 0xa6, 0xa6, 0xbd } };
typedef void* (WINAPI* D3DCreate9_t)(UINT);
static D3DCreate9_t g_oD3DCreate9;
static volatile LONG g_d3dCalls;
// The switches from SISCO.ini, all on unless the file says otherwise. There are no values here on purpose:
// nothing to set means nothing to set wrongly, and each switch only ever moves the game back towards vanilla.
struct SisSettings { bool enabled, fixes, limits, budget; };
static SisSettings g_set = { true, true, true, true };

// SISCO.ini, beside the plugin and its log. Every key defaults to ON, so a missing file, a missing key, a typo
// or an unreadable value all leave the mod behaving exactly as it does with no file at all. Enabled=0 turns the
// other three off with it, so there is no way to ask for "off" and still get some of it.
// Only a plain 0 turns something off. GetPrivateProfileInt would read any value it cannot parse as zero, so
// "off", "no", "false" or a typo would each silently disable a group, which is the one surprise a file like this
// must not have. Read as text, and anything that is not exactly 0 leaves that part on.
static bool IniSwitch(const char* iniPath, const char* key) {
    char v[16] = "";
    GetPrivateProfileStringA("SISCO", key, "1", v, sizeof(v), iniPath);
    const char* p = v;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '0') return true;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    return *p != 0 && *p != ';' && *p != '#';         // a comment after the 0 is still a 0
}
static SisSettings ReadSettings(const char* iniPath) {
    SisSettings s;
    s.enabled = IniSwitch(iniPath, "Enabled");
    s.fixes = IniSwitch(iniPath, "Fixes");
    s.limits = IniSwitch(iniPath, "Limits");
    s.budget = IniSwitch(iniPath, "Budget");
    if (!s.enabled) s.fixes = s.limits = s.budget = false;
    return s;
}

static int g_dxvk = -1;                  // -1 not known yet, 0 native Direct3D 9, 1 DXVK
static int64_t g_bootBudgetMb = -1;      // the card's budget when the game created Direct3D
static char g_vidmemArg[16];
static int g_playerVidmemMb;             // the player's own -availablevidmem, 0 if they set none

// The figure given to the game: the card's budget, at most 8192 MiB. Above that nothing changes (T + X is at most
// 4,799 MB at 4K), and 8192 is the figure every measured run above the stock budget used: the tested range.
#define VIDMEM_MAX_MB 8192
// The option slot holds a pointer to the argument text, which is how SISCO writes its own. Read it back as a
// number, and refuse anything that is not plainly one: a figure that bounds the whole sizing is not worth
// guessing at. The range is the one the game is given anyway, 64 MB up to the 8192 SISCO would write.
static int ReadVidmemArg(uint32_t p) {
    if (!p) return 0;
    char s[16] = "";
    __try {
        const char* src = (const char*)(uintptr_t)p;
        for (int i = 0; i < (int)sizeof(s) - 1; i++) { s[i] = src[i]; if (!src[i]) break; }
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    int n = 0, digits = 0;
    for (int i = 0; s[i] && s[i] != ' '; i++) {
        if (s[i] < '0' || s[i] > '9') return 0;
        n = n * 10 + (s[i] - '0');
        if (++digits > 5) return 0;
    }
    return (digits && n >= 64 && n <= VIDMEM_MAX_MB) ? n : 0;
}
static const char g_flagArg[1] = "";     // a flag's value: the game only tests the slot for non-NULL
static char g_d3dNote[560] = "Direct3DCreate9 not called yet";
static bool VulkanLoaded() { return GetModuleHandleA("vulkan-1.dll") != NULL; }
// DXVK's version, for the log only. It once gated -managed, on the belief that the one slow world build measured on
// a heavy install (23 s on 2.6.2) was 2.x's doing; the clean install then built the world in 61 s on 3.1.1 and 44 s
// on 2.6.2 under a 30 fps cap with -managed, and in 2 s under the same cap without it (runs L4, L5, L6). The cap is
// the ingredient, not the version. The version is the module's own marker, a standalone "v<major>.<minor>[.<patch>]"
// between two NULs (v3.1.1 and v2.6.2 in those files on disk, none in FusionFix's d3d9.dll proxy or the system
// d3d9.dll), looked for in the module that built the Direct3D object first, then in vulkan.dll and d3d9.dll by
// name, because a wrapper (ReShade, ENB) owns that object on some installs.
static int g_dxvkVer[3] = { -1, -1, -1 };          // -1: not found
static char g_dxvkVerFrom[48] = "";                // the module the version was read from, for the log
// A frame-rate cap. -managed is what makes a raised budget safe under DXVK (without it the address space collapses
// at a budget of 1400, run D), and under a cap a load with -managed is a slow one: the world build after the player
// appears is paced by presents that block, about 300 frames of them (5 fps and 61 s at a 30 fps cap, 9 fps and 30 s
// at 60, runs L4 and L7). SISCO can see DXVK's own cap and a VSync forced in DXVK's config (read by SIS.cpp before
// the game starts, with the parser below) and whether RTSS is loaded; it cannot see the driver's cap or RTSS's
// figure. Any visible cap, or a forced interval, means -managed is not written and the budget stays the game's own,
// whoever asked for -managed: the immediate present below bypasses neither. Everything else is logged, with the
// frame rate of the build where the game's counter is known, so a report carries it.
static int g_frameCap;                             // a visible DXVK cap in fps, 0 = none seen; set by SIS.cpp before Direct3DCreate9
static int g_forcedInterval = -1;                  // d3d9.presentInterval from DXVK's config; -1 = not set, 1 or more = VSync forced
static bool g_managedOn;                           // -managed is in effect after Direct3DCreate9: SISCO's or the player's
static char g_frameCapFrom[48] = "";               // where the cap was seen
static char g_capNote[400] = "";                   // why -managed was not set, for the size line; empty when it was
#ifdef SISCO_TEST
static int g_tRtss = -1;                           // the harness's answer for RTSS; -1 = ask the process
#endif
static bool RtssLoaded() {
#ifdef SISCO_TEST
    if (g_tRtss >= 0) return g_tRtss != 0;
#endif
    return GetModuleHandleA("RTSSHooks.dll") != NULL;
}
// DXVK's config, parsed the way DXVK 3.1 parses it (config.cpp, parseUserConfigLine): a line is "key = value", the
// key [A-Za-z0-9._]+ and the value up to the first whitespace with any quotes dropped; a "[name]" line switches the
// lines after it on only while name is the exe's file name, exactly; a later line wins, and a later value that is
// not DXVK's integer (an optional '-', then digits) unsets the key, because DXVK's own read then falls back to its
// default; anything else is ignored, which is how a '#' comment is. The DXVK_CONFIG variable holds the same lines
// split on ';', applied over the file. The keys 3.1's D3D9 reads: dxvk.maxFrameRate over d3d9.maxFrameRate (1 or
// more is a cap; 0 and below are not one SISCO can name), and d3d9.presentInterval (0 or more is forced over the
// game's interval, and over the immediate present SISCO asks for while the world builds). dxgi.maxFrameRate and the
// DXVK_FRAME_RATE variable belong to older DXVKs and are not read by 3.1's D3D9, so not here.
#define CONF_UNSET (-1000000)
struct DxvkConf { int d3d9Cap, dxvkCap, interval; const char* d3d9CapFrom; const char* dxvkCapFrom; const char* intervalFrom; };
static bool ConfInt(const char* p, const char* e, int* out) {
    int sign = 1; long long v = 0;
    if (p < e && *p == '-') { sign = -1; p++; }
    if (p >= e) return false;
    for (; p < e; p++) { if (*p < '0' || *p > '9') return false; v = v * 10 + (*p - '0'); if (v > 100000000) return false; }
    *out = (int)(sign * v);
    return true;
}
static void DxvkConfParse(const char* text, size_t n, char sep, const char* exeName, const char* from, DxvkConf* c) {
    bool active = true;
    const char* end = text + n;
    for (const char* line = text; line < end; ) {
        const char* le = line; while (le < end && *le != sep && *le != '\n' && *le != '\r') le++;
        const char* p = line; while (p < le && (*p == ' ' || *p == '\t')) p++;
        if (p < le && *p == '[') {
            const char* name = p + 1;
            const char* q = le; while (q > name && *(q - 1) != ']') q--;         // the name runs to the last ']'
            size_t nameN = q > name ? (size_t)(q - 1 - name) : 0;
            active = nameN == strlen(exeName) && memcmp(name, exeName, nameN) == 0;
        } else {
            const char* k = p;
            while (p < le && ((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || *p == '.' || *p == '_')) p++;
            size_t kn = (size_t)(p - k);
            while (p < le && (*p == ' ' || *p == '\t')) p++;
            if (kn && p < le && *p == '=') {
                p++; while (p < le && (*p == ' ' || *p == '\t')) p++;
                char v[32]; size_t vn = 0;
                for (; p < le && *p != ' ' && *p != '\t'; p++) if (*p != '"' && vn < sizeof(v) - 1) v[vn++] = *p;
                int i = CONF_UNSET; bool ok = ConfInt(v, v + vn, &i);
                if (active) {
                    if (kn == 17 && memcmp(k, "d3d9.maxFrameRate", 17) == 0) { c->d3d9Cap = ok ? i : CONF_UNSET; c->d3d9CapFrom = from; }
                    else if (kn == 17 && memcmp(k, "dxvk.maxFrameRate", 17) == 0) { c->dxvkCap = ok ? i : CONF_UNSET; c->dxvkCapFrom = from; }
                    else if (kn == 20 && memcmp(k, "d3d9.presentInterval", 20) == 0) { c->interval = ok ? i : CONF_UNSET; c->intervalFrom = from; }
                }
            }
        }
        line = le; while (line < end && (*line == sep || *line == '\n' || *line == '\r')) line++;
    }
}
#ifdef SISCO_TEST
static const uint8_t* g_tDxvkImage; static size_t g_tDxvkImageN;   // the harness plants an image in place of the module
#endif
static bool DxvkVersionIn(const uint8_t* p, size_t n, int v[3]) {
    for (size_t i = 0; i + 4 < n; i++) {
        if (p[i] != 'v' || (i && p[i - 1] != 0) || p[i + 1] < '0' || p[i + 1] > '9') continue;
        int parts[3] = { 0, 0, 0 }, k = 0, digits = 0; size_t j = i + 1;
        for (; j < n && k < 3; j++) {
            if (p[j] >= '0' && p[j] <= '9') { if (++digits > 3) break; parts[k] = parts[k] * 10 + (p[j] - '0'); }
            else if (p[j] == '.' && digits) { k++; digits = 0; }
            else break;
        }
        // The numbers end at a NUL, or at the '-' or '+' that git describe appends on a build not made from a clean tag.
        if (j < n && (p[j] == 0 || p[j] == '-' || p[j] == '+') && digits && k >= 1 && k <= 2) { v[0] = parts[0]; v[1] = parts[1]; v[2] = k == 2 ? parts[2] : 0; return true; }
    }
    return false;
}
static bool DxvkScan(const uint8_t* p, size_t n) { __try { return DxvkVersionIn(p, n, g_dxvkVer); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
// One module's readable, non-discardable sections, from its own headers, so nothing outside the image is touched.
static bool DxvkVersionInModule(HMODULE m) {
    if (!m) return false;
    __try {
        const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)m;
        const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)((const uint8_t*)m + dos->e_lfanew);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE) return false;
        const IMAGE_SECTION_HEADER* sh = IMAGE_FIRST_SECTION(nt);
        // The marker is a string: the data sections first (.rdata holds it, 0.7 MB), the code (6.2 MB) only if none has it.
        for (int pass = 0; pass < 2; pass++)
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++) {
            if (!(sh[i].Characteristics & IMAGE_SCN_MEM_READ) || (sh[i].Characteristics & IMAGE_SCN_MEM_DISCARDABLE)) continue;
            if (((sh[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0) != (pass == 1)) continue;
            if (DxvkScan((const uint8_t*)m + sh[i].VirtualAddress, sh[i].Misc.VirtualSize)) {
                char path[MAX_PATH] = "?"; GetModuleFileNameA(m, path, MAX_PATH);
                const char* base = strrchr(path, '\\');
                _snprintf_s(g_dxvkVerFrom, sizeof(g_dxvkVerFrom), _TRUNCATE, "%s", base ? base + 1 : path);
                return true;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { g_dxvkVer[0] = -1; }
    return false;
}
static void DxvkVersionOf(void* d3d) {
    g_dxvkVer[0] = g_dxvkVer[1] = g_dxvkVer[2] = -1; g_dxvkVerFrom[0] = 0;
#ifdef SISCO_TEST
    if (g_tDxvkImage) { if (DxvkScan(g_tDxvkImage, g_tDxvkImageN)) strcpy_s(g_dxvkVerFrom, "the planted image"); return; }
#endif
    uint32_t vt = 0, qi = 0;                        // the object's QueryInterface is code in the module that built it
    HMODULE owner = NULL;
    if (RdU32((uintptr_t)d3d, &vt) && RdU32(vt, &qi) && qi)
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)(uintptr_t)qi, &owner);
    HMODULE byName[2] = { GetModuleHandleA("vulkan.dll"), GetModuleHandleA("d3d9.dll") };
    if (DxvkVersionInModule(owner)) return;
    for (int i = 0; i < 2; i++) if (byName[i] && byName[i] != owner && DxvkVersionInModule(byName[i])) return;
}
static void SisOnFirstD3D(void* d3d) {
    const char* how = "no Direct3D object";
    if (d3d) {
        void* ip = NULL;
        HRESULT hr = E_FAIL;
        __try { hr = ((IUnknown*)d3d)->QueryInterface(kIID_D3D9VkInteropInterface, &ip); } __except (EXCEPTION_EXECUTE_HANDLER) { hr = E_FAIL; }
        if (SUCCEEDED(hr) && ip) { g_dxvk = 1; how = "DXVK (it answered its interop interface)"; ((IUnknown*)ip)->Release(); }
        else if (VulkanLoaded()) { g_dxvk = 1; how = "DXVK assumed: no interop interface, but vulkan-1.dll is loaded"; }
        else { g_dxvk = 0; how = "native Direct3D 9"; }
        if (g_dxvk == 1) DxvkVersionOf(d3d);
    }
    char ver[320] = "";
    if (g_dxvk == 1) {
        if (g_dxvkVer[0] >= 0) _snprintf_s(ver, sizeof(ver), _TRUNCATE, "; DXVK %d.%d.%d (read from %s)%s", g_dxvkVer[0], g_dxvkVer[1], g_dxvkVer[2], g_dxvkVerFrom,
                                           g_dxvkVer[0] < 3 ? "; SISCO is tested on 3.x, the vulkan.dll in the download" : "");
        else strcpy_s(ver, "; DXVK version not found in any module");
        if (g_frameCap > 0) _snprintf_s(ver + strlen(ver), sizeof(ver) - strlen(ver), _TRUNCATE, "; frame cap %d fps (%s)", g_frameCap, g_frameCapFrom);
        if (g_forcedInterval >= 1) _snprintf_s(ver + strlen(ver), sizeof(ver) - strlen(ver), _TRUNCATE, "; VSync forced in DXVK's config (d3d9.presentInterval %d)", g_forcedInterval);
        if (RtssLoaded()) _snprintf_s(ver + strlen(ver), sizeof(ver) - strlen(ver), _TRUNCATE, "; RTSS is loaded (its cap cannot be read: a frame limit makes loads slow with -managed)");
    }
    if (g_dxgiState == 0) DxgiInit();
    int64_t b = -1;
    if (DxgiPoll(&b)) g_bootBudgetMb = b >> 20;
    char opts[260] = "";
    if (g_dxvk == 1 && !g_set.budget) strcpy_s(opts, "; no launch option set (Budget=0 in SISCO.ini)");
    else if (g_dxvk == 1) {
        uint32_t un = 0, ma = 0, vm = 0;
        RdU32(g_cs.prmUnmanaged, &un); RdU32(g_cs.prmManaged, &ma); RdU32(g_cs.prmVidmem, &vm);
        // A visible frame cap, or a VSync forced in DXVK's config: -managed is not written, and the note holds the
        // budget at sizing even when the player's own -managed is kept, because the slow load was measured with
        // -managed however it got there. Neither is bypassed by the immediate present below.
        bool capped = g_frameCap > 0 || g_forcedInterval >= 1;
        if (g_frameCap > 0) _snprintf_s(g_capNote, sizeof(g_capNote), _TRUNCATE, "a frame-rate cap of %d fps (%s): under -managed a capped load is a slow one (measured: "
                                "a 30 fps cap turned a 2 s world build into 61 s, a 60 cap into 30 s), so SISCO leaves -managed unset and the budget at the "
                                "game's own; lift the cap while loading for the raised budget", g_frameCap, g_frameCapFrom);
        else if (capped) _snprintf_s(g_capNote, sizeof(g_capNote), _TRUNCATE, "VSync forced in DXVK's config (d3d9.presentInterval %d): it overrides the immediate "
                                "present SISCO uses while the world builds, and under -managed a load behind VSync is a slow one (measured: 27 s for a 2 s "
                                "world build at 120 Hz), so SISCO leaves -managed unset and the budget at the game's own; remove that line for the raised "
                                "budget (the game's own VSync setting is fine)", g_forcedInterval);
        else g_capNote[0] = 0;
        const char* m;
        if (un) m = "the player's -unmanaged kept";
        else if (ma) m = capped ? "the player's -managed kept; under the visible cap the budget still stays the game's own" : "the player's -managed kept";
        else if (capped) m = "-managed NOT set under a visible cap, so the budget stays the game's own (the size line says how to get it)";
        else m = WrU32(g_cs.prmManaged, (uint32_t)(uintptr_t)g_flagArg) ? "-managed set" : "-managed NOT set (write failed)";
        uint32_t m2 = 0; RdU32(g_cs.prmManaged, &m2); g_managedOn = m2 != 0 && !un;
        // Whatever the player set is the figure the game will believe, on whichever build, so it bounds the
        // sizing later however much memory the card really has.
        g_playerVidmemMb = ReadVidmemArg(vm);
        char v[160];
        // 1.0.8.0 only. The Complete Edition already works out a figure of its own, measured at
        // 16,210 MB under DXVK, so writing ours could only ever lower it, and on a small card it would.
        if (vm) strcpy_s(v, "the player's -availablevidmem kept");
        else if (g_cs.build == BUILD_CE) strcpy_s(v, "-availablevidmem left to the Complete Edition, which works out a figure of its own (FusionFix computes one from the card and the RAM)");
        else if (g_bootBudgetMb < 512) strcpy_s(v, "-availablevidmem NOT set (no budget from DXGI)");
        else {
            _snprintf_s(g_vidmemArg, sizeof(g_vidmemArg), _TRUNCATE, "%lld", g_bootBudgetMb > VIDMEM_MAX_MB ? (long long)VIDMEM_MAX_MB : g_bootBudgetMb);
            if (WrU32(g_cs.prmVidmem, (uint32_t)(uintptr_t)g_vidmemArg)) _snprintf_s(v, sizeof(v), _TRUNCATE, "-availablevidmem %s set", g_vidmemArg);
            else strcpy_s(v, "-availablevidmem NOT set (write failed)");
        }
        _snprintf_s(opts, sizeof(opts), _TRUNCATE, "; %s; %s", m, v);
    }
    _snprintf_s(g_d3dNote, sizeof(g_d3dNote), _TRUNCATE, "%s%s; card budget %lld MB%s", how, ver, g_bootBudgetMb, opts);
    SisLog("direct3d", "%s", g_d3dNote);
}
// ---------------------------------------------------------------------------------------------
// Immediate presents while the world builds. Under -managed the world build after the player appears is paced by
// presents that block: with VSync (120 Hz, no limiter, run L10) or a frame cap (L4 at 30, L7 at 60) the game crawls
// at 5 to 10 fps for about 300 frames, 27 to 61 s, where an unblocked present builds the same world in 2 s and no
// -managed builds it in 2 s under the 30 fps cap (L6). The phase before the player appears is not paced that way
// (31 to 32 s with and without VSync, L10 against L13). So from the tick the worker sees the player appear after a
// load, every present carries D3DPRESENT_FORCEIMMEDIATE, which DXVK 3.1 honours per call (d3d9_swapchain.cpp:135)
// unless d3d9.presentInterval is forced in its config (:140; SIS.cpp reads that and the cap rule holds the budget
// for it), and VSync is handed back once the world has built and 15 s have passed (LoadTick below). The menu and
// the loading screens run as the game presents them. DXVK's own limiter and external ones (RTSS, the driver's) are
// not bypassed by the flag; those keep the frame-cap rule. The hook is the shape DLSS-IV has run on this game under
// DXVK since September: IDirect3D9::CreateDevice (slot 16) on the object Direct3DCreate9 returns, then the swap
// chain's Present (slot 3) from GetSwapChain(0), each written once with the original read first; an overlay that
// hooks above later is left alone, since the slot is never rewritten. The two slots are DXVK's, not the game's, so
// they are chained, not compared against expected bytes. Only with -managed in effect and the budget on; the flag
// is raised by the worker alone, so without a worker it is never raised.
// ---------------------------------------------------------------------------------------------
#define SIS_FORCEIMMEDIATE 0x00000100u             // D3DPRESENT_FORCEIMMEDIATE
typedef HRESULT (__stdcall* CreateDevice_t)(void* d3d, UINT adapter, UINT type, HWND wnd, DWORD flags, void* pp, void** out);
typedef HRESULT (__stdcall* GetSwapChain_t)(void* dev, UINT i, void** out);
typedef HRESULT (__stdcall* SwapPresent_t)(void* sc, const RECT* src, const RECT* dst, HWND wnd, const RGNDATA* dirty, DWORD flags);
static CreateDevice_t g_oCreateDevice;
static SwapPresent_t g_oSwapPresent;
static void** g_d3dVtblHooked; static void** g_swapVtblHooked;
static volatile LONG g_presentImmediate;           // 1 while the world builds after a load: presents go out without waiting for VSync
static volatile LONG g_immediatePresents;          // how many went out that way this load, for the log
static bool WriteSlot(void** vt, int slot, void* fn) {
    DWORD old = 0;
    if (!VirtualProtect(vt + slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    vt[slot] = fn;
    DWORD o2; VirtualProtect(vt + slot, sizeof(void*), old, &o2);
    return true;
}
static HRESULT __stdcall HkSwapPresent(void* sc, const RECT* src, const RECT* dst, HWND wnd, const RGNDATA* dirty, DWORD flags) {
    if (g_presentImmediate) { flags |= SIS_FORCEIMMEDIATE; InterlockedIncrement(&g_immediatePresents); }
    return g_oSwapPresent(sc, src, dst, wnd, dirty, flags);
}
static HRESULT __stdcall HkCreateDevice(void* d3d, UINT adapter, UINT type, HWND wnd, DWORD flags, void* pp, void** out) {
    HRESULT hr = g_oCreateDevice(d3d, adapter, type, wnd, flags, pp, out);
    if (FAILED(hr) || !out || !*out || g_swapVtblHooked) return hr;
    void* dev = *out; void** dvt = NULL; void* sc = NULL; uint32_t probe = 0;
    if (!RdU32((uintptr_t)dev, (uint32_t*)&dvt) || !dvt || !RdU32((uintptr_t)(dvt + 14), &probe)) return hr;
    GetSwapChain_t gsc = (GetSwapChain_t)dvt[14];
    if (FAILED(gsc(dev, 0, &sc)) || !sc) return hr;
    void** svt = NULL;
    if (RdU32((uintptr_t)sc, (uint32_t*)&svt) && svt && RdU32((uintptr_t)(svt + 3), &probe) && svt[3] != (void*)HkSwapPresent) {
        g_oSwapPresent = (SwapPresent_t)svt[3];
        if (WriteSlot(svt, 3, (void*)HkSwapPresent)) {
            g_swapVtblHooked = svt;
            SisLog("present", "the swap chain Present is hooked: presents go out without waiting for VSync while the world builds after each load");
        } else { g_oSwapPresent = NULL; SisLog("present", "the swap chain Present could NOT be hooked: VSync is not bypassed while the world builds"); }
    }
    ((IUnknown*)sc)->Release();
    return hr;
}
static void HookCreateDevice(void* d3d) {
    if (!d3d || g_d3dVtblHooked) return;
    void** dvt = NULL; uint32_t probe = 0;
    if (!RdU32((uintptr_t)d3d, (uint32_t*)&dvt) || !dvt || !RdU32((uintptr_t)(dvt + 16), &probe) || dvt[16] == (void*)HkCreateDevice) return;
    g_oCreateDevice = (CreateDevice_t)dvt[16];
    if (WriteSlot(dvt, 16, (void*)HkCreateDevice)) g_d3dVtblHooked = dvt; else g_oCreateDevice = NULL;
}
static void* WINAPI HkDirect3DCreate9(UINT sdk) {
    void* d3d = g_oD3DCreate9(sdk);
    if (InterlockedIncrement(&g_d3dCalls) == 1) {
        SisOnFirstD3D(d3d);
        if (g_dxvk == 1 && g_set.budget && g_managedOn) HookCreateDevice(d3d);
    }
    return d3d;
}
// The import slot is swapped only when the call path is the one read: the call, the thunk through this slot, and a
// slot that holds d3d9.dll's Direct3DCreate9 (or, if another mod hooked it first, code in a loaded module: then it is
// chained).
static bool InstallD3DHook(const CoreSites& c) {
    uint32_t iat = (uint32_t)c.d3dIat;
    if (c.d3dThunk) {                                  // 1.0.8.0: call -> a jmp-thunk -> the import slot
        uint8_t thunk[6] = { 0xFF, 0x25 }; memcpy(thunk + 2, &iat, 4);
        uint8_t op = 0; int32_t rel = 0;
        __try { op = *(uint8_t*)c.d3dCall; rel = *(int32_t*)(c.d3dCall + 1); } __except (EXCEPTION_EXECUTE_HANDLER) { op = 0; }
        if (op != 0xE8 || c.d3dCall + 5 + rel != c.d3dThunk || !BytesEq(c.d3dThunk, thunk, 6)) {
            SiteSet(S_D3D_IAT, ST_OFF_MISMATCH, "the call or its thunk differs"); return false;
        }
    } else {                                           // the Complete Edition calls the import slot directly
        uint8_t call[6] = { 0xFF, 0x15 }; memcpy(call + 2, &iat, 4);
        if (!BytesEq(c.d3dCall, call, 6)) { SiteSet(S_D3D_IAT, ST_OFF_MISMATCH, "the call differs"); return false; }
    }
    uint32_t cur = 0;
    if (!RdU32(c.d3dIat, &cur) || !cur) { SiteSet(S_D3D_IAT, ST_OFF_MISMATCH, "the import slot is empty"); return false; }
    HMODULE owner = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)(uintptr_t)cur, &owner)) {
        SiteSet(S_D3D_IAT, ST_OFF_MISMATCH, "the import slot points outside every module (%08X)", cur); return false;
    }
    HMODULE d3d9 = GetModuleHandleA("d3d9.dll");
    bool direct = d3d9 && (uint32_t)(uintptr_t)GetProcAddress(d3d9, "Direct3DCreate9") == cur;
    g_oD3DCreate9 = (D3DCreate9_t)(uintptr_t)cur;
    DWORD old;
    if (!VirtualProtect((void*)c.d3dIat, 4, PAGE_READWRITE, &old)) { SiteSet(S_D3D_IAT, ST_OFF_ERROR, "VirtualProtect failed"); return false; }
    InterlockedExchange((volatile LONG*)c.d3dIat, (LONG)(uintptr_t)HkDirect3DCreate9);
    DWORD o2; VirtualProtect((void*)c.d3dIat, 4, old, &o2);
    char mod[MAX_PATH] = "?"; GetModuleFileNameA(owner, mod, MAX_PATH);
    const char* base = strrchr(mod, '\\');
    SiteSet(S_D3D_IAT, ST_ON, "%s %s", direct ? "d3d9.dll's Direct3DCreate9" : "chained after a hook in", base ? base + 1 : mod);
    return true;
}
// ---------------------------------------------------------------------------------------------
// The budget. At its limit the game holds 0.9 x (T + X) of video memory, measured, and the card uses O more for
// everything else, so T is the largest budget whose total fits in 85% of the card's
// budget B, never over 4000 (past that the 32-bit address space runs out first) and never under the game's own
// value for the texture quality.
// O rises with the render targets: 900 MB at 1080p (X 373) to 1,100 at 4K (X 799), above everything measured.
// FusionFix's ExtraStreamingMemory raises the 0.9 up to 1.5: then 1.5 is assumed.
// ---------------------------------------------------------------------------------------------
#define T_MAX_MB 4000
// The floor: never take the game below what it would have used on its own. Which row of the table the game reads is
// not fixed: with -nomemrestrict it reads row 15, without it 0x40F265 picks row 12, 13 or 14 by the card's vendor,
// and row 12 at High is 550 MB where row 15 is 800. Taking the smallest of all four, as SISCO first did, put the
// floor at 550 on installs the game itself runs at 800, and the brake then stepped the budget below what the game
// would have used on its own, with the traffic budgets following it down. So the floor asks the game's question.
static int StockBudgetMb(int quality) { return quality <= 0 ? 300 : quality == 1 ? 400 : 800; }   // row 15, the largest
// The stock test, built at run time the way every other absolute-address site is: its operand is the slot's address
// as the loader relocated it (both exes have DYNAMIC_BASE and that operand is a HIGHLOW relocation), so the file's
// bytes never match a running game. Only the rel32 tail is fixed: 0x158 on 1.0.8.0, 0x162 on the Complete Edition.
static void MemRestrictTestStock(const CoreSites& c, uint8_t out[13]) {
    uint32_t slot = (uint32_t)c.prmNoMemRestrict, rel = c.build == BUILD_CE ? 0x162u : 0x158u;
    out[0] = 0x83; out[1] = 0x3D; memcpy(out + 2, &slot, 4); out[6] = 0x00; out[7] = 0x0F; out[8] = 0x85; memcpy(out + 9, &rel, 4);
}
// FusionFix rewrites the test on EVERY install (an inline hook over its 13 bytes, in both of its modes) and decides
// inside the hook: ExtraStreamingMemory off runs row 15, on falls through to the vendor rows, and the player's own
// -nomemrestrict is ignored either way. So a rewritten test is read with the signal that tells the two modes apart,
// the hook FusionFix puts on the credit function only when ExtraStreamingMemory is on; a stock test is the game's
// own question, the slot.
static bool GameRunsRow15(const CoreSites& c, bool extraStreaming) {
    uint8_t stock[13]; MemRestrictTestStock(c, stock);
    if (c.memRestrictTest && !BytesEq(c.memRestrictTest, stock, 13)) return !extraStreaming;
    uint32_t slot = 0;
    return c.prmNoMemRestrict && RdU32(c.prmNoMemRestrict, &slot) && slot;
}
static int StockFloorMb(const CoreSites& c, int quality, bool extraStreaming) {
    uint64_t cell = 0;
    if (GameRunsRow15(c, extraStreaming)) {
        if (RdU64(c.table + 8 * (3 * 15 + quality), &cell) && (cell >> 20) > 0) return (int)(cell >> 20);
        return StockBudgetMb(quality);
    }
    int lo = 0;
    for (int row = 12; row <= 14; row++) {
        if (!RdU64(c.table + 8 * (3 * row + quality), &cell)) continue;
        int mb = (int)(cell >> 20);
        if (mb > 0 && (!lo || mb < lo)) lo = mb;
    }
    return lo ? lo : StockBudgetMb(quality);          // unreadable: fall back to the row the game reads with FusionFix
}
static int OtherMb(int64_t xMb) { return xMb <= 373 ? 900 : (int)(900 + (xMb - 373) * 200 / 426); }
static int SizeBudget(int64_t bMb, int64_t xMb, int tStock, bool extra, int* tVram) {
    double mult = extra ? 1.5 : 0.9;
    double v = ((double)bMb * 0.85 - OtherMb(xMb)) / mult - (double)xMb;
    int tv = v < -100000 ? -100000 : v > 100000 ? 100000 : (int)v;
    if (tVram) *tVram = tv;
    int t = tv < T_MAX_MB ? tv : T_MAX_MB;
    return t < tStock ? tStock : t;
}
// Car and ped budgets, from the measured ladders: 200 MB each once the budget reaches 2000 MB,
// scaled down with it to the game's own values at its stock budget, so a small card keeps its world. A larger value
// already there (FusionFix's VehicleBudget or PedBudget) stands. Bytes, as FusionFix writes them.
#define CARPED_FULL 200000000u
// The most car budget the game's OWN VehicleStruct pool can use. Its guard keeps 20 of its 50 slots free, so about
// 30 loaded models is the ceiling without the raise, and the ladder measured 24 loaded at 120 MB, 31 at 160 and 41
// at 200. Past this a refused pool raise means the extra budget buys nothing, so the budget stops here instead.
#define CAR_NO_POOL 120000000u
#define CAR_STOCK 40000000u
#define PED_STOCK 50000000u
static uint32_t CarPedFor(uint32_t stock, int t, int tStock) {
    if (t >= 2000) return CARPED_FULL;
    if (t <= tStock || tStock >= 2000) return stock;
    return stock + (uint32_t)((double)(CARPED_FULL - stock) * (t - tStock) / (2000 - tStock));
}

// ---------------------------------------------------------------------------------------------
// What the sizing decided, read by the brake when the traffic budgets follow it.
static bool g_sized;
static bool g_worldLogged;    // a "world" line has been written: the player has appeared after a load at least once
static uint32_t g_worldFrame; // the game's frame counter when the player last appeared, and the worker's clock then
static int64_t g_worldMs, g_tickMs;
// One load at a time, from the player appearing to the world built around them: the phase -managed makes slow, timed
// by the render targets, which are built in stages during it and hold once it is over. The verdict is the load line;
// a slow one is named as far as SISCO can see and handed to the worker for the file beside the log. LoadTick below.
static bool g_ldUp;              // the loading flag was up on the last tick
static bool g_ldPending = true;  // the flag is down and the player has not appeared yet: the next appearance is a load's edge
static int64_t g_ldEdgeMs;       // the worker's clock when the player appeared, 0 while no load is being timed
static int64_t g_ldChangeMs;     // the last tick the render targets changed since the edge: the build's end once they hold
static uint32_t g_ldChangeFrame; // the game's frame counter then
static int64_t g_ldX;            // the render targets on the last tick, -1 while not built
static int g_ldSteady;           // ticks they have held that size
static bool g_ldVerdict;         // the load line has been written for this load
static int g_loadN;              // loads timed this run
static int64_t g_loadBuildS;     // the last build, in whole seconds: from the edge to the render targets' last change
static int g_loadFps = -1;       // the game's frame rate over that build, where its counter is known; -1 = not measured
static char g_slowLoadNote[700] = "";   // the last slow load with -managed in effect, named as far as it can be seen; empty otherwise
static volatile LONG g_slowLoadPending; // 1 once per slow load: the worker writes the file beside the log and clears it
#define LOAD_STEADY_TICKS 3      // the render targets have held this long after a change: the build is over
#define LOAD_HOLD_MS 15000       // presents go out immediately at least this long after the player appears, in case a stage holds
#define LOAD_CEILING_MS 90000    // and never longer than this, whatever the render targets do
#define LOAD_SLOW_S 20           // a build longer than this with -managed is a slow load: 2 s measured unheld, 27 to 61 held
static int g_sizedT, g_sizedTStock, g_sizedX;
static bool g_carHeld;        // the car budget stopped where the game's own VehicleStruct pool can use it
static uint32_t g_setView, g_setDetail, g_setCars;   // the sliders as read at sizing, for the log and the harness
static int64_t g_settleX = -1; static int g_settleTicks;   // the render targets grow in stages while the world loads
// How long the render targets must hold the same size before the budget is sized against them. They are built in
// stages while the world loads, and the player appears and the loading byte clears BEFORE the last stage: measured
// at 63 MB after the player appeared, for two seconds on the clean install and for the whole 23 s world build on
// the heavy one (run D3), and two ticks let that install size against the half-built figure in the middle of its
// load. A part-built figure changes within seconds on a normal load; the final one holds for the whole session.
#define SETTLE_TICKS 10
// A set under 128 MB is a part-built one, unless it is at least twice the smallest set seen this run (the first
// stage is 28 MB at 1080p in two players' logs and 63 at 4K here, and no later stage of the same build is under
// twice that) or it has held for SETTLE_LONG_TICKS (no part-built stage ever held longer than 23 s, measured on
// the heavy install). A finished set is 252 MB at 1080p (a player's log), which scales to about 128 at 1366x768
// and 112 at 720p, figures no run here has measured: with the plain minimum those machines were never sized at all.
#define SETTLE_LONG_TICKS 45
static int64_t g_rtMin;       // the smallest render-target set seen this run, in MB; 0 until one is seen

// The brake, proven in the field without a texture pack, which is the hard case. Without a pack, a large
// budget holds so many models that the 32-bit address space runs out before V reaches the budget: DXVK keeps a mapped
// copy of every vertex and index buffer inside the process. The usable headroom is the largest free block L (every other
// free region is under the 16 MiB DXVK maps at a time), and L only shrinks: memory the game frees stays reserved inside
// the allocators. So L's LEVEL only starts the brake (and calls an emergency) and L's FALL decides: under HoldMB the
// budget is held once, at the one whose wall 0.9 x (T + X) - 2 is the V held then (no write when V is already at the
// wall); after that, each further fall of FallMB since the last action (measured from the highest L seen since it)
// lowers the budget one StepMB, no sooner than WaitS after the last action. A step turns resident data into reusable
// room inside the allocators, which is what stops the fall. Under EmergencyMB it steps every WaitS / 2 whether L fell
// or not. Never raised; never under the floor. Once a second, never while the loading screen is up.
// Every step comes out of the world and none of it out of the traffic: the car and ped budgets follow the budget
// itself, by the rule that sized them, so they hold at 200 MB until the budget falls under 2000. An earlier
// version took the steps out of the peds and then the cars, and a field run showed car variety collapsing from 25
// models to 4 while the world stayed whole, which is the wrong way round.
// ---------------------------------------------------------------------------------------------
struct BrakeCfg { int holdMb, fallMb, stepMb, emergencyMb, waitS, floorMb; };
struct BrakeState { int t; bool held; int64_t refL, lastActMs; };
enum { BRAKE_NONE = 0, BRAKE_HOLD = 1, BRAKE_STEP = 2, BRAKE_EMERGENCY = 3 };
static int BrakeDecide(BrakeState& st, int64_t nowMs, int64_t vMb, int64_t xMb, int64_t lMb, const BrakeCfg& c, int* action) {
    *action = BRAKE_NONE;
    if (vMb <= 0 || lMb < 0) return st.t;
    if (!st.held) {
        if (lMb >= c.holdMb) return st.t;
        st.held = true; st.refL = lMb; st.lastActMs = nowMs;
        int64_t t = ((vMb + 2) * 10 + 8) / 9 - xMb;      // rounded up: at the wall this gives back the budget itself
        if (t < c.floorMb) t = c.floorMb;
        if (t < st.t) { st.t = (int)t; *action = BRAKE_HOLD; }
        return st.t;
    }
    if (lMb > st.refL) st.refL = lMb;                     // each fall is measured from the highest L since the last action
    bool emergency = lMb < c.emergencyMb;
    if (nowMs - st.lastActMs < (int64_t)(emergency ? c.waitS / 2 : c.waitS) * 1000) return st.t;
    if (!emergency && st.refL - lMb < c.fallMb) return st.t;
    st.refL = lMb; st.lastActMs = nowMs;
    int t = st.t - c.stepMb; if (t < c.floorMb) t = c.floorMb;
    if (t < st.t) { st.t = t; *action = emergency ? BRAKE_EMERGENCY : BRAKE_STEP; }
    return st.t;
}
static int g_brakeOn;
static BrakeCfg g_brakeCfg = { 512, 32, 128, 192, 20, 800 };
static BrakeState g_brakeSt;
// All 48 cells, aligned 32-bit stores of the low dword (8v: safe from any thread; the high dword is 0 for every value
// under 4 GiB and stays so).
static bool WriteBudgetAll(uintptr_t table, int mb) {
    DWORD old;
    if (!VirtualProtect((void*)table, 48 * 8, PAGE_READWRITE, &old)) return false;
    for (int i = 0; i < 48; i++) InterlockedExchange((volatile LONG*)(table + 8 * i), (LONG)((uint32_t)mb << 20));
    DWORD o2; VirtualProtect((void*)table, 48 * 8, old, &o2);
    return true;
}
static void BrakeStart(int tMb, int floorMb) {
    g_brakeCfg.floorMb = floorMb < tMb ? floorMb : tMb;
    g_brakeSt.t = tMb; g_brakeSt.held = false; g_brakeSt.refL = -1; g_brakeSt.lastActMs = 0;
    g_brakeOn = 1;
}

// The car and ped budgets as SISCO keeps them: the target, and what it last wrote (anything else there was written by
// someone else: FusionFix's asynchronous init, or another mod).
struct TrafficBudget { uintptr_t addr; uint32_t stock, target, written, fromOther; bool on; const char* name; };
static TrafficBudget g_car = { 0, CAR_STOCK, 0, 0, 0, false, "car" }, g_ped = { 0, PED_STOCK, 0, 0, 0, false, "ped" };
static void TrafficSet(TrafficBudget& b, uint32_t v) {
    b.target = v;
    if (WrU32(b.addr, v)) b.written = v;
}
// Once a second: a larger value written by someone else stands (it becomes the target); a smaller one is replaced.
static void TrafficGuard(TrafficBudget& b) {
    if (!b.on) return;
    uint32_t cur = 0;
    if (!RdU32(b.addr, &cur) || cur == b.written) return;
    if (cur > b.target && cur <= 1000000000u) {
        SisLog("budget", "the %s budget was set to %u MB by another mod: kept (SISCO had %u MB)", b.name, cur / 1000000, b.target / 1000000);
        b.target = b.written = b.fromOther = cur;
        return;
    }
    TrafficSet(b, b.target);
}
// When the brake lowers the budget, the world gives: the car and ped budgets follow the budget itself, by the same
// rule that sized them, so they stay full until the budget falls under 2000 MB and only then scale with it. A larger
// value another mod set always stands. Never raised. Measured: taking it from the traffic first drops car variety
// from 25 models to between 4 and 9 while the world stays whole, which is the wrong way round.
static void TrafficFollow(int tMb, int tStock) {
    TrafficBudget* both[2] = { &g_car, &g_ped };
    for (int i = 0; i < 2; i++) {
        TrafficBudget& b = *both[i];
        if (!b.on) continue;
        uint32_t want = CarPedFor(b.stock, tMb, tStock);
        if (b.fromOther > want) want = b.fromOther;
        if (want >= b.target) continue;
        SisLog("budget", "the %s budget follows the world's: %u -> %u MB", b.name, b.target / 1000000, want / 1000000);
        TrafficSet(b, want);
    }
}

static void BrakeTick(uintptr_t table, int64_t nowMs, int64_t vMb, int64_t xMb, int64_t lMb, bool loading) {
    if (!g_brakeOn) return;
    if (loading) return;
    BrakeState before = g_brakeSt;
    int act = BRAKE_NONE, t = BrakeDecide(g_brakeSt, nowMs, vMb, xMb, lMb, g_brakeCfg, &act);
    if (act == BRAKE_NONE) return;
    if (!WriteBudgetAll(table, t)) { g_brakeSt = before; return; }   // not written: as if this tick never happened
    SisLog("brake", "%s at %lld s: budget %d -> %d MB (video memory held %lld MB, largest free block %lld MB)",
           act == BRAKE_HOLD ? "held" : act == BRAKE_STEP ? "stepped" : "EMERGENCY step", nowMs / 1000, before.t, t, vMb, lMb);
    TrafficFollow(t, g_sizedTStock);
}

// The largest free block of the address space (the brake's L), from the tool's own thread.
static uint64_t LargestFreeBlock() {
    uint64_t largest = 0;
    uintptr_t p = 0x10000;
    MEMORY_BASIC_INFORMATION m;
    while (p < 0xFFFF0000u && VirtualQuery((void*)p, &m, sizeof(m)) == sizeof(m)) {
        if (m.State == MEM_FREE && m.RegionSize > largest) largest = m.RegionSize;
        uintptr_t next = (uintptr_t)m.BaseAddress + m.RegionSize;
        if (next <= p) break;
        p = next;
    }
    return largest;
}

// ---------------------------------------------------------------------------------------------
// Once the world is loaded (the streamer is up and the render targets exist, so X is known): size the budget and the
// car and ped budgets, and start the brake. Then once a second: the car and ped guard, a lower budget if the render
// targets grew (a higher resolution), and the brake.
// ---------------------------------------------------------------------------------------------
static bool g_extraStreaming;
static char g_sizeNote[800] = "not yet: the world is not loaded";
// The player exists and stands somewhere in the world: how SISCO tells the game from the main menu, where the streamer is
// already up, the loading screen is down and the render targets are still the menu's, measured at 63 MB at 4K where
// the world's are 797. A budget sized against the menu's would be far too large on a small card.
static bool PlayerInWorld() {
    uint32_t pi = 0, ped = 0, mat = 0; float x = 0, y = 0, z = 0;
    if (!RdU32(g_cs.playerInfo, &pi) || !pi) return false;
    if (!RdU32(pi + g_cs.playerPedOff, &ped) || !ped) return false;
    if (!RdU32(ped + 0x20, &mat) || !mat) return false;
    if (!RdF32(mat + 0x30, &x) || !RdF32(mat + 0x34, &y) || !RdF32(mat + 0x38, &z)) return false;
    return x > -20000 && x < 20000 && y > -20000 && y < 20000 && z > -2000 && z < 5000;
}
// The engine-sound slots, for the log only: SISCO reads these three and writes none of them. The game fills a table at
// startup by asking pc\audio\config\waveslots.xml for STREAM_ENGINE_1, _2 and so on until one is not declared, stopping
// at the limit in its own loop as well, and then plays one looping engine sound per distinct vehicle model near the
// player out of that table. Stock GTA IV declares 15, so a sixteenth distinct model nearby is silent; a raised car
// budget is what makes that reachable, which is why the figure belongs in the log beside the budget. Raising it is
// another mod's job: the ones that do it patch this loop's limit, the table and the audio heap, and SISCO touches
// none of those three.
static void AudioNote(const CoreSites& c, char* out, size_t outCap) {
    uint8_t ins[3] = { 0 }, heap[5] = { 0 };
    uint32_t slots = 0;
    bool read = false;
    __try {
        memcpy(ins, (const void*)c.audioSlotCap, 3);
        memcpy(heap, (const void*)c.audioHeap, 5);
        read = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { read = false; }
    RdU32(c.audioSlots, &slots);
    if (!read || ins[0] != 0x83 || ins[1] != c.audioCapModRm || heap[0] != 0xBE) {
        _snprintf_s(out, outCap, _TRUNCATE, "the engine sound slots are not where SISCO reads them: nothing to report");
        return;
    }
    uint32_t limit = ins[2], heapBytes = 0;
    memcpy(&heapBytes, heap + 1, 4);
    bool stockHeap = heapBytes == 0x7E00000u || heapBytes == 0x7300000u;   // 126 MiB, or 115 on a machine with little RAM
    _snprintf_s(out, outCap, _TRUNCATE, "%u engine sound slots, the game's own limit %u, audio heap %u MiB%s",
                slots, limit, heapBytes >> 20,
                limit != 25 || !stockHeap ? ": another mod has changed them"
                : slots && slots < limit ? "; a distinct car model past that many near you plays no engine sound" : "");
}

// What the sizing works from: the card's own figure, bounded by the player's -availablevidmem if they set one.
// That option is what the game believes it has, so a budget sized past it is one the game cannot reach, and the
// traffic budgets that scale off it would be taking their share of a pool that size instead.
static int64_t CardBudgetMb() {
    int64_t b = -1;
    if (g_dxgiState == 0) DxgiInit();
    int64_t mb = DxgiPoll(&b) ? b >> 20 : g_bootBudgetMb;
    if (g_playerVidmemMb > 0 && mb > g_playerVidmemMb) mb = g_playerVidmemMb;
    return mb;
}

static bool WorldReady(int64_t* xMb) {
    uint32_t n = 0; uint64_t x = 0;
    if (!RdU32(g_cs.streamerN, &n) || !n || !RdU64(g_cs.rt0, &x) || !x) return false;
    *xMb = (int64_t)(x >> 20);
    return true;
}
// The game's own decision about managed resources, read rather than assumed. SISCO writes the -managed launch
// option before the game reads it, but three things undo that and none of them are visible from the write: the
// player's own -unmanaged, the player's own -nominimize (which the game tests AFTER -managed, so it wins), and
// the Direct3D hook never firing, in which case nothing was written at all. Run D measured DXVK without managed
// resources collapsing the address space at a budget of 1400, well under the 4000 SISCO asks for, so a raised
// budget behind an unmanaged pool is the configuration to refuse.
// Only a confident 0 counts. The game writes 0 or 1 there and nothing else, so an unreadable slot or any other
// value means SISCO is not looking at what it thinks it is, and then it leaves the decision alone.
static bool PoolUnmanaged() {
    uint32_t mode = 0xFFFFFFFFu;
    if (!g_cs.poolMode || !RdU32(g_cs.poolMode, &mode)) return false;
    return mode == 0;
}

static void SisSize(int64_t xMb) {
    g_sized = true;
    g_carHeld = false;
    char audio[200]; AudioNote(g_cs, audio, sizeof(audio));
    SisLog("audio", "%s", audio);
    // The sliders decide how much world the game asks for, so no two reports compare without them. Read only.
    g_setView = g_setDetail = g_setCars = 0xFFFFFFFFu;
    RdU32(g_cs.setView, &g_setView); RdU32(g_cs.setDetail, &g_setDetail); RdU32(g_cs.setCars, &g_setCars);
    SisLog("display", "view distance %u, detail distance %u, car density %u", g_setView, g_setDetail, g_setCars);
    // What the game actually built, read back now that every plugin has loaded and every pool exists. A raise that
    // another plugin undid after SISCO is marked here, and the gate below then holds the budget.
    if (g_set.limits) { ReadBackRaises(g_cs); SisLog("built", "%s", g_builtNote); }
    // g_dxvk is what Direct3DCreate9 found. If the hook never fired it is still -1 here, and vulkan-1.dll answers
    // instead: DXVK loads it when it builds its Direct3D object, long before the world is ready, so by now it is
    // there. Both are facts about the running process, never a guess, and the log says which one decided.
    bool dxvk = g_dxvk == 1 || (g_dxvk < 0 && VulkanLoaded());
    const char* saw = g_dxvk < 0 ? " (Direct3DCreate9 was never seen; vulkan-1.dll is loaded)" : "";
    if (!dxvk) {
        _snprintf_s(g_sizeNote, sizeof(g_sizeNote), _TRUNCATE, "native Direct3D 9%s: not supported, SISCO requires DXVK (the vulkan.dll in "
                    "the download); the budget, the car and ped budgets are left alone", g_dxvk < 0 ? " (Direct3DCreate9 was not seen)" : "");
        SisLog("size", "%s", g_sizeNote);
        return;
    }
    uint32_t q = 2; RdU32(g_cs.quality, &q); if (q > 2) q = 2;
    // FusionFix's ExtraStreamingMemory shows as its hook on the credit function; the floor needs it first.
    uint8_t g0 = 0x55; __try { g0 = *(uint8_t*)g_cs.gafm; } __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_extraStreaming = g0 != 0x55;
    int tStock = StockFloorMb(g_cs, (int)q, g_extraStreaming);
    int64_t bMb = CardBudgetMb();
    const char* vmNote = (g_playerVidmemMb > 0 && bMb == g_playerVidmemMb) ? ", bounded by your own -availablevidmem" : "";
    // A player's own -memrestrict N: the game computes its budget from that figure and never reads the table, so
    // anything written there changes nothing, and the log must not say it did.
    uint32_t mrSlot = 0; RdU32(g_cs.prmMemRestrict, &mrSlot);
    int tVram = 0;
    int t = bMb > 0 ? SizeBudget(bMb, xMb, tStock, g_extraStreaming, &tVram) : tStock;
    uint64_t cell = 0; RdU64(g_cs.table + 8 * (45 + q), &cell);
    int cellMb = (int)(cell >> 20);
    bool keptCell = cellMb > t && cellMb <= T_MAX_MB;
    if (keptCell) t = cellMb;
    // A raised budget is only safe behind the things that were raised to survive it: the two pools it fills, the
    // release queue it overflows on the way out, and the arena it loads through. If any of those was refused the
    // budget stays the game's own, because raising it then would create exactly the load the refused raise existed to
    // carry. Measured: a dry link pool is fatal even with the lock fix; the quit burst passes 65,536 with no texture
    // pack at all; and a budget raised while the arena stays at the game's 160 MiB moves the problem to the arena
    // within about 80 s of driving, where it fails by fragmentation and the world silently disappears.
    // The arena belongs in this list because it is raised before Direct3D exists, inside the CRT initialisers, and a
    // refused raise there is the only thing standing between a raised budget and an empty world.
    const char* needs = g_site[S_LINK_SIZE].state != ST_ON ? "the link pool was not raised"
                      : g_site[S_SLOT_SIZE].state != ST_ON ? "the drawable-slot pool was not raised"
                      : !g_fixInstalled ? "the release-queue fix is not in"
                      : !g_linkFixInstalled ? "the link-pool lock fix is not in"
                      : g_site[S_ARENA_SIZE].state != ST_ON ? "the streaming arena was not raised"
                      : g_capNote[0] ? g_capNote
                      : mrSlot ? "your own -memrestrict sets the budget, and the game does not read the table then"
                      : PoolUnmanaged() ? (g_dxvk < 0 ? "SISCO never saw Direct3DCreate9, so -managed was never written and the game runs its pool unmanaged"
                                                     : "the game is not using Direct3D managed resources (-unmanaged or -nominimize is set)") : NULL;
    if (needs) t = tStock;
    g_sizedT = t; g_sizedTStock = tStock; g_sizedX = (int)xMb;
    bool wrote = needs ? false : WriteBudgetAll(g_cs.table, t);
    // Car and ped budgets: the game cut both to a third on a PC with under 1.4 GiB of RAM (0xA3ED0D); then they stay.
    uint32_t car = 0, ped = 0; RdU32(g_cs.vehBudget, &car); RdU32(g_cs.pedBudget, &ped);
    bool lowRam = car == CAR_STOCK / 3 && ped == PED_STOCK / 3;
    g_car.addr = g_cs.vehBudget; g_ped.addr = g_cs.pedBudget;
    if (lowRam) { g_car.target = car; g_ped.target = ped; }
    else {
        uint32_t wantCar = CarPedFor(CAR_STOCK, t, tStock), wantPed = CarPedFor(PED_STOCK, t, tStock);
        // The car budget is only worth raising as far as there are slots to hold the models it pays for.
        if (g_site[S_VSTRUCT].state != ST_ON && wantCar > CAR_NO_POOL) { wantCar = CAR_NO_POOL; g_carHeld = true; }
        g_car.on = g_ped.on = true;
        g_car.written = car; g_ped.written = ped;
        // A larger value that was already there is another mod's, exactly as it is when the guard meets one later.
        // Recording it here is what makes the brake and a resize leave it alone: without this the floor in
        // TrafficFollow is zero, and a value FusionFix set before the world loaded would be cut to the game's own.
        if (car > wantCar) g_car.fromOther = car;
        if (ped > wantPed) g_ped.fromOther = ped;
        TrafficSet(g_car, car > wantCar ? car : wantCar);
        TrafficSet(g_ped, ped > wantPed ? ped : wantPed);
    }
    if (wrote) BrakeStart(t, tStock);
    char held[320] = "";
    if (needs) _snprintf_s(held, sizeof(held), _TRUNCATE, ", LEFT AT THE GAME'S OWN: %s", needs);
    else if (!wrote) strcpy_s(held, " NOT WRITTEN");
    _snprintf_s(g_sizeNote, sizeof(g_sizeNote), _TRUNCATE,
                "DXVK%s; card budget %lld MB%s, render targets %lld MB, others %d MB%s: budget %d MB (the card allows %d, the game's "
                "own %d%s)%s; car %u MB, ped %u MB%s%s", saw, bMb, vmNote, xMb, OtherMb(xMb),
                g_extraStreaming ? ", FusionFix's ExtraStreamingMemory assumed (the credit function is hooked)" : "", t, tVram,
                tStock, keptCell ? "; a larger value already in the table kept" : "", held,
                g_car.target / 1000000, g_ped.target / 1000000,
                lowRam ? " (the game's low-memory values kept)"
                       : g_carHeld ? " (car held where the game's own VehicleStruct pool can use it)" : "",
                car > CarPedFor(CAR_STOCK, t, tStock) || ped > CarPedFor(PED_STOCK, t, tStock) ? " (a larger value another mod set kept)" : "");
    SisLog("size", "%s", g_sizeNote);
}
// The render targets grew (the player raised the resolution): lower the budget to what the card allows now. Never raised.
static void SisResize(int64_t nowMs, int64_t xMb) {
    if (xMb - g_sizedX < 8) return;
    int64_t bMb = CardBudgetMb();
    int t = bMb > 0 ? SizeBudget(bMb, xMb, g_sizedTStock, g_extraStreaming, NULL) : g_sizedTStock;
    g_sizedX = (int)xMb;
    if (!g_brakeOn || t >= g_brakeSt.t) return;
    int before = g_brakeSt.t;
    if (!WriteBudgetAll(g_cs.table, t)) return;
    g_brakeSt.t = t;
    if (g_brakeCfg.floorMb > t) g_brakeCfg.floorMb = t;
    SisLog("size", "render targets now %lld MB at %lld s: budget %d -> %d MB", xMb, nowMs / 1000, before, t);
    TrafficFollow(t, g_sizedTStock);
}
// The verdict on a load: the load line, and the slow rule.
static void LoadVerdict(int64_t nowMs, bool ceiling) {
    int64_t buildMs = ceiling ? nowMs - g_ldEdgeMs : g_ldChangeMs - g_ldEdgeMs;
    g_loadBuildS = buildMs / 1000;
    g_loadFps = -1;
    if (g_cs.frameCounter && !ceiling && buildMs >= 1000 && g_ldChangeFrame > g_worldFrame)
        g_loadFps = (int)(((int64_t)(g_ldChangeFrame - g_worldFrame) * 1000) / buildMs);
    char rate[64] = "";
    if (g_loadFps >= 0) _snprintf_s(rate, sizeof(rate), _TRUNCATE, " at %d fps", g_loadFps);
    else if (!g_cs.frameCounter) strcpy_s(rate, " (the frame rate is not read on this build)");
    if (ceiling) SisLog("load", "the render targets never held for %d s in the %lld s since the player appeared: the build's end could not be seen", LOAD_STEADY_TICKS, g_loadBuildS);
    else SisLog("load", "the world built in %lld s after the player appeared%s%s", g_loadBuildS, rate,
                g_presentImmediate ? " (presenting without waiting for VSync)" : "");
    // A slow load with -managed in effect is the wait players report: 2 s when nothing holds the build back, 27 to 61 s
    // behind VSync or a cap (runs L4, L7, L10). Said here, named as far as SISCO can see, and written by the worker
    // into a file beside the log.
    if (!g_managedOn || g_loadBuildS <= LOAD_SLOW_S) return;
    char seen[300];
    if (g_frameCap > 0) _snprintf_s(seen, sizeof(seen), _TRUNCATE, "DXVK's own frame cap of %d fps (%s)", g_frameCap, g_frameCapFrom);
    else if (g_forcedInterval >= 1) _snprintf_s(seen, sizeof(seen), _TRUNCATE, "VSync forced in DXVK's config (d3d9.presentInterval %d), which SISCO cannot present past", g_forcedInterval);
    else if (RtssLoaded()) strcpy_s(seen, "RTSS is loaded, and a frame limit set there is the likeliest cause");
    else strcpy_s(seen, "nothing SISCO can see: the driver's control panel (a Max Frame Rate for GTAIV.exe) or a limiter that hooks the game; "
                        "if no limit is set anywhere, this machine renders the world that slowly and this note is a false alarm");
    _snprintf_s(g_slowLoadNote, sizeof(g_slowLoadNote), _TRUNCATE,
                "the world took %lld s to build after the player appeared%s, with -managed in effect (about 2 s when nothing holds it back). "
                "That is the wait a frame-rate limit causes: %s. Lift the limit while the game loads, or set Budget=0 in SISCO.ini.",
                g_loadBuildS, rate, seen);
    SisLog("slowload", "%s", g_slowLoadNote);
    InterlockedExchange(&g_slowLoadPending, 1);
}
// Once a second from the worker, before the sizing: the load being timed, and the presents while its world builds.
static void LoadTick(int64_t nowMs, bool loadingUp, bool playerIn, bool ready, int64_t xMb) {
    uint32_t fr = 0; bool haveFrame = g_cs.frameCounter && RdU32(g_cs.frameCounter, &fr);
    if (loadingUp) {
        // A load begins (or the menu is up): a load still being timed is dropped, and its presents go back to the game's.
        if (g_ldEdgeMs) { g_ldEdgeMs = 0; g_presentImmediate = 0; }
        g_ldUp = true; g_ldPending = false;
        return;
    }
    if (g_ldUp) { g_ldUp = false; g_ldPending = true; }
    if (g_ldPending) {
        if (!playerIn) return;           // the main menu: the streamer is up and the flag down, but nobody is in the world
        g_ldPending = false;
        g_ldEdgeMs = g_worldMs = g_ldChangeMs = nowMs; g_worldFrame = g_ldChangeFrame = haveFrame ? fr : 0;
        g_ldX = ready ? xMb : -1; g_ldSteady = 0; g_ldVerdict = false; g_loadN++;
        if (g_swapVtblHooked) { g_immediatePresents = 0; g_presentImmediate = 1; }
        g_worldLogged = true;
        // The game's loading flag drops when the player entity exists, while the world still streams in around it
        // and the visible loading screen stays up until it has: the phase timed from here is that build.
        SisLog("world", "the player exists and the game's loading flag is down; the world builds from here (the loading screen may still show); render targets %lld MB%s",
               ready ? xMb : 0, ready ? "" : " (not built yet)");
        return;
    }
    if (!g_ldEdgeMs) return;
    int64_t x = ready ? xMb : -1;
    if (x != g_ldX) { g_ldX = x; g_ldSteady = 0; g_ldChangeMs = nowMs; g_ldChangeFrame = haveFrame ? fr : 0; }
    else g_ldSteady++;
    if (x >= 0 && (g_rtMin <= 0 || x < g_rtMin)) g_rtMin = x;
    // A set under 128 MB that is not twice the smallest seen is a part-built stage that can hold for a whole build
    // (the sizing's rule, at SETTLE_LONG_TICKS above): the build is not over until it has held that long.
    bool partBuilt = x < 128 && x < 2 * g_rtMin;
    bool built = x >= 0 && g_ldSteady >= LOAD_STEADY_TICKS && (!partBuilt || g_ldSteady >= SETTLE_LONG_TICKS);
    bool ceiling = nowMs >= g_ldEdgeMs + LOAD_CEILING_MS;
    if (!g_ldVerdict && (built || ceiling)) { g_ldVerdict = true; LoadVerdict(nowMs, ceiling && !built); }
    if (g_ldVerdict && nowMs >= g_ldEdgeMs + LOAD_HOLD_MS) {
        if (g_presentImmediate) {
            g_presentImmediate = 0;
            SisLog("present", "VSync handed back %lld s after the player appeared: %ld presents went out immediately while the world built",
                   (nowMs - g_ldEdgeMs) / 1000, g_immediatePresents);
        }
        g_ldEdgeMs = 0;
    }
}
// Once a second, from the worker thread. lMb: the largest free block of the address space, in MB.
static void SisTick(int64_t nowMs, int64_t lMb) {
    int64_t xMb = 0;
    g_tickMs = nowMs;
    uint32_t loading = 0; RdU32(g_cs.loading, &loading);
    bool loadingUp = (loading & 0xFF) != 0;
    bool ready = WorldReady(&xMb), playerIn = PlayerInWorld();
    LoadTick(nowMs, loadingUp, playerIn, ready, xMb);
    if (!g_sized) {
        // Size only when the streamer is up, the player is in the world (not at the main menu), the loading screen is
        // down and the render targets have held the same size for SETTLE_TICKS seconds: they are created in stages,
        // measured at 63 MB, then 468, then 797 at 4K, the player appears before the last stage, and a budget raised
        // against a part-built figure is raised while the world is still loading, which slows that load badly.
        // Under 128 MB the render targets are still their first build stage (63 MB at 4K, 28 at 1080p; finished
        // sets are 765 and 252), and on a heavy install that stage can hold for the whole save load, longer than
        // any number of steady ticks. Never size against it.
        if (!ready || loadingUp || !playerIn) { g_settleX = -1; g_settleTicks = 0; return; }
        if (g_rtMin <= 0 || xMb < g_rtMin) g_rtMin = xMb;
        if (xMb != g_settleX) { g_settleX = xMb; g_settleTicks = 0; return; }
        if (++g_settleTicks < SETTLE_TICKS) return;
        if (xMb < 128 && xMb < 2 * g_rtMin && g_settleTicks < SETTLE_LONG_TICKS) return;   // part-built, see SETTLE_LONG_TICKS
        SisSize(xMb);
        return;
    }
    TrafficGuard(g_ped); TrafficGuard(g_car);
    if (!g_brakeOn || !ready) return;
    SisResize(nowMs, xMb);
    uint64_t v = 0;
    if (!RdU64(g_cs.cacheV, &v)) return;
    BrakeTick(g_cs.table, nowMs, (int64_t)(v >> 20), xMb, lMb, loadingUp);
}

// ---------------------------------------------------------------------------------------------
// Install
// ---------------------------------------------------------------------------------------------
// The release-queue fix: the drain wrappers carry the refill, so the push hooks go in only when every one of them is
// in (four on 1.0.8.0, five on the Complete Edition).
// True when the fix went in.
static bool InstallQueueFix(const CoreSites& c) {
    g_oDrain = c.drain; g_cacheAddr = c.cache;
    g_fixInstalled = 0;          // cleared up front: every path out of here that does not install must leave it off,
                                 // or a drain would feed back a spill list that nothing is filling under the lock
    g_callDrain = c.build == BUILD_CE ? CallDrainCe : CallDrain1080;
    void* drainThunk = c.build == BUILD_CE ? (void*)ThunkDrainCe : (void*)ThunkDrain;
    for (int i = c.pushInN; i < 4; i++) SiteSet(S_PUSH_IN1 + i, ST_ABSENT, "");      // the sites this build does not have
    for (int i = c.drainSiteN; i < 5; i++) SiteSet(S_DRAIN1 + i, ST_ABSENT, "");
    for (int i = 0; i < c.drainSiteN; i++) {
        PatchCall(S_DRAIN1 + i, c.drainSite[i], c.drain, drainThunk);
    }
    bool drainsOn = true;
    for (int i = 0; i < c.drainSiteN; i++) drainsOn &= g_site[S_DRAIN1 + i].state == ST_ON;
    if (!drainsOn || !c.cache) {
        for (int i = 0; i < c.pushInN; i++) SiteSet(S_PUSH_IN1 + i, ST_OFF_DISABLED, "needs every drain wrapper");
        SiteSet(S_PUSH, ST_OFF_DISABLED, "needs every drain wrapper");
        return false;
    }
    static const uint8_t kPushPro1080[7] = { 0x83, 0xBE, 0x60, 0x07, 0x04, 0x00, 0x00 };   // cmp [esi+0x40760], 0
    static const uint8_t kPushProCe[5] = { 0x53, 0x8B, 0x5C, 0x24, 0x08 };                 // push ebx; mov ebx, [esp+8]
    const uint8_t* pro = c.build == BUILD_CE ? kPushProCe : kPushPro1080;
    size_t proN = c.build == BUILD_CE ? 5 : 7;
    // A1 [count] / 8D 0C 85 [array] / 83 C0 01 (the CE: 40) / A3 [count] / 89 31 or 89 39, as the loader rebased them
    uint8_t inl[22]; uint32_t cnt = (uint32_t)(c.cache + 0x40148), arr = (uint32_t)(c.cache + 0x148);
    int n = c.pushInLen, k = 0;
    inl[k++] = 0xA1; memcpy(inl + k, &cnt, 4); k += 4;
    inl[k++] = 0x8D; inl[k++] = 0x0C; inl[k++] = 0x85; memcpy(inl + k, &arr, 4); k += 4;
    if (n == 22) { inl[k++] = 0x83; inl[k++] = 0xC0; inl[k++] = 0x01; } else inl[k++] = 0x40;
    inl[k++] = 0xA3; memcpy(inl + k, &cnt, 4); k += 4;
    inl[k++] = 0x89;
    // Every push site is checked before any of them is written, the way the lock fix already works. Half of this fix
    // is worse than none of it: an unpatched inline copy goes on writing past the array that the patched sites stop
    // filling, and an unpatched entry leaves nothing to feed the spill list back that the patched copies fill.
    bool ok = k + 1 == n && BytesEq(c.push, pro, proN);
    if (!ok) SiteSet(S_PUSH, ST_OFF_MISMATCH, "site bytes differ at %08X", (unsigned)c.push);
    for (int i = 0; i < c.pushInN; i++) {
        inl[n - 1] = c.pushInEdi[i] ? 0x39 : 0x31;    // mov [ecx], edi or mov [ecx], esi
        if (k + 1 != n || !BytesEq(c.pushIn[i], inl, n)) {
            ok = false; SiteSet(S_PUSH_IN1 + i, ST_OFF_MISMATCH, "site bytes differ at %08X", (unsigned)c.pushIn[i]);
        }
    }
    if (!ok) {
        if (g_site[S_PUSH].state != ST_OFF_MISMATCH) SiteSet(S_PUSH, ST_OFF_MISMATCH, "another push site differs");
        for (int i = 0; i < c.pushInN; i++)
            if (g_site[S_PUSH_IN1 + i].state != ST_OFF_MISMATCH) SiteSet(S_PUSH_IN1 + i, ST_OFF_MISMATCH, "another push site differs");
        return false;                                  // the drain wrappers stay: bounded or not, they are harmless
    }
    // The inline copies go in first and the entry last, and a write that fails puts back the ones already written.
    // Then the only two orders the game can ever be left in are all of them or none, whatever VirtualProtect does.
    int wrote = 0;
    for (; wrote < c.pushInN; wrote++) {
        void* stub = c.pushInEdi[wrote] ? (void*)HkInlinePushEdi : (void*)HkInlinePush;
        uint8_t nb[7]; nb[0] = 0xE8; int32_t rel = (int32_t)((uintptr_t)stub - (c.pushIn[wrote] + 5)); memcpy(nb + 1, &rel, 4);
        nb[5] = 0xEB; nb[6] = (uint8_t)(n - 7);       // jmp over the rest of the copy to the next instruction
        if (!WriteCode(c.pushIn[wrote], nb, 7)) break;
    }
    bool entry = wrote == c.pushInN &&
                 (c.build == BUILD_CE ? PatchBytes(S_PUSH, c.push, kPushProCe, 5, (void*)HkPushCe, 0xE9)
                                      : PatchBytes(S_PUSH, c.push, kPushPro1080, 7, (void*)HkPush, 0xE9));
    if (!entry) {
        for (int i = 0; i < wrote; i++) {             // put the copies back exactly as they were
            inl[n - 1] = c.pushInEdi[i] ? 0x39 : 0x31;
            WriteCode(c.pushIn[i], inl, n);
        }
        for (int i = 0; i < c.pushInN; i++) SiteSet(S_PUSH_IN1 + i, ST_OFF_ERROR, "a push site could not be written: none were");
        SiteSet(S_PUSH, ST_OFF_ERROR, "a push site could not be written: none were");
        g_fixInstalled = 0;
        return false;
    }
    for (int i = 0; i < c.pushInN; i++) SiteSet(S_PUSH_IN1 + i, ST_ON, "");
    g_fixInstalled = 1;                               // every writer into the spill list now has its refill behind it
    return true;
}
// The lock fix goes in whole or not at all. Every site is checked before any is written; then the releases are written
// before the takes, so that even with the game running (it is not: this runs at load) a lock taken the old way is
// released the old way (LinkLockGive's foreign path) and never leaves an owner behind.
static bool InstallLinkLockFix(const CoreSites& c) {
    static const int ss[5] = { S_LINK_TAKE1, S_LINK_TAKE2, S_LINK_FREE1, S_LINK_FREE2, S_LINK_FREE3 };
    uint32_t la = (uint32_t)c.linkLock;
    uint8_t f[3][10]; size_t fn[3];
    for (int i = 0; i < 3; i++) fn[i] = InsnForm(f[i], c.linkFreeForm[i], la);
    bool ok = true;
    for (int i = 0; i < 2; i++) {
        uint8_t op = 0; int32_t rel = 0;
        __try { op = *(uint8_t*)c.linkTake[i]; rel = *(int32_t*)(c.linkTake[i] + 1); } __except (EXCEPTION_EXECUTE_HANDLER) { op = 0; }
        if (op != 0xE8 || c.linkTake[i] + 5 + rel != c.spinAcquire) { ok = false; SiteSet(ss[i], ST_OFF_MISMATCH, "not the call to the spinlock at %08X", (unsigned)c.linkTake[i]); }
    }
    for (int i = 0; i < 3; i++)
        if (!fn[i] || !BytesEq(c.linkFree[i], f[i], fn[i])) { ok = false; SiteSet(ss[2 + i], ST_OFF_MISMATCH, "site bytes differ at %08X", (unsigned)c.linkFree[i]); }
    if (!ok || !c.linkLock) {
        for (int i = 0; i < 5; i++) if (g_site[ss[i]].state != ST_OFF_MISMATCH) SiteSet(ss[i], ST_OFF_MISMATCH, "another site of the five differs");
        return false;
    }
    g_spinAcquire = c.spinAcquire; g_linkLockAddr = c.linkLock;
    PatchBytes(S_LINK_FREE1, c.linkFree[0], f[0], fn[0], (void*)StubLinkFree1, 0xE8);
    PatchBytes(S_LINK_FREE2, c.linkFree[1], f[1], fn[1], (void*)StubLinkFree1, 0xE8);
    PatchBytes(S_LINK_FREE3, c.linkFree[2], f[2], fn[2], (void*)StubLinkFree1, 0xE8);
    PatchCall(S_LINK_TAKE1, c.linkTake[0], c.spinAcquire, (void*)ThunkLinkTake1);
    PatchCall(S_LINK_TAKE2, c.linkTake[1], c.spinAcquire, (void*)ThunkLinkTake1);
    bool all = true; for (int i = 0; i < 5; i++) all &= g_site[ss[i]].state == ST_ON;
    g_linkFixInstalled = all;
    return all;
}
// The launch options' readers must be the ones read, or the slots are not written at all.
static bool ParamReadersMatch(const CoreSites& c) {
    return FormEq(c.rdUnmanaged, c.paramCmpForm, (uint32_t)c.prmUnmanaged) &&
           FormEq(c.rdManaged, c.paramCmpForm, (uint32_t)c.prmManaged) &&
           FormEq(c.rdVidmem, F_LOAD_EAX, (uint32_t)c.prmVidmem);
}
// Everything SISCO does at load, in order.
struct SisLoadResult { bool queue, lock, race, arena, d3d, params; };
// The frame counter is read for the load line only, and only while the instruction that advances it is where SISCO
// expects it (add dword ptr [counter], 1, its operand rebased as the loader did): a build that moved either would
// give a rate of nothing in particular.
static void PinFrameCounter(CoreSites& c) {
    if (!c.frameCounter || !c.frameInc) { c.frameCounter = 0; return; }
    uint8_t inc[7] = { 0x83, 0x05, 0, 0, 0, 0, 0x01 };
    uint32_t a = (uint32_t)c.frameCounter; memcpy(inc + 2, &a, 4);
    if (!BytesEq(c.frameInc, inc, 7)) c.frameCounter = 0;
}
static SisLoadResult SisInstall(const CoreSites& c) {
    SisLoadResult r = {};
    g_cs = c;
    PinFrameCounter(g_cs);
    if (g_set.fixes) {
        r.queue = InstallQueueFix(c);
        r.lock = InstallLinkLockFix(c);
        r.race = InstallRaceFix(c);
    }
    if (g_set.limits) {
        RaiseVehicleStruct(c, 100);
        RaisePools(c, 32768, 65536);
    }
    // Always, on every renderer. The arena has to be sized before the game builds it, which happens inside the
    // CRT initialisers, before WinMain and so before any Direct3D exists: there is nothing here to ask. SISCO used
    // to guess from a config file that only one install arrangement has, and a player running DXVK as their own
    // d3d9.dll was guessed wrong and left with a raised budget loading through a stock arena, which empties the
    // world. Raising it always is measured to cost nothing that matters: without DXVK the budget stays the game's
    // own, the extra room is never the binding limit, and a texture pack can exhaust the stock arena by itself.
    if (g_set.limits) r.arena = RaiseArena(c, 400);
    r.params = ParamReadersMatch(c);
    if (r.params) r.d3d = InstallD3DHook(c);
    else SiteSet(S_D3D_IAT, ST_OFF_MISMATCH, "a launch-option reader differs: nothing is set at Direct3DCreate9");
    return r;
}
#ifndef SISCO_TEST
static CoreSites Sites1080() {
    CoreSites c = {};
    SitesShape1080(c);
    c.cache = VA(D_CACHE); c.push = VA(A_PUSH);
    c.pushIn[0] = VA(A_PUSH_INL1); c.pushIn[1] = VA(A_PUSH_INL2); c.pushIn[2] = VA(A_PUSH_INL3);
    c.drain = VA(A_DRAIN); c.drainSite[0] = VA(A_DRAIN_SITE1); c.drainSite[1] = VA(A_DRAIN_SITE2);
    c.drainSite[2] = VA(A_DRAIN_SITE3); c.drainSite[3] = VA(A_DRAIN_SITE4);
    c.linkLock = VA(D_LINK_LOCK); c.spinAcquire = VA(A_SPIN_ACQUIRE);
    c.linkTake[0] = VA(A_LINK_TAKE1); c.linkTake[1] = VA(A_LINK_TAKE2);
    c.linkFree[0] = VA(A_LINK_FREE1); c.linkFree[1] = VA(A_LINK_FREE2); c.linkFree[2] = VA(A_LINK_FREE3);
    c.slotSize = VA(A_SLOT_SIZE); c.slotPool = VA(D_SLOT_POOL); c.linkSize = VA(A_LINK_SIZE); c.linkPool = VA(D_LINK_POOL);
    c.arenaSize = VA(A_ARENA_SIZE); c.arenaObj = VA(D_ARENA); c.initGuard = VA(D_INIT_GUARD);
    c.nativeInit = VA(A_NATIVE_INIT); c.nativeSizeSt = VA(A_NATIVE_SIZE_ST); c.nativePtrSt = VA(A_NATIVE_PTR_ST);
    c.nativeCountSt = VA(A_NATIVE_CNT_ST); c.nativePtr = VA(D_NATIVE_PTR); c.nativeSize = VA(D_NATIVE_SIZE); c.nativeCount = VA(D_NATIVE_COUNT);
    c.vstructName = VA(A_VSTRUCT_NAME); c.vstructSize = VA(A_VSTRUCT_SIZE); c.vstructPool = VA(D_VSTRUCT_POOL); c.vstructNameStr = VA(A_VSTRUCT_STR);
    c.d3dIat = VA(A_D3D_IAT); c.d3dThunk = VA(A_D3D_THUNK); c.d3dCall = VA(A_D3D_CALL);
    c.rdUnmanaged = VA(A_RD_UNMANAGED); c.rdManaged = VA(A_RD_MANAGED); c.rdVidmem = VA(A_RD_VIDMEM);
    c.prmUnmanaged = VA(D_PARAM_UNMANAGED); c.prmManaged = VA(D_PARAM_MANAGED); c.prmVidmem = VA(D_PARAM_VIDMEM);
    c.poolMode = VA(D_POOL_MODE);
    c.memRestrictTest = VA(A_MEMRESTRICT_TEST); c.prmNoMemRestrict = VA(D_PARAM_NOMEMRESTRICT); c.prmMemRestrict = VA(D_PARAM_MEMRESTRICT);
    c.setView = VA(D_SET_VIEW); c.setDetail = VA(D_SET_DETAIL); c.setCars = VA(D_SET_CARS);
    c.frameCounter = VA(D_FRAME); c.frameInc = VA(A_FRAME_INC);
    c.table = VA(D_TABLE); c.quality = VA(D_QUALITY); c.rt0 = VA(D_RT0); c.cacheV = VA(D_CACHE) + C_V;
    c.streamerN = VA(D_STREAMER) + 4; c.loading = VA(D_LOADING); c.vehBudget = VA(D_VEH_BUDGET); c.pedBudget = VA(D_PED_BUDGET);
    c.playerInfo = VA(D_PLAYERINFO);
    c.gafm = VA(A_GAFM);
    c.audioSlotCap = VA(A_AUDIO_SLOT_CAP); c.audioHeap = VA(A_AUDIO_HEAP); c.audioSlots = VA(D_AUDIO_SLOTS);
    return c;
}
// The Complete Edition. Same fields, its own addresses, and the shapes its compiler chose.
static CoreSites SitesCe() {
    CoreSites c = {};
    SitesShapeCe(c);
    c.cache = VA(CE_D_CACHE); c.push = VA(CE_A_PUSH);
    c.pushIn[0] = VA(CE_A_PUSH_INL1); c.pushIn[1] = VA(CE_A_PUSH_INL2);
    c.pushIn[2] = VA(CE_A_PUSH_INL3); c.pushIn[3] = VA(CE_A_PUSH_INL4);
    c.drain = VA(CE_A_DRAIN); c.drainSite[0] = VA(CE_A_DRAIN_SITE1); c.drainSite[1] = VA(CE_A_DRAIN_SITE2);
    c.drainSite[2] = VA(CE_A_DRAIN_SITE3); c.drainSite[3] = VA(CE_A_DRAIN_SITE4); c.drainSite[4] = VA(CE_A_DRAIN_SITE5);
    c.linkLock = VA(CE_D_LINK_LOCK); c.spinAcquire = VA(CE_A_SPIN_ACQUIRE);
    c.linkTake[0] = VA(CE_A_LINK_TAKE1); c.linkTake[1] = VA(CE_A_LINK_TAKE2);
    c.linkFree[0] = VA(CE_A_LINK_FREE1); c.linkFree[1] = VA(CE_A_LINK_FREE2); c.linkFree[2] = VA(CE_A_LINK_FREE3);
    c.slotSize = VA(CE_A_SLOT_SIZE); c.slotPool = VA(CE_D_SLOT_POOL); c.linkSize = VA(CE_A_LINK_SIZE); c.linkPool = VA(CE_D_LINK_POOL);
    c.arenaSize = VA(CE_A_ARENA_SIZE); c.arenaObj = VA(CE_D_ARENA); c.initGuard = VA(CE_D_INIT_GUARD);
    c.nativeInit = VA(CE_A_NATIVE_INIT); c.nativeSizeSt = VA(CE_A_NATIVE_SIZE_ST); c.nativePtrSt = VA(CE_A_NATIVE_PTR_ST);
    c.nativeCountSt = VA(CE_A_NATIVE_CNT_ST); c.nativePtr = VA(CE_D_NATIVE_PTR); c.nativeSize = VA(CE_D_NATIVE_SIZE); c.nativeCount = VA(CE_D_NATIVE_COUNT);
    c.vstructName = VA(CE_A_VSTRUCT_NAME); c.vstructSize = VA(CE_A_VSTRUCT_SIZE); c.vstructPool = VA(CE_D_VSTRUCT_POOL); c.vstructNameStr = VA(CE_A_VSTRUCT_STR);
    c.d3dIat = VA(CE_A_D3D_IAT); c.d3dThunk = 0; c.d3dCall = VA(CE_A_D3D_CALL);
    c.rdUnmanaged = VA(CE_A_RD_UNMANAGED); c.rdManaged = VA(CE_A_RD_MANAGED); c.rdVidmem = VA(CE_A_RD_VIDMEM);
    c.prmUnmanaged = VA(CE_D_PARAM_UNMANAGED); c.prmManaged = VA(CE_D_PARAM_MANAGED); c.prmVidmem = VA(CE_D_PARAM_VIDMEM);
    c.poolMode = VA(CE_D_POOL_MODE);
    c.memRestrictTest = VA(CE_A_MEMRESTRICT_TEST); c.prmNoMemRestrict = VA(CE_D_PARAM_NOMEMRESTRICT); c.prmMemRestrict = VA(CE_D_PARAM_MEMRESTRICT);
    c.setView = VA(CE_D_SET_VIEW); c.setDetail = VA(CE_D_SET_DETAIL); c.setCars = VA(CE_D_SET_CARS);
    c.frameCounter = 0; c.frameInc = 0;         // the Complete Edition's counter is not carried yet: no rate on its load line
    c.table = VA(CE_D_TABLE); c.quality = VA(CE_D_QUALITY); c.rt0 = VA(CE_D_RT0); c.cacheV = VA(CE_D_CACHE) + C_V;
    c.streamerN = VA(CE_D_STREAMER) + 4; c.loading = VA(CE_D_LOADING); c.vehBudget = VA(CE_D_VEH_BUDGET); c.pedBudget = VA(CE_D_PED_BUDGET);
    c.playerInfo = VA(CE_D_PLAYERINFO);
    c.gafm = VA(CE_A_GAFM);
    c.audioSlotCap = VA(CE_A_AUDIO_SLOT_CAP); c.audioHeap = VA(CE_A_AUDIO_HEAP); c.audioSlots = VA(CE_D_AUDIO_SLOTS);
    return c;
}
// The two builds SISCO knows: 1.0.8.0 and the Complete Edition 1.2.0.59. Anything else: log why and change nothing. Each
// is named by its PE header and then by code that build alone has, which on the Complete Edition also proves its
// encrypted megabyte has already been decrypted (both anchors live inside it and neither holds a relocated address).
static const char* g_buildName = "";
static CoreSites g_buildSites;
static bool IsKnownBuild(char* why, size_t cap) {
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)g_base;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(g_base + dos->e_lfanew);
    uint32_t soi = nt->OptionalHeader.SizeOfImage, tds = nt->FileHeader.TimeDateStamp;
    if (soi == 0x18B2000u && tds == 0x57C6FB75u) {
        static const uint8_t resc[7] = { 'R', 'E', 'S', 'C', '1', '0', 0 };
        if (!BytesEq(VA(A_RESC10_STR), resc, 7)) { _snprintf_s(why, cap, _TRUNCATE, "RESC10 string not at 0xEBDD00"); return false; }
        // The arena vtable's first slot holds 0x4555D0 in the file; rebase it the same way the loader did.
        uint32_t slot0 = 0; RdU32(VA(A_ARENA_VTBL), &slot0);
        if (slot0 != (uint32_t)VA(0x4555D0)) { _snprintf_s(why, cap, _TRUNCATE, "arena vtable slot 0 is %08X", slot0); return false; }
        g_buildSites = Sites1080(); g_buildName = "1.0.8.0";
        return true;
    }
    if (soi == 0x1BE6400u && tds == 0x63D3E735u) {
        static const uint8_t anchorA[20] = { 0x0F, 0x58, 0xC8, 0xF3, 0x0F, 0x11, 0x0A, 0xF3, 0x0F, 0x10, 0x49, 0x04, 0xF3, 0x0F, 0x59, 0x48, 0x14, 0xF3, 0x0F, 0x10 };
        static const uint8_t anchorB[20] = { 0x6A, 0x10, 0x51, 0x8D, 0x44, 0x24, 0x30, 0xC7, 0x04, 0x24, 0x00, 0x00, 0x80, 0x3F, 0x50, 0x8B, 0xCE, 0xE8, 0xD5, 0x76 };
        bool a = BytesEq(VA(CE_A_ANCHOR_A), anchorA, 20), b = BytesEq(VA(CE_A_ANCHOR_B), anchorB, 20);
        if (!a || !b) {                  // both wrong: the megabyte is still ciphertext. One wrong: something rewrote it.
            _snprintf_s(why, cap, _TRUNCATE, !a && !b ? "the Complete Edition's code is still encrypted where SISCO reads it"
                        : "the Complete Edition's code at %08X is not what SISCO read", (unsigned)(a ? CE_A_ANCHOR_B : CE_A_ANCHOR_A));
            return false;
        }
        g_buildSites = SitesCe(); g_buildName = "the Complete Edition 1.2.0.59";
        return true;
    }
    _snprintf_s(why, cap, _TRUNCATE, "SizeOfImage %08X, TimeDateStamp %08X: neither 1.0.8.0 nor the Complete Edition", soi, tds);
    return false;
}
static CoreSites GameCoreSites() { return g_buildSites; }
#endif
