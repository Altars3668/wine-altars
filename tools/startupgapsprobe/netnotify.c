/* netnotify: the IP Helper change notifications -- when and on which thread the initial notification of
 * NotifyUnicastIpAddressChange, NotifyIpInterfaceChange, NotifyRouteChange2 and NotifyNetworkConnectivityHintChange
 * comes and with what, what cancelling them returns, and the overlapped NotifyAddrChange and NotifyRouteChange with
 * CancelIPChangeNotify.  Prints results only. */
#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <stdio.h>

/* not in mingw's headers */
typedef struct
{
    int ConnectivityLevel;
    int ConnectivityCost;
    BOOLEAN ApproachingDataLimit;
    BOOLEAN OverDataLimit;
    BOOLEAN Roaming;
} NL_NETWORK_CONNECTIVITY_HINT;
typedef void (WINAPI *hint_callback)(void *, NL_NETWORK_CONNECTIVITY_HINT);
static DWORD (WINAPI *pNotifyNetworkConnectivityHintChange)(hint_callback, void *, BOOLEAN, HANDLE *);

static DWORD main_thread;
static volatile LONG returned;

struct record
{
    const char *what;
    LONG calls;
    DWORD thread;
    BOOL before_return;
    BOOL row_null;
    int type;
};

static void note(struct record *r, const void *row, int type)
{
    if (!r->calls)
    {
        r->thread = GetCurrentThreadId();
        r->before_return = !returned;
        r->row_null = !row;
        r->type = type;
    }
    InterlockedIncrement(&r->calls);
}

static void WINAPI unicast_cb(void *context, MIB_UNICASTIPADDRESS_ROW *row, MIB_NOTIFICATION_TYPE type)
{
    note(context, row, type);
}

static void WINAPI interface_cb(void *context, MIB_IPINTERFACE_ROW *row, MIB_NOTIFICATION_TYPE type)
{
    note(context, row, type);
}

static void WINAPI route_cb(void *context, MIB_IPFORWARD_ROW2 *row, MIB_NOTIFICATION_TYPE type)
{
    note(context, row, type);
}

static void WINAPI hint_cb(void *context, NL_NETWORK_CONNECTIVITY_HINT hint)
{
    struct record *r = context;
    note(r, (void *)1, hint.ConnectivityLevel);
}

static void WINAPI stable_cb(void *context, MIB_UNICASTIPADDRESS_TABLE *table)
{
    note(context, table, table ? table->NumEntries : -1);
}

static void show(struct record *r, DWORD ret, HANDLE handle)
{
    printf("%s: %lu handle %s, %ld calls", r->what, ret, handle ? "set" : "NULL", r->calls);
    if (r->calls)
        printf(", first on %s thread %s returning, row %s, type %d", r->thread == main_thread ? "the calling" : "another",
               r->before_return ? "before" : "after", r->row_null ? "NULL" : "set", r->type);
    printf("\n");
}

int main(void)
{
    struct record unicast = { "NotifyUnicastIpAddressChange" }, iface = { "NotifyIpInterfaceChange" },
                  route = { "NotifyRouteChange2" }, hint = { "NotifyNetworkConnectivityHintChange" },
                  unicast_noinit = { "NotifyUnicastIpAddressChange without the initial one" },
                  stable = { "NotifyStableUnicastIpAddressTable" };
    HANDLE h1 = NULL, h2 = NULL, h3 = NULL, h4 = NULL, h5 = NULL, h6 = NULL;
    MIB_UNICASTIPADDRESS_TABLE *table = NULL;
    OVERLAPPED ovl = { 0 }, ovl2 = { 0 };
    HANDLE handle = NULL;
    DWORD ret;

    setvbuf(stdout, NULL, _IONBF, 0);
    main_thread = GetCurrentThreadId();

    returned = 0;
    ret = NotifyUnicastIpAddressChange(AF_UNSPEC, unicast_cb, &unicast, TRUE, &h1);
    returned = 1;
    Sleep(500);
    show(&unicast, ret, h1);
    returned = 0;
    ret = NotifyIpInterfaceChange(AF_UNSPEC, interface_cb, &iface, TRUE, &h2);
    returned = 1;
    Sleep(500);
    show(&iface, ret, h2);
    returned = 0;
    ret = NotifyRouteChange2(AF_UNSPEC, route_cb, &route, TRUE, &h3);
    returned = 1;
    Sleep(500);
    show(&route, ret, h3);
    returned = 0;
    pNotifyNetworkConnectivityHintChange = (void *)GetProcAddress(GetModuleHandleW(L"iphlpapi.dll"),
                                                                  "NotifyNetworkConnectivityHintChange");
    ret = pNotifyNetworkConnectivityHintChange(hint_cb, &hint, TRUE, &h4);
    returned = 1;
    Sleep(500);
    show(&hint, ret, h4);
    returned = 0;
    ret = NotifyUnicastIpAddressChange(AF_INET, unicast_cb, &unicast_noinit, FALSE, &h5);
    returned = 1;
    Sleep(500);
    show(&unicast_noinit, ret, h5);
    returned = 0;
    ret = NotifyStableUnicastIpAddressTable(AF_UNSPEC, &table, stable_cb, &stable, &h6);
    returned = 1;
    Sleep(500);
    printf("NotifyStableUnicastIpAddressTable: %lu table %s handle %s, %ld calls\n", ret,
           table ? "set" : "NULL", h6 ? "set" : "NULL", stable.calls);
    if (table) FreeMibTable(table);

    ret = NotifyUnicastIpAddressChange(7, unicast_cb, &unicast, TRUE, &h6);
    printf("NotifyUnicastIpAddressChange family 7: %lu\n", ret);
    ret = NotifyUnicastIpAddressChange(AF_INET, unicast_cb, &unicast, TRUE, NULL);
    printf("NotifyUnicastIpAddressChange no handle: %lu\n", ret);

    printf("CancelMibChangeNotify2: %lu %lu %lu %lu %lu\n", CancelMibChangeNotify2(h1), CancelMibChangeNotify2(h2),
           CancelMibChangeNotify2(h3), CancelMibChangeNotify2(h4), CancelMibChangeNotify2(h5));
    printf("CancelMibChangeNotify2 NULL: %lu\n", CancelMibChangeNotify2(NULL));

    ovl.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ret = NotifyAddrChange(&handle, &ovl);
    printf("NotifyAddrChange: %lu handle %s, pending %d\n", ret, handle ? "set" : "NULL",
           WaitForSingleObject(ovl.hEvent, 200) == WAIT_TIMEOUT);
    printf("  CancelIPChangeNotify: %d", CancelIPChangeNotify(&ovl));
    printf(", event %s\n", WaitForSingleObject(ovl.hEvent, 1000) ? "not set" : "set");
    ovl2.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    handle = NULL;
    ret = NotifyRouteChange(&handle, &ovl2);
    printf("NotifyRouteChange: %lu handle %s, pending %d\n", ret, handle ? "set" : "NULL",
           WaitForSingleObject(ovl2.hEvent, 200) == WAIT_TIMEOUT);
    printf("  CancelIPChangeNotify: %d", CancelIPChangeNotify(&ovl2));
    printf(", event %s\n", WaitForSingleObject(ovl2.hEvent, 1000) ? "not set" : "set");
    printf("CancelIPChangeNotify NULL: %d\n", CancelIPChangeNotify(NULL));
    return 0;
}
