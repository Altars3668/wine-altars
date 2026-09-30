/* capturevisualprobe: Windows.Graphics.Capture of a composition visual, the way Office's AirSpace uses it.
 *
 * AirSpace (Mso40UIwin32client.dll) takes GraphicsCaptureItem.CreateFromVisual, reads the item's Size,
 * makes a Direct3D11CaptureFramePool with CreateFreeThreaded, a session, and reads the frame's texture;
 * nearly every step fails fast on an unexpected HRESULT.  Wine has no GraphicsCaptureItem, so AirSpace
 * falls back.  This prints what Windows does at each step: the item's name and size, the pool and
 * session properties, when frames arrive and on which thread, what the frame's surface is, its pixels,
 * whether a change to the visual brings a new frame, and what a pool made on a thread with a dispatcher
 * queue does differently.
 *
 *   scripts/build-probe.sh tools/capturevisualprobe/capturevisualprobe.c \
 *       tools/capturevisualprobe/capturevisualprobe.exe combase d3d11 dxgi user32 uuid dxguid
 * (WINE_BUILD=<tree>/build-wow64 for another tree).  Run in a desktop session on Windows.
 */
#define COBJMACROS
#include <stdarg.h>
#include <stdio.h>
#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "winstring.h"
#include "roapi.h"
#include "d3d11.h"
#include "dxgi1_2.h"
#include "initguid.h"
#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Numerics
#define WIDL_using_Windows_Graphics
#define WIDL_using_Windows_Graphics_Capture
#define WIDL_using_Windows_Graphics_DirectX
#define WIDL_using_Windows_Graphics_DirectX_Direct3D11
#define WIDL_using_Windows_System
#define WIDL_using_Windows_UI
#define WIDL_using_Windows_UI_Composition
#include "windows.foundation.h"
#include "windows.system.h"
#include "windows.ui.composition.h"
#include "windows.graphics.capture.h"
#include "windows.graphics.directx.direct3d11.h"
#include "windows.graphics.directx.direct3d11.interop.h"

typedef struct { DWORD dwSize; int threadType; int apartmentType; } DispatcherQueueOptions_;

static ID3D11Device *d3d;
static ID3D11DeviceContext *immediate;
static ICompositor *compositor;

static HSTRING S(const WCHAR *s)
{
    HSTRING h = NULL;
    WindowsCreateString(s, wcslen(s), &h);
    return h;
}

static void *factory(const WCHAR *name, REFIID iid)
{
    void *out = NULL;
    HSTRING str = S(name);
    HRESULT hr = RoGetActivationFactory(str, iid, &out);

    WindowsDeleteString(str);
    printf("RoGetActivationFactory(%ls) %#lx\n", name, hr);
    return out;
}

/* the FrameArrived handler: agile, counts, remembers the thread */
struct arrived
{
    ITypedEventHandler_Direct3D11CaptureFramePool_IInspectable iface;
    LONG count;
    DWORD thread;
    double first_ms;
};

static double now_ms(void)
{
    LARGE_INTEGER counter, frequency;

    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return counter.QuadPart * 1000.0 / frequency.QuadPart;
}

static HRESULT WINAPI arrived_QueryInterface(ITypedEventHandler_Direct3D11CaptureFramePool_IInspectable *iface,
        REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IAgileObject)
            || IsEqualGUID(iid, &IID_ITypedEventHandler_Direct3D11CaptureFramePool_IInspectable))
    {
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI arrived_AddRef(ITypedEventHandler_Direct3D11CaptureFramePool_IInspectable *iface) { return 2; }
static ULONG WINAPI arrived_Release(ITypedEventHandler_Direct3D11CaptureFramePool_IInspectable *iface) { return 1; }

static HRESULT WINAPI arrived_Invoke(ITypedEventHandler_Direct3D11CaptureFramePool_IInspectable *iface,
        IDirect3D11CaptureFramePool *sender, IInspectable *args)
{
    struct arrived *arrived = CONTAINING_RECORD(iface, struct arrived, iface);

    if (InterlockedIncrement(&arrived->count) == 1)
    {
        arrived->first_ms = now_ms();
        arrived->thread = GetCurrentThreadId();
        printf("  FrameArrived first: sender %s, args %s\n", sender ? "set" : "NULL", args ? "set" : "NULL");
    }
    return S_OK;
}

static const ITypedEventHandler_Direct3D11CaptureFramePool_IInspectableVtbl arrived_vtbl =
{
    arrived_QueryInterface,
    arrived_AddRef,
    arrived_Release,
    arrived_Invoke,
};

static void pump_until(LONG *count, LONG target, DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG msg;

    while ((int)(end - GetTickCount()) > 0 && *count < target)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        MsgWaitForMultipleObjects(0, NULL, FALSE, 5, QS_ALLINPUT);
    }
}

