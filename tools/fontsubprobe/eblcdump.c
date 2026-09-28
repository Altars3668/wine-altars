/* eblcdump <font> <face> <size or -1> <subtables per size>: the structure of a font's EBLC, and which glyphs have data */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned int be16(const unsigned char *p) { return (p[0] << 8) | p[1]; }
static unsigned int be32(const unsigned char *p) { return ((unsigned int)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

int main(int argc, char **argv)
{
    FILE *f;
    unsigned char *b;
    long size;
    unsigned int face = 0, n, i, eblc = 0, eblc_len = 0, ebdt_len = 0, sizes, s, j, k, max;
    int only;

    if (argc < 5 || !(f = fopen(argv[1], "rb"))) return 2;
    fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc(size); fread(b, 1, size, f); fclose(f);
    if (!memcmp(b, "ttcf", 4)) face = be32(b + 12 + 4 * atoi(argv[2]));
    only = atoi(argv[3]); max = atoi(argv[4]);
    n = be16(b + face + 4);
    for (i = 0; i < n; i++)
    {
        const unsigned char *e = b + face + 12 + i * 16;
        if (!memcmp(e, "EBLC", 4)) { eblc = be32(e + 8); eblc_len = be32(e + 12); }
        if (!memcmp(e, "EBDT", 4)) ebdt_len = be32(e + 12);
    }
    printf("tables:");
    for (i = 0; i < n; i++) printf(" %.4s", b + face + 12 + i * 16);
    printf("\n");
    if (!eblc) { printf("no EBLC\n"); return 0; }
    sizes = be32(b + eblc + 4);
    printf("EBLC %u bytes, EBDT %u bytes, %u sizes\n", eblc_len, ebdt_len, sizes);
    for (s = 0; s < sizes; s++)
    {
        const unsigned char *st = b + eblc + 8 + s * 48;
        unsigned int array = be32(st), count = be32(st + 8);

        printf("size %u: ppem %u, glyphs %u-%u, %u subtables, array at %u, %u bytes\n", s, st[44], be16(st + 40),
               be16(st + 42), count, array, be32(st + 4));
        if (only >= 0 && (int)s != only) continue;
        for (j = 0; j < count && j < max; j++)
        {
            const unsigned char *a = b + eblc + array + j * 8, *h;
            unsigned int first = be16(a), last = be16(a + 2), fmt, shown = 0, empty = 0;

            h = b + eblc + array + be32(a + 4);
            fmt = be16(h);
            printf("  [%u] %u-%u at %u: %u/%u image %u", j, first, last, (unsigned int)(h - b - eblc), fmt, be16(h + 2),
                   be32(h + 4));
            if (fmt == 1 || fmt == 3)
            {
                printf(", data");
                for (k = 0; k <= last - first; k++)
                {
                    unsigned int o0 = fmt == 1 ? be32(h + 8 + k * 4) : be16(h + 8 + k * 2);
                    unsigned int o1 = fmt == 1 ? be32(h + 12 + k * 4) : be16(h + 10 + k * 2);
                    if (o1 > o0) { if (shown++ < 16) printf(" %u", first + k); }
                    else empty++;
                }
                printf(" (%u with data, %u empty)", shown, empty);
            }
            else if (fmt == 2) printf(", image size %u", be32(h + 8));
            else if (fmt == 4)
            {
                unsigned int g = be32(h + 8);
                printf(", %u glyphs:", g);
                for (k = 0; k < g && k < 16; k++) printf(" %u(%u)", be16(h + 12 + k * 4), be16(h + 18 + k * 4) - be16(h + 14 + k * 4));
            }
            else if (fmt == 5)
            {
                unsigned int g = be32(h + 20);
                printf(", image size %u, %u glyphs:", be32(h + 8), g);
                for (k = 0; k < g && k < 16; k++) printf(" %u", be16(h + 24 + k * 2));
            }
            printf("\n");
        }
    }
    return 0;
}
