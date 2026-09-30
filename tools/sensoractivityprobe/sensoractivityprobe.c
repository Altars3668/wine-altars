/*
 * sensoractivityprobe - what a Media Foundation sensor activity monitor reports, and when.
 *
 * PowerPoint delay-loads MFCreateSensorActivityMonitor from mfsensorgroup.dll during a slide
 * show; Wine had no such DLL and the failed delay load killed PowerPoint.  This prints what
 * Windows answers: whether MFStartup is needed, the arguments it refuses, whether Start
 * reports at once and on which thread and apartment, what a report holds (camera names, and
 * of each symbolic link only its start and its interface class, not the device instance),
 * what the report's methods answer outside their range, and what Start, Stop and Release do
 * when repeated or out of order.  It only watches: no camera is opened.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <objidl.h>
#include <mfapi.h>
#include <mfidl.h>
#include <stdio.h>
#include <stdarg.h>

static HRESULT (WINAPI *pMFCreateSensorActivityMonitor)(IMFSensorActivitiesReportCallback *, IMFSensorActivityMonitor **);
static CRITICAL_SECTION print_cs;
static DWORD main_tid;
static LARGE_INTEGER t0, freq;

static double now_ms(void)
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (t.QuadPart - t0.QuadPart) * 1000.0 / freq.QuadPart;
}

static void out(const char *fmt, ...)
{
    va_list args;
    EnterCriticalSection(&print_cs);
    printf("%7.0f ", now_ms());
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    fflush(stdout);
    LeaveCriticalSection(&print_cs);
}

static const char *guid_str(REFIID iid)
{
    static char buf[4][64];
    static int n;
    char *b = buf[n++ % 4];
    sprintf(b, "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", iid->Data1, iid->Data2, iid->Data3,
            iid->Data4[0], iid->Data4[1], iid->Data4[2], iid->Data4[3], iid->Data4[4], iid->Data4[5], iid->Data4[6], iid->Data4[7]);
    return b;
}

static const char *thread_str(void)
{
    static char buf[4][64];
    static int n;
    char *b = buf[n++ % 4];
    APTTYPE type;
    APTTYPEQUALIFIER qualifier;
    HRESULT hr = CoGetApartmentType(&type, &qualifier);
    if (hr == S_OK) sprintf(b, "%s thread, apartment %d/%d", GetCurrentThreadId() == main_tid ? "main" : "other", type, qualifier);
    else sprintf(b, "%s thread, no apartment (%#lx)", GetCurrentThreadId() == main_tid ? "main" : "other", hr);
    return b;
}

/* the start of a symbolic link up to its first '#', and its last two parts: no device instance */
static void link_shape(const WCHAR *link, char *buf, size_t size)
{
    const WCHAR *first = wcschr(link, '#'), *last = wcsrchr(link, '#');
    char a[64] = "", c[128] = "";
    WideCharToMultiByte(CP_UTF8, 0, link, first ? (int)(first - link) : lstrlenW(link), a, sizeof(a) - 1, NULL, NULL);
    if (last && last != first) WideCharToMultiByte(CP_UTF8, 0, last, -1, c, sizeof(c) - 1, NULL, NULL);
    snprintf(buf, size, "%s#...%s (%d chars)", a, c, lstrlenW(link));
}

struct callback
{
    IMFSensorActivitiesReportCallback IMFSensorActivitiesReportCallback_iface;
    LONG refcount;
    LONG reports;
    HANDLE event;
    const char *name;
    BOOL examine;
    HRESULT result;
};

static struct callback *impl_from_iface(IMFSensorActivitiesReportCallback *iface)
{
    return CONTAINING_RECORD(iface, struct callback, IMFSensorActivitiesReportCallback_iface);
}

