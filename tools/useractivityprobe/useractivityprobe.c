/*
 * useractivityprobe - what IUserActivityRequestManagerInterop::GetForWindow gives a desktop application: for a window
 * of its own (twice, and for a second window), for no window, for the desktop, for a window of another process and for
 * one that is gone; and whether the managers take a handler and give it back.  Word asks the factory of
 * UserActivityRequestManager for this interface when it opens a document.
 *
 *   useractivityprobe
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <roapi.h>
#include <winstring.h>
#include <stdio.h>

DEFINE_GUID(IID_IActivationFactory_, 0x00000035, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
DEFINE_GUID(IID_IInspectable_, 0xaf86e2e0, 0xb12d, 0x4c6a, 0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90);
DEFINE_GUID(IID_IUserActivityRequestManagerInterop, 0xdd69f876, 0x9699, 0x4715, 0x90, 0x95, 0xe3, 0x7e, 0xa3, 0x0d, 0xfa, 0x1b);
DEFINE_GUID(IID_IUserActivityRequestManager, 0x0c30be4e, 0x903d, 0x48d6, 0x82, 0xd4, 0x40, 0x43, 0xed, 0x57, 0x79, 0x1b);
DEFINE_GUID(IID_IUserActivityRequestManagerStatics, 0xc0392df1, 0x224a, 0x432c, 0x81, 0xe5, 0x0c, 0x76, 0xb4, 0xc4, 0xce, 0xfa);
DEFINE_GUID(IID_IAgileObject_, 0x94ea2b94, 0xe9cc, 0x49e0, 0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90);

typedef struct { void **vtbl; } iface_t;
typedef HRESULT (STDMETHODCALLTYPE *QI)(void *, REFIID, void **);
typedef ULONG (STDMETHODCALLTYPE *REL)(void *);
typedef HRESULT (STDMETHODCALLTYPE *GETFORWINDOW)(void *, HWND, REFIID, void **);
typedef HRESULT (STDMETHODCALLTYPE *ADD)(void *, void *, INT64 *);
typedef HRESULT (STDMETHODCALLTYPE *REMOVE)(void *, INT64);
typedef HRESULT (STDMETHODCALLTYPE *CLASSNAME)(void *, HSTRING *);

static HRESULT query(void *obj, REFIID iid, void **out) { return ((QI)((iface_t *)obj)->vtbl[0])(obj, iid, out); }
static ULONG release(void *obj) { return ((REL)((iface_t *)obj)->vtbl[2])(obj); }

/* a handler that is an IUnknown and nothing else, which is all the manager keeps */
static HRESULT STDMETHODCALLTYPE handler_qi(void *self, REFIID iid, void **out) { *out = self; return S_OK; }
static ULONG STDMETHODCALLTYPE handler_addref(void *self) { return 2; }
static ULONG STDMETHODCALLTYPE handler_release(void *self) { return 1; }
static HRESULT STDMETHODCALLTYPE handler_invoke(void *self, void *sender, void *args) { printf("  handler called\n"); return S_OK; }
static void *handler_vtbl[] = { handler_qi, handler_addref, handler_release, handler_invoke };
static iface_t handler = { handler_vtbl };

