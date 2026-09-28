/* fontcmp <a> <b>: which tables of two fonts differ, where, and the physical order when that differs */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned int be16(const unsigned char *p) { return (p[0] << 8) | p[1]; }
static unsigned int be32(const unsigned char *p) { return ((unsigned int)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

static unsigned char *load(const char *path, long *size)
{
    FILE *f = fopen(path, "rb");
    unsigned char *b;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); *size = ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc(*size); fread(b, 1, *size, f); fclose(f);
    return b;
}

static const unsigned char *table(const unsigned char *b, const char *tag, unsigned int *len)
{
    unsigned int i, n = be16(b + 4);
    for (i = 0; i < n; i++)
        if (!memcmp(b + 12 + i * 16, tag, 4)) { *len = be32(b + 12 + i * 16 + 12); return b + be32(b + 12 + i * 16 + 8); }
    return NULL;
}

static int by_offset(const void *x, const void *y)
{
    unsigned int a = be32((const unsigned char *)x + 8), b = be32((const unsigned char *)y + 8);
    return a < b ? -1 : a > b;
}

int main(int argc, char **argv)
{
    long sa, sb;
    unsigned char *a = load(argv[1], &sa), *b = load(argv[2], &sb), dir[64 * 16];
    unsigned int i, n, la, lb, k, differ = 0;

    if (!a || !b) { printf("missing\n"); return 2; }
    n = be16(a + 4);
    for (i = 0; i < n; i++)
    {
        char tag[5] = {0};
        const unsigned char *ta, *tb;
        memcpy(tag, a + 12 + i * 16, 4);
        ta = table(a, tag, &la);
        if (!(tb = table(b, tag, &lb))) { printf("  %s only in the first\n", tag); differ++; continue; }
        if (la == lb && !memcmp(ta, tb, la)) continue;
        if (!strcmp(tag, "head") && la == lb && la >= 12 && !memcmp(ta, tb, 8) && !memcmp(ta + 12, tb + 12, la - 12)) continue;
        for (k = 0; k < la && k < lb && ta[k] == tb[k]; k++) ;
        printf("  %s: %u and %u bytes, first difference at %u\n", tag, la, lb, k);
        differ++;
    }
    if (be16(b + 4) != n) printf("  %u and %u tables\n", n, be16(b + 4));
    for (i = 0; i < 2; i++)
    {
        unsigned char *f = i ? b : a;
        unsigned int c = be16(f + 4), j;
        memcpy(dir, f + 12, c * 16);
        qsort(dir, c, 16, by_offset);
        printf("  %s order:", i ? "second" : "first");
        for (j = 0; j < c; j++) printf(" %.4s", dir + j * 16);
        printf("\n");
    }
    printf("  %u tables differ\n", differ);
    return 0;
}
