/* probe_appvis: the class Word asks twinapi.dll for, {7E5FE3D9-985F-4908-91F9-EE19F9FD1514}, taken
 * to be CLSID_AppVisibility: what it answers to, and what IAppVisibility says. */
#define COBJMACROS
#include <stdio.h>
#include <windows.h>
#include <objbase.h>

static const CLSID CLSID_AppVisibility_ = {0x7e5fe3d9, 0x985f, 0x4908, {0x91, 0xf9, 0xee, 0x19, 0xf9, 0xfd, 0x15, 0x14}};
static const IID IID_IAppVisibility_ = {0x2246ea2d, 0xcaea, 0x4444, {0xa3, 0xc4, 0x6d, 0xe8, 0x27, 0xe4, 0x43, 0x13}};
static const IID IID_IAppVisibilityEvents_ = {0x6584ce6b, 0x7d82, 0x49c2, {0x89, 0xc9, 0xc6, 0xbc, 0x02, 0xba, 0x8c, 0x38}};

typedef struct IAppVisibilityEvents IAppVisibilityEvents;
typedef struct
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IAppVisibilityEvents *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IAppVisibilityEvents *);
    ULONG (STDMETHODCALLTYPE *Release)(IAppVisibilityEvents *);
    HRESULT (STDMETHODCALLTYPE *AppVisibilityOnMonitorChanged)(IAppVisibilityEvents *, HMONITOR, int, int);
    HRESULT (STDMETHODCALLTYPE *LauncherVisibilityChange)(IAppVisibilityEvents *, BOOL);
} IAppVisibilityEventsVtbl;
struct IAppVisibilityEvents { const IAppVisibilityEventsVtbl *lpVtbl; };

typedef struct IAppVisibility IAppVisibility;
typedef struct
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IAppVisibility *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(IAppVisibility *);
    ULONG (STDMETHODCALLTYPE *Release)(IAppVisibility *);
    HRESULT (STDMETHODCALLTYPE *GetAppVisibilityOnMonitor)(IAppVisibility *, HMONITOR, int *);
    HRESULT (STDMETHODCALLTYPE *IsLauncherVisible)(IAppVisibility *, BOOL *);
    HRESULT (STDMETHODCALLTYPE *Advise)(IAppVisibility *, IAppVisibilityEvents *, DWORD *);
    HRESULT (STDMETHODCALLTYPE *Unadvise)(IAppVisibility *, DWORD);
} IAppVisibilityVtbl;
struct IAppVisibility { const IAppVisibilityVtbl *lpVtbl; };

