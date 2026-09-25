/* probe_interact: Windows.UI.Composition.Interactions the way Office's AirSpace meets them -- a
 * VisualInteractionSource on a visual, an InteractionTracker with an owner, position updates and
 * the owner's callbacks, and an expression that moves a visual by the tracker's position.  Needs a
 * desktop session (scripts/winrun.sh --desktop). */
#define COBJMACROS
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "windef.h"
#include "initguid.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "winstring.h"
#include "roapi.h"
#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#define WIDL_using_Windows_Foundation_Numerics
#define WIDL_using_Windows_Graphics
#define WIDL_using_Windows_Graphics_DirectX
#define WIDL_using_Windows_Graphics_Effects
#define WIDL_using_Windows_System
#define WIDL_using_Windows_UI
#define WIDL_using_Windows_UI_Core
#define WIDL_using_Windows_UI_Composition
#define WIDL_using_Windows_UI_Composition_Core
#define WIDL_using_Windows_UI_Composition_Desktop
#define WIDL_using_Windows_UI_Composition_Interactions
#include "windows.foundation.h"
#include "windows.ui.composition.h"
#include "windows.ui.composition.interop.h"

static HSTRING S(const WCHAR *s)
{
    HSTRING h = NULL;
    WindowsCreateString(s, wcslen(s), &h);
    return h;
}

static void print_class(const char *what, void *obj)
{
    IInspectable *inspectable;
    HSTRING name = NULL;
    HRESULT hr;
    if (!obj) { printf("%s: (null)\n", what); return; }
    IUnknown_QueryInterface((IUnknown *)obj, &IID_IInspectable, (void **)&inspectable);
    hr = IInspectable_GetRuntimeClassName(inspectable, &name);
    printf("%s: class %#lx %ls\n", what, hr, name ? WindowsGetStringRawBuffer(name, NULL) : L"(null)");
    WindowsDeleteString(name);
    IInspectable_Release(inspectable);
}

static void qi(const char *what, void *obj, const IID *iid, const char *name)
{
    IUnknown *unk = NULL;
    HRESULT hr = IUnknown_QueryInterface((IUnknown *)obj, iid, (void **)&unk);
    printf("  %s QI %s %#lx\n", what, name, hr);
    if (unk) IUnknown_Release(unk);
}
#define QI(what, obj, i) qi(what, obj, &IID_##i, #i)

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static DWORD main_tid;
static const char *phase = "";

static void pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG msg;
    while (GetTickCount() < end)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        Sleep(5);
    }
}

static void print_v3(const char *what, Vector3 v)
{
    printf("  %s %g %g %g\n", what, v.X, v.Y, v.Z);
}

/*** the owner ***/

