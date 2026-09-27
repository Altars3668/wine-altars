/* Check that an out-of-process class registration dies with its server. */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <string.h>

static const GUID clsid = {0x719a0a61, 0x2bfc, 0x4b49, {0xb8, 0xaa, 0x2f, 0x20, 0x24, 0x67, 0x58, 0x02}};
static const GUID iid_unknown = {0x00000000, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID iid_factory = {0x00000001, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

static HRESULT WINAPI factory_QueryInterface(IClassFactory *iface, REFIID iid, void **object)
{
    if (IsEqualGUID(iid, &iid_unknown) || IsEqualGUID(iid, &iid_factory))
    {
        *object = iface;
        return S_OK;
    }
    *object = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef(IClassFactory *iface)
{
    return 2;
}

static ULONG WINAPI factory_Release(IClassFactory *iface)
{
    return 1;
}

static HRESULT WINAPI factory_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID iid, void **object)
{
    *object = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI factory_LockServer(IClassFactory *iface, BOOL lock)
{
    return S_OK;
}

static const IClassFactoryVtbl factory_vtbl = {
    factory_QueryInterface, factory_AddRef, factory_Release, factory_CreateInstance, factory_LockServer
};
static IClassFactory factory = {&factory_vtbl};

int main(int argc, char **argv)
{
    char path[MAX_PATH], command[MAX_PATH + 128], name[80];
    PROCESS_INFORMATION process;
    STARTUPINFOA startup = {sizeof(startup)};
    IClassFactory *proxy = NULL;
    HANDLE ready, handles[2];
    DWORD cookie, result;
    HRESULT hr, first, second;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (FAILED(hr = CoInitializeEx(NULL, COINIT_MULTITHREADED)))
    {
        printf("COM init %#lx\n", hr);
        return 1;
    }

    if (argc == 3 && !strcmp(argv[1], "--server"))
    {
        if (!(ready = OpenEventA(EVENT_MODIFY_STATE, FALSE, argv[2]))) return 2;
        hr = CoRegisterClassObject(&clsid, (IUnknown *)&factory, CLSCTX_LOCAL_SERVER, REGCLS_MULTIPLEUSE, &cookie);
        printf("registered %#lx\n", hr);
        if (FAILED(hr)) return 3;
        SetEvent(ready);
        CloseHandle(ready);
        Sleep(INFINITE); /* Parent terminates us without CoRevokeClassObject. */
        return 0;
    }

    snprintf(name, sizeof(name), "Local\\WineAltarsClassProbe-%lu", GetCurrentProcessId());
    if (!(ready = CreateEventA(NULL, TRUE, FALSE, name))) return 4;
    GetModuleFileNameA(NULL, path, sizeof(path));
    snprintf(command, sizeof(command), "\"%s\" --server %s", path, name);
    if (!CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process))
    {
        printf("server launch %lu\n", GetLastError());
        CloseHandle(ready);
        return 5;
    }
    CloseHandle(process.hThread);
    handles[0] = ready;
    handles[1] = process.hProcess;
    result = WaitForMultipleObjects(2, handles, FALSE, 20000);
    printf("server ready %lu\n", result);
    if (result == WAIT_OBJECT_0)
    {
        first = CoGetClassObject(&clsid, CLSCTX_LOCAL_SERVER, NULL, &iid_factory, (void **)&proxy);
        printf("before exit %#lx\n", first);
        if (proxy) IClassFactory_Release(proxy);
    }
    else first = E_FAIL;

    TerminateProcess(process.hProcess, 0);
    WaitForSingleObject(process.hProcess, 10000);
    CloseHandle(process.hProcess);
    CloseHandle(ready);

    proxy = NULL;
    second = CoGetClassObject(&clsid, CLSCTX_LOCAL_SERVER, NULL, &iid_factory, (void **)&proxy);
    printf("after exit %#lx\n", second);
    if (proxy) IClassFactory_Release(proxy);
    CoUninitialize();
    return first == S_OK && FAILED(second) && second != HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE) ? 0 : 6;
}
