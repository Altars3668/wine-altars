/* Excel 切换工作簿时在 RemoveAll 后对同一个 Visual 再次 Create 的契约。 */
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
#define WIDL_using_Windows_UI_Composition_Interactions
#include "windows.foundation.h"
#include "windows.ui.composition.h"

typedef struct { DWORD size; int thread_type; int apartment_type; } DispatcherQueueOptions_;

static HSTRING make_string(const WCHAR *text)
{
    HSTRING result = NULL;
    WindowsCreateString(text, wcslen(text), &result);
    return result;
}

int main(void)
{
    HRESULT (WINAPI *create_queue)(DispatcherQueueOptions_, IUnknown **);
    DispatcherQueueOptions_ options = {sizeof(options), 2, 2};
    IVisualInteractionSourceStatics *source_statics = NULL;
    IInteractionTrackerStatics *tracker_statics = NULL;
    ICompositionInteractionSourceCollection *sources = NULL;
    ICompositionInteractionSource *isource = NULL;
    IVisualInteractionSource *source = NULL, *second = NULL, *third = NULL;
    IInteractionTracker *tracker = NULL;
    IContainerVisual *container = NULL;
    ICompositor *compositor = NULL;
    IInspectable *instance = NULL;
    IUnknown *queue = NULL;
    IVisual *visual = NULL;
    HSTRING class_name;
    HMODULE messaging;
    HRESULT hr;
    INT32 count;

    setvbuf(stdout, NULL, _IONBF, 0);
    hr = RoInitialize(RO_INIT_SINGLETHREADED);
    printf("RoInitialize %#lx\n", hr);
    messaging = LoadLibraryW(L"coremessaging.dll");
    create_queue = (void *)GetProcAddress(messaging, "CreateDispatcherQueueController");
    if (!create_queue) return 1;
    hr = create_queue(options, &queue);
    printf("DispatcherQueue %#lx\n", hr);
    if (FAILED(hr)) return 2;

    class_name = make_string(L"Windows.UI.Composition.Compositor");
    hr = RoActivateInstance(class_name, &instance);
    WindowsDeleteString(class_name);
    printf("Compositor %#lx\n", hr);
    if (FAILED(hr)) return 3;
    hr = IInspectable_QueryInterface(instance, &IID_ICompositor, (void **)&compositor);
    IInspectable_Release(instance);
    if (FAILED(hr)) return 4;

    hr = ICompositor_CreateContainerVisual(compositor, &container);
    printf("ContainerVisual %#lx\n", hr);
    if (FAILED(hr)) return 5;
    hr = IContainerVisual_QueryInterface(container, &IID_IVisual, (void **)&visual);
    if (FAILED(hr)) return 6;

    class_name = make_string(L"Windows.UI.Composition.Interactions.VisualInteractionSource");
    hr = RoGetActivationFactory(class_name, &IID_IVisualInteractionSourceStatics, (void **)&source_statics);
    WindowsDeleteString(class_name);
    printf("SourceStatics %#lx\n", hr);
    if (FAILED(hr)) return 7;
    hr = IVisualInteractionSourceStatics_Create(source_statics, visual, &source);
    printf("first Create %#lx source %s\n", hr, source ? "set" : "null");
    if (FAILED(hr)) return 8;
    hr = IVisualInteractionSourceStatics_Create(source_statics, visual, &second);
    printf("second Create before Add %#lx source %s\n", hr, second ? "set" : "null");
    if (second) IVisualInteractionSource_Release(second);

    class_name = make_string(L"Windows.UI.Composition.Interactions.InteractionTracker");
    hr = RoGetActivationFactory(class_name, &IID_IInteractionTrackerStatics, (void **)&tracker_statics);
    WindowsDeleteString(class_name);
    printf("TrackerStatics %#lx\n", hr);
    if (FAILED(hr)) return 9;
    hr = IInteractionTrackerStatics_Create(tracker_statics, compositor, &tracker);
    printf("Tracker %#lx\n", hr);
    if (FAILED(hr)) return 10;
    hr = IInteractionTracker_get_InteractionSources(tracker, &sources);
    if (FAILED(hr)) return 11;
    hr = IVisualInteractionSource_QueryInterface(source, &IID_ICompositionInteractionSource, (void **)&isource);
    if (FAILED(hr)) return 12;
    hr = ICompositionInteractionSourceCollection_Add(sources, isource);
    printf("Add %#lx\n", hr);
    if (FAILED(hr)) return 13;
    ICompositionInteractionSource_Release(isource);
    count = -1;
    ICompositionInteractionSourceCollection_get_Count(sources, &count);
    printf("before RemoveAll count %d\n", count);
    hr = ICompositionInteractionSourceCollection_RemoveAll(sources);
    printf("RemoveAll %#lx\n", hr);
    count = -1;
    ICompositionInteractionSourceCollection_get_Count(sources, &count);
    printf("after RemoveAll count %d\n", count);
    second = NULL;
    hr = IVisualInteractionSourceStatics_Create(source_statics, visual, &second);
    printf("Create after RemoveAll with old source still held %#lx source %s\n", hr, second ? "set" : "null");
    if (second)
    {
        hr = IVisualInteractionSource_QueryInterface(second, &IID_ICompositionInteractionSource, (void **)&isource);
        if (FAILED(hr)) return 14;
        hr = ICompositionInteractionSourceCollection_Add(sources, isource);
        printf("Add second source %#lx\n", hr);
        if (FAILED(hr)) return 15;
        hr = ICompositionInteractionSourceCollection_Remove(sources, isource);
        printf("Remove second source %#lx\n", hr);
        ICompositionInteractionSource_Release(isource);
        if (FAILED(hr)) return 16;
        hr = IVisualInteractionSourceStatics_Create(source_statics, visual, &third);
        printf("Create after Remove with second source held %#lx source %s\n", hr, third ? "set" : "null");
        if (third) IVisualInteractionSource_Release(third);
        IVisualInteractionSource_Release(second);
    }
    IVisualInteractionSource_Release(source);
    ICompositionInteractionSourceCollection_Release(sources);
    IInteractionTracker_Release(tracker);
    IInteractionTrackerStatics_Release(tracker_statics);
    IVisualInteractionSourceStatics_Release(source_statics);
    IVisual_Release(visual);
    IContainerVisual_Release(container);
    ICompositor_Release(compositor);
    IUnknown_Release(queue);
    RoUninitialize();
    printf("done\n");
    return 0;
}
