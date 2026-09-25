/* probe_comp: Windows.UI.Composition as a desktop application without a dispatcher queue meets it,
 * the way Office's AirSpace does: through CompositorController, a desktop window target, a sprite
 * with a color brush, a manual commit, and the pixels that come out.  Needs a desktop session
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
#include "winternl.h"
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

static void pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG msg;
    while (GetTickCount() < end)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        Sleep(10);
    }
}

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
    /* PROBE_PAUSE=ms holds the picture for a capture from outside */
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

/* the ways a thread can be when it asks */
typedef struct { DWORD dwSize; int threadType; int apartmentType; } DispatcherQueueOptions_;
static void try_activation(const char *what)
{
    IActivationFactory *factory = NULL;
    IInspectable *inspectable = NULL;
    HSTRING str = S(L"Windows.UI.Composition.Core.CompositorController");
    HRESULT hr = RoGetActivationFactory(str, &IID_IActivationFactory, (void **)&factory);
    printf("%s: factory %#lx", what, hr);
    if (factory)
    {
        hr = IActivationFactory_ActivateInstance(factory, &inspectable);
        printf(", instance %#lx", hr);
        if (inspectable) IInspectable_Release(inspectable);
        IActivationFactory_Release(factory);
    }
    printf("\n");
    WindowsDeleteString(str);
    str = S(L"Windows.UI.Composition.Compositor");
    hr = RoActivateInstance(str, &inspectable);
    printf("%s: Compositor %#lx\n", what, hr);
    if (inspectable) IInspectable_Release(inspectable);
    WindowsDeleteString(str);
}

static DWORD WINAPI mta_thread(void *arg)
{
    RoInitialize(RO_INIT_MULTITHREADED);
    try_activation("mta");
    RoUninitialize();
    return 0;
}

static DWORD WINAPI queue_thread(void *arg)
{
    HRESULT (WINAPI *create)(DispatcherQueueOptions_, IUnknown **) =
        (void *)GetProcAddress(LoadLibraryW(L"coremessaging.dll"), "CreateDispatcherQueueController");
    DispatcherQueueOptions_ options = {sizeof(options), 2 /* DQTYPE_THREAD_CURRENT */, 2 /* DQTAT_COM_STA */};
    IUnknown *queue = NULL;
    HRESULT hr;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    hr = create(options, &queue);
    printf("CreateDispatcherQueueController %#lx\n", hr);
    try_activation("sta with a dispatcher queue");
    if (queue) IUnknown_Release(queue);
    CoUninitialize();
    return 0;
}

static void environment(void)
{
    WCHAR station[64] = {0}, desktop[64] = {0};
    DWORD len, level = 0;
    HANDLE token;
    char buf[64];
    HRESULT (WINAPI *create_device)(IUnknown *, REFIID, void **) =
        (void *)GetProcAddress(LoadLibraryW(L"dcomp.dll"), "DCompositionCreateDevice2");
    static const GUID IID_IDCompositionDevice_ = {0xc37ea93a,0xe7aa,0x450d,{0xb1,0x6f,0x97,0x46,0xcb,0x04,0x07,0xf3}};
    static const GUID IID_IDCompositionDesktopDevice_ = {0x5f4633fe,0x1e08,0x4cb8,{0x8c,0x75,0xce,0x24,0x33,0x3f,0x56,0x02}};
    IUnknown *device = NULL;
    HRESULT hr;

    GetUserObjectInformationW(GetProcessWindowStation(), UOI_NAME, station, sizeof(station), &len);
    GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()), UOI_NAME, desktop, sizeof(desktop), &len);
    printf("window station %ls, desktop %ls\n", station, desktop);
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        if (GetTokenInformation(token, TokenIntegrityLevel, buf, sizeof(buf), &len))
        {
            TOKEN_MANDATORY_LABEL *label = (void *)buf;
            level = *GetSidSubAuthority(label->Label.Sid, *GetSidSubAuthorityCount(label->Label.Sid) - 1);
        }
        CloseHandle(token);
    }
    printf("integrity %#lx, session %lu\n", level, (DWORD)NtCurrentTeb()->Peb->SessionId);
    hr = create_device(NULL, &IID_IDCompositionDesktopDevice_, (void **)&device);
    printf("DCompositionCreateDevice2(desktop device) %#lx\n", hr);
    if (device) IUnknown_Release(device);
    device = NULL;
    hr = create_device(NULL, &IID_IDCompositionDevice_, (void **)&device);
    printf("DCompositionCreateDevice2(IDCompositionDevice) %#lx\n", hr);
    if (device) IUnknown_Release(device);
}

