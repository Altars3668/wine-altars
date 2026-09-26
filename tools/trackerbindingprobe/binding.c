/* InteractionTrackerStatics2 的同步绑定契约；锁屏桌面下不据此推断逐帧动画。 */
#define COBJMACROS
#include <stdio.h>
#include "windef.h"
#include "initguid.h"
#include "winbase.h"
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
#define WIDL_using_Windows_UI_Composition_Interactions
#include "windows.foundation.h"
#include "windows.ui.composition.h"
#include "windows.ui.composition.interop.h"

typedef struct { DWORD dwSize; int threadType; int apartmentType; } DispatcherQueueOptions_;

static HSTRING make_string(const WCHAR *text)
{
    HSTRING string = NULL;
    WindowsCreateString(text, wcslen(text), &string);
    return string;
}

static void get_mode(IInteractionTrackerStatics2 *statics, const char *label,
                     IInteractionTracker *first, IInteractionTracker *second)
{
    InteractionBindingAxisModes mode = 0x7fff;
    HRESULT hr = IInteractionTrackerStatics2_GetBindingMode(statics, first, second, &mode);
    printf("%s: hr %#lx mode %#x\n", label, hr, mode);
}

static void set_mode(IInteractionTrackerStatics2 *statics, const char *label,
                     IInteractionTracker *first, IInteractionTracker *second,
                     InteractionBindingAxisModes mode)
{
    HRESULT hr = IInteractionTrackerStatics2_SetBindingMode(statics, first, second, mode);
    printf("%s: hr %#lx\n", label, hr);
}

static void show_values(const char *label, IInteractionTracker *first, IInteractionTracker *second)
{
    Vector3 first_position = {0}, second_position = {0};
    FLOAT first_scale = 0, second_scale = 0;

    IInteractionTracker_get_Position(first, &first_position);
    IInteractionTracker_get_Position(second, &second_position);
    IInteractionTracker_get_Scale(first, &first_scale);
    IInteractionTracker_get_Scale(second, &second_scale);
    printf("%s: first %g %g scale %g, second %g %g scale %g\n", label,
           first_position.X, first_position.Y, first_scale,
           second_position.X, second_position.Y, second_scale);
}

static void pump(DWORD milliseconds)
{
    DWORD end = GetTickCount() + milliseconds;
    MSG message;

    while (GetTickCount() < end)
    {
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
        Sleep(5);
    }
}

