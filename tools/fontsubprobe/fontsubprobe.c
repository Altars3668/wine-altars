/*
 * What CreateFontPackage makes of a TrueType font, as Office's PDF export asks for it (a subset of a list of glyph
 * indices, for the Microsoft platform) and in the other ways it can be asked: a list of character codes instead, a
 * face of a collection, the other formats and compression, and bad arguments.  For each package: the return value,
 * its size, its tables with their lengths, maxp's glyph count, hhea's count of horizontal metrics, which glyphs keep
 * their outlines, the cmap subtables and what the kept characters map to, the post format, and whether GDI takes the
 * package as a font and maps the characters to the same glyphs as the original.
 *
 * fontsubprobe [font file=face name...]   (default: Arial, Times New Roman, SimSun's collection and DengXian)
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void *(__cdecl *CFP_ALLOCPROC)(size_t);
typedef void *(__cdecl *CFP_REALLOCPROC)(void *, size_t);
typedef void (__cdecl *CFP_FREEPROC)(void *);
typedef ULONG (__cdecl *CREATEFONTPACKAGE)(const unsigned char *, ULONG, unsigned char **, ULONG *, ULONG *,
        unsigned short, unsigned short, unsigned short, unsigned short, unsigned short, unsigned short,
        const unsigned short *, unsigned short, CFP_ALLOCPROC, CFP_REALLOCPROC, CFP_FREEPROC, void *);
typedef ULONG (__cdecl *MERGEFONTPACKAGE)(const unsigned char *, ULONG, const unsigned char *, ULONG,
        unsigned char **, ULONG *, ULONG *, unsigned short, CFP_ALLOCPROC, CFP_REALLOCPROC, CFP_FREEPROC, void *);

static CREATEFONTPACKAGE pCreateFontPackage;
static MERGEFONTPACKAGE pMergeFontPackage;

static void *__cdecl alloc_proc(size_t size) { return malloc(size); }
static void *__cdecl realloc_proc(void *ptr, size_t size) { return realloc(ptr, size); }
static void __cdecl free_proc(void *ptr) { free(ptr); }

static unsigned int be16(const unsigned char *p) { return (p[0] << 8) | p[1]; }
static unsigned int be32(const unsigned char *p) { return ((unsigned int)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

static const unsigned char *find_table(const unsigned char *font, ULONG size, const char *tag, ULONG *length)
{
    unsigned int i, count;

    if (size < 12) return NULL;
    count = be16(font + 4);
    for (i = 0; i < count && 12 + i * 16 + 16 <= size; i++)
    {
        const unsigned char *entry = font + 12 + i * 16;
        if (memcmp(entry, tag, 4)) continue;
        if (be32(entry + 8) + be32(entry + 12) > size) return NULL;
        *length = be32(entry + 12);
        return font + be32(entry + 8);
    }
    return NULL;
}

/* the glyph a format 4 or 12 cmap subtable gives a character, -1 if none */
static int cmap_lookup(const unsigned char *sub, ULONG avail, unsigned int ch)
{
    unsigned int format = be16(sub), i;

    if (format == 4)
    {
        unsigned int segs = be16(sub + 6) / 2;
        const unsigned char *ends = sub + 14, *starts = ends + segs * 2 + 2, *deltas = starts + segs * 2;
        const unsigned char *offsets = deltas + segs * 2;
        for (i = 0; i < segs; i++)
        {
            unsigned int end = be16(ends + i * 2), start = be16(starts + i * 2);
            if (ch > end) continue;
            if (ch < start) return -1;
            if (!be16(offsets + i * 2)) return (ch + be16(deltas + i * 2)) & 0xffff;
            {
                const unsigned char *p = offsets + i * 2 + be16(offsets + i * 2) + (ch - start) * 2;
                unsigned int glyph;
                if (p + 2 > sub + avail) return -1;
                glyph = be16(p);
                return glyph ? (glyph + be16(deltas + i * 2)) & 0xffff : 0;
            }
        }
        return -1;
    }
    if (format == 12)
    {
        unsigned int groups = be32(sub + 12);
        for (i = 0; i < groups; i++)
        {
            const unsigned char *g = sub + 16 + i * 12;
            if (ch >= be32(g) && ch <= be32(g + 4)) return be32(g + 8) + ch - be32(g);
        }
        return -1;
    }
    if (format == 0) return ch < 256 ? sub[6 + ch] : -1;
    return -2;
}

