/* probe_dqtimer: Windows.System.DispatcherQueueTimer -- its defaults, when it ticks, on what thread,
 * and what Start, Stop, IsRepeating and a zero interval do. */
#define COBJMACROS
#include <stdarg.h>
#include <stdio.h>
#include "windef.h"
#include "initguid.h"
#include "winbase.h"
#include "winuser.h"
#include "winstring.h"
#include "roapi.h"
#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_System
#include "windows.foundation.h"
#include "windows.system.h"

typedef struct { DWORD dwSize; int threadType; int apartmentType; } DispatcherQueueOptions_;

static DWORD main_tid;
static LONG ticks;
static IDispatcherQueueTimer *stop_in_tick;

static HRESULT WINAPI tick_QueryInterface(ITypedEventHandler_DispatcherQueueTimer_IInspectable *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IAgileObject) ||
        IsEqualGUID(iid, &IID_ITypedEventHandler_DispatcherQueueTimer_IInspectable))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI tick_AddRef(ITypedEventHandler_DispatcherQueueTimer_IInspectable *iface) { return 2; }
static ULONG WINAPI tick_Release(ITypedEventHandler_DispatcherQueueTimer_IInspectable *iface) { return 1; }
static HRESULT WINAPI tick_Invoke(ITypedEventHandler_DispatcherQueueTimer_IInspectable *iface, IDispatcherQueueTimer *sender, IInspectable *args)
{
    boolean running = 2;
    ticks++;
    IDispatcherQueueTimer_get_IsRunning(sender, &running);
    if (ticks <= 3 || stop_in_tick)
        printf("    tick %ld args %p running %d (%s)\n", ticks, args, running, GetCurrentThreadId() == main_tid ? "main" : "other");
    if (stop_in_tick)
    {
        IDispatcherQueueTimer_Stop(stop_in_tick);
        IDispatcherQueueTimer_get_IsRunning(sender, &running);
        printf("    stopped in the tick, running %d\n", running);
        stop_in_tick = NULL;
    }
    return S_OK;
}
static const ITypedEventHandler_DispatcherQueueTimer_IInspectableVtbl tick_vtbl = {tick_QueryInterface, tick_AddRef, tick_Release, tick_Invoke};
static ITypedEventHandler_DispatcherQueueTimer_IInspectable tick_handler = {&tick_vtbl};

static void pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG msg;
    while (GetTickCount() < end)
    {
        int n = 0;
        /* a timer of no interval keeps the queue from ever being empty */
        while (n++ < 100 && PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        MsgWaitForMultipleObjects(0, NULL, FALSE, 5, QS_ALLINPUT);
    }
}

static void print_class(const char *what, void *obj)
{
    IInspectable *inspectable;
    HSTRING name = NULL;
    HRESULT hr;
    IUnknown_QueryInterface((IUnknown *)obj, &IID_IInspectable, (void **)&inspectable);
    hr = IInspectable_GetRuntimeClassName(inspectable, &name);
    printf("%s: class %#lx %ls\n", what, hr, name ? WindowsGetStringRawBuffer(name, NULL) : L"(null)");
    WindowsDeleteString(name);
    IInspectable_Release(inspectable);
}

static DWORD WINAPI other_thread(void *arg)
{
    IDispatcherQueueTimer *timer = arg;
    HRESULT hr;
    boolean running = 2;
    hr = IDispatcherQueueTimer_Start(timer);
    IDispatcherQueueTimer_get_IsRunning(timer, &running);
    printf("  Start from another thread %#lx running %d\n", hr, running);
    return 0;
}

