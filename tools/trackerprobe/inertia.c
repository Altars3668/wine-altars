/* inertia: how an InteractionTracker coasts, frame by frame -- where velocity of different sizes takes it and how
 * fast, with decay rates set, with a bound in the way, with more velocity added while it coasts, back into bounds
 * after a custom animation took it past them, and in scale.  Prints the state callbacks with all that
 * InertiaStateEntered reports, and every value change with the milliseconds since the request.  Needs a desktop
 * session that draws frames (scripts/windesktop-launch.ps1). */
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

/*** what the owner hears, with the time it heard it ***/

enum kind { VALUES, IDLE, CUSTOM, INERTIA, INTERACTING, IGNORED };

struct event
{
    enum kind kind;
    INT32 id;
    double ms;
    Vector3 position;
    float scale;
    /* inertia */
    Vector3 natural, velocity, modified;
    float natural_scale, scale_velocity, modified_scale;
    HRESULT modified_hr, modified_scale_hr;
    BOOL has_modified, has_modified_scale;
    boolean from_impulse;
};

static struct event events[2048];
static unsigned int event_count;
static LARGE_INTEGER t0, freq;

static struct event *add(enum kind kind, INT32 id)
{
    LARGE_INTEGER now;
    struct event *e;

    if (event_count == ARRAYSIZE(events)) return NULL;
    QueryPerformanceCounter(&now);
    e = &events[event_count++];
    memset(e, 0, sizeof(*e));
    e->kind = kind;
    e->id = id;
    e->ms = (now.QuadPart - t0.QuadPart) * 1000.0 / freq.QuadPart;
    return e;
}

static HRESULT WINAPI owner_QueryInterface(IInteractionTrackerOwner *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IInteractionTrackerOwner) || IsEqualGUID(iid, &IID_IAgileObject))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI owner_AddRef(IInteractionTrackerOwner *iface) { return 2; }
static ULONG WINAPI owner_Release(IInteractionTrackerOwner *iface) { return 1; }
static HRESULT WINAPI owner_GetIids(IInteractionTrackerOwner *iface, ULONG *count, IID **iids) { return E_NOTIMPL; }
static HRESULT WINAPI owner_GetRuntimeClassName(IInteractionTrackerOwner *iface, HSTRING *name) { return E_NOTIMPL; }
static HRESULT WINAPI owner_GetTrustLevel(IInteractionTrackerOwner *iface, TrustLevel *level) { return E_NOTIMPL; }