static void describe(const char *what, const unsigned char *font, ULONG size, const WCHAR *chars, int nchars,
                     const unsigned char *orig, ULONG orig_size)
{
    const unsigned char *table, *loca, *glyf, *cmap, *orig_cmap = NULL;
    ULONG length, loca_len, glyf_len, cmap_len, orig_cmap_len = 0;
    unsigned int i, count, glyphs = 0, long_loca = 0, kept = 0, first_kept = ~0u, last_kept = 0;
    char list[512] = "";

    printf("  %s: %lu bytes, tables", what, size);
    if (size < 12) { printf(" none\n"); return; }
    count = be16(font + 4);
    for (i = 0; i < count && 12 + i * 16 + 16 <= size; i++)
        printf(" %.4s:%u", font + 12 + i * 16, be32(font + 12 + i * 16 + 12));
    printf("\n");
    if ((table = find_table(font, size, "maxp", &length)) && length >= 6)
        printf("    maxp glyphs %u", glyphs = be16(table + 4));
    if ((table = find_table(font, size, "hhea", &length)) && length >= 36)
        printf(", hhea metrics %u", be16(table + 34));
    if ((table = find_table(font, size, "head", &length)) && length >= 54)
        printf(", head loca format %u", long_loca = be16(table + 50));
    if ((table = find_table(font, size, "post", &length)) && length >= 4)
        printf(", post %u.%u", be16(table), be16(table + 2) >> 8);
    if ((table = find_table(font, size, "OS/2", &length)) && length >= 68)
        printf(", OS/2 v%u chars %04x-%04x", be16(table), be16(table + 64), be16(table + 66));
    printf("\n");

    if ((loca = find_table(font, size, "loca", &loca_len)) && (glyf = find_table(font, size, "glyf", &glyf_len)))
    {
        for (i = 0; i < glyphs && (i + 1) * (long_loca ? 4 : 2) < loca_len + (long_loca ? 0 : 0); i++)
        {
            unsigned int start = long_loca ? be32(loca + i * 4) : be16(loca + i * 2) * 2;
            unsigned int end = long_loca ? be32(loca + i * 4 + 4) : be16(loca + i * 2 + 2) * 2;
            if (end > start)
            {
                kept++;
                if (first_kept == ~0u) first_kept = i;
                last_kept = i;
                if (strlen(list) < sizeof(list) - 16) sprintf(list + strlen(list), " %u", i);
            }
        }
        printf("    glyphs with outlines %u (first %u, last %u):%s\n", kept, first_kept, last_kept,
               kept <= 64 ? list : " ...");
    }

    if (orig) orig_cmap = find_table(orig, orig_size, "cmap", &orig_cmap_len);
    if ((cmap = find_table(font, size, "cmap", &cmap_len)))
    {
        unsigned int subs = be16(cmap + 2);
        for (i = 0; i < subs && 4 + i * 8 + 8 <= cmap_len; i++)
        {
            const unsigned char *rec = cmap + 4 + i * 8, *sub = cmap + be32(rec + 4);
            unsigned int format = be16(sub), sub_len = format >= 8 ? be32(sub + 4) : be16(sub + 2);
            int j, mapped = 0, same = 0;

            printf("    cmap %u/%u format %u length %u:", be16(rec), be16(rec + 2), format, sub_len);
            if (format == 4 || format == 12)
            {
                unsigned int segs = format == 4 ? be16(sub + 6) / 2 : be32(sub + 12);
                printf(" %u %s,", segs, format == 4 ? "segments" : "groups");
            }
            for (j = 0; j < nchars; j++)
            {
                int glyph = cmap_lookup(sub, cmap_len - be32(rec + 4), chars[j]);
                if (glyph > 0) mapped++;
                if (orig_cmap)
                {
                    unsigned int k, osubs = be16(orig_cmap + 2);
                    for (k = 0; k < osubs; k++)
                    {
                        const unsigned char *orec = orig_cmap + 4 + k * 8;
                        if (be16(orec) != be16(rec) || be16(orec + 2) != be16(rec + 2)) continue;
                        if (cmap_lookup(orig_cmap + be32(orec + 4), orig_cmap_len - be32(orec + 4), chars[j]) == glyph)
                            same++;
                        break;
                    }
                }
            }
            printf(" the %d characters: %d mapped, %d as in the original\n", nchars, mapped, same);
        }
    }
}

