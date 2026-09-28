/* os2info <original> <face> <subset>: OS/2's character range against what the cmaps of both fonts map */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned int be16(const unsigned char *p) { return (p[0] << 8) | p[1]; }
static unsigned int be32(const unsigned char *p) { return ((unsigned int)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

static unsigned char *load(const char *path, unsigned int face, unsigned int *dir)
{
    FILE *f = fopen(path, "rb");
    long size;
    unsigned char *b;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc(size); fread(b, 1, size, f); fclose(f);
    *dir = !memcmp(b, "ttcf", 4) ? be32(b + 12 + 4 * face) : 0;
    return b;
}

static const unsigned char *table(const unsigned char *b, unsigned int dir, const char *tag)
{
    unsigned int i, n = be16(b + dir + 4);
    for (i = 0; i < n; i++) if (!memcmp(b + dir + 12 + i * 16, tag, 4)) return b + be32(b + dir + 12 + i * 16 + 8);
    return NULL;
}

/* the lowest and highest characters a subtable maps to a glyph other than 0 */
static void range(const unsigned char *s, unsigned int *lo, unsigned int *hi)
{
    unsigned int fmt = be16(s), i, c;
    *lo = ~0u; *hi = 0;
    if (fmt == 4)
    {
        unsigned int segs = be16(s + 6) / 2;
        for (i = 0; i < segs; i++)
        {
            unsigned int end = be16(s + 14 + i * 2), start = be16(s + 16 + segs * 2 + i * 2), delta = be16(s + 16 + segs * 4 + i * 2);
            unsigned int ro = be16(s + 16 + segs * 6 + i * 2);
            for (c = start; c <= end && c != 0xffff; c++)
            {
                unsigned int g = ro ? be16(s + 16 + segs * 6 + i * 2 + ro + (c - start) * 2) : c;
                if (g) g = (g + delta) & 0xffff;
                if (g) { if (c < *lo) *lo = c; if (c > *hi) *hi = c; }
            }
        }
    }
    else if (fmt == 0)
    {
        for (c = 0; c < 256; c++) if (s[6 + c]) { if (c < *lo) *lo = c; if (c > *hi) *hi = c; }
    }
    else if (fmt == 6)
    {
        unsigned int first = be16(s + 6), n = be16(s + 8);
        for (i = 0; i < n; i++) if (be16(s + 10 + i * 2)) { if (first + i < *lo) *lo = first + i; if (first + i > *hi) *hi = first + i; }
    }
    else if (fmt == 12)
    {
        unsigned int n = be32(s + 12);
        for (i = 0; i < n; i++)
        {
            unsigned int start = be32(s + 16 + i * 12), end = be32(s + 20 + i * 12), g = be32(s + 24 + i * 12);
            if (!g && start == end) continue;
            if (!g) start++;
            if (start < *lo) *lo = start;
            if (end > *hi) *hi = end;
        }
    }
}

static void describe(const char *label, const unsigned char *b, unsigned int dir)
{
    const unsigned char *os2 = table(b, dir, "OS/2"), *cmap = table(b, dir, "cmap"), *head = table(b, dir, "head");
    unsigned int i, n = be16(cmap + 2), lo, hi;
    printf("  %s: OS/2 v%u %04x-%04x, head flags %04x", label, be16(os2), be16(os2 + 64), be16(os2 + 66), be16(head + 16));
    for (i = 0; i < n; i++)
    {
        const unsigned char *s = cmap + be32(cmap + 8 + i * 8);
        if (be16(s) != 4 && be16(s) != 12 && be16(s) != 0 && be16(s) != 6) { printf(", %u/%u f%u@%u", be16(cmap + 4 + i * 8), be16(cmap + 6 + i * 8), be16(s), be32(cmap + 8 + i * 8)); continue; }
        range(s, &lo, &hi);
        printf(", %u/%u f%u@%u %04x-%04x", be16(cmap + 4 + i * 8), be16(cmap + 6 + i * 8), be16(s), be32(cmap + 8 + i * 8), lo, hi);
    }
    printf("\n");
}

int main(int argc, char **argv)
{
    unsigned int od, sd;
    unsigned char *o = load(argv[1], atoi(argv[2]), &od), *s = load(argv[3], 0, &sd);
    if (!o || !s) { printf("  missing\n"); return 2; }
    describe("original", o, od);
    describe("subset  ", s, sd);
    return 0;
}
