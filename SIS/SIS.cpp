// SISCO.asi: the Streaming Issue Solver: Crash Override, for GTA IV 1.0.8.0 and the Complete Edition.
//
// GTA IV streams the city (buildings, cars, peds) through a memory budget set for the video cards of 2008. Raised, the
// game runs into limits it never reached before: a release queue with no bounds check, a spinlock its own eviction takes
// twice (the freeze), a link pool that runs dry, and the 32-bit address space. SISCO fixes those, sizes the budget to the
// card, raises the car and ped budgets for more variety, and keeps the address space safe while you play.
//
// Everything that does the work is in core/sis_core.h. This file loads it, writes a short log next to the .asi
// (plugins\SISCO.log) and ticks it once a second. Nothing to set up: no launch option, no ini.
#include "../core/sis_core.h"

static HANDLE g_log = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_logCs;

static void SisLog(const char* key, const char* fmt, ...) {
    if (g_log == INVALID_HANDLE_VALUE) return;
    char v[1200]; SYSTEMTIME t; GetLocalTime(&t);
    int n = _snprintf_s(v, sizeof(v) - 2, _TRUNCATE, "%02d:%02d:%02d  %-9s ", t.wHour, t.wMinute, t.wSecond, key);
    if (n < 0) n = 0;
    va_list ap; va_start(ap, fmt); _vsnprintf_s(v + n, sizeof(v) - 2 - n, _TRUNCATE, fmt, ap); va_end(ap);
    n = (int)strlen(v); v[n++] = '\r'; v[n++] = '\n';
    EnterCriticalSection(&g_logCs);
    DWORD w; WriteFile(g_log, v, (DWORD)n, &w, NULL);
    LeaveCriticalSection(&g_logCs);
}
static bool OpenLog(const char* path) {
    g_log = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    return g_log != INVALID_HANDLE_VALUE;
}
// A site's line for the log: what it did, or why it is off.
static const char* SiteText(int s, char* buf, size_t cap) {
    if (g_site[s].state == ST_ON) _snprintf_s(buf, cap, _TRUNCATE, "%s", g_site[s].note);
    else _snprintf_s(buf, cap, _TRUNCATE, "%s (%s)", StName(g_site[s].state), g_site[s].note);
    return buf;
}

static DWORD WINAPI Worker(void*) {
    for (;;) {
        Sleep(1000);
        SisTick(NowMs(), g_brakeOn ? (int64_t)(LargestFreeBlock() >> 20) : -1);
    }
}

#ifndef SISCO_TEST
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(h);
    // One copy only. A loader that scans both the game folder and plugins\ would otherwise run two of everything,
    // and the two workers would size, brake and guard the same budget table from their own separate state.
    if (!CreateMutexA(NULL, FALSE, "SISCO.asi.single") || GetLastError() == ERROR_ALREADY_EXISTS) return TRUE;
    g_base = (uintptr_t)GetModuleHandleA(NULL);
    QueryPerformanceFrequency(&g_qpf); QueryPerformanceCounter(&g_qpc0);
    InitializeCriticalSection(&g_logCs);
    InitializeCriticalSection(&g_spillCs);   // never deleted: the game pushes and drains during shutdown
    char path[MAX_PATH] = "";
    DWORD pathN = GetModuleFileNameA(h, path, MAX_PATH);
    if (!pathN || pathN >= MAX_PATH - 4) return TRUE;         // no room to turn it into a .log name: nothing to write to
    char* dot = strrchr(path, '.'); if (dot) strcpy_s(dot, MAX_PATH - (dot - path), ".log");
    OpenLog(path);
    SisLog("sis", "SISCO %s (build %s), the Streaming Issue Solver: Crash Override, for GTA IV 1.0.8.0 and the Complete Edition",
           SIS_VERSION, SISCO_BUILD);
    char why[160] = "";
    if (!IsKnownBuild(why, sizeof(why))) {
        SisLog("sis", "this is neither GTA IV 1.0.8.0 nor the Complete Edition (%s): SISCO changes nothing", why);
        if (g_log != INVALID_HANDLE_VALUE) { CloseHandle(g_log); g_log = INVALID_HANDLE_VALUE; }
        return TRUE;
    }
    SisLog("sis", "the game is %s", g_buildName);
    char dir[MAX_PATH]; GetModuleFileNameA(NULL, dir, MAX_PATH);
    char* sl = strrchr(dir, '\\'); if (sl) *sl = 0;
    int predicted = PredictDxvk(dir);
    SisLoadResult r = SisInstall(GameCoreSites(), predicted == 1);
    char a[128], b[128], c[128], d[128];
    SisLog("fixes", "release queue %s; link-pool lock %s; startup race %s", r.queue ? "ON" : "OFF", r.lock ? "ON" : "OFF", r.race ? "ON" : "OFF");
    SisLog("limits", "VehicleStruct %s; drawable slots %s; links %s; arena in KB %s (DXVK %s from d3d9.cfg)",
           SiteText(S_VSTRUCT, a, sizeof(a)), SiteText(S_SLOT_SIZE, b, sizeof(b)), SiteText(S_LINK_SIZE, c, sizeof(c)),
           SiteText(S_ARENA_SIZE, d, sizeof(d)), predicted == 1 ? "predicted" : "not predicted");
    SisLog("direct3d", "hook %s", SiteText(S_D3D_IAT, a, sizeof(a)));
    // Every site of the three fixes, named only when one of them is off: the lines above say which fix, this says
    // which site and why. The sites after this one carry their own reason in the lines above.
    for (int s = 0; s <= S_NATIVE_INIT; s++)
        if (g_site[s].state != ST_ON && g_site[s].state != ST_ABSENT) SisLog("site", "%s %s", kSiteName[s], SiteText(s, a, sizeof(a)));
    HANDLE t = CreateThread(NULL, 64 * 1024, Worker, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (t) CloseHandle(t);
    else SisLog("sis", "the worker thread did not start: no sizing and no brake (the fixes stay)");
    return TRUE;
}
#endif
