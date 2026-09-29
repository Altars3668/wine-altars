/* regbench - how long the registry operations COM and the shell make most often take, through
 * HKEY_CLASSES_ROOT and through HKLM\Software\Classes directly.
 *
 * Windows gives HKEY_CLASSES_ROOT a merged view of the user's classes over the machine's, resolved
 * again at every call.  This times the calls that view costs extra for: opening a class key, opening
 * one that does not exist, reading a value, opening a subkey of a handle opened through HKCR, counting
 * a key's subkeys, and enumerating the whole of CLSID and of the root; and CLSIDFromProgID, which goes
 * through COM's own lookup.  It prints counts and times only, no names.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <windows.h>
#include <objbase.h>
#include <stdio.h>

static LARGE_INTEGER freq;

static double now_us(void)
{
    LARGE_INTEGER t;

    QueryPerformanceCounter(&t);
    return t.QuadPart * 1e6 / freq.QuadPart;
}

/* a CLSID every Windows and every Wine prefix registers: StdFont */
static const WCHAR clsid[] = L"CLSID\\{0BE35203-8F91-11CE-9DE3-00AA004BB851}";

static void bench(HKEY root, const WCHAR *root_name, const WCHAR *prefix, int n)
{
    WCHAR path[256], buf[256];
    DWORD i, size, subkeys, values, count;
    HKEY key, sub;
    double t;
    LSTATUS res = 0;

    printf("through %ls:\n", root_name);

    swprintf(path, ARRAYSIZE(path), L"%ls%ls", prefix, clsid);
    t = now_us();
    for (i = 0; i < n; i++)
    {
        if ((res = RegOpenKeyExW(root, path, 0, KEY_READ, &key))) break;
        RegCloseKey(key);
    }
    printf("  open+close a class key:     %7.2f us (%ld)\n", (now_us() - t) / n, res);

    swprintf(path, ARRAYSIZE(path), L"%lsCLSID\\{00000000-1111-2222-3333-444444444444}", prefix);
    t = now_us();
    for (i = 0; i < n; i++) res = RegOpenKeyExW(root, path, 0, KEY_READ, &key);
    printf("  open a missing class key:   %7.2f us (%ld)\n", (now_us() - t) / n, res);

    swprintf(path, ARRAYSIZE(path), L"%ls%ls", prefix, clsid);
    if (!RegOpenKeyExW(root, path, 0, KEY_READ, &key))
    {
        t = now_us();
        for (i = 0; i < n; i++)
        {
            size = sizeof(buf);
            if ((res = RegQueryValueExW(key, NULL, NULL, NULL, (BYTE *)buf, &size))) break;
        }
        printf("  query the default value:    %7.2f us (%ld)\n", (now_us() - t) / n, res);
        t = now_us();
        for (i = 0; i < n; i++)
        {
            size = sizeof(buf);
            res = RegQueryValueExW(key, L"NoSuchValue", NULL, NULL, (BYTE *)buf, &size);
        }
        printf("  query a missing value:      %7.2f us (%ld)\n", (now_us() - t) / n, res);
        t = now_us();
        for (i = 0; i < n; i++)
        {
            if ((res = RegOpenKeyExW(key, L"InprocServer32", 0, KEY_READ, &sub))) break;
            RegCloseKey(sub);
        }
        printf("  open+close a subkey of it:  %7.2f us (%ld)\n", (now_us() - t) / n, res);
        RegCloseKey(key);
    }

    swprintf(path, ARRAYSIZE(path), L"%lsCLSID", prefix);
    if (!RegOpenKeyExW(root, path, 0, KEY_READ, &key))
    {
        t = now_us();
        res = RegQueryInfoKeyW(key, NULL, NULL, NULL, &subkeys, NULL, NULL, &values, NULL, NULL, NULL, NULL);
        printf("  count CLSID's subkeys:      %7.0f us (%ld, %lu subkeys)\n", now_us() - t, res, subkeys);
        t = now_us();
        for (count = 0; ; count++)
        {
            size = ARRAYSIZE(buf);
            if (RegEnumKeyExW(key, count, buf, &size, NULL, NULL, NULL, NULL)) break;
        }
        printf("  enumerate CLSID:            %7.0f us (%lu keys, %.2f us each)\n", now_us() - t, count,
               (now_us() - t) / (count ? count : 1));
        RegCloseKey(key);
    }

    if (prefix[0]) res = RegOpenKeyExW(root, L"Software\\Classes", 0, KEY_READ, &key);
    else res = RegOpenKeyExW(root, NULL, 0, KEY_READ, &key);
    if (!res)
    {
        t = now_us();
        res = RegQueryInfoKeyW(key, NULL, NULL, NULL, &subkeys, NULL, NULL, &values, NULL, NULL, NULL, NULL);
        printf("  count the root's subkeys:   %7.0f us (%ld, %lu subkeys)\n", now_us() - t, res, subkeys);
        t = now_us();
        for (count = 0; ; count++)
        {
            size = ARRAYSIZE(buf);
            if (RegEnumKeyExW(key, count, buf, &size, NULL, NULL, NULL, NULL)) break;
        }
        printf("  enumerate the root:         %7.0f us (%lu keys, %.2f us each)\n", now_us() - t, count,
               (now_us() - t) / (count ? count : 1));
        RegCloseKey(key);
    }
}

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 2000;
    CLSID id;
    double t;
    HRESULT hr = E_FAIL;
    int i;

    QueryPerformanceFrequency(&freq);
    bench(HKEY_CLASSES_ROOT, L"HKEY_CLASSES_ROOT", L"", n);
    bench(HKEY_LOCAL_MACHINE, L"HKLM\\Software\\Classes", L"Software\\Classes\\", n);

    CoInitialize(NULL);
    t = now_us();
    for (i = 0; i < n; i++) hr = CLSIDFromProgID(L"StdFont", &id);
    printf("CLSIDFromProgID:              %7.2f us (%#lx)\n", (now_us() - t) / n, hr);
    CoUninitialize();
    return 0;
}