static HRESULT WINAPI owner_CustomAnimationStateEntered(IInteractionTrackerOwner *iface, IInteractionTracker *sender,
        IInteractionTrackerCustomAnimationStateEnteredArgs *args)
{
    INT32 id = -1;
    IInteractionTrackerCustomAnimationStateEnteredArgs_get_RequestId(args, &id);
    add(CUSTOM, id);
    return S_OK;
}
static HRESULT WINAPI owner_IdleStateEntered(IInteractionTrackerOwner *iface, IInteractionTracker *sender,
        IInteractionTrackerIdleStateEnteredArgs *args)
{
    INT32 id = -1;
    IInteractionTrackerIdleStateEnteredArgs_get_RequestId(args, &id);
    add(IDLE, id);
    return S_OK;
}
static HRESULT WINAPI owner_InertiaStateEntered(IInteractionTrackerOwner *iface, IInteractionTracker *sender,
        IInteractionTrackerInertiaStateEnteredArgs *args)
{
    IInteractionTrackerInertiaStateEnteredArgs2 *args2;
    IReference_Vector3 *modified = NULL;
    IReference_FLOAT *modified_scale = NULL;
    struct event *e;
    INT32 id = -1;

    IInteractionTrackerInertiaStateEnteredArgs_get_RequestId(args, &id);
    if (!(e = add(INERTIA, id))) return S_OK;
    IInteractionTrackerInertiaStateEnteredArgs_get_NaturalRestingPosition(args, &e->natural);
    IInteractionTrackerInertiaStateEnteredArgs_get_NaturalRestingScale(args, &e->natural_scale);
    IInteractionTrackerInertiaStateEnteredArgs_get_PositionVelocityInPixelsPerSecond(args, &e->velocity);
    IInteractionTrackerInertiaStateEnteredArgs_get_ScaleVelocityInPercentPerSecond(args, &e->scale_velocity);
    IInteractionTrackerInertiaStateEnteredArgs_get_ModifiedRestingPosition(args, &modified);
    if ((e->has_modified = !!modified))
    {
        e->modified_hr = IReference_Vector3_get_Value(modified, &e->modified);
        IReference_Vector3_Release(modified);
    }
    IInteractionTrackerInertiaStateEnteredArgs_get_ModifiedRestingScale(args, &modified_scale);
    if ((e->has_modified_scale = !!modified_scale))
    {
        e->modified_scale_hr = IReference_FLOAT_get_Value(modified_scale, &e->modified_scale);
        IReference_FLOAT_Release(modified_scale);
    }
    if (SUCCEEDED(IInteractionTrackerInertiaStateEnteredArgs_QueryInterface(args,
            &IID_IInteractionTrackerInertiaStateEnteredArgs2, (void **)&args2)))
    {
        IInteractionTrackerInertiaStateEnteredArgs2_get_IsInertiaFromImpulse(args2, &e->from_impulse);
        IInteractionTrackerInertiaStateEnteredArgs2_Release(args2);
    }
    return S_OK;
}
static HRESULT WINAPI owner_InteractingStateEntered(IInteractionTrackerOwner *iface, IInteractionTracker *sender,
        IInteractionTrackerInteractingStateEnteredArgs *args)
{
    INT32 id = -1;
    IInteractionTrackerInteractingStateEnteredArgs_get_RequestId(args, &id);
    add(INTERACTING, id);
    return S_OK;
}
static HRESULT WINAPI owner_RequestIgnored(IInteractionTrackerOwner *iface, IInteractionTracker *sender,
        IInteractionTrackerRequestIgnoredArgs *args)
{
    INT32 id = -1;
    IInteractionTrackerRequestIgnoredArgs_get_RequestId(args, &id);
    add(IGNORED, id);
    return S_OK;
}
static HRESULT WINAPI owner_ValuesChanged(IInteractionTrackerOwner *iface, IInteractionTracker *sender,
        IInteractionTrackerValuesChangedArgs *args)
{
    struct event *e;
    INT32 id = -1;

    IInteractionTrackerValuesChangedArgs_get_RequestId(args, &id);
    if (!(e = add(VALUES, id))) return S_OK;
    IInteractionTrackerValuesChangedArgs_get_Position(args, &e->position);
    IInteractionTrackerValuesChangedArgs_get_Scale(args, &e->scale);
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

/*** a boxed Vector3, for the decay rates ***/

struct vector3_ref
{
    IReference_Vector3 IReference_Vector3_iface;
    Vector3 value;
};

static HRESULT WINAPI ref_QueryInterface(IReference_Vector3 *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) ||
        IsEqualGUID(iid, &IID_IReference_Vector3) || IsEqualGUID(iid, &IID_IAgileObject))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI ref_AddRef(IReference_Vector3 *iface) { return 2; }
static ULONG WINAPI ref_Release(IReference_Vector3 *iface) { return 1; }
static HRESULT WINAPI ref_GetIids(IReference_Vector3 *iface, ULONG *count, IID **iids) { return E_NOTIMPL; }
static HRESULT WINAPI ref_GetRuntimeClassName(IReference_Vector3 *iface, HSTRING *name) { return E_NOTIMPL; }
static HRESULT WINAPI ref_GetTrustLevel(IReference_Vector3 *iface, TrustLevel *level) { return E_NOTIMPL; }
static HRESULT WINAPI ref_get_Value(IReference_Vector3 *iface, Vector3 *value)
{
    *value = CONTAINING_RECORD(iface, struct vector3_ref, IReference_Vector3_iface)->value;
    return S_OK;
}

static const IReference_Vector3Vtbl ref_vtbl =
{
    ref_QueryInterface, ref_AddRef, ref_Release, ref_GetIids, ref_GetRuntimeClassName, ref_GetTrustLevel, ref_get_Value,
};

/*** the cases ***/

typedef struct { DWORD dwSize; int threadType; int apartmentType; } DispatcherQueueOptions_;

static ICompositor *compositor;
static IInteractionTracker *tracker;
static IInteractionTracker5 *tracker5;

static void pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG msg;
    while ((LONG)(end - GetTickCount()) > 0)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        Sleep(1);
    }
}

