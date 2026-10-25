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

    // The last session's log is kept as .prev.log, replacing an older one; nothing to keep is not an error.
    {
        DeleteFileA("keep_test.log"); DeleteFileA("keep_test.prev.log");
        CHECK(!KeepPrevLog("keep_test.log") && GetFileAttributesA("keep_test.prev.log") == INVALID_FILE_ATTRIBUTES, "prev log: nothing to keep, nothing made");
        FILE* k = NULL; fopen_s(&k, "keep_test.log", "wb"); if (k) { fputs("first", k); fclose(k); }
        CHECK(KeepPrevLog("keep_test.log") && GetFileAttributesA("keep_test.log") == INVALID_FILE_ATTRIBUTES, "prev log: the last log moves aside");
        k = NULL; fopen_s(&k, "keep_test.log", "wb"); if (k) { fputs("second", k); fclose(k); }
        CHECK(KeepPrevLog("keep_test.log"), "prev log: an older .prev.log is replaced");
        char kb[16] = {}; k = NULL; fopen_s(&k, "keep_test.prev.log", "rb"); if (k) { fread(kb, 1, sizeof(kb) - 1, k); fclose(k); }
        CHECK(!strcmp(kb, "second"), "prev log: it holds the last session's log (%s)", kb);
        HANDLE held = CreateFileA("keep_test.log", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);   // a game still closing
        CHECK(!KeepPrevLog("keep_test.log") && GetFileAttributesA("keep_test.log") != INVALID_FILE_ATTRIBUTES, "prev log: a log still held open stays where it is");
        CloseHandle(held);
        k = NULL; fopen_s(&k, "keep_test.txt", "wb"); if (k) { fputs("x", k); fclose(k); }
        CHECK(!KeepPrevLog("keep_test.txt") && GetFileAttributesA("keep_test.txt") != INVALID_FILE_ATTRIBUTES, "prev log: only a .log name is rotated");
        DeleteFileA("keep_test.log"); DeleteFileA("keep_test.prev.log"); DeleteFileA("keep_test.txt");
        // DllMain's start: the last log kept, then this one opened empty (StartLog is what DllMain calls).
        k = NULL; fopen_s(&k, "start_test.log", "wb"); if (k) { fputs("the crashed session", k); fclose(k); }
        CHECK(StartLog("start_test.log", true), "prev log: the start opens its log");
        CloseHandle(g_log); g_log = INVALID_HANDLE_VALUE;
        char sb[32] = {}; k = NULL; fopen_s(&k, "start_test.prev.log", "rb"); if (k) { fread(sb, 1, sizeof(sb) - 1, k); fclose(k); }
        WIN32_FILE_ATTRIBUTE_DATA fa = {}; GetFileAttributesExA("start_test.log", GetFileExInfoStandard, &fa);
        CHECK(!strcmp(sb, "the crashed session") && fa.nFileSizeLow == 0, "prev log: the start keeps the crashed session's log and begins a new one (%s)", sb);
        DeleteFileA("start_test.log"); DeleteFileA("start_test.prev.log");
        // Another process that loads the plugin (the second audit, rest-6): nothing moved, its note added to the log there.
        k = NULL; fopen_s(&k, "start_test.log", "wb"); if (k) { fputs("the crashed session\r\n", k); fclose(k); }
        CHECK(StartLog("start_test.log", false), "prev log: another process opens the log too");
        SisLog("sis", "a note");
        CloseHandle(g_log); g_log = INVALID_HANDLE_VALUE;
        char ab[128] = {}; k = NULL; fopen_s(&k, "start_test.log", "rb"); if (k) { fread(ab, 1, sizeof(ab) - 1, k); fclose(k); }
        CHECK(!strncmp(ab, "the crashed session\r\n", 21) && strstr(ab, "a note") && GetFileAttributesA("start_test.prev.log") == INVALID_FILE_ATTRIBUTES,
              "prev log: another process leaves the crashed session's log where it is and adds its note (%s)", ab);
        DeleteFileA("start_test.log"); DeleteFileA("start_test.prev.log");
    }
    CHECK(OpenLog("sis_test.log"), "the log opens");
    CoreTests();
    CloseHandle(g_log); g_log = INVALID_HANDLE_VALUE;
    static char buf[262144] = {};
    FILE* f = NULL; fopen_s(&f, "sis_test.log", "rb");
    if (f) { fread(buf, 1, sizeof(buf) - 1, f); fclose(f); }
    CHECK(strstr(buf, "  direct3d  DXVK") && !strstr(buf, "  direct3d  DXVK (assumed") && strstr(buf, "  size      budget ") && strstr(buf, "(card 23370 MB, render targets 799 MB")
          && strstr(buf, "; car 40 -> 200 MB, ped 50 -> 180 MB") && strstr(buf, "  brake     held at 18 s: budget 4000 -> 2870 MB") && strstr(buf, "  brake     stepped at 39 s: budget 2870 -> 2742 MB")
          && strstr(buf, "  budget    car 200 -> ") && strstr(buf, " MB (with the world budget)")
          && strstr(buf, "  brake     ceiling set at 2870 MB: 300 s of play") && strstr(buf, "  brake     raised at 697 s: budget 1974 -> 2102 MB (ceiling 2870 MB")
          && strstr(buf, " MB (back with the world budget)")
          && strstr(buf, "  memory    game figures: video 8192 MB, system 32420 MB")
          && strstr(buf, "  memory    clamp at 25 s (in play): video 8192 -> 3104 MB, system 32420 -> 2000 MB")
          && strstr(buf, "after a failed Direct3D create (both moved: a managed texture)") && strstr(buf, "(free less 288 MB) now on")
          && strstr(buf, "  brake     followed the clamp at 25 s: budget 4000 -> 2305 MB (video figure 3104 less render targets 799 MB")
          && strstr(buf, "  memory    figures restored at a device reset")
          && strstr(buf, "  brake     clamp needs no write at 33 s")
          && strstr(buf, "(system only: a system-memory texture, a managed texture already under its video figure, or out of system memory)")
          && strstr(buf, "from here clamps are counted, not logged (nor the brake's follows)") && strstr(buf, "10 clamps so far")
          && strstr(buf, "MB (2 clamps so far)") && strstr(buf, "MB (1 clamp so far)"),
          "the release's log names what it found and what it did");
    CHECK(strstr(buf, "taxi skips NOT covered (the frame count did not read as expected); the main menu held to 30 fps (the menu flag did not read as expected)"),
          "present: the hook line says when the frame count and the menu flag are not taken (the harness hooks with neither site planted)");
    CHECK(strstr(buf, "taxi-skip throttle off at 12 s: the frame count stood still 5 s at 120 presents/s")
          && strstr(buf, "taxi-skip throttle on again at 14 s (the frame count moves)"),
          "present: the frame count's stop and restart are logged");
    CHECK(strstr(buf, "clamp restored before the follow (at 45 s): video figure 3104 less render targets 799 MB; raises stop at 2305 MB, budget stays"),
          "figures: a clamp restored before the follow is logged with the clamp's own figure");
    CHECK(strstr(buf, "taxi-skip throttle: 3.0 s, ") && strstr(buf, " presents skipped (+1 more just before)"),
          "present: the throttle's line, and two episodes in one second say so");
    CHECK(strstr(buf, "clamp needs no write at 43 s") && !strstr(buf, "clamp needs no write at 45 s") && !strstr(buf, "followed the clamp at 47 s"),
          "figures: the brake's follow lines stop with the clamp lines after the eighth");
    CHECK(strstr(buf, "render targets now 799 MB at 25 s: budget 3039 -> 3497 MB (the card allows it again)")
          && strstr(buf, "budget 3039 -> 3300 MB (the card allows it again, up to the game's clamp)")
          && strstr(buf, "the card allows 3497 MB again; budget stays 2870 MB (brake holding)"),
          "resize: the give-back's lines");
    CHECK(strstr(buf, "texture quality low at 30 s: brake floor 550 -> 300 MB")
          && strstr(buf, "texture quality medium at 32 s: brake floor 550 -> 350 MB; budget stays under it (brake holding)"),
          "quality: the floor's lines");
    CHECK(strstr(buf, "texture quality high at 21 s: budget 300 -> 550 MB (the game's own), brake floor 300 -> 550 MB"),
          "quality: a raise that brings the budget up says so");
    {   // one line per change, not one a tick (rest-3): the cases change the quality three times
        int ql = 0; for (const char* q = buf; (q = strstr(q, "texture quality ")) != NULL; q++) ql++;
        CHECK(ql == 7, "quality: exactly one line per change, seven changes (%d)", ql);
        int ca = 0; for (const char* q = buf; (q = strstr(q, "MB again; budget stays")) != NULL; q++) ca++;
        CHECK(ca == 2, "resize: a give-back line that gives nothing for each of the two held cases; a large card's drop says nothing (%d)", ca);
        int ea = 0; for (const char* q = buf; (q = strstr(q, " engine sounds at once (the game's own 15 slots)")) != NULL; q++) ea++;
        CHECK(ea == 2 && strstr(buf, "  audio     22 engine sounds at once (the game's own 15 slots)") && strstr(buf, "  audio     25 engine sounds at once"),
              "engine: two peak lines, 22 at once and 25 after the minute (%d)", ea);
    }
    CHECK(strstr(buf, "taxi-skip throttle on at ") != NULL, "present: the throttle says once when it first trusts the frame count");
    CHECK(!strstr(buf, "clamp needs no write at 35 s"), "figures: the system-only clamp gives the brake nothing to follow, so no brake line at 35 s");
    CHECK(strstr(buf, "; brake 4000 MB, ceiling 0 MB; clamp 1)") && strstr(buf, "; no brake; clamp 1)") && !strstr(buf, "brake 0 MB"),
          "figures: the clamp line gives the brake's budget, or says there is no brake");
    // One line when a load starts and one at its verdict, not four (the owner, 2026-09-28).
    CHECK(strstr(buf, "  load      loading screen at ") && strstr(buf, "  load      world built ") && strstr(buf, " after the player (loading screen ")
          && strstr(buf, " presents skipped until +") && strstr(buf, "  load      world build end not seen in 90 s after the player (render targets never held 3 s")
          && strstr(buf, "  load      menu at ") && strstr(buf, "render targets 63 -> 468 MB") && !strstr(buf, "  world     ") && !strstr(buf, "  loading   ") && !strstr(buf, "presents go out again"),
          "load: a line at the loading screen, one at the verdict with what it started from, one for a return to the menu");
    CHECK(strstr(buf, "  memory    game figures: video 3104 MB (its own answer 8192 MB), system 2000 MB") && !strstr(buf, "(its own answer 8192 MB), system 32420"),
          "memory: the game's own answer is named only where it is not the figure in force");
    CHECK(strstr(buf, "  memory    game figures: video 2048 MB (not the 15209 MB SISCO set), system 63092 MB")
          && strstr(buf, "  memory    game figures: video 15209 MB, system 63092 MB")
          && strstr(buf, "  size      budget 550 -> 935 MB (card 15209 MB, render targets 1113 MB, the card allows 11865); the game's own video figure is 2048 MB: "
                         "the budget stays under it less the render targets; pool managed; car 40 -> "),
          "memory: a figure the game did not take from SISCO is named on both lines, one it took on neither");
    CHECK(strstr(buf, "  memory    game figures: video 3072 MB, system 31726 MB")
          && strstr(buf, "  size      budget 550 -> 1915 MB (card 20134 MB, render targets 1157 MB, the card allows 16449); the game's own video figure is 3072 MB: "
                         "the budget stays under it less the render targets; pool managed; car 40 -> ")
          && strstr(buf, "; brake lowers only (Wine, lines lowered); address space 103 MB free in 16 blocks, the largest 15 MB (on Proton, PROTON_USE_WOW64=1 leaves the game far more)"),
          "wine: a Proton player's run reads on the log as it sized: the game's figure, the layout, the pointer to WoW64");
    CHECK(strstr(buf, "  brake     held on a fast fall at 31 s: budget 4000 -> 3257 MB (V 3878 MB, largest free 398 MB")
          && strstr(buf, "; it fell 122 MB in one second)")
          && strstr(buf, "  brake     FAST fall, two steps at 32 s: budget 3257 -> 3001 MB (V 3878 MB, largest free 290 MB")
          && strstr(buf, "; it fell 108 MB in one second)"),
          "fast: the hold on a fast fall and the double step are on the log with what fell");
    CHECK(!strstr(buf, "  built     "), "sizing: the read-back line is gone; a raise another plugin undid has its own site line");
    CHECK(strstr(buf, "LEFT AT THE GAME'S OWN: the link pool was not raised") && !strstr(buf, ", the game's own (card 23370 MB, render targets 799 MB, the card allows 20050), LEFT"),
          "size: a budget left at the game's own says so once");
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
