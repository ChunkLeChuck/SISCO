// sis_build.exe: IsKnownBuild on real executables, mapped into this process the way the loader maps them (sections at
// their addresses, relocations applied for the base they land on). The release's own compile of the core, so the real
// site tables are built too. A build is named by its code: the stock header is not needed, another header is named in
// the log and changes nothing, and code SISCO did not read is refused whatever the header says.
// Usage: sis_build <1.0.8.0 GTAIV.exe> <ce-merged.exe> [<the Complete Edition as installed> [<any other GTAIV.exe>]]
// Exit code 0 = all passed.
#include "SIS.cpp"
#include <stdio.h>

static int g_fail, g_pass;
#define CHECK(cond, ...) do { if (cond) g_pass++; else { g_fail++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static IMAGE_NT_HEADERS* NtOf(uint8_t* img) { return (IMAGE_NT_HEADERS*)(img + ((IMAGE_DOS_HEADER*)img)->e_lfanew); }
// The file as the loader would leave it: NULL when it cannot be read or is not a 32-bit executable.
static uint8_t* MapExe(const char* path) {
    FILE* f = NULL; fopen_s(&f, path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t* file = (uint8_t*)malloc(n);
    bool read = file && fread(file, 1, n, f) == (size_t)n;
    fclose(f);
    if (!read || n < 0x400 || file[0] != 'M' || file[1] != 'Z') { free(file); return NULL; }
    IMAGE_NT_HEADERS* fnt = NtOf(file);
    if (fnt->Signature != IMAGE_NT_SIGNATURE || fnt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386) { free(file); return NULL; }
    uint8_t* img = (uint8_t*)VirtualAlloc(NULL, fnt->OptionalHeader.SizeOfImage, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!img) { free(file); return NULL; }
    memcpy(img, file, fnt->OptionalHeader.SizeOfHeaders);
    IMAGE_SECTION_HEADER* s = IMAGE_FIRST_SECTION(fnt);
    for (int i = 0; i < fnt->FileHeader.NumberOfSections; i++, s++) {
        uint32_t raw = s->SizeOfRawData;
        if (!raw || s->PointerToRawData + raw > (uint32_t)n || s->VirtualAddress + raw > fnt->OptionalHeader.SizeOfImage) continue;
        memcpy(img + s->VirtualAddress, file + s->PointerToRawData, raw);
    }
    uint32_t delta = (uint32_t)(uintptr_t)img - fnt->OptionalHeader.ImageBase;
    IMAGE_DATA_DIRECTORY rd = fnt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    for (uint32_t at = 0; rd.VirtualAddress && at + sizeof(IMAGE_BASE_RELOCATION) <= rd.Size;) {
        IMAGE_BASE_RELOCATION* r = (IMAGE_BASE_RELOCATION*)(img + rd.VirtualAddress + at);
        if (r->SizeOfBlock < sizeof(IMAGE_BASE_RELOCATION)) break;
        uint16_t* e = (uint16_t*)(r + 1);
        for (uint32_t k = 0; k < (r->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / 2; k++)
            if ((e[k] >> 12) == IMAGE_REL_BASED_HIGHLOW) *(uint32_t*)(img + r->VirtualAddress + (e[k] & 0xFFF)) += delta;
        at += r->SizeOfBlock;
    }
    free(file);
    return img;
}
static void Unmap(uint8_t* img) { if (img) VirtualFree(img, 0, MEM_RELEASE); }

// One question to IsKnownBuild about one mapped file, after `edit` changed the copy. The answer and what it said.
struct Answer { bool mapped, known; char why[160], name[64], hdr[80]; int build; bool pushAt; };
typedef void (*Edit_t)(uint8_t* img);
static Answer Ask(const char* path, Edit_t edit, uint32_t pushVa) {
    Answer r = {};
    uint8_t* img = MapExe(path);
    if (!img) return r;
    r.mapped = true;
    if (edit) edit(img);
    g_base = (uintptr_t)img; g_buildName = ""; g_buildSites = CoreSites{};
    r.known = IsKnownBuild(r.why, sizeof(r.why));
    strcpy_s(r.name, g_buildName); strcpy_s(r.hdr, g_buildHdr);
    r.build = g_buildSites.build; r.pushAt = g_buildSites.push == VA(pushVa);
    Unmap(img);
    return r;
}
static void EditStamp(uint8_t* img) { NtOf(img)->FileHeader.TimeDateStamp ^= 0x01010101u; }
static void EditSize(uint8_t* img) { NtOf(img)->OptionalHeader.SizeOfImage += 0x1000; }
static void EditBoth(uint8_t* img) { EditStamp(img); EditSize(img); }
static void EditResc(uint8_t* img) { img[A_RESC10_STR - 0x400000u] ^= 0x20; }
static void EditVtbl(uint8_t* img) { *(uint32_t*)(img + (A_ARENA_VTBL - 0x400000u)) += 0x10; }
static void EditAnchorB(uint8_t* img) { img[CE_A_ANCHOR_B - 0x400000u + 3] ^= 0xFF; }
static void EditAnchorBHdr(uint8_t* img) { EditAnchorB(img); EditBoth(img); }

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 3) { printf("usage: sis_build <1.0.8.0 GTAIV.exe> <ce-merged.exe> [<the Complete Edition as installed> [<another GTAIV.exe>]]\n"); return 2; }
    const char* p1080 = argv[1]; const char* pCe = argv[2];
    Answer r;

    r = Ask(p1080, NULL, A_PUSH);
    CHECK(r.mapped, "1.0.8.0 maps (%s)", p1080);
    CHECK(r.known && !strcmp(r.name, "1.0.8.0") && !r.hdr[0] && r.build == BUILD_1080 && r.pushAt,
          "1.0.8.0 as installed: known, its own table, the header not mentioned (%d '%s' '%s' '%s')", r.known, r.name, r.hdr, r.why);
    r = Ask(p1080, EditStamp, A_PUSH);
    CHECK(r.known && !strcmp(r.name, "1.0.8.0") && strstr(r.hdr, "exe header not stock") && r.build == BUILD_1080 && r.pushAt,
          "1.0.8.0 behind another time stamp: known by its code, the header named (%d '%s' '%s' '%s')", r.known, r.name, r.hdr, r.why);
    r = Ask(p1080, EditSize, A_PUSH);
    CHECK(r.known && !strcmp(r.name, "1.0.8.0") && strstr(r.hdr, "exe header not stock"),
          "1.0.8.0 behind another image size: known by its code (%d '%s' '%s' '%s')", r.known, r.name, r.hdr, r.why);
    r = Ask(p1080, EditResc, A_PUSH);
    CHECK(!r.known && strstr(r.why, "RESC10 string not at"), "1.0.8.0 with its RESC10 string changed: refused, and why (%d '%s')", r.known, r.why);
    r = Ask(p1080, EditVtbl, A_PUSH);
    CHECK(!r.known && strstr(r.why, "arena vtable slot 0 is"), "1.0.8.0 with its arena vtable changed: refused, and why (%d '%s')", r.known, r.why);

    r = Ask(pCe, NULL, CE_A_PUSH);
    CHECK(r.mapped, "the Complete Edition (recovered) maps (%s)", pCe);
    CHECK(r.known && !strcmp(r.name, "Complete Edition 1.2.0.59") && !r.hdr[0] && r.build == BUILD_CE && r.pushAt,
          "the Complete Edition, decrypted: known, its own table, the header not mentioned (%d '%s' '%s' '%s')", r.known, r.name, r.hdr, r.why);
    r = Ask(pCe, EditBoth, CE_A_PUSH);
    CHECK(r.known && !strcmp(r.name, "Complete Edition 1.2.0.59") && strstr(r.hdr, "exe header not stock") && r.build == BUILD_CE && r.pushAt,
          "the Complete Edition behind another header: known by its code, the header named (%d '%s' '%s' '%s')", r.known, r.name, r.hdr, r.why);
    r = Ask(pCe, EditAnchorB, CE_A_PUSH);
    CHECK(!r.known && strstr(r.why, "is not what SISCO read"), "the Complete Edition with one anchor rewritten: refused, and why (%d '%s')", r.known, r.why);
    r = Ask(pCe, EditAnchorBHdr, CE_A_PUSH);
    CHECK(!r.known && strstr(r.why, "is not what SISCO read"), "the same behind another header: refused all the same (%d '%s')", r.known, r.why);

    if (argc > 3) {
        r = Ask(argv[3], NULL, CE_A_PUSH);
        CHECK(r.mapped, "the Complete Edition as installed maps (%s)", argv[3]);
        CHECK(!r.known && strstr(r.why, "still encrypted"), "the Complete Edition as installed (its megabyte encrypted): refused, and why (%d '%s')", r.known, r.why);
        r = Ask(argv[3], EditBoth, CE_A_PUSH);
        CHECK(!r.known && strstr(r.why, "neither 1.0.8.0 nor"), "the same behind another header: refused as an unknown game (%d '%s')", r.known, r.why);
    } else printf("SKIPPED: no installed Complete Edition given, the encrypted case was NOT checked\n");
    if (argc > 4) {
        r = Ask(argv[4], NULL, A_PUSH);
        CHECK(r.mapped, "another build maps (%s)", argv[4]);
        CHECK(!r.known && strstr(r.why, "neither 1.0.8.0 nor"), "another build of the game: refused as an unknown game (%d '%s')", r.known, r.why);
    } else printf("SKIPPED: no other build given, the unknown-game case was NOT checked\n");

    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
