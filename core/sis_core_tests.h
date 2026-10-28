// sis_core_tests.h: the core's tests. They are compiled into the release's own build (sis_test.exe), so what they
// exercise is the code that ships, down to the instruction. The includer defines
// CHECK and has initialised g_base, the clock and g_spillCs.

#include "sis_core_traces.h"   // recorded runs the brake is replayed against (tools/brake_trace.py writes it)

// ------------------------------------------------------------------ the release queue
// A cache as the game lays it out (array at +0x148, count at +0x40148, a real critical section at +0x40760); the push
// as 0x411450 has it (its first 7 bytes are the ones the fix checks); three 22-byte inline pushes with the cache's
// absolute addresses; a drain that releases every entry (sums them) and four call sites for it.
__declspec(align(16)) static uint8_t g_ctCache[0x40800];
static void* g_ctEnterCs = (void*)EnterCriticalSection;
static void* g_ctLeaveCs = (void*)LeaveCriticalSection;
__declspec(naked) static void CtPush() {
    __asm {
        cmp dword ptr [esi + 0x40760], 0
        push edi
        lea edi, [esi + 0x40760]
        je nolock
        push edi
        call dword ptr [g_ctEnterCs]
    nolock:
        mov eax, dword ptr [esi + 0x40148]
        mov edx, [esp + 8]
        lea ecx, [eax + 1]
        mov dword ptr [esi + 0x40148], ecx
        mov dword ptr [esi + eax*4 + 0x148], edx
        cmp dword ptr [edi], 0
        je nounlock
        push edi
        call dword ptr [g_ctLeaveCs]
    nounlock:
        mov al, 1
        pop edi
        ret 4
    }
}
__declspec(naked) static uint32_t __cdecl CtPushCaller(uint32_t) {
    __asm {
        push esi
        mov esi, offset g_ctCache
        push dword ptr [esp + 8]
        call CtPush
        and eax, 0xFF
        pop esi
        ret
    }
}
// The three inline pushes (value, which); returns 1 when ebx, edx and esi came through unchanged.
__declspec(naked) static uint32_t __cdecl CtInlinePushes(uint32_t, uint32_t) {
    __asm {
        push ebx
        push esi
        mov esi, [esp + 12]
        mov ecx, [esp + 16]
        mov edx, 0x77777777
        mov ebx, 0x66666666
        cmp ecx, 0
        jne w1
        mov eax, dword ptr [g_ctCache + 0x40148]
        lea ecx, [eax*4 + g_ctCache + 0x148]
        add eax, 1
        mov dword ptr [g_ctCache + 0x40148], eax
        mov dword ptr [ecx], esi
        jmp check
    w1:
        cmp ecx, 1
        jne w2
        mov eax, dword ptr [g_ctCache + 0x40148]
        lea ecx, [eax*4 + g_ctCache + 0x148]
        add eax, 1
        mov dword ptr [g_ctCache + 0x40148], eax
        mov dword ptr [ecx], esi
        jmp check
    w2:
        mov eax, dword ptr [g_ctCache + 0x40148]
        lea ecx, [eax*4 + g_ctCache + 0x148]
        add eax, 1
        mov dword ptr [g_ctCache + 0x40148], eax
        mov dword ptr [ecx], esi
    check:
        xor eax, eax
        cmp edx, 0x77777777
        jne bad
        cmp ebx, 0x66666666
        jne bad
        cmp esi, [esp + 12]
        jne bad
        mov eax, 1
    bad:
        pop esi
        pop ebx
        ret
    }
}
static uint64_t g_ctSum; static uint32_t g_ctEntries, g_ctMaxCount;
static void __cdecl CtDrainSumC(uint32_t cache) {
    LPCRITICAL_SECTION cs = (LPCRITICAL_SECTION)(uintptr_t)(cache + 0x40760);
    EnterCriticalSection(cs);
    volatile uint32_t* cnt = (volatile uint32_t*)(uintptr_t)(cache + 0x40148);
    uint32_t* arr = (uint32_t*)(uintptr_t)(cache + 0x148);
    if (*cnt > g_ctMaxCount) g_ctMaxCount = *cnt;
    for (uint32_t i = 0; i < *cnt && i < 0x10000; i++) { g_ctSum += arr[i]; g_ctEntries++; }
    *cnt = 0;
    LeaveCriticalSection(cs);
}
// 0x4114A0's convention: eax = cache, al = 1, esi/edi/ebp/ebx kept, ecx/edx clobbered.
__declspec(naked) static void CtDrain() {
    __asm {
        push esi
        push edi
        push eax
        call CtDrainSumC
        add esp, 4
        mov ecx, 0xDEAD
        mov edx, 0xBEEF
        mov eax, 0x12345601
        pop edi
        pop esi
        ret
    }
}
// Four call sites; returns al, or 0xBAD if ebx was not kept.
__declspec(naked) static uint32_t __cdecl CtDrainCallers(uint32_t) {
    __asm {
        push ebx
        mov ebx, 0x5A5A5A5A
        mov ecx, [esp + 8]
        mov eax, offset g_ctCache
        cmp ecx, 0
        jne c1
        call CtDrain
        jmp done
    c1:
        cmp ecx, 1
        jne c2
        call CtDrain
        jmp done
    c2:
        cmp ecx, 2
        jne c3
        call CtDrain
        jmp done
    c3:
        call CtDrain
    done:
        cmp ebx, 0x5A5A5A5A
        jne bad
        and eax, 0xFF
        pop ebx
        ret
    bad:
        mov eax, 0xBAD
        pop ebx
        ret
    }
}
static int CtFindCalls(void* fn, size_t len, void* to, uintptr_t* out, int max) {
    int n = 0; uint8_t* p = (uint8_t*)fn;
    for (size_t i = 0; i + 5 <= len && n < max; i++)
        if (p[i] == 0xE8 && (uintptr_t)(p + i + 5) + *(int32_t*)(p + i + 1) == (uintptr_t)to) out[n++] = (uintptr_t)(p + i);
    return n;
}
static int CtFindBytes(void* fn, size_t len, const uint8_t* b, size_t n, uintptr_t* out, int max) {
    int k = 0; uint8_t* p = (uint8_t*)fn;
    for (size_t i = 0; i + n <= len && k < max; i++) if (memcmp(p + i, b, n) == 0) out[k++] = (uintptr_t)(p + i);
    return k;
}

// ------------------------------------------------------------------ the link-pool lock
// The spinlock as the game uses it (0x456320 spins forever; this one gives up after half a second and counts it, so a
// deadlock is a FAIL, not a hang); a link free as 0xB721D0 (take, release with mov [lock], 0); a link alloc as 0xB72140
// that evicts under the lock, which frees a link (the nested take), then releases with mov [lock], ebx on one of two
// paths.
static volatile LONG g_ctLock, g_ctGaveUp, g_ctNone, g_ctAfterInner;
static void __cdecl CtSpin(volatile LONG* lock) {
    DWORD t0 = GetTickCount();
    while (InterlockedCompareExchange(lock, 1, 0) != 0) {
        if (GetTickCount() - t0 > 500) { InterlockedIncrement(&g_ctGaveUp); return; }
        Sleep(0);
    }
}
__declspec(naked) static void CtLinkFree() {
    __asm {
        push offset g_ctLock
        call CtSpin
        add esp, 4
        mov dword ptr [g_ctLock], 0
        ret
    }
}
__declspec(naked) static uint32_t __cdecl CtLinkAlloc() {
    __asm {
        push ebx
        xor ebx, ebx
        push offset g_ctLock
        call CtSpin
        add esp, 4
        call CtLinkFree
        mov eax, dword ptr [g_ctLock]
        mov dword ptr [g_ctAfterInner], eax
        cmp dword ptr [g_ctNone], 0
        jne none
        mov eax, 0x5A5A
        mov dword ptr [g_ctLock], ebx
        pop ebx
        ret
    none:
        mov eax, ebx
        mov dword ptr [g_ctLock], ebx
        pop ebx
        ret
    }
}

