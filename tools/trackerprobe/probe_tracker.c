/* probe_tracker: what an InteractionTracker says, there and then, to the requests that move it by
 * animation and by velocity -- request ids, results, what it accepts as an animation of its
 * position and scale, and what StartAnimation on its own properties is told.  All of it answers
 * before a frame is drawn, so it measures the same on a locked session as on a live one; whatever
 * the owner hears afterwards is printed as it comes.  Needs a desktop session
 * (scripts/winrun.sh --desktop). */
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

/*** the owner: prints what it hears ***/

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
    printf("    [%s] CustomAnimationStateEntered id %d\n", phase, id);
    return S_OK;
}
static HRESULT WINAPI owner_IdleStateEntered(IInteractionTrackerOwner *iface, IInteractionTracker *sender,
        IInteractionTrackerIdleStateEnteredArgs *args)
{
    INT32 id = -1;
    IInteractionTrackerIdleStateEnteredArgs_get_RequestId(args, &id);
    printf("    [%s] IdleStateEntered id %d\n", phase, id);
    return S_OK;
}
static HRESULT WINAPI owner_InertiaStateEntered(IInteractionTrackerOwner *iface, IInteractionTracker *sender,
        IInteractionTrackerInertiaStateEnteredArgs *args)
{
    INT32 id = -1;
    Vector3 rest = {0}, velocity = {0};
    FLOAT scale = -1, scale_velocity = -1;
    IReference_Vector3 *modified = (void *)0xdeadbeef;
    IReference_FLOAT *modified_scale = (void *)0xdeadbeef;
    IInteractionTrackerInertiaStateEnteredArgs_get_RequestId(args, &id);
    IInteractionTrackerInertiaStateEnteredArgs_get_NaturalRestingPosition(args, &rest);
    IInteractionTrackerInertiaStateEnteredArgs_get_NaturalRestingScale(args, &scale);
    IInteractionTrackerInertiaStateEnteredArgs_get_PositionVelocityInPixelsPerSecond(args, &velocity);
    IInteractionTrackerInertiaStateEnteredArgs_get_ScaleVelocityInPercentPerSecond(args, &scale_velocity);
    IInteractionTrackerInertiaStateEnteredArgs_get_ModifiedRestingPosition(args, &modified);
    IInteractionTrackerInertiaStateEnteredArgs_get_ModifiedRestingScale(args, &modified_scale);
    printf("    [%s] InertiaStateEntered id %d resting %g %g %g scale %g velocity %g %g %g scale velocity %g "
           "modified %p %p\n", phase, id, rest.X, rest.Y, rest.Z, scale, velocity.X, velocity.Y, velocity.Z,
           scale_velocity, modified, modified_scale);
    return S_OK;
}
static HRESULT WINAPI owner_InteractingStateEntered(IInteractionTrackerOwner *iface, IInteractionTracker *sender,
        IInteractionTrackerInteractingStateEnteredArgs *args)
{
    INT32 id = -1;
    IInteractionTrackerInteractingStateEnteredArgs_get_RequestId(args, &id);
    printf("    [%s] InteractingStateEntered id %d\n", phase, id);
    return S_OK;
}
static HRESULT WINAPI owner_RequestIgnored(IInteractionTrackerOwner *iface, IInteractionTracker *sender,
        IInteractionTrackerRequestIgnoredArgs *args)
{
    INT32 id = -1;
    IInteractionTrackerRequestIgnoredArgs_get_RequestId(args, &id);
    printf("    [%s] RequestIgnored id %d\n", phase, id);
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
    printf("    [%s] ValuesChanged id %d position %g %g %g scale %g\n", phase, id, v.X, v.Y, v.Z, scale);
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

static ICompositionAnimation *vector3_kfa(Vector3 value, BOOL expression_frame)
{
    IVector3KeyFrameAnimation *kfa;
    IKeyFrameAnimation *kf;
    ICompositionAnimation *animation;
    TimeSpan duration = {2000000};
    HSTRING str;

    ICompositor_CreateVector3KeyFrameAnimation(compositor, &kfa);
    if (expression_frame)
    {
        IVector3KeyFrameAnimation_QueryInterface(kfa, &IID_IKeyFrameAnimation, (void **)&kf);
        str = S(L"this.StartingValue + Vector3(0, 10, 0)");
        IKeyFrameAnimation_InsertExpressionKeyFrame(kf, 1.0f, str);
        WindowsDeleteString(str);
        IKeyFrameAnimation_Release(kf);
    }
    else IVector3KeyFrameAnimation_InsertKeyFrame(kfa, 1.0f, value);
    IVector3KeyFrameAnimation_QueryInterface(kfa, &IID_IKeyFrameAnimation, (void **)&kf);
    IKeyFrameAnimation_put_Duration(kf, duration);
    IKeyFrameAnimation_Release(kf);
    IVector3KeyFrameAnimation_QueryInterface(kfa, &IID_ICompositionAnimation, (void **)&animation);
    IVector3KeyFrameAnimation_Release(kfa);
    return animation;
}

static ICompositionAnimation *empty_vector3_kfa(void)
{
    IVector3KeyFrameAnimation *kfa;
    ICompositionAnimation *animation;

    ICompositor_CreateVector3KeyFrameAnimation(compositor, &kfa);
    IVector3KeyFrameAnimation_QueryInterface(kfa, &IID_ICompositionAnimation, (void **)&animation);
    IVector3KeyFrameAnimation_Release(kfa);
    return animation;
}

static ICompositionAnimation *scalar_kfa(float value)
{
    IScalarKeyFrameAnimation *kfa;
    IKeyFrameAnimation *kf;
    ICompositionAnimation *animation;
    TimeSpan duration = {2000000};

    ICompositor_CreateScalarKeyFrameAnimation(compositor, &kfa);
    IScalarKeyFrameAnimation_InsertKeyFrame(kfa, 1.0f, value);
    IScalarKeyFrameAnimation_QueryInterface(kfa, &IID_IKeyFrameAnimation, (void **)&kf);
    IKeyFrameAnimation_put_Duration(kf, duration);
    IKeyFrameAnimation_Release(kf);
    IScalarKeyFrameAnimation_QueryInterface(kfa, &IID_ICompositionAnimation, (void **)&animation);
    IScalarKeyFrameAnimation_Release(kfa);
    return animation;
}

static ICompositionAnimation *vector2_kfa(void)
{
    IVector2KeyFrameAnimation *kfa;
    ICompositionAnimation *animation;
    Vector2 v = {1, 2};

    ICompositor_CreateVector2KeyFrameAnimation(compositor, &kfa);
    IVector2KeyFrameAnimation_InsertKeyFrame(kfa, 1.0f, v);
    IVector2KeyFrameAnimation_QueryInterface(kfa, &IID_ICompositionAnimation, (void **)&animation);
    IVector2KeyFrameAnimation_Release(kfa);
    return animation;
}

static ICompositionAnimation *expression(const WCHAR *text)
{
    IExpressionAnimation *expression;
    ICompositionAnimation *animation;
    HSTRING str = S(text);

    ICompositor_CreateExpressionAnimationWithExpression(compositor, str, &expression);
    WindowsDeleteString(str);
    IExpressionAnimation_QueryInterface(expression, &IID_ICompositionAnimation, (void **)&animation);
    IExpressionAnimation_Release(expression);
    return animation;
}

static void values(const char *what, IInteractionTracker *tracker)
{
    Vector3 position, velocity, rest;
    FLOAT scale, scale_velocity, scale_rest;
    IInteractionTracker4 *tracker4;
    boolean impulse = 2;

    IInteractionTracker_get_Position(tracker, &position);
    IInteractionTracker_get_Scale(tracker, &scale);
    IInteractionTracker_get_PositionVelocityInPixelsPerSecond(tracker, &velocity);
    IInteractionTracker_get_ScaleVelocityInPercentPerSecond(tracker, &scale_velocity);
    IInteractionTracker_get_NaturalRestingPosition(tracker, &rest);
    IInteractionTracker_get_NaturalRestingScale(tracker, &scale_rest);
    if (SUCCEEDED(IInteractionTracker_QueryInterface(tracker, &IID_IInteractionTracker4, (void **)&tracker4)))
    {
        IInteractionTracker4_get_IsInertiaFromImpulse(tracker4, &impulse);
        IInteractionTracker4_Release(tracker4);
    }
    printf("  %s: position %g %g %g scale %g velocity %g %g %g scale velocity %g resting %g %g %g scale %g impulse %d\n",
           what, position.X, position.Y, position.Z, scale, velocity.X, velocity.Y, velocity.Z, scale_velocity,
           rest.X, rest.Y, rest.Z, scale_rest, impulse);
}

static void start_on(IInspectable *object, const WCHAR *property, ICompositionAnimation *animation)
{
    ICompositionObject *composition;
    HSTRING str = S(property);
    HRESULT hr;

    IInspectable_QueryInterface(object, &IID_ICompositionObject, (void **)&composition);
    hr = ICompositionObject_StartAnimation(composition, str, animation);
    printf("  StartAnimation(%ls) %#lx\n", property, hr);
    if (SUCCEEDED(hr)) ICompositionObject_StopAnimation(composition, str);
    WindowsDeleteString(str);
    ICompositionObject_Release(composition);
}

int main(void)
{
    IInteractionTrackerStatics *statics = NULL;
    IInteractionTracker *tracker = NULL;
    IInteractionTracker4 *tracker4;
    IInteractionTracker5 *tracker5;
    ICompositionAnimation *animation;
    IContainerVisual *container;
    IInspectable *inspectable;
    Vector3 center = {10, 20, 0}, v3;
    INT32 id;
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
    IInspectable_Release(inspectable);

    str = S(L"Windows.UI.Composition.Interactions.InteractionTracker");
    hr = RoGetActivationFactory(str, &IID_IInteractionTrackerStatics, (void **)&statics);
    WindowsDeleteString(str);
    printf("InteractionTracker statics %#lx\n", hr);
    if (FAILED(hr)) return 1;

    phase = "create";
    hr = IInteractionTrackerStatics_CreateWithOwner(statics, compositor, &owner, &tracker);
    printf("CreateWithOwner %#lx\n", hr);
    pump(200);
    v3.X = 0; v3.Y = 1000; v3.Z = 0;
    IInteractionTracker_put_MaxPosition(tracker, v3);
    IInteractionTracker_put_MaxScale(tracker, 4.0f);
    IInteractionTracker_put_MinScale(tracker, 0.5f);
    values("start", tracker);

    phase = "animations";
    printf("TryUpdatePositionWithAnimation\n");
    hr = IInteractionTracker_TryUpdatePositionWithAnimation(tracker, NULL, &id);
    printf("  NULL %#lx\n", hr);
    animation = vector3_kfa((Vector3){0, 100, 0}, FALSE);
    id = -1;
    hr = IInteractionTracker_TryUpdatePositionWithAnimation(tracker, animation, &id);
    printf("  vector3 key frames %#lx id %d\n", hr, id);
    values("right after", tracker);
    hr = IInteractionTracker_TryUpdatePositionWithAnimation(tracker, animation, &id);
    printf("  same one again %#lx id %d\n", hr, id);
    ICompositionAnimation_Release(animation);
    animation = vector3_kfa((Vector3){0, 0, 0}, TRUE);
    hr = IInteractionTracker_TryUpdatePositionWithAnimation(tracker, animation, &id);
    printf("  expression key frame %#lx id %d\n", hr, id);
    ICompositionAnimation_Release(animation);
    animation = empty_vector3_kfa();
    hr = IInteractionTracker_TryUpdatePositionWithAnimation(tracker, animation, &id);
    printf("  no key frames %#lx id %d\n", hr, id);
    ICompositionAnimation_Release(animation);
    animation = scalar_kfa(2.0f);
    id = -1;
    hr = IInteractionTracker_TryUpdatePositionWithAnimation(tracker, animation, &id);
    printf("  scalar key frames %#lx id %d\n", hr, id);
    ICompositionAnimation_Release(animation);
    animation = vector2_kfa();
    id = -1;
    hr = IInteractionTracker_TryUpdatePositionWithAnimation(tracker, animation, &id);
    printf("  vector2 key frames %#lx id %d\n", hr, id);
    ICompositionAnimation_Release(animation);
    animation = expression(L"Vector3(0, 50, 0)");
    id = -1;
    hr = IInteractionTracker_TryUpdatePositionWithAnimation(tracker, animation, &id);
    printf("  vector3 expression %#lx id %d\n", hr, id);
    ICompositionAnimation_Release(animation);
    animation = expression(L"2.5");
    id = -1;
    hr = IInteractionTracker_TryUpdatePositionWithAnimation(tracker, animation, &id);
    printf("  scalar expression %#lx id %d\n", hr, id);
    ICompositionAnimation_Release(animation);
    animation = expression(L"nosuch.Offset");
    id = -1;
    hr = IInteractionTracker_TryUpdatePositionWithAnimation(tracker, animation, &id);
    printf("  bad expression %#lx id %d\n", hr, id);
    ICompositionAnimation_Release(animation);
    animation = expression(L"this.StartingValue + Vector3(0, 1, 0)");
    id = -1;
    hr = IInteractionTracker_TryUpdatePositionWithAnimation(tracker, animation, &id);
    printf("  expression of StartingValue %#lx id %d\n", hr, id);
    ICompositionAnimation_Release(animation);
    animation = expression(L"this.Target.Position + Vector3(0, 1, 0)");
    id = -1;
    hr = IInteractionTracker_TryUpdatePositionWithAnimation(tracker, animation, &id);
    printf("  expression of this.Target %#lx id %d\n", hr, id);
    ICompositionAnimation_Release(animation);
    values("after the position animations", tracker);

    printf("TryUpdateScaleWithAnimation\n");
    hr = IInteractionTracker_TryUpdateScaleWithAnimation(tracker, NULL, center, &id);
    printf("  NULL %#lx\n", hr);
    animation = scalar_kfa(2.0f);
    id = -1;
    hr = IInteractionTracker_TryUpdateScaleWithAnimation(tracker, animation, center, &id);
    printf("  scalar key frames %#lx id %d\n", hr, id);
    ICompositionAnimation_Release(animation);
    animation = vector3_kfa((Vector3){0, 100, 0}, FALSE);
    id = -1;
    hr = IInteractionTracker_TryUpdateScaleWithAnimation(tracker, animation, center, &id);
    printf("  vector3 key frames %#lx id %d\n", hr, id);
    ICompositionAnimation_Release(animation);
    animation = expression(L"3.0");
    id = -1;
    hr = IInteractionTracker_TryUpdateScaleWithAnimation(tracker, animation, center, &id);
    printf("  scalar expression %#lx id %d\n", hr, id);
    ICompositionAnimation_Release(animation);
    values("after the scale animations", tracker);

    phase = "velocity";
    printf("velocity\n");
    id = -1;
    hr = IInteractionTracker_TryUpdatePositionWithAdditionalVelocity(tracker, (Vector3){0, 500, 0}, &id);
    printf("  TryUpdatePositionWithAdditionalVelocity(0,500) %#lx id %d\n", hr, id);
    id = -1;
    hr = IInteractionTracker_TryUpdateScaleWithAdditionalVelocity(tracker, 1.0f, center, &id);
    printf("  TryUpdateScaleWithAdditionalVelocity(1) %#lx id %d\n", hr, id);
    values("after velocity", tracker);

    phase = "options";
    if (SUCCEEDED(IInteractionTracker_QueryInterface(tracker, &IID_IInteractionTracker5, (void **)&tracker5)))
    {
        id = -1;
        hr = IInteractionTracker5_TryUpdatePositionWithOption(tracker5, (Vector3){0, 5, 0},
                InteractionTrackerClampingOption_Auto, InteractionTrackerPositionUpdateOption_AllowActiveCustomScaleAnimation, &id);
        printf("  TryUpdatePosition(allow custom scale) %#lx id %d\n", hr, id);
        id = -1;
        hr = IInteractionTracker5_TryUpdatePositionWithOption(tracker5, (Vector3){0, 5, 0}, 7, 0, &id);
        printf("  TryUpdatePosition(clamping 7) %#lx id %d\n", hr, id);
        id = -1;
        hr = IInteractionTracker5_TryUpdatePositionWithOption(tracker5, (Vector3){0, 5, 0}, 0, 7, &id);
        printf("  TryUpdatePosition(update option 7) %#lx id %d\n", hr, id);
        IInteractionTracker5_Release(tracker5);
    }
    if (SUCCEEDED(IInteractionTracker_QueryInterface(tracker, &IID_IInteractionTracker4, (void **)&tracker4)))
    {
        id = -1;
        hr = IInteractionTracker4_TryUpdatePositionWithOption(tracker4, (Vector3){0, 5, 0}, 7, &id);
        printf("  TryUpdatePosition(clamping 7, 4) %#lx id %d\n", hr, id);
        IInteractionTracker4_Release(tracker4);
    }

    printf("StartAnimation on the tracker\n");
    IInteractionTracker_QueryInterface(tracker, &IID_IInspectable, (void **)&inspectable);
    animation = vector3_kfa((Vector3){0, 100, 0}, FALSE);
    start_on(inspectable, L"Position", animation);
    start_on(inspectable, L"MinPosition", animation);
    start_on(inspectable, L"MaxPosition", animation);
    start_on(inspectable, L"NaturalRestingPosition", animation);
    start_on(inspectable, L"PositionVelocityInPixelsPerSecond", animation);
    start_on(inspectable, L"Position.Y", animation);
    ICompositionAnimation_Release(animation);
    animation = scalar_kfa(2.0f);
    start_on(inspectable, L"Scale", animation);
    start_on(inspectable, L"MinScale", animation);
    start_on(inspectable, L"MaxScale", animation);
    start_on(inspectable, L"NaturalRestingScale", animation);
    start_on(inspectable, L"ScaleVelocityInPercentPerSecond", animation);
    start_on(inspectable, L"Position.Y", animation);
    ICompositionAnimation_Release(animation);
    animation = expression(L"Vector3(0, 7, 0)");
    start_on(inspectable, L"Position", animation);
    ICompositionAnimation_Release(animation);
    IInspectable_Release(inspectable);

    printf("an empty key frame animation on a visual\n");
    ICompositor_CreateContainerVisual(compositor, &container);
    animation = empty_vector3_kfa();
    IContainerVisual_QueryInterface(container, &IID_IInspectable, (void **)&inspectable);
    start_on(inspectable, L"Offset", animation);
    IInspectable_Release(inspectable);
    ICompositionAnimation_Release(animation);
    IContainerVisual_Release(container);

    phase = "late";
    pump(1500);
    values("after 1.5s", tracker);
    IInteractionTracker_Release(tracker);
    IInteractionTrackerStatics_Release(statics);
    ICompositor_Release(compositor);
    printf("done\n");
    return 0;
}
