// sis_core_tests.h: the core's tests. They are compiled into the release's own build (sis_test.exe), so what they
// exercise is the code that ships, down to the instruction. The includer defines
// CHECK and has initialised g_base, the clock and g_spillCs.

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
    g_playerVidmemMb = 0;
    g_sized = false; g_worldLogged = false; g_rtMin = 0; g_settleX = -1; g_settleTicks = 0;
    g_dxvk = dxvk; g_bootBudgetMb = bootMb; g_dxgiState = -1;   // no DXGI: the sizing uses the boot budget
    g_brakeOn = 0; g_brakeSt.t = 0; g_brakeSt.held = false; g_brakeCfg.floorMb = 800;
    g_car.on = g_ped.on = false;
    g_car.target = g_car.written = g_car.fromOther = g_ped.target = g_ped.written = g_ped.fromOther = 0;
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
    g_worldMs = 0; g_loadBuildS = 0;
    g_ldUp = false; g_ldUpMs = 0; g_ldPending = true; g_ldEdgeMs = 0; g_ldX = -1; g_ldSteady = 0; g_ldVerdict = false; g_loadN = 0;   // as at the start: the next player is a load's edge
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
        CHECK(r == &g_tD3D && g_tD3DSdk == 32 && g_dxvk == 1 && g_tD3D.refs == 1 && strstr(g_d3dNote, "interop"),
              "Direct3D: DXVK detected through its interop interface, the reference given back (%ld): %s", g_tD3D.refs, g_d3dNote);
        CHECK(g_tPrm[1] == (uint32_t)(uintptr_t)g_flagArg && g_tPrm[0] == 0, "Direct3D: -managed set, -unmanaged left alone");
        CHECK(g_bootBudgetMb >= 512 && g_tPrm[2] == (uint32_t)(uintptr_t)g_vidmemArg && strcmp(g_vidmemArg, want) == 0,
              "Direct3D: -availablevidmem = the card's budget, at most 8192: %s MiB", g_vidmemArg);
        // (2) a second call passes straight through
        g_tPrm[1] = 0; r = create(31);
        CHECK(r == &g_tD3D && g_tD3DSdk == 31 && g_d3dCalls == 2 && g_tPrm[1] == 0, "Direct3D: later calls pass straight through");
        // (3) the player's options stand
        g_d3dCalls = 0; g_dxvk = -1; g_tPrm[0] = 0x1234; g_tPrm[1] = 0; g_tPrm[2] = 0x5678;
        create(32);
        CHECK(g_dxvk == 1 && g_tPrm[0] == 0x1234 && g_tPrm[1] == 0 && g_tPrm[2] == 0x5678, "Direct3D: the player's -unmanaged and -availablevidmem stand");
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
        CHECK(g_dxvkVer[0] == 3 && g_dxvkVer[2] == 1 && g_tPrm[1] == (uint32_t)(uintptr_t)g_flagArg && strstr(g_d3dNote, "DXVK 3.1.1 (read from the planted image)"),
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
              && strstr(g_d3dNote, "Proton chose this version"),
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
        CHECK(BrakeDecide(bs, 0, 3300, 799, 1000, bc, &ba) == 4000 && ba == BRAKE_NONE && !bs.held, "brake: plenty of room, nothing");
        CHECK(BrakeDecide(bs, 1000, 3300, 799, 500, bc, &ba) == 2870 && ba == BRAKE_HOLD && bs.held,
              "brake: under 512 MB it holds once, at the budget whose wall is the V held now (%d, want 2870)", bs.t);
        CHECK(BrakeDecide(bs, 2000, 3290, 799, 499, bc, &ba) == 2870 && ba == BRAKE_NONE, "brake: held once, not again as V dips");
        CHECK(BrakeDecide(bs, 6000, 3300, 799, 460, bc, &ba) == 2870 && ba == BRAKE_NONE, "brake: a fall inside the wait does nothing");
        CHECK(BrakeDecide(bs, 21000, 3300, 799, 460, bc, &ba) == 2742 && ba == BRAKE_STEP, "brake: a 40 MB fall after the wait is one step (%d, want 2742)", bs.t);
        CHECK(BrakeDecide(bs, 42000, 3200, 799, 450, bc, &ba) == 2742 && ba == BRAKE_NONE, "brake: a 10 MB fall is not enough");
        CHECK(BrakeDecide(bs, 63000, 3200, 799, 470, bc, &ba) == 2742 && ba == BRAKE_NONE && bs.refL == 470, "brake: a rise moves the reference up");
        CHECK(BrakeDecide(bs, 84000, 3200, 799, 437, bc, &ba) == 2614 && ba == BRAKE_STEP, "brake: 33 MB under the highest point since is a step (%d)", bs.t);
        CHECK(BrakeDecide(bs, 90000, 3000, 799, 180, bc, &ba) == 2614 && ba == BRAKE_NONE, "brake: an emergency, but only 6 s after the last action");
        CHECK(BrakeDecide(bs, 95000, 3000, 799, 180, bc, &ba) == 2486 && ba == BRAKE_EMERGENCY, "brake: an emergency after 10 s needs no fall (%d)", bs.t);
        CHECK(BrakeDecide(bs, 105000, 3000, 799, 180, bc, &ba) == 2358 && ba == BRAKE_EMERGENCY, "brake: and again 10 s later, still no fall");
        bs.t = 850; bs.lastActMs = 0;
        CHECK(BrakeDecide(bs, 200000, 3000, 799, 100, bc, &ba) == 800 && ba == BRAKE_EMERGENCY, "brake: never under the floor");
        CHECK(BrakeDecide(bs, 300000, 3000, 799, 50, bc, &ba) == 800 && ba == BRAKE_NONE, "brake: at the floor, nothing more");
        BrakeState bw = { 4000, false, -1, 0 };
        CHECK(BrakeDecide(bw, 0, 4317, 799, 500, bc, &ba) == 4000 && ba == BRAKE_NONE && bw.held, "brake: a hold at the wall latches, no write");
        BrakeState bl = { 2000, false, -1, 0 };
        CHECK(BrakeDecide(bl, 0, 3300, 799, 500, bc, &ba) == 2000 && ba == BRAKE_NONE && bl.held, "brake: a hold never raises a lower budget");
        BrakeState bn = { 4000, false, -1, 0 };
        CHECK(BrakeDecide(bn, 0, -1, 799, 100, bc, &ba) == 4000 && ba == BRAKE_NONE && !bn.held, "brake: no V read, nothing");
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
        int64_t ms = 102000;                                  // an emergency steps every 10 s, fall or not
        while (g_brakeSt.t > 1400 && ms < 400000) { SisTick(ms, 100); ms += 11000; }
        uint32_t wantCar = CarPedFor(CAR_STOCK, CAR_FULL, g_brakeSt.t, TStockFloor(2));
        CHECK(g_tVeh == wantCar && wantCar < 200000000u && wantCar > CAR_STOCK && g_tPed == 250000000u && g_car.target == wantCar,
              "brake: under 2000 MB the car budget follows the world (budget %d MB, car %u MB), the larger ped budget stands",
              g_brakeSt.t, g_tVeh / 1000000);
        int held = g_brakeSt.t;
        g_tLoad = 1; SisTick(ms, 100);
        CHECK(TTabAll(held), "brake: nothing on the loading screen");
        g_tLoad = 0;
        g_bootBudgetMb = 4000; g_tRt0 = 1800ull << 20; SisTick(ms + 1000, 3000);   // render targets big enough to reach the floor
        CHECK(TTabAll(TStockFloor(2)) && g_brakeSt.t == TStockFloor(2) && g_brakeCfg.floorMb <= TStockFloor(2) && g_tVeh == CAR_STOCK && g_tPed == 250000000u,
              "world: the render targets grew on a 4 GB card: the budget lowered to the game's own, the cars follow to the game's own, the larger ped budget stands");
        g_tRt0 = 373ull << 20; SisTick(ms + 2000, 3000);
        CHECK(TTabAll(TStockFloor(2)), "world: smaller render targets never raise it");
        g_tVeh = 150000000u; SisTick(ms + 3000, 3000);
        CHECK(g_tVeh == 150000000u && g_car.target == 150000000u && g_car.fromOther == 150000000u,
              "guard: after the brake, a larger car budget from another mod still stands");
        TSizeReset(0, 23370, CAR_STOCK, PED_STOCK); TSizeSettle(1000, 3000);
        CHECK(g_sized && memcmp(g_tTab, kCtStockTable, sizeof(g_tTab)) == 0 && g_tVeh == CAR_STOCK && g_tPed == PED_STOCK && !g_brakeOn && strstr(g_sizeNote, "native"),
              "world: native Direct3D 9: the table, the car and ped budgets untouched, no brake");
        TSizeReset(1, 23370, CAR_STOCK / 3, PED_STOCK / 3); TSizeSettle(1000, 3000);
        CHECK(TTabAll(4000) && g_tVeh == CAR_STOCK / 3 && g_tPed == PED_STOCK / 3 && !g_car.on, "world: the game's low-memory car and ped budgets stay");
        TSizeReset(1, 6000, CAR_STOCK, PED_STOCK); g_tGafm[0] = 0xE9; TSizeSettle(1000, 3000);
        CHECK(g_sizedT == SizeBudget(6000, 799, 800, true, NULL) && TTabAll(g_sizedT) && strstr(g_sizeNote, "ExtraStreamingMemory"),
              "world: FusionFix's ExtraStreamingMemory: sized for its multiplier (%d)", g_sizedT);
        TSizeReset(1, 6000, CAR_STOCK, PED_STOCK); g_tTab[47] = 4000ull << 20; TSizeSettle(1000, 3000);
        CHECK(TTabAll(4000) && strstr(g_sizeNote, "larger value already in the table"), "world: a larger value already in the table stands");
        TSizeReset(1, 23370, 250000000u, PED_STOCK); TSizeSettle(1000, 3000);
        CHECK(g_tVeh == 250000000u && g_car.target == 250000000u && g_tPed == 180000000u && strstr(g_sizeNote, "another mod set kept"),
              "world: FusionFix's larger car budget, there before the world loaded, stands");
        // The value another mod set before the world loaded is its value, so the brake may not take it either. Before
        // this was recorded at sizing, the guard's floor was zero and a brake step cut it to the game's own.
        TSizeReset(1, 23370, 250000000u, PED_STOCK); TSizeSettle(1000, 3000);
        g_tV = 3300ull << 20;                                 // the brake needs a video-memory figure to hold against
        int64_t bms = 100000;
        while (g_brakeSt.t > g_sizedTStock && bms < 900000) { SisTick(bms, 100); bms += 11000; }
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
              && strstr(g_sizeNote, "2000 is the most SISCO asks for on Wine with a layout this fragmented") && strstr(g_sizeNote, "address space 1400 MB free in 17 blocks, the largest 269 MB"),
              "wine: 4000 becomes 2000, the brake holds at 134 and panics at 67, the layout is logged: %s", g_sizeNote);
        BrakeCfg wc = g_brakeCfg; BrakeState ws = { 2000, false, -1, 0 }; int wa = -1;
        CHECK(BrakeDecide(ws, 1000, 300, 110, 269, wc, &wa) == 2000 && wa == BRAKE_NONE, "wine: the Deck's 269 MB at sizing is not a hold there");
        CHECK(BrakeDecide(ws, 2000, 300, 110, 130, wc, &wa) == TStockFloor(2) && wa == BRAKE_HOLD, "wine: a fall to half of it is, to the floor (%d)", ws.t);
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); g_tWine = 1; g_tAddrSpace.largest = 269ull << 20; g_tAddrSpace.total = 1400ull << 20; g_tAddrSpace.blocks = 17; g_tTab[47] = 3000ull << 20; TSizeSettle(1000, 3000);
        CHECK(g_sized && g_sizedT == 3000, "wine: another mod's 3000 in the table stands above the cap (%d)", g_sizedT);
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); g_tWine = 1; g_tAddrSpace.largest = 2500ull << 20; g_tAddrSpace.total = 2600ull << 20; g_tAddrSpace.blocks = 2;
        TSizeSettle(1000, 3000);
        CHECK(g_sized && g_sizedT == 4000 && g_brakeCfg.holdMb == 512 && g_brakeCfg.emergencyMb == 192 && !strstr(g_sizeNote, "most SISCO asks") && strstr(g_sizeNote, "the largest 2500 MB"),
              "wine: a desktop Linux layout with a 2500 MB block is sized as Windows is, with the Windows lines, and logged (%d): %s", g_sizedT, g_sizeNote);
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); g_tWine = 1; g_tAddrSpace.largest = 1023ull << 20; g_tAddrSpace.total = 2000ull << 20; g_tAddrSpace.blocks = 5;
        TSizeSettle(1000, 3000);
        CHECK(g_sized && g_sizedT == WINE_BUDGET_MAX_MB && g_brakeCfg.holdMb == 511, "wine: one MB under the roomy line is still capped, with the lines from the block (%d, hold %d)", g_sizedT, g_brakeCfg.holdMb);
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK); TSizeSettle(1000, 3000);
        CHECK(g_sized && g_sizedT == 4000 && g_brakeCfg.holdMb == 512 && g_brakeCfg.emergencyMb == 192 && !strstr(g_sizeNote, "address space") && !strstr(g_sizeNote, "on Wine")
              && strstr(g_sizeNote, "; pool managed (read back from the game)"),
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
              && strstr(g_sizeNote, "VehicleStruct pool can use it"),
              "world: the VehicleStruct raise was refused: car held at %u MB, ped still %u MB", g_tVeh / 1000000, g_tPed / 1000000);
        SiteSet(S_VSTRUCT, ST_ON, "");

        // A player's own -availablevidmem is what the game believes it has, so it bounds the budget however big
        // the card is. Without this the budget was written past what the game could reach, and the traffic
        // budgets took their share of a pool that size.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        g_playerVidmemMb = 2048;
        TSizeSettle(1000, 3000);
        CHECK(g_sized && g_sizedT < 2048 && g_sizedT > TStockFloor(2) && strstr(g_sizeNote, "your own -availablevidmem"),
              "world: a 24 GB card with the player's own -availablevidmem 2048: budget %d MB", g_sizedT);

        // A figure larger than the card changes nothing, because the card is still the smaller of the two.
        TSizeReset(1, 3895, CAR_STOCK, PED_STOCK);
        g_playerVidmemMb = 8192;
        TSizeSettle(1000, 3000);
        CHECK(g_sized && !strstr(g_sizeNote, "your own -availablevidmem"),
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

        // The Complete Edition works out a video-memory figure of its own, measured far larger than anything SISCO would
        // write, so writing ours could only lower it. It is set on 1.0.8.0 only.
        CoreSites keepCs = g_cs;
        int keepDxvk = g_dxvk, keepState = g_dxgiState; int64_t keepBoot = g_bootBudgetMb; LONG keepCalls = g_d3dCalls;
        char keepNote[sizeof(g_d3dNote)]; memcpy(keepNote, g_d3dNote, sizeof(keepNote));
        uint32_t keepPrm[3] = { g_tPrm[0], g_tPrm[1], g_tPrm[2] };
        g_cs = dc;
        g_d3dCalls = 0; g_dxvk = -1; g_bootBudgetMb = 23370; g_dxgiState = -1;
        g_tPrm[0] = g_tPrm[1] = g_tPrm[2] = 0; g_tD3D.dxvk = 1; g_tD3D.refs = 1;
        static uint8_t ceImg[96]; memset(ceImg, 0xAB, sizeof(ceImg)); ceImg[19] = 0; memcpy(ceImg + 20, "v3.1.1", 7);   // not the test binary's own .rdata
        g_tDxvkImage = ceImg; g_tDxvkImageN = sizeof(ceImg);
        ((D3DCreate9_t)(uintptr_t)g_tIatSlot[0])(32);
        g_tDxvkImage = NULL; g_tDxvkImageN = 0;
        CHECK(g_tPrm[2] == 0 && g_tPrm[1] == (uint32_t)(uintptr_t)g_flagArg && strstr(g_d3dNote, "left to the Complete Edition"),
              "CE Direct3D: -managed set, -availablevidmem left to the game's own figure: %s", g_d3dNote);
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
        g_brakeCfg.holdMb = BRAKE_HOLD_MB; g_brakeCfg.emergencyMb = BRAKE_EMERGENCY_MB; BrakeWineLines(2000);
        CHECK(g_brakeCfg.holdMb == 512 && g_brakeCfg.emergencyMb == 192, "wine: a large block at sizing leaves the Windows lines, 512 and 192 as BRAKEN2 ran them");
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
        CHECK(strstr(an, "48 engine sound slots of the 48 SISCO declared (the game's own file 15), limit 48; the loader read 117 slots (SISCO's file has 117), audio heap 152 MiB set")
              && !strstr(an, ": the"), "engine: the audio line: %s", an);
        mCount = 21; mSummed = 91; AudioNote(ac, an, sizeof(an));
        CHECK(strstr(an, "the loader read 91 slots") && strstr(an, "the game read another slot file than waveslots_sisco.xml"), "engine: another file read is named as such: %s", an);
        uint8_t keepRd = mReader[0]; mReader[0] = 0xE9; AudioNote(ac, an, sizeof(an)); mReader[0] = keepRd;
        CHECK(strstr(an, "the slot file's reader was changed after SISCO set it"), "engine: a reader another mod hooked after SISCO is named as such: %s", an);
        mSummed = 117; AudioNote(ac, an, sizeof(an));
        CHECK(strstr(an, "found fewer engine slots in it"), "engine: the right file with too few slots is named as such: %s", an);
        mSummed = 0;
        mCount = 48; g_cs.audioSlots = (uintptr_t)&mCount; g_enginePeak = 0; memset(g_engineTable, 0, sizeof(g_engineTable));
        for (int i = 0; i < 15; i++) *(uint32_t*)(g_engineTable + 16 * i + 8) = 1;
        EngineSlotTick();
        CHECK(g_enginePeak == 15, "engine: 15 held: the peak, not over the game's own (%d)", g_enginePeak);
        for (int i = 15; i < 22; i++) *(uint32_t*)(g_engineTable + 16 * i + 8) = 2;
        EngineSlotTick();
        CHECK(g_enginePeak == 22, "engine: 22 held: a new peak over the game's 15 (%d)", g_enginePeak);
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
        CHECK(strstr(t, "15 engine sound slots") && strstr(t, "limit 25") && strstr(t, "126 MiB") && strstr(t, "no engine sound"),
              "audio: stock is read as 15 of 25 and what it costs is said: %s", t);
        capIns[2] = 50; slotCount = 50; heap = 160u << 20; memcpy(heapIns + 1, &heap, 4);
        AudioNote(c, t, sizeof(t));
        CHECK(strstr(t, "50 engine sound slots") && strstr(t, "another mod has changed them") && !strstr(t, "no engine sound"),
              "audio: an install another mod changed is named as one: %s", t);
        capIns[1] = 0xFF;                       // the Complete Edition's register, on 1.0.8.0's table
        AudioNote(c, t, sizeof(t));
        CHECK(strstr(t, "not where SISCO reads them"), "audio: the other build's loop is refused, no figure invented: %s", t);
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