static HRESULT WINAPI callback_QueryInterface(IMFSensorActivitiesReportCallback *iface, REFIID iid, void **out_obj)
{
    struct callback *callback = impl_from_iface(iface);
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IMFSensorActivitiesReportCallback))
    {
        *out_obj = iface;
        IMFSensorActivitiesReportCallback_AddRef(iface);
        return S_OK;
    }
    out("[%s] asked for %s on %s: E_NOINTERFACE\n", callback->name, guid_str(iid), thread_str());
    *out_obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI callback_AddRef(IMFSensorActivitiesReportCallback *iface)
{
    return InterlockedIncrement(&impl_from_iface(iface)->refcount);
}

static ULONG WINAPI callback_Release(IMFSensorActivitiesReportCallback *iface)
{
    return InterlockedDecrement(&impl_from_iface(iface)->refcount);
}

static void print_process(IMFSensorProcessActivity *activity, ULONG index)
{
    MFSensorDeviceMode mode = -1;
    BOOL streaming = -1;
    ULONG pid = 0;
    FILETIME time = {0}, now;
    HRESULT hr1, hr2, hr3, hr4;
    LONGLONG age;

    hr1 = IMFSensorProcessActivity_GetProcessId(activity, &pid);
    hr2 = IMFSensorProcessActivity_GetStreamingState(activity, &streaming);
    hr3 = IMFSensorProcessActivity_GetStreamingMode(activity, &mode);
    hr4 = IMFSensorProcessActivity_GetReportTime(activity, &time);
    GetSystemTimeAsFileTime(&now);
    age = (((LONGLONG)now.dwHighDateTime << 32 | now.dwLowDateTime) - ((LONGLONG)time.dwHighDateTime << 32 | time.dwLowDateTime)) / 10000;
    out("      process %lu: pid %s (%#lx), streaming %d (%#lx), mode %d (%#lx), report time %lld ms ago (%#lx)\n", index,
        pid == GetCurrentProcessId() ? "self" : pid ? "other" : "0", hr1, streaming, hr2, mode, hr3, age, hr4);
}

static void print_report(IMFSensorActivityReport *report, ULONG index)
{
    WCHAR name[512], link[1024];
    ULONG written = 12345, count = 12345, i;
    char name8[1024] = "", shape[256] = "";
    HRESULT hr;

    hr = IMFSensorActivityReport_GetFriendlyName(report, NULL, 0, &written);
    out("    report %lu: GetFriendlyName(NULL, 0) %#lx, written %lu\n", index, hr, written);
    written = 12345;
    hr = IMFSensorActivityReport_GetFriendlyName(report, name, 3, &written);
    out("    report %lu: GetFriendlyName(3 chars) %#lx, written %lu\n", index, hr, written);
    written = 12345;
    memset(name, 0, sizeof(name));
    hr = IMFSensorActivityReport_GetFriendlyName(report, name, ARRAYSIZE(name), &written);
    WideCharToMultiByte(CP_UTF8, 0, name, -1, name8, sizeof(name8) - 1, NULL, NULL);
    out("    report %lu: GetFriendlyName %#lx, written %lu, \"%s\" (%d chars)\n", index, hr, written, name8, lstrlenW(name));
    written = 12345;
    hr = IMFSensorActivityReport_GetSymbolicLink(report, NULL, 0, &written);
    out("    report %lu: GetSymbolicLink(NULL, 0) %#lx, written %lu\n", index, hr, written);
    written = 12345;
    memset(link, 0, sizeof(link));
    hr = IMFSensorActivityReport_GetSymbolicLink(report, link, ARRAYSIZE(link), &written);
    link_shape(link, shape, sizeof(shape));
    out("    report %lu: GetSymbolicLink %#lx, written %lu, %s\n", index, hr, written, shape);
    hr = IMFSensorActivityReport_GetProcessCount(report, &count);
    out("    report %lu: GetProcessCount %#lx, %lu\n", index, hr, count);
    for (i = 0; SUCCEEDED(hr) && i < count; i++)
    {
        IMFSensorProcessActivity *activity = NULL;
        HRESULT hr2 = IMFSensorActivityReport_GetProcessActivity(report, i, &activity);
        if (FAILED(hr2)) out("      process %lu: GetProcessActivity %#lx\n", i, hr2);
        else
        {
            print_process(activity, i);
            IMFSensorProcessActivity_Release(activity);
        }
    }
    if (SUCCEEDED(hr))
    {
        IMFSensorProcessActivity *activity = (void *)0xdeadbeef;
        HRESULT hr2 = IMFSensorActivityReport_GetProcessActivity(report, count, &activity);
        out("    report %lu: GetProcessActivity(%lu) %#lx, %p\n", index, count, hr2, activity);
        if (SUCCEEDED(hr2) && activity) IMFSensorProcessActivity_Release(activity);
        hr2 = IMFSensorActivityReport_GetProcessCount(report, NULL);
        out("    report %lu: GetProcessCount(NULL) %#lx\n", index, hr2);
    }
}

