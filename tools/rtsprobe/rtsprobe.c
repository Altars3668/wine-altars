/*
 * rtsprobe - what Windows' RealTimeStylus and StrokeBuilder answer, for Wine's rtscom to follow.
 *
 * Office's ink tool drives a RealTimeStylus with its own plugin and a StrokeBuilder.  Wine's rtscom
 * chose values nobody measured: the default desired packet description, the tablet context ids and
 * which tablets there are with the mouse and without, the metrics and scales of GetPacketDescription-
 * Data, what the mouse's tablet object says, the errors for enabling without a window and for changing
 * the window while enabled, and the stroke builder's default ink.  This prints them.  A tablet's
 * plug and play id is printed as its length and its first part only.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <ole2.h>
#include <rtscom.h>
#include <stdio.h>

static const char *guid_name(const GUID *guid)
{
    static const struct { GUID guid; const char *name; } names[] =
    {
        {{0x598a6a8f, 0x52c0, 0x4ba0, {0x93, 0xaf, 0xaf, 0x35, 0x74, 0x11, 0xa5, 0x61}}, "X"},
        {{0xb53f9f75, 0x04e0, 0x4498, {0xa7, 0xee, 0xc3, 0x0d, 0xbb, 0x5a, 0x90, 0x11}}, "Y"},
        {{0x7307502d, 0xf9f4, 0x4e18, {0xb3, 0xf2, 0x2c, 0xe1, 0xb1, 0xa3, 0x61, 0x0c}}, "NormalPressure"},
        {{0x6e0e07bf, 0xafe7, 0x4cf7, {0x87, 0xd1, 0xaf, 0x64, 0x46, 0x20, 0x84, 0x18}}, "PacketStatus"},
        {{0x436510c5, 0xfed3, 0x45d1, {0x8b, 0x76, 0x71, 0xd3, 0xea, 0x7a, 0x82, 0x9d}}, "TimerTick"},
        {{0x735adb30, 0x0ebb, 0x4788, {0xa0, 0xe4, 0x0f, 0x31, 0x64, 0x90, 0x05, 0x5d}}, "Z"},
        {{0x02585b91, 0x049b, 0x4750, {0x96, 0x15, 0xdf, 0x89, 0x48, 0xab, 0x3c, 0x9c}}, "DeviceContactId"},
    };
    static char buf[4][64];
    static int n;
    char *b = buf[n++ % 4];
    unsigned int i;

    for (i = 0; i < ARRAYSIZE(names); i++)
        if (IsEqualGUID(guid, &names[i].guid)) return names[i].name;
    sprintf(b, "{%08lx-%04x-%04x-...}", guid->Data1, guid->Data2, guid->Data3);
    return b;
}

static void print_tablet(IInkTablet *tablet)
{
    static const WCHAR *properties[] =
    {
        L"{598A6A8F-52C0-4BA0-93AF-AF357411A561}", L"{B53F9F75-04E0-4498-A7EE-C30DBB5A9011}",
        L"{7307502D-F9F4-4E18-B3F2-2CE1B1A3610C}", L"{6E0E07BF-AFE7-4CF7-87D1-AF6446208418}",
    };
    static const char *names[] = {"X", "Y", "NormalPressure", "PacketStatus"};
    TabletHardwareCapabilities caps = 0;
    IInkRectangle *rect = NULL;
    BSTR name = NULL, pnp = NULL;
    unsigned int i;
    HRESULT hr;

    hr = IInkTablet_get_Name(tablet, &name);
    printf("    Name %#lx \"%ls\"\n", hr, name ? name : L"");
    hr = IInkTablet_get_PlugAndPlayId(tablet, &pnp);
    if (pnp)
    {
        WCHAR *sep = wcschr(pnp, '\\');
        printf("    PlugAndPlayId %#lx, %u chars, starting \"%.*ls\"\n", hr, (unsigned)wcslen(pnp),
               sep ? (int)(sep - pnp) : (int)wcslen(pnp), pnp);
    }
    else printf("    PlugAndPlayId %#lx, NULL\n", hr);
    hr = IInkTablet_get_HardwareCapabilities(tablet, &caps);
    printf("    HardwareCapabilities %#lx %#x\n", hr, caps);
    hr = IInkTablet_get_MaximumInputRectangle(tablet, &rect);
    if (SUCCEEDED(hr) && rect)
    {
        RECT r;
        IInkRectangle_get_Data(rect, &r);
        printf("    MaximumInputRectangle (%ld,%ld)-(%ld,%ld)\n", r.left, r.top, r.right, r.bottom);
        IInkRectangle_Release(rect);
    }
    else printf("    MaximumInputRectangle %#lx\n", hr);
    for (i = 0; i < ARRAYSIZE(properties); i++)
    {
        BSTR property = SysAllocString(properties[i]);
        VARIANT_BOOL supported = 12345;
        LONG min = 0, max = 0;
        TabletPropertyMetricUnit units = 0;
        float resolution = 0;

        hr = IInkTablet_IsPacketPropertySupported(tablet, property, &supported);
        printf("    %s supported %#lx %d", names[i], hr, supported);
        hr = IInkTablet_GetPropertyMetrics(tablet, property, &min, &max, &units, &resolution);
        printf(", metrics %#lx %ld..%ld units %d resolution %f\n", hr, min, max, units, resolution);
        SysFreeString(property);
    }
    SysFreeString(name);
    SysFreeString(pnp);
}

static void print_contexts(IRealTimeStylus *rts)
{
    TABLET_CONTEXT_ID *tcids = NULL;
    ULONG count = 12345, i, j;
    HRESULT hr;

    hr = IRealTimeStylus_GetAllTabletContextIds(rts, &count, &tcids);
    printf("  GetAllTabletContextIds %#lx, %lu contexts\n", hr, count);
    for (i = 0; SUCCEEDED(hr) && i < count; i++)
    {
        PACKET_PROPERTY *properties = NULL;
        FLOAT sx = 0, sy = 0;
        ULONG n = 0;
        IInkTablet *tablet = NULL;

        printf("  context %lu: id %lu\n", i, tcids[i]);
        hr = IRealTimeStylus_GetPacketDescriptionData(rts, tcids[i], &sx, &sy, &n, &properties);
        printf("    GetPacketDescriptionData %#lx, scale %f x %f, %lu properties\n", hr, sx, sy, n);
        for (j = 0; SUCCEEDED(hr) && j < n; j++)
            printf("      %s: %ld..%ld units %d resolution %f\n", guid_name(&properties[j].guid),
                   properties[j].PropertyMetrics.nLogicalMin, properties[j].PropertyMetrics.nLogicalMax,
                   properties[j].PropertyMetrics.Units, properties[j].PropertyMetrics.fResolution);
        CoTaskMemFree(properties);
        hr = IRealTimeStylus_GetTabletFromTabletContextId(rts, tcids[i], &tablet);
        printf("    GetTabletFromTabletContextId %#lx\n", hr);
        if (SUCCEEDED(hr) && tablet)
        {
            TABLET_CONTEXT_ID back = 0;
            print_tablet(tablet);
            hr = IRealTimeStylus_GetTabletContextIdFromTablet(rts, tablet, &back);
            printf("    GetTabletContextIdFromTablet %#lx, %lu\n", hr, back);
            IInkTablet_Release(tablet);
        }
        hr = S_OK;
    }
    CoTaskMemFree(tcids);
}

/* a probe that hangs would hold up every command after it in a batch */
static DWORD WINAPI watchdog(void *arg)
{
    Sleep(60000);
    printf("watchdog: still running after 60 s, exiting\n");
    ExitProcess(1);
}

