// sis_load.exe: loads the real SISCO.asi into a process that is not GTA IV. It must recognise the wrong build, say so in
// its log (next to the .asi) and change nothing.
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <share.h>

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: sis_load <SISCO.asi>\n"); return 2; }
    char log[MAX_PATH]; strcpy_s(log, argv[1]);
    char* dot = strrchr(log, '.'); if (dot) strcpy_s(dot, MAX_PATH - (dot - log), ".log");
    // A log already there (the crashed session's) stays where it is and gets the note on its end: only the game moves it aside.
    char prev[MAX_PATH]; strcpy_s(prev, log); char* pd = strrchr(prev, '.'); if (pd) strcpy_s(pd, MAX_PATH - (pd - prev), ".prev.log");
    DeleteFileA(prev);
    FILE* mk = NULL; fopen_s(&mk, log, "wb"); if (mk) { fputs("the crashed session\r\n", mk); fclose(mk); }
    HMODULE h = LoadLibraryA(argv[1]);
    if (!h) { printf("LoadLibrary failed: %lu\n", GetLastError()); return 3; }
    Sleep(300);
    FreeLibrary(h);
    FILE* fp = _fsopen(log, "rb", _SH_DENYNO); if (!fp) { printf("no log at %s\n", log); return 4; }
    char buf[4096] = {}; fread(buf, 1, sizeof(buf) - 1, fp); fclose(fp);
    printf("%s", buf);
    bool refused = strstr(buf, "not GTA IV 1.0.8.0 or 1.2.0.59, the two versions SISCO supports") != NULL && strstr(buf, "nothing changed") != NULL;
    bool acted = strstr(buf, "fixes") != NULL || strstr(buf, "limits") != NULL;
    if (!refused || acted) { printf("\nFAIL: SISCO did not stay passive in a non-game process\n"); return 1; }
    bool kept = !strncmp(buf, "the crashed session\r\n", 21) && GetFileAttributesA(prev) == INVALID_FILE_ATTRIBUTES;
    DeleteFileA(log); DeleteFileA(prev);
    if (!kept) { printf("\nFAIL: a non-game process moved the log aside or replaced it\n"); return 1; }
    printf("\nok: passive in a non-game process\n");
    return 0;
}