/* pumps until the request's idle state is entered, or the time is up */
static BOOL wait_idle(INT32 id, DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    unsigned int i;
    MSG msg;

    while ((LONG)(end - GetTickCount()) > 0)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        for (i = 0; i < event_count; i++) if (events[i].kind == IDLE && events[i].id >= id) return TRUE;
        Sleep(1);
    }
    return FALSE;
}

static void begin(void)
{
    event_count = 0;
    QueryPerformanceCounter(&t0);
}

/* prints what the owner heard: the states, and the values between them, several to a line */
static void print_events(BOOL show_scale)
{
    static const char *names[] = {"values", "idle", "custom animation", "inertia", "interacting", "ignored"};
    unsigned int i, column = 0;

    for (i = 0; i < event_count; i++)
    {
        const struct event *e = &events[i];

        if (e->kind == VALUES)
        {
            if (!column) printf("   ");
            if (show_scale) printf(" %.1f:%g@%g", e->ms, e->scale, e->position.Y);
            else printf(" %.1f:%g", e->ms, e->position.Y);
            if (++column == 6)
            {
                printf("\n");
                column = 0;
            }
            continue;
        }
        if (column) printf("\n");
        column = 0;
        printf("  %.1f ms: %s for request %d\n", e->ms, names[e->kind], e->id);
        if (e->kind != INERTIA) continue;
        printf("    natural %g %g %g scale %g, velocity %g %g %g scale %g, from impulse %d\n", e->natural.X,
               e->natural.Y, e->natural.Z, e->natural_scale, e->velocity.X, e->velocity.Y, e->velocity.Z,
               e->scale_velocity, e->from_impulse);
        if (e->has_modified)
            printf("    modified position %#lx: %g %g %g\n", e->modified_hr, e->modified.X, e->modified.Y, e->modified.Z);
        else printf("    no modified position\n");
        if (e->has_modified_scale) printf("    modified scale %#lx: %g\n", e->modified_scale_hr, e->modified_scale);
        else printf("    no modified scale\n");
    }
    if (column) printf("\n");
}

static void settle_at(Vector3 position)
{
    INT32 id;

    IInteractionTracker_TryUpdatePosition(tracker, position, &id);
    pump(300);
}

static void velocity_case(const char *what, Vector3 from, Vector3 velocity, DWORD ms)
{
    INT32 id = -1;
    Vector3 at;
    HRESULT hr;

    settle_at(from);
    begin();
    hr = IInteractionTracker_TryUpdatePositionWithAdditionalVelocity(tracker, velocity, &id);
    printf("%s: from %g, velocity %g: %#lx id %d\n", what, from.Y, velocity.Y, hr, id);
    if (!wait_idle(id, ms)) printf("  not idle after %lu ms\n", ms);
    print_events(FALSE);
    IInteractionTracker_get_Position(tracker, &at);
    printf("  at %g\n", at.Y);
}

static void set_decay(Vector3 *rates)
{
    static struct vector3_ref ref = {{&ref_vtbl}};
    HRESULT hr;

    if (rates) ref.value = *rates;
    hr = IInteractionTracker_put_PositionInertiaDecayRate(tracker, rates ? &ref.IReference_Vector3_iface : NULL);
    if (FAILED(hr)) printf("put_PositionInertiaDecayRate %#lx\n", hr);
}

static ICompositionAnimation *key_frames(Vector3 value, INT64 duration)
{
    IVector3KeyFrameAnimation *kfa;
    ICompositionAnimation *animation;
    IKeyFrameAnimation *kf;
    TimeSpan span = {duration};

    ICompositor_CreateVector3KeyFrameAnimation(compositor, &kfa);
    IVector3KeyFrameAnimation_InsertKeyFrame(kfa, 1.0f, value);
    IVector3KeyFrameAnimation_QueryInterface(kfa, &IID_IKeyFrameAnimation, (void **)&kf);
    IKeyFrameAnimation_put_Duration(kf, span);
    IKeyFrameAnimation_Release(kf);
    IVector3KeyFrameAnimation_QueryInterface(kfa, &IID_ICompositionAnimation, (void **)&animation);
    IVector3KeyFrameAnimation_Release(kfa);
    return animation;
}

