// sis_test.exe: the core's tests on the release's own compile, and the release's log. Exit code 0 = all passed.
#define SISCO_TEST 1
#include "SIS.cpp"

#include <stdlib.h>

static int g_fail, g_pass;
#define CHECK(cond, ...) do { if (cond) g_pass++; else { g_fail++; printf("FAIL %s:%d  ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#include "../core/sis_core_tests.h"

int main() {
    g_base = (uintptr_t)GetModuleHandleA(NULL);
    QueryPerformanceFrequency(&g_qpf); QueryPerformanceCounter(&g_qpc0);
    InitializeCriticalSection(&g_logCs);
    InitializeCriticalSection(&g_spillCs);

    CHECK(OpenLog("sis_test.log"), "the log opens");
    CoreTests();
    CloseHandle(g_log); g_log = INVALID_HANDLE_VALUE;
    static char buf[65536] = {};
    FILE* f = NULL; fopen_s(&f, "sis_test.log", "rb");
    if (f) { fread(buf, 1, sizeof(buf) - 1, f); fclose(f); }
    CHECK(strstr(buf, "  direct3d  DXVK (it answered its interop interface)") && strstr(buf, "  size      DXVK; card budget 23370 MB")
          && strstr(buf, "  brake     held at 18 s: budget 4000 -> 2870 MB") && strstr(buf, "  brake     stepped at 39 s: budget 2870 -> 2742 MB")
          && strstr(buf, "  budget    the car budget follows the world's: 200 -> "),
          "the release's log names what it found and what it did");
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