/* a synchronous plugin that prints what the stylus hands it; free-threaded, or the stylus refuses it */
struct capture
{
    IStylusSyncPlugin IStylusSyncPlugin_iface;
    LONG ref;
    IUnknown *marshaler;
    LONG events;
};

static struct capture *impl_from_capture(IStylusSyncPlugin *iface)
{
    return CONTAINING_RECORD(iface, struct capture, IStylusSyncPlugin_iface);
}

static HRESULT WINAPI capture_QueryInterface(IStylusSyncPlugin *iface, REFIID iid, void **out)
{
    struct capture *capture = impl_from_capture(iface);

    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IStylusPlugin) || IsEqualGUID(iid, &IID_IStylusSyncPlugin))
    {
        *out = iface;
        IStylusSyncPlugin_AddRef(iface);
        return S_OK;
    }
    if (IsEqualGUID(iid, &IID_IMarshal) && capture->marshaler) return IUnknown_QueryInterface(capture->marshaler, iid, out);
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI capture_AddRef(IStylusSyncPlugin *iface) { return InterlockedIncrement(&impl_from_capture(iface)->ref); }
static ULONG WINAPI capture_Release(IStylusSyncPlugin *iface) { return InterlockedDecrement(&impl_from_capture(iface)->ref); }

static void print_packets(const char *what, const StylusInfo *info, ULONG count, ULONG length, const LONG *packets)
{
    ULONG size = count ? length / count : 0, i, j;

    printf("  %s: context %lu, stylus %lu, %lu packets of %lu values on thread %s:", what, info->tcid, info->cid,
           count, size, GetCurrentThreadId() == GetWindowThreadProcessId(GetForegroundWindow(), NULL) ? "fg" : "other");
    for (i = 0; i < count && i < 4; i++)
    {
        printf(" (");
        for (j = 0; j < size; j++) printf("%s%ld", j ? "," : "", packets[i * size + j]);
        printf(")");
    }
    printf("\n");
}