static void describe(const char *what, void *interop, HWND hwnd, void **keep)
{
    void *manager = NULL, *again = NULL, *unk1 = NULL, *unk2 = NULL, *other;
    HRESULT hr = ((GETFORWINDOW)((iface_t *)interop)->vtbl[6])(interop, hwnd, &IID_IUserActivityRequestManager, &manager);
    HSTRING name = NULL;
    INT64 token = 0;

    printf("%s: %#lx, %s\n", what, hr, manager ? "a manager" : "none");
    if (FAILED(hr) || !manager) return;
    hr = ((GETFORWINDOW)((iface_t *)interop)->vtbl[6])(interop, hwnd, &IID_IUserActivityRequestManager, &again);
    query(manager, &IID_IUnknown, &unk1);
    if (again) query(again, &IID_IUnknown, &unk2);
    printf("  again: %#lx, %s\n", hr, unk1 == unk2 ? "the same object" : "another object");
    if (keep && *keep)
    {
        void *unk3 = NULL;
        query(*keep, &IID_IUnknown, &unk3);
        printf("  against the first window's: %s\n", unk3 == unk1 ? "the same object" : "another object");
        if (unk3) release(unk3);
    }
    printf("  IAgileObject %#lx, IUserActivityRequestManagerStatics %#lx\n", query(manager, &IID_IAgileObject_, &other),
           query(manager, &IID_IUserActivityRequestManagerStatics, (void **)&unk2));
    if (SUCCEEDED(((CLASSNAME)((iface_t *)manager)->vtbl[4])(manager, &name)))
        printf("  class %ls\n", WindowsGetStringRawBuffer(name, NULL));
    hr = ((ADD)((iface_t *)manager)->vtbl[6])(manager, &handler, &token);
    printf("  add_UserActivityRequested %#lx, token %s the handler's address\n", hr,
           token == (INT64)(ULONG_PTR)&handler ? "is" : "is not");
    hr = ((ADD)((iface_t *)manager)->vtbl[6])(manager, NULL, &token);
    printf("  add of no handler %#lx\n", hr);
    printf("  remove %#lx, remove again %#lx\n", ((REMOVE)((iface_t *)manager)->vtbl[7])(manager, token),
           ((REMOVE)((iface_t *)manager)->vtbl[7])(manager, token));
    if (keep && !*keep) { *keep = manager; return; }
    release(manager);
}

int main(void)
{
    static const WCHAR class_name[] = L"Windows.ApplicationModel.UserActivities.UserActivityRequestManager";
    HSTRING str;
    void *factory = NULL, *interop = NULL, *first = NULL;
    HWND window, second, gone, shell;
    HRESULT hr;

    RoInitialize(RO_INIT_SINGLETHREADED);
    WindowsCreateString(class_name, lstrlenW(class_name), &str);
    hr = RoGetActivationFactory(str, &IID_IActivationFactory_, &factory);
    printf("factory %#lx\n", hr);
    if (FAILED(hr)) return 1;
    {
        void *statics = NULL, *view = NULL;
        typedef HRESULT (STDMETHODCALLTYPE *GETFORVIEW)(void *, void **);

        hr = query(factory, &IID_IUserActivityRequestManagerStatics, &statics);
        printf("IUserActivityRequestManagerStatics %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            hr = ((GETFORVIEW)((iface_t *)statics)->vtbl[6])(statics, &view);
            printf("GetForCurrentView %#lx, %s\n", hr, view ? "a manager" : "none");
        }
    }
    hr = query(factory, &IID_IUserActivityRequestManagerInterop, &interop);
    printf("IUserActivityRequestManagerInterop %#lx\n", hr);
    if (FAILED(hr)) return 1;

    window = CreateWindowW(L"static", L"useractivityprobe", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, NULL, NULL, NULL, NULL);
    second = CreateWindowW(L"static", L"useractivityprobe 2", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, NULL, NULL, NULL, NULL);
    gone = CreateWindowW(L"static", L"gone", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, NULL, NULL, NULL, NULL);
    DestroyWindow(gone);
    shell = GetShellWindow();

    describe("own window", interop, window, &first);
    describe("second own window", interop, second, &first);
    describe("no window", interop, NULL, NULL);
    describe("desktop window", interop, GetDesktopWindow(), NULL);
    describe(shell ? "shell window (another process)" : "no shell window", interop, shell, NULL);
    describe("destroyed window", interop, gone, NULL);
    describe("child window", interop, CreateWindowW(L"static", L"child", WS_CHILD, 0, 0, 10, 10, window, NULL, NULL, NULL), NULL);
    return 0;
}
