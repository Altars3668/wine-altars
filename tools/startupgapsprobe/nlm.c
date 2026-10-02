/* nlm: what the network list manager says -- connectivity, the networks and connections with their properties
 * (a network's name only by its length), how a connection's ids relate to its adapter's, costs and data plans, and
 * the connection points: which there are and what advising and unadvising return.  Prints results only. */
#define COBJMACROS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <initguid.h>
#include <netlistmgr.h>
#include <ocidl.h>
#include <stdio.h>

DEFINE_GUID(IID_INetworkConnectionCostEvents_, 0xdcb0000b, 0x570f, 0x4a9b, 0x8d, 0x69, 0x19, 0x9f, 0xdb, 0xa5, 0x72, 0x3b);
DEFINE_GUID(IID_office_private, 0xe19c7100, 0x9709, 0x4db7, 0x93, 0x73, 0xe7, 0xb5, 0x18, 0xb4, 0x70, 0x86);
DEFINE_GUID(IID_IAgileObject_, 0x94ea2b94, 0xe9cc, 0x49e0, 0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90);

static const struct { const GUID *iid; const char *name; } events[] =
{
    { &IID_INetworkListManagerEvents, "INetworkListManagerEvents" },
    { &IID_INetworkEvents, "INetworkEvents" },
    { &IID_INetworkConnectionEvents, "INetworkConnectionEvents" },
    { &IID_INetworkCostManagerEvents, "INetworkCostManagerEvents" },
    { &IID_INetworkConnectionCostEvents_, "INetworkConnectionCostEvents" },
};

