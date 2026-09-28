/* namedump <font> <face>: the name table's records in the order of their strings in the storage */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned int be16(const unsigned char *p) { return (p[0] << 8) | p[1]; }
static unsigned int be32(const unsigned char *p) { return ((unsigned int)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static const unsigned char *records;

static int by_offset(const void *a, const void *b)
{
    unsigned int x = *(const unsigned int *)a, y = *(const unsigned int *)b;
    unsigned int ox = be16(records + x * 12 + 10), oy = be16(records + y * 12 + 10);
    if (ox != oy) return ox < oy ? -1 : 1;
    return be16(records + y * 12 + 8) - be16(records + x * 12 + 8);
}

int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    unsigned char *b, *name = NULL;
    unsigned int dir = 0, i, n, count, storage, *order;
    long size;

    if (!f) return 2;
    fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc(size); fread(b, 1, size, f); fclose(f);
    if (!memcmp(b, "ttcf", 4)) dir = be32(b + 12 + 4 * atoi(argv[2]));
    n = be16(b + dir + 4);
    for (i = 0; i < n; i++) if (!memcmp(b + dir + 12 + i * 16, "name", 4)) name = b + be32(b + dir + 12 + i * 16 + 8);
    if (!name) return 3;
    count = be16(name + 2); storage = be16(name + 4); records = name + 6;
    order = malloc(count * sizeof(*order));
    for (i = 0; i < count; i++) order[i] = i;
    qsort(order, count, sizeof(*order), by_offset);
    for (i = 0; i < count; i++)
    {
        const unsigned char *r = records + order[i] * 12, *s = name + storage + be16(r + 10);
        unsigned int len = be16(r + 8), k, wide = be16(r) != 1;
        char text[25];
        for (k = 0; k < 24 && k < (wide ? len / 2 : len); k++)
        {
            unsigned int c = wide ? be16(s + k * 2) : s[k];
            text[k] = c >= 0x20 && c < 0x7f ? c : '.';
        }
        text[k] = 0;
        printf("  @%5u %5u #%-2u %u/%u/%x id %-2u %s\n", be16(r + 10), len, order[i], be16(r), be16(r + 2), be16(r + 4), be16(r + 6), text);
    }
    return 0;
}
