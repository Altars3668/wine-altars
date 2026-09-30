/* What MultiByteToWideChar makes of a double-byte lead byte that is followed by a byte that cannot trail it.
 *
 * Text read in the ANSI code page is often not in it -- UTF-8 read as GBK is the everyday case on a Chinese
 * system -- and a lead byte then meets a quote, a comma, a space or a line break.  Wine takes the pair as one
 * invalid character, so the ASCII byte disappears; kernel32's tests only show that Windows keeps a NUL
 * after a lead byte.  For the East Asian code pages this converts a lead byte followed by each kind of byte, and
 * a lead byte at the end, without and with MB_ERR_INVALID_CHARS, and prints the characters that come out.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror dbcsprobe.c -o dbcsprobe.exe
 */
#include <windows.h>
#include <stdio.h>

static void convert(UINT cp, const unsigned char *bytes, int len, DWORD flags)
{
    WCHAR out[16];
    int n, i;

    SetLastError(0xdeadbeef);
    n = MultiByteToWideChar(cp, flags, (const char *)bytes, len, out, ARRAYSIZE(out));
    printf("  %u", cp);
    for (i = 0; i < len; i++) printf(" %02x", bytes[i]);
    printf("%s -> %d", flags ? " strict" : "", n);
    if (!n) printf(" (error %lu)", GetLastError());
    for (i = 0; i < n; i++) printf(" %04x", out[i]);
    printf("\n");
}

int main(void)
{
    static const UINT cps[] = {936, 932, 949, 950};
    static const unsigned char leads[] = {0x81, 0xb4, 0xfe};
    static const unsigned char trails[] = {0x00, 0x09, 0x0a, 0x20, 0x22, 0x2c, 0x30, 0x3f, 0x40, 0x5c, 0x7e, 0x7f, 0x80, 0xa0, 0xff};
    unsigned int c, l, t;
    unsigned char buf[4];

    for (c = 0; c < ARRAYSIZE(cps); c++)
    {
        CPINFO info;
        if (!GetCPInfo(cps[c], &info)) { printf("%u: not installed\n", cps[c]); continue; }
        printf("%u: max %u, default char %02x %02x\n", cps[c], info.MaxCharSize, info.DefaultChar[0], info.DefaultChar[1]);
        for (l = 0; l < ARRAYSIZE(leads); l++)
        {
            if (!IsDBCSLeadByteEx(cps[c], leads[l])) continue;
            for (t = 0; t < ARRAYSIZE(trails); t++)
            {
                buf[0] = leads[l]; buf[1] = trails[t]; buf[2] = 'x';
                convert(cps[c], buf, 3, 0);
                if (trails[t] == 0x22 || trails[t] == 0x20) convert(cps[c], buf, 3, MB_ERR_INVALID_CHARS);
            }
            buf[0] = 'x'; buf[1] = leads[l];
            convert(cps[c], buf, 2, 0);
        }
    }
    return 0;
}
