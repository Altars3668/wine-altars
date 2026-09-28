/*
 * Which incoming calls COM hands the message filter a single-threaded apartment registers.  The main thread
 * registers a filter that logs HandleInComingCall and makes an object with IPersist; another thread unmarshals a
 * proxy for it, asks the proxy for an interface the object has and one it has not (COM asks the object's
 * apartment through IRemUnknown), calls IPersist::GetClassID, and releases the proxy (IRemUnknown again), while
 * the main thread waits in CoWaitForMultipleHandles; then calls GetClassID once more while the main thread only
 * pumps messages with GetMessage.
 */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <stdio.h>

static const IID iid_unknown = {0x00000000,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const IID iid_persist = {0x0000010c,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const IID iid_persistfile = {0x0000010b,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const IID iid_classfactory = {0x00000001,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const IID iid_remunknown = {0x00000131,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const IID iid_remunknown2 = {0x00000143,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};

static IStream *marshaled, *marshaled2;
static HANDLE done;
static DWORD main_thread;
static LONG object_refs = 1;

static const char *iid_name(REFIID iid)
{
    static char buffer[40];
    WCHAR str[40];

    if (IsEqualGUID(iid, &iid_unknown)) return "IUnknown";
    if (IsEqualGUID(iid, &iid_persist)) return "IPersist";
    if (IsEqualGUID(iid, &iid_persistfile)) return "IPersistFile";
    if (IsEqualGUID(iid, &iid_remunknown)) return "IRemUnknown";
    if (IsEqualGUID(iid, &iid_remunknown2)) return "IRemUnknown2";
    StringFromGUID2(iid, str, ARRAY_SIZE(str));
    snprintf(buffer, sizeof(buffer), "%ls", str);
    return buffer;
}

/* the object */
static HRESULT WINAPI object_QueryInterface(IPersist *iface, REFIID riid, void **obj)
{
    printf("  object QueryInterface %s\n", iid_name(riid));
    if (IsEqualGUID(riid, &iid_unknown) || IsEqualGUID(riid, &iid_persist))
    {
        *obj = iface;
        InterlockedIncrement(&object_refs);
        return S_OK;
    }
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI object_AddRef(IPersist *iface) { return InterlockedIncrement(&object_refs); }
static ULONG WINAPI object_Release(IPersist *iface) { return InterlockedDecrement(&object_refs); }

static HRESULT WINAPI object_GetClassID(IPersist *iface, CLSID *clsid)
{
    printf("  object GetClassID\n");
    *clsid = iid_persist;
    return S_OK;
}

static const IPersistVtbl object_vtbl = { object_QueryInterface, object_AddRef, object_Release, object_GetClassID };
static IPersist object = { &object_vtbl };

/* the filter */
static HRESULT WINAPI filter_QueryInterface(IMessageFilter *iface, REFIID riid, void **obj)
{
    *obj = iface;
    return S_OK;
}

static ULONG WINAPI filter_AddRef(IMessageFilter *iface) { return 2; }
static ULONG WINAPI filter_Release(IMessageFilter *iface) { return 1; }

static DWORD WINAPI filter_HandleInComingCall(IMessageFilter *iface, DWORD type, HTASK caller, DWORD tick,
                                              INTERFACEINFO *info)
{
    IUnknown *unk = NULL;

    printf("  HandleInComingCall type %lu, %s method %u, object %s\n", type, iid_name(&info->iid), info->wMethod,
           info->pUnk == (IUnknown *)&object ? "the object" : info->pUnk ? "another" : "NULL");
    if (info->pUnk && info->pUnk != (IUnknown *)&object)
    {
        /* what the object handed is */
        HRESULT hr = IUnknown_QueryInterface(info->pUnk, &iid_unknown, (void **)&unk);
        printf("    its IUnknown: hr %#lx%s\n", hr, SUCCEEDED(hr) && unk == (IUnknown *)&object ? ", the object" : "");
        if (SUCCEEDED(hr)) IUnknown_Release(unk);
    }
    return SERVERCALL_ISHANDLED;
}

static DWORD WINAPI filter_RetryRejectedCall(IMessageFilter *iface, HTASK callee, DWORD tick, DWORD type)
{
    return -1;
}

static DWORD WINAPI filter_MessagePending(IMessageFilter *iface, HTASK callee, DWORD tick, DWORD type)
{
    return PENDINGMSG_WAITDEFPROCESS;
}

static const IMessageFilterVtbl filter_vtbl =
{
    filter_QueryInterface, filter_AddRef, filter_Release, filter_HandleInComingCall, filter_RetryRejectedCall,
    filter_MessagePending,
};
static IMessageFilter filter = { &filter_vtbl };

static DWORD WINAPI thread_proc(void *arg)
{
    IPersist *proxy;
    IUnknown *unk;
    CLSID clsid;
    HRESULT hr;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    hr = CoGetInterfaceAndReleaseStream(marshaled, &iid_persist, (void **)&proxy);
    printf("client: unmarshaled, hr %#lx\n", hr);
    hr = IPersist_QueryInterface(proxy, &iid_classfactory, (void **)&unk);
    printf("client: QueryInterface of an interface the object has not, hr %#lx\n", hr);
    hr = IPersist_QueryInterface(proxy, &iid_unknown, (void **)&unk);
    printf("client: QueryInterface(IUnknown), hr %#lx\n", hr);
    if (SUCCEEDED(hr)) IUnknown_Release(unk);
    hr = IPersist_GetClassID(proxy, &clsid);
    printf("client: GetClassID, hr %#lx\n", hr);
    IPersist_Release(proxy);
    printf("client: released\n");
    SetEvent(done);

    hr = CoGetInterfaceAndReleaseStream(marshaled2, &iid_persist, (void **)&proxy);
    printf("client: unmarshaled again, hr %#lx\n", hr);
    hr = IPersist_GetClassID(proxy, &clsid);
    printf("client: GetClassID while the object's thread is in GetMessage, hr %#lx\n", hr);
    IPersist_Release(proxy);
    PostThreadMessageW(main_thread, WM_QUIT, 0, 0);
    CoUninitialize();
    return 0;
}

int main(void)
{
    IMessageFilter *old;
    HANDLE thread;
    DWORD index;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    CoInitialize(NULL);
    hr = CoRegisterMessageFilter(&filter, &old);
    printf("CoRegisterMessageFilter: hr %#lx\n", hr);
    done = CreateEventW(NULL, TRUE, FALSE, NULL);
    CoMarshalInterThreadInterfaceInStream(&iid_persist, (IUnknown *)&object, &marshaled);
    CoMarshalInterThreadInterfaceInStream(&iid_persist, (IUnknown *)&object, &marshaled2);
    printf("marshaled\n");
    main_thread = GetCurrentThreadId();
    thread = CreateThread(NULL, 0, thread_proc, NULL, 0, NULL);
    CoWaitForMultipleHandles(0, 20000, 1, &done, &index);
    {
        MSG msg;

        while (GetMessageW(&msg, NULL, 0, 0)) DispatchMessageW(&msg);
    }
    WaitForSingleObject(thread, 5000);
    CoRegisterMessageFilter(NULL, &old);
    CoUninitialize();
    printf("done\n");
    return 0;
}