static void pump(DWORD ms)
{
    LONG never = 0;
    pump_until(&never, 1, ms);
}

static void show_frame(const char *what, IDirect3D11CaptureFramePool *pool)
{
    static const POINT points[] = {{1, 1}, {12, 12}, {40, 20}, {63, 31}};
    IDirect3DDxgiInterfaceAccess *access = NULL;
    IDirect3D11CaptureFrame *frame = NULL;
    Direct3DSurfaceDescription surface_desc;
    ID3D11Texture2D *texture = NULL, *staging;
    D3D11_MAPPED_SUBRESOURCE map;
    IDirect3DSurface *surface = NULL;
    D3D11_TEXTURE2D_DESC desc;
    SizeInt32 size = {-1, -1};
    TimeSpan time = {-1};
    HSTRING name = NULL;
    unsigned int i;
    HRESULT hr;

    hr = IDirect3D11CaptureFramePool_TryGetNextFrame(pool, &frame);
    printf("%s: TryGetNextFrame %#lx, frame %s\n", what, hr, frame ? "set" : "NULL");
    if (!frame) return;
    hr = IDirect3D11CaptureFrame_GetRuntimeClassName(frame, &name);
    printf("  frame class %#lx %ls\n", hr, name ? WindowsGetStringRawBuffer(name, NULL) : L"(none)");
    WindowsDeleteString(name);
    hr = IDirect3D11CaptureFrame_get_ContentSize(frame, &size);
    printf("  ContentSize %#lx %dx%d\n", hr, size.Width, size.Height);
    hr = IDirect3D11CaptureFrame_get_SystemRelativeTime(frame, &time);
    printf("  SystemRelativeTime %#lx %s\n", hr, time.Duration > 0 ? "positive" : time.Duration ? "negative" : "0");
    hr = IDirect3D11CaptureFrame_get_Surface(frame, &surface);
    printf("  Surface %#lx %s\n", hr, surface ? "set" : "NULL");
    if (surface)
    {
        name = NULL;
        IDirect3DSurface_GetRuntimeClassName(surface, &name);
        printf("  surface class %ls\n", name ? WindowsGetStringRawBuffer(name, NULL) : L"(none)");
        WindowsDeleteString(name);
        hr = IDirect3DSurface_get_Description(surface, &surface_desc);
        printf("  Description %#lx: %dx%d format %u samples %d/%d\n", hr, surface_desc.Width, surface_desc.Height,
               surface_desc.Format, surface_desc.MultisampleDescription.Count, surface_desc.MultisampleDescription.Quality);
        hr = IDirect3DSurface_QueryInterface(surface, &IID_IDirect3DDxgiInterfaceAccess, (void **)&access);
        if (SUCCEEDED(hr)) hr = IDirect3DDxgiInterfaceAccess_GetInterface(access, &IID_ID3D11Texture2D, (void **)&texture);
        printf("  GetInterface(ID3D11Texture2D) %#lx\n", hr);
        if (texture)
        {
            ID3D11Device *device;

            ID3D11Texture2D_GetDesc(texture, &desc);
            ID3D11Texture2D_GetDevice(texture, &device);
            printf("  texture %ux%u mips %u array %u format %#x samples %u usage %u bind %#x cpu %#x misc %#x, %s device\n",
                   desc.Width, desc.Height, desc.MipLevels, desc.ArraySize, desc.Format, desc.SampleDesc.Count,
                   desc.Usage, desc.BindFlags, desc.CPUAccessFlags, desc.MiscFlags, device == d3d ? "the" : "another");
            ID3D11Device_Release(device);
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.MiscFlags = 0;
            if (SUCCEEDED(ID3D11Device_CreateTexture2D(d3d, &desc, NULL, &staging)))
            {
                ID3D11DeviceContext_CopyResource(immediate, (ID3D11Resource *)staging, (ID3D11Resource *)texture);
                if (SUCCEEDED(ID3D11DeviceContext_Map(immediate, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map)))
                {
                    printf("  pixels:");
                    for (i = 0; i < ARRAY_SIZE(points); i++)
                    {
                        if (points[i].x >= (LONG)desc.Width || points[i].y >= (LONG)desc.Height) continue;
                        printf(" (%ld,%ld) %08lx", points[i].x, points[i].y,
                               ((DWORD *)((BYTE *)map.pData + map.RowPitch * points[i].y))[points[i].x]);
                    }
                    printf("\n");
                    ID3D11DeviceContext_Unmap(immediate, (ID3D11Resource *)staging, 0);
                }
                ID3D11Texture2D_Release(staging);
            }
            ID3D11Texture2D_Release(texture);
        }
        if (access) IDirect3DDxgiInterfaceAccess_Release(access);
        IDirect3DSurface_Release(surface);
    }
    {
        IClosable *closable;
        if (SUCCEEDED(IDirect3D11CaptureFrame_QueryInterface(frame, &IID_IClosable, (void **)&closable)))
        {
            hr = IClosable_Close(closable);
            printf("  frame Close %#lx\n", hr);
            IClosable_Release(closable);
        }
    }
    IDirect3D11CaptureFrame_Release(frame);
}

