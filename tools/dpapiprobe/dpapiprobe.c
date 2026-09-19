/*
 * dpapiprobe -- can this prefix keep a secret between two runs?
 *
 * Office seals its signed-in identity with DPAPI and reads it back on the next
 * start.  If CryptProtectData works but the master key does not survive the
 * process, sign-in appears to succeed and is gone by the next launch -- which
 * looks like "it logged out again" and is easy to blame on the account.
 *
 * This writes a sealed blob in one run and unseals it in another, so the two
 * halves are genuinely separate processes.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -municode \
 *       -o dpapiprobe.exe dpapiprobe.c -lcrypt32
 *
 *   dpapiprobe seal   <file>
 *   dpapiprobe unseal <file>
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>

static const char secret[] = "wine-altars dpapi round trip";

static int seal(const WCHAR *path)
{
    DATA_BLOB in = { sizeof(secret), (BYTE *)secret }, out = { 0 };
    HANDLE f;
    DWORD written;

    if (!CryptProtectData(&in, L"probe", NULL, NULL, NULL, 0, &out))
    {
        printf("  CryptProtectData 失败 %lu\n", GetLastError());
        return 1;
    }
    printf("  封装成功，%lu 字节\n", out.cbData);
    f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) { printf("  写不了文件 %lu\n", GetLastError()); return 1; }
    WriteFile(f, out.pbData, out.cbData, &written, NULL);
    CloseHandle(f);
    LocalFree(out.pbData);
    printf("  已写入 %lu 字节\n", written);
    return 0;
}

static int unseal(const WCHAR *path)
{
    DATA_BLOB in = { 0 }, out = { 0 };
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    DWORD size, got;
    BYTE *buf;

    if (f == INVALID_HANDLE_VALUE) { printf("  读不了文件 %lu\n", GetLastError()); return 1; }
    size = GetFileSize(f, NULL);
    buf = malloc(size);
    ReadFile(f, buf, size, &got, NULL);
    CloseHandle(f);
    in.cbData = got; in.pbData = buf;

    if (!CryptUnprotectData(&in, NULL, NULL, NULL, NULL, 0, &out))
    {
        printf("  CryptUnprotectData 失败 %lu  <- 跨进程解不开，登录不会持久\n", GetLastError());
        free(buf);
        return 1;
    }
    printf("  解封成功: %s\n", (out.cbData == sizeof(secret) &&
           !memcmp(out.pbData, secret, sizeof(secret))) ? "内容一致 ✓" : "内容不符 !!");
    LocalFree(out.pbData);
    free(buf);
    return 0;
}

int wmain(int argc, WCHAR **argv)
{
    if (argc < 3) { printf("用法: dpapiprobe <seal|unseal> <文件>\n"); return 2; }
    if (!wcscmp(argv[1], L"seal"))   return seal(argv[2]);
    if (!wcscmp(argv[1], L"unseal")) return unseal(argv[2]);
    printf("未知动作\n");
    return 2;
}
