/* netchange: prints the IP Helper and network list manager notifications as they come, for a number of seconds,
 * while something outside changes the network (on Linux, a dummy interface brought up and down with nmcli).
 *
 *     netchange.exe [seconds]       20 by default */
#define COBJMACROS
#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <initguid.h>
#include <netlistmgr.h>
#include <ocidl.h>
#include <stdio.h>

typedef struct
{
    int ConnectivityLevel;
    int ConnectivityCost;
    BOOLEAN ApproachingDataLimit;
    BOOLEAN OverDataLimit;
    BOOLEAN Roaming;
} hint_t;

static DWORD start;
static CRITICAL_SECTION cs;

static void stamp(void)
{
    printf("%6lu ", GetTickCount() - start);
}

static const char *type_name(MIB_NOTIFICATION_TYPE type)
{
    switch (type)
    {
    case MibParameterNotification: return "parameter";
    case MibAddInstance: return "add";
    case MibDeleteInstance: return "delete";
    case MibInitialNotification: return "initial";
    default: return "?";
    }
}

static void address(const SOCKADDR_INET *addr, char *buf, DWORD size)
{
    DWORD len = size;
    if (WSAAddressToStringA((SOCKADDR *)addr, addr->si_family == AF_INET ? sizeof(SOCKADDR_IN) : sizeof(SOCKADDR_IN6),
                            NULL, buf, &len)) strcpy(buf, "?");
}

static void WINAPI unicast_cb(void *context, MIB_UNICASTIPADDRESS_ROW *row, MIB_NOTIFICATION_TYPE type)
{
    char buf[64] = "-";
    EnterCriticalSection(&cs);
    if (row) address(&row->Address, buf, sizeof(buf));
    stamp();
    printf("unicast %s: interface %lu %s/%u dad %d\n", type_name(type), row ? row->InterfaceIndex : 0, buf,
           row ? row->OnLinkPrefixLength : 0, row ? row->DadState : -1);
    LeaveCriticalSection(&cs);
}

static void WINAPI interface_cb(void *context, MIB_IPINTERFACE_ROW *row, MIB_NOTIFICATION_TYPE type)
{
    EnterCriticalSection(&cs);
    stamp();
    printf("interface %s: interface %lu family %u connected %d\n", type_name(type), row ? row->InterfaceIndex : 0,
           row ? row->Family : 0, row ? row->Connected : -1);
    LeaveCriticalSection(&cs);
}

static void WINAPI route_cb(void *context, MIB_IPFORWARD_ROW2 *row, MIB_NOTIFICATION_TYPE type)
{
    char dest[64] = "-", hop[64] = "-";
    EnterCriticalSection(&cs);
    if (row)
    {
        address(&row->DestinationPrefix.Prefix, dest, sizeof(dest));
        address(&row->NextHop, hop, sizeof(hop));
    }
    stamp();
    printf("route %s: interface %lu %s/%u via %s\n", type_name(type), row ? row->InterfaceIndex : 0, dest,
           row ? row->DestinationPrefix.PrefixLength : 0, hop);
    LeaveCriticalSection(&cs);
}

static void WINAPI hint_cb(void *context, hint_t hint)
{
    EnterCriticalSection(&cs);
    stamp();
    printf("hint: level %d cost %d\n", hint.ConnectivityLevel, hint.ConnectivityCost);
    LeaveCriticalSection(&cs);
}

/* an event sink for all the network list manager's interfaces */
static HRESULT WINAPI sink_QueryInterface(IUnknown *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_INetworkListManagerEvents) ||
        IsEqualGUID(iid, &IID_INetworkEvents) || IsEqualGUID(iid, &IID_INetworkConnectionEvents))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI sink_AddRef(IUnknown *iface) { return 2; }
static ULONG WINAPI sink_Release(IUnknown *iface) { return 1; }

static HRESULT WINAPI mgr_ConnectivityChanged(INetworkListManagerEvents *iface, NLM_CONNECTIVITY connectivity)
{
    EnterCriticalSection(&cs);
    stamp();
    printf("nlm ConnectivityChanged %#x\n", connectivity);
    LeaveCriticalSection(&cs);
    return S_OK;
}
static INetworkListManagerEventsVtbl mgr_vtbl = { (void *)sink_QueryInterface, (void *)sink_AddRef,
                                                  (void *)sink_Release, mgr_ConnectivityChanged };
static INetworkListManagerEvents mgr_sink = { &mgr_vtbl };