int main(void)
{
    WNDCLASSW cls = {0};
    ICompositorController *controller = NULL;
    ICompositorDesktopInterop *desktop;
    ICompositor *compositor = NULL;
    ICompositionObject *object;
    IDesktopWindowTarget *target = NULL, *target2 = NULL;
    ICompositionTarget *ctarget;
    IContainerVisual *container = NULL;
    ISpriteVisual *sprite = NULL;
    ICompositionColorBrush *color = NULL;
    ICompositionBrush *brush;
    IVisual *visual, *sprite_visual;
    IVisualCollection *children;
    IInspectable *inspectable;
    ICoreDispatcher *dispatcher = (void *)0xdeadbeef;
    Vector2 v2;
    Vector3 v3;
    FLOAT f;
    boolean b;
    Color c;
    HSTRING str;
    HWND hwnd;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("SetProcessDpiAwarenessContext(PMv2) %d\n", SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
    environment();
    WaitForSingleObject(CreateThread(NULL, 0, mta_thread, NULL, 0, NULL), INFINITE);
    WaitForSingleObject(CreateThread(NULL, 0, queue_thread, NULL, 0, NULL), INFINITE);
    RoInitialize(RO_INIT_SINGLETHREADED);
    try_activation("sta");
    {
        WNDCLASSW early = {0};
        HWND w;
        early.lpfnWndProc = wndproc;
        early.hInstance = GetModuleHandleW(NULL);
        early.lpszClassName = L"probe_comp_early";
        RegisterClassW(&early);
        w = CreateWindowExW(0, L"probe_comp_early", L"early", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, 200, 100, NULL, NULL, NULL, NULL);
        pump(300);
        try_activation("sta with a visible window");
        DestroyWindow(w);
    }

    {
        HRESULT (WINAPI *create)(DispatcherQueueOptions_, IUnknown **) =
            (void *)GetProcAddress(LoadLibraryW(L"coremessaging.dll"), "CreateDispatcherQueueController");
        DispatcherQueueOptions_ options = {sizeof(options), 2 /* DQTYPE_THREAD_CURRENT */, 0 /* DQTAT_COM_NONE */};
        IUnknown *queue = NULL;
        hr = create(options, &queue);
        printf("main thread CreateDispatcherQueueController %#lx\n", hr);
    }
    str = S(L"Windows.UI.Composition.Core.CompositorController");
    hr = RoActivateInstance(str, &inspectable);
    WindowsDeleteString(str);
    printf("activate CompositorController %#lx\n", hr);
    if (FAILED(hr)) return 1;
    print_class("controller", inspectable);
    IInspectable_QueryInterface(inspectable, &IID_ICompositorController, (void **)&controller);
    QI("controller", inspectable, IAgileObject);
    QI("controller", inspectable, IClosable);
    IInspectable_Release(inspectable);

    hr = ICompositorController_get_Compositor(controller, &compositor);
    printf("get_Compositor %#lx\n", hr);
    print_class("compositor", compositor);
    QI("compositor", compositor, ICompositor2);
    QI("compositor", compositor, ICompositor3);
    QI("compositor", compositor, ICompositor4);
    QI("compositor", compositor, ICompositor5);
    QI("compositor", compositor, ICompositor6);
    QI("compositor", compositor, ICompositor7);
    QI("compositor", compositor, ICompositorInterop);
    QI("compositor", compositor, ICompositorDesktopInterop);
    QI("compositor", compositor, ICompositionObject);
    QI("compositor", compositor, IAgileObject);
    QI("compositor", compositor, IClosable);
    {
        ICompositor *again = NULL;
        ICompositorController_get_Compositor(controller, &again);
        printf("  same compositor %d\n", again == compositor);
        if (again) ICompositor_Release(again);
    }
    hr = ICompositor_QueryInterface(compositor, &IID_ICompositionObject, (void **)&object);
    if (SUCCEEDED(hr))
    {
        hr = ICompositionObject_get_Dispatcher(object, &dispatcher);
        printf("  compositor Dispatcher %#lx %p\n", hr, dispatcher);
        ICompositionObject_Release(object);
    }
    hr = ICompositorController_Commit(controller);
    printf("empty Commit %#lx\n", hr);

    cls.lpfnWndProc = wndproc;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.lpszClassName = L"probe_comp";
    cls.hbrBackground = GetStockObject(WHITE_BRUSH);
    RegisterClassW(&cls);
    hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, L"probe_comp", L"probe_comp", WS_POPUP | WS_VISIBLE,
                           100, 100, 300, 200, NULL, NULL, NULL, NULL);
    pump(200);

    hr = ICompositor_QueryInterface(compositor, &IID_ICompositorDesktopInterop, (void **)&desktop);
    printf("ICompositorDesktopInterop %#lx\n", hr);
    hr = ICompositorDesktopInterop_CreateDesktopWindowTarget(desktop, hwnd, FALSE, &target);
    printf("CreateDesktopWindowTarget %#lx\n", hr);
    print_class("target", target);
    hr = ICompositorDesktopInterop_CreateDesktopWindowTarget(desktop, hwnd, FALSE, &target2);
    printf("CreateDesktopWindowTarget again %#lx %p\n", hr, target2);
    hr = ICompositorDesktopInterop_CreateDesktopWindowTarget(desktop, NULL, FALSE, &target2);
    printf("CreateDesktopWindowTarget(NULL) %#lx\n", hr);
    if (target)
    {
        b = 2;
        IDesktopWindowTarget_get_IsTopmost(target, &b);
        printf("  IsTopmost %d\n", b);
        QI("target", target, ICompositionTarget);
        QI("target", target, IDesktopWindowTargetInterop);
        QI("target", target, ICompositionObject);
    }

    hr = ICompositor_CreateContainerVisual(compositor, &container);
    printf("CreateContainerVisual %#lx\n", hr);
    print_class("container", container);
    IContainerVisual_QueryInterface(container, &IID_IVisual, (void **)&visual);
    IVisual_get_Size(visual, &v2);
    printf("  Size %g %g\n", v2.X, v2.Y);
    IVisual_get_Offset(visual, &v3);
    printf("  Offset %g %g %g\n", v3.X, v3.Y, v3.Z);
    IVisual_get_Opacity(visual, &f);
    printf("  Opacity %g\n", f);
    IVisual_get_IsVisible(visual, &b);
    printf("  IsVisible %d\n", b);
    IVisual_get_Scale(visual, &v3);
    printf("  Scale %g %g %g\n", v3.X, v3.Y, v3.Z);
    IVisual_get_AnchorPoint(visual, &v2);
    printf("  AnchorPoint %g %g\n", v2.X, v2.Y);
    IVisual_get_RotationAngle(visual, &f);
    printf("  RotationAngle %g\n", f);
    v2.X = 300; v2.Y = 200;
    IVisual_put_Size(visual, v2);

    hr = ICompositor_CreateSpriteVisual(compositor, &sprite);
    printf("CreateSpriteVisual %#lx\n", hr);
    ISpriteVisual_QueryInterface(sprite, &IID_IVisual, (void **)&sprite_visual);
    v2.X = 100; v2.Y = 100;
    IVisual_put_Size(sprite_visual, v2);
    v3.X = 10; v3.Y = 10; v3.Z = 0;
    IVisual_put_Offset(sprite_visual, v3);
    c.A = 255; c.R = 255; c.G = 0; c.B = 0;
    hr = ICompositor_CreateColorBrushWithColor(compositor, c, &color);
    printf("CreateColorBrushWithColor %#lx\n", hr);
    print_class("color brush", color);
    ICompositionColorBrush_QueryInterface(color, &IID_ICompositionBrush, (void **)&brush);
    hr = ISpriteVisual_put_Brush(sprite, brush);
    printf("put_Brush %#lx\n", hr);

    IContainerVisual_get_Children(container, &children);
    hr = IVisualCollection_InsertAtTop(children, sprite_visual);
    printf("InsertAtTop %#lx\n", hr);
    {
        INT32 count = -1;
        IVisualCollection_get_Count(children, &count);
        printf("  children %d\n", count);
    }

    IDesktopWindowTarget_QueryInterface(target, &IID_ICompositionTarget, (void **)&ctarget);
    hr = ICompositionTarget_put_Root(ctarget, visual);
    printf("put_Root %#lx\n", hr);

    read_pixels(hwnd, "before Commit");
    hr = ICompositorController_Commit(controller);
    printf("Commit %#lx\n", hr);
    pump(300);
    read_pixels(hwnd, "after Commit");

    /* changes without a commit */
    v3.X = 150; v3.Y = 50;
    IVisual_put_Offset(sprite_visual, v3);
    pump(300);
    read_pixels(hwnd, "moved, no Commit");
    ICompositorController_Commit(controller);
    pump(300);
    read_pixels(hwnd, "moved, Commit");

    /* a redirect visual: where does it draw what its source draws */
    {
        ICompositor6 *compositor6;
        IRedirectVisual *redirect = NULL;
        IVisual *redirect_visual, *source_visual;
        ISpriteVisual *source;
        IContainerVisual *holder;
        IVisual *holder_visual;
        IVisual *got = (void *)0xdeadbeef;

        if (SUCCEEDED(ICompositor_QueryInterface(compositor, &IID_ICompositor6, (void **)&compositor6)))
        {
            /* the source, green at (20,20) in a container at (0,0), itself not shown */
            ICompositor_CreateSpriteVisual(compositor, &source);
            ISpriteVisual_QueryInterface(source, &IID_IVisual, (void **)&source_visual);
            v2.X = 40; v2.Y = 40;
            IVisual_put_Size(source_visual, v2);
            v3.X = 20; v3.Y = 20; v3.Z = 0;
            IVisual_put_Offset(source_visual, v3);
            c.A = 255; c.R = 0; c.G = 255; c.B = 0;
            {
                ICompositionColorBrush *green;
                ICompositionBrush *green_brush;
                ICompositor_CreateColorBrushWithColor(compositor, c, &green);
                ICompositionColorBrush_QueryInterface(green, &IID_ICompositionBrush, (void **)&green_brush);
                ISpriteVisual_put_Brush(source, green_brush);
            }

            hr = ICompositor6_CreateRedirectVisual(compositor6, &redirect);
            printf("CreateRedirectVisual %#lx\n", hr);
            print_class("redirect", redirect);
            QI("redirect", redirect, IContainerVisual);
            QI("redirect", redirect, ISpriteVisual);
            QI("redirect", redirect, IVisual);
            IRedirectVisual_get_Source(redirect, &got);
            printf("  Source %p\n", got);
            IRedirectVisual_QueryInterface(redirect, &IID_IVisual, (void **)&redirect_visual);
            v2.X = 100; v2.Y = 100;
            IVisual_put_Size(redirect_visual, v2);
            v3.X = 150; v3.Y = 50; v3.Z = 0;
            IVisual_put_Offset(redirect_visual, v3);
            hr = IRedirectVisual_put_Source(redirect, source_visual);
            printf("  put_Source %#lx\n", hr);
            hr = IRedirectVisual_put_Source(redirect, visual);
            printf("  put_Source(its own ancestor) %#lx\n", hr);
            IRedirectVisual_put_Source(redirect, source_visual);

            /* the sprite moved out of the way; the source sits in a hidden holder; the redirect in the root */
            v3.X = 500; v3.Y = 500;
            IVisual_put_Offset(sprite_visual, v3);
            ICompositor_CreateContainerVisual(compositor, &holder);
            IContainerVisual_QueryInterface(holder, &IID_IVisual, (void **)&holder_visual);
            v3.X = 0; v3.Y = 150;
            IVisual_put_Offset(holder_visual, v3);
            {
                IVisualCollection *holder_children;
                IContainerVisual_get_Children(holder, &holder_children);
                IVisualCollection_InsertAtTop(holder_children, source_visual);
            }
            IVisualCollection_InsertAtTop(children, holder_visual);
            IVisualCollection_InsertAtTop(children, redirect_visual);
            ICompositorController_Commit(controller);
            pump(300);
            {
                static const POINT points[] = {{25, 175}, {175, 75}, {155, 55}, {195, 95}, {205, 105}, {150, 50}};
                HDC hdc = GetDC(hwnd), mem = CreateCompatibleDC(hdc);
                HBITMAP bmp = CreateCompatibleBitmap(hdc, 300, 200);
                int i;
                SelectObject(mem, bmp);
                printf("redirect: PrintWindow %d\n", PrintWindow(hwnd, mem, 2));
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
            ICompositor6_Release(compositor6);
        }
    }

    printf("done\n");
    return 0;
}
