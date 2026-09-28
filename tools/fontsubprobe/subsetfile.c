/*
 * subsetfile <font> <out> <face> <text>|@<file>|- [glyphs]: the subset CreateFontPackage makes of a font file, written
 * to out.  Without "glyphs" the text (or the UTF-16 text of the file) is a list of characters for the Microsoft
 * platform's Unicode cmap; with it the text lists glyph indices as decimal numbers, or the file holds them as 16-bit
 * values.  "-" gives no list at all, with a count of 3.  A collection gets TTFCFP_FLAGS_TTC and the face.  When
 * FONTSUB_DLL is set, CreateFontPackage comes from that DLL instead of fontsub.dll, so that another build of it can
 * make the same subsets on the same machine.
 */
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>

typedef ULONG (__cdecl *CREATEFONTPACKAGE)(const unsigned char *, ULONG, unsigned char **, ULONG *, ULONG *,
        unsigned short, unsigned short, unsigned short, unsigned short, unsigned short, unsigned short,
        const unsigned short *, unsigned short, void *, void *, void *, void *);
static void *__cdecl alloc_proc(size_t s) { return malloc(s); }
static void *__cdecl realloc_proc(void *p, size_t s) { return realloc(p, s); }
static void __cdecl free_proc(void *p) { free(p); }

int main(void)
{
    const char *dll = getenv("FONTSUB_DLL");
    CREATEFONTPACKAGE create = (void *)GetProcAddress(LoadLibraryA(dll ? dll : "fontsub.dll"), "CreateFontPackage");
    int argc, n = 0, glyphs, i;
    WCHAR **argv = CommandLineToArgvW(GetCommandLineW(), &argc), *p;
    static unsigned short keep[32768];
    unsigned char *font, *out = NULL;
    ULONG out_size = 0, written = 0, ret;
    DWORD size, done;
    HANDLE file;

    if (argc < 5) return 2;
    glyphs = argc > 5;
    file = CreateFileW(argv[1], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return 3;
    size = GetFileSize(file, NULL);
    font = malloc(size);
    ReadFile(file, font, size, &done, NULL);
    CloseHandle(file);
    if (!wcscmp(argv[4], L"-"))
    {
        /* no list at all, with a count */
        ret = create(font, size, &out, &out_size, &written, 0x9 | (!memcmp(font, "ttcf", 4) ? 4 : 0), _wtoi(argv[3]),
                     0, 0, 3, 0xffff, NULL, 3, alloc_proc, realloc_proc, free_proc, NULL);
        printf("CreateFontPackage %lu, %lu bytes from %lu\n", ret, written, size);
        if (!ret)
        {
            file = CreateFileW(argv[2], GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
            WriteFile(file, out, written, &done, NULL);
            CloseHandle(file);
        }
        return ret;
    }
    if (argv[4][0] == '@')
    {
        /* the text of a UTF-16 file */
        HANDLE text_file = CreateFileW(argv[4] + 1, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        DWORD text_size = GetFileSize(text_file, NULL);
        WCHAR *text = malloc(text_size);

        ReadFile(text_file, text, text_size, &done, NULL);
        CloseHandle(text_file);
        for (n = 0; n < text_size / 2 && n < 32768; n++) keep[n] = text[n];
    }
    else if (glyphs)
        for (p = argv[4]; *p; ) { keep[n++] = wcstoul(p, &p, 10); while (*p == ' ' || *p == ',') p++; }
    else
        for (n = 0; argv[4][n] && n < 4096; n++) keep[n] = argv[4][n];
    ret = create(font, size, &out, &out_size, &written, (glyphs ? 0x9 : 0x1) | (!memcmp(font, "ttcf", 4) ? 4 : 0),
                 _wtoi(argv[3]), 0, 0, 3, glyphs ? 0xffff : 1, keep, n, alloc_proc, realloc_proc, free_proc, NULL);
    printf("CreateFontPackage %lu, %lu bytes from %lu\n", ret, written, size);
    if (!ret)
    {
        file = CreateFileW(argv[2], GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        WriteFile(file, out, written, &done, NULL);
        CloseHandle(file);
    }
    return ret;
}
