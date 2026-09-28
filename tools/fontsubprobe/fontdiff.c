/* fontdiff <original> <face> <a> <b>: how two subsets of a font differ in the tables that describe glyphs */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned int be16(const unsigned char *p) { return (p[0] << 8) | p[1]; }
static unsigned int be32(const unsigned char *p) { return ((unsigned int)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

struct font { unsigned char *data; long size; unsigned int dir; };

static int load(const char *path, unsigned int face, struct font *f)
{
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    fseek(file, 0, SEEK_END); f->size = ftell(file); fseek(file, 0, SEEK_SET);
    f->data = malloc(f->size); fread(f->data, 1, f->size, file); fclose(file);
    f->dir = !memcmp(f->data, "ttcf", 4) ? be32(f->data + 12 + 4 * face) : 0;
    return 1;
}

static const unsigned char *table(const struct font *f, const char *tag, unsigned int *len)
{
    unsigned int i, n = be16(f->data + f->dir + 4);
    for (i = 0; i < n; i++)
    {
        const unsigned char *e = f->data + f->dir + 12 + i * 16;
        if (!memcmp(e, tag, 4)) { *len = be32(e + 12); return f->data + be32(e + 8); }
    }
    *len = 0;
    return NULL;
}

static void cmaps(const char *label, const struct font *f)
{
    unsigned int len, i, n;
    const unsigned char *c = table(f, "cmap", &len);
    if (!c) return;
    n = be16(c + 2);
    printf("  cmap %s %u:", label, len);
    for (i = 0; i < n; i++)
    {
        const unsigned char *s = c + be32(c + 8 + i * 8);
        unsigned int fmt = be16(s), l = fmt >= 8 && fmt != 14 ? be32(s + 4) : fmt == 14 ? be32(s + 2) : be16(s + 2);
        printf(" %u/%u@%u:f%u,%u", be16(c + 4 + i * 8), be16(c + 6 + i * 8), be32(c + 8 + i * 8), fmt, l);
        if (fmt == 4) printf(",seg%u", be16(s + 6) / 2);
        if (fmt == 12) printf(",grp%u", be32(s + 12));
    }
    printf("\n");
}

int main(int argc, char **argv)
{
    struct font o, a, b;
    const unsigned char *ta, *tb, *to;
    unsigned int la, lb, lo, i, shown, glyphs;

    if (argc < 5 || !load(argv[1], atoi(argv[2]), &o) || !load(argv[3], 0, &a) || !load(argv[4], 0, &b)) return 2;
    to = table(&o, "maxp", &lo); glyphs = be16(to + 4);
    if ((ta = table(&a, "OS/2", &la)) && (tb = table(&b, "OS/2", &lb)) && (to = table(&o, "OS/2", &lo)) && memcmp(ta, tb, la))
        printf("  OS/2 chars: original %04x-%04x, first %04x-%04x, second %04x-%04x\n", be16(to + 64), be16(to + 66),
               be16(ta + 64), be16(ta + 66), be16(tb + 64), be16(tb + 66));
    if ((ta = table(&a, "LTSH", &la)) && (tb = table(&b, "LTSH", &lb)) && (to = table(&o, "LTSH", &lo)) && memcmp(ta, tb, la))
    {
        printf("  LTSH (glyph: original first second):");
        for (i = 0, shown = 0; i < glyphs && shown < 12; i++)
            if (ta[4 + i] != tb[4 + i]) { printf(" %u:%u/%u/%u", i, to[4 + i], ta[4 + i], tb[4 + i]); shown++; }
        printf("\n");
    }
    if ((ta = table(&a, "hdmx", &la)) && (tb = table(&b, "hdmx", &lb)) && (to = table(&o, "hdmx", &lo)) && memcmp(ta, tb, la))
    {
        unsigned int rec = be32(ta + 4), r;
        for (r = 0; r < be16(ta + 2) && r < 2; r++)
        {
            printf("  hdmx record %u ppem %u (glyph: original first second):", r, ta[8 + r * rec]);
            for (i = 0, shown = 0; i < glyphs && shown < 10; i++)
                if (ta[8 + r * rec + 2 + i] != tb[8 + r * rec + 2 + i])
                { printf(" %u:%u/%u/%u", i, to[8 + r * rec + 2 + i], ta[8 + r * rec + 2 + i], tb[8 + r * rec + 2 + i]); shown++; }
            printf("\n");
        }
    }
    {
        static const char *const m[2][2] = { { "hmtx", "hhea" }, { "vmtx", "vhea" } };
        int k;
        for (k = 0; k < 2; k++)
        {
            unsigned int lha, lhb, lho, na, nb, no;
            const unsigned char *ha = table(&a, m[k][1], &lha), *hb = table(&b, m[k][1], &lhb), *ho = table(&o, m[k][1], &lho);
            if (!(ta = table(&a, m[k][0], &la)) || !(tb = table(&b, m[k][0], &lb)) || !(to = table(&o, m[k][0], &lo))) continue;
            if (la == lb && !memcmp(ta, tb, la)) continue;
            na = be16(ha + 34); nb = be16(hb + 34); no = be16(ho + 34);
            printf("  %s: long metrics original %u, first %u, second %u (glyph: original first second):", m[k][0], no, na, nb);
            for (i = 0, shown = 0; i < glyphs && shown < 8; i++)
            {
                unsigned int va, vb, vo, sa, sb, so;
                va = i < na ? be16(ta + i * 4) : be16(ta + (na - 1) * 4); sa = i < na ? be16(ta + i * 4 + 2) : be16(ta + na * 4 + (i - na) * 2);
                vb = i < nb ? be16(tb + i * 4) : be16(tb + (nb - 1) * 4); sb = i < nb ? be16(tb + i * 4 + 2) : be16(tb + nb * 4 + (i - nb) * 2);
                vo = i < no ? be16(to + i * 4) : be16(to + (no - 1) * 4); so = i < no ? be16(to + i * 4 + 2) : be16(to + no * 4 + (i - no) * 2);
                if (va != vb || sa != sb) { printf(" %u:%u,%d/%u,%d/%u,%d", i, vo, (short)so, va, (short)sa, vb, (short)sb); shown++; }
            }
            printf("\n");
        }
    }
    if ((ta = table(&a, "cmap", &la)) && (tb = table(&b, "cmap", &lb)) && (la != lb || memcmp(ta, tb, la)))
    {
        cmaps("original", &o); cmaps("first", &a); cmaps("second", &b);
    }
    if ((ta = table(&a, "head", &la)) && (tb = table(&b, "head", &lb)) && be16(ta + 50) != be16(tb + 50))
        printf("  loca format: first %u, second %u\n", be16(ta + 50), be16(tb + 50));
    if ((ta = table(&a, "glyf", &la)) && (tb = table(&b, "glyf", &lb)) && la != lb)
    {
        const unsigned char *hla = table(&a, "head", &lo), *hlb = table(&b, "head", &lo), *loa = table(&a, "loca", &lo), *lob = table(&b, "loca", &lo);
        printf("  glyf %u and %u bytes; glyphs (first second):", la, lb);
        for (i = 0, shown = 0; i < glyphs && shown < 12; i++)
        {
            unsigned int sa = be16(hla + 50) ? be32(loa + i * 4) : be16(loa + i * 2) * 2, ea = be16(hla + 50) ? be32(loa + i * 4 + 4) : be16(loa + i * 2 + 2) * 2;
            unsigned int sb = be16(hlb + 50) ? be32(lob + i * 4) : be16(lob + i * 2) * 2, eb = be16(hlb + 50) ? be32(lob + i * 4 + 4) : be16(lob + i * 2 + 2) * 2;
            if (ea > sa || eb > sb) { printf(" %u:%u@%u/%u@%u", i, ea - sa, sa, eb - sb, sb); shown++; }
        }
        printf("\n");
    }
    if ((ta = table(&a, "name", &la)) && (tb = table(&b, "name", &lb)) && (la != lb || memcmp(ta, tb, la)))
    {
        unsigned int n = be16(ta + 2);
        printf("  name %u and %u bytes, records at other places:", la, lb);
        for (i = 0, shown = 0; i < n && shown < 10; i++)
            if (be16(ta + 6 + i * 12 + 10) != be16(tb + 6 + i * 12 + 10))
            {
                printf(" #%u(%u/%u/%x id %u len %u)@%u/%u", i, be16(ta + 6 + i * 12), be16(ta + 8 + i * 12), be16(ta + 10 + i * 12),
                       be16(ta + 12 + i * 12), be16(ta + 14 + i * 12), be16(ta + 6 + i * 12 + 10), be16(tb + 6 + i * 12 + 10));
                shown++;
            }
        printf("\n");
    }
    return 0;
}