static LONG owner_refs = 1;
static HRESULT WINAPI owner_QueryInterface(IInteractionTrackerOwner *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IInteractionTrackerOwner) || IsEqualGUID(iid, &IID_IAgileObject))
    {
        *out = iface;
        InterlockedIncrement(&owner_refs);
        return S_OK;
    }
    {
        WCHAR buf[64];
        StringFromGUID2(iid, buf, 64);
        printf("    [owner QI %ls]\n", buf);
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI owner_AddRef(IInteractionTrackerOwner *iface) { return InterlockedIncrement(&owner_refs); }
static ULONG WINAPI owner_Release(IInteractionTrackerOwner *iface) { return InterlockedDecrement(&owner_refs); }
static HRESULT WINAPI owner_GetIids(IInteractionTrackerOwner *iface, ULONG *count, IID **iids) { return E_NOTIMPL; }
static HRESULT WINAPI owner_GetRuntimeClassName(IInteractionTrackerOwner *iface, HSTRING *name) { return E_NOTIMPL; }
static HRESULT WINAPI owner_GetTrustLevel(IInteractionTrackerOwner *iface, TrustLevel *level) { return E_NOTIMPL; }

static const char *thread_name(void)
{
    return GetCurrentThreadId() == main_tid ? "main" : "other";
}

static HRESULT WINAPI owner_CustomAnimationStateEntered(IInteractionTrackerOwner *iface, IInteractionTracker *sender,
        IInteractionTrackerCustomAnimationStateEnteredArgs *args)
{
    INT32 id = -1;
    IInteractionTrackerCustomAnimationStateEnteredArgs_get_RequestId(args, &id);
    printf("    [%s] CustomAnimationStateEntered id %d (%s)\n", phase, id, thread_name());
    return S_OK;
}
static HRESULT WINAPI owner_IdleStateEntered(IInteractionTrackerOwner *iface, IInteractionTracker *sender,
        IInteractionTrackerIdleStateEnteredArgs *args)
{
    INT32 id = -1;
    IInteractionTrackerIdleStateEnteredArgs_get_RequestId(args, &id);
    printf("    [%s] IdleStateEntered id %d (%s)\n", phase, id, thread_name());
    print_class("      args", args);
    return S_OK;
}
static HRESULT WINAPI owner_InertiaStateEntered(IInteractionTrackerOwner *iface, IInteractionTracker *sender,
        IInteractionTrackerInertiaStateEnteredArgs *args)
{
    INT32 id = -1;
    Vector3 v = {0};
    IInteractionTrackerInertiaStateEnteredArgs_get_RequestId(args, &id);
    IInteractionTrackerInertiaStateEnteredArgs_get_NaturalRestingPosition(args, &v);
    printf("    [%s] InertiaStateEntered id %d resting %g %g (%s)\n", phase, id, v.X, v.Y, thread_name());
    return S_OK;
}
static HRESULT WINAPI owner_InteractingStateEntered(IInteractionTrackerOwner *iface, IInteractionTracker *sender,
        IInteractionTrackerInteractingStateEnteredArgs *args)
{
    INT32 id = -1;
    IInteractionTrackerInteractingStateEnteredArgs_get_RequestId(args, &id);
    printf("    [%s] InteractingStateEntered id %d (%s)\n", phase, id, thread_name());
    return S_OK;
}
static HRESULT WINAPI owner_RequestIgnored(IInteractionTrackerOwner *iface, IInteractionTracker *sender,
        IInteractionTrackerRequestIgnoredArgs *args)
{
    INT32 id = -1;
    IInteractionTrackerRequestIgnoredArgs_get_RequestId(args, &id);
    printf("    [%s] RequestIgnored id %d (%s)\n", phase, id, thread_name());
    return S_OK;
}
static HRESULT WINAPI owner_ValuesChanged(IInteractionTrackerOwner *iface, IInteractionTracker *sender,
        IInteractionTrackerValuesChangedArgs *args)
{
    INT32 id = -1;
    Vector3 v = {0};
    FLOAT scale = -1;
    IInteractionTrackerValuesChangedArgs_get_RequestId(args, &id);
    IInteractionTrackerValuesChangedArgs_get_Position(args, &v);
    IInteractionTrackerValuesChangedArgs_get_Scale(args, &scale);
    printf("    [%s] ValuesChanged id %d position %g %g %g scale %g (%s)\n", phase, id, v.X, v.Y, v.Z, scale, thread_name());
    print_class("      args", args);
    return S_OK;
}

static const IInteractionTrackerOwnerVtbl owner_vtbl =
{
    owner_QueryInterface, owner_AddRef, owner_Release,
    owner_GetIids, owner_GetRuntimeClassName, owner_GetTrustLevel,
    owner_CustomAnimationStateEntered, owner_IdleStateEntered, owner_InertiaStateEntered,
    owner_InteractingStateEntered, owner_RequestIgnored, owner_ValuesChanged,
};
static IInteractionTrackerOwner owner = {&owner_vtbl};

/*** CommitNeeded ***/

static ICompositorController *the_controller;
static LONG commit_needed_count;
static BOOL commit_on_need;
static HRESULT WINAPI need_QueryInterface(ITypedEventHandler_CompositorController_IInspectable *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_ITypedEventHandler_CompositorController_IInspectable))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI need_AddRef(ITypedEventHandler_CompositorController_IInspectable *iface) { return 2; }
static ULONG WINAPI need_Release(ITypedEventHandler_CompositorController_IInspectable *iface) { return 1; }
static HRESULT WINAPI need_Invoke(ITypedEventHandler_CompositorController_IInspectable *iface,
        ICompositorController *sender, IInspectable *args)
{
    commit_needed_count++;
    printf("    [%s] CommitNeeded args %p (%s)%s\n", phase, args, thread_name(), commit_on_need ? ", committing" : "");
    if (commit_on_need) ICompositorController_Commit(sender);
    return S_OK;
}
static const ITypedEventHandler_CompositorController_IInspectableVtbl need_vtbl =
{
    need_QueryInterface, need_AddRef, need_Release, need_Invoke,
};
static ITypedEventHandler_CompositorController_IInspectable need_handler = {&need_vtbl};

static void read_pixels(HWND hwnd, const char *what)
{
    static const POINT points[] = {{5, 5}, {15, 15}, {60, 60}, {105, 105}, {115, 115}, {200, 150}};
    HDC hdc = GetDC(hwnd), mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, 300, 200);
    int i;

    SelectObject(mem, bmp);
    printf("%s: PrintWindow %d\n", what, PrintWindow(hwnd, mem, 2 /* PW_RENDERFULLCONTENT */));
    for (i = 0; i < ARRAY_SIZE(points); i++)
        printf("  pixel (%ld,%ld) %06lx\n", points[i].x, points[i].y, (ULONG)GetPixel(mem, points[i].x, points[i].y));
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(hwnd, hdc);
    if (GetEnvironmentVariableA("PROBE_PAUSE", NULL, 0))
    {
        char buf[16];
        POINT origin = {0, 0};
        GetEnvironmentVariableA("PROBE_PAUSE", buf, sizeof(buf));
        ClientToScreen(hwnd, &origin);
        printf("  (holding %s ms, client at %ld,%ld)\n", buf, origin.x, origin.y);
        pump(atoi(buf));
    }
}

