/* What OaBuildVersion() answers, next to GetVersion() and oleaut32.dll's file version.
 *
 * Office asks at every start; Wine's table stops at Windows 8, and for a program manifested for
 * Windows 10 and later (so GetVersion() says 10.0) it logs a FIXME and guesses.
 *
 * It carries a manifest saying it supports Windows 10, as Office's executables do, so that GetVersion() is not capped:
 *
 *   printf '1 24 "oaversionprobe.manifest"\n' > m.rc && x86_64-w64-mingw32-windres m.rc -O coff -o m.res
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror oaversionprobe.c m.res -o oaversionprobe.exe -loleaut32 -lversion
 */
#include <windows.h>
#include <oleauto.h>
#include <stdio.h>

int main(void)
{
    DWORD version = GetVersion(), size, handle;
    VS_FIXEDFILEINFO *info;
    UINT len;
    void *data;

    printf("GetVersion %#lx, OaBuildVersion %#lx\n", version, OaBuildVersion());
    size = GetFileVersionInfoSizeA("oleaut32.dll", &handle);
    if (size && (data = malloc(size)) && GetFileVersionInfoA("oleaut32.dll", 0, size, data)
            && VerQueryValueA(data, "\\", (void **)&info, &len))
        printf("oleaut32.dll %u.%u.%u.%u\n", HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
               HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
    return 0;
}