// ------------------------------------------------------------------ the native-table init
// Its entry (64 A1 2C 00 00 00, then ret) in executable memory; its three stores as the install checks them; the table's
// pointer and size each on a guard page, so the order of the stores can be seen; and a fake TLS array whose block holds a
// fake allocator at +8 (vtable slot 2), swapped into fs:[0x2C] only for the call.
static uint8_t* g_tNatEntry;
static uint8_t g_tNatS1[6], g_tNatS2[5], g_tNatS3[10];
static uint8_t* g_tNatPages;                 // two pages: the pointer at +0x10 of the first, the size at +0x10 of the second
static volatile uint32_t g_tNatCount;
static volatile uint32_t* TNatPtr() { return (volatile uint32_t*)(g_tNatPages + 0x10); }
static volatile uint32_t* TNatSize() { return (volatile uint32_t*)(g_tNatPages + 0x1010); }
static uint32_t g_tAllocArgs[3], g_tAllocCalls; static void* g_tAllocRet;
static void* __fastcall MockTlsAlloc(void* self, void* edx, uint32_t size, uint32_t align, uint32_t flags) {   // = thiscall, 3 args
    (void)self; (void)edx; g_tAllocCalls++; g_tAllocArgs[0] = size; g_tAllocArgs[1] = align; g_tAllocArgs[2] = flags; return g_tAllocRet;
}
static void* g_tAllocVtbl[4] = { 0, 0, (void*)MockTlsAlloc, 0 };
static void* g_tAllocObj[1] = { g_tAllocVtbl };
static uint8_t g_tTlsBlock[32];
static void* g_tTlsArray[2] = { g_tTlsBlock, 0 };
static volatile LONG g_tGuardPtr, g_tGuardSize;
static volatile uint32_t g_tAtPtrCount, g_tAtPtrZero, g_tAtPtrSizeHits, g_tAtSizePtrHits, g_tAtSizeAllocs;
static uint8_t* g_tNatTable; static uint32_t g_tNatCap;
static LONG CALLBACK TGuardVeh(EXCEPTION_POINTERS* ep) {
    if (ep->ExceptionRecord->ExceptionCode != STATUS_GUARD_PAGE_VIOLATION || ep->ExceptionRecord->NumberParameters < 2) return EXCEPTION_CONTINUE_SEARCH;
    ULONG_PTR at = ep->ExceptionRecord->ExceptionInformation[1];
    if (at == (ULONG_PTR)TNatPtr()) {            // the pointer is about to be stored: count, table and size as they are now
        g_tGuardPtr++; g_tAtPtrCount = g_tNatCount; g_tAtPtrSizeHits = (uint32_t)g_tGuardSize; g_tAtPtrZero = 1;
        if (g_tNatTable) for (uint32_t i = 0; i < g_tNatCap * 8; i++) if (g_tNatTable[i]) { g_tAtPtrZero = 0; break; }
        return EXCEPTION_CONTINUE_EXECUTION;      // the guard is gone; the store runs again and lands
    }
    if (at == (ULONG_PTR)TNatSize()) {           // the size is about to be stored: was the pointer stored, the table allocated?
        g_tGuardSize++; g_tAtSizePtrHits = (uint32_t)g_tGuardPtr; g_tAtSizeAllocs = g_tAllocCalls;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
static uint32_t g_tCallEsi, g_tCallRet, g_tAfter[4]; static uintptr_t g_tCallTarget;
// Calls g_tCallTarget with esi = g_tCallEsi and known ebx/edi/ebp; records eax and the four registers afterwards.
__declspec(naked) static void CallWithEsi() {
    __asm {
        pushad
        mov esi, [g_tCallEsi]
        mov ebx, 0x11111111
        mov edi, 0x22222222
        mov ebp, 0x33333333
        call dword ptr [g_tCallTarget]
        mov [g_tCallRet], eax
        mov [g_tAfter], esi
        mov [g_tAfter + 4], ebx
        mov [g_tAfter + 8], edi
        mov [g_tAfter + 12], ebp
        popad
        ret
    }
}
static void TNatArm(uint32_t cap, void* ret) {
    g_tNatCap = cap; g_tNatTable = (uint8_t*)ret; g_tAllocRet = ret; g_tAllocCalls = 0;
    if (ret) memset(ret, 0xCC, (size_t)cap * 8);
    *TNatPtr() = 0; *TNatSize() = 0; g_tNatCount = 0xDEAD;
    g_tGuardPtr = g_tGuardSize = 0; g_tAtPtrCount = 0x5A5A; g_tAtPtrZero = 0; g_tAtPtrSizeHits = 9; g_tAtSizePtrHits = 9; g_tAtSizeAllocs = 9;
    DWORD o;
    VirtualProtect(g_tNatPages, 0x1000, PAGE_READWRITE | PAGE_GUARD, &o);
    VirtualProtect(g_tNatPages + 0x1000, 0x1000, PAGE_READWRITE | PAGE_GUARD, &o);
}
static void TNatCall(uint32_t cap) {
    uint32_t saved = __readfsdword(0x2C);
    *(void**)(g_tTlsBlock + 8) = g_tAllocObj;
    g_tCallEsi = cap; g_tCallTarget = (uintptr_t)g_tNatEntry;
    __writefsdword(0x2C, (DWORD)(uintptr_t)g_tTlsArray);
    CallWithEsi();
    __writefsdword(0x2C, saved);
}

// ------------------------------------------------------------------ Direct3DCreate9
// A call, a thunk and an import slot, all mock; the Direct3D object answers DXVK's interop interface or not.
struct FakeD3D { void** vtbl; volatile LONG refs; int dxvk; };
static HRESULT __stdcall FakeD3DQI(FakeD3D* self, REFIID riid, void** out) {
    if (self->dxvk && IsEqualGUID(riid, kIID_D3D9VkInteropInterface)) { *out = self; InterlockedIncrement(&self->refs); return S_OK; }
    *out = NULL; return E_NOINTERFACE;
}
static ULONG __stdcall FakeD3DAddRef(FakeD3D* self) { return (ULONG)InterlockedIncrement(&self->refs); }
static ULONG __stdcall FakeD3DRelease(FakeD3D* self) { return (ULONG)InterlockedDecrement(&self->refs); }
struct FakeObj { void** vtbl; };
static DWORD g_tSwapFlags = 0xFFFFFFFFu; static volatile LONG g_tSwapPresents;
static HRESULT __stdcall FakeSwapPresent(void* sc, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD flags) { g_tSwapFlags = flags; InterlockedIncrement(&g_tSwapPresents); return S_OK; }
static ULONG __stdcall FakeNop(void*) { return 1; }
static volatile LONG g_tSwapRefs;                    // GetSwapChain adds one, Release takes one: the hook must leave it at zero
static ULONG __stdcall FakeSwapAddRef(void*) { return (ULONG)InterlockedIncrement(&g_tSwapRefs); }
static ULONG __stdcall FakeSwapRelease(void*) { return (ULONG)InterlockedDecrement(&g_tSwapRefs); }
static void* g_tSwapVtbl[4] = { (void*)FakeD3DQI, (void*)FakeSwapAddRef, (void*)FakeSwapRelease, (void*)FakeSwapPresent };
static FakeObj g_tSwap = { g_tSwapVtbl };
static HRESULT __stdcall FakeGetSwapChain(void*, UINT, void** out) { *out = &g_tSwap; InterlockedIncrement(&g_tSwapRefs); return S_OK; }
static void* g_tDevVtbl[18] = { (void*)FakeD3DQI, (void*)FakeNop, (void*)FakeNop, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, (void*)FakeGetSwapChain, 0, 0, 0 };
static FakeObj g_tDev = { g_tDevVtbl };
static volatile LONG g_tDevCreates;
static HRESULT __stdcall FakeCreateDevice(void*, UINT, UINT, HWND, DWORD, void*, void** out) { *out = &g_tDev; InterlockedIncrement(&g_tDevCreates); return S_OK; }
static void* g_tD3DVtbl[17] = { (void*)FakeD3DQI, (void*)FakeD3DAddRef, (void*)FakeD3DRelease, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, (void*)FakeCreateDevice };
static FakeD3D g_tD3D = { g_tD3DVtbl, 1, 1 };
static volatile UINT g_tD3DSdk; static volatile LONG g_tD3DCreates;
static void* WINAPI MockDirect3DCreate9(UINT sdk) { g_tD3DSdk = sdk; InterlockedIncrement(&g_tD3DCreates); return &g_tD3D; }
static uint32_t g_tIatSlot[1];
static uint8_t* g_tD3DCode;                  // +0: call rel32 -> +0x10; +0x10: jmp [g_tIatSlot]
static volatile uint32_t g_tPrm[3];          // the -unmanaged, -managed and -availablevidmem value slots
static uint8_t g_tRd[3][6];                  // their readers: cmp [slot], esi (x2) and mov eax, [slot]

// ------------------------------------------------------------------ launch-time sizes and the sizing
static uint8_t g_tV1Slot[5], g_tV1Link[5], g_tV1Arena[5], g_tV1VsName[5], g_tV1VsSize[4];
static uint32_t g_tV1SlotPool[16], g_tV1LinkPool[32], g_tV1ArenaObj[8], g_tV1Guard, g_tV1VsPool;
static char g_tV1VsStr[] = "VehicleStruct";
// The main heap's builder as the game has it: the call that reaches it, its four sizes, the object and the init guard;
// and the SMPA20 call with a stand-in for the fatal-error function, which records what it was called with.
static uint8_t g_tHpCall[5], g_tHpS1[5], g_tHpS2[10], g_tHpS3[5], g_tHpS4[5], g_tSmpaCall[5];
static uint32_t g_tHpObj[64], g_tHpGuard, g_tHpAudio[64], g_tHpAudioAt[2];
static uint32_t g_tFatalAsked, g_tFatalFree, g_tFatalN; static const char* g_tFatalCode;
static void __cdecl TestFatal(const char* code, uint32_t asked, uint32_t avail) { g_tFatalCode = code; g_tFatalAsked = asked; g_tFatalFree = avail; g_tFatalN++; }
static const char g_tSmpaCode[] = "SMPA20";
// The stock budget table of the 1.0.8.0 file (16 rows x 3 qualities, bytes).
static const uint64_t kCtStockTable[48] = {
    0xD200000, 0xF000000, 0x25800000, 0xD200000, 0xF000000, 0x25800000, 0xD200000, 0xF000000, 0x25800000,
    0xD200000, 0xF000000, 0x25800000, 0xD200000, 0xF000000, 0x11000000, 0xD200000, 0xF000000, 0x11000000,
    0xD200000, 0xF100000, 0x11000000, 0xD200000, 0xF100000, 0x11000000, 0x12C00000, 0x15E00000, 0x22600000,
    0x12C00000, 0x19000000, 0x2EE00000, 0x12C00000, 0x19000000, 0x2BC00000, 0x12C00000, 0x19000000, 0x2EE00000,
    0x12C00000, 0x15E00000, 0x22600000, 0x12C00000, 0x19000000, 0x2EE00000, 0x12C00000, 0x19000000, 0x2BC00000,
    0x12C00000, 0x19000000, 0x32000000 };
static uint64_t g_tTab[48], g_tRt0, g_tV;
static uint32_t g_tPoolMode;
static uint32_t g_tQual = 2, g_tStreamN, g_tLoad, g_tVeh, g_tPed;
static uint8_t g_tGafm[4] = { 0x55, 0x8B, 0xEC, 0x83 };
static uint8_t g_tMemRestrictTest[13] = { 0x83, 0x3D, 0x78, 0xAB, 0x0A, 0x01, 0x00, 0x0F, 0x85, 0x58, 0x01, 0x00, 0x00 };
static uint32_t g_tNoMemRestrict, g_tMemRestrict;
static uint32_t g_tSetView = 99, g_tSetDetail = 99, g_tSetCars = 50;
// The four size sites as the raises left them, and the VehicleStruct pool as the game built it, for the read-back at sizing.
static uint8_t g_tSzSlot[5], g_tSzLink[5], g_tSzArena[5], g_tSzVs[9];
static uint32_t g_tSzVsPoolObj[8], g_tSzVsPool;
// The player as the game keeps it: CPlayerInfo*[32] -> +0x58C the ped -> +0x20 its matrix -> +0x30 the position.
static float g_tPlayerMatrix[16]; static uint32_t g_tPlayerPed[0x100], g_tPlayerInfo[0x200];
static void TPlayerInWorld(bool yes) {
    g_tPlayerMatrix[12] = 100.0f; g_tPlayerMatrix[13] = 200.0f; g_tPlayerMatrix[14] = 30.0f;   // matrix + 0x30, +0x34, +0x38
    g_tPlayerPed[8] = (uint32_t)(uintptr_t)g_tPlayerMatrix;                                     // ped + 0x20
    g_tPlayerInfo[0x58C / 4] = (uint32_t)(uintptr_t)g_tPlayerPed;
    g_tPlayerInfo[0] = yes ? (uint32_t)(uintptr_t)g_tPlayerInfo : 0;                            // the game's pointer to it
}
static bool TTabAll(uint64_t mb) { for (int i = 0; i < 48; i++) if (g_tTab[i] != (mb << 20)) return false; return true; }
static void TSizeReset(int dxvk, int64_t bootMb, uint32_t veh, uint32_t ped) {
    memcpy(g_tTab, kCtStockTable, sizeof(g_tTab));
    g_tQual = 2; g_tRt0 = 799ull << 20; g_tV = 0; g_tStreamN = 1; g_tLoad = 0; g_tVeh = veh; g_tPed = ped; g_tGafm[0] = 0x55;
    g_cs.table = (uintptr_t)g_tTab; g_cs.quality = (uintptr_t)&g_tQual; g_cs.rt0 = (uintptr_t)&g_tRt0; g_cs.cacheV = (uintptr_t)&g_tV;
    g_cs.streamerN = (uintptr_t)&g_tStreamN; g_cs.loading = (uintptr_t)&g_tLoad; g_cs.vehBudget = (uintptr_t)&g_tVeh;
    g_cs.pedBudget = (uintptr_t)&g_tPed; g_cs.gafm = (uintptr_t)g_tGafm;
    // The row the game runs on: stock test bytes and no -nomemrestrict means the vendor rows, 550 at High.
    g_tMemRestrictTest[0] = 0x83; g_tNoMemRestrict = 0; g_tMemRestrict = 0; g_cs.build = BUILD_1080;
    { uint32_t a = (uint32_t)(uintptr_t)&g_tNoMemRestrict; memcpy(g_tMemRestrictTest + 2, &a, 4); }   // the operand as the loader relocates it
    g_cs.memRestrictTest = (uintptr_t)g_tMemRestrictTest; g_cs.prmNoMemRestrict = (uintptr_t)&g_tNoMemRestrict; g_cs.prmMemRestrict = (uintptr_t)&g_tMemRestrict;
    g_tSetView = 99; g_tSetDetail = 99; g_tSetCars = 50;
    g_cs.setView = (uintptr_t)&g_tSetView; g_cs.setDetail = (uintptr_t)&g_tSetDetail; g_cs.setCars = (uintptr_t)&g_tSetCars;
    g_cs.playerInfo = (uintptr_t)g_tPlayerInfo; g_cs.playerPedOff = 0x58Cu; TPlayerInWorld(true);
    g_playerVidmemMb = 0; g_vidmemSetMb = 0;
    g_sized = false; g_worldLogged = false; g_rtMin = 0; g_settleX = -1; g_settleTicks = 0;
    g_dxvk = dxvk; g_bootBudgetMb = bootMb; g_dxgiState = -1;   // no DXGI: the sizing uses the boot budget
    g_brakeOn = 0; g_brakeSt.t = 0; g_brakeSt.held = false; g_brakeCfg.floorMb = 800;
    g_car.on = g_ped.on = false;
    g_car.target = g_car.written = g_car.fromOther = g_ped.target = g_ped.written = g_ped.fromOther = g_car.sized = g_ped.sized = 0;
    // The budget is only raised behind the two pools, the queue fix and the arena; these cases are about the sizing
    // itself, so they start from an install where all four went in. That they were refused is its own test below.
    SiteSet(S_LINK_SIZE, ST_ON, ""); SiteSet(S_SLOT_SIZE, ST_ON, ""); SiteSet(S_ARENA_SIZE, ST_ON, "");
    SiteSet(S_VSTRUCT, ST_ON, ""); g_fixInstalled = g_linkFixInstalled = 1;
    g_tPoolMode = 1; g_cs.poolMode = (uintptr_t)&g_tPoolMode;   // the game decided managed, which is the normal case
    // The raises as they were left and the pool as the game built it from them: the read-back at sizing finds everything in place.
    static const uint8_t vs[9] = { 0x6A, 100, 0x8B, 0xC8, 0xE8, 0xF7, 0x9A, 0xDA, 0xFF };
    uint32_t sl = 32768, li = 65536, ar = 400u * 1024u;
    g_tSzSlot[0] = 0x68; memcpy(g_tSzSlot + 1, &sl, 4); g_tSzLink[0] = 0x68; memcpy(g_tSzLink + 1, &li, 4); g_tSzArena[0] = 0xBF; memcpy(g_tSzArena + 1, &ar, 4);
    memcpy(g_tSzVs, vs, 9); memset(g_tSzVsPoolObj, 0, sizeof(g_tSzVsPoolObj)); g_tSzVsPoolObj[2] = 100; g_tSzVsPool = (uint32_t)(uintptr_t)g_tSzVsPoolObj;
    g_cs.slotSize = (uintptr_t)g_tSzSlot; g_cs.linkSize = (uintptr_t)g_tSzLink; g_cs.arenaSize = (uintptr_t)g_tSzArena;
    g_cs.vstructSize = (uintptr_t)g_tSzVs; g_cs.vstructPool = (uintptr_t)&g_tSzVsPool;
    g_raised[S_SLOT_SIZE] = 32768; g_raised[S_LINK_SIZE] = 65536; g_raised[S_ARENA_SIZE] = 400u * 1024u; g_raised[S_VSTRUCT] = 100;
    g_dxvkVer[0] = g_dxvkVer[1] = g_dxvkVer[2] = -1;
    g_tWine = 0; g_wineNote[0] = 0; g_tAddrSpace.largest = g_tAddrSpace.total = 0; g_tAddrSpace.blocks = 0;
    g_asView = 0; g_procMaps[0] = 0; g_tProcRoot = NULL;
    g_worldMs = 0; g_loadBuildS = 0;
    g_ldUp = false; g_ldUpMs = 0; g_ldPending = true; g_ldEdgeMs = 0; g_ldSettleMs = 0; g_ldX = -1; g_ldSteady = 0; g_ldVerdict = false; g_loadN = 0;   // as at the start: the next player is a load's edge
    g_presentSkip = 0; g_skippedPresents = 0; g_lastShownMs = 0; g_tPresentMs = -1;
    g_showMs = PRESENT_SHOW_MS; g_menuTicks = 0;
    g_skipRtEnd = 0; g_skipEndedRt = 0;
    g_managedOn = true;
}

// ------------------------------------------------------------------ the Complete Edition's shapes
// The same code as CE's compiler emitted it, so the thunks and expected bytes SISCO uses only there are proven here:
// the push takes the cache in ecx, drops a null value and returns 0; the four inline copies are 20 bytes and two of
// them store edi; the drain takes the cache in ecx; the lock's three releases are mov [lock], 0 / mov [lock], eax /
// mov [lock], 0; the native-table init takes its capacity on the stack, returns the table's pointer and ends ret 4.
__declspec(naked) static void CtPushCe() {
    __asm {
        push ebx
        mov ebx, dword ptr [esp + 8]
        push edi
        mov edi, ecx
        test ebx, ebx
        je done
        cmp dword ptr [edi + 0x40760], 0
        push esi
        lea esi, [edi + 0x40760]
        je nolock
        push esi
        call dword ptr [g_ctEnterCs]
    nolock:
        mov eax, dword ptr [edi + 0x40148]
        lea ecx, [edi + eax * 4]
        inc eax
        mov dword ptr [edi + 0x40148], eax
        mov dword ptr [ecx + 0x148], ebx
        cmp dword ptr [esi], 0
        je nounlock
        push esi
        call dword ptr [g_ctLeaveCs]
    nounlock:
        pop esi
    done:
        pop edi
        xor eax, eax
        pop ebx
        ret 4
    }
}
// 1 when the push returned 0 and ebx, esi and edi came back unchanged.
__declspec(naked) static uint32_t __cdecl CtPushCallerCe(uint32_t) {
    __asm {
        push ebx
        push esi
        push edi
        mov ebx, 0x11111111
        mov esi, 0x22222222
        mov edi, 0x33333333
        mov ecx, offset g_ctCache
        push dword ptr [esp + 16]
        call CtPushCe
        test eax, eax
        jne bad
        cmp ebx, 0x11111111
        jne bad
        cmp esi, 0x22222222
        jne bad
        cmp edi, 0x33333333
        jne bad
        mov eax, 1
        jmp fin
    bad:
        xor eax, eax
    fin:
        pop edi
        pop esi
        pop ebx
        ret
    }
}
// The four 20-byte inline copies (value, which): the first and the fourth store edi, the other two esi.
__declspec(naked) static uint32_t __cdecl CtInlinePushesCe(uint32_t, uint32_t) {
    __asm {
        push ebx
        push esi
        push edi
        mov edi, [esp + 16]
        mov esi, edi
        mov ecx, [esp + 20]
        mov edx, 0x77777777
        mov ebx, 0x66666666
        cmp ecx, 0
        jne w1
        mov eax, dword ptr [g_ctCache + 0x40148]
        lea ecx, [eax * 4 + g_ctCache + 0x148]
        inc eax
        mov dword ptr [g_ctCache + 0x40148], eax
        mov dword ptr [ecx], edi
        jmp check
    w1:
        cmp ecx, 1
        jne w2
        mov eax, dword ptr [g_ctCache + 0x40148]
        lea ecx, [eax * 4 + g_ctCache + 0x148]
        inc eax
        mov dword ptr [g_ctCache + 0x40148], eax
        mov dword ptr [ecx], esi
        jmp check
    w2:
        cmp ecx, 2
        jne w3
        mov eax, dword ptr [g_ctCache + 0x40148]
        lea ecx, [eax * 4 + g_ctCache + 0x148]
        inc eax
        mov dword ptr [g_ctCache + 0x40148], eax
        mov dword ptr [ecx], esi
        jmp check
    w3:
        mov eax, dword ptr [g_ctCache + 0x40148]
        lea ecx, [eax * 4 + g_ctCache + 0x148]
        inc eax
        mov dword ptr [g_ctCache + 0x40148], eax
        mov dword ptr [ecx], edi
    check:
        xor eax, eax
        cmp edx, 0x77777777
        jne bad
        cmp ebx, 0x66666666
        jne bad
        cmp esi, [esp + 16]
        jne bad
        cmp edi, [esp + 16]
        jne bad
        mov eax, 1
    bad:
        pop edi
        pop esi
        pop ebx
        ret
    }
}
__declspec(naked) static void CtDrainCe() {
    __asm {
        push esi
        push edi
        push ebx
        push ecx
        call CtDrainSumC
        add esp, 4
        mov eax, 0x12345601
        pop ebx
        pop edi
        pop esi
        ret
    }
}
// Five call sites, the cache in ecx; returns al, or 0xBAD if ebx was not kept.
__declspec(naked) static uint32_t __cdecl CtDrainCallersCe(uint32_t) {
    __asm {
        push ebx
        mov ebx, 0x5A5A5A5A
        mov eax, [esp + 8]
        mov ecx, offset g_ctCache
        cmp eax, 0
        jne c1
        call CtDrainCe
        jmp done
    c1:
        cmp eax, 1
        jne c2
        call CtDrainCe
        jmp done
    c2:
        cmp eax, 2
        jne c3
        call CtDrainCe
        jmp done
    c3:
        cmp eax, 3
        jne c4
        call CtDrainCe
        jmp done
    c4:
        call CtDrainCe
    done:
        cmp ebx, 0x5A5A5A5A
        jne bad
        and eax, 0xFF
        pop ebx
        ret
    bad:
        mov eax, 0xBAD
        pop ebx
        ret
    }
}
static volatile LONG g_ctLockCe;
__declspec(naked) static void CtLinkFreeCe() {
    __asm {
        push offset g_ctLockCe
        call CtSpin
        add esp, 4
        mov dword ptr [g_ctLockCe], 0
        ret
    }
}
__declspec(naked) static uint32_t __cdecl CtLinkAllocCe() {
    __asm {
        push offset g_ctLockCe
        call CtSpin
        add esp, 4
        call CtLinkFreeCe
        mov eax, dword ptr [g_ctLockCe]
        mov dword ptr [g_ctAfterInner], eax
        cmp dword ptr [g_ctNone], 0
        jne none
        mov eax, 0x5A5A
        mov dword ptr [g_ctLockCe], 0
        ret
    none:
        xor eax, eax
        mov dword ptr [g_ctLockCe], eax
        ret
    }
}
// Calls g_tCallTarget with the capacity as a stack argument (the callee pops it) and known registers. The stack
// pointer is kept from before and after the call: equal means the callee popped the argument, as ret 4 must.
static uint32_t g_tEspBefore, g_tEspAfter;
__declspec(naked) static void CallWithStackArg() {
    __asm {
        pushad
        mov ebx, 0x11111111
        mov esi, 0x44444444
        mov edi, 0x22222222
        mov ebp, 0x33333333
        mov [g_tEspBefore], esp
        push dword ptr [g_tCallEsi]
        call dword ptr [g_tCallTarget]
        mov [g_tEspAfter], esp
        mov [g_tCallRet], eax
        mov [g_tAfter], esi
        mov [g_tAfter + 4], ebx
        mov [g_tAfter + 8], edi
        mov [g_tAfter + 12], ebp
        popad
        ret
    }
}
static void TNatCallCe(uint32_t cap) {
    uint32_t saved = __readfsdword(0x2C);
    *(void**)(g_tTlsBlock + 8) = g_tAllocObj;
    g_tCallEsi = cap; g_tCallTarget = (uintptr_t)g_tNatEntry;
    __writefsdword(0x2C, (DWORD)(uintptr_t)g_tTlsArray);
    CallWithStackArg();
    __writefsdword(0x2C, saved);
}

// What the running game could use on its own at this texture quality: the smallest of the four rows it can reach,
// worked out here from the test's own copy of the table rather than from the function under test.
static int TStockFloor(int q) {                     // the vendor rows 12 to 14: what the game runs on without FusionFix
    uint64_t lo = 0;
    for (int row = 12; row <= 14; row++) { uint64_t v = kCtStockTable[3 * row + q]; if (!lo || v < lo) lo = v; }
    return (int)(lo >> 20);
}
static int TRow15(int q) { return (int)(kCtStockTable[3 * 15 + q] >> 20); }   // what it runs on under FusionFix

// The sizing waits for the render targets to settle: three ticks with the same X and no loading screen.
static void TSizeSettle(int64_t ms0, int64_t lMb) { for (int i = 0; i < SETTLE_TICKS + 2; i++) SisTick(ms0 + i * 1000, lMb); }

static void CoreTests() {
    // ---- the release-queue fix: a burst past the game's 65,536 is held and released, nothing lost or overwritten
    {
        InitializeCriticalSection((LPCRITICAL_SECTION)(g_ctCache + 0x40760));
        CoreSites c = {};
        SitesShape1080(c);
        c.cache = (uintptr_t)g_ctCache; c.push = (uintptr_t)CtPush; c.drain = (uintptr_t)CtDrain;
        uintptr_t ds[4]; int nd = CtFindCalls((void*)CtDrainCallers, 120, (void*)CtDrain, ds, 4);
        uint8_t inl[22]; uint32_t cnt = (uint32_t)(uintptr_t)(g_ctCache + 0x40148), arr = (uint32_t)(uintptr_t)(g_ctCache + 0x148);
        inl[0] = 0xA1; memcpy(inl + 1, &cnt, 4); inl[5] = 0x8D; inl[6] = 0x0C; inl[7] = 0x85; memcpy(inl + 8, &arr, 4);
        inl[12] = 0x83; inl[13] = 0xC0; inl[14] = 0x01; inl[15] = 0xA3; memcpy(inl + 16, &cnt, 4); inl[20] = 0x89; inl[21] = 0x31;
        uintptr_t in[3]; int ni = CtFindBytes((void*)CtInlinePushes, 200, inl, 22, in, 3);
        CHECK(nd == 4 && ni == 3, "core queue: four drain call sites and three inline pushes found (%d, %d)", nd, ni);
        for (int i = 0; i < 4 && i < nd; i++) c.drainSite[i] = ds[i];
        for (int i = 0; i < 3 && i < ni; i++) c.pushIn[i] = in[i];

        // All or nothing. Half of this fix is worse than none: an inline copy left alone goes on writing past the
        // array the hooked sites stop filling, and an entry left alone leaves nothing to drain what they spill.
        {
            uint8_t keepPro[7]; memcpy(keepPro, (void*)c.push, 7);
            uint8_t keepCall[4][5];
            for (int i = 0; i < 4; i++) memcpy(keepCall[i], (void*)c.drainSite[i], 5);
            uint8_t orig = *(uint8_t*)(c.pushIn[1] + 1), flip = (uint8_t)(orig ^ 0x40);
            WriteCode(c.pushIn[1] + 1, &flip, 1);                          // one byte of one inline copy
            bool refused = !InstallQueueFix(c);
            bool untouched = memcmp((void*)c.push, keepPro, 7) == 0;
            for (int i = 0; i < 3; i++) untouched &= *(uint8_t*)c.pushIn[i] != 0xE8;
            CHECK(refused && untouched && !g_fixInstalled && g_site[S_PUSH].state == ST_OFF_MISMATCH,
                  "core queue: one inline copy that differs leaves every push site unwritten and the fix off "
                  "(refused %d, untouched %d, fix %d, entry %s)", refused, untouched, g_fixInstalled, StName(g_site[S_PUSH].state));
            WriteCode(c.pushIn[1] + 1, &orig, 1);
            for (int i = 0; i < 4; i++) WriteCode(c.drainSite[i], keepCall[i], 5);   // the wrappers went in: put them back
        }
        bool on = InstallQueueFix(c);
        bool all = on && g_site[S_PUSH].state == ST_ON;    // the fourth inline copy is the Complete Edition's, tested below
        for (int i = 0; i < 3; i++) all &= g_site[S_PUSH_IN1 + i].state == ST_ON;
        for (int i = 0; i < 4; i++) all &= g_site[S_DRAIN1 + i].state == ST_ON;
        CHECK(all, "core queue: the fix and its four drain wrappers installed");
        uint64_t sum = 0; uint32_t pushed = 0; bool regs = true;
        for (uint32_t v = 1; v <= 65536 + 1000; v++) { if (CtPushCaller(v) != 1) regs = false; sum += v; pushed++; }
        for (uint32_t k = 0; k < 3; k++) { uint32_t v = 0x70000000u + k; if (CtInlinePushes(v, k) != 1) regs = false; sum += v; pushed++; }
        uint32_t count = *(uint32_t*)(g_ctCache + 0x40148);
        CHECK(regs && count == 0x10000 && g_spillN == 1003, "core queue: the array stops at 65,536 (%u), 1,003 held aside (%ld), registers kept", count, g_spillN);
        g_ctSum = 0; g_ctEntries = 0; g_ctMaxCount = 0;
        uint32_t r = CtDrainCallers(1);
        CHECK(r == 1 && g_ctEntries == pushed && g_ctSum == sum && g_spillN == 0 && *(uint32_t*)(g_ctCache + 0x40148) == 0 && g_ctMaxCount <= 0x10000,
              "core queue: one drain releases all %u of %u entries exactly once (sums %s), ebx kept, al %u", g_ctEntries, pushed, g_ctSum == sum ? "equal" : "DIFFER", r);

    }
    // ---- the lock fix: without it the eviction under the lock waits on itself; with it the holder goes through
    {
        uintptr_t tk[2]; int nt = CtFindCalls((void*)CtLinkAlloc, 80, (void*)CtSpin, tk, 1) + CtFindCalls((void*)CtLinkFree, 40, (void*)CtSpin, tk + 1, 1);
        uint32_t la = (uint32_t)(uintptr_t)&g_ctLock;
        uint8_t f12[6] = { 0x89, 0x1D }; memcpy(f12 + 2, &la, 4);
        uint8_t f3[10] = { 0xC7, 0x05 }; memcpy(f3 + 2, &la, 4); memset(f3 + 6, 0, 4);
        uintptr_t fr[3]; int nf = CtFindBytes((void*)CtLinkAlloc, 80, f12, 6, fr, 2) + CtFindBytes((void*)CtLinkFree, 40, f3, 10, fr + 2, 1);
        CHECK(nt == 2 && nf == 3, "core lock: two takes and three releases found (%d, %d)", nt, nf);
        g_ctGaveUp = 0; g_ctLock = 0; g_ctNone = 0;
        CtLinkAlloc();
        CHECK(g_ctGaveUp == 1 && g_ctLock == 0, "core lock control: without the fix the eviction waits on the lock its own thread holds");
        CoreSites c = {};
        SitesShape1080(c);
        c.linkLock = (uintptr_t)&g_ctLock; c.spinAcquire = (uintptr_t)CtSpin;
        c.linkTake[0] = tk[0]; c.linkTake[1] = tk[1]; c.linkFree[0] = fr[0]; c.linkFree[1] = fr[1]; c.linkFree[2] = fr[2];
        uint8_t keep0[6]; memcpy(keep0, (void*)fr[0], 6); uint8_t keepT[5]; memcpy(keepT, (void*)tk[0], 5);
        uint8_t wrong = (uint8_t)(*(uint8_t*)(fr[2] + 1) ^ 1); WriteCode(fr[2] + 1, &wrong, 1);
        CHECK(!InstallLinkLockFix(c) && memcmp((void*)fr[0], keep0, 6) == 0 && memcmp((void*)tk[0], keepT, 5) == 0,
              "core lock: one of the five differs: none is patched");
        wrong ^= 1; WriteCode(fr[2] + 1, &wrong, 1);
        CHECK(InstallLinkLockFix(c) && g_linkFixInstalled, "core lock: all five sites patched");
        g_ctGaveUp = 0;
        g_ctAfterInner = 9;
        uint32_t a = CtLinkAlloc(); LONG inner = g_ctAfterInner; g_ctNone = 1; uint32_t b = CtLinkAlloc(); g_ctNone = 0;
        CHECK(a == 0x5A5A && b == 0 && g_ctGaveUp == 0 && g_ctLock == 0 && g_linkLockOwner == 0 && g_linkLockDepth == 0,
              "core lock: with the fix the holder goes through, both release paths free the lock, no owner left");
        CHECK(inner == 1, "core lock: the nested release leaves the lock held for the outer holder (%ld)", inner);
    }
    // ---- the native-table race fix
    {
        g_tNatEntry = (uint8_t*)VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        static const uint8_t entry[8] = { 0x64, 0xA1, 0x2C, 0x00, 0x00, 0x00, 0xC3, 0xCC };
        memcpy(g_tNatEntry, entry, 8);
        g_tNatPages = (uint8_t*)VirtualAlloc(NULL, 0x2000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        uint32_t sz = (uint32_t)(uintptr_t)TNatSize(), pt = (uint32_t)(uintptr_t)TNatPtr(), ct = (uint32_t)(uintptr_t)&g_tNatCount;
        g_tNatS1[0] = 0x89; g_tNatS1[1] = 0x35; memcpy(g_tNatS1 + 2, &sz, 4);
        g_tNatS2[0] = 0xA3; memcpy(g_tNatS2 + 1, &pt, 4);
        g_tNatS3[0] = 0xC7; g_tNatS3[1] = 0x05; memcpy(g_tNatS3 + 2, &ct, 4); memset(g_tNatS3 + 6, 0, 4);
        CoreSites c = {};
        SitesShape1080(c);
        c.nativeInit = (uintptr_t)g_tNatEntry; c.nativeSizeSt = (uintptr_t)g_tNatS1; c.nativePtrSt = (uintptr_t)g_tNatS2; c.nativeCountSt = (uintptr_t)g_tNatS3;
        c.nativePtr = pt; c.nativeSize = sz; c.nativeCount = ct;
        g_tNatS2[1] ^= 4;
        CHECK(!InstallRaceFix(c) && g_site[S_NATIVE_INIT].state == ST_OFF_MISMATCH && memcmp(g_tNatEntry, entry, 8) == 0,
              "race fix: refused when one of its stores differs, the entry untouched");
        g_tNatS2[1] ^= 4;
        CHECK(InstallRaceFix(c) && g_tNatEntry[0] == 0xE9 && g_tNatEntry[5] == 0x90 && g_tNatEntry[6] == 0xC3, "race fix: the entry jumps to the replacement");
        PVOID gv = AddVectoredExceptionHandler(1, TGuardVeh);
        static uint8_t table[0xC83 * 8];
        TNatArm(0xC83, table);
        TNatCall(0xC83);
        bool zero = true; for (size_t i = 0; i < sizeof(table); i++) if (table[i]) { zero = false; break; }
        CHECK(g_tAllocCalls == 1 && g_tAllocArgs[0] == 0xC83 * 8 && g_tAllocArgs[1] == 0x10 && g_tAllocArgs[2] == 0,
              "race fix: one allocation through the TLS allocator (%u bytes, align %u, flags %u)", g_tAllocArgs[0], g_tAllocArgs[1], g_tAllocArgs[2]);
        CHECK(g_tGuardPtr == 1 && g_tGuardSize == 1, "race fix: the pointer and the size each stored once (%ld, %ld)", g_tGuardPtr, g_tGuardSize);
        CHECK(*TNatPtr() == (uint32_t)(uintptr_t)table && *TNatSize() == 0xC83 && g_tNatCount == 0 && zero, "race fix: pointer, size, count 0, the whole table zeroed");
        CHECK(g_tAtPtrZero == 1 && g_tAtPtrCount == 0 && g_tAtPtrSizeHits == 0,
              "race fix: when the pointer is published the table is zeroed, the count is 0 and the size not yet stored");
        CHECK(g_tAtSizePtrHits == 1 && g_tAtSizeAllocs == 1, "race fix: the size is stored last, after the allocation and the pointer");
        CHECK(g_tCallRet == 0xC83 && g_tAfter[0] == 0xC83 && g_tAfter[1] == 0x11111111 && g_tAfter[2] == 0x22222222 && g_tAfter[3] == 0x33333333,
              "race fix: returns the capacity as the game's did; esi, ebx, edi and ebp kept");
        TNatArm(0xC83, NULL);
        TNatCall(0xC83);
        LONG sizeStores = g_tGuardSize, ptrStores = g_tGuardPtr;
        DWORD po; VirtualProtect(g_tNatPages, 0x2000, PAGE_READWRITE, &po);    // the size page is still guarded: nothing stored there
        CHECK(sizeStores == 0 && ptrStores == 1 && *TNatPtr() == 0 && *TNatSize() == 0 && g_tNatCount == 0, "race fix: a failed allocation stores no size and does not crash");
        RemoveVectoredExceptionHandler(gv);
        g_cs.nativePtr = pt; g_cs.nativeSize = sz;
    }
    // ---- the pools, the arena and VehicleStruct: raised before the game builds them, never lowered
    {
        CoreSites c = {};
        SitesShape1080(c);
        static const uint8_t slot0[5] = { 0x68, 0x10, 0x27, 0x00, 0x00 }, link0[5] = { 0x68, 0x20, 0x4E, 0x00, 0x00 }, arena0[5] = { 0xBF, 0x00, 0x80, 0x02, 0x00 };
        memcpy(g_tV1Slot, slot0, 5); memcpy(g_tV1Link, link0, 5); memcpy(g_tV1Arena, arena0, 5);
        c.slotSize = (uintptr_t)g_tV1Slot; c.slotPool = (uintptr_t)g_tV1SlotPool; c.linkSize = (uintptr_t)g_tV1Link; c.linkPool = (uintptr_t)g_tV1LinkPool;
        c.arenaSize = (uintptr_t)g_tV1Arena; c.arenaObj = (uintptr_t)g_tV1ArenaObj; c.initGuard = (uintptr_t)&g_tV1Guard;
        auto imm = [](const uint8_t* b) { uint32_t v; memcpy(&v, b + 1, 4); return v; };
        CHECK(RaisePools(c, 32768, 65536) && imm(g_tV1Slot) == 32768 && imm(g_tV1Link) == 65536 && g_tV1Slot[0] == 0x68 && g_tV1Link[0] == 0x68,
              "pools: slots 10,000 -> 32,768 and links 20,000 (FusionFix's) -> 65,536: %s / %s", g_site[S_SLOT_SIZE].note, g_site[S_LINK_SIZE].note);
        CHECK(g_raised[S_SLOT_SIZE] == 32768 && g_raised[S_LINK_SIZE] == 65536, "pools: what each raise left is recorded for the read-back at sizing");
        uint32_t big = 100000; memcpy(g_tV1Link + 1, &big, 4);
        CHECK(RaisePools(c, 32768, 65536) && imm(g_tV1Link) == 100000, "pools: a larger size already there stands: %s", g_site[S_LINK_SIZE].note);
        memcpy(g_tV1Slot, slot0, 5); g_tV1SlotPool[8] = 0x1000;     // +0x20: the free list exists: the pool is built
        CHECK(!RaisePools(c, 32768, 65536) && imm(g_tV1Slot) == 10000 && g_site[S_SLOT_SIZE].state == ST_OFF_ERROR, "pools: too late once the slot pool is built");
        g_tV1SlotPool[8] = 0; memcpy(g_tV1Link, link0, 5); g_tV1LinkPool[20] = 0x1000;   // +0x50: the link pool's node array exists
        CHECK(!RaisePools(c, 32768, 65536) && imm(g_tV1Link) == 20000 && g_site[S_LINK_SIZE].state == ST_OFF_ERROR, "pools: too late once the link pool is built");
        g_tV1LinkPool[20] = 0; g_tV1Link[0] = 0x6A;
        CHECK(!RaisePools(c, 32768, 65536) && g_site[S_LINK_SIZE].state == ST_OFF_MISMATCH && g_tV1Link[0] == 0x6A, "pools: not a push imm32: refused");
        memcpy(g_tV1Link, link0, 5);
        CHECK(RaiseArena(c, 400) && imm(g_tV1Arena) == 400u * 1024u && g_tV1Arena[0] == 0xBF, "arena: 160 -> 400 MiB: %s", g_site[S_ARENA_SIZE].note);
        memcpy(g_tV1Arena, arena0, 5); g_tV1Guard = 4;
        CHECK(!RaiseArena(c, 400) && imm(g_tV1Arena) == 0x28000, "arena: left alone once the game built it");
        g_tV1Guard = 0;
        uint32_t ns = (uint32_t)(uintptr_t)g_tV1VsStr; g_tV1VsName[0] = 0x68; memcpy(g_tV1VsName + 1, &ns, 4);
        static const uint8_t vs0[4] = { 0x6A, 0x32, 0x8B, 0xC8 }; memcpy(g_tV1VsSize, vs0, 4); g_tV1VsPool = 0;
        c.vstructName = (uintptr_t)g_tV1VsName; c.vstructSize = (uintptr_t)g_tV1VsSize; c.vstructPool = (uintptr_t)&g_tV1VsPool; c.vstructNameStr = (uintptr_t)g_tV1VsStr;
        CHECK(RaiseVehicleStruct(c, 100) && g_tV1VsSize[1] == 100 && g_tV1VsSize[0] == 0x6A && g_tV1VsSize[2] == 0x8B, "VehicleStruct: 50 -> 100");
        g_tV1VsSize[1] = 0x7F;
        CHECK(RaiseVehicleStruct(c, 100) && g_tV1VsSize[1] == 0x7F, "VehicleStruct: a larger value stands");
        g_tV1VsSize[1] = 0x32; g_tV1VsPool = 0x1000;
        CHECK(!RaiseVehicleStruct(c, 100) && g_tV1VsSize[1] == 0x32, "VehicleStruct: left alone once the pool exists");
        g_tV1VsPool = 0; g_tV1VsName[1] ^= 1;
        CHECK(!RaiseVehicleStruct(c, 100) && g_tV1VsSize[1] == 0x32 && g_site[S_VSTRUCT].state == ST_OFF_MISMATCH, "VehicleStruct: refused after another pool's name");
        g_tV1VsName[1] ^= 1;
        // The main heap: 100 MiB raised to 128 before the game builds it, all four sizes or none (H1, FINDINGS 8bc).
        {
            CoreSites h = {};
            auto reset = [&](CoreSites& k, bool ce) {
                if (ce) SitesShapeCe(k); else SitesShape1080(k);
                memset(g_tHpObj, 0, sizeof(g_tHpObj)); g_tHpGuard = 0;
                uint32_t sz = 100u << 20, field = (uint32_t)(uintptr_t)(g_tHpObj + 20);   // +0x50
                g_tHpS1[0] = 0x68; memcpy(g_tHpS1 + 1, &sz, 4);
                g_tHpS2[0] = 0xC7; g_tHpS2[1] = 0x05; memcpy(g_tHpS2 + 2, &field, 4); memcpy(g_tHpS2 + 6, &sz, 4);
                g_tHpS3[0] = 0x68; memcpy(g_tHpS3 + 1, &sz, 4);
                g_tHpS4[0] = ce ? 0x68 : 0xB9; memcpy(g_tHpS4 + 1, &sz, 4);
                k.heapBuild = (uintptr_t)0x00454740u;                     // any address: the call must reach it
                int32_t rel = (int32_t)(k.heapBuild - ((uintptr_t)g_tHpCall + 5)); g_tHpCall[0] = 0xE8; memcpy(g_tHpCall + 1, &rel, 4);
                k.heapCall = (uintptr_t)g_tHpCall; k.heapObj = (uintptr_t)g_tHpObj; k.initGuard = (uintptr_t)&g_tHpGuard;
                k.heapSize[0] = (uintptr_t)g_tHpS1; k.heapSize[1] = (uintptr_t)g_tHpS2; k.heapSize[2] = (uintptr_t)g_tHpS3; k.heapSize[3] = (uintptr_t)g_tHpS4;
            };
            auto at = [](const uint8_t* b, int o) { uint32_t v; memcpy(&v, b + o, 4); return v; };
            auto all = [&](uint32_t v) { return at(g_tHpS1, 1) == v && at(g_tHpS2, 6) == v && at(g_tHpS3, 1) == v && at(g_tHpS4, 1) == v; };
            const uint32_t M100 = 100u << 20, M128 = 128u << 20;
            reset(h, false);
            CHECK(RaiseGameHeap(h, 128) && all(M128) && g_tHpS4[0] == 0xB9 && g_tHpS2[0] == 0xC7 && g_raised[S_GAME_HEAP] == M128 && !strcmp(g_site[S_GAME_HEAP].note, "100 -> 128 MiB"),
                  "main heap: 100 -> 128 MiB at all four sizes: %s", g_site[S_GAME_HEAP].note);
            reset(h, false); g_tHpGuard = 1;
            CHECK(!RaiseGameHeap(h, 128) && all(M100) && g_site[S_GAME_HEAP].state == ST_OFF_ERROR, "main heap: left alone once the game set its guard");
            reset(h, false); g_tHpObj[0] = 0xEC07A4;
            CHECK(!RaiseGameHeap(h, 128) && all(M100) && g_site[S_GAME_HEAP].state == ST_OFF_ERROR, "main heap: left alone once it is built");
            reset(h, false); h.heapBuild += 0x10;
            CHECK(!RaiseGameHeap(h, 128) && all(M100) && g_site[S_GAME_HEAP].state == ST_OFF_MISMATCH && strstr(g_site[S_GAME_HEAP].note, "another mod builds it"),
                  "main heap: another mod's builder (ZolikaPatch's NewMemorySystem): nothing written: %s", g_site[S_GAME_HEAP].note);
            reset(h, false); g_tHpS3[1] ^= 1;
            CHECK(!RaiseGameHeap(h, 128) && at(g_tHpS1, 1) == M100 && at(g_tHpS2, 6) == M100 && at(g_tHpS4, 1) == M100 && g_site[S_GAME_HEAP].state == ST_OFF_MISMATCH,
                  "main heap: one size that disagrees: none of the four written: %s", g_site[S_GAME_HEAP].note);
            reset(h, false); g_tHpS2[2] ^= 4;
            CHECK(!RaiseGameHeap(h, 128) && all(M100) && g_site[S_GAME_HEAP].state == ST_OFF_MISMATCH, "main heap: a size field store at another address: refused");
            reset(h, true);
            CHECK(RaiseGameHeap(h, 128) && all(M128) && g_tHpS4[0] == 0x68, "main heap: the Complete Edition's shape, a push for the init's size");
            reset(h, true); g_tHpS4[0] = 0xB9;
            CHECK(!RaiseGameHeap(h, 128) && all(M100), "main heap: 1.0.8.0's mov ecx where the Complete Edition pushes: refused");
            reset(h, false);
            { uint32_t big = 160u << 20; memcpy(g_tHpS1 + 1, &big, 4); memcpy(g_tHpS2 + 6, &big, 4); memcpy(g_tHpS3 + 1, &big, 4); memcpy(g_tHpS4 + 1, &big, 4); }
            CHECK(RaiseGameHeap(h, 128) && all(160u << 20) && g_raised[S_GAME_HEAP] == (160u << 20), "main heap: a larger size another mod wrote stands: %s", g_site[S_GAME_HEAP].note);
            // Read back at sizing: the heap the game built must be at least what SISCO left.
            reset(h, false); RaiseGameHeap(h, 128);
            const int others[4] = { S_SLOT_SIZE, S_LINK_SIZE, S_ARENA_SIZE, S_VSTRUCT };
            for (int i = 0; i < 4; i++) SiteSet(others[i], ST_OFF_DISABLED, "");
            g_tHpObj[0] = 0xEC07A4; g_tHpObj[20] = M128;
            ReadBackRaises(h);
            CHECK(g_site[S_GAME_HEAP].state == ST_ON && strstr(g_builtNote, "main heap 128 MB"), "main heap: read back at 128 MB at sizing: %s", g_builtNote);
            g_tHpObj[20] = M100;
            ReadBackRaises(h);
            CHECK(g_site[S_GAME_HEAP].state == ST_OFF_MISMATCH && strstr(g_site[S_GAME_HEAP].note, "built with 100 MiB"), "main heap: built at 100 after all: marked: %s", g_site[S_GAME_HEAP].note);
            // SMPA20: the call goes through SISCO, which names the heap and passes the game's own call on unchanged.
            int32_t frel = (int32_t)((uintptr_t)TestFatal - ((uintptr_t)g_tSmpaCall + 5)); g_tSmpaCall[0] = 0xE8; memcpy(g_tSmpaCall + 1, &frel, 4);
            g_tHpAudioAt[1] = (uint32_t)(uintptr_t)g_tHpAudio;   // the audio object sits just before its buffer pointer
            h.smpaCall = (uintptr_t)g_tSmpaCall; h.fatal = (uintptr_t)TestFatal;
            h.audioHeapPtr = (uintptr_t)&g_tHpAudioAt[1] + 4;
            CHECK(InstallSmpaNote(h) && g_site[S_SMPA20].state == ST_ON, "smpa20: the call is taken");
            int32_t nrel; memcpy(&nrel, g_tSmpaCall + 1, 4);
            CHECK((uintptr_t)g_tSmpaCall + 5 + nrel == (uintptr_t)ThunkSmpa1080, "smpa20: 1.0.8.0's call reaches the 1.0.8.0 thunk");
            g_tHpObj[20] = M128; g_tHpObj[38] = 12u << 10;   // +0x98: 12 KB free
            g_tHpAudio[20] = 152u << 20; g_tHpAudio[38] = 3u << 10;
            // As the game calls it: the heap in edi (esi on the CE), what is free, the size asked and the code pushed.
            uint32_t hp = (uint32_t)(uintptr_t)g_tHpObj, au = (uint32_t)(uintptr_t)g_tHpAudio, code = (uint32_t)(uintptr_t)g_tSmpaCode, keepEdi = 0x5EED, keepEsi = 0xE51, outEdi = 0, outEsi = 0;
            void* t1 = (void*)ThunkSmpa1080; void* t2 = (void*)ThunkSmpaCe;
            g_tFatalN = 0;
            __asm {
                push edi
                push esi
                mov edi, hp
                mov esi, keepEsi
                push 0x3000
                push 48
                push code
                call t1
                add esp, 12
                mov outEdi, edi
                mov outEsi, esi
                pop esi
                pop edi
            }
            CHECK(g_tFatalN == 1 && g_tFatalCode == g_tSmpaCode && g_tFatalAsked == 48 && g_tFatalFree == 0x3000 && outEdi == hp && outEsi == keepEsi,
                  "smpa20: 1.0.8.0: the game's own call goes on with its arguments and registers (%u, %u, %u)", g_tFatalN, g_tFatalAsked, g_tFatalFree);
            __asm {
                push edi
                push esi
                mov esi, au
                mov edi, keepEdi
                push 0x0C00
                push 16
                push code
                call t2
                add esp, 12
                mov outEdi, edi
                mov outEsi, esi
                pop esi
                pop edi
            }
            CHECK(g_tFatalN == 2 && g_tFatalAsked == 16 && g_tFatalFree == 0x0C00 && outEsi == au && outEdi == keepEdi,
                  "smpa20: the Complete Edition: the heap read from esi, the call goes on unchanged (%u, %u)", g_tFatalN, g_tFatalAsked);
            char st[200]; SmpaText(st, sizeof(st), hp, 48);
            CHECK(!strcmp(st, "SMPA20: the game's main heap (128 MB) ran out: 12 KB free, 48 bytes asked"), "smpa20: the main heap named: %s", st);
            SmpaText(st, sizeof(st), au, 16);
            CHECK(!strcmp(st, "SMPA20: the audio heap (152 MB) ran out: 3 KB free, 16 bytes asked; the main heap had 12 KB free of 128 MB"), "smpa20: the audio heap named: %s", st);
            g_tHpAudioAt[1] = 0;
            SmpaText(st, sizeof(st), au, 16);
            CHECK(strstr(st, "one of the game's smaller heaps (152 MB)") != NULL, "smpa20: a heap that is neither: %s", st);
            g_tSmpaCall[1] ^= 1;
            CHECK(!InstallSmpaNote(h) && g_site[S_SMPA20].state == ST_OFF_MISMATCH, "smpa20: a call that reaches somewhere else is left alone");
            h.build = BUILD_CE; g_tSmpaCall[1] ^= 1;
            memcpy(g_tSmpaCall + 1, &frel, 4);
            CHECK(InstallSmpaNote(h), "smpa20: the Complete Edition's call is taken");
            memcpy(&nrel, g_tSmpaCall + 1, 4);
            CHECK((uintptr_t)g_tSmpaCall + 5 + nrel == (uintptr_t)ThunkSmpaCe, "smpa20: the Complete Edition's call reaches its own thunk");
        }
        // SISCO.ini. What matters is not the values but the defaults: a player with no file, a missing key or a
        // typo must get exactly the behaviour of a player with no file at all, because that is everyone.
        {
            char tmp[MAX_PATH], sub[40]; GetTempPathA(MAX_PATH, tmp);      // one folder per process: harnesses run side by side
            _snprintf_s(sub, sizeof(sub), _TRUNCATE, "sis_core_test_%lu", GetCurrentProcessId());
            strcat_s(tmp, sub); CreateDirectoryA(tmp, NULL);
            char p[MAX_PATH]; _snprintf_s(p, MAX_PATH, _TRUNCATE, "%s\\SISCO.ini", tmp); DeleteFileA(p);
            SisSettings s = ReadSettings(p);
            CHECK(s.enabled && s.fixes && s.limits && s.budget, "settings: no file at all: everything on");
            FILE* f = NULL; fopen_s(&f, p, "wb"); if (f) { fputs("[SISCO]\r\nBudget=0\r\n", f); fclose(f); }
            s = ReadSettings(p);
            CHECK(s.enabled && s.fixes && s.limits && !s.budget, "settings: Budget=0 turns off only the budget");
            f = NULL; fopen_s(&f, p, "wb"); if (f) { fputs("[SISCO]\r\nEnabled=0\r\n", f); fclose(f); }
            s = ReadSettings(p);
            CHECK(!s.enabled && !s.fixes && !s.limits && !s.budget, "settings: Enabled=0 takes the other three with it");
            f = NULL; fopen_s(&f, p, "wb"); if (f) { fputs("[SISCO]\r\nBudget=banana\r\nLmits=0\r\n", f); fclose(f); }
            s = ReadSettings(p);
            CHECK(s.enabled && s.fixes && s.limits && s.budget, "settings: a bad value and a misspelt key both mean on");
            f = NULL; fopen_s(&f, p, "wb"); if (f) { fputs("[SISCO]\r\nFixes=off\r\nBudget=no\r\nLimits=false\r\n", f); fclose(f); }
            s = ReadSettings(p);
            CHECK(s.enabled && s.fixes && s.limits && s.budget,
                  "settings: off, no and false are not 0, so they leave everything on");
            f = NULL; fopen_s(&f, p, "wb"); if (f) { fputs("[SISCO]\r\nBudget = 0 \r\n", f); fclose(f); }
            s = ReadSettings(p);
            CHECK(s.enabled && s.fixes && s.limits && !s.budget, "settings: spaces around a 0 still turn it off");
            f = NULL; fopen_s(&f, p, "wb"); if (f) { fputs("[SISCO]\r\nBudget=0 ; while testing\r\n", f); fclose(f); }
            s = ReadSettings(p);
            CHECK(s.enabled && s.fixes && s.limits && !s.budget, "settings: a comment after the 0 still turns it off");
            f = NULL; fopen_s(&f, p, "wb"); if (f) { fputs("nonsense, not an ini at all\r\n", f); fclose(f); }
            s = ReadSettings(p);
            CHECK(s.enabled && s.fixes && s.limits && s.budget, "settings: a file that is not an ini means on");
            f = NULL; fopen_s(&f, p, "wb"); if (f) { fputs("[SISCO]\r\nFixes=0\r\n", f); fclose(f); }
            s = ReadSettings(p);
            CHECK(s.enabled && !s.fixes && s.limits && s.budget, "settings: Fixes=0 alone turns off only the fixes");
            f = NULL; fopen_s(&f, p, "wb"); if (f) { fputs("[SISCO]\r\nLimits=0\r\n", f); fclose(f); }
            s = ReadSettings(p);
            CHECK(s.enabled && s.fixes && !s.limits && s.budget, "settings: Limits=0 alone turns off only the limits");
            DeleteFileA(p); RemoveDirectoryA(tmp);
        }

        // The arena asks nothing now and is raised on every renderer, so there is no prediction left to test.
        // That SisInstall still ATTEMPTS it is proved by the empty-sites case at the end of this file: a
        // reintroduced condition would leave the site NOTTRIED or OFF_DISABLED there instead of OFF_ERROR.
    }
    // ---- Direct3DCreate9: the first call detects DXVK and sets the launch options only where the player set none
    {
        g_tD3DCode = (uint8_t*)VirtualAlloc(NULL, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        memset(g_tD3DCode, 0xCC, 0x40);
        g_tD3DCode[0] = 0xE8; int32_t rel = 0x10 - 5; memcpy(g_tD3DCode + 1, &rel, 4);
        g_tD3DCode[0x10] = 0xFF; g_tD3DCode[0x11] = 0x25; uint32_t slot = (uint32_t)(uintptr_t)g_tIatSlot; memcpy(g_tD3DCode + 0x12, &slot, 4);
        g_tIatSlot[0] = (uint32_t)(uintptr_t)MockDirect3DCreate9;
        CoreSites c = {};
        SitesShape1080(c);
        c.d3dCall = (uintptr_t)g_tD3DCode; c.d3dThunk = (uintptr_t)g_tD3DCode + 0x10; c.d3dIat = (uintptr_t)g_tIatSlot;
        c.prmUnmanaged = (uintptr_t)&g_tPrm[0]; c.prmManaged = (uintptr_t)&g_tPrm[1]; c.prmVidmem = (uintptr_t)&g_tPrm[2];
        uint32_t a0 = (uint32_t)(uintptr_t)&g_tPrm[0], a1 = (uint32_t)(uintptr_t)&g_tPrm[1], a2 = (uint32_t)(uintptr_t)&g_tPrm[2];
        g_tRd[0][0] = 0x39; g_tRd[0][1] = 0x35; memcpy(&g_tRd[0][2], &a0, 4);
        g_tRd[1][0] = 0x39; g_tRd[1][1] = 0x35; memcpy(&g_tRd[1][2], &a1, 4);
        g_tRd[2][0] = 0xA1; memcpy(&g_tRd[2][1], &a2, 4);
        c.rdUnmanaged = (uintptr_t)g_tRd[0]; c.rdManaged = (uintptr_t)g_tRd[1]; c.rdVidmem = (uintptr_t)g_tRd[2];
        CHECK(ParamReadersMatch(c), "Direct3D: the three launch-option readers read their slots");
        g_tRd[1][2] ^= 4;
        CHECK(!ParamReadersMatch(c), "Direct3D: a reader of another slot is refused");
        g_tRd[1][2] ^= 4;
        g_tD3DCode[0x12] ^= 4;
        CHECK(!InstallD3DHook(c) && g_site[S_D3D_IAT].state == ST_OFF_MISMATCH && g_tIatSlot[0] == (uint32_t)(uintptr_t)MockDirect3DCreate9,
              "Direct3D: refused when the thunk reads another slot, the slot untouched: %s", g_site[S_D3D_IAT].note);
        g_tD3DCode[0x12] ^= 4;
        CHECK(InstallD3DHook(c) && g_tIatSlot[0] == (uint32_t)(uintptr_t)HkDirect3DCreate9 && g_oD3DCreate9 == MockDirect3DCreate9,
              "Direct3D: hooked in its import slot: %s", g_site[S_D3D_IAT].note);
        CoreSites keep = g_cs;
        g_cs.prmUnmanaged = c.prmUnmanaged; g_cs.prmManaged = c.prmManaged; g_cs.prmVidmem = c.prmVidmem;
        D3DCreate9_t create = (D3DCreate9_t)(uintptr_t)g_tIatSlot[0];
        // The DXVK module the version is read from is, in this harness, the test binary itself, whose .rdata holds
        // every marker spelt out below. So an image is planted for the whole block: DXVK 3.1.1 until (6) says otherwise.
        static uint8_t tImg[96];
        g_tDxvkImage = tImg; g_tDxvkImageN = sizeof(tImg);
        memset(tImg, 0xAB, sizeof(tImg)); tImg[19] = 0; memcpy(tImg + 20, "v3.1.1", 7);
        // (1) DXVK, no option from the player: -managed, and -availablevidmem = the card's budget in MiB
        g_d3dCalls = 0; g_dxvk = -1; g_bootBudgetMb = -1; g_dxgiState = 0; g_tPrm[0] = g_tPrm[1] = g_tPrm[2] = 0; g_tD3D.dxvk = 1; g_tD3D.refs = 1;
        void* r = create(32);
        char want[16]; _snprintf_s(want, sizeof(want), _TRUNCATE, "%lld", g_bootBudgetMb > VIDMEM_MAX_MB ? (long long)VIDMEM_MAX_MB : g_bootBudgetMb);
        CHECK(r == &g_tD3D && g_tD3DSdk == 32 && g_dxvk == 1 && g_tD3D.refs == 1 && !strncmp(g_d3dNote, "DXVK", 4) && !strstr(g_d3dNote, "assumed"),
              "Direct3D: DXVK detected through its interop interface, the reference given back (%ld): %s", g_tD3D.refs, g_d3dNote);
        CHECK(g_tPrm[1] == (uint32_t)(uintptr_t)g_flagArg && g_tPrm[0] == 0, "Direct3D: -managed set, -unmanaged left alone");
        CHECK(g_dxgiState != 1 || (g_gpuName[0] && strstr(g_d3dNote, g_gpuName) && !strstr(g_d3dNote, "LUID")),
              "Direct3D: the card DXGI picked is named on the direct3d line (%s): %s", g_gpuName, g_d3dNote);
        CHECK(g_bootBudgetMb >= 512 && g_tPrm[2] == (uint32_t)(uintptr_t)g_vidmemArg && strcmp(g_vidmemArg, want) == 0 && g_vidmemSetMb == atoi(want),
              "Direct3D: -availablevidmem = the card's budget, at most 8192: %s MiB (remembered as %d)", g_vidmemArg, g_vidmemSetMb);
        // (2) a second call passes straight through
        g_tPrm[1] = 0; r = create(31);
        CHECK(r == &g_tD3D && g_tD3DSdk == 31 && g_d3dCalls == 2 && g_tPrm[1] == 0, "Direct3D: later calls pass straight through");
        // (3) the player's options stand
        g_d3dCalls = 0; g_dxvk = -1; g_tPrm[0] = 0x1234; g_tPrm[1] = 0; g_tPrm[2] = 0x5678;
        create(32);
        CHECK(g_dxvk == 1 && g_tPrm[0] == 0x1234 && g_tPrm[1] == 0 && g_tPrm[2] == 0x5678 && g_vidmemSetMb == 0, "Direct3D: the player's -unmanaged and -availablevidmem stand");
        g_d3dCalls = 0; g_dxvk = -1; g_tPrm[0] = 0; g_tPrm[1] = 0x4321; g_tPrm[2] = 0;
        create(32);
        CHECK(g_tPrm[1] == 0x4321 && g_tPrm[2] == (uint32_t)(uintptr_t)g_vidmemArg, "Direct3D: the player's -managed stands");
        // (4) no interop interface: native Direct3D 9, so nothing is set, unless vulkan-1.dll happens to be loaded
        // in this process, in which case DXVK is assumed
        g_d3dCalls = 0; g_dxvk = -1; g_tPrm[0] = g_tPrm[1] = g_tPrm[2] = 0; g_tD3D.dxvk = 0;
        create(32);
        if (!VulkanLoaded())
            CHECK(g_dxvk == 0 && !g_tPrm[0] && !g_tPrm[1] && !g_tPrm[2] && strstr(g_d3dNote, "native Direct3D 9"), "Direct3D: native: no option set: %s", g_d3dNote);
        else
            CHECK(g_dxvk == 1 && g_tPrm[1] && strstr(g_d3dNote, "vulkan-1.dll is loaded"), "Direct3D: no interop interface but vulkan-1.dll loaded: DXVK assumed: %s", g_d3dNote);
        g_tD3D.dxvk = 1;
        // (5) DXVK's version is the standalone v<major>.<minor>[.<patch>] marker in its module, and nothing else.
        {
            int v[3]; uint8_t img[64];
            auto plant = [&](const char* s) { memset(img, 0xAB, sizeof(img)); img[9] = 0; memcpy(img + 10, s, strlen(s) + 1); };
            plant("v2.6.2");
            CHECK(DxvkVersionIn(img, sizeof(img), v) && v[0] == 2 && v[1] == 6 && v[2] == 2, "DXVK version: v2.6.2 read as 2.6.2");
            plant("v3.1");
            CHECK(DxvkVersionIn(img, sizeof(img), v) && v[0] == 3 && v[1] == 1 && v[2] == 0, "DXVK version: v3.1 read as 3.1.0");
            plant("v2.6.2-14-gabcdef0");
            CHECK(DxvkVersionIn(img, sizeof(img), v) && v[0] == 2 && v[1] == 6 && v[2] == 2, "DXVK version: git describe's -14-g... suffix is read past: 2.6.2");
            plant("v2.6.2+");
            CHECK(DxvkVersionIn(img, sizeof(img), v) && v[0] == 2 && v[1] == 6 && v[2] == 2, "DXVK version: the dirty marker + is read past: 2.6.2");
            plant("v3.1a");    bool a = DxvkVersionIn(img, sizeof(img), v);
            plant("v31");      bool b = DxvkVersionIn(img, sizeof(img), v);
            plant("v3.1.1.1"); bool d = DxvkVersionIn(img, sizeof(img), v);
            memset(img, 0xAB, sizeof(img)); memcpy(img + 10, "v3.1.1", 7);   // no NUL before it: the tail of a longer string
            bool e = DxvkVersionIn(img, sizeof(img), v);
            memset(img, 0, sizeof(img)); bool f = DxvkVersionIn(img, sizeof(img), v);
            CHECK(!a && !b && !d && !e && !f, "DXVK version: v3.1a, v31, four parts, the tail of a longer string and an empty image are all no version");
        }
        // (6) The version is logged, and gates nothing: 2.6.2 gets -managed like 3.1.1. No marker: logged as such.
        memset(tImg, 0xAB, sizeof(tImg)); tImg[19] = 0; memcpy(tImg + 20, "v2.6.2", 7);
        g_d3dCalls = 0; g_dxvk = -1; g_tPrm[0] = g_tPrm[1] = g_tPrm[2] = 0;
        create(32);
        CHECK(g_dxvk == 1 && g_dxvkVer[0] == 2 && g_dxvkVer[1] == 6 && g_dxvkVer[2] == 2 && g_tPrm[1] == (uint32_t)(uintptr_t)g_flagArg && g_tPrm[2] != 0
              && strstr(g_d3dNote, "DXVK 2.6.2") && strstr(g_d3dNote, "-managed set"),
              "Direct3D: DXVK 2.6.2: -managed set, the version in the line: %s", g_d3dNote);
        memset(tImg, 0xAB, sizeof(tImg)); tImg[19] = 0; memcpy(tImg + 20, "v3.1.1", 7);
        g_d3dCalls = 0; g_dxvk = -1; g_tPrm[0] = g_tPrm[1] = g_tPrm[2] = 0;
        create(32);
        CHECK(g_dxvkVer[0] == 3 && g_dxvkVer[2] == 1 && g_tPrm[1] == (uint32_t)(uintptr_t)g_flagArg && strstr(g_d3dNote, "DXVK 3.1.1") && !strstr(g_d3dNote, "read from") && strstr(g_d3dNote, ", card budget "),
              "Direct3D: DXVK 3.1.1: -managed set, the version in the line: %s", g_d3dNote);
        memset(tImg, 0xAB, sizeof(tImg));
        g_d3dCalls = 0; g_dxvk = -1; g_tPrm[0] = g_tPrm[1] = g_tPrm[2] = 0;
        create(32);
        CHECK(g_dxvkVer[0] == -1 && g_tPrm[1] == (uint32_t)(uintptr_t)g_flagArg && strstr(g_d3dNote, "version not found"),
              "Direct3D: no version marker: logged, and -managed set: %s", g_d3dNote);
        // Wine (Proton, the Steam Deck): -managed is set as on Windows, the line names Wine, and a 2.x loaded as
        // d3d9.dll is named as Proton's choice rather than as the download's.
        g_tWine = 1; g_wineNote[0] = 0;
        memset(tImg, 0xAB, sizeof(tImg)); tImg[19] = 0; memcpy(tImg + 20, "v2.7.1", 7);
        g_d3dCalls = 0; g_dxvk = -1; g_tPrm[0] = g_tPrm[1] = g_tPrm[2] = 0;
        create(32);
        CHECK(g_tPrm[1] == (uint32_t)(uintptr_t)g_flagArg && g_managedOn && strstr(g_d3dNote, "Wine (planted)")
              && strstr(g_d3dNote, "Proton's choice"),
              "Direct3D: on Wine -managed is set as usual, and a 2.x is named as Proton's choice, not the download's: %s", g_d3dNote);
        g_tWine = 0; g_wineNote[0] = 0;
        memset(tImg, 0xAB, sizeof(tImg)); tImg[19] = 0; memcpy(tImg + 20, "v3.1.1", 7);
        // (7) Presents not made while a load is in progress. With -managed in effect the first Direct3DCreate9 hooks
        // CreateDevice on the object; the device it returns has its swap chain's Present hooked, and released as many
        // times as taken; nothing is skipped until the worker raises the flag (its side is under "load" below), and a
        // present that goes out goes out with the game's own flags, untouched. The fake vtables sit in a read-only
        // page for this, as DXVK's do, so the write has to lift the protection and put it back.
        g_tD3DVtbl[16] = (void*)FakeCreateDevice; g_tSwapVtbl[3] = (void*)FakeSwapPresent;   // the writable fakes, as built: (6) hooked the first
        void** roD3D = (void**)VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        void** roDev = roD3D + 32; void** roSwap = roD3D + 64;
        memcpy(roD3D, g_tD3DVtbl, sizeof(g_tD3DVtbl)); memcpy(roDev, g_tDevVtbl, sizeof(g_tDevVtbl)); memcpy(roSwap, g_tSwapVtbl, sizeof(g_tSwapVtbl));
        DWORD roOld = 0; VirtualProtect(roD3D, 4096, PAGE_READONLY, &roOld);
        g_tD3D.vtbl = roD3D; g_tDev.vtbl = roDev; g_tSwap.vtbl = roSwap;
        g_d3dVtblHooked = NULL; g_swapVtblHooked = NULL; g_oCreateDevice = NULL; g_oSwapPresent = NULL; g_presentSkip = 0; g_skippedPresents = 0; g_tSwapRefs = 0;
        g_d3dCalls = 0; g_dxvk = -1; g_tPrm[0] = g_tPrm[1] = g_tPrm[2] = 0;
        create(32);
        CHECK(g_tPrm[1] == (uint32_t)(uintptr_t)g_flagArg && g_d3dVtblHooked == roD3D && roD3D[16] == (void*)HkCreateDevice,
              "present: with -managed set, CreateDevice is hooked on the object Direct3DCreate9 returned, through the page's protection");
        MEMORY_BASIC_INFORMATION roMbi = {}; VirtualQuery(roD3D, &roMbi, sizeof(roMbi));
        CHECK(roMbi.Protect == PAGE_READONLY, "present: the page is read-only again after the write (%08X)", (unsigned)roMbi.Protect);
        void* dev = NULL;
        ((CreateDevice_t)roD3D[16])(&g_tD3D, 0, 1, NULL, 0, NULL, &dev);
        CHECK(dev == &g_tDev && g_tDevCreates == 1 && g_swapVtblHooked == roSwap && roSwap[3] == (void*)HkSwapPresent && g_presentSkip == 0 && g_tSwapRefs == 0,
              "present: the device came from the original, its swap chain's Present is hooked and released once, and nothing is skipped yet");
        g_tSwapFlags = 0xFFFFFFFFu;
        ((SwapPresent_t)roSwap[3])(&g_tSwap, NULL, NULL, NULL, NULL, 1);                  // D3DPRESENT_DONOTWAIT, as a game may send it
        CHECK(g_tSwapFlags == 1 && g_skippedPresents == 0, "present: outside a build the present goes out exactly as the game sent it (%08X)", (unsigned)g_tSwapFlags);
        // Skipping: with the flag up, a present within PRESENT_SHOW_MS of the last one shown is not made at all (the
        // original never called, S_OK returned, counted); the next past that goes through, as the game sent it.
        g_presentSkip = 1; g_lastShownMs = 0; g_skippedPresents = 0; g_tPresentMs = 5000; g_showMs = PRESENT_SHOW_MS;
        LONG shown0 = g_tSwapPresents; g_tSwapFlags = 0xFFFFFFFFu;
        HRESULT hs = ((SwapPresent_t)roSwap[3])(&g_tSwap, NULL, NULL, NULL, NULL, 0);
        CHECK(hs == S_OK && g_tSwapPresents == shown0 + 1 && g_tSwapFlags == 0 && g_skippedPresents == 0 && g_lastShownMs == 5000,
              "present: skipping: the first present goes through, as the game sent it");
        g_tPresentMs = 5050; g_tSwapFlags = 0xFFFFFFFFu;
        hs = ((SwapPresent_t)roSwap[3])(&g_tSwap, NULL, NULL, NULL, NULL, 0);
        CHECK(hs == S_OK && g_tSwapPresents == shown0 + 1 && g_tSwapFlags == 0xFFFFFFFFu && g_skippedPresents == 1,
              "present: skipping: 50 ms later the present is not made and S_OK is returned");
        g_tPresentMs = 5100;
        hs = ((SwapPresent_t)roSwap[3])(&g_tSwap, NULL, NULL, NULL, NULL, 0);
        CHECK(hs == S_OK && g_tSwapPresents == shown0 + 2 && g_tSwapFlags == 0 && g_lastShownMs == 5100,
              "present: skipping: at 100 ms one goes through again");
        // On a loading screen the rate is 33 ms: 20 ms later not made, 33 ms later shown.
        g_showMs = PRESENT_SHOW_LOADING_MS; g_tPresentMs = 5133;
        ((SwapPresent_t)roSwap[3])(&g_tSwap, NULL, NULL, NULL, NULL, 0);
        LONG s133 = g_tSwapPresents;
        CHECK(s133 == shown0 + 3, "present: on a loading screen, 33 ms after the last shown the next goes through");
        g_tPresentMs = 5153; ((SwapPresent_t)roSwap[3])(&g_tSwap, NULL, NULL, NULL, NULL, 0);
        CHECK(g_tSwapPresents == s133, "present: 20 ms after that one the next is not made");
        g_tPresentMs = 5166; ((SwapPresent_t)roSwap[3])(&g_tSwap, NULL, NULL, NULL, NULL, 0);
        CHECK(g_tSwapPresents == s133 + 1, "present: and 33 ms after it the next goes through");
        // The world's render targets end the skip on the present that sees them, even within the 100 ms: that frame
        // and every one after goes out as the game sent it.
        uintptr_t keepRt0 = g_cs.rt0; static uint64_t rtNow; g_cs.rt0 = (uintptr_t)&rtNow;
        g_showMs = PRESENT_SHOW_MS; g_skipRtEnd = 126; rtNow = 63ull << 20; g_tPresentMs = 6000;
        ((SwapPresent_t)roSwap[3])(&g_tSwap, NULL, NULL, NULL, NULL, 0);
        LONG s6 = g_tSwapPresents; g_tPresentMs = 6020;
        ((SwapPresent_t)roSwap[3])(&g_tSwap, NULL, NULL, NULL, NULL, 0);
        CHECK(g_tSwapPresents == s6 && g_presentSkip == 1, "present: the loading screen's 63 MB set: still skipping");
        rtNow = 469ull << 20; g_tPresentMs = 6040;
        ((SwapPresent_t)roSwap[3])(&g_tSwap, NULL, NULL, NULL, NULL, 0);
        CHECK(g_tSwapPresents == s6 + 1 && g_presentSkip == 0 && g_skipEndedRt == 469,
              "present: the world's set (469 MB, over twice 63) ends the skip on that very present, 40 ms after the last shown");
        g_cs.rt0 = keepRt0; g_skipRtEnd = 0; g_skipEndedRt = 0;
        g_presentSkip = 0; g_skippedPresents = 0; g_tPresentMs = -1; g_showMs = PRESENT_SHOW_MS;
        // (8) A load without a loading screen: the render thread presents the same screen over and over while the game
        // draws a few frames a second (T18: 121 presents a second against 2 to 7 game frames). Presents 8 ms apart.
        static uint32_t tFrame = 1000; uintptr_t keepFrame = g_cs.frame; g_cs.frame = (uintptr_t)&tFrame;
        g_rpWinMs = 0; g_rpOn = false; g_rpLastMadeMs = 0; g_rpSkipped = 0; g_rpDone = 0; g_rpLogged = 0;
        auto present = [&](int64_t at) { g_tPresentMs = at; LONG b = g_tSwapPresents; ((SwapPresent_t)roSwap[3])(&g_tSwap, NULL, NULL, NULL, NULL, 0); return g_tSwapPresents > b; };
        int64_t pms = 100000; int made = 0, calls = 0;
        g_rpArmed = false;                                                    // before any world has loaded: splash screens, left alone
        for (int i = 0; i < 375; i++) { if (i % 30 == 0) tFrame++; made += present(pms); calls++; pms += 8; }
        CHECK(made == calls && !g_rpOn, "present: before the first world has loaded the repeat rule is not armed (%d of %d made)", made, calls);
        g_rpArmed = true; made = calls = 0;
        for (int i = 0; i < 1250; i++) { tFrame++; made += present(pms); calls++; pms += 8; }
        CHECK(made == calls && !g_rpOn, "present: play at 120 frames a second, one present per frame: all %d made (%d)", calls, made);
        made = calls = 0;
        for (int i = 0; i < 300; i++) { tFrame++; for (int k = 0; k < 4; k++) { made += present(pms); calls++; pms += 8; } }
        CHECK(made == calls && !g_rpOn, "present: four presents per game frame at 30 frames a second (frame generation): all %d made (%d)", calls, made);
        made = calls = 0;
        for (int i = 0; i < 300; i++) { tFrame++; for (int k = 0; k < 10; k++) { made += present(pms); calls++; pms += 3; } }
        CHECK(made == calls && !g_rpOn, "present: ten presents per game frame at 33 frames a second is not a load either: all made (%d of %d)", made, calls);
        made = calls = 0;
        for (int i = 0; i < 60; i++) { tFrame++; for (int k = 0; k < 4; k++) { made += present(pms); calls++; pms += 21; } }
        CHECK(made == calls && !g_rpOn, "present: frame generation on a slow machine, four presents per frame at 12 frames a second: all made (%d of %d)", made, calls);
        made = calls = 0;
        for (int i = 0; i < 60; i++) { tFrame++; made += present(pms); calls++; pms += 83; }
        CHECK(made == calls && !g_rpOn, "present: a slow machine at 12 frames a second, one present per frame: all made (%d of %d)", made, calls);
        pms += 400; made = calls = 0;                                         // a 400 ms hitch with no presents, then play again
        for (int i = 0; i < 125; i++) { tFrame++; made += present(pms); calls++; pms += 8; }
        CHECK(made == calls && !g_rpOn, "present: after a 400 ms hitch, play goes on unthrottled (%d of %d)", made, calls);
        // The taxi: 120 presents a second, 4 game frames a second, for 3 s. Throttled within two windows, and from then on
        // one present per PRESENT_SHOW_LOADING_MS at most.
        made = calls = 0; int64_t engagedAt = -1, lastMade = -1, minGap = 1000000; int madeAfter = 0;
        for (int i = 0; i < 375; i++) {
            if (i % 30 == 0) tFrame++;
            bool m = present(pms); made += m; calls++;
            if (g_rpOn && engagedAt < 0) engagedAt = pms;
            if (m && engagedAt >= 0 && pms > engagedAt) { madeAfter++; if (lastMade >= engagedAt && pms - lastMade < minGap) minGap = pms - lastMade; }
            if (m) lastMade = pms;
            pms += 8;
        }
        CHECK(g_rpOn && engagedAt >= 0 && engagedAt - (pms - 375 * 8) <= 2 * REPEAT_WINDOW_MS + 8 && minGap >= PRESENT_SHOW_LOADING_MS && madeAfter >= 60 && madeAfter <= 95,
              "present: the taxi's loop is throttled %lld ms in, then %d presents made in the rest of 3 s, never closer than %lld ms (%d of %d made)",
              engagedAt - (pms - 375 * 8), madeAfter, minGap, made, calls);
        CHECK(g_rpDone == 0, "present: no line while it lasts");
        // Back to one present per frame: released within two windows, and every present after that made.
        int madeLate = 0, callsLate = 0;
        for (int i = 0; i < 250; i++) { tFrame++; bool m = present(pms); pms += 8; if (i >= 64) { madeLate += m; callsLate++; } }
        CHECK(!g_rpOn && madeLate == callsLate && g_rpDone == 1 && g_rpDoneSkipped > 200 && g_rpDoneFrames >= 10 && g_rpDoneMs >= 2500,
              "present: the loop over, released and every present made again (%d of %d); the episode: %ld ms, %ld frames, %ld not made",
              madeLate, callsLate, (long)g_rpDoneMs, (long)g_rpDoneFrames, (long)g_rpDoneSkipped);
        // Frame generation after a load: four presents per game frame at 30 frames a second also ends it.
        for (int i = 0; i < 375; i++) { if (i % 30 == 0) tFrame++; present(pms); pms += 8; }
        CHECK(g_rpOn, "present: a second loop, throttled again");
        madeLate = callsLate = 0;
        for (int i = 0; i < 150; i++) { tFrame++; for (int k = 0; k < 4; k++) { bool m = present(pms); pms += 8; if (i >= 16) { madeLate += m; callsLate++; } } }
        CHECK(!g_rpOn && madeLate == callsLate && g_rpDone == 2, "present: and frame generation after it releases it too (%d of %d made)", madeLate, callsLate);
        RepeatLine();
        CHECK(g_rpLogged == 2, "present: one line for the episodes over");
        RepeatLine();
        CHECK(g_rpLogged == 2, "present: and only one");
        // The loading screen's own skip comes first: with it up the loop is not counted, and play after it is untouched.
        // A throttle already running when it comes up (the splash before the first loading screen, T19) ends there,
        // with no line: the loading screen logs itself.
        for (int i = 0; i < 125; i++) { present(pms); pms += 8; }            // a splash: presents, the count still
        CHECK(g_rpOn, "present: the splash before the first loading screen starts the throttle");
        LONG doneBefore = g_rpDone;
        g_presentSkip = 1; g_showMs = PRESENT_SHOW_LOADING_MS; g_lastShownMs = 0;
        for (int i = 0; i < 375; i++) { if (i % 30 == 0) tFrame++; present(pms); pms += 8; }
        CHECK(!g_rpOn && g_rpWinMs == 0 && g_rpDone == doneBefore, "present: on a loading screen the repeat rule stands aside, and the splash's throttle ends unlogged");
        g_presentSkip = 0; made = calls = 0;
        for (int i = 0; i < 125; i++) { tFrame++; made += present(pms); calls++; pms += 8; }
        CHECK(made == calls && !g_rpOn, "present: and play after it is untouched from its first present (%d of %d)", made, calls);
        tFrame -= 5000; made = calls = 0;                                    // the game sets its count back (a menu does, T16)
        for (int i = 0; i < 125; i++) { tFrame++; made += present(pms); calls++; pms += 8; }
        CHECK(made == calls && !g_rpOn, "present: a frame count set back is not a load (%d of %d)", made, calls);
        g_cs.frame = 0; made = calls = 0;
        for (int i = 0; i < 375; i++) { made += present(pms); calls++; pms += 8; }
        CHECK(made == calls && !g_rpOn, "present: with no frame count the loop is left alone (%d of %d)", made, calls);
        // A menu on screen gets every frame: the main menu under the loading screen's rule (M1: 505 asked, 30 made a
        // second), and a menu over the throttle; when it closes the rules come back.
        static uint8_t tMenu; uintptr_t keepMenu = g_cs.menu; g_cs.menu = (uintptr_t)&tMenu;
        g_cs.frame = (uintptr_t)&tFrame; tMenu = 1;
        g_presentSkip = 1; g_showMs = PRESENT_SHOW_LOADING_MS; g_lastShownMs = 0; made = calls = 0;
        for (int i = 0; i < 500; i++) { tFrame++; made += present(pms); calls++; pms += 2; }
        CHECK(made == calls, "present: the main menu under the loading screen's rule: every frame made (%d of %d)", made, calls);
        tMenu = 0; made = calls = 0;
        for (int i = 0; i < 500; i++) { made += present(pms); calls++; pms += 2; }
        CHECK(made <= 35 && made >= 25, "present: the menu closed, the load after it is skipped again (%d of %d made in 1 s)", made, calls);
        g_presentSkip = 0; g_rpWinMs = 0; g_rpOn = false;
        for (int i = 0; i < 375; i++) { if (i % 30 == 0) tFrame++; present(pms); pms += 8; }
        CHECK(g_rpOn, "present: a load without a loading screen throttled");
        tMenu = 1; made = calls = 0;
        for (int i = 0; i < 125; i++) { made += present(pms); calls++; pms += 8; }
        CHECK(made == calls && !g_rpOn, "present: a menu opened over it gets every frame and ends the throttle (%d of %d)", made, calls);
        tMenu = 0; g_cs.menu = 0; made = calls = 0;
        g_presentSkip = 1; g_lastShownMs = 0;
        for (int i = 0; i < 500; i++) { made += present(pms); calls++; pms += 2; }
        CHECK(made <= 35, "present: with no menu byte the loading screen's rule stands (%d made in 1 s)", made);
        g_presentSkip = 0; g_cs.menu = keepMenu;
        static uint8_t mc[8]; static uint8_t mcTarget;
        uint32_t mtgt = (uint32_t)(uintptr_t)&mcTarget;
        mc[0] = 0x80; mc[1] = 0x3D; memcpy(mc + 2, &mtgt, 4); mc[6] = 0;
        CHECK(MenuByteAt((uintptr_t)mc, (uintptr_t)&mcTarget) == (uintptr_t)&mcTarget, "menu byte: cmp byte [menu], 0");
        CHECK(MenuByteAt((uintptr_t)mc, (uintptr_t)&mcTarget + 1) == 0, "menu byte: a check on another address is refused");
        mc[6] = 1;
        CHECK(MenuByteAt((uintptr_t)mc, (uintptr_t)&mcTarget) == 0, "menu byte: a compare against 1 is refused");
        mc[6] = 0; mc[0] = 0x38;
        CHECK(MenuByteAt((uintptr_t)mc, (uintptr_t)&mcTarget) == 0 && MenuByteAt(0x10, (uintptr_t)&mcTarget) == 0, "menu byte: another instruction, or unreadable code, is refused");
        // Disarmed by the worker (the count stopped): a throttle it was in ends at once, unlogged, and every present is made.
        g_presentSkip = 0; g_rpWinMs = 0; g_rpOn = false; g_rpArmed = true;
        for (int i = 0; i < 375; i++) { if (i % 30 == 0) tFrame++; present(pms); pms += 8; }
        CHECK(g_rpOn, "present: throttled before the worker disarms it");
        LONG doneD = g_rpDone; g_rpArmed = false; made = calls = 0;
        for (int i = 0; i < 125; i++) { if (i % 30 == 0) tFrame++; made += present(pms); calls++; pms += 8; }
        CHECK(made == calls && !g_rpOn && g_rpWinMs == 0 && g_rpDone == doneD, "present: disarmed, every present made at once and the episode ends unlogged (%d of %d)", made, calls);
        g_rpArmed = true;
        // The frame count is taken only where the update that counts it reads as expected, or behind FusionFix's hook on
        // that update (since 2026-08-06) with an untouched load of the count proving the variable.
        static uint8_t fc[8], rd[8]; static uint32_t fcTarget; bool hk = true;
        uint32_t tgt = (uint32_t)(uintptr_t)&fcTarget;
        rd[0] = 0x8B; rd[1] = 0x15; memcpy(rd + 2, &tgt, 4);                 // mov edx, [count]: 1.0.8.0's reader
        fc[0] = 0x83; fc[1] = 0x05; memcpy(fc + 2, &tgt, 4); fc[6] = 1;
        CHECK(FrameCountAt((uintptr_t)fc, (uintptr_t)&fcTarget, 7, (uintptr_t)rd, &hk) == (uintptr_t)&fcTarget && !hk, "frame count: add dword [count], 1 (1.0.8.0)");
        CHECK(FrameCountAt((uintptr_t)fc, (uintptr_t)&fcTarget, 6, (uintptr_t)rd, &hk) == 0, "frame count: 1.0.8.0's form is not taken at the CE's length");
        fc[0] = 0xFF; fc[1] = 0x05; fc[6] = 0xCC;
        CHECK(FrameCountAt((uintptr_t)fc, (uintptr_t)&fcTarget, 6, (uintptr_t)rd, &hk) == (uintptr_t)&fcTarget && !hk, "frame count: inc dword [count] (the CE)");
        CHECK(FrameCountAt((uintptr_t)fc, (uintptr_t)&fcTarget + 4, 6, (uintptr_t)rd, &hk) == 0, "frame count: an instruction on another address is refused");
        fc[0] = 0x83; fc[6] = 2;
        CHECK(FrameCountAt((uintptr_t)fc, (uintptr_t)&fcTarget, 7, (uintptr_t)rd, &hk) == 0, "frame count: adding 2 is not the count");
        fc[0] = 0x8B;
        CHECK(FrameCountAt((uintptr_t)fc, (uintptr_t)&fcTarget, 7, (uintptr_t)rd, &hk) == 0 && FrameCountAt(0x10, (uintptr_t)&fcTarget, 7, (uintptr_t)rd, &hk) == 0,
              "frame count: a load, or unreadable code, is refused");
        fc[0] = 0xE9; fc[1] = 0x11; fc[2] = 0x22; fc[3] = 0x33; fc[4] = 0x44; fc[5] = 0x90; fc[6] = 0x90;
        CHECK(FrameCountAt((uintptr_t)fc, (uintptr_t)&fcTarget, 7, (uintptr_t)rd, &hk) == (uintptr_t)&fcTarget && hk,
              "frame count: FusionFix's hook on 1.0.8.0's update (E9, then 90 90), proven by mov edx, [count]");
        rd[0] = 0xA1; memcpy(rd + 1, &tgt, 4);                                // mov eax, [count]: the CE's reader
        CHECK(FrameCountAt((uintptr_t)fc, (uintptr_t)&fcTarget, 6, (uintptr_t)rd, &hk) == (uintptr_t)&fcTarget && hk,
              "frame count: and on the CE's (E9, then 90), proven by mov eax, [count]");
        memset(fc, 0x90, 7);
        CHECK(FrameCountAt((uintptr_t)fc, (uintptr_t)&fcTarget, 7, (uintptr_t)rd, &hk) == (uintptr_t)&fcTarget && hk, "frame count: all NOPs, between FusionFix's two writes");
        fc[0] = 0xE9; fc[5] = 0xCC;
        CHECK(FrameCountAt((uintptr_t)fc, (uintptr_t)&fcTarget, 7, (uintptr_t)rd, &hk) == 0, "frame count: a jump followed by anything but NOPs is refused");
        fc[5] = 0x90;
        CHECK(FrameCountAt((uintptr_t)fc, (uintptr_t)&fcTarget + 4, 7, (uintptr_t)rd, &hk) == 0, "frame count: a hook whose reader loads another address is refused");
        rd[0] = 0x8B; rd[1] = 0x45; memcpy(rd + 2, &tgt, 4);                  // mov eax, [ebp + disp8]: not a load of an address
        CHECK(FrameCountAt((uintptr_t)fc, (uintptr_t)&fcTarget, 7, (uintptr_t)rd, &hk) == 0, "frame count: a reader that is not a load of the count is refused");
        // The worker arms the throttle once a world has loaded and the count is seen moving in play on two ticks in a row,
        // and disarms it when the count stands still in play while presents go out (a hook that stopped it).
        {
            static uint32_t lf = 500; static uint8_t lm = 0;
            uintptr_t keepF = g_cs.frame, keepM = g_cs.menu; bool keepW = g_worldLogged;
            g_cs.frame = (uintptr_t)&lf; g_cs.menu = (uintptr_t)&lm;
            auto fresh = [&]() { g_rpArmed = false; g_flSampled = false; g_flDisarmed = false; g_flArmedOnce = false; g_flStuck = 0; g_flMoved = 0; g_flLines = 0; };
            fresh(); g_worldLogged = false;
            FrameLiveTick(1000, false, true); lf += 60; FrameLiveTick(2000, false, true); lf += 60; FrameLiveTick(3000, false, true);
            CHECK(!g_rpArmed, "frame count: the count moves but no world has loaded: not armed");
            g_worldLogged = true; lf += 60; FrameLiveTick(4000, true, false);
            CHECK(!g_rpArmed, "frame count: a loading screen up: not armed");
            lf += 60; FrameLiveTick(5000, false, false);
            CHECK(!g_rpArmed && g_flMoved == 0, "frame count: nobody in the world yet: not counted");
            lf += 60; FrameLiveTick(6000, false, true);
            CHECK(!g_rpArmed && g_flMoved == 1, "frame count: one tick moving in play is not enough yet");
            lf += 60; FrameLiveTick(7000, false, true);
            CHECK(g_rpArmed && g_flArmedOnce, "frame count: the second tick in a row: armed, and said once");
            // Still, with the game's own presents going out through the hook (120 a tick): the hook's count drives it.
            for (int i = 0; i < FRAME_STUCK_TICKS - 1; i++) { for (int k = 0; k < 120; k++) { present(pms); pms += 8; } FrameLiveTick(8000 + 1000 * i, false, true); }
            CHECK(g_rpArmed, "frame count: still for %d s with presents going out: still armed", FRAME_STUCK_TICKS - 1);
            for (int k = 0; k < 120; k++) { present(pms); pms += 8; } FrameLiveTick(12000, false, true);
            CHECK(!g_rpArmed && g_flDisarmed, "frame count: still for %d s, counted from the hook's own presents: disarmed", FRAME_STUCK_TICKS);
            lf += 3; g_presentCalls += 120; FrameLiveTick(13000, false, true);
            CHECK(!g_rpArmed, "frame count: moving again for one tick: not yet");
            lf += 3; g_presentCalls += 120; FrameLiveTick(14000, false, true);
            CHECK(g_rpArmed, "frame count: moving again for two (three frames a second are enough): armed again");
            for (int i = 0; i < 20; i++) { lf += 2; g_presentCalls += 120; FrameLiveTick(15000 + 1000 * i, false, true); }
            CHECK(g_rpArmed, "frame count: a taxi skip, 2 frames and 120 presents a second for 20 s: still armed");
            for (int i = 0; i < 10; i++) { g_presentCalls += 5; FrameLiveTick(40000 + 1000 * i, false, true); }
            CHECK(g_rpArmed, "frame count: still with few presents (the game not drawing): still armed");
            lm = 1;
            for (int i = 0; i < 10; i++) { g_presentCalls += 120; FrameLiveTick(60000 + 1000 * i, false, true); }
            CHECK(g_rpArmed, "frame count: still under a menu: still armed");
            lm = 0;
            for (int i = 0; i < 10; i++) { g_presentCalls += 120; FrameLiveTick(70000 + 1000 * i, true, true); }
            CHECK(g_rpArmed, "frame count: still under a loading screen: still armed");
            for (int i = 0; i < 10; i++) { g_presentCalls += 120; FrameLiveTick(80000 + 1000 * i, false, false); }
            CHECK(g_rpArmed && g_flStuck == 0, "frame count: still with nobody in the world: neither counted nor disarmed");
            // A write outside the update (a save load's restore: 840 -> 1213851 in T12) is neither moving nor still.
            fresh(); FrameLiveTick(90000, false, true);
            lf += 1213000; g_presentCalls += 120; FrameLiveTick(91000, false, true);
            lf += 60; g_presentCalls += 120; FrameLiveTick(92000, false, true);
            CHECK(!g_rpArmed && g_flMoved == 1, "frame count: a save load's jump is not a move: one real tick after it is not enough");
            // A count that never moves in play never arms it, and it says so once.
            fresh();
            for (int i = 0; i < 12; i++) { g_presentCalls += 120; FrameLiveTick(100000 + 1000 * i, false, true); }
            CHECK(!g_rpArmed && g_flDisarmed && g_flLines == 1 && !g_flArmedOnce, "frame count: a count that never moves never arms it, and says so once (%d lines)", g_flLines);
            g_cs.frame = keepF; g_cs.menu = keepM; g_worldLogged = keepW; fresh();
        }
        g_cs.frame = keepFrame; g_tPresentMs = -1; g_rpWinMs = 0; g_rpOn = false; g_rpArmed = false; g_presentSkip = 0; g_showMs = PRESENT_SHOW_MS;
        // A second Direct3DCreate9 hooks nothing again, and a second CreateDevice leaves the one hook as it is.
        ((CreateDevice_t)roD3D[16])(&g_tD3D, 0, 1, NULL, 0, NULL, &dev);
        CHECK(g_tDevCreates == 2 && roSwap[3] == (void*)HkSwapPresent && g_oSwapPresent == (SwapPresent_t)FakeSwapPresent && g_tSwapRefs == 0,
              "present: a second device does not rehook the swap chain over itself, and releases what it took");
        g_tD3D.vtbl = g_tD3DVtbl; g_tDev.vtbl = g_tDevVtbl; g_tSwap.vtbl = g_tSwapVtbl; VirtualFree(roD3D, 0, MEM_RELEASE);
        g_d3dVtblHooked = NULL; g_swapVtblHooked = NULL; g_oCreateDevice = NULL; g_oSwapPresent = NULL;
        g_tDxvkImage = NULL; g_tDxvkImageN = 0;
        g_dxvkVer[0] = g_dxvkVer[1] = g_dxvkVer[2] = -1;
        g_tPrm[0] = 0; g_tPrm[1] = (uint32_t)(uintptr_t)g_flagArg; g_tPrm[2] = (uint32_t)(uintptr_t)g_vidmemArg;
        keep.prmUnmanaged = c.prmUnmanaged; keep.prmManaged = c.prmManaged; keep.prmVidmem = c.prmVidmem;
        g_cs = keep;
    }
    // ---- the brake's decisions, as a sequence (4K, X 799: the wall of 4000 is 4,317 MB of V)
    {
        BrakeCfg bc = { 512, 32, 128, 192, 20, 800 }; int ba = -1;
        BrakeState bs = { 4000, false, -1, 0 };
        // (560, not 1000: since 1.0.5 a fall of 96 MB or more in the second before the next tick is a fast fall, 8az)
        CHECK(BrakeDecide(bs, 0, 3300, 799, 560, bc, &ba) == 4000 && ba == BRAKE_NONE && !bs.held, "brake: plenty of room, nothing");
        CHECK(BrakeDecide(bs, 1000, 3300, 799, 500, bc, &ba) == 2870 && ba == BRAKE_HOLD && bs.held,
              "brake: under 512 MB it holds once, at the budget whose wall is the V held now (%d, want 2870)", bs.t);
        CHECK(BrakeDecide(bs, 2000, 3290, 799, 499, bc, &ba) == 2870 && ba == BRAKE_NONE, "brake: held once, not again as V dips");
        CHECK(BrakeDecide(bs, 6000, 3300, 799, 460, bc, &ba) == 2870 && ba == BRAKE_NONE, "brake: a fall inside the wait does nothing");
        CHECK(BrakeDecide(bs, 21000, 3300, 799, 460, bc, &ba) == 2742 && ba == BRAKE_STEP, "brake: a 40 MB fall after the wait is one step (%d, want 2742)", bs.t);
        CHECK(BrakeDecide(bs, 42000, 3200, 799, 450, bc, &ba) == 2742 && ba == BRAKE_NONE, "brake: a 10 MB fall is not enough");
        // The fall is measured from L at the last action: L rising and falling back to where it was is no fall (the
        // field report lost two steps to that, at L 404 after rises to 420 and 436).
        CHECK(BrakeDecide(bs, 63000, 3200, 799, 470, bc, &ba) == 2742 && ba == BRAKE_NONE && bs.refL == 460, "brake: a rise does not move the reference");
        CHECK(BrakeDecide(bs, 84000, 3200, 799, 437, bc, &ba) == 2742 && ba == BRAKE_NONE,
              "brake: 33 MB under the highest point since, but 23 under L at the last action, is no step (%d)", bs.t);
        CHECK(BrakeDecide(bs, 85000, 3200, 799, 428, bc, &ba) == 2614 && ba == BRAKE_STEP && bs.refL == 428,
              "brake: 32 MB under L at the last action is a step (%d)", bs.t);
        CHECK(BrakeDecide(bs, 91000, 3000, 799, 180, bc, &ba) == 2614 && ba == BRAKE_NONE, "brake: an emergency, but only 6 s after the last action");
        CHECK(BrakeDecide(bs, 95000, 3000, 799, 180, bc, &ba) == 2486 && ba == BRAKE_EMERGENCY, "brake: an emergency after 10 s needs no fall (%d)", bs.t);
        CHECK(BrakeDecide(bs, 105000, 3000, 799, 180, bc, &ba) == 2358 && ba == BRAKE_EMERGENCY, "brake: and again 10 s later, still no fall");
        bs.t = 850; bs.lastActMs = 0;
        CHECK(BrakeDecide(bs, 200000, 3000, 799, 100, bc, &ba) == 800 && ba == BRAKE_EMERGENCY, "brake: never under the floor");
        CHECK(BrakeDecide(bs, 300000, 3000, 799, 50, bc, &ba) == 800 && ba == BRAKE_NONE, "brake: at the floor, nothing more");
        // A fast fall (BRAKE_FAST_MB from one tick to the next, a second apart; a player's crash, FINDINGS 8az): it acts on
        // that tick, without the wait, two steps at once; a hold reached on one goes two steps under what is in use.
        {
            BrakeCfg fc = { 512, 32, 128, 192, 20, 800 }; int fa = -1;
            int hold = (int)(((3878 + 2) * 10 + 8) / 9 - 749);
            BrakeState fs = { 4000, false, -1, 0 };
            BrakeDecide(fs, 1000, 3878, 749, 520, fc, &fa);                  // his last tick over the line
            int t = BrakeDecide(fs, 2000, 3878, 749, 398, fc, &fa);          // 122 MB in one second, under it
            CHECK(fa == BRAKE_HOLD && t == hold - 256 && fs.fastMb == 122,
                  "fast: a hold reached on a fall of 122 MB in one second goes two steps under what is in use (%d, the plain hold %d)", t, hold);
            t = BrakeDecide(fs, 3000, 3800, 749, 290, fc, &fa);
            CHECK(fa == BRAKE_FAST && t == hold - 512 && fs.fastMb == 108, "fast: 108 MB more a second later: two steps at once, no 20 s wait (%d)", t);
            t = BrakeDecide(fs, 4000, 3750, 749, 250, fc, &fa);
            CHECK(fa == BRAKE_NONE && t == hold - 512 && fs.fastMb == 0, "fast: 40 MB in a second is not fast, and the step's wait stands (%d)", t);
            t = BrakeDecide(fs, 5000, 3700, 749, 150, fc, &fa);
            CHECK(fa == BRAKE_FAST && t == hold - 768, "fast: under the emergency line a fast fall still acts at once, not after 10 s (%d)", t);
            // The same falls two seconds apart (a missed tick, a pause): not fast, the plain rules.
            BrakeState gs = { 4000, false, -1, 0 };
            BrakeDecide(gs, 1000, 3878, 749, 520, fc, &fa);
            t = BrakeDecide(gs, 3000, 3878, 749, 398, fc, &fa);
            CHECK(fa == BRAKE_HOLD && t == hold && gs.fastMb == 0, "fast: the same fall across two seconds is a plain hold (%d)", t);
            t = BrakeDecide(gs, 5000, 3800, 749, 290, fc, &fa);
            CHECK(fa == BRAKE_NONE && t == hold, "fast: and the next fall two seconds on waits as before (%d)", t);
            // Never under the floor.
            BrakeState ls = { 900, true, 600, 0 }; ls.lastL = 600; ls.lastLMs = 9000; ls.downMs = 9000;
            t = BrakeDecide(ls, 10000, 3000, 749, 450, fc, &fa);
            CHECK(t == 800 && fa == BRAKE_FAST, "fast: two steps stop at the floor (%d)", t);
            // A fast cut, then the climb back. The ceiling only learns from a down action that follows a raise, so the
            // case runs one first: 2870 proven, a step to 2742, the raise back to 2870, pressed. A fast fall more than
            // five minutes after that raise and its press is a burst: the ceiling stays 2870 and the budget climbs
            // back one step per quiet five minutes, each pressed before the next. The same fall inside those five
            // minutes is the world growing into the raise: the ceiling comes down to the cut and the climb stops there.
            {
                BrakeCfg uc = { 512, 32, 128, 192, 20, 800, 1, 0, 0 }; int ua = -1;
                auto lead = [&](BrakeState& s) {
                    s = BrakeState{}; s.t = 4000; s.refL = -1;
                    BrakeDecide(s, 1000, 3300, 799, 500, uc, &ua);             // the hold: 2870
                    BrakeDecide(s, 301000, 3300, 799, 500, uc, &ua);           // 2870 proven: the ceiling
                    BrakeDecide(s, 330000, 3300, 799, 466, uc, &ua);           // a 34 MB fall: a step to 2742
                    BrakeDecide(s, 631000, 3300, 799, 466, uc, &ua);           // five quiet minutes: back to 2870
                    BrakeDecide(s, 632000, 3300, 799, 466, uc, &ua);           // pressed
                };
                BrakeState us; lead(us);
                CHECK(us.t == 2870 && us.ceiling == 2870 && us.raised && !us.awaitPress, "climb: the lead-in: proven, stepped, raised back and pressed (%d, ceiling %d)", us.t, us.ceiling);
                BrakeDecide(us, 940000, 3300, 799, 466, uc, &ua);
                BrakeDecide(us, 941000, 3300, 799, 350, uc, &ua);
                CHECK(ua == BRAKE_FAST && us.t == 2614 && us.ceiling == 2870, "climb: a fast fall long after the raise is a burst: two steps cut, the ceiling stays 2870 (%d, %d)", us.t, us.ceiling);
                BrakeDecide(us, 1100000, 3300, 799, 350, uc, &ua);
                CHECK(ua == BRAKE_NONE && us.t == 2614, "climb: nothing back before five quiet minutes (%d)", us.t);
                BrakeDecide(us, 1241000, 3300, 799, 350, uc, &ua);
                CHECK(ua == BRAKE_RAISE && us.t == 2742, "climb: five quiet minutes later, one step back (%d)", us.t);
                BrakeDecide(us, 1242000, 3300, 799, 350, uc, &ua);
                BrakeDecide(us, 1400000, 3300, 799, 350, uc, &ua);
                CHECK(ua == BRAKE_NONE && us.t == 2742, "climb: the next step waits five minutes from when the world pressed the last (%d)", us.t);
                BrakeDecide(us, 1542000, 3300, 799, 350, uc, &ua);
                CHECK(ua == BRAKE_RAISE && us.t == 2870, "climb: and comes back to the ceiling (%d)", us.t);
                BrakeDecide(us, 2000000, 3300, 799, 350, uc, &ua);
                CHECK(ua == BRAKE_NONE && us.t == 2870, "climb: never past the ceiling (%d)", us.t);
                BrakeState vs; lead(vs);
                BrakeDecide(vs, 700000, 3300, 799, 466, uc, &ua);
                BrakeDecide(vs, 701000, 3300, 799, 350, uc, &ua);
                CHECK(ua == BRAKE_FAST && vs.t == 2614 && vs.ceiling == 2614, "climb: the same fall a minute after the raise lowers the ceiling to the cut (%d, %d)", vs.t, vs.ceiling);
                BrakeDecide(vs, 1100000, 3300, 799, 350, uc, &ua);
                BrakeDecide(vs, 1500000, 3300, 799, 350, uc, &ua);
                CHECK(vs.t == 2614, "climb: and nothing climbs past it (%d)", vs.t);
            }
            // Emergency steps that free nothing stop (two field logs of 1.0.5, FINDINGS 8bb), replayed with their own numbers.
            {
                BrakeCfg ec = { 512, 32, 128, 192, 20, 800, 1, 0, 0 }; int ea = -1;
                // The first: held, stepped, a ceiling at 3175, a step to 3047 at 944 s; a fast fall of 112 MB at 1945 s,
                // then the largest free block flat at 133 to 138 MB. 1.0.5 stepped every 10 s to the floor of 800.
                BrakeState es = {}; es.t = 3047; es.held = true; es.refL = 245; es.lastActMs = es.downMs = 944000; es.ceiling = 3175;
                BrakeDecide(es, 1944000, 3300, 799, 245, ec, &ea);
                BrakeDecide(es, 1945000, 3175, 799, 133, ec, &ea);
                CHECK(ea == BRAKE_FAST && es.t == 2791, "stall: his fast fall at 1945 s, two steps (%d)", es.t);
                BrakeDecide(es, 1955000, 2937, 799, 133, ec, &ea);
                CHECK(ea == BRAKE_EMERGENCY && es.t == 2663 && !es.stalled, "stall: the fast cut freed nothing: one more emergency step (%d)", es.t);
                BrakeDecide(es, 1966000, 2830, 799, 133, ec, &ea);
                CHECK(ea == BRAKE_NONE && es.t == 2663 && es.stalled && (es.ev & BRAKE_EV_STALL), "stall: that one freed nothing either: the steps stop at 2663 (%d)", es.t);
                const int64_t laterL[] = { 133, 133, 133, 133, 133, 133, 138, 138, 138, 138, 138, 133 };
                bool quiet = true;
                for (int i = 0; i < 12; i++) { BrakeDecide(es, 1976000 + 10500 * i, 2500, 799, laterL[i], ec, &ea); if (ea != BRAKE_NONE || (es.ev & BRAKE_EV_STALL)) quiet = false; }
                CHECK(quiet && es.t == 2663, "stall: the rest of his three minutes: no step, said once, the budget holds 2663 where 1.0.5 reached 800 (%d)", es.t);
                // The second: stepped to 2333 at 586 s with the block at 408, then 91 MB at 624 s and flat at 91.
                BrakeState zs = {}; zs.t = 2333; zs.held = true; zs.refL = 408; zs.lastActMs = zs.downMs = 586000; zs.lastL = 408; zs.lastLMs = 586000;
                BrakeDecide(zs, 624000, 2812, 799, 91, ec, &ea);
                CHECK(ea == BRAKE_EMERGENCY && zs.t == 2205, "stall: his fall to 91 MB: an emergency step (%d)", zs.t);
                BrakeDecide(zs, 634000, 2708, 799, 91, ec, &ea);
                CHECK(ea == BRAKE_EMERGENCY && zs.t == 2077, "stall: it freed nothing: one more (%d)", zs.t);
                BrakeDecide(zs, 644000, 2590, 799, 91, ec, &ea);
                CHECK(ea == BRAKE_NONE && zs.t == 2077 && zs.stalled, "stall: nothing again: stopped at 2077 where 1.0.5 reached 800 (%d)", zs.t);
                for (int i = 0; i < 9; i++) BrakeDecide(zs, 655000 + 10500 * i, 2400, 799, 91, ec, &ea);
                CHECK(zs.t == 2077, "stall: and it holds (%d)", zs.t);
                // A further fall of a step's fall (32 MB) resumes them; flat again, they stop again.
                BrakeDecide(zs, 760000, 2400, 799, 55, ec, &ea);
                CHECK(ea == BRAKE_EMERGENCY && zs.t == 1949 && !zs.stalled, "stall: a further 36 MB fall resumes the steps (%d)", zs.t);
                BrakeDecide(zs, 770500, 2300, 799, 55, ec, &ea);
                BrakeDecide(zs, 781000, 2300, 799, 55, ec, &ea);
                CHECK(ea == BRAKE_NONE && zs.t == 1821 && zs.stalled, "stall: one more that frees nothing, then they stop again (%d)", zs.t);
                // The block back over the emergency line, then under it again: the count starts afresh.
                BrakeDecide(zs, 800000, 2300, 799, 300, ec, &ea);
                CHECK(!zs.stalled && zs.emFlat == 0, "stall: back over the emergency line, the stop is lifted");
                BrakeDecide(zs, 900000, 2300, 799, 150, ec, &ea);
                CHECK(ea == BRAKE_EMERGENCY && zs.t == 1693, "stall: under it again, an emergency step (%d)", zs.t);
                // Through the worker's tick, onto the release's log.
                TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); TSizeSettle(1000, 3000);
                g_tV = 3300ull << 20; SisTick(20000, 500); SisTick(41000, 150); SisTick(52000, 150); SisTick(63000, 150); SisTick(74000, 150);
                CHECK(g_brakeSt.t == 2614 && g_brakeSt.stalled && TTabAll(2614), "stall: in play: held, two emergency steps, stopped at 2614 (%d)", g_brakeSt.t);
                g_tV = 0; TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
            }
            // In play, through the worker's tick, onto the release's log (sis_test reads the lines back).
            TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); TSizeSettle(1000, 3000);
            g_tV = 3878ull << 20; SisTick(30000, 520); SisTick(31000, 398); int t1 = g_brakeSt.t; SisTick(32000, 290);
            int want1 = (int)(((3878 + 2) * 10 + 8) / 9 - 799) - 256;
            CHECK(g_brakeSt.held && t1 == want1 && g_brakeSt.t == want1 - 256 && TTabAll((uint64_t)(want1 - 256)),
                  "fast: in play, the hold on a fast fall and two steps a second later, written to the table (%d, %d)", t1, g_brakeSt.t);
            g_tV = 0; TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        }
        BrakeState bw = { 4000, false, -1, 0 };
        CHECK(BrakeDecide(bw, 0, 4317, 799, 500, bc, &ba) == 4000 && ba == BRAKE_NONE && bw.held, "brake: a hold at the wall latches, no write");
        BrakeState bl = { 2000, false, -1, 0 };
        CHECK(BrakeDecide(bl, 0, 3300, 799, 500, bc, &ba) == 2000 && ba == BRAKE_NONE && bl.held, "brake: a hold never raises a lower budget");
        BrakeState bn = { 4000, false, -1, 0 };
        CHECK(BrakeDecide(bn, 0, -1, 799, 100, bc, &ba) == 4000 && ba == BRAKE_NONE && !bn.held, "brake: no V read, nothing");
    }
    // ---- the way back up (docs/TWO-WAY-BUDGET.md): a level proven to hold is the ceiling, the budget comes back to it,
    // and a step after a raise lowers it for good. 4K, X 799: the wall of 2870 is 3,300 MB of V, of 2742 3,184, of 2614 3,069.
    {
        BrakeCfg rc = { 512, 32, 128, 192, 20, 800, 1, 4000 }; int ra = -1;
        BrakeState rs = { 4000, false, -1, 0 };
        CHECK(BrakeDecide(rs, 0, 3300, 799, 500, rc, &ra) == 2870 && ra == BRAKE_HOLD, "up: the hold (%d)", rs.t);
        BrakeDecide(rs, 200000, 3290, 799, 500, rc, &ra);
        CHECK(ra == BRAKE_NONE && rs.ceiling == 0, "up: no ceiling before the level has stood 300 s");
        BrakeDecide(rs, 299000, 3290, 799, 500, rc, &ra);
        CHECK(ra == BRAKE_NONE && rs.ceiling == 0, "up: still none at 299 s");
        BrakeDecide(rs, 300000, 3290, 799, 500, rc, &ra);
        CHECK(ra == BRAKE_NONE && rs.ceiling == 2870 && (rs.ev & BRAKE_EV_CEIL_SET), "up: 300 s pressed with L flat proves the level: ceiling %d", rs.ceiling);
        CHECK(BrakeDecide(rs, 320000, 3290, 799, 468, rc, &ra) == 2742 && ra == BRAKE_STEP && rs.ceiling == 2870 && !rs.ev,
              "up: a step from a level never raised to leaves the ceiling (%d, ceiling %d)", rs.t, rs.ceiling);
        CHECK(BrakeDecide(rs, 345000, 3200, 799, 436, rc, &ra) == 2614 && ra == BRAKE_STEP, "up: a second step (%d)", rs.t);
        CHECK(BrakeDecide(rs, 600000, 3050, 799, 436, rc, &ra) == 2614 && ra == BRAKE_NONE, "up: no raise 255 s after a down action");
        CHECK(BrakeDecide(rs, 645000, 3050, 799, 436, rc, &ra) == 2742 && ra == BRAKE_RAISE && rs.raised,
              "up: 300 s after the last step, pressed and quiet: one step back up (%d)", rs.t);
        CHECK(BrakeDecide(rs, 1000000, 3050, 799, 436, rc, &ra) == 2742 && ra == BRAKE_NONE && rs.awaitPress,
              "up: no second raise before the world has pressed the first, however long it waits");
        CHECK(BrakeDecide(rs, 1100000, 3130, 799, 436, rc, &ra) == 2742 && ra == BRAKE_NONE && !rs.awaitPress, "up: the world presses the raise");
        CHECK(BrakeDecide(rs, 1399000, 3130, 799, 436, rc, &ra) == 2742 && ra == BRAKE_NONE, "up: and the next raise waits 300 s from then");
        CHECK(BrakeDecide(rs, 1400000, 3130, 799, 436, rc, &ra) == 2870 && ra == BRAKE_RAISE, "up: the second raise, to the ceiling (%d)", rs.t);
        int acts = 0, evs = 0;
        for (int64_t ms = 1401000; ms <= 5000000; ms += 1000) { BrakeDecide(rs, ms, 3300, 799, 436, rc, &ra); acts += ra != BRAKE_NONE; evs += rs.ev != 0; }
        CHECK(acts == 0 && evs == 0 && rs.t == 2870, "up: an hour at the ceiling, pressed and quiet: nothing (%d actions, %d events)", acts, evs);
        CHECK(BrakeDecide(rs, 5100000, 3300, 799, 420, rc, &ra) == 2870 && ra == BRAKE_NONE, "up: 16 MB under L at the last action is no step");
        CHECK(BrakeDecide(rs, 5190000, 3300, 799, 404, rc, &ra) == 2742 && ra == BRAKE_STEP && rs.ceiling == 2742 && (rs.ev & BRAKE_EV_CEIL_LEARNED) && !rs.raised,
              "up: a slow fall (drift, 32 MB over 90 s) long after a raise lowers the ceiling to where it lands (%d, ceiling %d)", rs.t, rs.ceiling);
        acts = 0;
        for (int64_t ms = 5191000; ms <= 7000000; ms += 1000) { BrakeDecide(rs, ms, 3180, 799, 404, rc, &ra); acts += ra == BRAKE_RAISE; }
        CHECK(acts == 0 && rs.t == 2742, "up: and nothing ever raises past the lowered ceiling (%d)", rs.t);

        // A raise never blinds the brake: an emergency or a step right after it fires at once, and lowers the ceiling.
        BrakeState es = {}; es.t = 2614; es.held = true; es.refL = 270; es.ceiling = 2870; es.proofL = 270; es.quietHiL = 260; es.xRef = 799;
        CHECK(BrakeDecide(es, 400000, 3050, 799, 257, rc, &ra) == 2742 && ra == BRAKE_RAISE, "up: a raise at L 257, 1 MB over the raise line (%d)", es.t);
        CHECK(BrakeDecide(es, 403000, 3050, 799, 180, rc, &ra) == 2614 && ra == BRAKE_EMERGENCY && es.ceiling == 2614,
              "up: L 180 3 s later is an emergency at once, and the ceiling comes down with it (%d, ceiling %d)", es.t, es.ceiling);
        BrakeState ss = {}; ss.t = 2614; ss.held = true; ss.refL = 300; ss.ceiling = 2870; ss.proofL = 300; ss.quietHiL = 295; ss.xRef = 799;
        CHECK(BrakeDecide(ss, 400000, 3050, 799, 290, rc, &ra) == 2742 && ra == BRAKE_RAISE, "up: a raise (%d)", ss.t);
        CHECK(BrakeDecide(ss, 403000, 3050, 799, 268, rc, &ra) == 2614 && ra == BRAKE_STEP && ss.ceiling == 2614,
              "up: a net 32 MB fall 3 s later is a step at once (%d, ceiling %d)", ss.t, ss.ceiling);

        // Every condition, one missing at a time, against a state where the raise fires.
        BrakeState b0 = {}; b0.t = 2614; b0.held = true; b0.refL = 436; b0.ceiling = 2870; b0.proofL = 436; b0.quietHiL = 436; b0.xRef = 799;
        BrakeState b = b0;
        CHECK(BrakeDecide(b, 400000, 3050, 799, 436, rc, &ra) == 2742 && ra == BRAKE_RAISE, "up: the control case raises (%d)", b.t);
        b = b0; b.quietMs = 200000;
        CHECK(BrakeDecide(b, 400000, 3050, 799, 436, rc, &ra) == 2614 && ra == BRAKE_NONE, "up: none with a chunk lost 200 s ago");
        b = b0; b.refL = b.proofL = b.quietHiL = 250;
        CHECK(BrakeDecide(b, 400000, 3050, 799, 250, rc, &ra) == 2614 && ra == BRAKE_NONE, "up: none with L under the emergency line + 64");
        b = b0;
        CHECK(BrakeDecide(b, 400000, 3000, 799, 436, rc, &ra) == 2614 && ra == BRAKE_NONE, "up: none while the world does not press the budget");
        b = b0; b.ceiling = 2614;
        CHECK(BrakeDecide(b, 400000, 3050, 799, 436, rc, &ra) == 2614 && ra == BRAKE_NONE, "up: none at the ceiling");
        b = b0; b.lastActMs = 200000;
        CHECK(BrakeDecide(b, 400000, 3050, 799, 436, rc, &ra) == 2614 && ra == BRAKE_NONE, "up: none 200 s after a down action");
        b = b0; BrakeCfg off = rc; off.raiseOn = 0;
        CHECK(BrakeDecide(b, 400000, 3050, 799, 436, off, &ra) == 2614 && ra == BRAKE_NONE, "up: none with raises off (ExtraStreamingMemory, Wine with the lines lowered)");
        b = b0; BrakeCfg capped = rc; capped.capMb = 2700;
        CHECK(BrakeDecide(b, 400000, 3050, 799, 436, capped, &ra) == 2700 && ra == BRAKE_RAISE && b.ceiling == 2700 && (b.ev & BRAKE_EV_CEIL_CAP),
              "up: the sizing's figure after a resize caps the ceiling and the raise, and says so (%d, ceiling %d)", b.t, b.ceiling);
        // Smaller render targets clamp the ceiling to the budget in force once they have held BRAKE_X_TICKS; a device
        // reset's one-tick dip (T18: 797, 518, 801 MB) does not. A down action 200 s ago keeps raises out of the way.
        b = b0; b.xRef = 797; b.lastActMs = 200000;
        BrakeDecide(b, 400000, 3050, 518, 436, rc, &ra);
        BrakeDecide(b, 401000, 3050, 801, 436, rc, &ra);
        CHECK(b.ceiling == 2870 && !b.ev && b.xRef == 797, "up: a device reset's one-tick dip in the render targets leaves the ceiling (%d)", b.ceiling);
        BrakeDecide(b, 402000, 3050, 252, 436, rc, &ra);
        BrakeDecide(b, 403000, 3050, 252, 436, rc, &ra);
        CHECK(b.ceiling == 2870, "up: two ticks smaller are not yet a new resolution");
        CHECK(BrakeDecide(b, 404000, 3050, 252, 436, rc, &ra) == 2614 && ra == BRAKE_NONE && b.ceiling == 2614 && (b.ev & BRAKE_EV_CEIL_X) && b.xRef == 252,
              "up: the third is: the ceiling comes to the budget in force (ceiling %d)", b.ceiling);
        // With every raise condition met, a smaller set being confirmed holds the raise off: the ceiling it sets is the
        // level in force, not a raised one (the 1.0.4 review, brake-3).
        b = b0;
        BrakeDecide(b, 400000, 3050, 252, 436, rc, &ra);
        CHECK(ra == BRAKE_NONE && b.t == 2614 && b.xLowTicks == 1, "up: no raise on the first tick of a smaller render-target set (%d)", b.t);
        BrakeDecide(b, 401000, 3050, 252, 436, rc, &ra); BrakeDecide(b, 402000, 3050, 252, 436, rc, &ra);
        CHECK(b.t == 2614 && b.ceiling == 2614, "up: nor on the second; the third sets the ceiling to the level in force (%d, ceiling %d)", b.t, b.ceiling);
        // Before any ceiling is proven a smaller set proves nothing: the ceiling stays unset (the X rule's guard).
        BrakeState xg = {}; xg.t = 2614; xg.held = true; xg.refL = 436; xg.proofL = 436; xg.quietHiL = 436; xg.xRef = 797;
        for (int i = 0; i < 3; i++) BrakeDecide(xg, 400000 + 1000 * i, 2900, 252, 436, rc, &ra);
        CHECK(xg.ceiling == 0 && xg.xRef == 252, "up: a smaller set with no ceiling proven leaves it unset (%d), and is the new reference", xg.ceiling);
        // A larger set restarts the proof: no ceiling from a span that ran mostly at the old resolution.
        BrakeState xu = {}; xu.t = 2614; xu.held = true; xu.refL = 436; xu.proofL = 436; xu.quietHiL = 436; xu.xRef = 252;
        BrakeDecide(xu, 400000, 2900, 797, 436, rc, &ra);
        BrakeDecide(xu, 600000, 3100, 797, 436, rc, &ra);
        CHECK(xu.ceiling == 0 && xu.xRef == 797, "up: a larger set restarts the proof: none 200 s after it (%d)", xu.ceiling);
        BrakeDecide(xu, 700000, 3100, 797, 436, rc, &ra);
        CHECK(xu.ceiling == 2614, "up: and 300 s after it the level is proven (%d)", xu.ceiling);
        // A down action restarts the proof: an emergency at a constant L, then L over the line: no proof for 300 s.
        BrakeState xp = {}; xp.t = 2614; xp.held = true; xp.refL = 180; xp.proofL = 180; xp.quietHiL = 180; xp.xRef = 799;
        CHECK(BrakeDecide(xp, 400000, 2950, 799, 180, rc, &ra) == 2486 && ra == BRAKE_EMERGENCY, "up: an emergency (%d)", xp.t);
        BrakeDecide(xp, 410000, 2950, 799, 300, rc, &ra); BrakeDecide(xp, 600000, 2950, 799, 300, rc, &ra);
        CHECK(xp.ceiling == 0, "up: no proof 200 s after the down action (%d)", xp.ceiling);
        BrakeDecide(xp, 700000, 2950, 799, 300, rc, &ra);
        CHECK(xp.ceiling == 2486, "up: proven 300 s after it (%d)", xp.ceiling);
        // The quiet window restarts when L falls more than 8 MB under its highest, even with no step: the raise waits
        // 300 s from that fall, not from the last down action.
        BrakeState qw = {}; qw.t = 2742; qw.held = true; qw.refL = 468; qw.ceiling = 2870; qw.proofL = 468; qw.quietHiL = 468; qw.xRef = 799;
        qw.quietMs = qw.lastActMs = qw.proofMs = 320000;
        BrakeDecide(qw, 520000, 3200, 799, 452, rc, &ra);
        CHECK(BrakeDecide(qw, 620000, 3200, 799, 452, rc, &ra) == 2742 && ra == BRAKE_NONE, "up: 16 MB lost 100 s ago, no step: no raise 300 s after the last one");
        CHECK(BrakeDecide(qw, 820000, 3200, 799, 452, rc, &ra) == 2870 && ra == BRAKE_RAISE, "up: the raise comes 300 s after that loss (%d)", qw.t);
        // An emergency is never a burst: a fast fall under the emergency line long after a climb lowers the ceiling.
        BrakeState ee = {}; ee.t = 2870; ee.held = true; ee.refL = 270; ee.ceiling = 2870; ee.raised = true; ee.lastUpMs = 100000; ee.pressMs = 100000;
        ee.proofL = 270; ee.quietHiL = 270; ee.xRef = 799;
        BrakeDecide(ee, 500000, 3300, 799, 270, rc, &ra); BrakeDecide(ee, 501000, 3300, 799, 250, rc, &ra);
        CHECK(BrakeDecide(ee, 503000, 3300, 799, 180, rc, &ra) == 2742 && ra == BRAKE_EMERGENCY && ee.ceiling == 2742 && (ee.ev & BRAKE_EV_CEIL_LEARNED),
              "burst: an emergency 400 s after a climb is never a burst: the ceiling comes down with it (%d)", ee.ceiling);
        // Bursts and drift (owner, 2026-09-26). After a climb back to the proven level, a burst (32 MB lost in 2 s, 15
        // minutes after the raise) steps the budget down but teaches the ceiling nothing, and the budget comes back.
        BrakeState bb = {}; bb.t = 2870; bb.held = true; bb.refL = 436; bb.ceiling = 2870; bb.raised = true; bb.lastUpMs = 100000;
        bb.proofL = 436; bb.quietHiL = 436; bb.xRef = 799;
        BrakeDecide(bb, 500000, 3300, 799, 420, rc, &ra);                   // a 16 MB dip that recovers: the fall's clock starts again
        BrakeDecide(bb, 600000, 3300, 799, 436, rc, &ra);
        CHECK(BrakeDecide(bb, 1000000, 3300, 799, 436, rc, &ra) == 2870 && ra == BRAKE_NONE && bb.fallMs == 0, "burst: at the proven level after a climb back");
        BrakeDecide(bb, 1001000, 3300, 799, 420, rc, &ra);
        CHECK(BrakeDecide(bb, 1003000, 3300, 799, 400, rc, &ra) == 2742 && ra == BRAKE_STEP && bb.ceiling == 2870 && !(bb.ev & BRAKE_EV_CEIL_LEARNED),
              "burst: a taxi skip's fall steps the budget down and leaves the ceiling (%d, ceiling %d)", bb.t, bb.ceiling);
        int ups = 0; int64_t upAt = -1;
        for (int64_t ms = 1004000; ms <= 1400000; ms += 1000) { BrakeDecide(bb, ms, 3130, 799, 400, rc, &ra); if (ra == BRAKE_RAISE) { ups++; if (upAt < 0) upAt = ms; } }
        CHECK(ups == 1 && upAt == 1303000 && bb.t == 2870 && bb.ceiling == 2870,
              "burst: and it comes back to the proven level 300 s later (%d raise at %lld s, budget %d)", ups, upAt / 1000, bb.t);
        // The same burst within BRAKE_UP_SPAN_MS of a raise is the world growing into it: that teaches the ceiling.
        BrakeState bg = {}; bg.t = 2870; bg.held = true; bg.refL = 436; bg.ceiling = 2870; bg.raised = true; bg.lastUpMs = 900000;
        bg.proofL = 436; bg.quietHiL = 436; bg.xRef = 799;
        BrakeDecide(bg, 1001000, 3300, 799, 420, rc, &ra);
        CHECK(BrakeDecide(bg, 1003000, 3300, 799, 400, rc, &ra) == 2742 && bg.ceiling == 2742 && (bg.ev & BRAKE_EV_CEIL_LEARNED),
              "burst: a fast fall that began 101 s after a raise lowers the ceiling (%d)", bg.ceiling);
        // A raise the world leaves unpressed for 5 minutes (a pause, a long stand) and then grows into fast: that fall is
        // the world growing into the raise, and it teaches the ceiling (the 1.0.4 review, brake-1). Pressed soon after
        // the raise, a fast fall 5 minutes after the press is a burst again.
        BrakeState bu0 = {}; bu0.t = 2742; bu0.held = true; bu0.refL = 436; bu0.ceiling = 2870; bu0.proofL = 436; bu0.quietHiL = 436; bu0.xRef = 799; bu0.lastActMs = 100000;
        BrakeState bu = bu0;
        CHECK(BrakeDecide(bu, 400000, 3150, 799, 436, rc, &ra) == 2870 && ra == BRAKE_RAISE && bu.awaitPress, "burst: a raise to the ceiling (%d)", bu.t);
        BrakeDecide(bu, 701000, 3150, 799, 436, rc, &ra);
        CHECK(bu.awaitPress, "burst: still unpressed 301 s later");
        BrakeDecide(bu, 702000, 3300, 799, 420, rc, &ra);                   // play resumes: the world grows into the raise
        CHECK(BrakeDecide(bu, 704000, 3300, 799, 400, rc, &ra) == 2742 && ra == BRAKE_STEP && bu.ceiling == 2742 && (bu.ev & BRAKE_EV_CEIL_LEARNED),
              "burst: a fast fall into a raise left unpressed for 5 minutes lowers the ceiling (%d)", bu.ceiling);
        bu = bu0;
        BrakeDecide(bu, 400000, 3150, 799, 436, rc, &ra);
        BrakeDecide(bu, 430000, 3300, 799, 436, rc, &ra);
        CHECK(!bu.awaitPress && bu.pressMs == 430000, "burst: the world presses the raise 30 s after it");
        BrakeDecide(bu, 731000, 3300, 799, 420, rc, &ra);
        CHECK(BrakeDecide(bu, 733000, 3300, 799, 400, rc, &ra) == 2742 && ra == BRAKE_STEP && bu.ceiling == 2870 && !(bu.ev & BRAKE_EV_CEIL_LEARNED),
              "burst: a fast fall 301 s after the press is a burst: the ceiling stays (%d)", bu.ceiling);
        // Pauses do not count: BrakeResume moves every window on by the pause, and a window never set stays unset.
        BrakeState pz = {}; pz.lastActMs = 1000; pz.proofMs = 2000; pz.quietMs = 3000; pz.lastUpMs = 0; pz.pressMs = 4000; pz.fallMs = 5000; pz.downMs = 1000;
        g_brakePauseMs = 10000; BrakeResume(pz, 50000);
        CHECK(pz.lastActMs == 41000 && pz.proofMs == 42000 && pz.quietMs == 43000 && pz.lastUpMs == 0 && pz.pressMs == 44000 && pz.fallMs == 45000
              && pz.downMs == 1000 && g_brakePauseMs == 0,
              "up: a 40 s pause moves the windows on by 40 s, leaves the unset ones at 0, and never the step and emergency wait");
        // A raise the world has not pressed yet: a fast fall into it is not a burst, however long after the raise (D3b).
        BrakeState ap = {}; ap.t = 2870; ap.held = true; ap.refL = 436; ap.ceiling = 2870; ap.raised = true; ap.awaitPress = true; ap.lastUpMs = 100000;
        ap.proofL = 436; ap.quietHiL = 436; ap.xRef = 799;
        BrakeDecide(ap, 501000, 2900, 799, 420, rc, &ra);
        CHECK(BrakeDecide(ap, 503000, 2900, 799, 400, rc, &ra) == 2742 && ra == BRAKE_STEP && ap.ceiling == 2742 && (ap.ev & BRAKE_EV_CEIL_LEARNED),
              "burst: a fast fall into a raise not yet pressed lowers the ceiling (%d)", ap.ceiling);

        // The proof: a span restarts when L drops more than 8 MB under its start, and it is taken only on a pressed tick.
        BrakeState ps = { 4000, false, -1, 0 };
        BrakeDecide(ps, 0, 3300, 799, 500, rc, &ra);
        BrakeDecide(ps, 200000, 3290, 799, 484, rc, &ra);
        BrakeDecide(ps, 300000, 3290, 799, 484, rc, &ra);
        CHECK(ra == BRAKE_NONE && ps.ceiling == 0 && ps.t == 2870, "up: a 16 MB drop inside the span starts it again, no proof at 300 s");
        BrakeDecide(ps, 500000, 3000, 799, 484, rc, &ra);
        CHECK(ps.ceiling == 0, "up: no proof on a tick the world does not press");
        BrakeDecide(ps, 510000, 3290, 799, 484, rc, &ra);
        CHECK(ps.ceiling == 2870, "up: proven on the first pressed tick after the span (%d)", ps.ceiling);
        // The hold level did not hold in BRAKEN2 (L 507 at V 3253, then 475 at the same V): only the step's level can prove.
        BrakeState hs = { 4000, false, -1, 0 };
        BrakeDecide(hs, 133000, 3253, 801, 507, rc, &ra);
        int hold = hs.t;
        BrakeDecide(hs, 179000, 3250, 801, 475, rc, &ra);
        CHECK(ra == BRAKE_STEP && hs.t == hold - 128 && hs.ceiling == 0, "up: the hold level loses L at the same V: a step, no ceiling (%d)", hs.t);
        BrakeDecide(hs, 479000, 3250, 801, 475, rc, &ra);
        CHECK(hs.ceiling == hold - 128, "up: the step's level proves itself, not the hold's (%d)", hs.ceiling);

        // The field report replayed with the corrected fall (the one-way brake it ran, X 568): hold at 1,615, steps at L 452
        // and 420, then L rising 16 and 32 MB and falling back to 404 twice. It ends at 1,359 where the field ended at 1,103.
        BrakeCfg fc = { 512, 32, 128, 192, 20, 800, 0, 0 }; BrakeState fs = { 4000, false, -1, 0 };
        static const int kField[][3] = { { 142000, 1962, 484 }, { 168000, 1963, 452 }, { 600000, 1900, 468 }, { 1310000, 1900, 420 },
                                         { 1400000, 1900, 436 }, { 1481000, 1900, 404 }, { 1560000, 1900, 452 }, { 1596000, 1900, 404 } };
        int steps = 0;
        for (int i = 0; i < 8; i++) { BrakeDecide(fs, kField[i][0], kField[i][1], 568, kField[i][2], fc, &ra); steps += ra == BRAKE_STEP; }
        CHECK(fs.t == 1359 && steps == 2, "up: the field replay ends at 1,359 MB after two steps, not 1,103 after four (%d, %d steps)", fs.t, steps);

        // BRAKEN2 replayed second by second through the two-way rule: the hold and the three steps exactly as its own
        // brake took them, and no ceiling and no raise, because L never stood 300 s.
        BrakeState ks = { 4000, false, -1, 0 };
        int kAct[8][2] = {}, kN = 0, kRaise = 0;
        for (size_t i = 0; i < sizeof(kBraken2) / sizeof(kBraken2[0]); i++) {
            int t = BrakeDecide(ks, (int64_t)kBraken2[i][0] * 100, kBraken2[i][1], kBraken2[i][2], kBraken2[i][3], rc, &ra);
            if (ra == BRAKE_RAISE) kRaise++;
            else if (ra != BRAKE_NONE && kN < 8) { kAct[kN][0] = kBraken2[i][0] / 10; kAct[kN][1] = t; kN++; }
        }
        CHECK(kN == 4 && kAct[0][0] == 133 && kAct[0][1] == 2818 && kAct[1][0] == 179 && kAct[1][1] == 2690 && kAct[2][0] == 209 && kAct[2][1] == 2562
              && kAct[3][0] == 558 && kAct[3][1] == 2434 && kRaise == 0 && ks.ceiling == 0,
              "up: BRAKEN2 replayed: %d actions (%d s %d, %d s %d, %d s %d, %d s %d), %d raises, ceiling %d", kN, kAct[0][0], kAct[0][1],
              kAct[1][0], kAct[1][1], kAct[2][0], kAct[2][1], kAct[3][0], kAct[3][1], kRaise, ks.ceiling);

        // The car and ped budgets come back with a raise, never past what the sizing set.
        TrafficBudget keepCar = g_car, keepPed = g_ped; uint32_t keepVeh = g_tVeh;
        g_ped.on = false;
        g_car = { (uintptr_t)&g_tVeh, CAR_STOCK, CAR_FULL, 100000000u, 100000000u, 0, true, "car", CAR_NO_POOL }; g_tVeh = 100000000u;
        TrafficRaise(4000, 800);
        CHECK(g_tVeh == CAR_NO_POOL && g_car.target == CAR_NO_POOL, "up: the car budget comes back only as far as the sizing held it (%u)", g_tVeh);
        g_car.target = g_car.written = g_car.fromOther = 250000000u; g_car.sized = 200000000u; g_tVeh = 250000000u;
        TrafficRaise(4000, 800);
        CHECK(g_tVeh == 250000000u && g_car.target == 250000000u, "up: a larger value another mod set stands through a raise");
        g_car.on = false; g_car.target = 40000000u; g_tVeh = 13333333u;
        TrafficRaise(4000, 800);
        CHECK(g_tVeh == 13333333u, "up: the game's low-memory values are never touched");
        g_car = keepCar; g_ped = keepPed; g_tVeh = keepVeh;
    }
    // ---- the size line names the main heap as the game built it and what is free in it, and only when it can be read
    {
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        memset(g_tHpObj, 0, sizeof(g_tHpObj)); g_tHpObj[20] = 128u << 20; g_tHpObj[38] = 41u << 20;
        g_cs.heapObj = (uintptr_t)g_tHpObj; TSizeSettle(1000, 3000); g_cs.heapObj = 0;
        CHECK(g_sized && strstr(g_sizeNote, "; main heap 128 MB, 41 MB free") != NULL, "size: the main heap on the size line: %s", g_sizeNote);
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); TSizeSettle(1000, 3000);
        CHECK(g_sized && !strstr(g_sizeNote, "main heap"), "size: no main heap note where it cannot be read");
    }
    // ---- the way back up through the worker: the ceiling proven at the hold, emergencies under 2000 MB, the world
    // settling again, nothing on the loading screen, then one raise written whole with the car and ped budgets following
    {
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); TSizeSettle(1000, 3000);
        CHECK(g_sized && TTabAll(4000) && g_brakeCfg.raiseOn && g_brakeCfg.capMb == 4000 && g_car.sized == 200000000u && g_ped.sized == 180000000u,
              "up: sized at 4000 with raises on, capped at the sizing's 4000, car and ped remembered (%u, %u)", g_car.sized, g_ped.sized);
        g_tV = 3300ull << 20;
        SisTick(18000, 500);
        CHECK(TTabAll(2870), "up: the hold through the worker");
        for (int64_t ms = 19000; ms <= 318000; ms += 1000) SisTick(ms, 500);
        CHECK(g_brakeSt.ceiling == 2870 && TTabAll(2870), "up: the hold level proven after 300 s (%d)", g_brakeSt.ceiling);
        static uint8_t tMenuB; uintptr_t keepMenuB = g_cs.menu; g_cs.menu = (uintptr_t)&tMenuB; tMenuB = 1;
        SisTick(330000, 100);
        CHECK(TTabAll(2870) && g_brakeSt.t == 2870, "up: with a menu open the brake decides nothing, not even an emergency");
        tMenuB = 0; g_cs.menu = keepMenuB;
        int64_t ms = 330000;
        // Each emergency on a fresh fall: since 1.0.5's stop (8bb) steps that free nothing stop after two, so the block
        // comes back over the line between them (300 MB, 5.5 s later) and falls to 100 again; the steps keep their 11 s.
        while (g_brakeSt.t >= 2000 && ms < 500000) { SisTick(ms, 100); SisTick(ms + 5500, 300); ms += 11000; }
        int low = g_brakeSt.t;
        CHECK(low == 1974 && TTabAll(1974) && g_tVeh == CarPedFor(CAR_STOCK, CAR_FULL, 1974, TStockFloor(2)) && g_tPed == CarPedFor(PED_STOCK, PED_FULL, 1974, TStockFloor(2)),
              "up: emergencies down to %d, car and ped follow (%u, %u)", low, g_tVeh, g_tPed);
        g_tV = 2480ull << 20;                                 // pressing 1974 (its wall is 2,493 MB)
        for (; ms < 696000; ms += 1000) SisTick(ms, 500);
        CHECK(TTabAll(1974), "up: no raise until 300 s after the last emergency");
        g_tLoad = 1; SisTick(696000, 500);
        CHECK(TTabAll(1974), "up: nothing on the loading screen");
        g_tLoad = 0; SisTick(697000, 500);
        CHECK(TTabAll(2102) && g_brakeSt.t == 2102 && g_tVeh == 200000000u && g_tPed == 180000000u,
              "up: one raise, written whole, and the car and ped budgets come back (%d, car %u, ped %u)", g_brakeSt.t, g_tVeh, g_tPed);
        // 400 s in a menu count for nothing (the 1.0.4 review, brake-2): the world presses the raise at 698 s, and the next
        // raise waits 300 s of play from there, not 300 s of the clock. Counted by the clock it came at 1,100 s.
        g_tV = 2600ull << 20; SisTick(698000, 500); SisTick(699000, 500);
        static uint8_t tMenuU; uintptr_t keepMenuU = g_cs.menu; g_cs.menu = (uintptr_t)&tMenuU; tMenuU = 1;
        for (int64_t m2 = 700000; m2 < 1100000; m2 += 1000) SisTick(m2, 500);
        tMenuU = 0;
        int64_t upAt2 = -1;
        for (int64_t m2 = 1100000; m2 <= 1500000 && upAt2 < 0; m2 += 1000) { SisTick(m2, 500); if (g_brakeSt.t > 2102) upAt2 = m2; }
        CHECK(upAt2 == 1398000 && TTabAll(2230), "up: after 400 s in a menu the next raise waits for 300 s of play (at %lld s)", upAt2 / 1000);
        g_cs.menu = keepMenuU;
        int64_t act0 = g_brakeSt.lastActMs;
        g_tStreamN = 0; SisTick(1400000, 500); SisTick(1401000, 500);          // the world not readable (the streamer down): not play either
        g_tStreamN = 1; SisTick(1402000, 500);
        CHECK(g_brakeSt.lastActMs == act0 + 2000 && g_brakePauseMs == 0, "up: two ticks with the world not readable move the windows on by 2 s");
        // After a load the render targets hold the loading screen's small set while the world builds: not a resolution
        // drop, so the ceiling stays (the 1.0.4 review, the judge's new finding).
        int ceil0 = g_brakeSt.ceiling;
        g_tLoad = 1; SisTick(1500000, 500); SisTick(1501000, 500);
        g_tLoad = 0; g_tRt0 = 63ull << 20;
        for (int i = 0; i < 6; i++) SisTick(1502000 + 1000 * i, 500);
        CHECK(g_brakeSt.ceiling == ceil0 && ceil0 == 2870 && g_brakeSt.xRef == 799 && g_ldEdgeMs != 0,
              "up: six ticks of the loading screen's 63 MB after a load leave the ceiling at %d", g_brakeSt.ceiling);
        g_tRt0 = 799ull << 20; for (int i = 0; i < 5; i++) SisTick(1508000 + 1000 * i, 500);
        // Judged, the set can still hold a stage short of the finished one (the second audit, brake-3): 765 MB for 12 ticks
        // after the player appeared, the verdict after three of them, then 799. Not a resolution drop either.
        g_tLoad = 1; SisTick(1520000, 500); SisTick(1521000, 500);
        g_tLoad = 0; g_tRt0 = 765ull << 20;
        for (int i = 0; i < 12; i++) SisTick(1522000 + 1000 * i, 500);
        CHECK(g_ldVerdict && g_ldEdgeMs == 0 && g_ldSettleMs == 1522000 && g_brakeSt.ceiling == ceil0 && g_brakeSt.xRef == 799 && g_sizedX == 799,
              "up: 765 MB for 12 ticks after a load, judged after three: the ceiling stays %d, the brake and the resize keep 799 (%d, %d)",
              g_brakeSt.ceiling, (int)g_brakeSt.xRef, g_sizedX);
        g_tRt0 = 799ull << 20; SisTick(1534000, 500);
        CHECK(g_ldSettleMs == 0, "up: the finished set back: the load is done with");
        // A smaller set that holds is a resolution after all: from LOAD_CEILING_MS after the edge it is taken as it is.
        g_tLoad = 1; SisTick(1540000, 500); SisTick(1541000, 500);
        g_tLoad = 0; g_tRt0 = 765ull << 20;
        int64_t xAt = -1;
        for (int64_t m4 = 1542000; m4 <= 1542000 + LOAD_CEILING_MS + 10000 && xAt < 0; m4 += 1000) { SisTick(m4, 500); if (g_brakeSt.xRef == 765) xAt = m4; }
        CHECK(xAt == 1542000 + LOAD_CEILING_MS + 2000 && g_sizedX == 765 && g_brakeSt.ceiling == g_brakeSt.t,
              "up: 765 MB held past LOAD_CEILING_MS after the load is a resolution: taken on the third tick after it (%lld s)", xAt / 1000);
        g_tRt0 = 799ull << 20;
    }
    // ---- the game's own memory figures (reports/raw/clamp-research-2026-09-26): taken only where the code around them
    // reads as expected; watched, never written; a clamp logged and followed by the brake, a reset's blip ignored
    {
        static uint64_t fV, fS, fSaved, fDead, fS2, fCv, fCs; static uint8_t fRestore[32], fGate[5];
        auto plant = [&]() {
            const uintptr_t order[4] = { (uintptr_t)&fSaved, (uintptr_t)&fV, (uintptr_t)&fDead, (uintptr_t)&fS };
            for (int i = 0; i < 4; i++) {
                fRestore[8 * i] = i % 2 ? 0x66 : 0xF3; fRestore[8 * i + 1] = 0x0F; fRestore[8 * i + 2] = i % 2 ? 0xD6 : 0x7E; fRestore[8 * i + 3] = 0x05;
                uint32_t a = (uint32_t)order[i]; memcpy(fRestore + 8 * i + 4, &a, 4);
            }
            static const uint8_t g[5] = { 0x3D, 0x7C, 0x01, 0x76, 0x88 }; memcpy(fGate, g, 5);
        };
        uintptr_t wv = 0, ws = 0, wsv = 0;
        auto sites = [&]() { FigSites(wv, ws, wsv, (uintptr_t)fRestore, 0x40E550, (uintptr_t)fGate, 0x4109D2, (uintptr_t)&fV, (uintptr_t)&fS, (uintptr_t)&fSaved, (uintptr_t)&fDead); };
        plant(); sites();
        CHECK(wv == (uintptr_t)&fV && ws == (uintptr_t)&fS && wsv == (uintptr_t)&fSaved && !g_figOff[0], "figures: the stock code around them: watched");
        fRestore[13] ^= 1; sites();
        CHECK(!wv && !ws && !wsv && strstr(g_figOff, "0x40E550"), "figures: one byte off in the reset's restore: not watched, and the size line names it (%s)", g_figOff);
        plant(); fGate[2] = 0x02; sites();
        CHECK(!wv && strstr(g_figOff, "0x4109D2"), "figures: the clamp's gate changed: not watched (%s)", g_figOff);
        plant(); sites();
        auto setFigs = [&](uint64_t vMb, uint64_t sMb) { fV = vMb << 20; fS = sMb << 20; };
        // Sized at 4000 at 4K (X 799) with the figures as the game starts them: video 8192 (SISCO's -availablevidmem), system 32420.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        g_fig = FigWatch{}; fSaved = 8192ull << 20; fDead = 0; setFigs(8192, 32420); fCv = 40ull << 20; fCs = 30ull << 20; fS2 = 700ull << 20;
        g_cs.figVideo = wv; g_cs.figSystem = ws; g_cs.figSaved = wsv; g_cs.cacheS = (uintptr_t)&fS2; g_cs.cachedV = (uintptr_t)&fCv; g_cs.cachedS = (uintptr_t)&fCs;
        TSizeSettle(1000, 3000);
        CHECK(g_sized && TTabAll(4000) && g_fig.accepted && g_fig.video == (8192ull << 20) && g_fig.clamps == 0, "figures: read once at the start, no action (budget 4000)");
        // A device reset: the system figure 0 for a tick, then back. Not a clamp; the brake skips that tick.
        setFigs(8192, 0); g_tV = 3300ull << 20; SisTick(20000, 400);         // with V and L that would hold the budget on any other tick
        CHECK(g_fig.blips == 1 && g_fig.resetTick && g_fig.clamps == 0 && TTabAll(4000) && !g_brakeSt.held && g_brakePauseMs == 20000,
              "figures: a reset's blip (system 0) is not a clamp, and the brake decides nothing on that tick");
        g_tV = 0;
        setFigs(8192, 32420); SisTick(21000, 3000);
        CHECK(!g_fig.resetTick && g_fig.clamps == 0 && !g_fig.pending, "figures: and back, nothing");
        // A value read between the game's two dword stores, one tick only: nothing.
        setFigs(8192, 1340); SisTick(22000, 3000); setFigs(8192, 32420); SisTick(23000, 3000);
        CHECK(g_fig.clamps == 0 && !g_fig.follow && TTabAll(4000), "figures: a one-tick odd value is not a clamp");
        // A managed texture fails: both figures down, on two reads. Logged; the brake follows to video - X on the same tick.
        setFigs(3104, 2000); SisTick(24000, 3000);
        CHECK(g_fig.clamps == 0 && TTabAll(4000), "figures: one read of a lower value waits for a second");
        SisTick(25000, 3000);
        CHECK(g_fig.clamps == 1 && !g_fig.follow && TTabAll(2305) && g_brakeSt.t == 2305 && g_brakeCfg.capMb == 2305 && g_brakeCfg.clampCapMb == 2305
              && g_brakeSt.lastActMs == 25000 && !g_brakeSt.raised,
              "figures: a managed clamp (video 8192 -> 3104, system 32420 -> 2000): the brake follows to 3104 - 799 = %d, raises stop there (%d)", g_brakeSt.t, g_brakeCfg.capMb);
        // A clamp with a loading screen up is logged at once and followed on the first tick with it down.
        g_tLoad = 1; setFigs(3000, 2000); SisTick(26000, 3000); SisTick(27000, 3000);
        CHECK(g_fig.clamps == 2 && g_fig.follow && TTabAll(2305), "figures: a second, lower clamp with a loading screen up: logged, the write waits");
        g_tLoad = 0; SisTick(28000, 3000);
        CHECK(!g_fig.follow && TTabAll(2201) && g_brakeCfg.capMb == 2201, "figures: followed once the loading screen is down (%d)", g_brakeSt.t);
        // The game restores them at a device reset: logged; the cap stays, that level failed once.
        setFigs(8192, 0); SisTick(29000, 3000);
        setFigs(8192, 32420); SisTick(30000, 3000); SisTick(31000, 3000);
        CHECK(g_fig.restores == 1 && g_fig.video == (8192ull << 20) && g_brakeCfg.capMb == 2201 && TTabAll(2201), "figures: the game's restore is logged, the cap stays at %d", g_brakeCfg.capMb);
        // A later clamp that leaves the budget under video - X: no write, the cap only if lower.
        setFigs(3050, 32420); SisTick(32000, 3000); SisTick(33000, 3000);
        CHECK(g_fig.clamps == 3 && TTabAll(2201) && g_brakeCfg.capMb == 2201 && g_brakeCfg.clampCapMb == 2201 && !g_fig.follow, "figures: a clamp above the budget writes nothing (budget %d, cap %d)", g_brakeSt.t, g_brakeCfg.capMb);
        // A system-memory texture fails: the system figure alone. Logged; nothing for the brake to follow.
        setFigs(3050, 1800); SisTick(34000, 3000); SisTick(35000, 3000);
        CHECK(g_fig.clamps == 4 && !g_fig.follow && TTabAll(2201), "figures: a system-only clamp is logged and not followed");
        // Past eight lines only the count moves; the video clamps are still followed.
        for (int i = 0; i < 6; i++) { setFigs(3040 - 10 * i, 1800); SisTick(36000 + 2000 * i, 3000); SisTick(37000 + 2000 * i, 3000); }
        CHECK(g_fig.clamps == 10 && TTabAll(2191) && g_brakeSt.t == 2191 && g_brakeCfg.capMb == 2191,
              "figures: ten clamps counted, the budget follows the lowest figure, 2990 - 799 (%d)", g_brakeSt.t);
        // The pure follow at the floor: video - X under the floor holds at the floor.
        BrakeCfg fc = { 512, 32, 128, 192, 20, 800, 1, 4000 }; BrakeState fb = {}; fb.t = 2000; fb.held = true; int fa = -1;
        CHECK(BrakeFollowClamp(fb, fc, 1000, 1200, 799, 300, false, &fa) == 800 && fa == BRAKE_CLAMP && fc.capMb == 800, "figures: a figure under the floor plus X: the brake stops at the floor (%d)", fb.t);
        // No brake (the budget left at the game's own): lines only.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); SiteSet(S_LINK_SIZE, ST_OFF_DISABLED, "test");
        g_fig = FigWatch{}; setFigs(8192, 32420); TSizeSettle(1000, 3000);
        CHECK(g_sized && !g_brakeOn && memcmp(g_tTab, kCtStockTable, sizeof(g_tTab)) == 0, "figures: with the link pool refused the budget is the game's own and there is no brake");
        setFigs(3104, 2000); SisTick(20000, 3000); SisTick(21000, 3000);
        CHECK(g_fig.clamps == 1 && memcmp(g_tTab, kCtStockTable, sizeof(g_tTab)) == 0, "figures: a clamp there is only logged");
        SiteSet(S_LINK_SIZE, ST_ON, "");
        // A clamp in the first load: the sizing itself stays under video - X, and says why.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        g_fig = FigWatch{}; setFigs(3104, 2000); TSizeSettle(1000, 3000);
        CHECK(g_sized && g_sizedT == 2305 && TTabAll(2305) && g_brakeCfg.capMb == 2305 && strstr(g_sizeNote, "video figure already lowered to 3104 MB"),
              "figures: a clamp before the sizing: budget %d, not 4000: %s", g_sizedT, g_sizeNote);
        // Another mod's larger value in the table, and a clamp in the first load: lowered with the figure, and said so.
        TSizeReset(1, 5843, CAR_STOCK, PED_STOCK); g_tTab[47] = 4000ull << 20;
        g_fig = FigWatch{}; setFigs(3104, 2000); TSizeSettle(1000, 3000);
        CHECK(g_sizedT == 2305 && strstr(g_sizeNote, "a larger table value, lowered to the game's figure") && !strstr(g_sizeNote, "a larger table value kept")
              && !strstr(g_sizeNote, "another mod"),
              "figures: a larger value in the table lowered by a first-load clamp is named as lowered, not blamed on a mod: %s", g_sizeNote);
        // A first-load clamp so low that the game's own budget binds: the note says the game's own budget holds it.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        g_fig = FigWatch{}; setFigs(1200, 2000); TSizeSettle(1000, 3000);
        CHECK(g_sizedT == TStockFloor(2) && strstr(g_sizeNote, "the game's own budget holds"),
              "figures: a first-load clamp under the game's own budget: the budget %d, and the note says the game's own holds it: %s", g_sizedT, g_sizeNote);
        // A clamp during a load is followed against the finished render targets, not the loading screen's set that the
        // first tick after it shows (the 1.0.4 review, clamp-1): 63 MB would have given 3041.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        g_fig = FigWatch{}; setFigs(8192, 32420); TSizeSettle(1000, 3000);
        g_tLoad = 1; setFigs(3104, 2000); SisTick(30000, 3000); SisTick(31000, 3000);
        CHECK(g_fig.follow && TTabAll(4000), "figures: a clamp during a load waits for it");
        g_tLoad = 0; g_tRt0 = 63ull << 20; SisTick(32000, 3000);
        CHECK(!g_fig.follow && TTabAll(2305) && g_brakeCfg.capMb == 2305,
              "figures: followed on the first tick after the load against the finished set: 3104 - 799 = %d (the targets read 63)", g_brakeSt.t);
        g_tRt0 = 799ull << 20;
        // The game restores its figures before the brake can follow (a clamp with the pause menu open, then a VSync switch
        // in the same menu; clamp-2): the cap still comes down to the clamp's level, and nothing is written.
        TSizeReset(1, 5843, CAR_STOCK, PED_STOCK);
        g_fig = FigWatch{}; setFigs(8192, 32420); TSizeSettle(1000, 3000);
        int sized6 = g_sizedT;
        static uint8_t tMenuC; uintptr_t keepMenuC = g_cs.menu; g_cs.menu = (uintptr_t)&tMenuC; tMenuC = 1;
        setFigs(3104, 2000); SisTick(40000, 3000); SisTick(41000, 3000);
        setFigs(8192, 0); SisTick(42000, 3000);
        setFigs(8192, 32420); SisTick(43000, 3000); SisTick(44000, 3000);
        CHECK(g_fig.clamps == 1 && g_fig.restores == 1 && g_fig.follow && g_fig.followV == (3104ull << 20), "figures: clamped and restored with a menu open: the follow waits, the clamp's figure kept");
        tMenuC = 0; SisTick(45000, 3000);
        CHECK(!g_fig.follow && g_brakeCfg.capMb == 2305 && g_brakeSt.t == sized6 && TTabAll(sized6),
              "figures: the menu closed: raises stop at 2305, the budget stays %d (cap %d)", g_brakeSt.t, g_brakeCfg.capMb);
        // A resize to larger render targets never lifts that cap (brake-5): the budget comes down to the card's figure,
        // the cap stays at the clamp's.
        g_tRt0 = 900ull << 20; SisTick(46000, 3000);
        CHECK(g_brakeCfg.capMb == 2305 && g_brakeSt.t < sized6 && g_brakeSt.t > 2305,
              "figures: a resize after the clamp: budget %d -> %d, the cap stays %d", sized6, g_brakeSt.t, g_brakeCfg.capMb);
        g_tRt0 = 799ull << 20; g_cs.menu = keepMenuC;
        // Two clamps before the follow, the higher one after a restore (G1): the follow takes the lowest, the cap its level.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        g_fig = FigWatch{}; setFigs(8192, 32420); TSizeSettle(1000, 3000);
        static uint8_t tMenuG; uintptr_t keepMenuG = g_cs.menu; g_cs.menu = (uintptr_t)&tMenuG; tMenuG = 1;
        setFigs(3104, 2000); SisTick(50000, 3000); SisTick(51000, 3000);
        setFigs(8192, 0); SisTick(52000, 3000); setFigs(8192, 32420); SisTick(53000, 3000); SisTick(54000, 3000);
        setFigs(3500, 2000); SisTick(55000, 3000); SisTick(56000, 3000);
        CHECK(g_fig.clamps == 2 && g_fig.followV == (3104ull << 20), "figures: two clamps with a restore between, the follow waiting: the lowest kept (%llu MB)", g_fig.followV >> 20);
        tMenuG = 0; SisTick(57000, 3000);
        CHECK(!g_fig.follow && g_brakeCfg.capMb == 2305 && g_brakeCfg.clampCapMb == 2305 && g_brakeSt.t == 4000,
              "figures: the menu closed: the cap at the lowest clamp's level (%d), the budget untouched (%d)", g_brakeCfg.capMb, g_brakeSt.t);
        g_cs.menu = keepMenuG;
        // A resolution changed in a menu while held, then a load straight from it with a clamp in the load (the second
        // audit, brake-5): the follow reasons with the set the resize took in the menu, not the brake's from before it.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        g_fig = FigWatch{}; setFigs(8192, 32420); TSizeSettle(1000, 3000);
        g_tV = 3300ull << 20; SisTick(18000, 500); g_tV = 0;
        static uint8_t tMenuR; uintptr_t keepMenuR = g_cs.menu; g_cs.menu = (uintptr_t)&tMenuR; tMenuR = 1;
        g_tRt0 = 252ull << 20; for (int i = 0; i < 4; i++) SisTick(19000 + 1000 * i, 3000);
        CHECK(g_brakeSt.held && g_brakeSt.t == 2870 && g_sizedX == 252 && g_brakeSt.xRef == 799,
              "figures: held at %d, then 1080p chosen in the menu: taken by the resize (%d), not by the brake (%d)", g_brakeSt.t, g_sizedX, (int)g_brakeSt.xRef);
        tMenuR = 0; g_tLoad = 1; setFigs(3104, 2000); SisTick(23000, 3000); SisTick(24000, 3000);
        g_tLoad = 0; g_tRt0 = 63ull << 20; SisTick(25000, 3000);
        CHECK(!g_fig.follow && g_brakeSt.t == 2852 && TTabAll(2852) && g_brakeCfg.capMb == 2852,
              "figures: the clamp in that load followed against 252 MB: 3104 - 252 = %d, not 3104 - 799 (cap %d)", g_brakeSt.t, g_brakeCfg.capMb);
        g_tRt0 = 799ull << 20; g_cs.menu = keepMenuR;
        // The game's own answer low from the start, no clamp (a player's log, FINDINGS 8av: 2048 MB on a 16 GB card with
        // render targets of 1113 MB, though SISCO had asked for the card's figure): the sizing stays under it less the
        // render targets, the car and ped budgets with it, the brake's raises stop there, and both lines say whose
        // figure it is. Until 1.0.5 this sized 4000 and gave cars and peds 380 MB of a world the game held to 935.
        TSizeReset(1, 15209, CAR_STOCK, PED_STOCK); g_tRt0 = 1113ull << 20;
        g_fig = FigWatch{}; fSaved = 2048ull << 20; setFigs(2048, 63092); g_vidmemSetMb = 15209;
        TSizeSettle(1000, 3000);
        CHECK(g_sized && g_sizedT == 935 && TTabAll(935) && g_brakeOn && g_brakeSt.t == 935 && g_brakeCfg.capMb == 935 && g_fig.clamps == 0
              && g_tVeh == CarPedFor(CAR_STOCK, CAR_FULL, 935, TStockFloor(2)) && g_tPed == CarPedFor(PED_STOCK, PED_FULL, 935, TStockFloor(2))
              && g_tVeh > CAR_STOCK && g_tVeh < CAR_FULL / 2
              && strstr(g_sizeNote, "the card allows 11865); the game's own video figure is 2048 MB: the budget stays under it less the render targets")
              && !strstr(g_sizeNote, "failed create"),
              "figures: the game's own answer 2048 MB at 1113 MB of render targets: budget %d, car %u, ped %u: %s", g_sizedT, g_tVeh, g_tPed, g_sizeNote);
        // The same so low that the game's own budget binds: the budget is the game's own, and so are cars and peds.
        TSizeReset(1, 15209, CAR_STOCK, PED_STOCK); g_tRt0 = 1113ull << 20;
        g_fig = FigWatch{}; fSaved = 1500ull << 20; setFigs(1500, 63092); g_vidmemSetMb = 15209;
        TSizeSettle(1000, 3000);
        CHECK(g_sizedT == TStockFloor(2) && g_tVeh == CAR_STOCK && g_tPed == PED_STOCK
              && strstr(g_sizeNote, "the game's own video figure is 1500 MB: the game's own budget holds"),
              "figures: the game's own answer under its own budget plus the render targets: budget %d, cars and peds the game's: %s", g_sizedT, g_sizeNote);
        // The figure SISCO asked for, taken: the full budget, and neither line mentions it.
        TSizeReset(1, 15209, CAR_STOCK, PED_STOCK); g_tRt0 = 1113ull << 20;
        g_fig = FigWatch{}; fSaved = 15209ull << 20; setFigs(15209, 63092); g_vidmemSetMb = 15209;
        TSizeSettle(1000, 3000);
        CHECK(g_sizedT == T_MAX_MB && TTabAll(T_MAX_MB) && g_tVeh == CAR_FULL && g_tPed == PED_FULL && !strstr(g_sizeNote, "video figure"),
              "figures: the figure SISCO set, taken by the game: budget %d, nothing said: %s", g_sizedT, g_sizeNote);
        g_tRt0 = 799ull << 20; fSaved = 8192ull << 20; g_vidmemSetMb = 0;
        g_cs.figVideo = g_cs.figSystem = g_cs.figSaved = 0; g_cs.cacheS = g_cs.cachedV = g_cs.cachedS = 0; g_fig = FigWatch{};
    }
    // ---- the budget rule and the car and ped rule, against a table worked out by hand
    {
        int tv = 0;
        CHECK(OtherMb(373) == 900 && OtherMb(799) == 1100 && OtherMb(100) == 900, "rule: others 900 MB at 1080p, 1,100 at 4K");
        CHECK(SizeBudget(23370, 799, 800, false, &tv) == 4000 && tv > 4000, "rule: a 24 GB card at 4K: 4000 (the card allows %d)", tv);
        CHECK(SizeBudget(5843, 799, 800, false, &tv) == tv && tv >= 3490 && tv <= 3505, "rule: 6 GB at 4K: about 3,500 (%d)", tv);
        CHECK(SizeBudget(3895, 799, 800, false, &tv) == tv && tv >= 1650 && tv <= 1665, "rule: 4 GB at 4K: about 1,660 (%d)", tv);
        CHECK(SizeBudget(3895, 373, 800, false, &tv) == tv && tv >= 2300 && tv <= 2312, "rule: 4 GB at 1080p: about 2,310 (%d)", tv);
        CHECK(SizeBudget(2922, 799, 800, false, &tv) == 800 && tv < 800, "rule: 3 GB at 4K: the game's own 800 (the card allows %d)", tv);
        CHECK(SizeBudget(2922, 373, 800, false, &tv) == tv && tv >= 1380 && tv <= 1392, "rule: 3 GB at 1080p: about 1,390 (%d)", tv);
        CHECK(SizeBudget(1948, 373, 800, false, &tv) == 800, "rule: 2 GB: the game's own (%d)", tv);
        CHECK(SizeBudget(6000, 799, 800, true, &tv) == tv && tv < SizeBudget(6000, 799, 800, false, NULL), "rule: FusionFix's ExtraStreamingMemory: smaller (%d)", tv);
        CHECK(CarPedFor(CAR_STOCK, CAR_FULL, 4000, 800) == 200000000u && CarPedFor(PED_STOCK, PED_FULL, 2000, 800) == 180000000u
              && CarPedFor(CAR_STOCK, CAR_FULL, 800, 800) == CAR_STOCK && CarPedFor(PED_STOCK, PED_FULL, 300, 800) == PED_STOCK
              && CarPedFor(CAR_STOCK, CAR_FULL, 1400, 800) == 120000000u && CarPedFor(PED_STOCK, PED_FULL, 1400, 800) == 115000000u,
              "rule: car 200 MB and ped 180 MB from a 2000 MB budget, the game's own at its stock budget, linear between");
    }
    // ---- once the world is loaded: the sizing (only when the render targets have settled), the car and ped budgets and
    // their guard, and the brake, which takes from the world: the car and ped budgets follow the budget itself
    {
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        g_tStreamN = 0; TSizeSettle(1000, 3000);
        CHECK(!g_sized && memcmp(g_tTab, kCtStockTable, sizeof(g_tTab)) == 0 && g_tVeh == CAR_STOCK, "world: nothing before the world is loaded");
        g_tStreamN = 1; g_tLoad = 1; TSizeSettle(4000, 3000);
        CHECK(!g_sized, "world: nothing while the loading screen is up");
        g_tLoad = 0; TPlayerInWorld(false); TSizeSettle(7000, 3000);
        CHECK(!g_sized, "world: nothing at the main menu, where the streamer is up and the loading screen down but there is no player");
        TPlayerInWorld(true);
        // The sliders are read at sizing and logged: the heavy install ran at 24 and 30 against 99 and 99 on the
        // clean one, and no report compares without them.
        g_tSetView = 24; g_tSetDetail = 30; g_tSetCars = 29;
        // The first render-target stage can hold for a whole save load on a heavy install (23 s on GTAIV-dipo),
        // longer than any number of steady ticks, so under 128 MB nothing is sized against it for 45 s.
        g_tRt0 = 63ull << 20;
        for (int i = 0; i < SETTLE_LONG_TICKS - 1; i++) SisTick(8000 + i * 1000, 3000);
        CHECK(!g_sized, "world: a first stage under 128 MB is not sized against for 45 s, however steady");
        // Below 1080p a FINISHED set is under 128 MB too (about 128 at 1366x768, 112 at 720p), and with a plain
        // minimum those machines were never sized at all. Twice the smallest set seen this run is the finished set.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        g_tRt0 = 12ull << 20; SisTick(8000, 3000); SisTick(9000, 3000);          // the first stage at 720p
        g_tRt0 = 112ull << 20;
        for (int i = 0; i < SETTLE_TICKS + 1; i++) SisTick(10000 + i * 1000, 3000);
        CHECK(g_sized && g_sizedX == 112, "world: a finished 112 MB set at 720p, twice the first stage seen, is sized after the settle (%d)", g_sizedX);
        // Seen only when already finished (a fast machine at 720p): sized once it has held for 45 s.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        g_tRt0 = 112ull << 20;
        for (int i = 0; i < SETTLE_LONG_TICKS; i++) SisTick(8000 + i * 1000, 3000);   // the first tick only records
        CHECK(!g_sized, "world: a 112 MB set with nothing smaller seen waits");
        SisTick(8000 + SETTLE_LONG_TICKS * 1000, 3000);
        CHECK(g_sized && g_sizedX == 112, "world: and is sized once it has held for 45 s (%d)", g_sizedX);
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); TPlayerInWorld(true); g_tSetView = 24; g_tSetDetail = 30; g_tSetCars = 29;
        g_tRt0 = 63ull << 20;
        for (int i = 0; i < SETTLE_TICKS + 4; i++) SisTick(8000 + i * 1000, 3000);
        CHECK(!g_sized, "world: back at the 4K first stage, still not sized against");
        // The heavy install, as measured: the player appears with the render targets still at their first stage and
        // they stay there for three seconds. Two steady ticks sized against 63 MB and slowed the rest of the load
        // four times over. Nothing may be sized here.
        int64_t tick = 11000;
        g_tRt0 = 63ull << 20; for (int i = 0; i < 3; i++) { SisTick(tick, 3000); tick += 1000; }
        CHECK(!g_sized, "world: nothing while the render targets sit at their first stage for three seconds (the heavy install)");
        g_tRt0 = 468ull << 20; SisTick(tick, 3000); tick += 1000;
        g_tRt0 = 799ull << 20; SisTick(tick, 3000); tick += 1000; SisTick(tick, 3000); tick += 1000;
        CHECK(!g_sized, "world: nothing while the render targets are still growing (63 -> 468 -> 799 MB, as in V1P)");
        g_tRt0 = 468ull << 20; SisTick(tick, 3000); tick += 1000;
        CHECK(!g_sized, "world: a change in the render targets starts the wait again");
        for (int i = 0; i < SETTLE_TICKS - 2; i++) { SisTick(tick, 3000); tick += 1000; }
        CHECK(!g_sized, "world: steady for less than SETTLE_TICKS after a change is not enough");
        // The first tick at a new figure only records it, so holding for SETTLE_TICKS takes one tick more.
        g_tRt0 = 799ull << 20; for (int i = 0; i < SETTLE_TICKS + 1; i++) { SisTick(tick, 3000); tick += 1000; }
        CHECK(g_sized && g_sizedX == 799 && TTabAll(4000) && g_sizedT == 4000 && g_brakeOn && g_brakeCfg.floorMb == TStockFloor(2),
              "world: sized once they held for SETTLE_TICKS, at the full size (X %d): %s", g_sizedX, g_sizeNote);
        CHECK(g_tVeh == 200000000u && g_tPed == 180000000u && g_car.on && g_ped.on, "world: car budget 200 MB, ped budget 180 MB (%u, %u)", g_tVeh, g_tPed);
        CHECK(g_setView == 24 && g_setDetail == 30 && g_setCars == 29, "display: the sliders were read at sizing (%u, %u, %u)", g_setView, g_setDetail, g_setCars);
        g_tPed = 250000000u; g_tVeh = 100000000u; SisTick(20000, 3000);
        CHECK(g_ped.target == 250000000u && g_tPed == 250000000u && g_tVeh == 200000000u && g_car.target == 200000000u,
              "guard: FusionFix's larger ped budget kept (%u), a smaller car budget replaced (%u)", g_tPed, g_tVeh);
        g_tV = 3300ull << 20;
        SisTick(18000, 500);
        CHECK(TTabAll(2870) && g_brakeSt.held && g_tPed == 250000000u && g_tVeh == 200000000u, "brake: the hold at the wall (2870), car and ped untouched");
        SisTick(39000, 460);
        CHECK(TTabAll(2742) && g_tPed == 250000000u && g_tVeh == 200000000u, "brake: a step takes from the world, not from the cars or the peds");
        SisTick(60000, 420); SisTick(81000, 380);
        CHECK(TTabAll(2486) && g_tPed == 250000000u && g_tVeh == 200000000u,
              "brake: still nothing off the cars and peds while the budget is over 2000 MB (car %u, ped %u)", g_tVeh, g_tPed);
        int64_t ms = 102000;                                  // an emergency steps every 10 s on a fresh fall (8bb: the block back over the line between)
        while (g_brakeSt.t > 1400 && ms < 400000) { SisTick(ms, 100); SisTick(ms + 5500, 300); ms += 11000; }
        uint32_t wantCar = CarPedFor(CAR_STOCK, CAR_FULL, g_brakeSt.t, TStockFloor(2));
        CHECK(g_tVeh == wantCar && wantCar < 200000000u && wantCar > CAR_STOCK && g_tPed == 250000000u && g_car.target == wantCar,
              "brake: under 2000 MB the car budget follows the world (budget %d MB, car %u MB), the larger ped budget stands",
              g_brakeSt.t, g_tVeh / 1000000);
        int held = g_brakeSt.t;
        g_tLoad = 1; SisTick(ms, 100);
        CHECK(TTabAll(held), "brake: nothing on the loading screen");
        g_tLoad = 0;
        g_bootBudgetMb = 4000; g_tRt0 = 1800ull << 20; SisTick(ms + 1000, 3000);   // render targets big enough to reach the floor
        CHECK(TTabAll(TStockFloor(2)) && g_brakeSt.t == TStockFloor(2) && g_brakeCfg.floorMb <= TStockFloor(2) && g_tVeh == CAR_STOCK && g_tPed == 250000000u
              && g_sizedT == TStockFloor(2) && g_brakeCfg.capMb == TStockFloor(2),
              "world: the render targets grew on a 4 GB card: the budget lowered to the game's own, the cars follow to the game's own, the larger ped budget stands, and no raise may go past it");
        g_tRt0 = 373ull << 20; SisTick(ms + 2000, 3000);
        CHECK(TTabAll(TStockFloor(2)), "world: smaller render targets never raise it");
        g_tVeh = 150000000u; SisTick(ms + 3000, 3000);
        CHECK(g_tVeh == 150000000u && g_car.target == 150000000u && g_car.fromOther == 150000000u,
              "guard: after the brake, a larger car budget from another mod still stands");
        // The resolution back down (2026-09-27, the owner: fix before release): on a card that lowered the budget for the
        // larger set, what the card allows comes back once the smaller set has held three ticks, where the brake has not
        // held; never on a reset's one-tick dip, never on a load's small set, never past a followed clamp's cap.
        TSizeReset(1, 5843, CAR_STOCK, PED_STOCK); TSizeSettle(1000, 3000);
        int s6 = g_sizedT;
        CHECK(s6 > 3000 && g_sizedT0 == s6 && g_stockFloorQ[0] == TStockFloor(0) && g_stockFloorQ[1] == TStockFloor(1) && g_stockFloorQ[2] == TStockFloor(2) && g_floorQ == 2,
              "resize: sized at %d on a 6 GB card, the game's own budgets for all three qualities read before the write (%d, %d, %d)",
              s6, g_stockFloorQ[0], g_stockFloorQ[1], g_stockFloorQ[2]);
        g_tRt0 = 1100ull << 20; SisTick(20000, 3000);
        int big = g_brakeSt.t;
        CHECK(big < s6 && g_sizedT == big && g_brakeCfg.capMb == big && TTabAll(big) && !g_brakeSt.held, "resize: a larger resolution lowers the budget (%d)", big);
        g_tRt0 = 518ull << 20; SisTick(21000, 3000); g_tRt0 = 1100ull << 20; SisTick(22000, 3000);
        CHECK(g_brakeSt.t == big && g_sizedX == 1100, "resize: a reset's one-tick dip gives nothing back");
        g_tRt0 = 799ull << 20; SisTick(23000, 3000); SisTick(24000, 3000);
        CHECK(g_brakeSt.t == big, "resize: two ticks at the old resolution, nothing yet");
        SisTick(25000, 3000);
        CHECK(g_brakeSt.t == s6 && TTabAll(s6) && g_sizedT == s6 && g_brakeCfg.capMb == s6 && g_sizedX == 799,
              "resize: on the third the budget comes back to what the card allows (%d)", g_brakeSt.t);
        g_tLoad = 1; SisTick(30000, 3000); g_tLoad = 0; g_tRt0 = 63ull << 20;
        for (int i = 0; i < 5; i++) SisTick(31000 + 1000 * i, 3000);
        CHECK(g_brakeSt.t == s6 && g_sizedX == 799 && g_shrinkTicks == 0 && g_ldEdgeMs != 0, "resize: a load's small set is not a resolution");
        g_tRt0 = 799ull << 20;
        TSizeReset(1, 5843, CAR_STOCK, PED_STOCK); TSizeSettle(1000, 3000);
        g_tRt0 = 1100ull << 20; SisTick(20000, 3000);
        g_tRt0 = 500ull << 20; for (int i = 0; i < 3; i++) SisTick(21000 + 1000 * i, 3000);   // smaller than at sizing
        CHECK(g_brakeSt.t == s6 && g_sizedT == s6 && g_sizedX == 500, "resize: a smaller resolution than at sizing gives back no more than the sizing's own %d (%d)", s6, g_brakeSt.t);
        g_tRt0 = 799ull << 20;
        TSizeReset(1, 5843, CAR_STOCK, PED_STOCK); TSizeSettle(1000, 3000);
        g_tRt0 = 1100ull << 20; SisTick(20000, 3000);
        g_brakeCfg.clampCapMb = 3300;                                        // a clamp the brake followed, at 3300
        g_tRt0 = 799ull << 20; for (int i = 0; i < 3; i++) SisTick(21000 + 1000 * i, 3000);
        CHECK(g_brakeSt.t == 3300 && g_brakeCfg.capMb == 3300 && g_sizedT == s6 && TTabAll(3300), "resize: given back only as far as a followed clamp's cap (%d)", g_brakeSt.t);
        // Where the brake holds, the address space is the limit: the sizing's figure and the cap come back, the budget stays.
        TSizeReset(1, 5843, CAR_STOCK, PED_STOCK); TSizeSettle(1000, 3000);
        g_tV = 3300ull << 20; SisTick(18000, 500);
        int heldT = g_brakeSt.t;
        g_tRt0 = 1100ull << 20; SisTick(19000, 500);
        g_tRt0 = 799ull << 20; for (int i = 0; i < 3; i++) SisTick(20000 + 1000 * i, 500);
        CHECK(g_brakeSt.held && g_brakeSt.t == heldT && g_sizedT == s6 && TTabAll(heldT), "resize: held, the budget stays %d while the card's figure comes back (%d)", heldT, g_sizedT);
        // Texture quality changed mid-session: the brake's floor follows the game's own budget for it.
        g_tQual = 0; SisTick(30000, 500);
        CHECK(g_floorQ == 0 && g_brakeCfg.floorMb == TStockFloor(0), "quality: low, the floor follows the game's own budget for it (%d)", g_brakeCfg.floorMb);
        g_tQual = 2; SisTick(31000, 500);
        CHECK(g_floorQ == 2 && g_brakeCfg.floorMb == TStockFloor(2), "quality: back to high, the floor too (%d)", g_brakeCfg.floorMb);
        g_brakeSt.t = TStockFloor(1) - 10; g_tQual = 1; SisTick(32000, 500);   // a budget the brake had taken under medium's
        CHECK(g_brakeCfg.floorMb == TStockFloor(1) && g_brakeSt.t == TStockFloor(1) - 10, "quality: a held budget under the new floor stays there (%d)", g_brakeSt.t);
        g_tQual = 2; g_tV = 0;
        // The car and ped budgets come back with a give-back (R10): a larger set that takes the budget under 2000 MB.
        TSizeReset(1, 5843, CAR_STOCK, PED_STOCK); TSizeSettle(1000, 3000);
        g_tRt0 = 2000ull << 20; SisTick(20000, 3000);
        CHECK(g_brakeSt.t < 2000 && g_tVeh < 200000000u, "resize: a much larger set takes the budget under 2000 (%d) and the cars follow (%u)", g_brakeSt.t, g_tVeh);
        g_tRt0 = 799ull << 20; for (int i = 0; i < 3; i++) SisTick(21000 + 1000 * i, 3000);
        CHECK(g_brakeSt.t == s6 && g_tVeh == 200000000u && g_tPed == 180000000u, "resize: the give-back brings the car and ped budgets back too (%u, %u)", g_tVeh, g_tPed);
        // The floor at a resize (R2, R12, and the second audit's brake-4): sized at low quality on a 3 GB card, raised to
        // high, then a resolution round trip: the larger set lowers the budget no further than high's own, as a sizing at
        // high gives (the card's figure there is 354), the floor stays there, and the smaller set gives the card's back.
        TSizeReset(1, 3000, CAR_STOCK, PED_STOCK); g_tQual = 0; TSizeSettle(1000, 3000);
        int s3 = g_sizedT;
        g_tQual = 2; SisTick(20000, 3000);
        int fHigh = g_brakeCfg.floorMb;
        g_tRt0 = 1100ull << 20; SisTick(21000, 3000);
        int fLow = g_brakeCfg.floorMb, bigT = g_brakeSt.t;
        g_tRt0 = 799ull << 20; for (int i = 0; i < 3; i++) SisTick(22000 + 1000 * i, 3000);
        CHECK(s3 > TStockFloor(2) && fHigh == TStockFloor(2) && bigT == TStockFloor(2) && fLow == fHigh && g_brakeCfg.floorMb == fHigh && g_brakeSt.t == s3,
              "resize: after a raise to high a larger set stops at high's own (%d, floor %d), and the budget comes back to %d", bigT, fLow, g_brakeSt.t);
        // Held, a raise to high leaves the budget and the floor where the card allowed at low (354); a smaller set then gives
        // back the card's figure at high's own, as a sizing at high gives, and the floor with it.
        TSizeReset(1, 3000, CAR_STOCK, PED_STOCK); g_tQual = 0; TSizeSettle(1000, 3000);
        g_tRt0 = 1100ull << 20; SisTick(20000, 3000);
        int lowBig = g_brakeSt.t;
        g_tV = 1400ull << 20; SisTick(21000, 500);
        g_tQual = 2; SisTick(22000, 500);
        CHECK(lowBig == SizeBudget(3000, 1100, TStockFloor(0), false, NULL) && lowBig < TStockFloor(2) && g_brakeSt.held && g_brakeSt.t == lowBig
              && g_brakeCfg.floorMb == lowBig && TTabAll(lowBig),
              "quality: held, raised to high: the budget stays %d and the floor stops there (%d)", g_brakeSt.t, g_brakeCfg.floorMb);
        g_tRt0 = 1000ull << 20; for (int i = 0; i < 3; i++) SisTick(23000 + 1000 * i, 500);
        CHECK(g_brakeSt.t == lowBig && g_sizedT == TStockFloor(2) && g_brakeCfg.floorMb == TStockFloor(2) && TTabAll(lowBig),
              "resize: held at high, a smaller set: the card's figure back at high's own (%d), the floor with it (%d), the budget held", g_sizedT, g_brakeCfg.floorMb);
        g_tV = 0; g_tRt0 = 799ull << 20;
        // A raise to high on a small card sized at low (the second audit, brake-4): the budget under high's own comes up to
        // it where the brake has not held, as a sizing at high gives, with the car budget a larger set had taken.
        TSizeReset(1, 1948, CAR_STOCK, PED_STOCK); g_tQual = 0; g_tRt0 = 373ull << 20; TSizeSettle(1000, 3000);
        int sLow = g_sizedT;
        g_tRt0 = 1100ull << 20; SisTick(20000, 3000);
        CHECK(sLow == SizeBudget(1948, 373, TStockFloor(0), false, NULL) && sLow < TStockFloor(2) && g_brakeSt.t == TStockFloor(0) && g_tVeh < g_car.sized,
              "quality: a 2 GB card sized at low (%d); a larger set takes it to low's own (%d) and the cars follow (%u)", sLow, g_brakeSt.t, g_tVeh);
        g_tQual = 2; SisTick(21000, 3000);
        CHECK(TTabAll(TStockFloor(2)) && g_brakeSt.t == TStockFloor(2) && g_sizedT == TStockFloor(2) && g_sizedT0 == TStockFloor(2)
              && g_brakeCfg.capMb == TStockFloor(2) && g_brakeCfg.floorMb == TStockFloor(2) && g_tVeh == g_car.sized,
              "quality: raised to high, not held: the budget up to high's own (%d), the cap and the sizing's figure with it, the cars back (%u)", g_brakeSt.t, g_tVeh);
        g_tQual = 0; SisTick(22000, 3000);
        CHECK(g_brakeSt.t == TStockFloor(2) && TTabAll(TStockFloor(2)) && g_brakeCfg.floorMb == TStockFloor(0),
              "quality: back to low, only the floor moves (budget %d, floor %d)", g_brakeSt.t, g_brakeCfg.floorMb);
        g_tQual = 2; g_tRt0 = 799ull << 20;
        // With the loading flag up, a small set is the loading screen's too (the second audit, brake-1): no give-back, and
        // the finished set stays the one the brake and the clamp follow reason with.
        TSizeReset(1, 5843, CAR_STOCK, PED_STOCK); TSizeSettle(1000, 3000);
        g_tRt0 = 1100ull << 20; SisTick(20000, 3000);
        int big2 = g_brakeSt.t;
        g_tLoad = 1; g_tRt0 = 63ull << 20; for (int i = 0; i < 4; i++) SisTick(21000 + 1000 * i, 3000);
        CHECK(g_brakeSt.t == big2 && g_sizedX == 1100 && TTabAll(big2), "resize: four ticks of a loading screen's 63 MB with the flag up give nothing back (%d, X %d)", g_brakeSt.t, g_sizedX);
        g_tLoad = 0; g_tRt0 = 799ull << 20;
        // A resolution drop on a card that never lowered the budget gives nothing back and says nothing (R7).
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); TSizeSettle(1000, 3000);
        g_tRt0 = 900ull << 20; SisTick(20000, 3000);
        g_tRt0 = 799ull << 20; for (int i = 0; i < 4; i++) SisTick(21000 + 1000 * i, 3000);
        CHECK(g_brakeSt.t == 4000 && g_sizedT == 4000 && g_sizedX == 799, "resize: a large card, a smaller set: nothing to give back, only the set recorded (%d)", g_sizedX);
        g_tRt0 = 799ull << 20;
        // A card that allows less than the game's own: the size line says the budget is the game's own, with no arrow.
        TSizeReset(1, 1948, CAR_STOCK, PED_STOCK); g_tQual = 0; TSizeSettle(1000, 3000);
        char ownB[64]; _snprintf_s(ownB, sizeof(ownB), _TRUNCATE, "budget %d MB, the game's own (card 1948 MB", TStockFloor(0));
        CHECK(g_sizedT == TStockFloor(0) && strstr(g_sizeNote, ownB) && !strstr(g_sizeNote, "budget " "300 ->"), "size: a small card at the game's own: %s", g_sizeNote);
        g_tQual = 2;
        // A loading screen never delays an emergency (the second audit, brake-2): a hold at 18 s, a loading screen from
        // 19 to 48 s, then L under the emergency line: the emergency comes on the first tick after it, as in 1.0.3.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); TSizeSettle(1000, 3000);
        g_tV = 3300ull << 20; SisTick(18000, 500);
        int h18 = g_brakeSt.t;
        g_tLoad = 1; for (int64_t m3 = 19000; m3 <= 48000; m3 += 1000) SisTick(m3, 500);
        g_tLoad = 0; SisTick(49000, 150);
        CHECK(g_brakeSt.held && g_brakeSt.t == h18 - 128 && g_brakeSt.downMs == 49000, "brake: the emergency right after a loading screen, not 10 s later (%d -> %d)", h18, g_brakeSt.t);
        // And the loading screen still counts as no play for the raise's windows (rest-4): the hold's 300 s wait moved on.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); TSizeSettle(1000, 3000);
        g_tV = 3300ull << 20; SisTick(18000, 500);
        g_tLoad = 1; for (int64_t m3 = 19000; m3 <= 48000; m3 += 1000) SisTick(m3, 500);
        g_tLoad = 0; SisTick(49000, 500);
        CHECK(g_brakeSt.lastActMs == 48000 && g_brakeSt.downMs == 18000, "brake: 30 s of loading screen move the raise's clock on by 30 s (%lld), not the emergency's (%lld)",
              g_brakeSt.lastActMs, g_brakeSt.downMs);
        g_tV = 0;
        TSizeReset(0, 23370, CAR_STOCK, PED_STOCK); TSizeSettle(1000, 3000);
        CHECK(g_sized && memcmp(g_tTab, kCtStockTable, sizeof(g_tTab)) == 0 && g_tVeh == CAR_STOCK && g_tPed == PED_STOCK && !g_brakeOn && strstr(g_sizeNote, "native"),
              "world: native Direct3D 9: the table, the car and ped budgets untouched, no brake");
        TSizeReset(1, 23370, CAR_STOCK / 3, PED_STOCK / 3); TSizeSettle(1000, 3000);
        CHECK(TTabAll(4000) && g_tVeh == CAR_STOCK / 3 && g_tPed == PED_STOCK / 3 && !g_car.on, "world: the game's low-memory car and ped budgets stay");
        CHECK(strstr(g_sizeNote, "; car 13 MB, ped 16 MB (the game's low-memory values kept)") != NULL,
              "world: values SISCO left alone read without an arrow: %s", g_sizeNote);
        TSizeReset(1, 6000, CAR_STOCK, PED_STOCK); g_tGafm[0] = 0xE9; TSizeSettle(1000, 3000);
        CHECK(g_sizedT == SizeBudget(6000, 799, 800, true, NULL) && TTabAll(g_sizedT) && strstr(g_sizeNote, "ExtraStreamingMemory")
              && !g_brakeCfg.raiseOn && strstr(g_sizeNote, "brake lowers only (ExtraStreamingMemory)"),
              "world: FusionFix's ExtraStreamingMemory: sized for its multiplier, and the brake only lowers (%d): %s", g_sizedT, g_sizeNote);
        TSizeReset(1, 6000, CAR_STOCK, PED_STOCK); g_tTab[47] = 4000ull << 20; TSizeSettle(1000, 3000);
        CHECK(TTabAll(4000) && strstr(g_sizeNote, "a larger table value kept"), "world: a larger value already in the table stands");
        TSizeReset(1, 23370, 250000000u, PED_STOCK); TSizeSettle(1000, 3000);
        CHECK(g_tVeh == 250000000u && g_car.target == 250000000u && g_tPed == 180000000u && strstr(g_sizeNote, "another mod's larger value kept"),
              "world: FusionFix's larger car budget, there before the world loaded, stands");
        CHECK(strstr(g_sizeNote, "; car 250 MB, ped 50 -> 180 MB (another mod's larger value kept)") != NULL,
              "world: the size line gives the ped budget's own figure beside SISCO's, and the car budget another mod set as it is: %s", g_sizeNote);
        // The value another mod set before the world loaded is its value, so the brake may not take it either. Before
        // this was recorded at sizing, the guard's floor was zero and a brake step cut it to the game's own.
        TSizeReset(1, 23370, 250000000u, PED_STOCK); TSizeSettle(1000, 3000);
        g_tV = 3300ull << 20;                                 // the brake needs a video-memory figure to hold against
        int64_t bms = 100000;
        while (g_brakeSt.t > g_sizedTStock && bms < 900000) { SisTick(bms, 100); SisTick(bms + 5500, 300); bms += 11000; }
        CHECK(g_brakeSt.t == g_sizedTStock && g_tVeh == 250000000u && g_car.target == 250000000u,
              "brake: at the floor, a car budget another mod set before the sizing still stands (%u MB, budget %d)",
              g_tVeh / 1000000, g_brakeSt.t);

        // The moment the player stands in the world is logged once, before and apart from any sizing, so every
        // log carries the load moment and shows the worker was looking even when nothing is ever sized.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); TPlayerInWorld(false);
        SisTick(1000, 3000); SisTick(2000, 3000);
        CHECK(!g_worldLogged, "world line: not while the player is not in the world");
        TPlayerInWorld(true); g_tRt0 = 63ull << 20;      // the first render-target stage: nothing is sized, but the player is there
        SisTick(3000, 3000);
        CHECK(g_worldLogged && !g_sized, "world line: written the first tick the player stands in the world, before any sizing");
        g_tLoad = 1; g_worldLogged = false; SisTick(4000, 3000);
        CHECK(!g_worldLogged, "world line: not while the loading screen is up");
        g_tLoad = 0;

        // What the game built is read back at sizing, because a raise SISCO made can be undone after it with
        // nothing to see at the write: FusionFix sets the link pool to 20,000 and IVTweaker's MaxGameHeap the
        // arena, each without looking, and IVTweaker's MaxVehicleStruct hooks the constructor call and passes its
        // own count. Everything as left: the line names the four figures and the budget is raised.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); TSizeSettle(1000, 3000);
        CHECK(g_sized && g_sizedT > TStockFloor(2) && strstr(g_builtNote, "drawable slots 32768, links 65536, arena 409600 KB, VehicleStruct pool 100"),
              "built: read back as left, the budget raised: %s", g_builtNote);
        // The link pool set back to 20,000 by a plugin that loaded after SISCO: marked, and the budget stays.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        { uint32_t li = 20000; memcpy(g_tSzLink + 1, &li, 4); }
        TSizeSettle(1000, 3000);
        CHECK(g_sized && g_sizedT == TStockFloor(2) && g_site[S_LINK_SIZE].state == ST_OFF_MISMATCH && strstr(g_site[S_LINK_SIZE].note, "read back 20000")
              && strstr(g_sizeNote, "the link pool was not raised"),
              "built: the link pool read back at 20,000: the budget stays the game's own: %s", g_site[S_LINK_SIZE].note);
        // The VehicleStruct pool built with 50 although the site says 100: the constructor call is hooked. Marked,
        // the car budget held where 50 slots can use it, the budget itself untouched.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        g_tSzVsPoolObj[2] = 50; g_tSzVs[4] = 0xE9;
        TSizeSettle(1000, 3000);
        CHECK(g_sized && g_sizedT > TStockFloor(2) && g_site[S_VSTRUCT].state == ST_OFF_MISMATCH && strstr(g_site[S_VSTRUCT].note, "hooked")
              && g_carHeld && g_tVeh == CAR_NO_POOL,
              "built: VehicleStruct built with 50 behind a hooked constructor call: car held at %u MB: %s", g_tVeh / 1000000, g_site[S_VSTRUCT].note);
        // Built with 120: another plugin raised it further, which stands.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        g_tSzVsPoolObj[2] = 120;
        TSizeSettle(1000, 3000);
        CHECK(g_sized && g_site[S_VSTRUCT].state == ST_ON && !g_carHeld && g_tVeh == 200000000u && strstr(g_builtNote, "VehicleStruct pool 120"),
              "built: a pool built larger than SISCO asked stands: %s", g_builtNote);
        // The load line: the build is timed from the player appearing (the edge: the loading flag down, the player in
        // the world) to the last change of the render targets, once they have held LOAD_STEADY_TICKS.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        g_tRt0 = 63ull << 20; SisTick(1000, 3000);                                        // the edge, at the first stage
        CHECK(g_worldLogged && g_worldMs == 1000 && g_loadN == 1 && !g_ldVerdict, "load: the edge is the first tick the player stands in the world with the flag down");
        g_tRt0 = 799ull << 20; SisTick(5000, 3000);                                      // the last change 4 s later
        SisTick(6000, 3000); SisTick(7000, 3000);
        CHECK(!g_ldVerdict, "load: no verdict until the targets have held %d ticks", LOAD_STEADY_TICKS);
        SisTick(8000, 3000);
        CHECK(g_ldVerdict && g_loadBuildS == 4, "load: the world built in 4 s, the settle after it not counted (%lld s)", g_loadBuildS);
        // Already final at the edge: a build of 0 s.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        g_tRt0 = 799ull << 20; SisTick(1000, 3000);
        for (int i = 2; i <= 4; i++) SisTick(i * 1000, 3000);
        CHECK(g_ldVerdict && g_loadBuildS == 0, "load: targets final at the edge: a 0 s build (%lld s)", g_loadBuildS);
        // A first stage under 128 MB is not the build's end however long it holds (the heavy install held it 23 s), so
        // the verdict waits for the stage after it.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        g_tRt0 = 63ull << 20; SisTick(1000, 3000);
        for (int i = 2; i <= 21; i++) SisTick(i * 1000, 3000);                            // the first stage holds 20 s
        CHECK(!g_ldVerdict && g_presentSkip == 0, "load: no verdict while the first stage holds (and nothing skipped without the hook)");
        g_tRt0 = 799ull << 20; SisTick(22000, 3000);
        for (int i = 23; i <= 25; i++) SisTick(i * 1000, 3000);
        CHECK(g_ldVerdict && g_loadBuildS == 21, "load: a 21 s build is timed to the stage after the first (%lld s)", g_loadBuildS);
        // The render targets never hold: the verdict comes at the ceiling and says so.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        g_tRt0 = 63ull << 20; SisTick(1000, 3000);
        for (int i = 2; i <= 91; i++) { g_tRt0 = (uint64_t)(100 + i) << 20; SisTick(i * 1000, 3000); }
        CHECK(g_ldVerdict && g_loadBuildS == 90, "load: targets that never hold: the verdict at the ceiling (%lld s)", g_loadBuildS);
        // Every load is timed, not the first alone: a save switch (the flag up, then the player again) has its own edge.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        g_tRt0 = 799ull << 20; SisTick(1000, 3000); for (int i = 2; i <= 4; i++) SisTick(i * 1000, 3000);
        CHECK(g_ldVerdict && g_loadN == 1, "load: the first load timed");
        g_tLoad = 1; SisTick(20000, 3000); SisTick(21000, 3000);
        g_tLoad = 0; g_tRt0 = 63ull << 20; SisTick(22000, 3000);
        CHECK(g_loadN == 2 && g_worldMs == 22000 && !g_ldVerdict, "load: a save switch is a new load with its own edge");
        g_tRt0 = 799ull << 20; SisTick(44000, 3000); for (int i = 45; i <= 47; i++) SisTick(i * 1000, 3000);
        CHECK(g_ldVerdict && g_loadBuildS == 22, "load: and it is timed on its own (%lld s)", g_loadBuildS);
        // The taxi throttle's arming through the worker's own tick: nothing on the splash and loading screens before the
        // first world, armed once the player is in and the count has moved on two ticks in a row; never on a still count.
        {
            static uint32_t wf = 900; uintptr_t keepF = g_cs.frame, keepM = g_cs.menu; g_cs.frame = (uintptr_t)&wf; g_cs.menu = 0;
            TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); g_rpArmed = false; g_flSampled = false; g_flDisarmed = false; g_flStuck = 0; g_flMoved = 0;
            TPlayerInWorld(false); SisTick(1000, 3000); wf += 60; SisTick(2000, 3000);   // a splash: the count moves, nobody in the world
            g_tLoad = 1; wf += 60; SisTick(3000, 3000); wf += 60; SisTick(4000, 3000);
            CHECK(!g_rpArmed && !g_worldLogged, "present: the splash and the first loading screen do not arm the taxi throttle");
            g_tLoad = 0; TPlayerInWorld(true); g_tRt0 = 63ull << 20; wf += 60; SisTick(5000, 3000);
            CHECK(!g_rpArmed && g_worldLogged, "present: the player appeared with the count moving: one tick is not enough yet");
            wf += 60; SisTick(6000, 3000);
            CHECK(g_rpArmed, "present: the second tick in a row: the worker arms it");
            // A second world edge with the count held still: the edge itself arms nothing (the second audit, rest-2).
            g_rpArmed = false; g_flMoved = 0;
            g_tLoad = 1; SisTick(20000, 3000); g_tLoad = 0; SisTick(21000, 3000); SisTick(22000, 3000); SisTick(23000, 3000);
            CHECK(!g_rpArmed, "present: a world edge with a still count arms nothing");
            g_cs.frame = keepF; g_cs.menu = keepM; g_rpArmed = false; g_flSampled = false;
        }
        // The presents from the worker's side, with the hook in: skipped at 33 ms while a loading screen is up, at 100 ms
        // from the edge until the finished set has held a tick, not at the menu; a new load's flag starts over.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); g_swapVtblHooked = g_tSwapVtbl;
        g_tLoad = 1; TPlayerInWorld(false); SisTick(1000, 3000); SisTick(2000, 3000);
        CHECK(g_presentSkip == 1 && g_showMs == PRESENT_SHOW_LOADING_MS && !g_worldLogged && g_ldUpMs == 1000,
              "present: while a loading screen is up most presents are not made, one shown every 33 ms; the screen's start is kept");
        g_tLoad = 0; SisTick(3000, 3000);
        CHECK(g_presentSkip == 1 && !g_worldLogged, "present: the flag dropped with nobody in the world yet: one tick of grace, the player can follow");
        SisTick(4000, 3000);
        CHECK(g_presentSkip == 0 && !g_worldLogged, "present: two ticks with nobody in the world (the menu): presents as the game makes them");
        TPlayerInWorld(true); g_tRt0 = 63ull << 20; SisTick(5000, 3000);
        CHECK(g_presentSkip == 1 && g_showMs == PRESENT_SHOW_MS && g_worldLogged && g_skippedPresents == 0 && g_ldEdgeMs == 5000,
              "present: the player appeared: skipping at 100 ms from this tick, the counts reset since no loading screen was being skipped");
        g_tRt0 = 468ull << 20; SisTick(6000, 3000);
        g_tRt0 = 799ull << 20; SisTick(7000, 3000);
        CHECK(g_presentSkip == 1, "present: still skipping on the tick the finished set appears");
        SisTick(8000, 3000);
        CHECK(g_presentSkip == 0 && g_ldEdgeMs != 0, "present: skipping ends once the finished set has held a tick; the load is still being judged");
        SisTick(9000, 3000); SisTick(10000, 3000);
        CHECK(g_ldVerdict && g_ldEdgeMs == 0, "present: the world built and judged three ticks after the last change, and the load is done with");
        // A save switch: skipped through its loading screen at 33 ms and on from its edge at 100, the counts kept
        // across the two; the game window's background seconds counted on both.
        g_tLoad = 1; SisTick(20000, 3000);
        CHECK(g_presentSkip == 1 && g_showMs == PRESENT_SHOW_LOADING_MS && g_skippedPresents == 0, "present: a save switch's loading screen is skipped (the count starts at its rise)");
        g_tLoad = 0; g_tRt0 = 63ull << 20; InterlockedExchange(&g_skippedPresents, 7); SisTick(21000, 3000);
        CHECK(g_presentSkip == 1 && g_showMs == PRESENT_SHOW_MS && g_skippedPresents == 7, "present: and on from its edge, the count kept");
        for (int i = 22; i <= 30; i++) SisTick(i * 1000, 3000);                              // the first stage holds: 9 steady ticks
        CHECK(g_presentSkip == 1 && !g_ldVerdict, "present: the first stage holding 9 ticks: still skipping");
        for (int i = 31; i <= 36; i++) SisTick(i * 1000, 3000);                              // 15 s: the stage still holds
        CHECK(g_presentSkip == 0 && !g_ldVerdict, "present: at %d steady ticks the skip ends whatever the set (a build this slow is not paced by presents)", SKIP_MAX_STEADY);
        g_tRt0 = 799ull << 20; SisTick(37000, 3000); SisTick(38000, 3000); SisTick(39000, 3000);
        CHECK(g_presentSkip == 0 && !g_ldVerdict, "present: the targets changed at 16 s: the skip stays ended while they settle");
        SisTick(40000, 3000);
        CHECK(g_ldVerdict && g_loadBuildS == 16 && g_ldEdgeMs == 0, "present: judged once they have held 3 ticks (a 16 s build: %lld s)", g_loadBuildS);
        // A new load's flag going up mid-build: the timed load is dropped and its loading screen skipped at its own rate.
        g_tLoad = 1; SisTick(41000, 3000); g_tLoad = 0; g_tRt0 = 63ull << 20; SisTick(42000, 3000);
        g_tLoad = 1; SisTick(43000, 3000);
        CHECK(g_presentSkip == 1 && g_showMs == PRESENT_SHOW_LOADING_MS && g_ldEdgeMs == 0, "present: a new load's flag mid-build: the timed load dropped, its loading screen skipped");
        g_tLoad = 0;
        // A set that never reads as finished (final at the edge, no first stage seen, under 128 MB): the skip ends at
        // SKIP_MAX_STEADY ticks past the last change, not at the 45 s the sizing waits.
        g_tLoad = 0; TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); g_swapVtblHooked = g_tSwapVtbl;
        g_tRt0 = 112ull << 20; SisTick(1000, 3000);
        for (int i = 2; i <= 10; i++) SisTick(i * 1000, 3000);
        CHECK(g_presentSkip == 1, "present: a 112 MB set with nothing smaller seen: still skipping at 9 steady ticks");
        SisTick(11000, 3000);
        CHECK(g_presentSkip == 0, "present: and no longer at %d", SKIP_MAX_STEADY);
        // A middle stage at the edge (156 MB at 4K, run S2): a first-seen set of 128 MB or more needs two held ticks,
        // and a larger set after it one, so the skip outlasts the build's last stage.
        g_tLoad = 0; TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); g_swapVtblHooked = g_tSwapVtbl;
        g_tRt0 = 156ull << 20; SisTick(1000, 3000); SisTick(2000, 3000);
        CHECK(g_presentSkip == 1, "present: a 156 MB set first seen and held one tick: still skipping");
        g_tRt0 = 770ull << 20; SisTick(3000, 3000);
        CHECK(g_presentSkip == 1, "present: the finished set appeared: skipping until it has held a tick");
        SisTick(4000, 3000);
        CHECK(g_presentSkip == 0, "present: it held: presents go out again");
        g_tLoad = 0; TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); g_swapVtblHooked = g_tSwapVtbl;
        g_tRt0 = 797ull << 20; SisTick(1000, 3000); SisTick(2000, 3000);
        CHECK(g_presentSkip == 1, "present: a finished set already at the edge (a fast machine): held one tick, still skipping");
        SisTick(3000, 3000);
        CHECK(g_presentSkip == 0, "present: two ticks: presents go out again");
        // The worker's side of the end: set at the edge from the set seen there, off on a loading screen, off where the
        // world's set already existed (a save switch), and the hook's end logged on the next tick.
        g_tLoad = 0; TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); g_swapVtblHooked = g_tSwapVtbl;
        g_tLoad = 1; TPlayerInWorld(false); SisTick(1000, 3000);
        CHECK(g_skipRtEnd == 0, "present: no render-target end on a loading screen");
        g_tLoad = 0; TPlayerInWorld(true); g_tRt0 = 63ull << 20; SisTick(2000, 3000);
        CHECK(g_presentSkip == 1 && g_skipRtEnd == 126, "present: at the edge with the 63 MB set the world's is anything from 126 MB (%ld)", g_skipRtEnd);
        InterlockedExchange(&g_presentSkip, 0); InterlockedExchange(&g_skipEndedRt, 765); g_tRt0 = 765ull << 20; SisTick(3000, 3000);
        CHECK(g_skipEndedRt == 0 && g_presentSkip == 0, "present: the hook's end is logged on the next tick and not undone");
        g_tLoad = 1; SisTick(20000, 3000); g_tLoad = 0; SisTick(21000, 3000);
        CHECK(g_presentSkip == 1 && g_skipRtEnd == 0, "present: a save switch's edge with the world's 765 MB set already there: the worker's rule, not the hook's");
        g_tLoad = 0; TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); g_swapVtblHooked = g_tSwapVtbl; g_tRt0 = 156ull << 20; SisTick(1000, 3000);
        CHECK(g_skipRtEnd == 312, "present: the 156 MB set left by the menu: the world's is anything from 312 MB (%ld)", g_skipRtEnd);
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); g_swapVtblHooked = g_tSwapVtbl; g_tRt0 = 5ull << 20; SisTick(1000, 3000);
        CHECK(g_skipRtEnd == RT_FINISHED_MIN_MB, "present: a Deck's 5 MB set: the world's is anything from %d MB (%ld)", RT_FINISHED_MIN_MB, g_skipRtEnd);
        g_swapVtblHooked = NULL; g_tLoad = 0; g_presentSkip = 0; g_skipRtEnd = 0;

        // A render-target set under 24 MB is never a finished one, however long it holds (the Deck's first stage is 5 MB
        // and held past 45 s): not sized against, no verdict; the finished set after it is sized as usual.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        g_tRt0 = 5ull << 20; SisTick(1000, 3000);
        for (int i = 2; i <= 60; i++) SisTick(i * 1000, 3000);
        CHECK(!g_sized && !g_ldVerdict && g_rtMin == 5, "floor: 5 MB of render targets held 60 s: not sized, no verdict, and recorded as the smallest set (so 110 is finished by the 2x rule)");
        g_tRt0 = 110ull << 20;
        for (int i = 61; i <= 60 + SETTLE_TICKS + 1; i++) SisTick(i * 1000, 3000);
        CHECK(g_sized && g_sizedX == 110 && g_ldVerdict && g_loadBuildS == 60, "floor: the 110 MB set after it is sized after the settle, and the build timed to it (%d, %lld s)", g_sizedX, g_loadBuildS);
        // Wine: the sizing's answer is capped at 2000, the brake's lines follow the largest free block at sizing, the
        // layout is on the size line; a larger value another mod put in the table still stands above the cap.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); g_tWine = 1;
        g_tAddrSpace.largest = 269ull << 20; g_tAddrSpace.total = 1400ull << 20; g_tAddrSpace.blocks = 17;
        TSizeSettle(1000, 3000);
        CHECK(g_sized && g_sizedT == WINE_BUDGET_MAX_MB && TTabAll(WINE_BUDGET_MAX_MB) && g_brakeOn && g_brakeCfg.holdMb == 134 && g_brakeCfg.emergencyMb == 67
              && strstr(g_sizeNote, "capped at 2000 on Wine (fragmented address space)") && strstr(g_sizeNote, "address space 1400 MB free in 17 blocks, the largest 269 MB")
              && !g_brakeCfg.raiseOn && strstr(g_sizeNote, "brake lowers only (Wine, lines lowered)"),
              "wine: 4000 becomes 2000, the brake holds at 134 and panics at 67 and only lowers, the layout is logged: %s", g_sizeNote);
        BrakeCfg wc = g_brakeCfg; BrakeState ws = { 2000, false, -1, 0 }; int wa = -1;
        CHECK(BrakeDecide(ws, 1000, 300, 110, 269, wc, &wa) == 2000 && wa == BRAKE_NONE, "wine: the Deck's 269 MB at sizing is not a hold there");
        CHECK(BrakeDecide(ws, 2000, 300, 110, 130, wc, &wa) == TStockFloor(2) && wa == BRAKE_HOLD, "wine: a fall to half of it is, to the floor (%d)", ws.t);
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); g_tWine = 1; g_tAddrSpace.largest = 269ull << 20; g_tAddrSpace.total = 1400ull << 20; g_tAddrSpace.blocks = 17; g_tTab[47] = 3000ull << 20; TSizeSettle(1000, 3000);
        CHECK(g_sized && g_sizedT == 3000, "wine: another mod's 3000 in the table stands above the cap (%d)", g_sizedT);
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); g_tWine = 1; g_tAddrSpace.largest = 2500ull << 20; g_tAddrSpace.total = 2600ull << 20; g_tAddrSpace.blocks = 2;
        TSizeSettle(1000, 3000);
        CHECK(g_sized && g_sizedT == 4000 && g_brakeCfg.holdMb == 512 && g_brakeCfg.emergencyMb == 192 && !strstr(g_sizeNote, "capped at 2000") && strstr(g_sizeNote, "the largest 2500 MB")
              && g_brakeCfg.raiseOn && !strstr(g_sizeNote, "brake lowers only"),
              "wine: a desktop Linux layout with a 2500 MB block is sized as Windows is, with the Windows lines and raises, and logged (%d): %s", g_sizedT, g_sizeNote);
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); g_tWine = 1; g_tAddrSpace.largest = 1023ull << 20; g_tAddrSpace.total = 2000ull << 20; g_tAddrSpace.blocks = 5;
        TSizeSettle(1000, 3000);
        CHECK(g_sized && g_sizedT == WINE_BUDGET_MAX_MB && g_brakeCfg.holdMb == 511, "wine: one MB under the roomy line is still capped, with the lines from the block (%d, hold %d)", g_sizedT, g_brakeCfg.holdMb);
        // Proton's WoW64 mode is read from the environment Wine hands the game: exactly "1" is on.
        SetEnvironmentVariableA("PROTON_USE_WOW64", NULL); bool w0 = ProtonWow64();
        SetEnvironmentVariableA("PROTON_USE_WOW64", "1"); bool w1 = ProtonWow64();
        SetEnvironmentVariableA("PROTON_USE_WOW64", "0"); bool w2 = ProtonWow64();
        SetEnvironmentVariableA("PROTON_USE_WOW64", "10"); bool w3 = ProtonWow64();
        SetEnvironmentVariableA("PROTON_USE_WOW64", NULL);
        CHECK(!w0 && w1 && !w2 && !w3, "wine: PROTON_USE_WOW64 is on only when it is exactly 1 (%d %d %d %d)", w0, w1, w2, w3);
        // A player's two runs of one install, the Complete Edition on Proton 11 (FINDINGS 8aw): the game's own video
        // figure 3072 MB, render targets 1157 MB. Without WoW64: 103 MB of address space free at sizing in 16 blocks.
        // The budget stays under the game's figure less the render targets (which is what held his world: 2,763 MB of
        // video memory = 0.9 x 3072), the line names the figure and not Wine's cap, and it points to WoW64.
        static uint64_t wFigV, wFigS, wFigSaved;
        wFigV = wFigSaved = 3072ull << 20; wFigS = 31726ull << 20;
        TSizeReset(1, 20134, CAR_STOCK, PED_STOCK); g_tWine = 1; g_tRt0 = 1157ull << 20;
        g_tAddrSpace.largest = 15ull << 20; g_tAddrSpace.total = 103ull << 20; g_tAddrSpace.blocks = 16;
        g_fig = FigWatch{}; g_cs.figVideo = (uintptr_t)&wFigV; g_cs.figSystem = (uintptr_t)&wFigS; g_cs.figSaved = (uintptr_t)&wFigSaved;
        TSizeSettle(1000, 3000);
        CHECK(g_sized && g_sizedT == 1915 && TTabAll(1915) && g_brakeCfg.holdMb == 7 && g_brakeCfg.emergencyMb == 3 && !g_brakeCfg.raiseOn
              && strstr(g_sizeNote, "the card allows 16449); the game's own video figure is 3072 MB: the budget stays under it less the render targets")
              && !strstr(g_sizeNote, "capped at 2000 on Wine")
              && strstr(g_sizeNote, "; address space 103 MB free in 16 blocks, the largest 15 MB (on Proton, PROTON_USE_WOW64=1 leaves the game far more)"),
              "wine: his run without WoW64: budget %d under the game's 3072 MB figure, the layout and the pointer to WoW64: %s", g_sizedT, g_sizeNote);
        // The same install with it: 2,438 MB free, the largest block 2048. Sized as Windows is, still under the game's
        // figure, with the Windows lines and raises, and nothing to point to.
        SetEnvironmentVariableA("PROTON_USE_WOW64", "1");
        TSizeReset(1, 20003, CAR_STOCK, PED_STOCK); g_tWine = 1; g_tRt0 = 1157ull << 20;
        g_tAddrSpace.largest = 2048ull << 20; g_tAddrSpace.total = 2438ull << 20; g_tAddrSpace.blocks = 8;
        g_fig = FigWatch{}; TSizeSettle(1000, 3000);
        CHECK(g_sized && g_sizedT == 1915 && TTabAll(1915) && g_brakeCfg.holdMb == 512 && g_brakeCfg.raiseOn && g_brakeCfg.capMb == 1915
              && strstr(g_sizeNote, "the card allows 16325); the game's own video figure is 3072 MB") && !strstr(g_sizeNote, "capped at 2000")
              && strstr(g_sizeNote, "the largest 2048 MB") && !strstr(g_sizeNote, "PROTON_USE_WOW64"),
              "wine: his run with WoW64: budget %d, the Windows lines, no pointer: %s", g_sizedT, g_sizeNote);
        // A fragmented layout with WoW64 already on (a Deck, say): the cap as before, and nothing to point to.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); g_tWine = 1; g_cs.figVideo = g_cs.figSystem = g_cs.figSaved = 0; g_fig = FigWatch{};
        g_tAddrSpace.largest = 269ull << 20; g_tAddrSpace.total = 1400ull << 20; g_tAddrSpace.blocks = 17;
        TSizeSettle(1000, 3000);
        CHECK(g_sizedT == WINE_BUDGET_MAX_MB && strstr(g_sizeNote, "capped at 2000 on Wine (fragmented address space)") && !strstr(g_sizeNote, "PROTON_USE_WOW64"),
              "wine: fragmented with WoW64 on: capped, no pointer: %s", g_sizeNote);
        SetEnvironmentVariableA("PROTON_USE_WOW64", NULL);
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); g_tWine = 1;
        g_tAddrSpace.largest = 269ull << 20; g_tAddrSpace.total = 1400ull << 20; g_tAddrSpace.blocks = 17;
        TSizeSettle(1000, 3000);
        CHECK(g_sizedT == WINE_BUDGET_MAX_MB && strstr(g_sizeNote, "capped at 2000 on Wine (fragmented address space)")
              && strstr(g_sizeNote, "the largest 269 MB (on Proton, PROTON_USE_WOW64=1 leaves the game far more)"),
              "wine: fragmented without it: capped, and the pointer: %s", g_sizeNote);
        // The Linux view (FINDINGS 8ay): on Wine the free address space is the unmapped gaps under 4 GB in the game's
        // /proc/<pid>/maps. The gaps: between mappings and up to 4 GB; a 64-bit mapping above 4 GB counts for nothing;
        // a map cut short counts no gap after its last line.
        {
            const char* mt = "00010000-00400000 r--p 00000000 00:00 0\n"
                             "00400000-02000000 r-xp 00000000 08:01 11 /g/GTAIV.exe\n"
                             "10000000-f0000000 ---p 00000000 00:00 0\n"
                             "f0100000-f0200000 rw-s 00000000 00:05 9 /dev/dri/renderD128\n"
                             "7f0000000000-7f0000100000 r--p 00000000 00:00 0\n";
            AddrSpace g = MapsGaps(mt, strlen(mt), 0xFFFF0000ull, true);
            CHECK(g.largest == 0x0FDF0000ull && g.total == 0x0E000000ull + 0x00100000ull + 0x0FDF0000ull && g.blocks == 3,
                  "linux map: gaps between mappings and up to 4 GB, nothing above it (largest %llu, total %llu, %d)", g.largest, g.total, g.blocks);
            size_t cut = strstr(mt, "7f00") - mt;
            AddrSpace c = MapsGaps(mt, cut, 0xFFFF0000ull, false);
            CHECK(c.largest == 0x0E000000ull && c.total == 0x0E100000ull && c.blocks == 2,
                  "linux map: a map cut short counts no gap after its last line (largest %llu, total %llu)", c.largest, c.total);
        }
        {
            char tmp[MAX_PATH]; GetTempPathA(MAX_PATH, tmp);
            char root[MAX_PATH]; _snprintf_s(root, sizeof(root), _TRUNCATE, "%ssisco_proc_test", tmp);
            auto put = [&](const char* rel, const char* data, size_t n) {
                char p[MAX_PATH]; _snprintf_s(p, sizeof(p), _TRUNCATE, "%s\\%s", root, rel);
                char d[MAX_PATH]; strcpy_s(d, p); *strrchr(d, '\\') = 0; CreateDirectoryA(root, NULL); CreateDirectoryA(d, NULL);
                FILE* f = NULL; fopen_s(&f, p, "wb"); if (f) { fwrite(data, 1, n, f); fclose(f); }
            };
            const char self[] = "6a2f0000-6a2f1000 r--p 00000000 08:01 7 /home/p/GTAIV/plugins/SISCO.asi\n";
            char m300[512];
            _snprintf_s(m300, sizeof(m300), _TRUNCATE, "00010000-6a2f0000 ---p 00000000 00:00 0\n%s6a2f1000-c0000000 rw-p 00000000 00:00 0\n"
                        "c8000000-ffff0000 ---p 00000000 00:00 0\n", self);
            const char m200[] = "00010000-ffff0000 ---p 00000000 00:00 0\n";
            put("100\\cmdline", "C:\\windows\\system32\\services.exe\0", 34);
            put("200\\cmdline", "C:\\Program Files\\GTAIV\\GTAIV.exe\0-arg\0", 38); put("200\\maps", m200, strlen(m200));
            put("300\\cmdline", "Z:\\games\\GTAIV\\GTAIV.exe\0", 25); put("300\\maps", m300, strlen(m300));
            put("self\\cmdline", "GTAIV.exe\0", 10);
            uintptr_t keepBase = g_selfBase; char keepName[sizeof(g_selfName)]; strcpy_s(keepName, g_selfName);
            g_selfBase = 0x6A2F0000u; strcpy_s(g_selfName, "sisco.asi");
            // Two games' command lines: the one whose map holds SISCO's own image is the game, whatever the order.
            TSizeReset(1, 20134, CAR_STOCK, PED_STOCK); g_tWine = 1; g_tProcRoot = root;
            AddrSpace a = AddressSpace();
            CHECK(g_asView == 1 && strstr(g_procMaps, "\\300\\maps") && a.largest == (128ull << 20) && a.total == (128ull << 20) && a.blocks == 1,
                  "linux map: the game's pid is the one whose map holds SISCO (%s): largest %llu MB", g_procMaps, a.largest >> 20);
            // Sized from it: Wine's cap, the lines from the block with the Linux floors (hold 96, emergency 48 where half
            // and a quarter of 128 would be 64 and 32), the size line names the map and points to WoW64.
            TSizeSettle(1000, 3000);
            CHECK(g_sizedT == WINE_BUDGET_MAX_MB && g_brakeCfg.holdMb == 96 && g_brakeCfg.emergencyMb == 48 && !g_brakeCfg.raiseOn
                  && strstr(g_sizeNote, "; address space (Linux map) 128 MB free in 1 blocks, the largest 128 MB (on Proton, PROTON_USE_WOW64=1 leaves the game far more)"),
                  "linux map: sized from it, the lines floored at 96 and 48: %s", g_sizeNote);
            // The brake reads it every tick: the driver maps 48 MB more, the gap is 80 MB, under the hold.
            const char* gapAt = strstr(m300, "c8000000"); char m300b[512]; strcpy_s(m300b, m300); memcpy(m300b + (gapAt - m300), "c5000000", 8);
            put("300\\maps", m300b, strlen(m300b));
            int64_t lNow = (int64_t)(LargestFreeBlock() >> 20);
            BrakeCfg lc = g_brakeCfg; BrakeState lst = { 2000, false, -1, 0 }; int la = -1;
            int lt = BrakeDecide(lst, 1000, 2500, 797, lNow, lc, &la);
            CHECK(lNow == 80 && la == BRAKE_HOLD && lt < 2000, "linux map: the brake sees the driver's mappings: 80 MB, a hold (%lld, act %d, budget %d)", lNow, la, lt);
            // A lone GTAIV.exe whose map does not show SISCO (an image Wine copied): it stands.
            put("300\\cmdline", "C:\\windows\\explorer.exe\0", 24);
            TSizeReset(1, 20134, CAR_STOCK, PED_STOCK); g_tWine = 1; g_tProcRoot = root;
            a = AddressSpace();
            CHECK(g_asView == 1 && strstr(g_procMaps, "\\200\\maps") && a.largest == 0, "linux map: a lone game stands without the confirming line (%s)", g_procMaps);
            // Two games and neither map shows SISCO: no guess; the Windows view, and the size line says so.
            put("300\\cmdline", "Z:\\games\\GTAIV\\GTAIV.exe\0", 25); put("300\\maps", m200, strlen(m200));
            TSizeReset(1, 20134, CAR_STOCK, PED_STOCK); g_tWine = 1; g_tProcRoot = root;
            TSizeSettle(1000, 3000);
            CHECK(g_asView == -1 && !g_procMaps[0] && strstr(g_sizeNote, "; address space (Windows' view: the Linux map not found) "),
                  "linux map: two games and no confirming line: not guessed, the Windows view, said: %s", g_sizeNote);
            // Wine's 64-bit WoW64 mode (a mapping above 4 GB): Wine holds the low 4 GB for the game there, so its gaps are
            // not the game's room. Windows' view, said on the size line, and no pointer to the mode it is already in.
            char m64[640]; _snprintf_s(m64, sizeof(m64), _TRUNCATE, "%s7f0000000000-7f0000100000 r--p 00000000 00:00 0\n", m300);
            put("300\\maps", m64, strlen(m64));
            bool hi = false; MapsGaps(m64, strlen(m64), 0xFFFF0000ull, true, &hi);
            TSizeReset(1, 20134, CAR_STOCK, PED_STOCK); g_tWine = 1; g_tProcRoot = root;
            TSizeSettle(1000, 3000);
            CHECK(hi && g_asView == -2 && strstr(g_sizeNote, "; address space (Windows' view: Wine's 64-bit WoW64 mode) ") && !strstr(g_sizeNote, "PROTON_USE_WOW64=1 leaves"),
                  "linux map: Wine's 64-bit mode is seen in the map, and Windows' view stands there: %s", g_sizeNote);
            // No /proc at all (drive Z: not mapped): the same.
            TSizeReset(1, 20134, CAR_STOCK, PED_STOCK); g_tWine = 1; g_tProcRoot = "Q:\\no\\such\\proc";
            AddressSpace();
            CHECK(g_asView == -1, "linux map: no /proc to read: the Windows view (%d)", g_asView);
            g_selfBase = keepBase; strcpy_s(g_selfName, keepName);
            const char* rels[] = { "100\\cmdline", "200\\cmdline", "200\\maps", "300\\cmdline", "300\\maps", "self\\cmdline" };
            for (const char* r : rels) { char p[MAX_PATH]; _snprintf_s(p, sizeof(p), _TRUNCATE, "%s\\%s", root, r); DeleteFileA(p); }
            const char* dirs[] = { "100", "200", "300", "self" };
            for (const char* d : dirs) { char p[MAX_PATH]; _snprintf_s(p, sizeof(p), _TRUNCATE, "%s\\%s", root, d); RemoveDirectoryA(p); }
            RemoveDirectoryA(root);
            TSizeReset(1, 20134, CAR_STOCK, PED_STOCK);
        }
        g_tRt0 = 799ull << 20;
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); TSizeSettle(1000, 3000);
        CHECK(g_sized && g_sizedT == 4000 && g_brakeCfg.holdMb == 512 && g_brakeCfg.emergencyMb == 192 && !strstr(g_sizeNote, "address space") && !strstr(g_sizeNote, "on Wine")
              && strstr(g_sizeNote, "; pool managed;"),
              "wine: on Windows nothing of this applies: 4000, the measured lines, no layout on the line, and the pool named managed: %s", g_sizeNote);
        // The settle boundary itself: not one tick early, sized exactly at SETTLE_TICKS steady ticks.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        for (int i = 0; i < SETTLE_TICKS; i++) SisTick(1000 + i * 1000, 3000);   // the first tick only records
        CHECK(!g_sized, "settle: not sized one tick before the settle");
        SisTick(1000 + SETTLE_TICKS * 1000, 3000);
        CHECK(g_sized, "settle: sized exactly at SETTLE_TICKS steady ticks");

        // The budget is only raised behind the pools and the queue fix that were raised to carry it.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        SiteSet(S_LINK_SIZE, ST_OFF_MISMATCH, "");
        TSizeSettle(1000, 3000);
        CHECK(g_sized && memcmp(g_tTab, kCtStockTable, sizeof(g_tTab)) == 0 && g_sizedT == TStockFloor(2) && strstr(g_sizeNote, "the link pool was not raised"),
              "world: the table is left exactly as the game had it when the link pool was not raised: %s", g_sizeNote);
        SiteSet(S_LINK_SIZE, ST_ON, "");

        // The lock fix is the other half of surviving a pool that runs dry: raised pools keep the game away
        // from the edge, the recursive lock is what saves it at the edge. Both, or the budget stays.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        g_linkFixInstalled = 0;
        TSizeSettle(1000, 3000);
        CHECK(g_sized && memcmp(g_tTab, kCtStockTable, sizeof(g_tTab)) == 0 && g_sizedT == TStockFloor(2)
              && strstr(g_sizeNote, "the link-pool lock fix is not in"),
              "world: the budget stays the game's own when the link-pool lock fix did not go in: %s", g_sizeNote);
        g_linkFixInstalled = 1;

        // The same for the arena. It is raised on every renderer now, but it can still be refused: the site may not
        // match, or the plugin may have loaded after the game already built it. Either way a raised budget would be
        // loading through the game's 160 MiB, which is what empties the world, so the budget stays where it is.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        SiteSet(S_ARENA_SIZE, ST_OFF_ERROR, "too late: the game already built it");
        TSizeSettle(1000, 3000);
        CHECK(g_sized && memcmp(g_tTab, kCtStockTable, sizeof(g_tTab)) == 0 && g_sizedT == TStockFloor(2) && !g_brakeOn
              && strstr(g_sizeNote, "the streaming arena was not raised"),
              "world: DXVK proven but the arena left at the game's 160 MiB: the budget stays the game's own: %s", g_sizeNote);
        SiteSet(S_ARENA_SIZE, ST_ON, "");

        // The budget row the game runs on. Under FusionFix the branch to row 15 is forced (its ExtraStreamingMemory
        // off, which is every FusionFix install), and the game runs at 800, not the 550 SISCO's floor once assumed:
        // the brake was measured stepping to 664, 644 and 550 on such installs, below what the game itself would
        // have used. The floor now asks the game's own question, both ways it can be answered.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        g_tMemRestrictTest[0] = 0xE9;                         // FusionFix's forced branch: the test's first byte is a jump
        TSizeSettle(1000, 3000);
        CHECK(g_sized && g_sizedTStock == TRow15(2) && g_brakeCfg.floorMb == TRow15(2) && TRow15(2) == 800,
              "floor: the row-15 branch forced: the game's own is %d and the brake cannot go below it", g_sizedTStock);
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        g_tNoMemRestrict = 1;                                 // the player's own -nomemrestrict: the same row
        TSizeSettle(1000, 3000);
        CHECK(g_sized && g_sizedTStock == TRow15(2), "floor: the player's own -nomemrestrict: %d", g_sizedTStock);
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); TSizeSettle(1000, 3000);
        CHECK(g_sized && g_sizedTStock == TStockFloor(2) && TStockFloor(2) == 550,
              "floor: stock test and no option: the vendor rows, %d", g_sizedTStock);
        // FusionFix rewrites the test on every install; with ExtraStreamingMemory on it hooks the credit function too
        // and the game runs the vendor rows, ignoring even a player's own -nomemrestrict.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); g_tMemRestrictTest[0] = 0xE9; g_tGafm[0] = 0xE9; TSizeSettle(1000, 3000);
        CHECK(g_sized && g_sizedTStock == TStockFloor(2), "floor: FusionFix with ExtraStreamingMemory on: the vendor rows, %d", g_sizedTStock);
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); g_tMemRestrictTest[0] = 0xE9; g_tGafm[0] = 0xE9; g_tNoMemRestrict = 1; TSizeSettle(1000, 3000);
        CHECK(g_sized && g_sizedTStock == TStockFloor(2), "floor: and the player's own -nomemrestrict is ignored there, %d", g_sizedTStock);
        // The stock test carries the slot's address as the loader relocated it, so the file's own operand (0x10AAB78,
        // where the mock cannot be) reads as a rewritten test: the compare uses the rebased slot, as it must.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); { uint32_t fa = 0x10AAB78u; memcpy(g_tMemRestrictTest + 2, &fa, 4); } TSizeSettle(1000, 3000);
        CHECK(g_sized && g_sizedTStock == TRow15(2), "floor: the file's own operand is not the stock test in a relocated image, %d", g_sizedTStock);
        // The Complete Edition's stock test has the same shape with its own jump distance (0x162): recognised on its
        // build alone, and 1.0.8.0's distance reads as rewritten there.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); g_cs.build = BUILD_CE; memcpy(g_tSzVs + 4, kVsCtorCallCe, 5); g_tMemRestrictTest[9] = 0x62; TSizeSettle(1000, 3000);
        CHECK(g_sized && g_sizedTStock == TStockFloor(2), "floor: the Complete Edition's stock test is recognised by its own distance, %d", g_sizedTStock);
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); g_cs.build = BUILD_CE; memcpy(g_tSzVs + 4, kVsCtorCallCe, 5); g_tMemRestrictTest[9] = 0x58; TSizeSettle(1000, 3000);
        CHECK(g_sized && g_sizedTStock == TRow15(2), "floor: 1.0.8.0's distance on the Complete Edition reads as rewritten (FusionFix), row 15: %d", g_sizedTStock);
        g_tMemRestrictTest[9] = 0x58;
        // A player's own -memrestrict N: the game never reads the table then, so it is left alone and the line says why.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); g_tMemRestrict = 1; TSizeSettle(1000, 3000);
        CHECK(g_sized && memcmp(g_tTab, kCtStockTable, sizeof(g_tTab)) == 0 && strstr(g_sizeNote, "-memrestrict"),
              "floor: the player's own -memrestrict: the table is not read, so it is left: %s", g_sizeNote);

        // SISCO writes the -managed launch option and used to assume it took. The game tests -nominimize AFTER
        // -managed and that one wins, so a player with it runs unmanaged while the log says -managed set. Run D
        // measured DXVK without managed resources collapsing the address space at a budget of 1400, well under the
        // 4000 asked for here, so the raise is refused instead.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        g_tPoolMode = 0;
        TSizeSettle(1000, 3000);
        CHECK(g_sized && memcmp(g_tTab, kCtStockTable, sizeof(g_tTab)) == 0 && g_sizedT == TStockFloor(2)
              && strstr(g_sizeNote, "not using Direct3D managed resources"),
              "world: the game decided unmanaged: the budget stays the game's own: %s", g_sizeNote);

        // But only a confident 0 counts. The game writes 0 or 1 there and nothing else, so anything else means
        // SISCO is not reading what it thinks it is, and a wrong address must not cost every player their budget.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        g_cs.poolMode = 0x10;                                 // not readable at all, not a readable odd value
        TSizeSettle(1000, 3000);
        CHECK(g_sized && g_sizedT == T_MAX_MB && !strstr(g_sizeNote, "managed resources"),
              "world: an unreadable pool mode leaves the decision alone: %d MB", g_sizedT);
        g_cs.poolMode = (uintptr_t)&g_tPoolMode; g_tPoolMode = 1;

        // The car budget is only worth raising as far as there are slots for the models it pays for.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        SiteSet(S_VSTRUCT, ST_OFF_MISMATCH, "site bytes differ");
        TSizeSettle(1000, 3000);
        CHECK(g_sized && g_sizedT == T_MAX_MB && g_tVeh == CAR_NO_POOL && g_tPed == PED_FULL
              && strstr(g_sizeNote, "(car held at 120 MB: VehicleStruct pool not raised)"),
              "world: the VehicleStruct raise was refused: car held at %u MB, ped still %u MB", g_tVeh / 1000000, g_tPed / 1000000);
        SiteSet(S_VSTRUCT, ST_ON, "");

        // A player's own -availablevidmem is what the game believes it has, so it bounds the budget however big
        // the card is. Without this the budget was written past what the game could reach, and the traffic
        // budgets took their share of a pool that size.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        g_playerVidmemMb = 2048;
        TSizeSettle(1000, 3000);
        CHECK(g_sized && g_sizedT < 2048 && g_sizedT > TStockFloor(2) && strstr(g_sizeNote, "your -availablevidmem"),
              "world: a 24 GB card with the player's own -availablevidmem 2048: budget %d MB", g_sizedT);

        // A figure larger than the card changes nothing, because the card is still the smaller of the two.
        TSizeReset(1, 3895, CAR_STOCK, PED_STOCK);
        g_playerVidmemMb = 8192;
        TSizeSettle(1000, 3000);
        CHECK(g_sized && !strstr(g_sizeNote, "your -availablevidmem"),
              "world: an -availablevidmem above the card's own figure is not a bound: %d MB", g_sizedT);

        TSizeReset(1, 3895, CAR_STOCK, PED_STOCK); TSizeSettle(1000, 3000);
        CHECK(g_sizedT >= 1650 && g_sizedT <= 1665 && g_tVeh == CarPedFor(CAR_STOCK, CAR_FULL, g_sizedT, TStockFloor(2)) && g_tPed == CarPedFor(PED_STOCK, PED_FULL, g_sizedT, TStockFloor(2))
              && g_tVeh > CAR_STOCK && g_tVeh < 200000000u, "world: a 4 GB card at 4K: budget %d, car %u, ped %u", g_sizedT, g_tVeh, g_tPed);
    }
    // ---- the Complete Edition's shapes. Everything SISCO does differently there is installed on code shaped as CE's
    // compiler left it, so the CE thunks and the CE expected bytes are proven in this build and not only on one
    // machine: the push's ecx and its 0, the 20-byte inline copies with edi, the drain's ecx, the three release
    // encodings, and the native init's stack argument and ret 4.
    {
        CoreSites c = {}; SitesShapeCe(c);
        c.cache = (uintptr_t)g_ctCache; c.push = (uintptr_t)CtPushCe; c.drain = (uintptr_t)CtDrainCe;
        uintptr_t ds[5]; int nd = CtFindCalls((void*)CtDrainCallersCe, 200, (void*)CtDrainCe, ds, 5);
        uint8_t inl[20]; uint32_t cnt = (uint32_t)(uintptr_t)(g_ctCache + 0x40148), arr = (uint32_t)(uintptr_t)(g_ctCache + 0x148);
        inl[0] = 0xA1; memcpy(inl + 1, &cnt, 4); inl[5] = 0x8D; inl[6] = 0x0C; inl[7] = 0x85; memcpy(inl + 8, &arr, 4);
        inl[12] = 0x40; inl[13] = 0xA3; memcpy(inl + 14, &cnt, 4); inl[18] = 0x89; inl[19] = 0x39;
        uintptr_t ine[2], ins[2];
        int ni = CtFindBytes((void*)CtInlinePushesCe, 400, inl, 20, ine, 2);
        inl[19] = 0x31;
        ni += CtFindBytes((void*)CtInlinePushesCe, 400, inl, 20, ins, 2);
        CHECK(nd == 5 && ni == 4, "CE queue: five drain call sites and four 20-byte inline pushes found (%d, %d)", nd, ni);
        for (int i = 0; i < 5 && i < nd; i++) c.drainSite[i] = ds[i];
        if (ni == 4) { c.pushIn[0] = ine[0]; c.pushIn[1] = ins[0]; c.pushIn[2] = ins[1]; c.pushIn[3] = ine[1]; }

        *(uint32_t*)(g_ctCache + 0x40148) = 0; g_spillN = 0;
        bool on = InstallQueueFix(c);
        bool all = on && g_site[S_PUSH].state == ST_ON;
        for (int i = 0; i < 4; i++) all &= g_site[S_PUSH_IN1 + i].state == ST_ON;
        for (int i = 0; i < 5; i++) all &= g_site[S_DRAIN1 + i].state == ST_ON;
        CHECK(all, "CE queue: the fix, its four inline copies and its five drain wrappers installed");
        CHECK(CtPushCallerCe(0) == 1 && *(uint32_t*)(g_ctCache + 0x40148) == 0, "CE queue: a null value is dropped, as the game's own push drops it");
        uint64_t sum = 0; uint32_t pushed = 0; bool regs = true;
        for (uint32_t v = 1; v <= 65536 + 1000; v++) { if (CtPushCallerCe(v) != 1) regs = false; sum += v; pushed++; }
        for (uint32_t k = 0; k < 4; k++) { uint32_t v = 0x70000000u + k; if (CtInlinePushesCe(v, k) != 1) regs = false; sum += v; pushed++; }
        uint32_t count = *(uint32_t*)(g_ctCache + 0x40148);
        CHECK(regs && count == 0x10000 && g_spillN == 1004, "CE queue: the array stops at 65,536 (%u), 1,004 held aside (%ld), 0 returned, registers kept", count, g_spillN);
        g_ctSum = 0; g_ctEntries = 0; g_ctMaxCount = 0;
        uint32_t r = CtDrainCallersCe(2);
        CHECK(r == 1 && g_ctEntries == pushed && g_ctSum == sum && g_spillN == 0 && *(uint32_t*)(g_ctCache + 0x40148) == 0 && g_ctMaxCount <= 0x10000,
              "CE queue: one drain releases all %u of %u entries exactly once (sums %s), ebx kept, al %u", g_ctEntries, pushed, g_ctSum == sum ? "equal" : "DIFFER", r);


        g_ctGaveUp = 0; g_ctLockCe = 0; g_ctNone = 0;
        CtLinkAllocCe();
        CHECK(g_ctGaveUp == 1 && g_ctLockCe == 0, "CE lock control: without the fix the nested take waits on the lock its own thread holds");
        CoreSites lc = {}; SitesShapeCe(lc);
        lc.linkLock = (uintptr_t)&g_ctLockCe; lc.spinAcquire = (uintptr_t)CtSpin;
        int nt = CtFindCalls((void*)CtLinkAllocCe, 80, (void*)CtSpin, lc.linkTake, 1)
               + CtFindCalls((void*)CtLinkFreeCe, 40, (void*)CtSpin, lc.linkTake + 1, 1);
        uint8_t z[10], a[5]; uint32_t la = (uint32_t)(uintptr_t)&g_ctLockCe;
        InsnForm(z, F_MOV_IMM0, la); InsnForm(a, F_MOV_EAX, la);
        int nf = CtFindBytes((void*)CtLinkAllocCe, 80, z, 10, lc.linkFree, 1)
               + CtFindBytes((void*)CtLinkAllocCe, 80, a, 5, lc.linkFree + 1, 1)
               + CtFindBytes((void*)CtLinkFreeCe, 40, z, 10, lc.linkFree + 2, 1);
        CHECK(nt == 2 && nf == 3, "CE lock: two takes and the three releases in their own encodings found (%d, %d)", nt, nf);
        CHECK(InstallLinkLockFix(lc), "CE lock: the fix installed on all five sites");
        g_ctGaveUp = 0; g_ctLockCe = 0; g_linkLockOwner = 0; g_linkLockDepth = 0;
        uint32_t lr = CtLinkAllocCe();
        CHECK(g_ctGaveUp == 0 && g_ctLockCe == 0 && lr == 0x5A5A && g_ctAfterInner == 1,
              "CE lock: the holder goes through the nested take, the lock stays held inside (%ld) and is free at the end (%ld)", g_ctAfterInner, g_ctLockCe);
        g_ctNone = 1; g_ctLockCe = 0;
        CtLinkAllocCe();
        CHECK(g_ctGaveUp == 0 && g_ctLockCe == 0, "CE lock: the release that stores eax frees the lock too");
        g_ctNone = 0;

        static const uint8_t entry[8] = { 0x64, 0xA1, 0x2C, 0x00, 0x00, 0x00, 0xC3, 0xCC };
        memcpy(g_tNatEntry, entry, 8);
        uint32_t sz = (uint32_t)(uintptr_t)TNatSize(), pt = (uint32_t)(uintptr_t)TNatPtr(), ct = (uint32_t)(uintptr_t)&g_tNatCount;
        InsnForm(g_tNatS1, F_MOV_EBX, sz); InsnForm(g_tNatS2, F_MOV_EAX, pt); InsnForm(g_tNatS3, F_MOV_IMM0, ct);
        CoreSites rc = {}; SitesShapeCe(rc);
        rc.nativeInit = (uintptr_t)g_tNatEntry; rc.nativeSizeSt = (uintptr_t)g_tNatS1; rc.nativePtrSt = (uintptr_t)g_tNatS2; rc.nativeCountSt = (uintptr_t)g_tNatS3;
        rc.nativePtr = pt; rc.nativeSize = sz; rc.nativeCount = ct;
        g_tNatS1[1] ^= 4;
        CHECK(!InstallRaceFix(rc) && memcmp(g_tNatEntry, entry, 8) == 0, "CE race fix: refused when the size store is not the one read");
        g_tNatS1[1] ^= 4;
        CHECK(InstallRaceFix(rc) && g_tNatEntry[0] == 0xE9, "CE race fix: the entry jumps to the replacement");
        PVOID gv = AddVectoredExceptionHandler(1, TGuardVeh);
        static uint8_t ceTable[0x40 * 8];
        TNatArm(0x40, ceTable);
        TNatCallCe(0x40);
        RemoveVectoredExceptionHandler(gv);
        CHECK(g_tEspAfter == g_tEspBefore, "CE race fix: the replacement pops its stack argument (ret 4)");
        CHECK(g_tCallRet == (uint32_t)(uintptr_t)ceTable && *TNatPtr() == (uint32_t)(uintptr_t)ceTable && *TNatSize() == 0x40 && g_tNatCount == 0,
              "CE race fix: the table's pointer returned, the pointer, the size and the count stored as on 1.0.8.0");
        CHECK(g_tAtSizePtrHits == 1 && g_tAtPtrCount == 0 && g_tAtPtrZero == 1 && g_tAtPtrSizeHits == 0,
              "CE race fix: the size is still stored last, after the zeroed table and the pointer");
        CHECK(g_tAfter[1] == 0x11111111 && g_tAfter[2] == 0x22222222 && g_tAfter[3] == 0x33333333, "CE race fix: ebx, edi and ebp kept");

        CHECK(FormEq((uintptr_t)g_tNatS2, F_MOV_EAX, pt) && !FormEq((uintptr_t)g_tNatS2, F_MOV_EBX, pt),
              "the instruction forms: each build's encoding is told apart from the other's");

        // The launch options: CE compares the slots against ecx, not esi, and calls the import slot with no thunk.
        CoreSites dc = {}; SitesShapeCe(dc);
        uint32_t s0 = (uint32_t)(uintptr_t)&g_tPrm[0], s1 = (uint32_t)(uintptr_t)&g_tPrm[1], s2 = (uint32_t)(uintptr_t)&g_tPrm[2];
        InsnForm(g_tRd[0], F_CMP_ECX, s0); InsnForm(g_tRd[1], F_CMP_ECX, s1); InsnForm(g_tRd[2], F_LOAD_EAX, s2);
        dc.rdUnmanaged = (uintptr_t)g_tRd[0]; dc.rdManaged = (uintptr_t)g_tRd[1]; dc.rdVidmem = (uintptr_t)g_tRd[2];
        dc.prmUnmanaged = (uintptr_t)&g_tPrm[0]; dc.prmManaged = (uintptr_t)&g_tPrm[1]; dc.prmVidmem = (uintptr_t)&g_tPrm[2];
        CoreSites dc80 = dc; SitesShape1080(dc80);
        CHECK(ParamReadersMatch(dc) && !ParamReadersMatch(dc80), "CE options: the readers that compare against ecx are read, and 1.0.8.0's are not");
        uint32_t slot = (uint32_t)(uintptr_t)g_tIatSlot;
        g_tD3DCode[0x20] = 0xFF; g_tD3DCode[0x21] = 0x15; memcpy(g_tD3DCode + 0x22, &slot, 4);
        dc.d3dCall = (uintptr_t)g_tD3DCode + 0x20; dc.d3dThunk = 0; dc.d3dIat = (uintptr_t)g_tIatSlot;
        g_tIatSlot[0] = (uint32_t)(uintptr_t)MockDirect3DCreate9;
        g_tD3DCode[0x22] ^= 4;
        CHECK(!InstallD3DHook(dc) && g_tIatSlot[0] == (uint32_t)(uintptr_t)MockDirect3DCreate9,
              "CE Direct3D: refused when the direct call reads another slot, the slot untouched");
        g_tD3DCode[0x22] ^= 4;
        CHECK(InstallD3DHook(dc) && g_tIatSlot[0] == (uint32_t)(uintptr_t)HkDirect3DCreate9 && g_oD3DCreate9 == MockDirect3DCreate9,
              "CE Direct3D: hooked in its import slot with no thunk to check: %s", g_site[S_D3D_IAT].note);

        // The Complete Edition is given a video-memory figure too, since 1.0.5: the one FusionFix's substitute gives
        // where the game's own answer is right (the card's budget, at most half the RAM, never under 2048 MB), so
        // nothing moves there, and a game that answered 2048 MB on a 16 GB card (a player's log, FINDINGS 8av) is
        // told what the card has. Until then it was left to the game.
        CoreSites keepCs = g_cs;
        int keepDxvk = g_dxvk, keepState = g_dxgiState; int64_t keepBoot = g_bootBudgetMb; LONG keepCalls = g_d3dCalls;
        char keepNote[sizeof(g_d3dNote)]; memcpy(keepNote, g_d3dNote, sizeof(keepNote));
        uint32_t keepPrm[3] = { g_tPrm[0], g_tPrm[1], g_tPrm[2] };
        uint64_t keepRam = g_tTotalRam;
        g_cs = dc;
        g_d3dCalls = 0; g_dxvk = -1; g_bootBudgetMb = 23370; g_dxgiState = -1; g_tTotalRam = 32420ull << 20;
        g_tPrm[0] = g_tPrm[1] = g_tPrm[2] = 0; g_tD3D.dxvk = 1; g_tD3D.refs = 1;
        static uint8_t ceImg[96]; memset(ceImg, 0xAB, sizeof(ceImg)); ceImg[19] = 0; memcpy(ceImg + 20, "v3.1.1", 7);   // not the test binary's own .rdata
        g_tDxvkImage = ceImg; g_tDxvkImageN = sizeof(ceImg);
        ((D3DCreate9_t)(uintptr_t)g_tIatSlot[0])(32);
        CHECK(g_tPrm[2] == (uint32_t)(uintptr_t)g_vidmemArg && !strcmp(g_vidmemArg, "16210") && g_vidmemSetMb == 16210
              && g_tPrm[1] == (uint32_t)(uintptr_t)g_flagArg && strstr(g_d3dNote, "-availablevidmem 16210 set") && !strstr(g_d3dNote, "left to the game"),
              "CE Direct3D: -managed set, -availablevidmem = the card's budget, at most half the RAM (24 GB card, 32 GB RAM: %s): %s", g_vidmemArg, g_d3dNote);
        g_d3dCalls = 0; g_dxvk = -1; g_bootBudgetMb = 15209; g_tTotalRam = 63092ull << 20; g_tPrm[1] = g_tPrm[2] = 0;
        ((D3DCreate9_t)(uintptr_t)g_tIatSlot[0])(32);
        CHECK(g_tPrm[2] == (uint32_t)(uintptr_t)g_vidmemArg && !strcmp(g_vidmemArg, "15209") && g_vidmemSetMb == 15209,
              "CE Direct3D: the player's install (16 GB card, 64 GB RAM): the game is told %s MiB", g_vidmemArg);
        g_d3dCalls = 0; g_dxvk = -1; g_tPrm[1] = 0; g_tPrm[2] = 0x5678;
        ((D3DCreate9_t)(uintptr_t)g_tIatSlot[0])(32);
        CHECK(g_tPrm[2] == 0x5678 && g_vidmemSetMb == 0 && strstr(g_d3dNote, "the player's -availablevidmem kept"),
              "CE Direct3D: the player's own -availablevidmem stands there too: %s", g_d3dNote);
        g_d3dCalls = 0; g_dxvk = -1; g_bootBudgetMb = 300; g_tPrm[1] = g_tPrm[2] = 0;
        ((D3DCreate9_t)(uintptr_t)g_tIatSlot[0])(32);
        CHECK(g_tPrm[2] == 0 && g_vidmemSetMb == 0 && strstr(g_d3dNote, "-availablevidmem NOT set (no budget from DXGI)"),
              "CE Direct3D: no budget from DXGI: nothing written: %s", g_d3dNote);
        // On Wine the Complete Edition's own answer stands (3072 MB in a player's Proton logs, FINDINGS 8aw): the
        // address space is what runs short there, and no Wine run has measured the world past that figure.
        int keepWine = g_tWine;
        g_d3dCalls = 0; g_dxvk = -1; g_bootBudgetMb = 20855; g_tPrm[1] = g_tPrm[2] = 0; g_tWine = 1; g_wineNote[0] = 0;
        ((D3DCreate9_t)(uintptr_t)g_tIatSlot[0])(32);
        CHECK(g_tPrm[2] == 0 && g_vidmemSetMb == 0 && g_tPrm[1] == (uint32_t)(uintptr_t)g_flagArg && strstr(g_d3dNote, "-availablevidmem left to the game on Wine"),
              "CE Direct3D: on Wine -managed is set and the video figure is left to the game: %s", g_d3dNote);
        g_tWine = keepWine; g_wineNote[0] = 0;
        g_tDxvkImage = NULL; g_tDxvkImageN = 0;
        CHECK(CeVidmemMb(15209, 63092ull << 20) == 15209 && CeVidmemMb(23370, 32420ull << 20) == 16210 && CeVidmemMb(1700, 16384ull << 20) == 2048
              && CeVidmemMb(3400, 8192ull << 20) == 3400 && CeVidmemMb(23370, 0) == 23370,
              "CE Direct3D: the figure's rule: the budget, at most half the RAM, never under 2048, the budget alone when the RAM is not known");
        g_tTotalRam = keepRam; g_vidmemSetMb = 0;
        // Everything this case touched goes back: the cases after it read the Direct3D state this one overwrote.
        g_cs = keepCs; g_dxvk = keepDxvk; g_dxgiState = keepState; g_bootBudgetMb = keepBoot; g_d3dCalls = keepCalls;
        memcpy(g_d3dNote, keepNote, sizeof(g_d3dNote));
        g_tPrm[0] = keepPrm[0]; g_tPrm[1] = keepPrm[1]; g_tPrm[2] = keepPrm[2];
        g_tD3DVtbl[16] = (void*)FakeCreateDevice; g_d3dVtblHooked = NULL; g_oCreateDevice = NULL;   // this create() hooked the writable fake
        g_managedOn = false; g_dxvkVer[0] = g_dxvkVer[1] = g_dxvkVer[2] = -1;
    }
    // ---- Wine is asked of the process once (this one is Windows), and the address-space walk answers with real figures
    {
        g_tWine = -1; g_wine = -1;
        CHECK(!IsWine() && g_wine == 0, "wine: this process is Windows, and the answer is cached");
        g_tWine = 0;
        g_tAddrSpace.largest = 0;
        AddrSpace a = AddressSpace();
        CHECK(a.largest >= (64u << 20) && a.total >= a.largest && a.blocks >= 1 && LargestFreeBlock() == a.largest,
              "wine: the walk finds the free space of this process (largest %llu MB, %llu MB in %d blocks)", a.largest >> 20, a.total >> 20, a.blocks);
        char n[120]; AddrSpaceNote(n, sizeof(n));
        CHECK(!n[0], "wine: no layout note on Windows");
        g_tWine = 1; AddrSpaceNote(n, sizeof(n)); g_tWine = 0;
        CHECK(strstr(n, "; address space ") && strstr(n, "the largest"), "wine: and one on Wine: %s", n);
        g_brakeCfg.holdMb = BRAKE_HOLD_MB; g_brakeCfg.emergencyMb = BRAKE_EMERGENCY_MB; g_brakeCfg.raiseOn = 1; BrakeWineLines(2000);
        CHECK(g_brakeCfg.holdMb == 512 && g_brakeCfg.emergencyMb == 192 && g_brakeCfg.raiseOn,
              "wine: a large block at sizing leaves the Windows lines, 512 and 192 as BRAKEN2 ran them, and the raises");
        BrakeWineLines(269);
        CHECK(g_brakeCfg.holdMb == 134 && g_brakeCfg.emergencyMb == 67 && !g_brakeCfg.raiseOn, "wine: the Deck's 269 MB lowers the lines and turns the raises off");
        g_brakeCfg.holdMb = BRAKE_HOLD_MB; g_brakeCfg.emergencyMb = BRAKE_EMERGENCY_MB; g_brakeCfg.raiseOn = 1; BrakeWineLines(900);
        CHECK(g_brakeCfg.holdMb == 450 && g_brakeCfg.emergencyMb == 192 && !g_brakeCfg.raiseOn, "wine: 900 MB lowers only the hold line, and that alone turns the raises off");
        g_brakeCfg.holdMb = BRAKE_HOLD_MB; g_brakeCfg.emergencyMb = BRAKE_EMERGENCY_MB; g_brakeCfg.raiseOn = 1;
        BrakeWineLines(0);
        CHECK(g_brakeCfg.holdMb == 512, "wine: no block measured leaves them too");
    }
    // ---- the engine-sound slots, raised: the slot file, the gate, the writes all or nothing, the proof and the peak
    {
        // The file: CRLF, a slot of size 108,713,984 standing for the stock file's rest (its block is 120,633,344 with the
        // 15 engine slots: 126 MiB exactly with the margin), a TEST slot big enough to break the heap if it were counted,
        // and engine slots 1 to 15 but 14 (a gap: the game stops at 13).
        static char wf[16384]; size_t wn = 0;
        wn += sprintf_s(wf + wn, sizeof(wf) - wn, "<WaveSlots>\r\n  <Slot>\r\n    <Name content=\"ascii\">REST</Name>\r\n    <MaxHeaderSize value=\"2048\" />\r\n"
                        "    <LoadType content=\"ascii\">BANK</LoadType>\r\n    <Size value=\"108713984\" />\r\n  </Slot>\r\n");
        wn += sprintf_s(wf + wn, sizeof(wf) - wn, "  <Slot>\r\n    <Name content=\"ascii\">TEST</Name>\r\n    <Size value=\"200000000\" />\r\n  </Slot>\r\n");
        for (int k = 1; k <= 15; k++) if (k != 14)
            wn += sprintf_s(wf + wn, sizeof(wf) - wn, "  <Slot>\r\n    <Name content=\"ascii\">STREAM_ENGINE_%d</Name>\r\n    <Size value=\"794624\" />\r\n  </Slot>\r\n", k);
        wn += sprintf_s(wf + wn, sizeof(wf) - wn, "</WaveSlots>\r\n");
        static char wo[65536];
        WaveBuild wb = WaveslotsBuild(wf, wn, ENGINE_SLOTS, wo, sizeof(wo));
        CHECK(wb.ok && wb.before == 13 && wb.after == 48 && wb.added == 34, "engine: the file: 13 loaded before (the gap at 14), 48 after, 34 added (%d %d %d): %s", wb.before, wb.after, wb.added, wb.why);
        CHECK(wb.heapBytes == 152u << 20, "engine: the heap for 48 over the stock rest is 152 MiB, TEST left out (%u MiB)", wb.heapBytes >> 20);
        wo[wb.outN] = 0;
        CHECK(strstr(wo, ">STREAM_ENGINE_14</Name>") && strstr(wo, ">STREAM_ENGINE_48</Name>") && !strstr(wo, "STREAM_ENGINE_49") && !memcmp(wo, wf, 64)
              && strstr(wo, "<Size value=\"794624\" />\r\n  </Slot>\r\n</WaveSlots>\r\n") && wb.outN > wn,
              "engine: the gap and 16 to 48 added before the closing tag, in the file's CRLF, the rest untouched");
        WaveBuild w15 = WaveslotsBuild(wf, wn, 15, wo, sizeof(wo));
        CHECK(w15.ok && w15.after == 15 && w15.added == 1 && w15.heapBytes == AUDIO_HEAP_STOCK, "engine: asked for 15 on the stock rest: 126 MiB, the heap untouched (%u MiB)", w15.heapBytes >> 20);
        // Refusals: a name declared twice (the game refuses it too), no closing tag, a heap past 256 MiB.
        static char bad[20000]; memcpy(bad, wf, wn); bad[wn] = 0;
        char* dupAt = strstr(bad, "STREAM_ENGINE_2<"); memcpy(dupAt, "STREAM_ENGINE_1<", 16);
        WaveBuild wd = WaveslotsBuild(bad, wn, ENGINE_SLOTS, wo, sizeof(wo));
        CHECK(!wd.ok && strstr(wd.why, "STREAM_ENGINE_1 twice"), "engine: a duplicate name is refused: %s", wd.why);
        memcpy(bad, wf, wn); memcpy(strstr(bad, "</WaveSlots>"), "</WaveSlotz>", 12);
        WaveBuild wc = WaveslotsBuild(bad, wn, ENGINE_SLOTS, wo, sizeof(wo));
        CHECK(!wc.ok, "engine: a file with no closing tag is refused: %s", wc.why);
        memcpy(bad, wf, wn); memcpy(strstr(bad, "108713984"), "308713984", 9);
        WaveBuild wh = WaveslotsBuild(bad, wn, ENGINE_SLOTS, wo, sizeof(wo));
        CHECK(!wh.ok && strstr(wh.why, "MiB audio heap"), "engine: a file that needs over 256 MiB is refused: %s", wh.why);
        // LF only: the added slots follow it.
        static char lf[16384]; size_t ln = 0;
        for (size_t i = 0; i < wn; i++) if (wf[i] != '\r') lf[ln++] = wf[i];
        WaveBuild wl = WaveslotsBuild(lf, ln, ENGINE_SLOTS, wo, sizeof(wo)); wo[wl.outN] = 0;
        CHECK(wl.ok && !strchr(wo, '\r'), "engine: a file in LF gets its slots in LF");
        // Comments: a slot a modder commented out is not one (the game skips it too), a closing tag in a comment after
        // the file's own is not the file's, and the comments are copied through as they are.
        static char cm[20000]; size_t cn = 0;
        { const char* at = strstr(wf, "</WaveSlots>"); cn = (size_t)(at - wf); memcpy(cm, wf, cn);
          cn += sprintf_s(cm + cn, sizeof(cm) - cn, "<!--  <Slot>\r\n    <Name content=\"ascii\">STREAM_ENGINE_14</Name>\r\n    <Size value=\"794624\" />\r\n  </Slot>-->\r\n");
          cn += sprintf_s(cm + cn, sizeof(cm) - cn, "</WaveSlots>\r\n<!-- </WaveSlots> -->\r\n"); }
        WaveBuild wm = WaveslotsBuild(cm, cn, ENGINE_SLOTS, wo, sizeof(wo)); wo[wm.ok ? wm.outN : 0] = 0;
        CHECK(wm.ok && wm.before == 13 && wm.added == 34 && wm.heapBytes == 152u << 20,
              "engine: a slot commented out is not counted: 13 before, 34 added (%d, %d): %s", wm.before, wm.added, wm.why);
        const char* realClose = wm.ok ? strstr(wo, "</WaveSlots>\r\n<!-- </WaveSlots> -->") : NULL;
        const char* last = strstr(wo, ">STREAM_ENGINE_48</Name>");
        CHECK(realClose && last && last < realClose && strstr(wo, "<!--  <Slot>"),
              "engine: the slots go in before the file's own closing tag, not one in a comment, and the comments stay");
        memcpy(bad, wf, wn); bad[wn] = 0; memcpy(strstr(bad, "  <Slot>"), " <Slot >", 8);
        WaveBuild wa = WaveslotsBuild(bad, wn, ENGINE_SLOTS, wo, sizeof(wo));
        CHECK(!wa.ok && strstr(wa.why, "slot tag"), "engine: a slot tag SISCO cannot read is refused, since the game would load it all the same: %s", wa.why);
        { char tiny[] = "<a><!-- x --><b/><!-- never closed"; char* lv = XmlLive(tiny, strlen(tiny));
          CHECK(lv && !strcmp(lv, "<a>          <b/>                 "), "engine: XmlLive blanks every comment and keeps every offset (%s)", lv ? lv : "no memory");
          if (lv) HeapFree(GetProcessHeap(), 0, lv); }

        // The gate and the writes, on mocks: four references (one per field), the limit, both heaps, the count, the heap
        // pointer and the path pointer, as the game ships them.
        static uint8_t mTable[400]; static uint32_t mCount, mHeapPtr, mPath, mSummed; static char mStr[] = "config/waveslots.xml";
        static uint8_t mLim[3] = { 0x83, 0xF8, 0x19 }, mH1[5] = { 0xBE, 0x00, 0x00, 0xE0, 0x07 }, mH2[5] = { 0xBE, 0x00, 0x00, 0x30, 0x07 };
        uint8_t* code = (uint8_t*)VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        static AudioRef mRefs[4];
        for (int i = 0; i < 4; i++) {
            uint8_t* at = code + 16 * i + 2; at[-2] = 0x8B; at[-1] = 0x80;
            uint32_t d = (uint32_t)(uintptr_t)mTable + 4 * i; memcpy(at, &d, 4);
            mRefs[i].at = (uint32_t)(uintptr_t)at; mRefs[i].pre[0] = 0x8B; mRefs[i].pre[1] = 0x80; mRefs[i].preN = 2; mRefs[i].field = (uint8_t)(4 * i);
        }
        mPath = (uint32_t)(uintptr_t)mStr; mCount = 0; mHeapPtr = 0;
        CoreSites ec = {};
        ec.audioCapModRm = 0xF8; ec.audioSlotCap = (uintptr_t)mLim; ec.audioHeap = (uintptr_t)mH1; ec.audioHeapLow = (uintptr_t)mH2;
        ec.audioSlots = (uintptr_t)&mCount; ec.audioHeapPtr = (uintptr_t)&mHeapPtr; ec.audioTable = (uintptr_t)mTable;
        ec.audioRefs = mRefs; ec.audioRefN = 4; ec.audioRefShift = 0; ec.wavePath = (uintptr_t)&mPath;
        static uint8_t mReader[6]; mReader[0] = 0x8B; mReader[1] = 0x15; { uint32_t pa = (uint32_t)(uintptr_t)&mPath; memcpy(mReader + 2, &pa, 4); }
        ec.waveReader = (uintptr_t)mReader; ec.waveReadPush = 0; g_tTotalRam = 16ull << 30;
        ec.waveSummed = (uintptr_t)&mSummed; mSummed = 0;
        char why[120] = "";
        CHECK(EngineSitesMatch(ec, why, sizeof(why)), "engine: the sites as shipped match: %s", why);
        EnginePlan ep = {}; ep.ready = true; ep.before = 15; ep.slots = 48; ep.fileSlots = 117; ep.heapBytes = 152u << 20;
        uint8_t snap[64];
        code[16 * 2 + 2] ^= 1; memcpy(snap, code, 64);
        CHECK(!InstallEngineSlots(ec, ep) && !memcmp(snap, code, 64) && mLim[2] == 0x19 && mH1[3] == 0xE0 && mReader[0] == 0x8B
              && g_site[S_ENGINE].state == ST_OFF_MISMATCH && strstr(g_site[S_ENGINE].note, "another audio mod moved it") && !g_engineOn,
              "engine: one reference differs: nothing written at all: %s", g_site[S_ENGINE].note);
        code[16 * 2 + 2] ^= 1;
        mCount = 15; CHECK(!EngineSitesMatch(ec, why, sizeof(why)) && strstr(why, "too late"), "engine: the game's audio already built: refused: %s", why); mCount = 0;
        mSummed = 84; CHECK(!EngineSitesMatch(ec, why, sizeof(why)) && strstr(why, "read its slot file"), "engine: the slot file already read: refused: %s", why); mSummed = 0;
        mLim[2] = 0x40; CHECK(!EngineSitesMatch(ec, why, sizeof(why)) && strstr(why, "slot limit"), "engine: a limit another mod raised: refused: %s", why); mLim[2] = 0x19;
        mH2[4] = 0x0C; CHECK(!EngineSitesMatch(ec, why, sizeof(why)) && strstr(why, "audio heap"), "engine: a heap another mod raised: refused: %s", why); mH2[4] = 0x07;
        mReader[0] = 0xE9; CHECK(!EngineSitesMatch(ec, why, sizeof(why)) && strstr(why, "reader differs"), "engine: a slot-file reader another mod hooked: refused: %s", why); mReader[0] = 0x8B;
        g_tTotalRam = 1ull << 30; CHECK(!EngineSitesMatch(ec, why, sizeof(why)) && strstr(why, "1.4 GB"), "engine: under 1.4 GB of RAM the game's small file stands: %s", why); g_tTotalRam = 16ull << 30;
        EnginePlan notReady = {}; strcpy_s(notReady.why, "SISCO could not write pc\\audio\\config\\waveslots_sisco.xml");
        CHECK(!InstallEngineSlots(ec, notReady) && g_site[S_ENGINE].state == ST_OFF_DISABLED && strstr(g_site[S_ENGINE].note, "could not write"), "engine: no file written: nothing moved");
        CHECK(InstallEngineSlots(ec, ep) && g_engineOn && g_site[S_ENGINE].state == ST_ON && strstr(g_site[S_ENGINE].note, "15 -> 48, audio heap 126 -> 152 MiB"),
              "engine: the raise goes in: %s", g_site[S_ENGINE].note);
        bool refsMoved = true;
        for (int i = 0; i < 4; i++) { uint32_t d = 0; memcpy(&d, code + 16 * i + 2, 4); refsMoved &= d == (uint32_t)(uintptr_t)g_engineTable + 4 * i && code[16 * i] == 0x8B; }
        uint32_t h1 = 0, h2 = 0; memcpy(&h1, mH1 + 1, 4); memcpy(&h2, mH2 + 1, 4);
        uint32_t rdImm = 0; memcpy(&rdImm, mReader + 1, 4);
        CHECK(refsMoved && mLim[2] == 48 && h1 == 152u << 20 && h2 == 152u << 20 && mReader[0] == 0xBA && rdImm == (uint32_t)(uintptr_t)g_waveSiscoPath && mReader[5] == 0x90
              && mPath == (uint32_t)(uintptr_t)mStr,
              "engine: every reference points at SISCO's table with its field, the limit is 48, both heaps 152 MiB, the reader hands over waveslots_sisco.xml, the pointer untouched");
        // The proof on the log, from the count the game resolved, and the peak.
        mCount = 48; mSummed = 117; g_enginePlan = ep;
        char an[400]; CoreSites keepCs = g_cs; CoreSites ac = ec; AudioNote(ac, an, sizeof(an));
        CHECK(strstr(an, "48 engine sound slots in use (the game's own file 15), audio heap 152 MiB") && !strstr(an, "declared")
              && !strstr(an, ": the"), "engine: the audio line, everything as SISCO set it: the short form: %s", an);
        mCount = 21; mSummed = 91; AudioNote(ac, an, sizeof(an));
        CHECK(strstr(an, "the loader read 91 slots") && strstr(an, "the game read another slot file than waveslots_sisco.xml"), "engine: another file read is named as such: %s", an);
        uint8_t keepRd = mReader[0]; mReader[0] = 0xE9; AudioNote(ac, an, sizeof(an)); mReader[0] = keepRd;
        CHECK(strstr(an, "the slot file's reader was changed after SISCO set it"), "engine: a reader another mod hooked after SISCO is named as such: %s", an);
        mSummed = 117; AudioNote(ac, an, sizeof(an));
        CHECK(strstr(an, "found fewer engine slots in it"), "engine: the right file with too few slots is named as such: %s", an);
        mSummed = 0;
        mCount = 48; g_cs.audioSlots = (uintptr_t)&mCount; g_enginePeak = 0; memset(g_engineTable, 0, sizeof(g_engineTable));
        g_enginePeakLogged = 0; g_enginePeakMs = -ENGINE_PEAK_GAP_MS;
        for (int i = 0; i < 15; i++) *(uint32_t*)(g_engineTable + 16 * i + 8) = 1;
        EngineSlotTick(1000);
        CHECK(g_enginePeak == 15 && g_enginePeakLogged == 0, "engine: 15 held: the peak, not over the game's own, no line (%d)", g_enginePeak);
        for (int i = 15; i < 22; i++) *(uint32_t*)(g_engineTable + 16 * i + 8) = 2;
        EngineSlotTick(2000);
        CHECK(g_enginePeak == 22 && g_enginePeakLogged == 22, "engine: 22 held: a new peak over the game's 15, on the log at once (%d)", g_enginePeak);
        for (int i = 22; i < 25; i++) *(uint32_t*)(g_engineTable + 16 * i + 8) = 3;
        EngineSlotTick(3000);
        for (int i = 22; i < 25; i++) *(uint32_t*)(g_engineTable + 16 * i + 8) = 0;
        EngineSlotTick(30000);
        CHECK(g_enginePeak == 25 && g_enginePeakLogged == 22, "engine: 25 a second later: the peak kept, no line inside the minute (%d)", g_enginePeakLogged);
        EngineSlotTick(62000);
        CHECK(g_enginePeakLogged == 25, "engine: the minute over, the peak on the log though it has passed (%d)", g_enginePeakLogged);
        EngineSlotTick(200000);
        CHECK(g_enginePeakLogged == 25 && g_enginePeakMs == 62000, "engine: no new peak, no line");
        g_cs = keepCs; memset(g_engineTable, 0, sizeof(g_engineTable)); g_engineOn = false; g_enginePeak = 0;
        // The Complete Edition's reader pushes the pointer: push [ptr] becomes push imm32 and a NOP.
        { CoreSites cc = ec; cc.waveReadPush = 1; uint8_t a[6], b2[6]; WaveReaderBytes(cc, false, a); WaveReaderBytes(cc, true, b2);
          uint32_t pa = 0, pb = 0; memcpy(&pa, a + 2, 4); memcpy(&pb, b2 + 1, 4);
          CHECK(a[0] == 0xFF && a[1] == 0x35 && pa == (uint32_t)(uintptr_t)&mPath && b2[0] == 0x68 && pb == (uint32_t)(uintptr_t)g_waveSiscoPath && b2[5] == 0x90,
                "engine: the CE reader, push [ptr], becomes push imm32 and a NOP"); }
        // driverSettings.xml names the file: the stock value, none, another name, and two refusals.
        char rel[64], dwhy[120];
        const char* ds1 = "<DriverSettings>\r\n\t<WaveSlotsConfig value=\"audio:/config/waveslots.xml\"/>\r\n</DriverSettings>";
        CHECK(DriverWaveslots(ds1, strlen(ds1), rel, sizeof(rel), dwhy, sizeof(dwhy)) && !strcmp(rel, "config\\waveslots.xml"), "engine: driverSettings.xml's stock value is config\\waveslots.xml (%s)", rel);
        CHECK(DriverWaveslots("<DriverSettings/>", 17, rel, sizeof(rel), dwhy, sizeof(dwhy)) && !strcmp(rel, "config\\waveslots.xml"), "engine: no key: the game's default");
        CHECK(DriverWaveslots(NULL, 0, rel, sizeof(rel), dwhy, sizeof(dwhy)) && !strcmp(rel, "config\\waveslots.xml"), "engine: no file: the game's default");
        const char* ds2 = "<WaveSlotsConfig value=\"audio:/config/waveslots_big.xml\"/>";
        CHECK(DriverWaveslots(ds2, strlen(ds2), rel, sizeof(rel), dwhy, sizeof(dwhy)) && !strcmp(rel, "config\\waveslots_big.xml"), "engine: another name is followed (%s)", rel);
        const char* ds3 = "<WaveSlotsConfig value=\"platform:/audio/waveslots.xml\"/>";
        CHECK(!DriverWaveslots(ds3, strlen(ds3), rel, sizeof(rel), dwhy, sizeof(dwhy)) && strstr(dwhy, "outside audio:/"), "engine: a file outside audio:/ is refused: %s", dwhy);
        const char* ds4 = "<WaveSlotsConfig value=\"audio:/../../x.xml\"/>";
        CHECK(!DriverWaveslots(ds4, strlen(ds4), rel, sizeof(rel), dwhy, sizeof(dwhy)) && strstr(dwhy, "will not follow"), "engine: a path that climbs out is refused: %s", dwhy);
        const char* ds5 = "<!--\t<WaveSlotsConfig value=\"audio:/config/waveslots_old.xml\"/>-->\r\n\t<WaveSlotsConfig value=\"audio:/config/waveslots.xml\"/>";
        CHECK(DriverWaveslots(ds5, strlen(ds5), rel, sizeof(rel), dwhy, sizeof(dwhy)) && !strcmp(rel, "config\\waveslots.xml"),
              "engine: a WaveSlotsConfig line commented out is not the one followed (%s)", rel);
        memset(&g_site[S_ENGINE], 0, sizeof(g_site[S_ENGINE]));
        // The mocks back as the game ships them, for the plan below.
        for (int i = 0; i < 4; i++) { uint32_t d = (uint32_t)(uintptr_t)mTable + 4 * i; memcpy(code + 16 * i + 2, &d, 4); }
        mLim[2] = 0x19; { uint32_t a = 0x7E00000u, b = 0x7300000u; memcpy(mH1 + 1, &a, 4); memcpy(mH2 + 1, &b, 4); }
        mPath = (uint32_t)(uintptr_t)mStr; mCount = 0; mSummed = 0; mReader[0] = 0x8B; mReader[1] = 0x15; { uint32_t pa = (uint32_t)(uintptr_t)&mPath; memcpy(mReader + 2, &pa, 4); }
        CHECK(EngineSitesMatch(ec, why, sizeof(why)), "engine: the mocks are back as shipped: %s", why);

        // The plan in a folder: the game's own file read and left alone, SISCO's written beside it; FusionFix's update\
        // copy read when FusionFix is there to serve it; an unchanged file is not rewritten.
        char td[MAX_PATH] = ""; GetTempPathA(MAX_PATH, td); strcat_s(td, "sisco_engine_test");
        char d1[MAX_PATH], d2[MAX_PATH], d3[MAX_PATH], f1[MAX_PATH], f2[MAX_PATH], f3[MAX_PATH], u1[MAX_PATH], u2[MAX_PATH], u3[MAX_PATH], u4[MAX_PATH], uf[MAX_PATH], ffp[MAX_PATH], pl[MAX_PATH];
        sprintf_s(d1, "%s\\pc", td); sprintf_s(d2, "%s\\pc\\audio", td); sprintf_s(d3, "%s\\pc\\audio\\config", td);
        sprintf_s(f1, "%s\\pc\\audio\\config\\waveslots.xml", td); sprintf_s(f2, "%s\\pc\\audio\\config\\waveslots_sisco.xml", td);
        sprintf_s(f3, "%s\\pc\\audio\\config\\waveslots_sisco.tmp", td);
        CreateDirectoryA(td, NULL); CreateDirectoryA(d1, NULL); CreateDirectoryA(d2, NULL); CreateDirectoryA(d3, NULL);
        auto writeFile = [](const char* path, const char* data, size_t n) { HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL); DWORD w = 0; if (h != INVALID_HANDLE_VALUE) { WriteFile(h, data, (DWORD)n, &w, NULL); CloseHandle(h); } };
        char dsf[MAX_PATH]; sprintf_s(dsf, "%s\\pc\\audio\\config\\driverSettings.xml", td);
        writeFile(dsf, ds1, strlen(ds1));
        writeFile(f1, wf, wn);
        PlanEngineSlots(ec, td);
        size_t rn = 0; char* rb = ReadWhole(f2, &rn, 1 << 20); size_t on = 0; char* ob = ReadWhole(f1, &on, 1 << 20);
        CHECK(g_enginePlan.ready && g_enginePlan.before == 13 && g_enginePlan.slots == 48 && g_enginePlan.fileSlots == 1 + 48 && g_enginePlan.heapBytes == 152u << 20
              && !strcmp(g_enginePlan.from, "pc\\audio\\config\\waveslots.xml")
              && rb && strstr(rb, ">STREAM_ENGINE_48</Name>") && ob && on == wn && !memcmp(ob, wf, wn) && !FileIs(f3),
              "engine: the plan writes waveslots_sisco.xml beside the game's file and leaves the game's alone: %s", g_enginePlan.why);
        if (rb) HeapFree(GetProcessHeap(), 0, rb); if (ob) HeapFree(GetProcessHeap(), 0, ob);
        FILETIME t0 = {}, t1 = {}; HANDLE fh = CreateFileA(f2, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL); GetFileTime(fh, NULL, NULL, &t0); CloseHandle(fh);
        Sleep(20); PlanEngineSlots(ec, td);
        fh = CreateFileA(f2, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL); GetFileTime(fh, NULL, NULL, &t1); CloseHandle(fh);
        CHECK(g_enginePlan.ready && CompareFileTime(&t0, &t1) == 0, "engine: an unchanged file is not written again");
        sprintf_s(u1, "%s\\update", td); sprintf_s(u2, "%s\\update\\pc", td); sprintf_s(u3, "%s\\update\\pc\\audio", td); sprintf_s(u4, "%s\\update\\pc\\audio\\config", td);
        sprintf_s(uf, "%s\\update\\pc\\audio\\config\\waveslots.xml", td); sprintf_s(pl, "%s\\plugins", td); sprintf_s(ffp, "%s\\plugins\\GTAIV.EFLC.FusionFix.asi", td);
        CreateDirectoryA(u1, NULL); CreateDirectoryA(u2, NULL); CreateDirectoryA(u3, NULL); CreateDirectoryA(u4, NULL); CreateDirectoryA(pl, NULL);
        static char upd[20000]; memcpy(upd, wf, wn); memcpy(strstr(upd, ">REST<"), ">UPDR<", 6); writeFile(uf, upd, wn);
        PlanEngineSlots(ec, td);
        rb = ReadWhole(f2, &rn, 1 << 20);
        CHECK(g_enginePlan.ready && !strcmp(g_enginePlan.from, "pc\\audio\\config\\waveslots.xml") && rb && strstr(rb, ">REST<"), "engine: an update\\ file without FusionFix is not read");
        if (rb) HeapFree(GetProcessHeap(), 0, rb);
        writeFile(ffp, "", 0);
        PlanEngineSlots(ec, td);
        rb = ReadWhole(f2, &rn, 1 << 20);
        CHECK(g_enginePlan.ready && !strcmp(g_enginePlan.from, "update\\pc\\audio\\config\\waveslots.xml") && rb && strstr(rb, ">UPDR<") && !strstr(rb, ">REST<"),
              "engine: with FusionFix, its update\\ file is the one SISCO builds from");
        if (rb) HeapFree(GetProcessHeap(), 0, rb);
        { char scr[MAX_PATH], ffs[MAX_PATH]; sprintf_s(scr, "%s\\scripts", td); sprintf_s(ffs, "%s\\scripts\\GTAIV.EFLC.FusionFix.asi", td);
          DeleteFileA(ffp); CreateDirectoryA(scr, NULL); writeFile(ffs, "", 0);
          writeFile(f1, wf, wn); DeleteFileA(f2);
          PlanEngineSlots(ec, td);
          rb = ReadWhole(f2, &rn, 1 << 20);
          CHECK(g_enginePlan.ready && !strcmp(g_enginePlan.from, "update\\pc\\audio\\config\\waveslots.xml") && rb && strstr(rb, ">UPDR<"),
                "engine: FusionFix in scripts\\ serves the update folder too: %s", g_enginePlan.from);
          if (rb) HeapFree(GetProcessHeap(), 0, rb);
          DeleteFileA(ffs); RemoveDirectoryA(scr); }
        { char longDir[300]; memset(longDir, 'a', 240); longDir[240] = 0; memcpy(longDir, "C:\\", 3);
          PlanEngineSlots(ec, longDir);
          CHECK(!g_enginePlan.ready && strstr(g_enginePlan.why, "too long"),
                "engine: a game folder whose path leaves no room for the slot file's name is refused before anything is read: %s", g_enginePlan.why); }
        mCount = 15; PlanEngineSlots(ec, td);
        CHECK(!g_enginePlan.ready && strstr(g_enginePlan.why, "too late"), "engine: no plan once the game's audio exists: %s", g_enginePlan.why);
        mCount = 0;
        // driverSettings.xml naming another file: SISCO builds from that one.
        char bigf[MAX_PATH]; sprintf_s(bigf, "%s\\pc\\audio\\config\\waveslots_big.xml", td);
        static char big[20000]; memcpy(big, wf, wn); memcpy(strstr(big, ">REST<"), ">BIGR<", 6); writeFile(bigf, big, wn);
        writeFile(dsf, ds2, strlen(ds2)); DeleteFileA(ffp);
        PlanEngineSlots(ec, td);
        rb = ReadWhole(f2, &rn, 1 << 20);
        CHECK(g_enginePlan.ready && !strcmp(g_enginePlan.from, "pc\\audio\\config\\waveslots_big.xml") && rb && strstr(rb, ">BIGR<"),
              "engine: driverSettings.xml's own slot file is the one SISCO builds from (%s): %s", g_enginePlan.from, g_enginePlan.why);
        if (rb) HeapFree(GetProcessHeap(), 0, rb);
        DeleteFileA(bigf); DeleteFileA(dsf);
        DeleteFileA(ffp); DeleteFileA(uf); DeleteFileA(f2); DeleteFileA(f1);
        RemoveDirectoryA(u4); RemoveDirectoryA(u3); RemoveDirectoryA(u2); RemoveDirectoryA(u1); RemoveDirectoryA(pl);
        RemoveDirectoryA(d3); RemoveDirectoryA(d2); RemoveDirectoryA(d1); RemoveDirectoryA(td);
        memset(&g_enginePlan, 0, sizeof(g_enginePlan)); g_tTotalRam = 0;
        VirtualFree(code, 0, MEM_RELEASE);
    }
    // ---- the engine-sound slots: read and never written, and an install someone else raised is told apart from stock
    {
        static uint8_t capIns[3], heapIns[5];
        static uint32_t slotCount;
        CoreSites c = {}; SitesShape1080(c);
        c.audioSlotCap = (uintptr_t)capIns; c.audioHeap = (uintptr_t)heapIns; c.audioSlots = (uintptr_t)&slotCount;
        uint32_t heap = 0x7E00000;
        capIns[0] = 0x83; capIns[1] = 0xF8; capIns[2] = 25;
        heapIns[0] = 0xBE; memcpy(heapIns + 1, &heap, 4);
        slotCount = 15;
        uint8_t keepCap[3], keepHeap[5]; memcpy(keepCap, capIns, 3); memcpy(keepHeap, heapIns, 5);
        char t[200]; AudioNote(c, t, sizeof(t));
        CHECK(!memcmp(keepCap, capIns, 3) && !memcmp(keepHeap, heapIns, 5), "audio: nothing was written back");
        CHECK(strstr(t, "15 engine sound slots") && strstr(t, "limit 25") && strstr(t, "126 MiB") && !strstr(t, "another mod"),
              "audio: stock is read as 15 of 25: %s", t);
        capIns[2] = 50; slotCount = 50; heap = 160u << 20; memcpy(heapIns + 1, &heap, 4);
        AudioNote(c, t, sizeof(t));
        CHECK(strstr(t, "50 engine sound slots") && strstr(t, "another mod has changed them") && !strstr(t, "no engine sound"),
              "audio: an install another mod changed is named as one: %s", t);
        capIns[1] = 0xFF;                       // the Complete Edition's register, on 1.0.8.0's table
        AudioNote(c, t, sizeof(t));
        CHECK(strstr(t, "not found where SISCO reads them"), "audio: the other build's loop is refused, no figure invented: %s", t);
        SitesShapeCe(c);
        AudioNote(c, t, sizeof(t));
        CHECK(strstr(t, "50 engine sound slots"), "audio: the Complete Edition's own register is read: %s", t);
        memcpy(keepCap, capIns, 3); memcpy(keepHeap, heapIns, 5); AudioNote(c, t, sizeof(t));
        CHECK(!memcmp(keepCap, capIns, 3) && !memcmp(keepHeap, heapIns, 5), "audio: nothing was written back on the Complete Edition either");
    }
    // ---- the install order: the arena is always attempted; a launch-option reader that differs keeps the
    // Direct3D hook out. The queue and lock sites are empty here (their installs are tested above); the globals they set
    // are put back.
    {
        uintptr_t sOD = g_oDrain, sCA = g_cacheAddr; int sFI = g_fixInstalled, sLF = g_linkFixInstalled;
        static Site sSites[S_COUNT]; memcpy(sSites, g_site, sizeof(sSites)); CoreSites sCs = g_cs;
        CoreSites z = {};
        SitesShape1080(z);
        // Each group of the settings file is honoured, and anything not tried stays NOTTRIED rather than
        // pretending to have been refused.
        g_set.limits = g_set.fixes = false;
        memset(g_site, 0, sizeof(g_site));       // ST_NOTTRIED, so what this call does not touch stays visible
        SisInstall(z);
        CHECK(g_site[S_ARENA_SIZE].state == ST_NOTTRIED && g_site[S_SLOT_SIZE].state == ST_NOTTRIED
              && g_site[S_VSTRUCT].state == ST_NOTTRIED && g_site[S_PUSH].state == ST_NOTTRIED,
              "install: Fixes=0 and Limits=0 leave every site untouched");
        // Each alone: the other group is still tried (on empty sites it is refused, which is not NOTTRIED).
        g_set.limits = false; g_set.fixes = true; memset(g_site, 0, sizeof(g_site)); SisInstall(z);
        CHECK(g_site[S_ARENA_SIZE].state == ST_NOTTRIED && g_site[S_SLOT_SIZE].state == ST_NOTTRIED && g_site[S_VSTRUCT].state == ST_NOTTRIED
              && g_site[S_PUSH].state != ST_NOTTRIED, "install: Limits=0 alone leaves the raises untouched and still tries the fixes");
        g_set.limits = true; g_set.fixes = false; memset(g_site, 0, sizeof(g_site)); SisInstall(z);
        CHECK(g_site[S_PUSH].state == ST_NOTTRIED && g_site[S_LINK_TAKE1].state == ST_NOTTRIED && g_site[S_ARENA_SIZE].state != ST_NOTTRIED,
              "install: Fixes=0 alone leaves the fixes untouched and still tries the raises");
        g_set.limits = g_set.fixes = true;
        memcpy(g_site, sSites, sizeof(sSites));

        SisLoadResult lr = SisInstall(z);
        CHECK(!lr.queue && !lr.lock && !lr.race && !lr.arena && g_site[S_ARENA_SIZE].state == ST_OFF_ERROR && !lr.params && !lr.d3d
              && g_site[S_D3D_IAT].state == ST_OFF_MISMATCH,
              "install: the arena is always attempted, no Direct3D hook without its readers, nothing on empty sites");
        g_oDrain = sOD; g_cacheAddr = sCA; g_fixInstalled = sFI; g_linkFixInstalled = sLF; memcpy(g_site, sSites, sizeof(sSites)); g_cs = sCs;
    }
}