/* a sink that is every kind of event sink, and one that is none */
static BOOL sink_takes_all = TRUE;
static HRESULT WINAPI sink_QueryInterface(IUnknown *iface, REFIID iid, void **out)
{
    unsigned int i;

    if (IsEqualGUID(iid, &IID_IUnknown))
    {
        *out = iface;
        return S_OK;
    }
    for (i = 0; sink_takes_all && i < ARRAYSIZE(events); i++)
        if (IsEqualGUID(iid, events[i].iid))
        {
            *out = iface;
            return S_OK;
        }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI sink_AddRef(IUnknown *iface) { return 2; }
static ULONG WINAPI sink_Release(IUnknown *iface) { return 1; }
static HRESULT WINAPI sink_method(IUnknown *iface) { return S_OK; }
static void *sink_vtbl[] = { sink_QueryInterface, sink_AddRef, sink_Release, sink_method, sink_method, sink_method,
                             sink_method, sink_method, sink_method };
static IUnknown sink = { (IUnknownVtbl *)sink_vtbl };

static const char *guid_relation(const GUID *guid)
{
    static char buf[256];
    IP_ADAPTER_ADDRESSES *addrs, *a;
    ULONG size = 0;
    GUID iface;

    buf[0] = 0;
    GetAdaptersAddresses(AF_UNSPEC, 0, NULL, NULL, &size);
    if (!(addrs = malloc(size))) return "?";
    if (!GetAdaptersAddresses(AF_UNSPEC, 0, NULL, addrs, &size))
    {
        for (a = addrs; a; a = a->Next)
        {
            if (!ConvertInterfaceLuidToGuid(&a->Luid, &iface) && IsEqualGUID(&iface, guid))
                snprintf(buf, sizeof(buf), "the interface GUID of %ls (type %lu)", a->Description, a->IfType);
            else if (IsEqualGUID(&a->NetworkGuid, guid))
                snprintf(buf, sizeof(buf), "the NetworkGuid of %ls", a->Description);
        }
    }
    free(addrs);
    return buf[0] ? buf : "no adapter's";
}

static void show_cost(const char *what, HRESULT hr, DWORD cost)
{
    printf("  %s: %#lx cost %#lx\n", what, hr, cost);
}

static void show_plan(const char *what, HRESULT hr, const NLM_DATAPLAN_STATUS *plan)
{
    printf("  %s: %#lx", what, hr);
    if (SUCCEEDED(hr))
        printf(" interface %s, usage %#lx, sync %08lx%08lx, limit %#lx, in %#lx, out %#lx, billing %08lx%08lx,"
               " transfer %#lx, reserved %#lx", guid_relation(&plan->InterfaceGuid), plan->UsageData.UsageInMegabytes,
               plan->UsageData.LastSyncTime.dwHighDateTime, plan->UsageData.LastSyncTime.dwLowDateTime,
               plan->DataLimitInMegabytes, plan->InboundBandwidthInKbps, plan->OutboundBandwidthInKbps,
               plan->NextBillingCycle.dwHighDateTime, plan->NextBillingCycle.dwLowDateTime,
               plan->MaxTransferSizeInMegabytes, plan->Reserved);
    printf("\n");
}

int main(void)
{
    INetworkListManager *manager;
    INetworkCostManager *costs;
    IConnectionPointContainer *container;
    IEnumConnectionPoints *points;
    IConnectionPoint *point;
    IEnumNetworks *networks;
    IEnumNetworkConnections *connections;
    INetworkConnection *connection;
    INetworkConnectionCost *connection_cost;
    INetwork *network, *conn_network;
    NLM_CONNECTIVITY connectivity;
    NLM_DATAPLAN_STATUS plan;
    NLM_DOMAIN_TYPE domain;
    NLM_NETWORK_CATEGORY category;
    NLM_SOCKADDR dest;
    VARIANT_BOOL bool1, bool2;
    GUID id1, id2, id3;
    DWORD cost, cookie, cookie2, low1, high1, low2, high2;
    BSTR name, description;
    unsigned int i;
    HRESULT hr;
    IUnknown *unk;

    setvbuf(stdout, NULL, _IONBF, 0);
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    hr = CoCreateInstance(&CLSID_NetworkListManager, NULL, CLSCTX_INPROC_SERVER, &IID_INetworkListManager,
                          (void **)&manager);
    printf("CoCreateInstance: %#lx\n", hr);
    if (FAILED(hr)) return 1;

    hr = INetworkListManager_GetConnectivity(manager, &connectivity);
    printf("GetConnectivity: %#lx %#x\n", hr, connectivity);
    hr = INetworkListManager_IsConnected(manager, &bool1);
    hr = INetworkListManager_IsConnectedToInternet(manager, &bool2);
    printf("IsConnected %d, IsConnectedToInternet %d\n", bool1, bool2);
    {
        static const struct { const GUID *iid; const char *name; } ifaces[] =
        {
            { &IID_INetworkCostManager, "INetworkCostManager" }, { &IID_IConnectionPointContainer, "IConnectionPointContainer" },
            { &IID_office_private, "Office's private" }, { &IID_IMarshal, "IMarshal" }, { &IID_IAgileObject_, "IAgileObject" },
            { &IID_IDispatch, "IDispatch" },
        };
        for (i = 0; i < ARRAYSIZE(ifaces); i++)
        {
            hr = INetworkListManager_QueryInterface(manager, ifaces[i].iid, (void **)&unk);
            printf("QueryInterface %s: %#lx\n", ifaces[i].name, hr);
            if (SUCCEEDED(hr)) IUnknown_Release(unk);
        }
    }

    hr = INetworkListManager_GetNetworks(manager, NLM_ENUM_NETWORK_ALL, &networks);
    printf("GetNetworks: %#lx\n", hr);
    while (SUCCEEDED(hr) && IEnumNetworks_Next(networks, 1, &network, NULL) == S_OK)
    {
        name = description = NULL;
        INetwork_GetName(network, &name);
        INetwork_GetDescription(network, &description);
        INetwork_GetNetworkId(network, &id1);
        INetwork_GetDomainType(network, &domain);
        INetwork_GetCategory(network, &category);
        INetwork_GetConnectivity(network, &connectivity);
        INetwork_get_IsConnected(network, &bool1);
        INetwork_get_IsConnectedToInternet(network, &bool2);
        low1 = high1 = low2 = high2 = 0;
        INetwork_GetTimeCreatedAndConnected(network, &low1, &high1, &low2, &high2);
        printf("network: name of %u characters, description %s, id is %s, domain %d, category %d, connectivity %#x,"
               " connected %d, internet %d, created %s, connected %s\n",
               name ? SysStringLen(name) : 0, name && description && !wcscmp(name, description) ? "the name" : "other",
               guid_relation(&id1), domain, category, connectivity, bool1, bool2,
               low1 || high1 ? "set" : "zero", low2 || high2 ? "set" : "zero");
        SysFreeString(name);
        SysFreeString(description);
        if (SUCCEEDED(INetwork_GetNetworkConnections(network, &connections)))
        {
            while (IEnumNetworkConnections_Next(connections, 1, &connection, NULL) == S_OK)
            {
                INetworkConnection_GetConnectionId(connection, &id2);
                INetworkConnection_GetAdapterId(connection, &id3);
                INetworkConnection_GetConnectivity(connection, &connectivity);
                INetworkConnection_GetDomainType(connection, &domain);
                INetworkConnection_get_IsConnected(connection, &bool1);
                INetworkConnection_get_IsConnectedToInternet(connection, &bool2);
                conn_network = NULL;
                INetworkConnection_GetNetwork(connection, &conn_network);
                printf("  connection: connection id is %s,", guid_relation(&id2));
                printf(" adapter id is %s, %s the connection id, %s the network id, connectivity %#x, domain %d,"
                       " connected %d, internet %d, network %s\n", guid_relation(&id3),
                       IsEqualGUID(&id2, &id3) ? "same as" : "not", IsEqualGUID(&id1, &id3) ? "same as" : "not",
                       connectivity, domain, bool1, bool2, conn_network ? "got" : "none");
                if (conn_network)
                {
                    INetwork_GetNetworkId(conn_network, &id2);
                    printf("    its network: %s the enumerated one\n", IsEqualGUID(&id1, &id2) ? "same id as" : "not");
                    INetwork_Release(conn_network);
                }
                if (SUCCEEDED(INetworkConnection_QueryInterface(connection, &IID_INetworkConnectionCost,
                                                                (void **)&connection_cost)))
                {
                    cost = 0xdeadbeef;
                    hr = INetworkConnectionCost_GetCost(connection_cost, &cost);
                    show_cost("connection GetCost", hr, cost);
                    memset(&plan, 0xcc, sizeof(plan));
                    hr = INetworkConnectionCost_GetDataPlanStatus(connection_cost, &plan);
                    show_plan("connection GetDataPlanStatus", hr, &plan);
                    INetworkConnectionCost_Release(connection_cost);
                }
                INetworkConnection_Release(connection);
            }
            IEnumNetworkConnections_Release(connections);
        }
        INetwork_Release(network);
    }
    if (SUCCEEDED(hr)) IEnumNetworks_Release(networks);

    hr = INetworkListManager_GetNetworkConnections(manager, &connections);
    printf("GetNetworkConnections: %#lx\n", hr);
    if (SUCCEEDED(hr)) IEnumNetworkConnections_Release(connections);

    if (SUCCEEDED(INetworkListManager_QueryInterface(manager, &IID_INetworkCostManager, (void **)&costs)))
    {
        SOCKADDR_IN *in = (SOCKADDR_IN *)&dest;

        cost = 0xdeadbeef;
        hr = INetworkCostManager_GetCost(costs, &cost, NULL);
        show_cost("GetCost NULL", hr, cost);
        memset(&dest, 0, sizeof(dest));
        in->sin_family = AF_INET;
        in->sin_addr.s_addr = inet_addr("8.8.8.8");
        cost = 0xdeadbeef;
        hr = INetworkCostManager_GetCost(costs, &cost, &dest);
        show_cost("GetCost 8.8.8.8", hr, cost);
        in->sin_addr.s_addr = inet_addr("127.0.0.1");
        cost = 0xdeadbeef;
        hr = INetworkCostManager_GetCost(costs, &cost, &dest);
        show_cost("GetCost 127.0.0.1", hr, cost);
        memset(&plan, 0xcc, sizeof(plan));
        hr = INetworkCostManager_GetDataPlanStatus(costs, &plan, NULL);
        show_plan("GetDataPlanStatus NULL", hr, &plan);
        in->sin_addr.s_addr = inet_addr("8.8.8.8");
        memset(&plan, 0xcc, sizeof(plan));
        hr = INetworkCostManager_GetDataPlanStatus(costs, &plan, &dest);
        show_plan("GetDataPlanStatus 8.8.8.8", hr, &plan);
        hr = INetworkCostManager_SetDestinationAddresses(costs, 0, NULL, VARIANT_FALSE);
        printf("  SetDestinationAddresses none: %#lx\n", hr);
        INetworkCostManager_Release(costs);
    }

    if (SUCCEEDED(INetworkListManager_QueryInterface(manager, &IID_IConnectionPointContainer, (void **)&container)))
    {
        if (SUCCEEDED(IConnectionPointContainer_EnumConnectionPoints(container, &points)))
        {
            printf("connection points:");
            while (IEnumConnectionPoints_Next(points, 1, &point, NULL) == S_OK)
            {
                GUID iid;
                IConnectionPoint_GetConnectionInterface(point, &iid);
                for (i = 0; i < ARRAYSIZE(events); i++) if (IsEqualGUID(&iid, events[i].iid)) break;
                printf(" %s", i < ARRAYSIZE(events) ? events[i].name : "other");
                IConnectionPoint_Release(point);
            }
            printf("\n");
            IEnumConnectionPoints_Release(points);
        }
        for (i = 0; i < ARRAYSIZE(events); i++)
        {
            hr = IConnectionPointContainer_FindConnectionPoint(container, events[i].iid, &point);
            printf("FindConnectionPoint %s: %#lx", events[i].name, hr);
            if (SUCCEEDED(hr))
            {
                sink_takes_all = TRUE;
                cookie = cookie2 = 0;
                hr = IConnectionPoint_Advise(point, &sink, &cookie);
                printf(", Advise %#lx cookie %lu", hr, cookie);
                hr = IConnectionPoint_Advise(point, &sink, &cookie2);
                printf(", again %#lx cookie %lu", hr, cookie2);
                hr = IConnectionPoint_Unadvise(point, cookie);
                printf(", Unadvise %#lx", hr);
                hr = IConnectionPoint_Unadvise(point, cookie);
                printf(", again %#lx", hr);
                hr = IConnectionPoint_Unadvise(point, cookie2);
                printf(", second %#lx", hr);
                sink_takes_all = FALSE;
                hr = IConnectionPoint_Advise(point, &sink, &cookie);
                printf(", Advise a sink of none %#lx", hr);
                if (SUCCEEDED(hr)) IConnectionPoint_Unadvise(point, cookie);
                IConnectionPoint_Release(point);
            }
            printf("\n");
        }
        hr = IConnectionPointContainer_FindConnectionPoint(container, &IID_IUnknown, &point);
        printf("FindConnectionPoint IUnknown: %#lx\n", hr);
        IConnectionPointContainer_Release(container);
    }
    INetworkListManager_Release(manager);
    CoUninitialize();
    return 0;
}
