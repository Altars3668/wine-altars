/*
 * 把安装目录里的许可逐份交给 SLInstallLicense，数它登记了多少。
 *
 * Click-to-Run 在安装时做的就是这件事，但只做它当次用得上的那一小撮。这个
 * 工具把同一个目录里的每一份都交一遍，于是能把两件事分开：交进去登记不上
 * （sppc 的实现有漏），和根本没交进去（安装流程没走到）。
 *
 *     sppcinstall <licenses-dir> [--apply]
 *
 * 不带 --apply 时只统计，不调用任何写入。
 */
#define COBJMACROS
#include <windows.h>
#include <stdio.h>

typedef PVOID HSLC;
typedef GUID SLID;
typedef HRESULT (WINAPI *sl_open_t)(HSLC *);
typedef HRESULT (WINAPI *sl_close_t)(HSLC);
typedef HRESULT (WINAPI *sl_install_t)(HSLC, UINT, const BYTE *, SLID *);

static sl_open_t sl_open;
static sl_close_t sl_close;
static sl_install_t sl_install;

static BYTE *read_file(const WCHAR *path, DWORD *size)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    BYTE *data;
    DWORD got = 0;

    if (f == INVALID_HANDLE_VALUE) return NULL;
    *size = GetFileSize(f, NULL);
    if (*size == INVALID_FILE_SIZE || *size > 4 * 1024 * 1024) { CloseHandle(f); return NULL; }
    if (!(data = malloc(*size))) { CloseHandle(f); return NULL; }
    if (!ReadFile(f, data, *size, &got, NULL) || got != *size) { free(data); CloseHandle(f); return NULL; }
    CloseHandle(f);
    return data;
}

/* 目录里有多少个 SKU 键；登记前后各数一次就知道这一轮新增了多少。 */
static DWORD count_skus(void)
{
    HKEY apps;
    DWORD total = 0, i = 0;
    WCHAR app[64];

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Wine\\SPPC\\Applications", 0, KEY_READ, &apps))
    {
        printf("  打不开 Applications 键，错误 %lu\n", GetLastError());
        return 0;
    }
    for (i = 0; ; i++)
    {
        DWORD len = ARRAYSIZE(app);
        HKEY skus;
        WCHAR path[160];
        DWORD n = 0;

        if (RegEnumKeyExW(apps, i, app, &len, NULL, NULL, NULL, NULL)) break;
        printf("  app %ls\n", app);
        swprintf(path, ARRAYSIZE(path), L"%ls\\Skus", app);
        if (RegOpenKeyExW(apps, path, 0, KEY_READ, &skus))
        {
            printf("  打不开 %ls，错误 %lu\n", path, GetLastError());
            continue;
        }
        {
            LSTATUS st = RegQueryInfoKeyW(skus, NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
            if (st) printf("  查询 %ls 失败 %ld\n", path, (long)st);
        }
        RegCloseKey(skus);
        total += n;
    }
    RegCloseKey(apps);
    return total;
}

int wmain(int argc, WCHAR **argv)
{
    WCHAR pattern[MAX_PATH], path[MAX_PATH];
    WIN32_FIND_DATAW find;
    HMODULE module;
    HANDLE search;
    HSLC handle = NULL;
    HRESULT hr;
    DWORD files = 0, ok = 0, failed = 0, before, after;
    BOOL apply = (argc > 2 && !wcscmp(argv[2], L"--apply"));
    struct { HRESULT hr; DWORD n; } codes[16] = {0};
    int ncodes = 0, i;

    if (argc < 2)
    {
        printf("usage: sppcinstall <licenses-dir> [--apply]\n");
        return 2;
    }

    /* 目录名里有空格，调用方通常要加引号；引号会原样留在参数里。 */
    if (argv[1][0] == '"')
    {
        size_t len = wcslen(argv[1]);
        argv[1]++;
        if (len >= 2 && argv[1][len - 2] == '"') argv[1][len - 2] = 0;
    }

    if (!(module = LoadLibraryW(L"sppc.dll"))) { printf("no sppc.dll\n"); return 1; }
    sl_open = (void *)GetProcAddress(module, "SLOpen");
    sl_close = (void *)GetProcAddress(module, "SLClose");
    sl_install = (void *)GetProcAddress(module, "SLInstallLicense");
    if (!sl_open || !sl_close || !sl_install) { printf("sppc.dll 缺少入口\n"); return 1; }

    before = count_skus();
    printf("登记前目录里的 SKU: %lu\n", before);

    if (!apply)
    {
        printf("（只统计，不写入；加 --apply 才真正登记）\n");
    }
    else if (FAILED(hr = sl_open(&handle)))
    {
        printf("SLOpen 失败 0x%08lx\n", (unsigned long)hr);
        return 1;
    }

    swprintf(pattern, ARRAYSIZE(pattern), L"%ls\\*.xrm-ms", argv[1]);
    search = FindFirstFileW(pattern, &find);
    if (search == INVALID_HANDLE_VALUE)
    {
        printf("找不到许可: pattern=%ls  错误 %lu\n", pattern, GetLastError());
        return 1;
    }
    do
    {
        BYTE *data;
        DWORD size = 0;
        SLID id = {0};

        files++;
        if (!apply) continue;
        swprintf(path, ARRAYSIZE(path), L"%ls\\%ls", argv[1], find.cFileName);
        if (!(data = read_file(path, &size))) { failed++; continue; }
        hr = sl_install(handle, size, data, &id);
        free(data);
        if (SUCCEEDED(hr)) ok++;
        else
        {
            failed++;
            for (i = 0; i < ncodes; i++) if (codes[i].hr == hr) break;
            if (i == ncodes && ncodes < 16) { codes[ncodes].hr = hr; codes[ncodes].n = 0; ncodes++; }
            if (i < 16) codes[i].n++;
            if (failed <= 8) printf("   拒绝: %ls  (0x%08lx, %lu 字节)\n", find.cFileName, (unsigned long)hr, size);
        }
    } while (FindNextFileW(search, &find));
    FindClose(search);

    if (apply) sl_close(handle);
    after = count_skus();

    printf("许可文件            %lu\n", files);
    if (apply)
    {
        printf("SLInstallLicense 成功 %lu，失败 %lu\n", ok, failed);
        for (i = 0; i < ncodes; i++)
            printf("   失败码 0x%08lx  %lu 次\n", (unsigned long)codes[i].hr, codes[i].n);
        printf("登记后目录里的 SKU: %lu  (本轮新增 %ld)\n", after, (long)after - (long)before);
    }
    return 0;
}
