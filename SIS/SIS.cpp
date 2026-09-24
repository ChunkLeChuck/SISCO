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
static char g_slowFile[MAX_PATH] = "";   // plugins\SISCO-SLOW-LOAD.txt: written once per slow load, removed at start

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

// The file a player notices when a load was slow: what SISCO saw, and what to do. Plain text, beside the log.
static void WriteSlowLoadFile() {
    if (!g_slowFile[0]) return;
    HANDLE f = CreateFileA(g_slowFile, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    char cap[48] = "none", vs[48] = "no";
    if (g_frameCap > 0) _snprintf_s(cap, sizeof(cap), _TRUNCATE, "%d fps (%s)", g_frameCap, g_frameCapFrom);
    if (g_forcedInterval >= 1) _snprintf_s(vs, sizeof(vs), _TRUNCATE, "yes (d3d9.presentInterval %d)", g_forcedInterval);
    char text[1400];
    int n = _snprintf_s(text, sizeof(text), _TRUNCATE,
        "SISCO: the last load was slow.\r\n\r\n%s\r\n\r\n"
        "What SISCO could see: RTSS %s; DXVK's own frame cap %s; VSync forced in DXVK's config: %s; "
        "SISCO's own presenting without VSync while the world builds: %s.\r\n"
        "The full record is in SISCO.log beside this file. This file is written again only when a load is slow again.\r\n",
        g_slowLoadNote, RtssLoaded() ? "loaded" : "not loaded", cap, vs,
        !g_swapVtblHooked ? "not in effect (the Present hook did not go in; the log's present line says why)"
        : g_forcedInterval >= 1 ? "overridden by that forced VSync" : "was in effect, so VSync itself is not the cause");
    DWORD w; if (n > 0) WriteFile(f, text, (DWORD)n, &w, NULL);
    CloseHandle(f);
    SisLog("slowload", "written to %s", g_slowFile);
}
static DWORD WINAPI Worker(void*) {
    for (;;) {
        Sleep(1000);
        SisTick(NowMs(), g_brakeOn ? (int64_t)(LargestFreeBlock() >> 20) : -1);
        if (g_slowLoadPending && InterlockedExchange(&g_slowLoadPending, 0)) WriteSlowLoadFile();
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
    // The slow-load file is the log's stem plus a longer suffix: a name that does not fit is never written at all.
    if (_snprintf_s(g_slowFile, sizeof(g_slowFile), _TRUNCATE, "%.*s-SLOW-LOAD.txt", (int)(dot ? dot - path : (ptrdiff_t)strlen(path)), path) < 0) g_slowFile[0] = 0;
    if (g_slowFile[0]) DeleteFileA(g_slowFile);              // only ever about this run
    // SISCO.ini sits beside the plugin and its log. Every key defaults to on, so a missing file, a missing key,
    // a typo or an unreadable value all leave the mod behaving exactly as it does with no file at all.
    char ini[MAX_PATH]; strcpy_s(ini, path);
    char* ext = strrchr(ini, '.'); if (ext) strcpy_s(ext, MAX_PATH - (ext - ini), ".ini");
    bool haveIni = GetFileAttributesA(ini) != INVALID_FILE_ATTRIBUTES;
    g_set = ReadSettings(ini);
    SisLog("sis", "SISCO %s (build %s), the Streaming Issue Solver: Crash Override, for GTA IV 1.0.8.0 and the Complete Edition",
           SIS_VERSION, SISCO_BUILD);
    char why[160] = "";
    if (!IsKnownBuild(why, sizeof(why))) {
        SisLog("sis", "this is neither GTA IV 1.0.8.0 nor the Complete Edition (%s): SISCO changes nothing", why);
        if (g_log != INVALID_HANDLE_VALUE) { CloseHandle(g_log); g_log = INVALID_HANDLE_VALUE; }
        return TRUE;
    }
    SisLog("sis", "the game is %s", g_buildName);
    // Always logged, on or off, so a bug report can never hide a setting and nobody has to be asked for one.
    char sw[140] = "";
    if (!g_set.enabled) strcpy_s(sw, "everything OFF (Enabled=0)");
    else if (g_set.fixes && g_set.limits && g_set.budget) strcpy_s(sw, "everything on");
    else _snprintf_s(sw, sizeof(sw), _TRUNCATE, "OFF: %s%s%s", g_set.fixes ? "" : "Fixes ",
                     g_set.limits ? "" : "Limits ", g_set.budget ? "" : "Budget ");
    SisLog("settings", "%s (%s)", sw, haveIni ? "SISCO.ini" : "no SISCO.ini, so the defaults");
    if (!g_set.enabled) {
        SisLog("sis", "SISCO changes nothing this run. Set Enabled=1 in SISCO.ini to turn it back on.");
        return TRUE;
    }
    // DXVK's config, read as DXVK 3.1 reads it: DXVK_CONFIG_FILE if set, else dxvk.conf in the game's working folder
    // (the exe's, from Steam and a shortcut alike), then the DXVK_CONFIG variable over it. A cap or a forced VSync
    // found there holds -managed and the budget (the size line says so); the driver's cap and RTSS's figure are not
    // visible, RTSS itself is (its module, at Direct3DCreate9).
    {
        char exe[MAX_PATH] = "", conf[MAX_PATH] = "";
        GetModuleFileNameA(NULL, exe, MAX_PATH);
        const char* exeName = strrchr(exe, '\\'); exeName = exeName ? exeName + 1 : exe;
        const char* confFrom = "DXVK_CONFIG_FILE";
        if (!GetEnvironmentVariableA("DXVK_CONFIG_FILE", conf, MAX_PATH) || !conf[0]) {
            _snprintf_s(conf, MAX_PATH, _TRUNCATE, "%.*sdxvk.conf", (int)(exeName - exe), exe);
            confFrom = "dxvk.conf";
        }
        DxvkConf dc = { CONF_UNSET, CONF_UNSET, CONF_UNSET, "", "", "" };
        static char text[16384];                              // a dxvk.conf is a few KB; a larger one is read this far
        HANDLE cf = CreateFileA(conf, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        bool haveConf = false;
        if (cf != INVALID_HANDLE_VALUE) {
            DWORD n = 0;
            if (ReadFile(cf, text, sizeof(text) - 1, &n, NULL)) { DxvkConfParse(text, n, '\n', exeName, confFrom, &dc); haveConf = true; }
            CloseHandle(cf);
        }
        char env[1024] = "";
        DWORD en = GetEnvironmentVariableA("DXVK_CONFIG", env, sizeof(env));
        if (en && en < sizeof(env)) DxvkConfParse(env, en, ';', exeName, "DXVK_CONFIG", &dc);
        int cap = dc.dxvkCap != CONF_UNSET ? dc.dxvkCap : dc.d3d9Cap;
        if (cap != CONF_UNSET && cap > 0) { g_frameCap = cap; strcpy_s(g_frameCapFrom, dc.dxvkCap != CONF_UNSET ? dc.dxvkCapFrom : dc.d3d9CapFrom); }
        if (dc.interval != CONF_UNSET && dc.interval >= 0) g_forcedInterval = dc.interval;
        char capText[64] = "none", vsText[64] = "not forced";
        if (g_frameCap) _snprintf_s(capText, sizeof(capText), _TRUNCATE, "%d fps (%s)", g_frameCap, g_frameCapFrom);
        if (g_forcedInterval >= 0) _snprintf_s(vsText, sizeof(vsText), _TRUNCATE, "forced, d3d9.presentInterval %d (%s)", g_forcedInterval, dc.intervalFrom);
        SisLog("dxvkconf", "%s for %s: DXVK's own frame cap %s, VSync %s%s", haveConf ? confFrom : "no dxvk.conf", exeName, capText, vsText,
               g_frameCap || g_forcedInterval >= 1 ? ": -managed will not be set and the budget stays the game's own (the size line says why)"
               : "; the driver's cap and RTSS's figure cannot be seen, the load line shows the build's rate");
    }
    SisLoadResult r = SisInstall(GameCoreSites());
    char a[128], b[128], c[128], d[128];
    if (!g_set.fixes) SisLog("fixes", "off (Fixes=0 in SISCO.ini)");
    else SisLog("fixes", "release queue %s; link-pool lock %s; startup race %s", r.queue ? "ON" : "OFF", r.lock ? "ON" : "OFF", r.race ? "ON" : "OFF");
    if (!g_set.limits) SisLog("limits", "off (Limits=0 in SISCO.ini)");
    else SisLog("limits", "VehicleStruct %s; drawable slots %s; links %s; arena in KB %s",
                SiteText(S_VSTRUCT, a, sizeof(a)), SiteText(S_SLOT_SIZE, b, sizeof(b)), SiteText(S_LINK_SIZE, c, sizeof(c)),
                SiteText(S_ARENA_SIZE, d, sizeof(d)));
    SisLog("direct3d", "hook %s", SiteText(S_D3D_IAT, a, sizeof(a)));
    // Every site of the three fixes, named only when one of them is off: the lines above say which fix, this says
    // which site and why. The sites after this one carry their own reason in the lines above.
    for (int s = 0; s <= S_NATIVE_INIT; s++)
        if (g_site[s].state != ST_ON && g_site[s].state != ST_ABSENT && g_site[s].state != ST_NOTTRIED) SisLog("site", "%s %s", kSiteName[s], SiteText(s, a, sizeof(a)));
    if (!g_set.budget) return TRUE;                  // the worker only sizes, guards and brakes
    HANDLE t = CreateThread(NULL, 64 * 1024, Worker, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (t) CloseHandle(t);
    else SisLog("sis", "the worker thread did not start: no sizing and no brake (the fixes stay)");
    return TRUE;
}
#endif