static ICompositionColorBrush *colour_brush(Color c)
{
    ICompositionColorBrush *brush = NULL;
    ICompositor_CreateColorBrushWithColor(compositor, c, &brush);
    return brush;
}

static ISpriteVisual *sprite(float w, float h, float x, float y, Color c, ICompositionColorBrush **out)
{
    Vector2 size = {w, h};
    Vector3 offset = {x, y, 0.0f};
    ICompositionColorBrush *brush = colour_brush(c);
    ICompositionBrush *cbrush;
    ISpriteVisual *visual = NULL;
    IVisual *v;

    ICompositor_CreateSpriteVisual(compositor, &visual);
    ISpriteVisual_QueryInterface(visual, &IID_IVisual, (void **)&v);
    IVisual_put_Size(v, size);
    IVisual_put_Offset(v, offset);
    IVisual_Release(v);
    ICompositionColorBrush_QueryInterface(brush, &IID_ICompositionBrush, (void **)&cbrush);
    ISpriteVisual_put_Brush(visual, cbrush);
    ICompositionBrush_Release(cbrush);
    if (out) *out = brush;
    else ICompositionColorBrush_Release(brush);
    return visual;
}

static void capture(IGraphicsCaptureItemStatics *items, IDirect3DDevice *device, BOOL free_threaded)
{
    static const Color red = {255, 255, 0, 0}, green = {255, 0, 255, 0}, blue = {255, 0, 0, 255};
    IDirect3D11CaptureFramePoolStatics2 *statics2 = NULL;
    IDirect3D11CaptureFramePoolStatics *statics = NULL;
    struct arrived arrived = {{&arrived_vtbl}};
    IDirect3D11CaptureFramePool *pool = NULL;
    ICompositionColorBrush *brush = NULL;
    IGraphicsCaptureSession *session = NULL;
    IGraphicsCaptureSession2 *session2;
    IGraphicsCaptureSession3 *session3;
    IVisualCollection *children = NULL;
    IGraphicsCaptureItem *item = NULL;
    ISpriteVisual *root, *child;
    IDispatcherQueue *queue = NULL;
    EventRegistrationToken token;
    IVisual *visual, *child_visual;
    SizeInt32 size = {0, 0};
    IContainerVisual *container;
    HSTRING name = NULL;
    boolean value;
    double start;
    HRESULT hr;

    printf("--- %s pool\n", free_threaded ? "free-threaded" : "dispatcher queue");
    root = sprite(64.0f, 32.0f, 0.0f, 0.0f, red, &brush);
    child = sprite(16.0f, 16.0f, 8.0f, 8.0f, green, NULL);
    ISpriteVisual_QueryInterface(root, &IID_IVisual, (void **)&visual);
    ISpriteVisual_QueryInterface(child, &IID_IVisual, (void **)&child_visual);
    ISpriteVisual_QueryInterface(root, &IID_IContainerVisual, (void **)&container);
    IContainerVisual_get_Children(container, &children);
    IVisualCollection_InsertAtTop(children, child_visual);

    hr = IGraphicsCaptureItemStatics_CreateFromVisual(items, visual, &item);
    printf("CreateFromVisual %#lx\n", hr);
    if (!item) goto done;
    hr = IGraphicsCaptureItem_GetRuntimeClassName(item, &name);
    printf("  item class %#lx %ls\n", hr, name ? WindowsGetStringRawBuffer(name, NULL) : L"(none)");
    WindowsDeleteString(name);
    name = NULL;
    hr = IGraphicsCaptureItem_get_DisplayName(item, &name);
    printf("  DisplayName %#lx \"%ls\"\n", hr, name ? WindowsGetStringRawBuffer(name, NULL) : L"");
    WindowsDeleteString(name);
    hr = IGraphicsCaptureItem_get_Size(item, &size);
    printf("  Size %#lx %dx%d\n", hr, size.Width, size.Height);

    if (free_threaded)
    {
        statics2 = factory(L"Windows.Graphics.Capture.Direct3D11CaptureFramePool", &IID_IDirect3D11CaptureFramePoolStatics2);
        if (!statics2) goto done;
        hr = IDirect3D11CaptureFramePoolStatics2_CreateFreeThreaded(statics2, device,
                DirectXPixelFormat_B8G8R8A8UIntNormalized, 1, size, &pool);
    }
    else
    {
        statics = factory(L"Windows.Graphics.Capture.Direct3D11CaptureFramePool", &IID_IDirect3D11CaptureFramePoolStatics);
        if (!statics) goto done;
        hr = IDirect3D11CaptureFramePoolStatics_Create(statics, device, DirectXPixelFormat_B8G8R8A8UIntNormalized, 1,
                size, &pool);
    }
    printf("create pool %#lx\n", hr);
    if (!pool) goto done;
    name = NULL;
    IDirect3D11CaptureFramePool_GetRuntimeClassName(pool, &name);
    printf("  pool class %ls\n", name ? WindowsGetStringRawBuffer(name, NULL) : L"(none)");
    WindowsDeleteString(name);
    hr = IDirect3D11CaptureFramePool_get_DispatcherQueue(pool, &queue);
    printf("  DispatcherQueue %#lx %s\n", hr, queue ? "set" : "NULL");
    if (queue) IDispatcherQueue_Release(queue);
    hr = IDirect3D11CaptureFramePool_add_FrameArrived(pool, &arrived.iface, &token);
    printf("  add_FrameArrived %#lx\n", hr);
    show_frame("before a session", pool);

    hr = IDirect3D11CaptureFramePool_CreateCaptureSession(pool, item, &session);
    printf("CreateCaptureSession %#lx\n", hr);
    if (!session) goto done;
    if (SUCCEEDED(IGraphicsCaptureSession_QueryInterface(session, &IID_IGraphicsCaptureSession2, (void **)&session2)))
    {
        value = 0xcc;
        hr = IGraphicsCaptureSession2_get_IsCursorCaptureEnabled(session2, &value);
        printf("  IsCursorCaptureEnabled %#lx %d\n", hr, value);
        IGraphicsCaptureSession2_Release(session2);
    }
    if (SUCCEEDED(IGraphicsCaptureSession_QueryInterface(session, &IID_IGraphicsCaptureSession3, (void **)&session3)))
    {
        value = 0xcc;
        hr = IGraphicsCaptureSession3_get_IsBorderRequired(session3, &value);
        printf("  IsBorderRequired %#lx %d\n", hr, value);
        IGraphicsCaptureSession3_Release(session3);
    }
    show_frame("before StartCapture", pool);

    start = now_ms();
    hr = IGraphicsCaptureSession_StartCapture(session);
    printf("StartCapture %#lx\n", hr);
    pump_until(&arrived.count, 1, 2000);
    printf("  frames %ld, first after %.0f ms on %s thread\n", arrived.count,
           arrived.count ? arrived.first_ms - start : -1.0,
           !arrived.count ? "no" : arrived.thread == GetCurrentThreadId() ? "this" : "another");
    hr = IGraphicsCaptureSession_StartCapture(session);
    printf("StartCapture again %#lx\n", hr);
    pump(300);
    printf("  frames after 300 ms more, nothing changed: %ld\n", arrived.count);
    show_frame("first", pool);
    show_frame("second at once", pool);

    /* the only buffer is back: does a change bring a frame? */
    arrived.count = 0;
    ICompositionColorBrush_put_Color(brush, blue);
    pump_until(&arrived.count, 1, 1000);
    printf("after the colour changed: frames %ld\n", arrived.count);
    show_frame("changed", pool);
    arrived.count = 0;
    pump(300);
    printf("  frames after 300 ms more, nothing changed: %ld\n", arrived.count);

    hr = IDirect3D11CaptureFramePool_remove_FrameArrived(pool, token);
    printf("remove_FrameArrived %#lx\n", hr);
    {
        IClosable *closable;
        if (SUCCEEDED(IGraphicsCaptureSession_QueryInterface(session, &IID_IClosable, (void **)&closable)))
        {
            hr = IClosable_Close(closable);
            printf("session Close %#lx\n", hr);
            IClosable_Release(closable);
        }
        if (SUCCEEDED(IDirect3D11CaptureFramePool_QueryInterface(pool, &IID_IClosable, (void **)&closable)))
        {
            IDirect3D11CaptureFrame *frame = NULL;

            hr = IClosable_Close(closable);
            printf("pool Close %#lx\n", hr);
            IClosable_Release(closable);
            hr = IDirect3D11CaptureFramePool_TryGetNextFrame(pool, &frame);
            printf("TryGetNextFrame after Close %#lx, %s\n", hr, frame ? "a frame" : "NULL");
            if (frame) IDirect3D11CaptureFrame_Release(frame);
        }
    }

done:
    if (session) IGraphicsCaptureSession_Release(session);
    if (pool) IDirect3D11CaptureFramePool_Release(pool);
    if (statics) IDirect3D11CaptureFramePoolStatics_Release(statics);
    if (statics2) IDirect3D11CaptureFramePoolStatics2_Release(statics2);
    if (item) IGraphicsCaptureItem_Release(item);
    if (brush) ICompositionColorBrush_Release(brush);
    IVisualCollection_Release(children);
    IContainerVisual_Release(container);
    IVisual_Release(child_visual);
    IVisual_Release(visual);
    ISpriteVisual_Release(child);
    ISpriteVisual_Release(root);
}