int main(void)
{
    HRESULT (WINAPI *create_queue)(DispatcherQueueOptions_, IUnknown **);
    DispatcherQueueOptions_ options = {sizeof(options), 2, 2};
    IInteractionTrackerStatics *statics = NULL;
    IInteractionTrackerStatics2 *statics2 = NULL;
    IInteractionTracker *first = NULL, *second = NULL, *third = NULL;
    IInteractionTracker *same_device_tracker = NULL, *other_device_tracker = NULL;
    ICompositorController *controller = NULL;
    ICompositor *compositor = NULL, *other_compositor = NULL;
    IInspectable *instance = NULL;
    IUnknown *queue = NULL;
    HMODULE messaging;
    HSTRING name;
    HRESULT hr;
    INT32 id;

    setvbuf(stdout, NULL, _IONBF, 0);
    hr = RoInitialize(RO_INIT_SINGLETHREADED);
    printf("RoInitialize %#lx\n", hr);
    messaging = LoadLibraryW(L"coremessaging.dll");
    create_queue = (void *)GetProcAddress(messaging, "CreateDispatcherQueueController");
    if (!create_queue) return 1;
    hr = create_queue(options, &queue);
    printf("DispatcherQueue %#lx\n", hr);
    if (FAILED(hr)) return 2;

    name = make_string(L"Windows.UI.Composition.Core.CompositorController");
    hr = RoActivateInstance(name, &instance);
    WindowsDeleteString(name);
    printf("CompositorController %#lx\n", hr);
    if (FAILED(hr)) return 3;
    hr = IInspectable_QueryInterface(instance, &IID_ICompositorController, (void **)&controller);
    IInspectable_Release(instance);
    if (FAILED(hr)) return 4;
    hr = ICompositorController_get_Compositor(controller, &compositor);
    if (FAILED(hr)) return 4;

    name = make_string(L"Windows.UI.Composition.Interactions.InteractionTracker");
    hr = RoGetActivationFactory(name, &IID_IInteractionTrackerStatics, (void **)&statics);
    WindowsDeleteString(name);
    printf("TrackerStatics %#lx\n", hr);
    if (FAILED(hr)) return 5;
    hr = IInteractionTrackerStatics_QueryInterface(statics, &IID_IInteractionTrackerStatics2, (void **)&statics2);
    printf("TrackerStatics2 %#lx\n", hr);
    if (FAILED(hr)) return 6;

    hr = IInteractionTrackerStatics_Create(statics, compositor, &first);
    printf("first %#lx\n", hr);
    if (FAILED(hr)) return 7;
    hr = IInteractionTrackerStatics_Create(statics, compositor, &second);
    printf("second %#lx\n", hr);
    if (FAILED(hr)) return 8;
    hr = IInteractionTrackerStatics_Create(statics, compositor, &third);
    printf("third %#lx\n", hr);
    if (FAILED(hr)) return 9;

    set_mode(statics2, "fresh bind x first-second", first, second, InteractionBindingAxisModes_PositionX);
    set_mode(statics2, "fresh bind y first-third", first, third, InteractionBindingAxisModes_PositionY);
    get_mode(statics2, "fresh first-second after third y", first, second);
    get_mode(statics2, "fresh first-third after third y", first, third);
    get_mode(statics2, "fresh third-first reverse", third, first);
    set_mode(statics2, "fresh clear first-third", first, third, InteractionBindingAxisModes_None);
    get_mode(statics2, "fresh first-second after clear third", first, second);
    set_mode(statics2, "fresh clear first-second", first, second, InteractionBindingAxisModes_None);
    get_mode(statics2, "before first-second", first, second);
    set_mode(statics2, "set x", first, second, InteractionBindingAxisModes_PositionX);
    get_mode(statics2, "after x", first, second);
    set_mode(statics2, "set y", first, second, InteractionBindingAxisModes_PositionY);
    get_mode(statics2, "after y", first, second);
    set_mode(statics2, "set scale", first, second, InteractionBindingAxisModes_Scale);
    get_mode(statics2, "after scale", first, second);
    set_mode(statics2, "set all", first, second, 7);
    get_mode(statics2, "after all", first, second);
    set_mode(statics2, "set none", first, second, InteractionBindingAxisModes_None);
    get_mode(statics2, "after none", first, second);
    set_mode(statics2, "first-second again x", first, second, InteractionBindingAxisModes_PositionX);
    get_mode(statics2, "reverse again x", second, first);
    set_mode(statics2, "none on unrelated third", first, third, InteractionBindingAxisModes_None);
    get_mode(statics2, "first-second after unrelated none", first, second);
    set_mode(statics2, "first-third y", first, third, InteractionBindingAxisModes_PositionY);
    get_mode(statics2, "first-second after third", first, second);
    get_mode(statics2, "first-third", first, third);
    get_mode(statics2, "third-first", third, first);

    set_mode(statics2, "clear third", first, third, InteractionBindingAxisModes_None);
    IInteractionTracker_put_MaxPosition(first, (Vector3){1000, 1000, 0});
    IInteractionTracker_put_MaxPosition(second, (Vector3){1000, 1000, 0});
    id = -1;
    hr = IInteractionTracker_TryUpdatePosition(first, (Vector3){10, 20, 0}, &id);
    printf("first position request %#lx id %d\n", hr, id);
    id = -1;
    hr = IInteractionTracker_TryUpdatePosition(second, (Vector3){70, 80, 0}, &id);
    printf("second position request %#lx id %d\n", hr, id);
    pump(100);
    show_values("before position bind", first, second);
    set_mode(statics2, "bind position x", first, second, InteractionBindingAxisModes_PositionX);
    pump(100);
    show_values("after position bind", first, second);
    id = -1;
    hr = IInteractionTracker_TryUpdatePosition(first, (Vector3){30, 40, 0}, &id);
    printf("bound position request %#lx id %d\n", hr, id);
    pump(100);
    show_values("after bound update", first, second);

    set_mode(statics2, "none on unrelated pair", first, third, InteractionBindingAxisModes_None);
    get_mode(statics2, "first-second after unrelated none", first, second);
    set_mode(statics2, "unsupported axis 8", first, second, 8);
    get_mode(statics2, "first-second after unsupported axis", first, second);

    name = make_string(L"Windows.UI.Composition.Compositor");
    hr = RoActivateInstance(name, &instance);
    WindowsDeleteString(name);
    printf("second Compositor %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IInspectable_QueryInterface(instance, &IID_ICompositor, (void **)&other_compositor);
        IInspectable_Release(instance);
        printf("second Compositor interface %#lx\n", hr);
    }
    if (other_compositor)
    {
        hr = IInteractionTrackerStatics_Create(statics, compositor, &same_device_tracker);
        printf("same compositor tracker %#lx\n", hr);
        hr = IInteractionTrackerStatics_Create(statics, other_compositor, &other_device_tracker);
        printf("other compositor tracker %#lx\n", hr);
        if (same_device_tracker && other_device_tracker)
        {
            set_mode(statics2, "cross compositor x", same_device_tracker, other_device_tracker,
                     InteractionBindingAxisModes_PositionX);
            get_mode(statics2, "cross compositor mode", same_device_tracker, other_device_tracker);
        }
    }
    if (other_device_tracker) IInteractionTracker_Release(other_device_tracker);
    if (same_device_tracker) IInteractionTracker_Release(same_device_tracker);
    if (other_compositor) ICompositor_Release(other_compositor);

    IInteractionTracker_Release(third);
    IInteractionTracker_Release(second);
    IInteractionTracker_Release(first);
    IInteractionTrackerStatics2_Release(statics2);
    IInteractionTrackerStatics_Release(statics);
    ICompositor_Release(compositor);
    ICompositorController_Release(controller);
    IUnknown_Release(queue);
    RoUninitialize();
    printf("done\n");
    return 0;
}
