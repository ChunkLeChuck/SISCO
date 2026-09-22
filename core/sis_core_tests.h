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
static void* g_tD3DVtbl[3] = { (void*)FakeD3DQI, (void*)FakeD3DAddRef, (void*)FakeD3DRelease };
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
static uint32_t g_tQual = 2, g_tStreamN, g_tLoad, g_tVeh, g_tPed;
static uint8_t g_tGafm[4] = { 0x55, 0x8B, 0xEC, 0x83 };
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
    g_cs.playerInfo = (uintptr_t)g_tPlayerInfo; g_cs.playerPedOff = 0x58Cu; TPlayerInWorld(true);
    g_sized = false; g_dxvk = dxvk; g_bootBudgetMb = bootMb; g_dxgiState = -1;   // no DXGI: the sizing uses the boot budget
    g_brakeOn = 0; g_brakeSt.t = 0; g_brakeSt.held = false; g_brakeCfg.floorMb = 800;
    g_car.on = g_ped.on = false;
    g_car.target = g_car.written = g_car.fromOther = g_ped.target = g_ped.written = g_ped.fromOther = 0;
    // The budget is only raised behind the two pools and the queue fix; these cases are about the sizing itself, so
    // they start from an install where all three went in. The case that they were refused is its own test below.
    SiteSet(S_LINK_SIZE, ST_ON, ""); SiteSet(S_SLOT_SIZE, ST_ON, ""); g_fixInstalled = 1;
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
static int TStockFloor(int q) {
    uint64_t lo = 0;
    for (int row = 12; row <= 15; row++) { uint64_t v = kCtStockTable[3 * row + q]; if (!lo || v < lo) lo = v; }
    return (int)(lo >> 20);
}