static HRESULT WINAPI net_NetworkAdded(INetworkEvents *iface, GUID id)
{
    EnterCriticalSection(&cs); stamp(); printf("nlm NetworkAdded %08lx\n", id.Data1); LeaveCriticalSection(&cs);
    return S_OK;
}
static HRESULT WINAPI net_NetworkDeleted(INetworkEvents *iface, GUID id)
{
    EnterCriticalSection(&cs); stamp(); printf("nlm NetworkDeleted %08lx\n", id.Data1); LeaveCriticalSection(&cs);
    return S_OK;
}
static HRESULT WINAPI net_NetworkConnectivityChanged(INetworkEvents *iface, GUID id, NLM_CONNECTIVITY connectivity)
{
    EnterCriticalSection(&cs); stamp();
    printf("nlm NetworkConnectivityChanged %08lx %#x\n", id.Data1, connectivity);
    LeaveCriticalSection(&cs);
    return S_OK;
}
static HRESULT WINAPI net_NetworkPropertyChanged(INetworkEvents *iface, GUID id, NLM_NETWORK_PROPERTY_CHANGE flags)
{
    EnterCriticalSection(&cs); stamp(); printf("nlm NetworkPropertyChanged %08lx %#x\n", id.Data1, flags);
    LeaveCriticalSection(&cs);
    return S_OK;
}
static INetworkEventsVtbl net_vtbl = { (void *)sink_QueryInterface, (void *)sink_AddRef, (void *)sink_Release,
                                       net_NetworkAdded, net_NetworkDeleted, net_NetworkConnectivityChanged,
                                       net_NetworkPropertyChanged };
static INetworkEvents net_sink = { &net_vtbl };

static HRESULT WINAPI conn_ConnectivityChanged(INetworkConnectionEvents *iface, GUID id, NLM_CONNECTIVITY connectivity)
{
    EnterCriticalSection(&cs); stamp();
    printf("nlm NetworkConnectionConnectivityChanged %08lx %#x\n", id.Data1, connectivity);
    LeaveCriticalSection(&cs);
    return S_OK;
}
static HRESULT WINAPI conn_PropertyChanged(INetworkConnectionEvents *iface, GUID id, NLM_CONNECTION_PROPERTY_CHANGE flags)
{
    EnterCriticalSection(&cs); stamp();
    printf("nlm NetworkConnectionPropertyChanged %08lx %#x\n", id.Data1, flags);
    LeaveCriticalSection(&cs);
    return S_OK;
}
static INetworkConnectionEventsVtbl conn_vtbl = { (void *)sink_QueryInterface, (void *)sink_AddRef,
                                                  (void *)sink_Release, conn_ConnectivityChanged,
                                                  conn_PropertyChanged };
static INetworkConnectionEvents conn_sink = { &conn_vtbl };

static void advise(IConnectionPointContainer *container, REFIID iid, IUnknown *sink, const char *what)
{
    IConnectionPoint *point;
    DWORD cookie;
    HRESULT hr;

    hr = IConnectionPointContainer_FindConnectionPoint(container, iid, &point);
    if (SUCCEEDED(hr))
    {
        hr = IConnectionPoint_Advise(point, sink, &cookie);
        IConnectionPoint_Release(point);
    }
    printf("advise %s: %#lx\n", what, hr);
}

int main(int argc, char **argv)
{
    DWORD (WINAPI *pNotifyNetworkConnectivityHintChange)(void *, void *, BOOLEAN, HANDLE *);
    HANDLE h1, h2, h3, h4;
    INetworkListManager *manager;
    IConnectionPointContainer *container;
    unsigned int seconds = argc > 1 ? atoi(argv[1]) : 20;
    WSADATA wsa;
    MSG msg;

    setvbuf(stdout, NULL, _IONBF, 0);
    WSAStartup(MAKEWORD(2, 2), &wsa);
    InitializeCriticalSection(&cs);
    start = GetTickCount();
    pNotifyNetworkConnectivityHintChange = (void *)GetProcAddress(GetModuleHandleW(L"iphlpapi.dll"),
                                                                  "NotifyNetworkConnectivityHintChange");
    NotifyUnicastIpAddressChange(AF_UNSPEC, unicast_cb, NULL, FALSE, &h1);
    NotifyIpInterfaceChange(AF_UNSPEC, interface_cb, NULL, FALSE, &h2);
    NotifyRouteChange2(AF_UNSPEC, route_cb, NULL, FALSE, &h3);
    pNotifyNetworkConnectivityHintChange(hint_cb, NULL, FALSE, &h4);

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(CoCreateInstance(&CLSID_NetworkListManager, NULL, CLSCTX_ALL, &IID_INetworkListManager,
                                   (void **)&manager)))
    {
        NLM_CONNECTIVITY connectivity = 0;
        INetworkListManager_GetConnectivity(manager, &connectivity);
        printf("connectivity at start %#x\n", connectivity);
        INetworkListManager_QueryInterface(manager, &IID_IConnectionPointContainer, (void **)&container);
        advise(container, &IID_INetworkListManagerEvents, (IUnknown *)&mgr_sink, "list manager");
        advise(container, &IID_INetworkEvents, (IUnknown *)&net_sink, "networks");
        advise(container, &IID_INetworkConnectionEvents, (IUnknown *)&conn_sink, "connections");
    }
    printf("ready\n");
    /* the list manager's events come through this apartment's messages */
    while (GetTickCount() - start < seconds * 1000)
    {
        MsgWaitForMultipleObjects(0, NULL, FALSE, 100, QS_ALLINPUT);
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
    }
    printf("cancel: %lu %lu %lu %lu\n", CancelMibChangeNotify2(h1), CancelMibChangeNotify2(h2),
           CancelMibChangeNotify2(h3), CancelMibChangeNotify2(h4));
    return 0;
}