static BOOL gdi_takes(const unsigned char *font, ULONG size, const WCHAR *face, const WCHAR *chars, int nchars,
                      const WORD *expected)
{
    DWORD installed = 0;
    HANDLE handle = AddFontMemResourceEx((void *)font, size, NULL, &installed);
    LOGFONTW lf = {0};
    HDC hdc;
    HFONT hfont, old;
    WORD glyphs[64];
    WCHAR got_face[LF_FACESIZE];
    BOOL same;

    if (!handle) { printf("    GDI: AddFontMemResourceEx failed, error %lu\n", GetLastError()); return FALSE; }
    hdc = CreateCompatibleDC(0);
    lf.lfHeight = -20;
    lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpyW(lf.lfFaceName, face);
    hfont = CreateFontIndirectW(&lf);
    old = SelectObject(hdc, hfont);
    GetTextFaceW(hdc, LF_FACESIZE, got_face);
    GetGlyphIndicesW(hdc, chars, nchars, glyphs, GGI_MARK_NONEXISTING_GLYPHS);
    same = !memcmp(glyphs, expected, nchars * sizeof(WORD));
    printf("    GDI: %lu installed, face %s, the characters map %s\n", installed,
           lstrcmpiW(got_face, face) ? "substituted" : "as asked", same ? "to the original glyphs" : "differently");
    SelectObject(hdc, old);
    DeleteObject(hfont);
    DeleteDC(hdc);
    RemoveFontMemResourceEx(handle);
    return same;
}


/* whether the outline of a kept glyph and the metrics of a dropped one are as in the original */
static void compare_details(const unsigned char *font, ULONG size, const WORD *glyphs, int nglyphs)
{
    unsigned short keep[64];
    unsigned char *package = NULL;
    ULONG package_size = 0, written = 0, ret, len1, len2, hl1, hl2;
    const unsigned char *loca1, *loca2, *glyf1, *glyf2, *head1, *head2, *hmtx1, *hmtx2, *hhea2;
    unsigned int g = glyphs[0], dropped = glyphs[0] + 1, i, long1, long2, s1, e1, s2, e2, metrics2;
    int n;

    for (n = 0; n < nglyphs; n++) keep[n] = glyphs[n];
    for (n = 0; n < nglyphs; n++) if (glyphs[n] == dropped) dropped++;
    ret = pCreateFontPackage(font, size, &package, &package_size, &written, 0x9, 0, 0, 0, 3, 0xffff, keep, nglyphs,
                             alloc_proc, realloc_proc, free_proc, NULL);
    if (ret) return;
    head1 = find_table(font, size, "head", &hl1);
    head2 = find_table(package, written, "head", &hl2);
    long1 = be16(head1 + 50);
    long2 = be16(head2 + 50);
    loca1 = find_table(font, size, "loca", &len1);
    loca2 = find_table(package, written, "loca", &len2);
    glyf1 = find_table(font, size, "glyf", &len1);
    glyf2 = find_table(package, written, "glyf", &len2);
    s1 = long1 ? be32(loca1 + g * 4) : be16(loca1 + g * 2) * 2;
    e1 = long1 ? be32(loca1 + g * 4 + 4) : be16(loca1 + g * 2 + 2) * 2;
    s2 = long2 ? be32(loca2 + g * 4) : be16(loca2 + g * 2) * 2;
    e2 = long2 ? be32(loca2 + g * 4 + 4) : be16(loca2 + g * 2 + 2) * 2;
    printf("  glyph %u: %u bytes, %u in the package, %s\n", g, e1 - s1, e2 - s2,
           e1 - s1 == e2 - s2 && !memcmp(glyf1 + s1, glyf2 + s2, e1 - s1) ? "the same" : "different");
    hmtx1 = find_table(font, size, "hmtx", &len1);
    hmtx2 = find_table(package, written, "hmtx", &len2);
    hhea2 = find_table(package, written, "hhea", &hl2);
    metrics2 = be16(hhea2 + 34);
    printf("  dropped glyph %u: advance %u lsb %d, in the package advance %u lsb %d\n", dropped,
           be16(hmtx1 + dropped * 4), (short)be16(hmtx1 + dropped * 4 + 2),
           dropped < metrics2 ? be16(hmtx2 + dropped * 4) : be16(hmtx2 + (metrics2 - 1) * 4),
           dropped < metrics2 ? (short)be16(hmtx2 + dropped * 4 + 2) : (short)be16(hmtx2 + metrics2 * 4 + (dropped - metrics2) * 2));
    printf("  head checksum adjustment %08x, the original %08x; flags %04x, the original %04x; modified %s\n",
           be32(head2 + 8), be32(head1 + 8), be16(head2 + 16), be16(head1 + 16),
           memcmp(head1 + 28, head2 + 28, 8) ? "changed" : "the same");
    for (i = 0; i < be16(package + 4); i++)
    {
        const unsigned char *entry = package + 12 + i * 16;
        unsigned int sum = 0, j, off = be32(entry + 8), len = be32(entry + 12);
        for (j = 0; j < (len + 3) / 4; j++)
        {
            unsigned char word[4] = {0};
            memcpy(word, package + off + j * 4, min(4, len - j * 4));
            if (!memcmp(entry, "head", 4) && j == 2) continue;
            sum += be32(word);
        }
        if (sum != be32(entry + 4)) printf("  table %.4s: checksum %08x, stored %08x\n", entry, sum, be32(entry + 4));
        if (off % 4) printf("  table %.4s at an unaligned offset %u\n", entry, off);
    }
    printf("  table order:");
    for (i = 0; i < be16(package + 4); i++) printf(" %.4s@%u", package + 12 + i * 16, be32(package + 12 + i * 16 + 8));
    printf("\n  header: version %08x, tables %u, search range %u, entry selector %u, range shift %u\n",
           be32(package), be16(package + 4), be16(package + 6), be16(package + 8), be16(package + 10));
    free(package);
}