static HRESULT WINAPI callback_OnActivitiesReport(IMFSensorActivitiesReportCallback *iface, IMFSensorActivitiesReport *report)
{
    struct callback *callback = impl_from_iface(iface);
    LONG n = InterlockedIncrement(&callback->reports);
    ULONG count = 12345, i;
    HRESULT hr;

    out("[%s] report %ld on %s, report %p\n", callback->name, n, thread_str(), report);
    if (!report) goto done;
    hr = IMFSensorActivitiesReport_GetCount(report, &count);
    out("  GetCount %#lx, %lu\n", hr, count);
    for (i = 0; SUCCEEDED(hr) && i < count; i++)
    {
        IMFSensorActivityReport *activity = NULL;
        HRESULT hr2 = IMFSensorActivitiesReport_GetActivityReport(report, i, &activity);
        if (FAILED(hr2)) out("    report %lu: GetActivityReport %#lx\n", i, hr2);
        else
        {
            print_report(activity, i);
            IMFSensorActivityReport_Release(activity);
        }
    }
    if (callback->examine && n == 1)
    {
        IMFSensorActivityReport *activity = (void *)0xdeadbeef;
        IUnknown *unk = (void *)0xdeadbeef;

        hr = IMFSensorActivitiesReport_GetCount(report, NULL);
        out("  GetCount(NULL) %#lx\n", hr);
        hr = IMFSensorActivitiesReport_GetActivityReport(report, count, &activity);
        out("  GetActivityReport(%lu) %#lx, %p\n", count, hr, activity);
        if (SUCCEEDED(hr) && activity) IMFSensorActivityReport_Release(activity);
        hr = IMFSensorActivitiesReport_GetActivityReport(report, 0, NULL);
        out("  GetActivityReport(0, NULL) %#lx\n", hr);
        activity = (void *)0xdeadbeef;
        hr = IMFSensorActivitiesReport_GetActivityReportByDeviceName(report, L"\\\\?\\no#such#device", &activity);
        out("  GetActivityReportByDeviceName(no such device) %#lx, %p\n", hr, activity);
        if (SUCCEEDED(hr) && activity) IMFSensorActivityReport_Release(activity);
        activity = (void *)0xdeadbeef;
        hr = IMFSensorActivitiesReport_GetActivityReportByDeviceName(report, NULL, &activity);
        out("  GetActivityReportByDeviceName(NULL) %#lx, %p\n", hr, activity);
        if (SUCCEEDED(hr) && activity) IMFSensorActivityReport_Release(activity);
        if (count)
        {
            IMFSensorActivityReport *first, *byname = NULL;
            WCHAR link[1024] = {0};
            ULONG written;
            if (SUCCEEDED(IMFSensorActivitiesReport_GetActivityReport(report, 0, &first)))
            {
                IMFSensorActivityReport_GetSymbolicLink(first, link, ARRAYSIZE(link), &written);
                hr = IMFSensorActivitiesReport_GetActivityReportByDeviceName(report, link, &byname);
                out("  GetActivityReportByDeviceName(link of report 0) %#lx, same object %d\n", hr, byname == first);
                if (byname) IMFSensorActivityReport_Release(byname);
                CharUpperW(link);
                byname = NULL;
                hr = IMFSensorActivitiesReport_GetActivityReportByDeviceName(report, link, &byname);
                out("  GetActivityReportByDeviceName(upper-cased link) %#lx\n", hr);
                if (byname) IMFSensorActivityReport_Release(byname);
                IMFSensorActivityReport_Release(first);
            }
        }
        hr = IMFSensorActivitiesReport_QueryInterface(report, &IID_IAgileObject, (void **)&unk);
        out("  report QI IAgileObject %#lx\n", hr);
        if (SUCCEEDED(hr)) IUnknown_Release(unk);
        hr = IMFSensorActivitiesReport_QueryInterface(report, &IID_IMarshal, (void **)&unk);
        out("  report QI IMarshal %#lx\n", hr);
        if (SUCCEEDED(hr)) IUnknown_Release(unk);
    }
done:
    SetEvent(callback->event);
    return callback->result;
}

