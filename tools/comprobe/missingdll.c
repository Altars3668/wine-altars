/*
 * What COM returns when the in-process server it is asked for cannot be loaded: a class registered under
 * HKCU\Software\Classes\CLSID with an InprocServer32 that does not exist, and a class that an activation context
 * declares in a file that does not exist -- the way OfficeClickToRun.exe's manifest keeps InfoPath's XML MIME
 * filter out of the service.  Also a server that exists but is not a DLL.  The registry key is removed after.
 */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <stdio.h>

static const CLSID clsid_missing = {0x6d0f3a51, 0x2c1e, 0x4b8a, {0x9f, 0x3d, 0x51, 0x7a, 0x0e, 0x2b, 0x44, 0x91}};
static const CLSID clsid_manifest = {0x6d0f3a52, 0x2c1e, 0x4b8a, {0x9f, 0x3d, 0x51, 0x7a, 0x0e, 0x2b, 0x44, 0x91}};
static const CLSID clsid_notdll = {0x6d0f3a53, 0x2c1e, 0x4b8a, {0x9f, 0x3d, 0x51, 0x7a, 0x0e, 0x2b, 0x44, 0x91}};

static void create(const char *name, const CLSID *clsid)
{
    IUnknown *unk = NULL;
    void *factory = NULL;
    HRESULT hr;

    SetLastError(0xdeadbeef);
    hr = CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, &IID_IUnknown, (void **)&unk);
    printf("%s: CoCreateInstance %#lx, last error %lu\n", name, hr, GetLastError());
    if (unk) IUnknown_Release(unk);
    hr = CoGetClassObject(clsid, CLSCTX_INPROC_SERVER, NULL, &IID_IClassFactory, &factory);
    printf("%s: CoGetClassObject %#lx\n", name, hr);
    if (factory) IUnknown_Release((IUnknown *)factory);
}

static BOOL register_server(const CLSID *clsid, const char *path)
{
    char key[128];
    HKEY hkey;
    LONG ret;

    sprintf(key, "Software\\Classes\\CLSID\\{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}\\InprocServer32",
            clsid->Data1, clsid->Data2, clsid->Data3, clsid->Data4[0], clsid->Data4[1], clsid->Data4[2],
            clsid->Data4[3], clsid->Data4[4], clsid->Data4[5], clsid->Data4[6], clsid->Data4[7]);
    if ((ret = RegCreateKeyExA(HKEY_CURRENT_USER, key, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hkey, NULL)))
    {
        printf("RegCreateKeyEx %ld\n", ret);
        return FALSE;
    }
    RegSetValueExA(hkey, NULL, 0, REG_SZ, (const BYTE *)path, strlen(path) + 1);
    RegSetValueExA(hkey, "ThreadingModel", 0, REG_SZ, (const BYTE *)"Both", 5);
    RegCloseKey(hkey);
    return TRUE;
}

static void unregister_server(const CLSID *clsid)
{
    char key[128];

    sprintf(key, "Software\\Classes\\CLSID\\{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
            clsid->Data1, clsid->Data2, clsid->Data3, clsid->Data4[0], clsid->Data4[1], clsid->Data4[2],
            clsid->Data4[3], clsid->Data4[4], clsid->Data4[5], clsid->Data4[6], clsid->Data4[7]);
    printf("unregister: %ld\n", RegDeleteTreeA(HKEY_CURRENT_USER, key));
}

int main(void)
{
    static const char manifest[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">\n"
        "  <assemblyIdentity type=\"win32\" name=\"WineAltars.MissingDll\" version=\"1.0.0.0\"/>\n"
        "  <file name=\"wine_altars_missing.dll\">\n"
        "    <comClass clsid=\"{6D0F3A52-2C1E-4B8A-9F3D-517A0E2B4491}\" threadingModel=\"Apartment\"/>\n"
        "  </file>\n"
        "</assembly>\n";
    char dir[MAX_PATH], path[MAX_PATH], notdll[MAX_PATH];
    ULONG_PTR cookie;
    ACTCTXA ctx = {0};
    HANDLE actctx, file;
    DWORD written;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    GetTempPathA(sizeof(dir), dir);

    /* Registered, missing. */
    sprintf(path, "%swine_altars_missing.dll", dir);
    if (register_server(&clsid_missing, path))
    {
        create("registered, missing", &clsid_missing);
        unregister_server(&clsid_missing);
    }

    /* Registered, a text file. */
    sprintf(notdll, "%swine_altars_notdll.dll", dir);
    file = CreateFileA(notdll, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    WriteFile(file, "not a dll", 9, &written, NULL);
    CloseHandle(file);
    if (register_server(&clsid_notdll, notdll))
    {
        create("registered, not a dll", &clsid_notdll);
        unregister_server(&clsid_notdll);
    }
    DeleteFileA(notdll);

    /* Declared by an activation context, missing. */
    sprintf(path, "%swine_altars_missing.manifest", dir);
    file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    WriteFile(file, manifest, sizeof(manifest) - 1, &written, NULL);
    CloseHandle(file);
    ctx.cbSize = sizeof(ctx);
    ctx.lpSource = path;
    actctx = CreateActCtxA(&ctx);
    if (actctx == INVALID_HANDLE_VALUE)
        printf("CreateActCtx %lu\n", GetLastError());
    else if (ActivateActCtx(actctx, &cookie))
    {
        create("manifest, missing", &clsid_manifest);
        DeactivateActCtx(0, cookie);
        ReleaseActCtx(actctx);
    }
    DeleteFileA(path);

    /* Not registered at all. */
    create("unregistered", &clsid_manifest);

    CoUninitialize();
    printf("done\n");
    return 0;
}
