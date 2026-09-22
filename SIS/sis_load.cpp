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
    DeleteFileA(log);
    HMODULE h = LoadLibraryA(argv[1]);
    if (!h) { printf("LoadLibrary failed: %lu\n", GetLastError()); return 3; }
    Sleep(300);
    FreeLibrary(h);
    FILE* fp = _fsopen(log, "rb", _SH_DENYNO); if (!fp) { printf("no log at %s\n", log); return 4; }
    char buf[4096] = {}; fread(buf, 1, sizeof(buf) - 1, fp); fclose(fp);
    printf("%s", buf);
    bool refused = strstr(buf, "this is neither GTA IV 1.0.8.0 nor the Complete Edition") != NULL && strstr(buf, "SISCO changes nothing") != NULL;
    bool acted = strstr(buf, "fixes") != NULL || strstr(buf, "limits") != NULL;
    if (!refused || acted) { printf("\nFAIL: SISCO did not stay passive in a non-game process\n"); return 1; }
    printf("\nok: passive in a non-game process\n");
    return 0;
}