static LONG event_refs, qi_calls;
static HRESULT STDMETHODCALLTYPE ev_QueryInterface(IAppVisibilityEvents *iface, REFIID iid, void **out)
{
    WCHAR buf[64];
    StringFromGUID2(iid, buf, 64);
    qi_calls++;
    printf("    events QI %ls\n", buf);
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IAppVisibilityEvents_))
    {
        *out = iface;
        event_refs++;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE ev_AddRef(IAppVisibilityEvents *iface) { return ++event_refs; }
static ULONG STDMETHODCALLTYPE ev_Release(IAppVisibilityEvents *iface) { return --event_refs; }
static HRESULT STDMETHODCALLTYPE ev_changed(IAppVisibilityEvents *iface, HMONITOR monitor, int prev, int cur)
{
    printf("    AppVisibilityOnMonitorChanged %d -> %d\n", prev, cur);
    return S_OK;
}
static HRESULT STDMETHODCALLTYPE ev_launcher(IAppVisibilityEvents *iface, BOOL visible)
{
    printf("    LauncherVisibilityChange %d\n", visible);
    return S_OK;
}
static const IAppVisibilityEventsVtbl ev_vtbl = {ev_QueryInterface, ev_AddRef, ev_Release, ev_changed, ev_launcher};
static IAppVisibilityEvents events = {&ev_vtbl};

static void try_qi(IUnknown *unk, const char *name, REFIID iid)
{
    IUnknown *out = NULL;
    HRESULT hr = IUnknown_QueryInterface(unk, iid, (void **)&out);
    printf("  QI %s %#lx\n", name, hr);
    if (out) IUnknown_Release(out);
}

static void run(const char *what, DWORD model)
{
    IAppVisibility *vis = NULL, *vis2 = NULL;
    HMONITOR monitor = MonitorFromPoint((POINT){0, 0}, MONITOR_DEFAULTTOPRIMARY);
    DWORD cookie = 0xdead, cookie2 = 0xdead;
    int mode = -1;
    BOOL visible = 2;
    HRESULT hr;

    printf("=== %s\n", what);
    CoInitializeEx(NULL, model);
    hr = CoCreateInstance(&CLSID_AppVisibility_, NULL, CLSCTX_INPROC_SERVER, &IID_IAppVisibility_, (void **)&vis);
    printf("CoCreateInstance %#lx %d\n", hr, vis != NULL);
    if (!vis) { CoUninitialize(); return; }
    hr = CoCreateInstance(&CLSID_AppVisibility_, NULL, CLSCTX_INPROC_SERVER, &IID_IAppVisibility_, (void **)&vis2);
    printf("second instance %#lx same %d\n", hr, vis == vis2);
    if (vis2) vis2->lpVtbl->Release(vis2);
    try_qi((IUnknown *)vis, "IUnknown", &IID_IUnknown);
    try_qi((IUnknown *)vis, "IMarshal", &IID_IMarshal);
    try_qi((IUnknown *)vis, "IAgileObject", &IID_IAgileObject);
    try_qi((IUnknown *)vis, "IClassFactory", &IID_IClassFactory);

    hr = vis->lpVtbl->IsLauncherVisible(vis, &visible);
    printf("IsLauncherVisible %#lx %d\n", hr, visible);
    hr = vis->lpVtbl->GetAppVisibilityOnMonitor(vis, monitor, &mode);
    printf("GetAppVisibilityOnMonitor(primary) %#lx %d\n", hr, mode);
    mode = -1;
    hr = vis->lpVtbl->GetAppVisibilityOnMonitor(vis, NULL, &mode);
    printf("GetAppVisibilityOnMonitor(NULL) %#lx %d\n", hr, mode);
    mode = -1;
    hr = vis->lpVtbl->GetAppVisibilityOnMonitor(vis, (HMONITOR)0x12345, &mode);
    printf("GetAppVisibilityOnMonitor(bogus) %#lx %d\n", hr, mode);

    hr = vis->lpVtbl->Advise(vis, &events, &cookie);
    printf("Advise %#lx cookie %lu refs %ld qi %ld\n", hr, cookie, event_refs, qi_calls);
    hr = vis->lpVtbl->Advise(vis, &events, &cookie2);
    printf("Advise again %#lx cookie %lu refs %ld\n", hr, cookie2, event_refs);
    hr = vis->lpVtbl->Unadvise(vis, cookie);
    printf("Unadvise %#lx refs %ld\n", hr, event_refs);
    hr = vis->lpVtbl->Unadvise(vis, cookie);
    printf("Unadvise again %#lx\n", hr);
    hr = vis->lpVtbl->Unadvise(vis, cookie2);
    printf("Unadvise second %#lx refs %ld\n", hr, event_refs);
    hr = vis->lpVtbl->Unadvise(vis, 0);
    printf("Unadvise 0 %#lx\n", hr);
    hr = vis->lpVtbl->Advise(vis, NULL, &cookie);
    printf("Advise NULL %#lx\n", hr);
    printf("Release %lu\n", vis->lpVtbl->Release(vis));
    CoUninitialize();
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    run("sta", COINIT_APARTMENTTHREADED);
    run("mta", COINIT_MULTITHREADED);
    printf("done\n");
    return 0;
}