static HRESULT WINAPI capture_RealTimeStylusEnabled(IStylusSyncPlugin *iface, IRealTimeStylus *rts, ULONG count,
        const TABLET_CONTEXT_ID *tcids)
{
    printf("  enabled with %lu contexts\n", count);
    return S_OK;
}
static HRESULT WINAPI capture_RealTimeStylusDisabled(IStylusSyncPlugin *iface, IRealTimeStylus *rts, ULONG count,
        const TABLET_CONTEXT_ID *tcids) { return S_OK; }
static HRESULT WINAPI capture_StylusInRange(IStylusSyncPlugin *iface, IRealTimeStylus *rts, TABLET_CONTEXT_ID tcid,
        STYLUS_ID sid) { printf("  in range: context %lu, stylus %lu\n", tcid, sid); return S_OK; }
static HRESULT WINAPI capture_StylusOutOfRange(IStylusSyncPlugin *iface, IRealTimeStylus *rts, TABLET_CONTEXT_ID tcid,
        STYLUS_ID sid) { printf("  out of range: context %lu, stylus %lu\n", tcid, sid); return S_OK; }
static HRESULT WINAPI capture_StylusDown(IStylusSyncPlugin *iface, IRealTimeStylus *rts, const StylusInfo *info,
        ULONG count, LONG *packet, LONG **changed)
{
    InterlockedIncrement(&impl_from_capture(iface)->events);
    print_packets("down", info, 1, count, packet);
    return S_OK;
}
static HRESULT WINAPI capture_StylusUp(IStylusSyncPlugin *iface, IRealTimeStylus *rts, const StylusInfo *info,
        ULONG count, LONG *packet, LONG **changed)
{
    InterlockedIncrement(&impl_from_capture(iface)->events);
    print_packets("up", info, 1, count, packet);
    return S_OK;
}
static HRESULT WINAPI capture_StylusButtonDown(IStylusSyncPlugin *iface, IRealTimeStylus *rts, STYLUS_ID sid,
        const GUID *button, POINT *pos) { return S_OK; }
static HRESULT WINAPI capture_StylusButtonUp(IStylusSyncPlugin *iface, IRealTimeStylus *rts, STYLUS_ID sid,
        const GUID *button, POINT *pos) { return S_OK; }
static HRESULT WINAPI capture_InAirPackets(IStylusSyncPlugin *iface, IRealTimeStylus *rts, const StylusInfo *info,
        ULONG count, ULONG length, LONG *packets, ULONG *changed_count, LONG **changed)
{
    InterlockedIncrement(&impl_from_capture(iface)->events);
    print_packets("in air", info, count, length, packets);
    return S_OK;
}
static HRESULT WINAPI capture_Packets(IStylusSyncPlugin *iface, IRealTimeStylus *rts, const StylusInfo *info,
        ULONG count, ULONG length, LONG *packets, ULONG *changed_count, LONG **changed)
{
    InterlockedIncrement(&impl_from_capture(iface)->events);
    print_packets("packets", info, count, length, packets);
    return S_OK;
}
static HRESULT WINAPI capture_CustomStylusDataAdded(IStylusSyncPlugin *iface, IRealTimeStylus *rts, const GUID *guid,
        ULONG size, const BYTE *data) { return S_OK; }