/* one bad argument at a time, each in a process of its own, as some make fontsub crash */
static const char *const error_names[] =
{
    "no source", "no source size", "no buffer pointer", "no size pointer", "no written pointer", "no allocator",
    "no reallocator", "no free", "format 3", "platform 4", "unknown flag 0x10", "compress only",
    "collection flag on a single font, face 0", "a collection without the flag", "a collection, face 1",
    "not a font", "cut short",
};

static void run_error(const unsigned char *font, ULONG size, int which)
{
    unsigned short keep[2] = { 36, 37 }, flags_ttc = !memcmp(font, "ttcf", 4) ? 0x4 : 0;
    unsigned char *package = NULL, junk[256];
    ULONG package_size = 0, written = 0, ret = 0;
    unsigned char **dest = &package;
    ULONG *dest_size = &package_size, *written_ptr = &written;
    CFP_ALLOCPROC a = alloc_proc;
    CFP_REALLOCPROC r = realloc_proc;
    CFP_FREEPROC f = free_proc;
    const unsigned char *src = font;
    unsigned short flags = 0x9 | flags_ttc, face = 0, format = 0, platform = 3, encoding = 0xffff;
    ULONG len = size;

    memset(junk, 0x41, sizeof(junk));
    switch (which)
    {
    case 0: src = NULL; break;
    case 1: len = 0; break;
    case 2: dest = NULL; break;
    case 3: dest_size = NULL; break;
    case 4: written_ptr = NULL; break;
    case 5: a = NULL; break;
    case 6: r = NULL; break;
    case 7: f = NULL; break;
    case 8: format = 3; break;
    case 9: platform = 4; encoding = 1; flags = 0x1 | flags_ttc; break;
    case 10: flags |= 0x10; break;
    case 11: flags = 0x2 | flags_ttc; break;
    case 12: if (flags_ttc) return; flags = 0x9 | 0x4; break;
    case 13: if (!flags_ttc) return; flags = 0x9; break;
    case 14: if (!flags_ttc) return; face = 1; break;
    case 15: src = junk; len = sizeof(junk); flags = 0x9; break;
    case 16: len = 2000; break;
    }
    ret = pCreateFontPackage(src, len, dest, dest_size, written_ptr, flags, face, format, 0, platform, encoding, keep,
                             2, a, r, f, NULL);
    printf("  %s: %lu, written %lu\n", error_names[which], ret, written);
}