// The sizing waits for the render targets to settle: three ticks with the same X and no loading screen.
static void TSizeSettle(int64_t ms0, int64_t lMb) { for (int i = 0; i < 3; i++) SisTick(ms0 + i * 1000, lMb); }

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
        // DXVK predicted from FusionFix's d3d9.cfg, for the arena
        char tmp[MAX_PATH], sub[40]; GetTempPathA(MAX_PATH, tmp);        // one folder per process: harnesses run side by side
        _snprintf_s(sub, sizeof(sub), _TRUNCATE, "sis_core_test_%lu", GetCurrentProcessId()); strcat_s(tmp, sub); CreateDirectoryA(tmp, NULL);
        char cfg[MAX_PATH]; _snprintf_s(cfg, MAX_PATH, _TRUNCATE, "%s\\d3d9.cfg", tmp); DeleteFileA(cfg);
        CHECK(PredictDxvk(tmp) == 0, "prediction: no d3d9.cfg: native Direct3D 9");
        FILE* f = NULL; fopen_s(&f, cfg, "wb"); if (f) { fputs("[MAIN]\r\nAPI = 1\r\n", f); fclose(f); }
        CHECK(PredictDxvk(tmp) == 1, "prediction: d3d9.cfg API = 1: DXVK");
        f = NULL; fopen_s(&f, cfg, "wb"); if (f) { fputs("[MAIN]\r\nAPI = 0\r\n", f); fclose(f); }
        CHECK(PredictDxvk(tmp) == 0, "prediction: d3d9.cfg API = 0: native");
        DeleteFileA(cfg); RemoveDirectoryA(tmp);
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
        CHECK(CarPedFor(CAR_STOCK, 4000, 800) == 200000000u && CarPedFor(PED_STOCK, 2000, 800) == 200000000u && CarPedFor(CAR_STOCK, 800, 800) == CAR_STOCK
              && CarPedFor(PED_STOCK, 300, 800) == PED_STOCK && CarPedFor(CAR_STOCK, 1400, 800) == 120000000u,
              "rule: car and ped budgets 200 MB from a 2000 MB budget, the game's own at its stock budget, linear between");
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
        g_tRt0 = 63ull << 20; SisTick(11000, 3000);
        g_tRt0 = 468ull << 20; SisTick(12000, 3000);
        g_tRt0 = 799ull << 20; SisTick(13000, 3000); SisTick(14000, 3000);
        CHECK(!g_sized, "world: nothing while the render targets are still growing (63 -> 468 -> 799 MB, as in V1P)");
        g_tRt0 = 468ull << 20; SisTick(15000, 3000);
        CHECK(!g_sized, "world: a change in the render targets starts the wait again");
        SisTick(16000, 3000);
        CHECK(!g_sized, "world: one steady tick after a change is not enough");
        g_tRt0 = 799ull << 20; SisTick(17000, 3000); SisTick(18000, 3000); SisTick(19000, 3000);
        CHECK(g_sized && g_sizedX == 799 && TTabAll(4000) && g_sizedT == 4000 && g_brakeOn && g_brakeCfg.floorMb == TStockFloor(2),
              "world: sized once they settled, at the full size (X %d): %s", g_sizedX, g_sizeNote);
        CHECK(g_tVeh == 200000000u && g_tPed == 200000000u && g_car.on && g_ped.on, "world: car and ped budgets 200 MB (%u, %u)", g_tVeh, g_tPed);
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
        uint32_t wantCar = CarPedFor(CAR_STOCK, g_brakeSt.t, TStockFloor(2));
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
        CHECK(g_tVeh == 250000000u && g_car.target == 250000000u && g_tPed == 200000000u && strstr(g_sizeNote, "from FusionFix kept"),
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

        // The budget is only raised behind the pools and the queue fix that were raised to carry it.
        TSizeReset(1, 23370, CAR_STOCK, PED_STOCK);
        SiteSet(S_LINK_SIZE, ST_OFF_MISMATCH, "");
        TSizeSettle(1000, 3000);
        CHECK(g_sized && memcmp(g_tTab, kCtStockTable, sizeof(g_tTab)) == 0 && g_sizedT == TStockFloor(2) && strstr(g_sizeNote, "the link pool was not raised"),
              "world: the table is left exactly as the game had it when the link pool was not raised: %s", g_sizeNote);
        SiteSet(S_LINK_SIZE, ST_ON, "");

        TSizeReset(1, 3895, CAR_STOCK, PED_STOCK); TSizeSettle(1000, 3000);
        CHECK(g_sizedT >= 1650 && g_sizedT <= 1665 && g_tVeh == CarPedFor(CAR_STOCK, g_sizedT, TStockFloor(2)) && g_tPed == CarPedFor(PED_STOCK, g_sizedT, TStockFloor(2))
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
        ((D3DCreate9_t)(uintptr_t)g_tIatSlot[0])(32);
        CHECK(g_tPrm[2] == 0 && g_tPrm[1] == (uint32_t)(uintptr_t)g_flagArg && strstr(g_d3dNote, "left to the Complete Edition"),
              "CE Direct3D: -managed set, -availablevidmem left to the game's own figure: %s", g_d3dNote);
        // Everything this case touched goes back: the cases after it read the Direct3D state this one overwrote.
        g_cs = keepCs; g_dxvk = keepDxvk; g_dxgiState = keepState; g_bootBudgetMb = keepBoot; g_d3dCalls = keepCalls;
        memcpy(g_d3dNote, keepNote, sizeof(g_d3dNote));
        g_tPrm[0] = keepPrm[0]; g_tPrm[1] = keepPrm[1]; g_tPrm[2] = keepPrm[2];
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
        char t[200]; AudioNote(c, t, sizeof(t));
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
        uint8_t keep[3]; memcpy(keep, capIns, 3);
        CHECK(memcmp(keep, capIns, 3) == 0 && heapIns[0] == 0xBE, "audio: nothing was written back");
    }
    // ---- the install order: native predicted leaves the arena alone; a launch-option reader that differs keeps the
    // Direct3D hook out. The queue and lock sites are empty here (their installs are tested above); the globals they set
    // are put back.
    {
        uintptr_t sOD = g_oDrain, sCA = g_cacheAddr; int sFI = g_fixInstalled, sLF = g_linkFixInstalled;
        static Site sSites[S_COUNT]; memcpy(sSites, g_site, sizeof(sSites)); CoreSites sCs = g_cs;
        CoreSites z = {};
        SitesShape1080(z);
        SisLoadResult lr = SisInstall(z, false);
        CHECK(!lr.queue && !lr.lock && !lr.race && !lr.arena && g_site[S_ARENA_SIZE].state == ST_OFF_DISABLED && !lr.params && !lr.d3d
              && g_site[S_D3D_IAT].state == ST_OFF_MISMATCH, "install: the arena left alone for native, no Direct3D hook without its readers, nothing on empty sites");
        g_oDrain = sOD; g_cacheAddr = sCA; g_fixInstalled = sFI; g_linkFixInstalled = sLF; memcpy(g_site, sSites, sizeof(sSites)); g_cs = sCs;
    }
}