static HRESULT WINAPI capture_SystemEvent(IStylusSyncPlugin *iface, IRealTimeStylus *rts, TABLET_CONTEXT_ID tcid,
        STYLUS_ID sid, SYSTEM_EVENT event, SYSTEM_EVENT_DATA data) { return S_OK; }
static HRESULT WINAPI capture_TabletAdded(IStylusSyncPlugin *iface, IRealTimeStylus *rts, IInkTablet *tablet)
{ return S_OK; }
static HRESULT WINAPI capture_TabletRemoved(IStylusSyncPlugin *iface, IRealTimeStylus *rts, LONG index)
{ return S_OK; }
static HRESULT WINAPI capture_Error(IStylusSyncPlugin *iface, IRealTimeStylus *rts, IStylusPlugin *plugin,
        RealTimeStylusDataInterest interest, HRESULT error, LONG_PTR *key)
{ printf("  error %#lx\n", error); return S_OK; }
static HRESULT WINAPI capture_UpdateMapping(IStylusSyncPlugin *iface, IRealTimeStylus *rts) { return S_OK; }
static HRESULT WINAPI capture_DataInterest(IStylusSyncPlugin *iface, RealTimeStylusDataInterest *interest)
{
    *interest = RTSDI_StylusDown | RTSDI_Packets | RTSDI_StylusUp | RTSDI_InAirPackets | RTSDI_StylusInRange |
                RTSDI_StylusOutOfRange | RTSDI_RealTimeStylusEnabled | RTSDI_Error;
    return S_OK;
}

static const IStylusSyncPluginVtbl capture_vtbl =
{
    capture_QueryInterface, capture_AddRef, capture_Release, capture_RealTimeStylusEnabled,
    capture_RealTimeStylusDisabled, capture_StylusInRange, capture_StylusOutOfRange, capture_StylusDown,
    capture_StylusUp, capture_StylusButtonDown, capture_StylusButtonUp, capture_InAirPackets, capture_Packets,
    capture_CustomStylusDataAdded, capture_SystemEvent, capture_TabletAdded, capture_TabletRemoved, capture_Error,
    capture_UpdateMapping, capture_DataInterest,
};

static void pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG msg;

    while ((LONG)(end - GetTickCount()) > 0)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        MsgWaitForMultipleObjects(0, NULL, FALSE, 10, QS_ALLINPUT);
    }
}

static void send_mouse(int x, int y, DWORD flags)
{
    INPUT input = {INPUT_MOUSE};

    input.mi.dx = MulDiv(x, 65535, GetSystemMetrics(SM_CXSCREEN) - 1);
    input.mi.dy = MulDiv(y, 65535, GetSystemMetrics(SM_CYSCREEN) - 1);
    input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | flags;
    SendInput(1, &input, sizeof(input));
}

/* What the mouse gives a plugin, in which units: drags across the window with input made here, only the mouse,
 * and puts the cursor back afterwards.  Run with "drag". */