int main(void)
{
    HRESULT (WINAPI *from_device)(IDXGIDevice *, IInspectable **);
    HRESULT (WINAPI *from_surface)(IDXGISurface *, IInspectable **);
    IGraphicsCaptureSessionStatics *session_statics;
    IGraphicsCaptureItemStatics *items;
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    IInspectable *inspectable = NULL;
    IDirect3DDevice *device = NULL;
    IGraphicsCaptureItem *item;
    ISpriteVisual *empty;
    IDXGIDevice *dxgi;
    HSTRING name = NULL;
    SizeInt32 size;
    boolean value;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    RoInitialize(RO_INIT_SINGLETHREADED);
    {
        HRESULT (WINAPI *create)(DispatcherQueueOptions_, IUnknown **) =
            (void *)GetProcAddress(LoadLibraryW(L"coremessaging.dll"), "CreateDispatcherQueueController");
        DispatcherQueueOptions_ options = {sizeof(options), 2 /* DQTYPE_THREAD_CURRENT */, 2 /* DQTAT_COM_STA */};
        IUnknown *queue = NULL;

        hr = create ? create(options, &queue) : E_NOTIMPL;
        printf("CreateDispatcherQueueController %#lx\n", hr);
    }
    {
        HSTRING str = S(L"Windows.UI.Composition.Compositor");
        hr = RoActivateInstance(str, &inspectable);
        WindowsDeleteString(str);
        printf("activate Compositor %#lx\n", hr);
        if (FAILED(hr)) return 1;
        IInspectable_QueryInterface(inspectable, &IID_ICompositor, (void **)&compositor);
        IInspectable_Release(inspectable);
    }

    session_statics = factory(L"Windows.Graphics.Capture.GraphicsCaptureSession", &IID_IGraphicsCaptureSessionStatics);
    if (session_statics)
    {
        value = 0xcc;
        hr = IGraphicsCaptureSessionStatics_IsSupported(session_statics, &value);
        printf("IsSupported %#lx %d\n", hr, value);
        IGraphicsCaptureSessionStatics_Release(session_statics);
    }
    items = factory(L"Windows.Graphics.Capture.GraphicsCaptureItem", &IID_IGraphicsCaptureItemStatics);
    if (!items) return 1;
    item = NULL;
    hr = IGraphicsCaptureItemStatics_CreateFromVisual(items, NULL, &item);
    printf("CreateFromVisual(NULL) %#lx %s\n", hr, item ? "item" : "NULL");
    if (item) IGraphicsCaptureItem_Release(item);
    {
        static const Color white = {255, 255, 255, 255};
        IVisual *v;

        empty = sprite(0.0f, 0.0f, 0.0f, 0.0f, white, NULL);
        ISpriteVisual_QueryInterface(empty, &IID_IVisual, (void **)&v);
        item = NULL;
        hr = IGraphicsCaptureItemStatics_CreateFromVisual(items, v, &item);
        size.Width = size.Height = -1;
        if (item) IGraphicsCaptureItem_get_Size(item, &size);
        printf("CreateFromVisual(a visual of no size) %#lx, size %dx%d\n", hr, size.Width, size.Height);
        if (item) IGraphicsCaptureItem_Release(item);
        IVisual_Release(v);
        ISpriteVisual_Release(empty);
    }

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                           D3D11_SDK_VERSION, &d3d, &level, &immediate);
    if (FAILED(hr))
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                               D3D11_SDK_VERSION, &d3d, &level, &immediate);
    printf("D3D11CreateDevice %#lx\n", hr);
    if (FAILED(hr)) return 1;
    ID3D11Device_QueryInterface(d3d, &IID_IDXGIDevice, (void **)&dxgi);
    from_device = (void *)GetProcAddress(GetModuleHandleW(L"d3d11.dll"), "CreateDirect3D11DeviceFromDXGIDevice");
    from_surface = (void *)GetProcAddress(GetModuleHandleW(L"d3d11.dll"), "CreateDirect3D11SurfaceFromDXGISurface");
    printf("exports: device %s, surface %s\n", from_device ? "yes" : "no", from_surface ? "yes" : "no");
    if (!from_device) return 1;
    hr = from_device(dxgi, &inspectable);
    printf("CreateDirect3D11DeviceFromDXGIDevice %#lx\n", hr);
    if (FAILED(hr)) return 1;
    IInspectable_GetRuntimeClassName(inspectable, &name);
    printf("  device class %ls\n", name ? WindowsGetStringRawBuffer(name, NULL) : L"(none)");
    WindowsDeleteString(name);
    hr = IInspectable_QueryInterface(inspectable, &IID_IDirect3DDevice, (void **)&device);
    printf("  QI IDirect3DDevice %#lx\n", hr);
    {
        IDirect3DDxgiInterfaceAccess *access;
        IDXGIDevice *back = NULL;

        hr = IInspectable_QueryInterface(inspectable, &IID_IDirect3DDxgiInterfaceAccess, (void **)&access);
        if (SUCCEEDED(hr))
        {
            hr = IDirect3DDxgiInterfaceAccess_GetInterface(access, &IID_IDXGIDevice, (void **)&back);
            IDirect3DDxgiInterfaceAccess_Release(access);
        }
        printf("  GetInterface(IDXGIDevice) %#lx, %s\n", hr, back == dxgi ? "the same" : back ? "another" : "NULL");
        if (back) IDXGIDevice_Release(back);
    }
    IInspectable_Release(inspectable);

    capture(items, device, TRUE);
    capture(items, device, FALSE);

    IDirect3DDevice_Release(device);
    IGraphicsCaptureItemStatics_Release(items);
    IDXGIDevice_Release(dxgi);
    ID3D11DeviceContext_Release(immediate);
    ID3D11Device_Release(d3d);
    ICompositor_Release(compositor);
    printf("done\n");
    return 0;
}
