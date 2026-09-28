/* clamp: whether an InteractionTracker keeps a position it was told to take without clamping while bounds that
 * expression animations compute from a visual, whose size a key frame animation changes, move past it -- what
 * Office's scrolling layer does when it scrolls to where the bounds reach only once the content has grown -- and
 * whether a position is clamped again when the bounds change while it is idle.  The compositor draws into a window
 * of its own, so that frames come.  Needs a desktop session (scripts/winrun.sh --desktop). */
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

static DWORD start;
static unsigned int changes;

static void pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG msg;
    while ((LONG)(end - GetTickCount()) > 0)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        Sleep(5);
    }
}

/*** the owner: prints what it hears, with the time since the case began ***/

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
    printf("    CustomAnimationStateEntered\n");
    return S_OK;
}
static HRESULT WINAPI owner_IdleStateEntered(IInteractionTrackerOwner *iface, IInteractionTracker *sender,
        IInteractionTrackerIdleStateEnteredArgs *args)
{
    INT32 id = -1;
    IInteractionTrackerIdleStateEnteredArgs_get_RequestId(args, &id);
    printf("    IdleStateEntered id %d\n", id);
    return S_OK;
}
static HRESULT WINAPI owner_InertiaStateEntered(IInteractionTrackerOwner *iface, IInteractionTracker *sender,
        IInteractionTrackerInertiaStateEnteredArgs *args)
{
    INT32 id = -1;
    IInteractionTrackerInertiaStateEnteredArgs_get_RequestId(args, &id);
    printf("    InertiaStateEntered id %d\n", id);
    return S_OK;
}
static HRESULT WINAPI owner_InteractingStateEntered(IInteractionTrackerOwner *iface, IInteractionTracker *sender,
        IInteractionTrackerInteractingStateEnteredArgs *args)
{
    printf("    InteractingStateEntered\n");
    return S_OK;
}
static HRESULT WINAPI owner_RequestIgnored(IInteractionTrackerOwner *iface, IInteractionTracker *sender,
        IInteractionTrackerRequestIgnoredArgs *args)
{
    printf("    RequestIgnored\n");
    return S_OK;
}
static HRESULT WINAPI owner_ValuesChanged(IInteractionTrackerOwner *iface, IInteractionTracker *sender,
        IInteractionTrackerValuesChangedArgs *args)
{
    INT32 id = -1;
    Vector3 v = {0};
    IInteractionTrackerValuesChangedArgs_get_RequestId(args, &id);
    IInteractionTrackerValuesChangedArgs_get_Position(args, &v);
    /* the times vary from run to run; the order and the values are what is compared */
    printf("    ValuesChanged id %d position %g %g\n", id, v.X, v.Y);
    changes++;
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

typedef struct { DWORD dwSize; int threadType; int apartmentType; } DispatcherQueueOptions_;

static ICompositor *compositor;
static IInteractionTracker *tracker;
static IInteractionTracker5 *tracker5;

static void position(const char *what)
{
    Vector3 v;

    IInteractionTracker_get_Position(tracker, &v);
    printf("  %s: position %g %g\n", what, v.X, v.Y);
}

static void update(Vector3 value, InteractionTrackerClampingOption option, InteractionTrackerPositionUpdateOption update)
{
    INT32 id = -1;
    HRESULT hr = IInteractionTracker5_TryUpdatePositionWithOption(tracker5, value, option, update, &id);

    printf("  TryUpdatePosition(%g %g, %s) %#lx id %d\n", value.X, value.Y,
           option == InteractionTrackerClampingOption_Disabled ? "clamping disabled" : "clamping", hr, id);
}

static void start_expression(IInspectable *object, const WCHAR *property, const WCHAR *text, IInspectable *content)
{
    ICompositionAnimation *animation;
    IExpressionAnimation *expression;
    ICompositionObject *target, *reference;
    HSTRING str = S(text), name;
    HRESULT hr;

    ICompositor_CreateExpressionAnimationWithExpression(compositor, str, &expression);
    WindowsDeleteString(str);
    IExpressionAnimation_QueryInterface(expression, &IID_ICompositionAnimation, (void **)&animation);
    IInspectable_QueryInterface(content, &IID_ICompositionObject, (void **)&reference);
    name = S(L"Content");
    ICompositionAnimation_SetReferenceParameter(animation, name, reference);
    WindowsDeleteString(name);
    ICompositionObject_Release(reference);
    IInspectable_QueryInterface(object, &IID_ICompositionObject, (void **)&target);
    name = S(property);
    hr = ICompositionObject_StartAnimation(target, name, animation);
    WindowsDeleteString(name);
    if (FAILED(hr)) printf("  StartAnimation(%ls) %#lx\n", property, hr);
    ICompositionObject_Release(target);
    ICompositionAnimation_Release(animation);
    IExpressionAnimation_Release(expression);
}

/* grows the content's height to the value over 300 ms, easing out as Office's scrolling layer does */
static void grow(IInspectable *content, float height)
{
    IScalarKeyFrameAnimation *kfa;
    ICompositionAnimation *animation;
    ICubicBezierEasingFunction *bezier;
    ICompositionEasingFunction *easing;
    ICompositionObject *target;
    IKeyFrameAnimation *kf;
    TimeSpan duration = {3000000};
    HSTRING name;
    HRESULT hr;

    ICompositor_CreateCubicBezierEasingFunction(compositor, (Vector2){0, 0}, (Vector2){0, 1}, &bezier);
    ICubicBezierEasingFunction_QueryInterface(bezier, &IID_ICompositionEasingFunction, (void **)&easing);
    ICompositor_CreateScalarKeyFrameAnimation(compositor, &kfa);
    IScalarKeyFrameAnimation_InsertKeyFrameWithEasingFunction(kfa, 1.0f, height, easing);
    IScalarKeyFrameAnimation_QueryInterface(kfa, &IID_IKeyFrameAnimation, (void **)&kf);
    IKeyFrameAnimation_put_Duration(kf, duration);
    IKeyFrameAnimation_Release(kf);
    IScalarKeyFrameAnimation_QueryInterface(kfa, &IID_ICompositionAnimation, (void **)&animation);
    IInspectable_QueryInterface(content, &IID_ICompositionObject, (void **)&target);
    name = S(L"Size.Y");
    hr = ICompositionObject_StartAnimation(target, name, animation);
    WindowsDeleteString(name);
    printf("  grow the content to %g: %#lx\n", height, hr);
    ICompositionObject_Release(target);
    ICompositionAnimation_Release(animation);
    IScalarKeyFrameAnimation_Release(kfa);
    ICompositionEasingFunction_Release(easing);
    ICubicBezierEasingFunction_Release(bezier);
}

static void set_height(ISpriteVisual *content, float height)
{
    IVisual *visual;

    ISpriteVisual_QueryInterface(content, &IID_IVisual, (void **)&visual);
    IVisual_put_Size(visual, (Vector2){100, height});
    IVisual_Release(visual);
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
    IVisualCollection *children;
    ISpriteVisual *content;
    ICompositionColorBrush *brush;
    ICompositionBrush *base_brush;
    IVisual *visual;
    IInspectable *inspectable, *content_object, *tracker_object;
    WNDCLASSW cls = {0};
    HWND hwnd;
    HSTRING str;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
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

    /* a window for the compositor to draw into, with the content in it */
    cls.lpfnWndProc = window_proc;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.lpszClassName = L"trackerclamp";
    RegisterClassW(&cls);
    hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, L"trackerclamp", L"trackerclamp",
                           WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 300, 300, NULL, NULL, cls.hInstance, NULL);
    hr = ICompositorDesktopInterop_CreateDesktopWindowTarget(interop, hwnd, FALSE, &desktop_target);
    printf("CreateDesktopWindowTarget %#lx\n", hr);
    if (FAILED(hr)) return 1;
    IDesktopWindowTarget_QueryInterface(desktop_target, &IID_ICompositionTarget, (void **)&target);
    ICompositor_CreateContainerVisual(compositor, &root);
    IContainerVisual_QueryInterface(root, &IID_IVisual, (void **)&visual);
    ICompositionTarget_put_Root(target, visual);
    IVisual_Release(visual);
    ICompositor_CreateSpriteVisual(compositor, &content);
    ICompositor_CreateColorBrushWithColor(compositor, (Color){255, 40, 120, 200}, &brush);
    ICompositionColorBrush_QueryInterface(brush, &IID_ICompositionBrush, (void **)&base_brush);
    ISpriteVisual_put_Brush(content, base_brush);
    ICompositionBrush_Release(base_brush);
    set_height(content, 400);
    IContainerVisual_get_Children(root, &children);
    ISpriteVisual_QueryInterface(content, &IID_IVisual, (void **)&visual);
    IVisualCollection_InsertAtTop(children, visual);
    IVisual_Release(visual);
    ISpriteVisual_QueryInterface(content, &IID_IInspectable, (void **)&content_object);

    str = S(L"Windows.UI.Composition.Interactions.InteractionTracker");
    hr = RoGetActivationFactory(str, &IID_IInteractionTrackerStatics, (void **)&statics);
    WindowsDeleteString(str);
    if (FAILED(hr)) return 1;
    hr = IInteractionTrackerStatics_CreateWithOwner(statics, compositor, &owner, &tracker);
    printf("CreateWithOwner %#lx\n", hr);
    IInteractionTracker_QueryInterface(tracker, &IID_IInteractionTracker5, (void **)&tracker5);
    IInteractionTracker_QueryInterface(tracker, &IID_IInspectable, (void **)&tracker_object);
    pump(300);

    printf("fixed bounds 0..100, a position beyond them, clamping disabled:\n");
    IInteractionTracker_put_MaxPosition(tracker, (Vector3){0, 100, 0});
    pump(200);
    update((Vector3){0, 300, 0}, InteractionTrackerClampingOption_Disabled, InteractionTrackerPositionUpdateOption_Default);
    pump(400);
    position("after 400 ms");
    printf("  the upper bound moves to 200 while it is idle\n");
    IInteractionTracker_put_MaxPosition(tracker, (Vector3){0, 200, 0});
    pump(400);
    position("after 400 ms");

    printf("a position within the bounds; then the upper bound moves below it while it is idle:\n");
    update((Vector3){0, 50, 0}, InteractionTrackerClampingOption_Auto, InteractionTrackerPositionUpdateOption_Default);
    pump(400);
    IInteractionTracker_put_MaxPosition(tracker, (Vector3){0, 20, 0});
    pump(400);
    position("after 400 ms");

    printf("bounds an expression computes from the content's height, 299..300:\n");
    start_expression(tracker_object, L"MinPosition", L"Vector3(0, Floor(Content.Size.Y - 100.5), 0)", content_object);
    start_expression(tracker_object, L"MaxPosition", L"Vector3(0, Ceil(Content.Size.Y - 100.5), 0)", content_object);
    pump(400);
    position("after 400 ms");

    printf("the content grows to 1000, and the position is to be where its bounds then are, clamping disabled:\n");
    changes = 0;
    start = GetTickCount();
    grow(content_object, 1000);
    update((Vector3){0, 900, 0}, InteractionTrackerClampingOption_Disabled,
           InteractionTrackerPositionUpdateOption_AllowActiveCustomScaleAnimation);
    pump(900);
    position("after 900 ms");
    printf("  %u value changes\n", changes);

    printf("the content shrinks back to 400, and the position is to be at 299 with clamping:\n");
    changes = 0;
    grow(content_object, 400);
    update((Vector3){0, 299, 0}, InteractionTrackerClampingOption_Auto, InteractionTrackerPositionUpdateOption_Default);
    pump(900);
    position("after 900 ms");
    printf("  %u value changes\n", changes);

    printf("the content grows to 1000 again, the position to be at 900 with clamping:\n");
    changes = 0;
    grow(content_object, 1000);
    update((Vector3){0, 900, 0}, InteractionTrackerClampingOption_Auto, InteractionTrackerPositionUpdateOption_Default);
    pump(900);
    position("after 900 ms");
    printf("  %u value changes\n", changes);

    IInspectable_Release(tracker_object);
    IInspectable_Release(content_object);
    IInteractionTracker5_Release(tracker5);
    IInteractionTracker_Release(tracker);
    IInteractionTrackerStatics_Release(statics);
    DestroyWindow(hwnd);
    printf("done\n");
    return 0;
}