int main(void)
{
    HRESULT (WINAPI *create)(DispatcherQueueOptions_, IUnknown **) =
        (void *)GetProcAddress(LoadLibraryW(L"coremessaging.dll"), "CreateDispatcherQueueController");
    DispatcherQueueOptions_ options = {sizeof(options), 2 /* DQTYPE_THREAD_CURRENT */, 0};
    IDispatcherQueueController *controller = NULL;
    IDispatcherQueue *queue = NULL;
    IDispatcherQueueTimer *timer = NULL, *timer2 = NULL;
    EventRegistrationToken token, token2;
    TimeSpan interval;
    boolean b;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    main_tid = GetCurrentThreadId();
    RoInitialize(RO_INIT_SINGLETHREADED);
    hr = create(options, (IUnknown **)&controller);
    printf("CreateDispatcherQueueController %#lx\n", hr);
    IDispatcherQueueController_get_DispatcherQueue(controller, &queue);
    hr = IDispatcherQueue_CreateTimer(queue, &timer);
    printf("CreateTimer %#lx\n", hr);
    if (!timer) return 1;
    print_class("timer", timer);
    hr = IDispatcherQueue_CreateTimer(queue, &timer2);
    printf("CreateTimer again %#lx same %d\n", hr, timer == timer2);
    interval.Duration = -1;
    IDispatcherQueueTimer_get_Interval(timer, &interval);
    printf("Interval %I64d\n", interval.Duration);
    b = 2; IDispatcherQueueTimer_get_IsRepeating(timer, &b); printf("IsRepeating %d\n", b);
    b = 2; IDispatcherQueueTimer_get_IsRunning(timer, &b); printf("IsRunning %d\n", b);
    hr = IDispatcherQueueTimer_add_Tick(timer, &tick_handler, &token);
    printf("add_Tick %#lx token %I64d\n", hr, token.value);
    hr = IDispatcherQueueTimer_add_Tick(timer, &tick_handler, &token2);
    printf("add_Tick again %#lx token %I64d\n", hr, token2.value);
    hr = IDispatcherQueueTimer_remove_Tick(timer, token2);
    printf("remove_Tick %#lx\n", hr);
    hr = IDispatcherQueueTimer_remove_Tick(timer, token2);
    printf("remove_Tick again %#lx\n", hr);

    /* repeating with no interval, Windows ticks without ever returning to the message loop */
    printf("zero interval, not repeating:\n");
    IDispatcherQueueTimer_put_IsRepeating(timer, FALSE);
    ticks = 0;
    hr = IDispatcherQueueTimer_Start(timer);
    b = 2; IDispatcherQueueTimer_get_IsRunning(timer, &b);
    printf("  Start %#lx running %d, ticks before pumping %ld\n", hr, b, ticks);
    pump(200);
    b = 2; IDispatcherQueueTimer_get_IsRunning(timer, &b);
    printf("  ticks in 200ms %ld running %d\n", ticks, b);
    hr = IDispatcherQueueTimer_Stop(timer);
    printf("  Stop %#lx\n", hr);
    hr = IDispatcherQueueTimer_Stop(timer);
    printf("  Stop again %#lx\n", hr);
    IDispatcherQueueTimer_put_IsRepeating(timer, TRUE);

    printf("50ms, repeating:\n");
    interval.Duration = 500000;
    hr = IDispatcherQueueTimer_put_Interval(timer, interval);
    printf("  put_Interval %#lx\n", hr);
    ticks = 0;
    IDispatcherQueueTimer_Start(timer);
    pump(520);
    printf("  ticks in 520ms %ld\n", ticks);
    IDispatcherQueueTimer_Stop(timer);

    printf("50ms, not repeating:\n");
    hr = IDispatcherQueueTimer_put_IsRepeating(timer, FALSE);
    printf("  put_IsRepeating %#lx\n", hr);
    ticks = 0;
    IDispatcherQueueTimer_Start(timer);
    pump(300);
    b = 2; IDispatcherQueueTimer_get_IsRunning(timer, &b);
    printf("  ticks in 300ms %ld running %d\n", ticks, b);
    IDispatcherQueueTimer_put_IsRepeating(timer, TRUE);

    printf("stopped from its own tick:\n");
    ticks = 0;
    stop_in_tick = timer;
    IDispatcherQueueTimer_Start(timer);
    pump(300);
    printf("  ticks in 300ms %ld\n", ticks);

    printf("started from another thread:\n");
    ticks = 0;
    WaitForSingleObject(CreateThread(NULL, 0, other_thread, timer, 0, NULL), INFINITE);
    pump(120);
    printf("  ticks in 120ms %ld\n", ticks);
    IDispatcherQueueTimer_Stop(timer);

    printf("negative interval:\n");
    interval.Duration = -10000;
    hr = IDispatcherQueueTimer_put_Interval(timer, interval);
    printf("  put_Interval(-1ms) %#lx\n", hr);
    IDispatcherQueueTimer_get_Interval(timer, &interval);
    printf("  Interval %I64d\n", interval.Duration);

    printf("released while running:\n");
    interval.Duration = 100000;
    IDispatcherQueueTimer_put_Interval(timer2, interval);
    IDispatcherQueueTimer_add_Tick(timer2, &tick_handler, &token2);
    ticks = 0;
    IDispatcherQueueTimer_Start(timer2);
    printf("  Release %lu\n", IDispatcherQueueTimer_Release(timer2));
    pump(100);
    printf("  ticks in 100ms %ld\n", ticks);

    printf("done\n");
    return 0;
}