static void drag(void)
{
    static const POINT path[] = {{40, 40}, {60, 50}, {80, 60}, {120, 80}};
    struct capture capture = {{&capture_vtbl}, 1};
    IStrokeBuilder *builder = NULL;
    IInkDisp *ink = NULL;
    TABLET_CONTEXT_ID *tcids = NULL;
    PACKET_PROPERTY *properties;
    IRealTimeStylus *rts;
    POINT cursor, origin = {0, 0};
    ULONG count = 0, n, i;
    FLOAT sx, sy;
    HWND hwnd;
    HRESULT hr;

    /* SS_NOTIFY: a static control is otherwise transparent to the mouse, and the clicks would land on whatever
     * window is under it */
    hwnd = CreateWindowExW(WS_EX_TOPMOST, L"static", L"rtsprobe drag", WS_POPUP | WS_VISIBLE | WS_BORDER | SS_NOTIFY,
                           200, 200, 300, 200, NULL, NULL, NULL, NULL);
    UpdateWindow(hwnd);
    ClientToScreen(hwnd, &origin);
    printf("drag: window client origin %ld,%ld on the screen, screen %dx%d, %d dpi\n", origin.x, origin.y,
           GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), GetDeviceCaps(GetDC(NULL), LOGPIXELSX));
    hr = CoCreateInstance(&CLSID_RealTimeStylus, NULL, CLSCTX_INPROC_SERVER, &IID_IRealTimeStylus, (void **)&rts);
    if (FAILED(hr)) { DestroyWindow(hwnd); return; }
    CoCreateFreeThreadedMarshaler((IUnknown *)&capture.IStylusSyncPlugin_iface, &capture.marshaler);
    hr = IRealTimeStylus_AddStylusSyncPlugin(rts, 0, &capture.IStylusSyncPlugin_iface);
    printf("  AddStylusSyncPlugin %#lx\n", hr);
    /* and the stroke builder, to see which points the stroke gets */
    if (SUCCEEDED(CoCreateInstance(&CLSID_StrokeBuilder, NULL, CLSCTX_INPROC_SERVER, &IID_IStrokeBuilder, (void **)&builder)))
    {
        IStylusAsyncPlugin *async;

        IStrokeBuilder_get_Ink(builder, &ink);
        if (SUCCEEDED(IStrokeBuilder_QueryInterface(builder, &IID_IStylusAsyncPlugin, (void **)&async)))
        {
            hr = IRealTimeStylus_AddStylusAsyncPlugin(rts, 0, async);
            printf("  AddStylusAsyncPlugin(StrokeBuilder) %#lx\n", hr);
            IStylusAsyncPlugin_Release(async);
        }
    }
    IRealTimeStylus_put_HWND(rts, (HANDLE_PTR)hwnd);
    hr = IRealTimeStylus_put_Enabled(rts, TRUE);
    printf("  Enabled %#lx\n", hr);
    if (SUCCEEDED(IRealTimeStylus_GetAllTabletContextIds(rts, &count, &tcids)))
    {
        for (i = 0; i < count; i++)
        {
            properties = NULL;
            n = 0;
            if (SUCCEEDED(IRealTimeStylus_GetPacketDescriptionData(rts, tcids[i], &sx, &sy, &n, &properties)))
                printf("  context %lu: scale %f x %f, %lu properties\n", tcids[i], sx, sy, n);
            CoTaskMemFree(properties);
        }
        CoTaskMemFree(tcids);
    }
    pump(200);
    GetCursorPos(&cursor);
    for (i = 0; i < ARRAY_SIZE(path); i++)
    {
        printf("  mouse %s at client %ld,%ld, screen %ld,%ld\n", !i ? "down" : i + 1 < ARRAY_SIZE(path) ? "move" : "up",
               path[i].x, path[i].y, origin.x + path[i].x, origin.y + path[i].y);
        send_mouse(origin.x + path[i].x, origin.y + path[i].y,
                   !i ? MOUSEEVENTF_LEFTDOWN : i + 1 < ARRAY_SIZE(path) ? 0 : MOUSEEVENTF_LEFTUP);
        pump(150);
    }
    send_mouse(cursor.x, cursor.y, 0);
    pump(200);
    printf("  %ld events\n", capture.events);
    if (ink)
    {
        IInkStrokes *strokes = NULL;
        IInkStrokeDisp *stroke;
        LONG n = 0, k, lower, upper, *values;
        VARIANT v;

        IInkDisp_get_Strokes(ink, &strokes);
        if (strokes) IInkStrokes_get_Count(strokes, &n);
        printf("  the stroke builder's ink has %ld strokes\n", n);
        for (k = 0; k < n; k++)
        {
            if (FAILED(IInkStrokes_Item(strokes, k, &stroke))) continue;
            VariantInit(&v);
            if (SUCCEEDED(IInkStrokeDisp_GetPacketData(stroke, 0, -1, &v)) && V_VT(&v) == (VT_ARRAY | VT_I4))
            {
                SafeArrayGetLBound(V_ARRAY(&v), 1, &lower);
                SafeArrayGetUBound(V_ARRAY(&v), 1, &upper);
                SafeArrayAccessData(V_ARRAY(&v), (void **)&values);
                printf("  stroke %ld: %ld values:", k, upper - lower + 1);
                for (i = 0; i <= (ULONG)(upper - lower) && i < 16; i++) printf(" %ld", values[i]);
                printf("\n");
                SafeArrayUnaccessData(V_ARRAY(&v));
            }
            VariantClear(&v);
            IInkStrokeDisp_Release(stroke);
        }
        if (strokes) IInkStrokes_Release(strokes);
        IInkDisp_Release(ink);
    }
    if (builder) IStrokeBuilder_Release(builder);
    IRealTimeStylus_put_Enabled(rts, FALSE);
    IRealTimeStylus_RemoveAllStylusSyncPlugins(rts);
    IRealTimeStylus_Release(rts);
    if (capture.marshaler) IUnknown_Release(capture.marshaler);
    DestroyWindow(hwnd);
}

