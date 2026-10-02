/* requestid: which of an InteractionTracker's setters and methods take a request id -- the next request's id, less
 * the one before the call, less one.  Answers there and then, so it needs no frames.  Prints results only. */
#define COBJMACROS
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "windef.h"
#include "initguid.h"
#include "winbase.h"
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
#define WIDL_using_Windows_UI_Composition_Interactions
#include "windows.foundation.h"
#include "windows.ui.composition.h"

static HSTRING S(const WCHAR *s)
{
    HSTRING h = NULL;
    WindowsCreateString(s, wcslen(s), &h);
    return h;
}

typedef struct { DWORD dwSize; int threadType; int apartmentType; } DispatcherQueueOptions_;

struct float_ref { IReference_FLOAT IReference_FLOAT_iface; };
static HRESULT WINAPI fref_QueryInterface(IReference_FLOAT *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) || IsEqualGUID(iid, &IID_IReference_FLOAT))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI fref_AddRef(IReference_FLOAT *iface) { return 2; }
static ULONG WINAPI fref_Release(IReference_FLOAT *iface) { return 1; }
static HRESULT WINAPI fref_GetIids(IReference_FLOAT *iface, ULONG *count, IID **iids) { return E_NOTIMPL; }
static HRESULT WINAPI fref_GetRuntimeClassName(IReference_FLOAT *iface, HSTRING *name) { return E_NOTIMPL; }
static HRESULT WINAPI fref_GetTrustLevel(IReference_FLOAT *iface, TrustLevel *level) { return E_NOTIMPL; }
static HRESULT WINAPI fref_get_Value(IReference_FLOAT *iface, FLOAT *value) { *value = 0.9f; return S_OK; }
static const IReference_FLOATVtbl fref_vtbl =
{
    fref_QueryInterface, fref_AddRef, fref_Release, fref_GetIids, fref_GetRuntimeClassName, fref_GetTrustLevel,
    fref_get_Value,
};
static struct float_ref fref = {{&fref_vtbl}};

static IInteractionTracker *tracker;
static INT32 last;

static void step(const char *what, HRESULT hr)
{
    INT32 id = -1;

    IInteractionTracker_TryUpdatePosition(tracker, (Vector3){0, 0, 0}, &id);
    printf("%-40s %#lx, %d ids\n", what, hr, id - last - 1);
    last = id;
}

int main(void)
{
    IInteractionTrackerStatics *statics;
    IInteractionTracker2 *tracker2;
    IInteractionTracker4 *tracker4;
    IInteractionTracker5 *tracker5;
    IInspectable *inspectable;
    ICompositor *compositor;
    HSTRING str;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    RoInitialize(RO_INIT_SINGLETHREADED);
    {
        HRESULT (WINAPI *create)(DispatcherQueueOptions_, IUnknown **) =
            (void *)GetProcAddress(LoadLibraryW(L"coremessaging.dll"), "CreateDispatcherQueueController");
        DispatcherQueueOptions_ options = {sizeof(options), 2, 2};
        IUnknown *queue = NULL;
        create(options, &queue);
    }
    str = S(L"Windows.UI.Composition.Compositor");
    hr = RoActivateInstance(str, &inspectable);
    WindowsDeleteString(str);
    if (FAILED(hr)) { printf("activate Compositor %#lx\n", hr); return 1; }
    IInspectable_QueryInterface(inspectable, &IID_ICompositor, (void **)&compositor);
    IInspectable_Release(inspectable);
    str = S(L"Windows.UI.Composition.Interactions.InteractionTracker");
    hr = RoGetActivationFactory(str, &IID_IInteractionTrackerStatics, (void **)&statics);
    WindowsDeleteString(str);
    if (FAILED(hr)) return 1;
    IInteractionTrackerStatics_Create(statics, compositor, &tracker);
    IInteractionTracker_QueryInterface(tracker, &IID_IInteractionTracker2, (void **)&tracker2);
    IInteractionTracker_QueryInterface(tracker, &IID_IInteractionTracker4, (void **)&tracker4);
    IInteractionTracker_QueryInterface(tracker, &IID_IInteractionTracker5, (void **)&tracker5);
    step("start", S_OK);
    step("nothing", S_OK);
    step("put_MinPosition", IInteractionTracker_put_MinPosition(tracker, (Vector3){-10, -10, 0}));
    step("put_MaxPosition", IInteractionTracker_put_MaxPosition(tracker, (Vector3){100, 100, 0}));
    step("put_MinScale", IInteractionTracker_put_MinScale(tracker, 0.5f));
    step("put_MaxScale", IInteractionTracker_put_MaxScale(tracker, 4.0f));
    step("put_PositionInertiaDecayRate(NULL)", IInteractionTracker_put_PositionInertiaDecayRate(tracker, NULL));
    step("put_ScaleInertiaDecayRate", IInteractionTracker_put_ScaleInertiaDecayRate(tracker, &fref.IReference_FLOAT_iface));
    step("put_ScaleInertiaDecayRate(NULL)", IInteractionTracker_put_ScaleInertiaDecayRate(tracker, NULL));
    step("ConfigurePositionXInertiaModifiers(NULL)", IInteractionTracker_ConfigurePositionXInertiaModifiers(tracker, NULL));
    step("ConfigureScaleInertiaModifiers(NULL)", IInteractionTracker_ConfigureScaleInertiaModifiers(tracker, NULL));
    step("AdjustPositionYIfGreaterThanThreshold", IInteractionTracker_AdjustPositionYIfGreaterThanThreshold(tracker, 5, -100));
    step("ConfigureCenterPointXInertiaModifiers", IInteractionTracker2_ConfigureCenterPointXInertiaModifiers(tracker2, NULL));
    step("put_MinScale again, same value", IInteractionTracker_put_MinScale(tracker, 0.5f));
    {
        boolean value = FALSE;
        step("get_IsInertiaFromImpulse", IInteractionTracker4_get_IsInertiaFromImpulse(tracker4, &value));
    }
    {
        INT32 id = -1;
        hr = IInteractionTracker5_TryUpdatePositionWithOption(tracker5, (Vector3){0, 50, 0},
                InteractionTrackerClampingOption_Disabled, InteractionTrackerPositionUpdateOption_Default, &id);
        step("TryUpdatePositionWithOption (one itself)", hr);
    }
    printf("done\n");
    return 0;
}