static void errors(const char *path)
{
    char self[MAX_PATH], cmdline[MAX_PATH * 3];
    unsigned int i;

    GetModuleFileNameA(NULL, self, sizeof(self));
    for (i = 0; i < ARRAY_SIZE(error_names); i++)
    {
        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        DWORD code;

        sprintf(cmdline, "\"%s\" error %u \"%s\"", self, i, path);
        if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) continue;
        WaitForSingleObject(pi.hProcess, 60000);
        GetExitCodeProcess(pi.hProcess, &code);
        if (code) printf("  %s: crashed, exception %#lx\n", error_names[i], code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

static unsigned char *read_file(const char *path, ULONG *size)
{
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    unsigned char *data;
    DWORD read;

    if (file == INVALID_HANDLE_VALUE) return NULL;
    *size = GetFileSize(file, NULL);
    data = malloc(*size);
    ReadFile(file, data, *size, &read, NULL);
    CloseHandle(file);
    return data;
}

static void probe_font(const char *path, const WCHAR *face, unsigned short face_index)
{
    static const WCHAR latin[] = L"Hello, PDF 123!";
    static const WCHAR cjk[] = L"\x5bfc\x51fa\x6d4b\x8bd5\x4e2d\x6587" L"Ab";
    const WCHAR *chars;
    unsigned short keep[64], flags_ttc;
    unsigned char *font, *package, *merged;
    ULONG size, package_size, written, merged_size, merged_written, ret;
    WORD orig_glyphs[64];
    DWORD installed;
    HANDLE handle;
    HDC hdc;
    HFONT hfont, old;
    LOGFONTW lf = {0};
    int nchars, i;
    char name[64];

    if (!(font = read_file(path, &size))) { printf("%s: not there\n", path); return; }
    flags_ttc = !memcmp(font, "ttcf", 4) ? 0x4 : 0;
    WideCharToMultiByte(CP_UTF8, 0, face, -1, name, sizeof(name), NULL, NULL);
    printf("%s (%s, face %u), %lu bytes%s\n", path, name, face_index, size, flags_ttc ? ", a collection" : "");
    chars = flags_ttc || wcsstr(face, L"Deng") || wcsstr(face, L"Sim") ? cjk : latin;
    nchars = lstrlenW(chars);

    /* the glyphs GDI maps the characters to in the installed font */
    hdc = CreateCompatibleDC(0);
    lf.lfHeight = -20;
    lf.lfCharSet = DEFAULT_CHARSET;
    lstrcpyW(lf.lfFaceName, face);
    hfont = CreateFontIndirectW(&lf);
    old = SelectObject(hdc, hfont);
    GetGlyphIndicesW(hdc, chars, nchars, orig_glyphs, GGI_MARK_NONEXISTING_GLYPHS);
    SelectObject(hdc, old);
    DeleteObject(hfont);
    DeleteDC(hdc);
    printf("  glyphs of the characters:");
    for (i = 0; i < nchars; i++) printf(" %u", orig_glyphs[i]);
    printf("\n");

    /* as Office's PDF export asks: a subset of glyph indices for the Microsoft platform */
    for (i = 0; i < nchars; i++) keep[i] = orig_glyphs[i];
    package = NULL; package_size = written = 0;
    ret = pCreateFontPackage(font, size, &package, &package_size, &written, 0x9 | flags_ttc, face_index, 0, 0, 3,
                             0xffff, keep, nchars, alloc_proc, realloc_proc, free_proc, NULL);
    printf("  glyph list, subset: %lu, buffer %lu, written %lu\n", ret, package_size, written);
    if (!ret)
    {
        describe("package", package, written, chars, nchars, flags_ttc ? NULL : font, size);
        gdi_takes(package, written, face, chars, nchars, orig_glyphs);
        free(package);
    }

    /* a list of character codes */
    for (i = 0; i < nchars; i++) keep[i] = chars[i];
    package = NULL; package_size = written = 0;
    ret = pCreateFontPackage(font, size, &package, &package_size, &written, 0x1 | flags_ttc, face_index, 0, 0, 3, 1,
                             keep, nchars, alloc_proc, realloc_proc, free_proc, NULL);
    printf("  character list, subset: %lu, buffer %lu, written %lu\n", ret, package_size, written);
    if (!ret)
    {
        describe("package", package, written, chars, nchars, flags_ttc ? NULL : font, size);
        gdi_takes(package, written, face, chars, nchars, orig_glyphs);

        /* a subset1 package of the same characters and a delta, merged */
        free(package);
    }

    /* not subsetting at all */
    package = NULL; package_size = written = 0;
    ret = pCreateFontPackage(font, size, &package, &package_size, &written, 0x0 | flags_ttc, face_index, 0, 0, 3, 1,
                             keep, nchars, alloc_proc, realloc_proc, free_proc, NULL);
    printf("  no subset flag: %lu, written %lu%s\n", ret, written,
           !ret && written == size ? " (the font as it is)" : "");
    if (!ret && written != size && !flags_ttc) describe("package", package, written, chars, nchars, font, size);
    else if (!ret && flags_ttc) describe("package", package, written, chars, nchars, NULL, 0);
    free(package);

    /* subset1, then a delta with more characters, merged */
    package = NULL; package_size = written = 0;
    ret = pCreateFontPackage(font, size, &package, &package_size, &written, 0x9 | flags_ttc, face_index, 1, 0, 3,
                             0xffff, orig_glyphs, nchars / 2, alloc_proc, realloc_proc, free_proc, NULL);
    printf("  glyph list, subset1 of half: %lu, written %lu\n", ret, written);
    if (!ret)
    {
        unsigned char *delta = NULL;
        ULONG delta_size = 0, delta_written = 0;

        describe("subset1", package, written, chars, nchars, flags_ttc ? NULL : font, size);
        ret = pCreateFontPackage(font, size, &delta, &delta_size, &delta_written, 0x9 | flags_ttc, face_index, 2, 0,
                                 3, 0xffff, orig_glyphs, nchars, alloc_proc, realloc_proc, free_proc, NULL);
        printf("  glyph list, delta of all: %lu, written %lu\n", ret, delta_written);
        if (!ret)
        {
            describe("delta", delta, delta_written, chars, nchars, flags_ttc ? NULL : font, size);
            if (pMergeFontPackage)
            {
                merged = NULL; merged_size = merged_written = 0;
                ret = pMergeFontPackage(NULL, 0, package, written, &merged, &merged_size, &merged_written, 1,
                                        alloc_proc, realloc_proc, free_proc, NULL);
                printf("  merge the subset1 into nothing: %lu, written %lu\n", ret, merged_written);
                if (!ret)
                {
                    unsigned char *final = NULL;
                    ULONG final_size = 0, final_written = 0;

                    ret = pMergeFontPackage(merged, merged_written, delta, delta_written, &final, &final_size,
                                            &final_written, 2, alloc_proc, realloc_proc, free_proc, NULL);
                    printf("  merge the delta into that: %lu, written %lu\n", ret, final_written);
                    if (!ret)
                    {
                        describe("merged", final, final_written, chars, nchars, flags_ttc ? NULL : font, size);
                        gdi_takes(final, final_written, face, chars, nchars, orig_glyphs);
                        free(final);
                    }
                    free(merged);
                }
                merged = NULL; merged_size = merged_written = 0;
                ret = pMergeFontPackage(NULL, 0, package, written, &merged, &merged_size, &merged_written, 0,
                                        alloc_proc, realloc_proc, free_proc, NULL);
                printf("  merge the subset1 as a plain subset: %lu, written %lu\n", ret, merged_written);
                free(merged);
            }
            free(delta);
        }
        free(package);
    }

    /* compressed */
    package = NULL; package_size = written = 0;
    ret = pCreateFontPackage(font, size, &package, &package_size, &written, 0xb | flags_ttc, face_index, 0, 0, 3,
                             0xffff, orig_glyphs, nchars, alloc_proc, realloc_proc, free_proc, NULL);
    printf("  glyph list, subset, compressed: %lu, written %lu, starts %02x %02x %02x %02x\n", ret, written,
           package && written > 3 ? package[0] : 0, package && written > 3 ? package[1] : 0,
           package && written > 3 ? package[2] : 0, package && written > 3 ? package[3] : 0);
    free(package);

    /* a glyph beyond the font, an empty list, and no list */
    keep[0] = 0xfffe;
    package = NULL; package_size = written = 0;
    ret = pCreateFontPackage(font, size, &package, &package_size, &written, 0x9 | flags_ttc, face_index, 0, 0, 3,
                             0xffff, keep, 1, alloc_proc, realloc_proc, free_proc, NULL);
    printf("  a glyph beyond the font: %lu, written %lu\n", ret, written);
    if (!ret) describe("package", package, written, chars, 0, NULL, 0);
    free(package);
    package = NULL; package_size = written = 0;
    ret = pCreateFontPackage(font, size, &package, &package_size, &written, 0x9 | flags_ttc, face_index, 0, 0, 3,
                             0xffff, keep, 0, alloc_proc, realloc_proc, free_proc, NULL);
    printf("  an empty list: %lu, written %lu\n", ret, written);
    if (!ret) describe("package", package, written, chars, 0, NULL, 0);
    free(package);
    package = NULL; package_size = written = 0;
    ret = pCreateFontPackage(font, size, &package, &package_size, &written, 0x9 | flags_ttc, face_index, 0, 0, 3,
                             0xffff, NULL, 3, alloc_proc, realloc_proc, free_proc, NULL);
    printf("  no list with a count: %lu, written %lu\n", ret, written);
    if (!ret) describe("package", package, written, chars, 0, NULL, 0);
    free(package);
    /* a face beyond the collection, and the collection flag on a single font */
    package = NULL; package_size = written = 0;
    ret = pCreateFontPackage(font, size, &package, &package_size, &written, 0x9 | 0x4, 7, 0, 0, 3, 0xffff,
                             orig_glyphs, nchars, alloc_proc, realloc_proc, free_proc, NULL);
    printf("  collection flag, face 7: %lu, written %lu\n", ret, written);
    free(package);
    if (!flags_ttc)
    {
        /* character list for a platform the cmap does not have */
        for (i = 0; i < nchars; i++) keep[i] = chars[i];
        package = NULL; package_size = written = 0;
        ret = pCreateFontPackage(font, size, &package, &package_size, &written, 0x1, 0, 0, 0, 1, 0, keep, nchars,
                                 alloc_proc, realloc_proc, free_proc, NULL);
        printf("  character list, Apple platform: %lu, written %lu\n", ret, written);
        if (!ret) describe("package", package, written, chars, nchars, font, size);
        free(package);
    }
    if (!flags_ttc) compare_details(font, size, orig_glyphs, nchars);
    fflush(stdout);
    errors(path);
    (void)handle; (void)installed;
    free(font);
}

int main(int argc, char **argv)
{
    HMODULE module = LoadLibraryA("fontsub.dll");
    char windir[MAX_PATH], path[MAX_PATH];
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    pCreateFontPackage = (CREATEFONTPACKAGE)GetProcAddress(module, "CreateFontPackage");
    pMergeFontPackage = (MERGEFONTPACKAGE)GetProcAddress(module, "MergeFontPackage");
    if (!pCreateFontPackage) return 1;
    if (argc == 4 && !strcmp(argv[1], "error"))
    {
        ULONG size;
        unsigned char *font = read_file(argv[3], &size);

        if (font) run_error(font, size, atoi(argv[2]));
        return 0;
    }
    printf("fontsub %s, MergeFontPackage %s\n", module ? "loaded" : "missing", pMergeFontPackage ? "there" : "missing");
    if (argc > 1)
    {
        /* path=face */
        for (i = 1; i < argc; i++)
        {
            WCHAR face[LF_FACESIZE] = L"Arial";
            char *eq = strchr(argv[i], '=');

            if (eq)
            {
                *eq = 0;
                MultiByteToWideChar(CP_UTF8, 0, eq + 1, -1, face, LF_FACESIZE);
            }
            probe_font(argv[i], face, 0);
        }
        return 0;
    }
    GetWindowsDirectoryA(windir, sizeof(windir));
    sprintf(path, "%s\\Fonts\\arial.ttf", windir);
    probe_font(path, L"Arial", 0);
    sprintf(path, "%s\\Fonts\\times.ttf", windir);
    probe_font(path, L"Times New Roman", 0);
    sprintf(path, "%s\\Fonts\\simsun.ttc", windir);
    probe_font(path, L"SimSun", 0);
    sprintf(path, "%s\\Fonts\\Deng.ttf", windir);
    probe_font(path, L"DengXian", 0);
    return 0;
}