typedef struct { DWORD dwSize; int threadType; int apartmentType; } DispatcherQueueOptions_;

static void source_defaults(IVisualInteractionSource *source)
{
    IVisualInteractionSource2 *source2;
    IVisualInteractionSource3 *source3;
    VisualInteractionSourceRedirectionMode redirection = -1;
    InteractionChainingMode chaining = -1;
    InteractionSourceMode mode = -1;
    boolean b = 2;
    Vector3 v3;
    FLOAT f;
    HRESULT hr;

    IVisualInteractionSource_get_IsPositionXRailsEnabled(source, &b); printf("  IsPositionXRailsEnabled %d\n", b);
    IVisualInteractionSource_get_IsPositionYRailsEnabled(source, &b); printf("  IsPositionYRailsEnabled %d\n", b);
    hr = IVisualInteractionSource_get_ManipulationRedirectionMode(source, &redirection);
    printf("  ManipulationRedirectionMode %#lx %d\n", hr, redirection);
    IVisualInteractionSource_get_PositionXChainingMode(source, &chaining); printf("  PositionXChainingMode %d\n", chaining);
    IVisualInteractionSource_get_PositionYChainingMode(source, &chaining); printf("  PositionYChainingMode %d\n", chaining);
    IVisualInteractionSource_get_ScaleChainingMode(source, &chaining); printf("  ScaleChainingMode %d\n", chaining);
    IVisualInteractionSource_get_PositionXSourceMode(source, &mode); printf("  PositionXSourceMode %d\n", mode);
    IVisualInteractionSource_get_PositionYSourceMode(source, &mode); printf("  PositionYSourceMode %d\n", mode);
    IVisualInteractionSource_get_ScaleSourceMode(source, &mode); printf("  ScaleSourceMode %d\n", mode);
    if (SUCCEEDED(IVisualInteractionSource_QueryInterface(source, &IID_IVisualInteractionSource2, (void **)&source2)))
    {
        IVisualInteractionSource2_get_DeltaPosition(source2, &v3); print_v3("DeltaPosition", v3);
        IVisualInteractionSource2_get_DeltaScale(source2, &f); printf("  DeltaScale %g\n", f);
        IVisualInteractionSource2_get_Position(source2, &v3); print_v3("Position", v3);
        IVisualInteractionSource2_get_PositionVelocity(source2, &v3); print_v3("PositionVelocity", v3);
        IVisualInteractionSource2_get_Scale(source2, &f); printf("  Scale %g\n", f);
        IVisualInteractionSource2_get_ScaleVelocity(source2, &f); printf("  ScaleVelocity %g\n", f);
        hr = IVisualInteractionSource2_ConfigureDeltaPositionXModifiers(source2, NULL);
        printf("  ConfigureDeltaPositionXModifiers(NULL) %#lx\n", hr);
        IVisualInteractionSource2_Release(source2);
    }
    if (SUCCEEDED(IVisualInteractionSource_QueryInterface(source, &IID_IVisualInteractionSource3, (void **)&source3)))
    {
        IInteractionSourceConfiguration *config = NULL;
        hr = IVisualInteractionSource3_get_PointerWheelConfig(source3, &config);
        printf("  PointerWheelConfig %#lx\n", hr);
        print_class("  config", config);
        if (config)
        {
            InteractionSourceRedirectionMode m = -1;
            IInteractionSourceConfiguration_get_PositionXSourceMode(config, &m); printf("    PositionXSourceMode %d\n", m);
            IInteractionSourceConfiguration_get_PositionYSourceMode(config, &m); printf("    PositionYSourceMode %d\n", m);
            IInteractionSourceConfiguration_get_ScaleSourceMode(config, &m); printf("    ScaleSourceMode %d\n", m);
            QI("config", config, ICompositionObject);
            IInteractionSourceConfiguration_Release(config);
        }
        IVisualInteractionSource3_Release(source3);
    }
}

static void tracker_values(const char *what, IInteractionTracker *tracker)
{
    Vector3 v3;
    FLOAT f;
    IInteractionTracker_get_Position(tracker, &v3);
    IInteractionTracker_get_Scale(tracker, &f);
    printf("  %s: Position %g %g %g Scale %g\n", what, v3.X, v3.Y, v3.Z, f);
}