int main(int argc, char **argv)
{
    IRealTimeStylus *rts, *rts2;
    IStrokeBuilder *builder;
    IInkTablet *tablet;
    IInkStrokeDisp *stroke;
    IInkDisp *ink;
    IUnknown *unk;
    GUID *guids = NULL;
    ULONG count = 0, i;
    RECT rect, dirty;
    BOOL enabled;
    HWND hwnd, hwnd2;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    CloseHandle(CreateThread(NULL, 0, watchdog, NULL, 0, NULL));
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (argc > 1 && !strcmp(argv[1], "drag"))
    {
        drag();
        CoUninitialize();
        return 0;
    }
    hwnd = CreateWindowW(L"static", L"rtsprobe", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 300, 200, NULL, NULL, NULL, NULL);
    hwnd2 = CreateWindowW(L"static", L"rtsprobe 2", WS_OVERLAPPEDWINDOW, 100, 100, 300, 200, NULL, NULL, NULL, NULL);

    hr = CoCreateInstance(&CLSID_RealTimeStylus, NULL, CLSCTX_INPROC_SERVER, &IID_IRealTimeStylus, (void **)&rts);
    printf("RealTimeStylus %#lx\n", hr);
    if (FAILED(hr)) return 0;
    hr = IRealTimeStylus_QueryInterface(rts, &IID_IRealTimeStylus2, (void **)&unk);
    printf("QI IRealTimeStylus2 %#lx\n", hr);
    if (SUCCEEDED(hr)) IUnknown_Release(unk);
    hr = IRealTimeStylus_QueryInterface(rts, &IID_IRealTimeStylus3, (void **)&unk);
    printf("QI IRealTimeStylus3 %#lx\n", hr);
    if (SUCCEEDED(hr)) IUnknown_Release(unk);
    hr = IRealTimeStylus_QueryInterface(rts, &IID_IRealTimeStylusSynchronization, (void **)&unk);
    printf("QI IRealTimeStylusSynchronization %#lx\n", hr);
    if (SUCCEEDED(hr)) IUnknown_Release(unk);

    hr = IRealTimeStylus_GetDesiredPacketDescription(rts, &count, &guids);
    printf("GetDesiredPacketDescription %#lx, %lu:", hr, count);
    for (i = 0; SUCCEEDED(hr) && i < count; i++) printf(" %s", guid_name(&guids[i]));
    printf("\n");
    CoTaskMemFree(guids);
    hr = IRealTimeStylus_get_WindowInputRectangle(rts, &rect);
    printf("WindowInputRectangle %#lx (%ld,%ld)-(%ld,%ld)\n", hr, rect.left, rect.top, rect.right, rect.bottom);

    hr = IRealTimeStylus_put_Enabled(rts, TRUE);
    printf("Enabled without a window %#lx\n", hr);
    print_contexts(rts);
    tablet = (IInkTablet *)0xdeadbeef;
    hr = IRealTimeStylus_GetTablet(rts, &tablet);
    printf("GetTablet in all-tablets mode %#lx, tablet %s\n", hr, tablet == (IInkTablet *)0xdeadbeef ? "untouched"
           : tablet ? "set" : "NULL");
    if (SUCCEEDED(hr) && tablet && tablet != (IInkTablet *)0xdeadbeef) IInkTablet_Release(tablet);

    hr = IRealTimeStylus_put_HWND(rts, (HANDLE_PTR)hwnd);
    printf("put_HWND %#lx\n", hr);
    hr = IRealTimeStylus_put_Enabled(rts, TRUE);
    printf("Enabled %#lx\n", hr);
    hr = IRealTimeStylus_get_Enabled(rts, &enabled);
    printf("get_Enabled %#lx %d\n", hr, enabled);
    hr = IRealTimeStylus_get_WindowInputRectangle(rts, &rect);
    printf("WindowInputRectangle once enabled %#lx (%ld,%ld)-(%ld,%ld)\n", hr, rect.left, rect.top, rect.right, rect.bottom);
    hr = IRealTimeStylus_put_HWND(rts, (HANDLE_PTR)hwnd2);
    printf("put_HWND while enabled %#lx\n", hr);
    print_contexts(rts);
    hr = IRealTimeStylus_put_Enabled(rts, FALSE);
    printf("Disabled %#lx\n", hr);

    hr = IRealTimeStylus_SetAllTabletsMode(rts, FALSE);
    printf("SetAllTabletsMode(FALSE) %#lx\n", hr);
    hr = IRealTimeStylus_put_Enabled(rts, TRUE);
    printf("Enabled without the mouse %#lx\n", hr);
    print_contexts(rts);
    IRealTimeStylus_put_Enabled(rts, FALSE);
    hr = IRealTimeStylus_SetAllTabletsMode(rts, TRUE);
    printf("SetAllTabletsMode(TRUE) %#lx\n", hr);
    hr = IRealTimeStylus_put_Enabled(rts, TRUE);
    printf("Enabled with the mouse %#lx\n", hr);
    print_contexts(rts);
    IRealTimeStylus_put_Enabled(rts, FALSE);

    hr = CoCreateInstance(&CLSID_RealTimeStylus, NULL, CLSCTX_INPROC_SERVER, &IID_IRealTimeStylus, (void **)&rts2);
    if (SUCCEEDED(hr))
    {
        hr = IRealTimeStylus_put_Enabled(rts2, TRUE);
        printf("second stylus, enabled with no window ever set %#lx\n", hr);
        IRealTimeStylus_Release(rts2);
    }
    IRealTimeStylus_Release(rts);

    hr = CoCreateInstance(&CLSID_StrokeBuilder, NULL, CLSCTX_INPROC_SERVER, &IID_IStrokeBuilder, (void **)&builder);
    printf("StrokeBuilder %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        ink = NULL;
        hr = IStrokeBuilder_get_Ink(builder, &ink);
        printf("  default Ink %#lx %s\n", hr, ink ? "set" : "NULL");
        if (ink) IInkDisp_Release(ink);
        stroke = (void *)0xdeadbeef;
        hr = IStrokeBuilder_EndStroke(builder, 1, 1, &stroke, &dirty);
        printf("  EndStroke without BeginStroke %#lx, stroke %p\n", hr, stroke);
        hr = IStrokeBuilder_QueryInterface(builder, &IID_IStylusAsyncPlugin, (void **)&unk);
        printf("  QI IStylusAsyncPlugin %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            RealTimeStylusDataInterest interest = 0;
            IStylusPlugin_DataInterest((IStylusPlugin *)unk, &interest);
            printf("  DataInterest %#x\n", interest);
            IUnknown_Release(unk);
        }
        hr = IStrokeBuilder_QueryInterface(builder, &IID_IStylusSyncPlugin, (void **)&unk);
        printf("  QI IStylusSyncPlugin %#lx\n", hr);
        if (SUCCEEDED(hr)) IUnknown_Release(unk);
        IStrokeBuilder_Release(builder);
    }

    DestroyWindow(hwnd2);
    DestroyWindow(hwnd);
    CoUninitialize();
    return 0;
}
