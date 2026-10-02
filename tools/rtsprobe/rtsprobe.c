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

int main(void)
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