int main(void)
{
    WNDCLASSW cls = {0};
    ICompositorController *controller = NULL;
    ICompositorDesktopInterop *desktop;
    ICompositor *compositor = NULL;
    IDesktopWindowTarget *target = NULL;
    ICompositionTarget *ctarget;
    IContainerVisual *container = NULL;
    ISpriteVisual *sprite = NULL;
    ICompositionColorBrush *color = NULL;
    ICompositionBrush *brush;
    IVisual *visual, *sprite_visual;
    IVisualCollection *children;
    IInspectable *inspectable;
    IVisualInteractionSourceStatics *source_statics = NULL;
    IVisualInteractionSource *source = NULL, *source2 = NULL;
    IInteractionTrackerStatics *tracker_statics = NULL;
    IInteractionTracker *tracker = NULL, *owned = NULL;
    ICompositionInteractionSourceCollection *sources = NULL;
    ICompositionInteractionSource *isource;
    IInteractionTracker4 *tracker4;
    __FIReference_1_Vector3 *ref3 = (void *)0xdeadbeef;
    __FIReference_1_FLOAT *reff = (void *)0xdeadbeef;
    IInteractionTrackerOwner *got_owner = (void *)0xdeadbeef;
    Vector2 v2;
    Vector3 v3;
    FLOAT f;
    boolean b;
    INT32 id, count;
    Color c;
    HSTRING str;
    HWND hwnd;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    main_tid = GetCurrentThreadId();
    RoInitialize(RO_INIT_SINGLETHREADED);
    {
        HRESULT (WINAPI *create)(DispatcherQueueOptions_, IUnknown **) =
            (void *)GetProcAddress(LoadLibraryW(L"coremessaging.dll"), "CreateDispatcherQueueController");
        DispatcherQueueOptions_ options = {sizeof(options), 2 /* DQTYPE_THREAD_CURRENT */, 2 /* DQTAT_COM_STA */};
        IUnknown *queue = NULL;
        hr = create(options, &queue);
        printf("CreateDispatcherQueueController %#lx\n", hr);
    }
    str = S(L"Windows.UI.Composition.Core.CompositorController");
    hr = RoActivateInstance(str, &inspectable);
    WindowsDeleteString(str);
    printf("activate CompositorController %#lx\n", hr);
    if (FAILED(hr)) return 1;
    IInspectable_QueryInterface(inspectable, &IID_ICompositorController, (void **)&controller);
    IInspectable_Release(inspectable);
    ICompositorController_get_Compositor(controller, &compositor);
    the_controller = controller;
    {
        EventRegistrationToken token;
        phase = "setup";
        hr = ICompositorController_add_CommitNeeded(controller, &need_handler, &token);
        printf("add_CommitNeeded %#lx\n", hr);
    }

    cls.lpfnWndProc = wndproc;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.lpszClassName = L"probe_interact";
    cls.hbrBackground = GetStockObject(WHITE_BRUSH);
    RegisterClassW(&cls);
    hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, L"probe_interact", L"probe_interact", WS_POPUP | WS_VISIBLE,
                           100, 100, 300, 200, NULL, NULL, NULL, NULL);
    pump(200);
    ICompositor_QueryInterface(compositor, &IID_ICompositorDesktopInterop, (void **)&desktop);
    ICompositorDesktopInterop_CreateDesktopWindowTarget(desktop, hwnd, FALSE, &target);

    ICompositor_CreateContainerVisual(compositor, &container);
    IContainerVisual_QueryInterface(container, &IID_IVisual, (void **)&visual);
    v2.X = 300; v2.Y = 200;
    IVisual_put_Size(visual, v2);
    ICompositor_CreateSpriteVisual(compositor, &sprite);
    ISpriteVisual_QueryInterface(sprite, &IID_IVisual, (void **)&sprite_visual);
    v2.X = 100; v2.Y = 100;
    IVisual_put_Size(sprite_visual, v2);
    v3.X = 10; v3.Y = 10; v3.Z = 0;
    IVisual_put_Offset(sprite_visual, v3);
    c.A = 255; c.R = 255; c.G = 0; c.B = 0;
    ICompositor_CreateColorBrushWithColor(compositor, c, &color);
    ICompositionColorBrush_QueryInterface(color, &IID_ICompositionBrush, (void **)&brush);
    ISpriteVisual_put_Brush(sprite, brush);
    IContainerVisual_get_Children(container, &children);
    IVisualCollection_InsertAtTop(children, sprite_visual);
    IDesktopWindowTarget_QueryInterface(target, &IID_ICompositionTarget, (void **)&ctarget);
    ICompositionTarget_put_Root(ctarget, visual);
    ICompositorController_Commit(controller);
    pump(200);

    /* VisualInteractionSource */
    str = S(L"Windows.UI.Composition.Interactions.VisualInteractionSource");
    hr = RoGetActivationFactory(str, &IID_IVisualInteractionSourceStatics, (void **)&source_statics);
    WindowsDeleteString(str);
    printf("VisualInteractionSource statics %#lx\n", hr);
    if (source_statics)
    {
        QI("statics", source_statics, IActivationFactory);
        QI("statics", source_statics, IVisualInteractionSourceStatics2);
        hr = IVisualInteractionSourceStatics_Create(source_statics, NULL, &source);
        printf("Create(NULL) %#lx %p\n", hr, source);
        source = NULL;
        hr = IVisualInteractionSourceStatics_Create(source_statics, visual, &source);
        printf("Create(container) %#lx\n", hr);
        print_class("source", source);
        hr = IVisualInteractionSourceStatics_Create(source_statics, visual, &source2);
        printf("Create(container) again %#lx same %d\n", hr, source == source2);
        if (source2) IVisualInteractionSource_Release(source2);
    }
    if (source)
    {
        IVisual *got = NULL;
        QI("source", source, ICompositionInteractionSource);
        QI("source", source, ICompositionObject);
        QI("source", source, IVisualInteractionSource2);
        QI("source", source, IVisualInteractionSource3);
        QI("source", source, IVisualInteractionSourceInterop);
        QI("source", source, IAgileObject);
        IVisualInteractionSource_get_Source(source, &got);
        printf("  Source same %d\n", got == visual);
        if (got) IVisual_Release(got);
        source_defaults(source);
        hr = IVisualInteractionSource_TryRedirectForManipulation(source, NULL);
        printf("  TryRedirectForManipulation(NULL) %#lx\n", hr);
        hr = IVisualInteractionSource_put_PositionYSourceMode(source, InteractionSourceMode_EnabledWithInertia);
        printf("  put_PositionYSourceMode %#lx\n", hr);
        hr = IVisualInteractionSource_put_ManipulationRedirectionMode(source, VisualInteractionSourceRedirectionMode_CapableTouchpadOnly);
        printf("  put_ManipulationRedirectionMode %#lx\n", hr);
    }

    /* InteractionTracker */
    str = S(L"Windows.UI.Composition.Interactions.InteractionTracker");
    hr = RoGetActivationFactory(str, &IID_IInteractionTrackerStatics, (void **)&tracker_statics);
    WindowsDeleteString(str);
    printf("InteractionTracker statics %#lx\n", hr);
    if (tracker_statics)
    {
        QI("statics", tracker_statics, IInteractionTrackerStatics2);
        hr = IInteractionTrackerStatics_Create(tracker_statics, NULL, &tracker);
        printf("Create(NULL) %#lx\n", hr);
        tracker = NULL;
        hr = IInteractionTrackerStatics_Create(tracker_statics, compositor, &tracker);
        printf("Create %#lx\n", hr);
        print_class("tracker", tracker);
    }
    if (tracker)
    {
        QI("tracker", tracker, IInteractionTracker2);
        QI("tracker", tracker, IInteractionTracker3);
        QI("tracker", tracker, IInteractionTracker4);
        QI("tracker", tracker, IInteractionTracker5);
        QI("tracker", tracker, ICompositionObject);
        QI("tracker", tracker, ICompositionInteractionSource);
        IInteractionTracker_get_Position(tracker, &v3); print_v3("Position", v3);
        IInteractionTracker_get_Scale(tracker, &f); printf("  Scale %g\n", f);
        IInteractionTracker_get_MinPosition(tracker, &v3); print_v3("MinPosition", v3);
        IInteractionTracker_get_MaxPosition(tracker, &v3); print_v3("MaxPosition", v3);
        IInteractionTracker_get_MinScale(tracker, &f); printf("  MinScale %g\n", f);
        IInteractionTracker_get_MaxScale(tracker, &f); printf("  MaxScale %g\n", f);
        IInteractionTracker_get_NaturalRestingPosition(tracker, &v3); print_v3("NaturalRestingPosition", v3);
        IInteractionTracker_get_NaturalRestingScale(tracker, &f); printf("  NaturalRestingScale %g\n", f);
        IInteractionTracker_get_PositionVelocityInPixelsPerSecond(tracker, &v3); print_v3("PositionVelocity", v3);
        IInteractionTracker_get_ScaleVelocityInPercentPerSecond(tracker, &f); printf("  ScaleVelocity %g\n", f);
        IInteractionTracker_get_IsPositionRoundingSuggested(tracker, &b); printf("  IsPositionRoundingSuggested %d\n", b);
        hr = IInteractionTracker_get_PositionInertiaDecayRate(tracker, &ref3); printf("  PositionInertiaDecayRate %#lx %p\n", hr, ref3);
        hr = IInteractionTracker_get_ScaleInertiaDecayRate(tracker, &reff); printf("  ScaleInertiaDecayRate %#lx %p\n", hr, reff);
        hr = IInteractionTracker_get_Owner(tracker, &got_owner); printf("  Owner %#lx %p\n", hr, got_owner);
        hr = IInteractionTracker_get_InteractionSources(tracker, &sources);
        printf("  InteractionSources %#lx\n", hr);
        print_class("  sources", sources);
        if (sources)
        {
            count = -1;
            ICompositionInteractionSourceCollection_get_Count(sources, &count); printf("  Count %d\n", count);
            if (source)
            {
                IVisualInteractionSource_QueryInterface(source, &IID_ICompositionInteractionSource, (void **)&isource);
                hr = ICompositionInteractionSourceCollection_Add(sources, isource); printf("  Add %#lx\n", hr);
                hr = ICompositionInteractionSourceCollection_Add(sources, isource); printf("  Add again %#lx\n", hr);
                ICompositionInteractionSourceCollection_get_Count(sources, &count); printf("  Count %d\n", count);
                hr = ICompositionInteractionSourceCollection_Remove(sources, isource); printf("  Remove %#lx\n", hr);
                ICompositionInteractionSourceCollection_get_Count(sources, &count); printf("  Count %d\n", count);
                hr = ICompositionInteractionSourceCollection_Remove(sources, isource); printf("  Remove again %#lx\n", hr);
                hr = ICompositionInteractionSourceCollection_Add(sources, isource); printf("  Add %#lx\n", hr);
                hr = ICompositionInteractionSourceCollection_Add(sources, NULL); printf("  Add(NULL) %#lx\n", hr);
                ICompositionInteractionSource_Release(isource);
            }
        }
        phase = "no owner";
        hr = IInteractionTracker_TryUpdatePosition(tracker, (Vector3){30, 20, 0}, &id);
        printf("  TryUpdatePosition(30,20) %#lx id %d\n", hr, id);
        tracker_values("right after", tracker);
        pump(300);
        tracker_values("after 300ms", tracker);
    }

    if (tracker_statics)
    {
        phase = "created";
        hr = IInteractionTrackerStatics_CreateWithOwner(tracker_statics, compositor, &owner, &owned);
        printf("CreateWithOwner %#lx owner refs %ld\n", hr, owner_refs);
        pump(300);
        printf("  (pumped 300ms)\n");
        phase = "commit";
        ICompositorController_Commit(controller);
        pump(300);
        printf("  (committed, pumped 300ms)\n");
    }
    if (owned)
    {
        v3.X = 200; v3.Y = 200; v3.Z = 0;
        hr = IInteractionTracker_put_MaxPosition(owned, v3);
        printf("  put_MaxPosition %#lx\n", hr);
        tracker_values("after MaxPosition", owned);
        phase = "update";
        hr = IInteractionTracker_TryUpdatePosition(owned, (Vector3){100, 50, 0}, &id);
        printf("  TryUpdatePosition(100,50) %#lx id %d\n", hr, id);
        tracker_values("right after", owned);
        pump(300);
        tracker_values("after 300ms", owned);
        phase = "update-commit";
        ICompositorController_Commit(controller);
        pump(300);
        tracker_values("after commit", owned);
        phase = "clamp";
        hr = IInteractionTracker_TryUpdatePosition(owned, (Vector3){500, -10, 0}, &id);
        printf("  TryUpdatePosition(500,-10) %#lx id %d\n", hr, id);
        ICompositorController_Commit(controller);
        pump(300);
        tracker_values("after commit", owned);
        phase = "by";
        hr = IInteractionTracker_TryUpdatePositionBy(owned, (Vector3){-50, 30, 0}, &id);
        printf("  TryUpdatePositionBy(-50,30) %#lx id %d\n", hr, id);
        ICompositorController_Commit(controller);
        pump(300);
        tracker_values("after commit", owned);
        phase = "twice";
        hr = IInteractionTracker_TryUpdatePosition(owned, (Vector3){10, 10, 0}, &id);
        printf("  TryUpdatePosition(10,10) %#lx id %d\n", hr, id);
        hr = IInteractionTracker_TryUpdatePosition(owned, (Vector3){20, 20, 0}, &id);
        printf("  TryUpdatePosition(20,20) %#lx id %d\n", hr, id);
        ICompositorController_Commit(controller);
        pump(300);
        tracker_values("after commit", owned);
        phase = "scale";
        hr = IInteractionTracker_TryUpdateScale(owned, 2.0f, (Vector3){0, 0, 0}, &id);
        printf("  TryUpdateScale(2) %#lx id %d\n", hr, id);
        ICompositorController_Commit(controller);
        pump(300);
        tracker_values("after commit", owned);
        if (SUCCEEDED(IInteractionTracker_QueryInterface(owned, &IID_IInteractionTracker4, (void **)&tracker4)))
        {
            phase = "unclamped";
            hr = IInteractionTracker4_TryUpdatePositionWithOption(tracker4, (Vector3){500, -10, 0},
                                                                  InteractionTrackerClampingOption_Disabled, &id);
            printf("  TryUpdatePositionWithOption(500,-10, disabled) %#lx id %d\n", hr, id);
            ICompositorController_Commit(controller);
            pump(300);
            tracker_values("after commit", owned);
            IInteractionTracker4_Release(tracker4);
        }
        phase = "max";
        v3.X = 50; v3.Y = 50; v3.Z = 0;
        hr = IInteractionTracker_put_MaxPosition(owned, v3);
        ICompositorController_Commit(controller);
        pump(300);
        tracker_values("after MaxPosition 50 and commit", owned);
    }

    /* an expression moving the sprite by the tracker's position */
    if (owned)
    {
        IExpressionAnimation *expression = NULL;
        ICompositionAnimation *animation;
        ICompositionObject *object, *tracker_object;
        HSTRING key, prop;

        phase = "expression";
        hr = IInteractionTracker_TryUpdatePosition(owned, (Vector3){40, 30, 0}, &id);
        ICompositorController_Commit(controller);
        pump(300);
        tracker_values("before expression", owned);
        str = S(L"-tracker.Position");
        hr = ICompositor_CreateExpressionAnimationWithExpression(compositor, str, &expression);
        WindowsDeleteString(str);
        printf("CreateExpressionAnimationWithExpression %#lx\n", hr);
        print_class("expression", expression);
        if (expression)
        {
            IExpressionAnimation_QueryInterface(expression, &IID_ICompositionAnimation, (void **)&animation);
            IInteractionTracker_QueryInterface(owned, &IID_ICompositionObject, (void **)&tracker_object);
            key = S(L"tracker");
            hr = ICompositionAnimation_SetReferenceParameter(animation, key, tracker_object);
            printf("  SetReferenceParameter %#lx\n", hr);
            WindowsDeleteString(key);
            IContainerVisual_QueryInterface(container, &IID_ICompositionObject, (void **)&object);
            prop = S(L"Offset");
            hr = ICompositionObject_StartAnimation(object, prop, animation);
            printf("  StartAnimation(container Offset) %#lx\n", hr);
            WindowsDeleteString(prop);
            ICompositorController_Commit(controller);
            pump(300);
            IVisual_get_Offset(visual, &v3);
            print_v3("container Offset", v3);
            read_pixels(hwnd, "expression");
            phase = "expression-move";
            hr = IInteractionTracker_TryUpdatePosition(owned, (Vector3){0, 0, 0}, &id);
            ICompositorController_Commit(controller);
            pump(300);
            read_pixels(hwnd, "tracker back at 0");
            ICompositionObject_Release(object);
            ICompositionObject_Release(tracker_object);
            ICompositionAnimation_Release(animation);
        }
    }

    /* the controller committing whenever it says it needs to */
    if (owned)
    {
        phase = "commit-on-need";
        commit_on_need = TRUE;
        hr = IInteractionTracker_TryUpdatePosition(owned, (Vector3){25, 15, 0}, &id);
        printf("  TryUpdatePosition(25,15) %#lx id %d\n", hr, id);
        pump(1000);
        tracker_values("after 1s", owned);
        commit_on_need = FALSE;
    }

    /* a compositor that commits by itself */
    {
        ICompositor *plain = NULL;
        IInteractionTracker *auto_tracker = NULL;

        str = S(L"Windows.UI.Composition.Compositor");
        hr = RoActivateInstance(str, &inspectable);
        WindowsDeleteString(str);
        printf("activate Compositor %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            IInspectable_QueryInterface(inspectable, &IID_ICompositor, (void **)&plain);
            IInspectable_Release(inspectable);
            phase = "auto-created";
            hr = IInteractionTrackerStatics_CreateWithOwner(tracker_statics, plain, &owner, &auto_tracker);
            printf("CreateWithOwner(plain) %#lx\n", hr);
            pump(500);
            printf("  (pumped 500ms)\n");
        }
        if (auto_tracker)
        {
            v3.X = 200; v3.Y = 200; v3.Z = 0;
            IInteractionTracker_put_MaxPosition(auto_tracker, v3);
            phase = "auto-update";
            hr = IInteractionTracker_TryUpdatePosition(auto_tracker, (Vector3){100, 50, 0}, &id);
            printf("  TryUpdatePosition(100,50) %#lx id %d\n", hr, id);
            tracker_values("right after", auto_tracker);
            pump(500);
            tracker_values("after 500ms", auto_tracker);
            phase = "auto-clamp";
            hr = IInteractionTracker_TryUpdatePosition(auto_tracker, (Vector3){500, -10, 0}, &id);
            printf("  TryUpdatePosition(500,-10) %#lx id %d\n", hr, id);
            pump(500);
            tracker_values("after 500ms", auto_tracker);
            phase = "auto-twice";
            hr = IInteractionTracker_TryUpdatePosition(auto_tracker, (Vector3){10, 10, 0}, &id);
            printf("  TryUpdatePosition(10,10) %#lx id %d\n", hr, id);
            hr = IInteractionTracker_TryUpdatePositionBy(auto_tracker, (Vector3){5, 5, 0}, &id);
            printf("  TryUpdatePositionBy(5,5) %#lx id %d\n", hr, id);
            pump(500);
            tracker_values("after 500ms", auto_tracker);
            phase = "auto-scale";
            IInteractionTracker_put_MaxScale(auto_tracker, 4.0f);
            hr = IInteractionTracker_TryUpdateScale(auto_tracker, 2.0f, (Vector3){0, 0, 0}, &id);
            printf("  TryUpdateScale(2) %#lx id %d\n", hr, id);
            pump(500);
            tracker_values("after 500ms", auto_tracker);
            phase = "auto-max";
            v3.X = 5; v3.Y = 5; v3.Z = 0;
            IInteractionTracker_put_MaxPosition(auto_tracker, v3);
            pump(500);
            tracker_values("after MaxPosition 5", auto_tracker);
            IInteractionTracker_Release(auto_tracker);
        }
        if (plain) ICompositor_Release(plain);
    }
    /* everything AirSpace sets up: an owner, a source on a visual of the tree, an expression using the tracker */
    {
        IInteractionTracker *full = NULL;
        IVisualInteractionSource *full_source = NULL;
        ISpriteVisual *sprite2 = NULL;
        IVisual *sprite2_visual;
        IExpressionAnimation *expression = NULL;
        ICompositionAnimation *animation;
        ICompositionObject *object, *tracker_object;
        IAsyncAction *action = NULL;
        HSTRING key, prop;

        commit_on_need = TRUE;
        ICompositor_CreateSpriteVisual(compositor, &sprite2);
        ISpriteVisual_QueryInterface(sprite2, &IID_IVisual, (void **)&sprite2_visual);
        v2.X = 50; v2.Y = 50;
        IVisual_put_Size(sprite2_visual, v2);
        ISpriteVisual_put_Brush(sprite2, brush);
        IVisualCollection_InsertAtTop(children, sprite2_visual);
        hr = IVisualInteractionSourceStatics_Create(source_statics, sprite2_visual, &full_source);
        printf("full: source %#lx\n", hr);
        IVisualInteractionSource_put_PositionYSourceMode(full_source, InteractionSourceMode_EnabledWithInertia);
        IVisualInteractionSource_put_PositionXSourceMode(full_source, InteractionSourceMode_EnabledWithInertia);
        phase = "full-created";
        hr = IInteractionTrackerStatics_CreateWithOwner(tracker_statics, compositor, &owner, &full);
        printf("full: tracker %#lx\n", hr);
        IInteractionTracker_get_InteractionSources(full, &sources);
        IVisualInteractionSource_QueryInterface(full_source, &IID_ICompositionInteractionSource, (void **)&isource);
        hr = ICompositionInteractionSourceCollection_Add(sources, isource);
        printf("full: Add source %#lx\n", hr);
        v3.X = 200; v3.Y = 200; v3.Z = 0;
        IInteractionTracker_put_MaxPosition(full, v3);
        str = S(L"-tracker.Position");
        ICompositor_CreateExpressionAnimationWithExpression(compositor, str, &expression);
        WindowsDeleteString(str);
        IExpressionAnimation_QueryInterface(expression, &IID_ICompositionAnimation, (void **)&animation);
        IInteractionTracker_QueryInterface(full, &IID_ICompositionObject, (void **)&tracker_object);
        key = S(L"tracker");
        ICompositionAnimation_SetReferenceParameter(animation, key, tracker_object);
        WindowsDeleteString(key);
        ISpriteVisual_QueryInterface(sprite2, &IID_ICompositionObject, (void **)&object);
        prop = S(L"Offset");
        hr = ICompositionObject_StartAnimation(object, prop, animation);
        printf("full: StartAnimation %#lx\n", hr);
        WindowsDeleteString(prop);
        ICompositorController_Commit(controller);
        pump(1000);
        printf("  (committed, pumped 1s)\n");
        phase = "full-update";
        hr = IInteractionTracker_TryUpdatePosition(full, (Vector3){100, 50, 0}, &id);
        printf("full: TryUpdatePosition(100,50) %#lx id %d\n", hr, id);
        ICompositorController_Commit(controller);
        hr = ICompositorController_EnsurePreviousCommitCompletedAsync(controller, &action);
        printf("full: EnsurePreviousCommitCompletedAsync %#lx\n", hr);
        if (action)
        {
            IAsyncInfo *info;
            AsyncStatus status = -1;
            IAsyncAction_QueryInterface(action, &IID_IAsyncInfo, (void **)&info);
            IAsyncInfo_get_Status(info, &status);
            printf("  status right away %d\n", status);
            pump(200);
            IAsyncInfo_get_Status(info, &status);
            printf("  status after 200ms %d\n", status);
            IAsyncInfo_Release(info);
            IAsyncAction_Release(action);
        }
        pump(1000);
        tracker_values("full after 1s", full);
        read_pixels(hwnd, "full");
        phase = "full-by";
        hr = IInteractionTracker_TryUpdatePositionBy(full, (Vector3){-50, 100, 0}, &id);
        printf("full: TryUpdatePositionBy(-50,100) %#lx id %d\n", hr, id);
        ICompositorController_Commit(controller);
        pump(1000);
        tracker_values("full after 1s", full);
        commit_on_need = FALSE;
    }
    printf("CommitNeeded raised %ld times\n", commit_needed_count);

    printf("done\n");
    return 0;
}