static void animation_case(const char *what, Vector3 from, Vector3 to, INT64 duration)
{
    ICompositionAnimation *animation = key_frames(to, duration);
    INT32 id = -1;
    Vector3 at;
    HRESULT hr;

    settle_at(from);
    begin();
    hr = IInteractionTracker_TryUpdatePositionWithAnimation(tracker, animation, &id);
    printf("%s: from %g, animated to %g in %lld ms: %#lx id %d\n", what, from.Y, to.Y, duration / 10000, hr, id);
    ICompositionAnimation_Release(animation);
    if (!wait_idle(id, 5000)) printf("  not idle after 5 s\n");
    print_events(FALSE);
    IInteractionTracker_get_Position(tracker, &at);
    printf("  at %g\n", at.Y);
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

int main(void)
{
    IInteractionTrackerStatics *statics = NULL;
    ICompositorDesktopInterop *interop;
    IDesktopWindowTarget *desktop_target;
    ICompositionTarget *target;
    IContainerVisual *root;
    IInspectable *inspectable;
    IVisual *visual;
    WNDCLASSW cls = {0};
    Vector3 rates, at;
    float scale;
    INT32 id;
    HWND hwnd;
    HSTRING str;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    QueryPerformanceFrequency(&freq);
    RoInitialize(RO_INIT_SINGLETHREADED);
    {
        HRESULT (WINAPI *create)(DispatcherQueueOptions_, IUnknown **) =
            (void *)GetProcAddress(LoadLibraryW(L"coremessaging.dll"), "CreateDispatcherQueueController");
        DispatcherQueueOptions_ options = {sizeof(options), 2 /* DQTYPE_THREAD_CURRENT */, 2 /* DQTAT_COM_STA */};
        IUnknown *queue = NULL;
        hr = create(options, &queue);
        printf("CreateDispatcherQueueController %#lx\n", hr);
    }
    str = S(L"Windows.UI.Composition.Compositor");
    hr = RoActivateInstance(str, &inspectable);
    WindowsDeleteString(str);
    printf("activate Compositor %#lx\n", hr);
    if (FAILED(hr)) return 1;
    IInspectable_QueryInterface(inspectable, &IID_ICompositor, (void **)&compositor);
    IInspectable_QueryInterface(inspectable, &IID_ICompositorDesktopInterop, (void **)&interop);
    IInspectable_Release(inspectable);

    /* a window for the compositor to draw into, so that frames come */
    cls.lpfnWndProc = window_proc;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.lpszClassName = L"trackerinertia";
    RegisterClassW(&cls);
    hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, L"trackerinertia", L"trackerinertia",
                           WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 300, 300, NULL, NULL, cls.hInstance, NULL);
    hr = ICompositorDesktopInterop_CreateDesktopWindowTarget(interop, hwnd, FALSE, &desktop_target);
    printf("CreateDesktopWindowTarget %#lx\n", hr);
    if (FAILED(hr)) return 1;
    IDesktopWindowTarget_QueryInterface(desktop_target, &IID_ICompositionTarget, (void **)&target);
    ICompositor_CreateContainerVisual(compositor, &root);
    IContainerVisual_QueryInterface(root, &IID_IVisual, (void **)&visual);
    ICompositionTarget_put_Root(target, visual);
    IVisual_Release(visual);

    str = S(L"Windows.UI.Composition.Interactions.InteractionTracker");
    hr = RoGetActivationFactory(str, &IID_IInteractionTrackerStatics, (void **)&statics);
    WindowsDeleteString(str);
    if (FAILED(hr)) return 1;
    hr = IInteractionTrackerStatics_CreateWithOwner(statics, compositor, &owner, &tracker);
    printf("CreateWithOwner %#lx\n", hr);
    IInteractionTracker_QueryInterface(tracker, &IID_IInteractionTracker5, (void **)&tracker5);
    IInteractionTracker_put_MinPosition(tracker, (Vector3){-100000, -100000, 0});
    IInteractionTracker_put_MaxPosition(tracker, (Vector3){100000, 100000, 0});
    pump(500);
    begin();
    pump(200);
    printf("frames while idle: %u events\n", event_count);

    printf("== velocity, nothing in the way, the default decay rate\n");
    velocity_case("v100", (Vector3){0, 0, 0}, (Vector3){0, 100, 0}, 5000);
    velocity_case("v200", (Vector3){0, 0, 0}, (Vector3){0, 200, 0}, 5000);
    velocity_case("v500", (Vector3){0, 0, 0}, (Vector3){0, 500, 0}, 5000);
    velocity_case("v1000", (Vector3){0, 0, 0}, (Vector3){0, 1000, 0}, 5000);
    velocity_case("v3000", (Vector3){0, 0, 0}, (Vector3){0, 3000, 0}, 5000);
    velocity_case("v-1000", (Vector3){0, 0, 0}, (Vector3){0, -1000, 0}, 5000);

    printf("== decay rates\n");
    rates = (Vector3){0.95f, 0.5f, 0};
    set_decay(&rates);
    velocity_case("decay 0.5", (Vector3){0, 0, 0}, (Vector3){0, 1000, 0}, 5000);
    rates = (Vector3){0.95f, 0.99f, 0};
    set_decay(&rates);
    velocity_case("decay 0.99", (Vector3){0, 0, 0}, (Vector3){0, 1000, 0}, 5000);
    rates = (Vector3){0.95f, 0.95f, 0};
    set_decay(&rates);
    velocity_case("decay 0.95 set", (Vector3){0, 0, 0}, (Vector3){0, 1000, 0}, 5000);
    set_decay(NULL);

    printf("== more velocity while it coasts\n");
    settle_at((Vector3){0, 0, 0});
    begin();
    hr = IInteractionTracker_TryUpdatePositionWithAdditionalVelocity(tracker, (Vector3){0, 1000, 0}, &id);
    printf("v1000: %#lx id %d\n", hr, id);
    pump(100);
    hr = IInteractionTracker_TryUpdatePositionWithAdditionalVelocity(tracker, (Vector3){0, 1000, 0}, &id);
    printf("after 100 ms, v1000 more: %#lx id %d\n", hr, id);
    if (!wait_idle(id, 5000)) printf("  not idle after 5 s\n");
    print_events(FALSE);
    IInteractionTracker_get_Position(tracker, &at);
    printf("  at %g\n", at.Y);

    printf("== a bound in the way\n");
    IInteractionTracker_put_MinPosition(tracker, (Vector3){0, 0, 0});
    IInteractionTracker_put_MaxPosition(tracker, (Vector3){0, 100, 0});
    pump(200);
    velocity_case("v1000 to 100", (Vector3){0, 0, 0}, (Vector3){0, 1000, 0}, 5000);
    velocity_case("v-1000 to 0", (Vector3){0, 50, 0}, (Vector3){0, -1000, 0}, 5000);

    printf("== custom animations past the bounds\n");
    IInteractionTracker_put_MaxPosition(tracker, (Vector3){0, 1000, 0});
    pump(200);
    animation_case("past the upper bound", (Vector3){0, 300, 0}, (Vector3){0, 5000, 0}, 2000000);
    animation_case("past the lower bound", (Vector3){0, 300, 0}, (Vector3){0, -500, 0}, 2000000);
    animation_case("within them", (Vector3){0, 300, 0}, (Vector3){0, 600, 0}, 2000000);

    printf("== scale velocity\n");
    IInteractionTracker_put_MinScale(tracker, 0.25f);
    IInteractionTracker_put_MaxScale(tracker, 8.0f);
    IInteractionTracker_put_MinPosition(tracker, (Vector3){-100000, -100000, 0});
    IInteractionTracker_put_MaxPosition(tracker, (Vector3){100000, 100000, 0});
    settle_at((Vector3){0, 0, 0});
    begin();
    hr = IInteractionTracker_TryUpdateScaleWithAdditionalVelocity(tracker, 1.0f, (Vector3){0, 0, 0}, &id);
    printf("scale velocity 1: %#lx id %d\n", hr, id);
    if (!wait_idle(id, 5000)) printf("  not idle after 5 s\n");
    print_events(TRUE);
    IInteractionTracker_get_Scale(tracker, &scale);
    printf("  scale %g\n", scale);
    IInteractionTracker_TryUpdateScale(tracker, 1.0f, (Vector3){0, 0, 0}, &id);
    pump(300);
    begin();
    hr = IInteractionTracker_TryUpdateScaleWithAdditionalVelocity(tracker, 100.0f, (Vector3){0, 0, 0}, &id);
    printf("scale velocity 100: %#lx id %d\n", hr, id);
    if (!wait_idle(id, 5000)) printf("  not idle after 5 s\n");
    print_events(TRUE);
    IInteractionTracker_get_Scale(tracker, &scale);
    printf("  scale %g\n", scale);

    IInteractionTracker5_Release(tracker5);
    IInteractionTracker_Release(tracker);
    IInteractionTrackerStatics_Release(statics);
    DestroyWindow(hwnd);
    printf("done\n");
    return 0;
}
