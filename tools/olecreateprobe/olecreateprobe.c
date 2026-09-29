/* olecreateprobe - what OleCreate() makes of a class with no in-process server.
 *
 * CrossOver falls back to OleCreateDefaultHandler() whenever
 * CoCreateInstance(CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER) fails inside OleCreate().  This
 * registers a class of its own under HKCU\Software\Classes in a few shapes -- nothing at all, a
 * LocalServer32 alone, ole32.dll as its InprocHandler32 with and without a LocalServer32 -- and for
 * each prints what CoCreateInstance and OleCreate answer, and whether the object OleCreate gives
 * back is running.  The LocalServer32 names an executable that does not exist, so running the
 * object can only fail.  The registration is removed again at the end.  "olecreateprobe hklm"
 * registers under HKEY_LOCAL_MACHINE instead, which needs an elevated process on Windows.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <windows.h>
#include <ole2.h>
#include <stdio.h>

static const CLSID clsid = {0x6f1b4a52, 0x8c3e, 0x4d2a, {0x9b, 0x71, 0x2e, 0x5c, 0x0d, 0x8a, 0x4f, 0x13}};
static const WCHAR key[] = L"Software\\Classes\\CLSID\\{6F1B4A52-8C3E-4D2A-9B71-2E5C0D8A4F13}";
static HKEY root = HKEY_CURRENT_USER;

static void set_value(const WCHAR *sub, const WCHAR *value)
{
    WCHAR path[256];
    HKEY hkey;

    swprintf(path, ARRAYSIZE(path), L"%ls%ls%ls", key, sub ? L"\\" : L"", sub ? sub : L"");
    if (RegCreateKeyExW(root, path, 0, NULL, 0, KEY_WRITE, NULL, &hkey, NULL))
    {
        printf("cannot create %ls\n", path);
        return;
    }
    RegSetValueExW(hkey, NULL, 0, REG_SZ, (const BYTE *)value, (wcslen(value) + 1) * sizeof(WCHAR));
    RegCloseKey(hkey);
}

static void clear(void)
{
    RegDeleteTreeW(root, key);
}

static void create(const char *what, DWORD render)
{
    IOleObject *object = NULL;
    IRunnableObject *runnable;
    ILockBytes *bytes;
    IStorage *storage;
    IUnknown *unknown;
    FORMATETC format = { CF_METAFILEPICT, NULL, DVASPECT_CONTENT, -1, TYMED_MFPICT };
    CLSID got;
    HRESULT hr;

    hr = CoCreateInstance(&clsid, NULL, CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER, &IID_IUnknown,
                          (void **)&unknown);
    printf("%-34s render %lu  CoCreateInstance(inproc|handler) %#lx", what, render, hr);
    if (SUCCEEDED(hr)) IUnknown_Release(unknown);

    CreateILockBytesOnHGlobal(NULL, TRUE, &bytes);
    StgCreateDocfileOnILockBytes(bytes, STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, &storage);
    hr = OleCreate(&clsid, &IID_IOleObject, render, render == OLERENDER_FORMAT ? &format : NULL, NULL, storage,
                   (void **)&object);
    printf("  OleCreate %#lx", hr);
    if (object)
    {
        memset(&got, 0, sizeof(got));
        hr = IOleObject_GetUserClassID(object, &got);
        printf("  class %s  running %d", SUCCEEDED(hr) && IsEqualCLSID(&got, &clsid) ? "same" : "other",
               OleIsRunning(object));
        if (SUCCEEDED(IOleObject_QueryInterface(object, &IID_IRunnableObject, (void **)&runnable)))
        {
            printf("  Run %#lx", IRunnableObject_Run(runnable, NULL));
            IRunnableObject_Release(runnable);
        }
        IOleObject_Release(object);
    }
    printf("\n");
    IStorage_Release(storage);
    ILockBytes_Release(bytes);
}

static void both(const char *what)
{
    create(what, OLERENDER_NONE);
    create(what, OLERENDER_DRAW);
}

int main(int argc, char **argv)
{
    static const WCHAR missing[] = L"C:\\olecreateprobe-missing\\server.exe";

    if (argc > 1 && !strcmp(argv[1], "hklm")) root = HKEY_LOCAL_MACHINE;
    printf("registered under %s\n", root == HKEY_LOCAL_MACHINE ? "HKEY_LOCAL_MACHINE" : "HKEY_CURRENT_USER");
    OleInitialize(NULL);

    clear();
    both("not registered");

    set_value(NULL, L"olecreateprobe");
    both("key only");

    set_value(L"LocalServer32", missing);
    both("LocalServer32 only");

    set_value(L"InprocHandler32", L"ole32.dll");
    both("InprocHandler32 ole32 + LocalServer32");

    clear();
    set_value(NULL, L"olecreateprobe");
    set_value(L"InprocHandler32", L"ole32.dll");
    both("InprocHandler32 ole32 only");

    clear();
    OleUninitialize();
    return 0;
}