static const IMFSensorActivitiesReportCallbackVtbl callback_vtbl =
{
    callback_QueryInterface,
    callback_AddRef,
    callback_Release,
    callback_OnActivitiesReport,
};

static void callback_init(struct callback *callback, const char *name)
{
    memset(callback, 0, sizeof(*callback));
    callback->IMFSensorActivitiesReportCallback_iface.lpVtbl = &callback_vtbl;
    callback->refcount = 1;
    callback->event = CreateEventW(NULL, FALSE, FALSE, NULL);
    callback->name = name;
}

/* waits while pumping messages, as an STA would; returns whether the event came */
static BOOL pump(HANDLE event, DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    for (;;)
    {
        DWORD left = (LONG)(end - GetTickCount()) > 0 ? end - GetTickCount() : 0;
        DWORD ret = MsgWaitForMultipleObjects(event ? 1 : 0, event ? &event : NULL, FALSE, left, QS_ALLINPUT);
        MSG msg;
        if (event && ret == WAIT_OBJECT_0) return TRUE;
        if (ret == WAIT_TIMEOUT) return FALSE;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
}

int main(void)
{
    struct callback callback, callback2, callback3;
    IMFSensorActivityMonitor *monitor = (void *)0xdeadbeef, *monitor2, *monitor3;
    IUnknown *unk;
    HMODULE module;
    HRESULT hr;
    LONG reports;
    ULONG ref;

    InitializeCriticalSection(&print_cs);
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    main_tid = GetCurrentThreadId();

    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    out("CoInitializeEx(STA) %#lx; the main thread pumps messages while it waits\n", hr);
    module = LoadLibraryW(L"mfsensorgroup.dll");
    out("mfsensorgroup.dll %s (%lu)\n", module ? "loaded" : "missing", module ? 0 : GetLastError());
    if (!module) return 0;
    pMFCreateSensorActivityMonitor = (void *)GetProcAddress(module, "MFCreateSensorActivityMonitor");
    out("MFCreateSensorActivityMonitor %s\n", pMFCreateSensorActivityMonitor ? "exported" : "missing");
    if (!pMFCreateSensorActivityMonitor) return 0;

    callback_init(&callback, "cb1");
    callback.examine = TRUE;
    hr = pMFCreateSensorActivityMonitor(&callback.IMFSensorActivitiesReportCallback_iface, &monitor);
    out("before MFStartup: create %#lx, monitor %s, callback refs %ld\n", hr, monitor == (void *)0xdeadbeef ? "untouched" : monitor ? "set" : "NULL", callback.refcount - 1);
    if (SUCCEEDED(hr) && monitor)
    {
        ref = IMFSensorActivityMonitor_Release(monitor);
        out("  released it: %lu, callback refs %ld\n", ref, callback.refcount - 1);
    }
    hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    out("MFStartup %#lx\n", hr);

    monitor = (void *)0xdeadbeef;
    hr = pMFCreateSensorActivityMonitor(NULL, &monitor);
    out("create(NULL callback) %#lx, monitor %s\n", hr, monitor == (void *)0xdeadbeef ? "untouched" : monitor ? "set" : "NULL");
    if (SUCCEEDED(hr) && monitor && monitor != (void *)0xdeadbeef) IMFSensorActivityMonitor_Release(monitor);
    hr = pMFCreateSensorActivityMonitor(&callback.IMFSensorActivitiesReportCallback_iface, NULL);
    out("create(NULL out) %#lx, callback refs %ld\n", hr, callback.refcount - 1);

    hr = pMFCreateSensorActivityMonitor(&callback.IMFSensorActivitiesReportCallback_iface, &monitor);
    out("create %#lx, callback refs %ld\n", hr, callback.refcount - 1);
    if (FAILED(hr)) return 0;
    hr = IMFSensorActivityMonitor_QueryInterface(monitor, &IID_IAgileObject, (void **)&unk);
    out("monitor QI IAgileObject %#lx\n", hr);
    if (SUCCEEDED(hr)) IUnknown_Release(unk);
    hr = IMFSensorActivityMonitor_QueryInterface(monitor, &IID_IMarshal, (void **)&unk);
    out("monitor QI IMarshal %#lx\n", hr);
    if (SUCCEEDED(hr)) IUnknown_Release(unk);
    pump(NULL, 500);
    out("500 ms after create, no Start: %ld reports\n", callback.reports);

    hr = IMFSensorActivityMonitor_Start(monitor);
    out("Start %#lx, callback refs %ld\n", hr, callback.refcount - 1);
    out("first report %s\n", pump(callback.event, 3000) ? "came" : "did not come within 3 s");
    pump(NULL, 2000);
    out("2 s later: %ld reports\n", callback.reports);
    reports = callback.reports;
    hr = IMFSensorActivityMonitor_Start(monitor);
    out("Start again %#lx\n", hr);
    pump(NULL, 1500);
    out("1.5 s later: %ld new reports\n", callback.reports - reports);
    hr = IMFSensorActivityMonitor_Stop(monitor);
    out("Stop %#lx, callback refs %ld\n", hr, callback.refcount - 1);
    reports = callback.reports;
    pump(NULL, 1500);
    out("1.5 s after Stop: %ld new reports\n", callback.reports - reports);
    hr = IMFSensorActivityMonitor_Stop(monitor);
    out("Stop again %#lx\n", hr);
    reports = callback.reports;
    hr = IMFSensorActivityMonitor_Start(monitor);
    out("Start after Stop %#lx\n", hr);
    pump(NULL, 3000);
    out("3 s later: %ld new reports\n", callback.reports - reports);
    hr = IMFSensorActivityMonitor_Stop(monitor);
    out("Stop %#lx\n", hr);
    ref = IMFSensorActivityMonitor_Release(monitor);
    out("Release %lu, callback refs %ld\n", ref, callback.refcount - 1);

    callback_init(&callback2, "cb2");
    hr = pMFCreateSensorActivityMonitor(&callback2.IMFSensorActivitiesReportCallback_iface, &monitor2);
    out("second monitor: create %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IMFSensorActivityMonitor_Stop(monitor2);
        out("  Stop before any Start %#lx\n", hr);
        pump(NULL, 500);
        ref = IMFSensorActivityMonitor_Release(monitor2);
        out("  Release %lu, callback refs %ld, reports %ld\n", ref, callback2.refcount - 1, callback2.reports);
    }

    callback_init(&callback3, "cb3");
    callback3.result = E_FAIL;
    hr = pMFCreateSensorActivityMonitor(&callback3.IMFSensorActivitiesReportCallback_iface, &monitor3);
    out("third monitor (callback fails its reports): create %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IMFSensorActivityMonitor_Start(monitor3);
        out("  Start %#lx\n", hr);
        ref = IMFSensorActivityMonitor_Release(monitor3);
        out("  Release while started %lu, callback refs %ld\n", ref, callback3.refcount - 1);
        pump(NULL, 3000);
        out("  3 s later: reports %ld, callback refs %ld\n", callback3.reports, callback3.refcount - 1);
    }

    MFShutdown();
    out("done\n");
    return 0;
}
