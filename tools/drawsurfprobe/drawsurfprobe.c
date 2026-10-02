/* drawsurfprobe: the drawing state of Windows.UI.Composition drawing surfaces, the way Office's
 * AirSpace drives them from two threads.  AirSpace's render thread begins a draw on a surface,
 * suspends it, begins again on the same surface with another update rectangle (several times), and
 * its UI thread ends the draw later; Wine answered every BeginDraw after the suspension with
 * DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED.  Each sequence below prints every call's HRESULT, and
 * the colour each update filled is read back with CopySurface, to show which of the updates land.
 *
 *   scripts/build-probe.sh tools/drawsurfprobe/drawsurfprobe.c tools/drawsurfprobe/drawsurfprobe.exe \
 *       combase d3d11 d2d1 dxgi uuid
 * (WINE_BUILD=<tree>/build-wow64 for another tree).  Run in a desktop session on Windows.
 */
#define COBJMACROS
#include <stdarg.h>
#include <stdio.h>
#include "windef.h"
#include "initguid.h"
#include "winbase.h"
#include "winstring.h"
#include "roapi.h"
#include "d3d11.h"
#include "d2d1_1.h"
#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Numerics
#define WIDL_using_Windows_Graphics
#define WIDL_using_Windows_Graphics_DirectX
#define WIDL_using_Windows_UI
#define WIDL_using_Windows_UI_Composition
#define WIDL_using_Windows_UI_Composition_Core
#include "windows.foundation.h"
#include "windows.ui.composition.h"
#include "windows.ui.composition.interop.h"

typedef ICompositionDrawingSurfaceInterop2 surface_t;
typedef struct { DWORD dwSize; int threadType; int apartmentType; } DispatcherQueueOptions_;

static ICompositionGraphicsDevice *graphics;
static ID3D11Device *d3d;
static ID3D11DeviceContext *immediate;

static HSTRING S(const WCHAR *s)
{
    HSTRING h = NULL;
    WindowsCreateString(s, wcslen(s), &h);
    return h;
}

static surface_t *make_surface(int width, int height)
{
    ICompositionDrawingSurface *surface = NULL;
    surface_t *interop = NULL;
    Size size = {width, height};
    HRESULT hr;

    hr = ICompositionGraphicsDevice_CreateDrawingSurface(graphics, size, DirectXPixelFormat_B8G8R8A8UIntNormalized,
            DirectXAlphaMode_Premultiplied, &surface);
    if (FAILED(hr)) { printf("CreateDrawingSurface %#lx\n", hr); return NULL; }
    hr = ICompositionDrawingSurface_QueryInterface(surface, &IID_ICompositionDrawingSurfaceInterop2, (void **)&interop);
    if (FAILED(hr)) printf("QI ICompositionDrawingSurfaceInterop2 %#lx\n", hr);
    ICompositionDrawingSurface_Release(surface);
    /* a new surface's first draw has to cover all of it: clear it, so the sequences start from a drawn surface */
    if (interop)
    {
        ID2D1DeviceContext *dc = NULL;
        D2D1_COLOR_F clear = {0.0f, 0.0f, 0.0f, 0.0f};
        POINT offset;

        hr = ICompositionDrawingSurfaceInterop2_BeginDraw(interop, NULL, &IID_ID2D1DeviceContext, (void **)&dc, &offset);
        if (SUCCEEDED(hr))
        {
            ID2D1DeviceContext_Clear(dc, &clear);
            ID2D1DeviceContext_Release(dc);
            hr = ICompositionDrawingSurfaceInterop2_EndDraw(interop);
        }
        if (FAILED(hr)) printf("first draw of the whole surface %#lx\n", hr);
    }
    return interop;
}

static const char *name_of(DWORD color)
{
    switch (color)
    {
    case 0xffff0000: return "red";
    case 0xff00ff00: return "green";
    case 0xff0000ff: return "blue";
    case 0xffffff00: return "yellow";
    case 0: return "clear";
    default: return "other";
    }
}

/* begins a draw and, when it succeeds, fills the update rectangle with a colour */
static HRESULT begin(const char *what, surface_t *s, int l, int t, int r, int b, DWORD color)
{
    ID2D1DeviceContext *dc = NULL;
    ID2D1SolidColorBrush *brush;
    RECT rect = {l, t, r, b};
    POINT offset = {-1, -1};
    D2D1_COLOR_F c = {((color >> 16) & 0xff) / 255.0f, ((color >> 8) & 0xff) / 255.0f, (color & 0xff) / 255.0f, 1.0f};
    D2D1_RECT_F fill;
    HRESULT hr;

    hr = ICompositionDrawingSurfaceInterop2_BeginDraw(s, &rect, &IID_ID2D1DeviceContext, (void **)&dc, &offset);
    printf("  %s BeginDraw (%d,%d)-(%d,%d): %#lx", what, l, t, r, b, hr);
    if (SUCCEEDED(hr))
    {
        printf(" offset %ld,%ld", offset.x, offset.y);
        fill.left = offset.x; fill.top = offset.y;
        fill.right = offset.x + (r - l); fill.bottom = offset.y + (b - t);
        ID2D1DeviceContext_CreateSolidColorBrush(dc, &c, NULL, &brush);
        ID2D1DeviceContext_FillRectangle(dc, &fill, (ID2D1Brush *)brush);
        ID2D1SolidColorBrush_Release(brush);
        ID2D1DeviceContext_Release(dc);
        printf(" filled %s", name_of(color));
    }
    printf("\n");
    return hr;
}

enum op { OP_END, OP_SUSPEND, OP_RESUME, OP_RESIZE, OP_SCROLL };

static HRESULT do_op(enum op op, surface_t *s)
{
    SIZE size = {128, 64};
    RECT scroll = {0, 0, 64, 32};

    switch (op)
    {
    case OP_END: return ICompositionDrawingSurfaceInterop2_EndDraw(s);
    case OP_SUSPEND: return ICompositionDrawingSurfaceInterop2_SuspendDraw(s);
    case OP_RESUME: return ICompositionDrawingSurfaceInterop2_ResumeDraw(s);
    case OP_RESIZE: return ICompositionDrawingSurfaceInterop2_Resize(s, size);
    case OP_SCROLL: return ICompositionDrawingSurfaceInterop2_Scroll(s, &scroll, NULL, 1, 0);
    }
    return E_FAIL;
}

static const char *op_names[] = { "EndDraw", "SuspendDraw", "ResumeDraw", "Resize", "Scroll" };

static void call(const char *what, enum op op, surface_t *s)
{
    printf("  %s %s: %#lx\n", what, op_names[op], do_op(op, s));
}

struct thread_call { enum op op; surface_t *s; HRESULT hr; };

static DWORD WINAPI thread_proc(void *arg)
{
    struct thread_call *c = arg;

    RoInitialize(RO_INIT_MULTITHREADED);
    c->hr = do_op(c->op, c->s);
    RoUninitialize();
    return 0;
}

/* the call from another thread, as AirSpace's UI thread ends what its render thread began */
static void call_other_thread(const char *what, enum op op, surface_t *s)
{
    struct thread_call c = {op, s, E_FAIL};
    HANDLE thread = CreateThread(NULL, 0, thread_proc, &c, 0, NULL);

    if (WaitForSingleObject(thread, 5000) == WAIT_TIMEOUT)
        printf("  %s %s on another thread: still waiting after 5 s\n", what, op_names[op]);
    else
        printf("  %s %s on another thread: %#lx\n", what, op_names[op], c.hr);
    CloseHandle(thread);
}

static DWORD read_pixel(surface_t *s, int x, int y)
{
    D3D11_TEXTURE2D_DESC desc = {1, 1, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT,
                                 D3D11_BIND_SHADER_RESOURCE, 0, 0};
    D3D11_MAPPED_SUBRESOURCE map;
    ID3D11Texture2D *texture, *staging;
    RECT rect = {x, y, x + 1, y + 1};
    DWORD value = 0xdeadbeef;
    HRESULT hr;

    if (FAILED(ID3D11Device_CreateTexture2D(d3d, &desc, NULL, &texture))) return value;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Device_CreateTexture2D(d3d, &desc, NULL, &staging);
    hr = ICompositionDrawingSurfaceInterop2_CopySurface(s, (IUnknown *)texture, 0, 0, &rect);
    if (SUCCEEDED(hr))
    {
        ID3D11DeviceContext_CopyResource(immediate, (ID3D11Resource *)staging, (ID3D11Resource *)texture);
        if (SUCCEEDED(ID3D11DeviceContext_Map(immediate, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map)))
        {
            value = *(DWORD *)map.pData;
            ID3D11DeviceContext_Unmap(immediate, (ID3D11Resource *)staging, 0);
        }
    }
    else value = hr;
    ID3D11Texture2D_Release(staging);
    ID3D11Texture2D_Release(texture);
    return value;
}

static void pixels(surface_t *s, const POINT *points, int count)
{
    int i;

    for (i = 0; i < count; i++)
    {
        DWORD v = read_pixel(s, points[i].x, points[i].y);
        printf("    pixel (%ld,%ld) %08lx %s\n", points[i].x, points[i].y, v, name_of(v));
    }
}

/* what BeginDraw takes at all: the size a surface got, no update rectangle, a DXGI surface, and a graphics
 * device made from the Direct3D device rather than the Direct2D one */
static void diagnose_surface(const char *what, ICompositionGraphicsDevice *device)
{
    ICompositionDrawingSurface *surface = NULL;
    ICompositionDrawingSurface2 *surface2;
    surface_t *s = NULL;
    Size size = {128, 64}, got = {-1, -1};
    SizeInt32 got32 = {-1, -1};
    RECT rect = {0, 0, 32, 32};
    POINT offset = {-1, -1};
    IUnknown *object;
    HRESULT hr;

    hr = ICompositionGraphicsDevice_CreateDrawingSurface(device, size, DirectXPixelFormat_B8G8R8A8UIntNormalized,
            DirectXAlphaMode_Premultiplied, &surface);
    printf("  %s: CreateDrawingSurface %#lx\n", what, hr);
    if (FAILED(hr)) return;
    hr = ICompositionDrawingSurface_get_Size(surface, &got);
    printf("  %s: get_Size %#lx, %.1f x %.1f\n", what, hr, got.Width, got.Height);
    if (SUCCEEDED(ICompositionDrawingSurface_QueryInterface(surface, &IID_ICompositionDrawingSurface2, (void **)&surface2)))
    {
        hr = ICompositionDrawingSurface2_get_SizeInt32(surface2, &got32);
        printf("  %s: get_SizeInt32 %#lx, %d x %d\n", what, hr, got32.Width, got32.Height);
        ICompositionDrawingSurface2_Release(surface2);
    }
    ICompositionDrawingSurface_QueryInterface(surface, &IID_ICompositionDrawingSurfaceInterop2, (void **)&s);
    ICompositionDrawingSurface_Release(surface);
    if (!s) return;

    object = NULL;
    hr = ICompositionDrawingSurfaceInterop2_BeginDraw(s, NULL, &IID_ID2D1DeviceContext, (void **)&object, &offset);
    printf("  %s: BeginDraw(no rectangle, ID2D1DeviceContext) %#lx, offset %ld,%ld\n", what, hr, offset.x, offset.y);
    if (SUCCEEDED(hr)) { IUnknown_Release(object); ICompositionDrawingSurfaceInterop2_EndDraw(s); }
    object = NULL;
    hr = ICompositionDrawingSurfaceInterop2_BeginDraw(s, &rect, &IID_IDXGISurface, (void **)&object, &offset);
    printf("  %s: BeginDraw(rectangle, IDXGISurface) %#lx, offset %ld,%ld\n", what, hr, offset.x, offset.y);
    if (SUCCEEDED(hr)) { IUnknown_Release(object); ICompositionDrawingSurfaceInterop2_EndDraw(s); }
    object = NULL;
    hr = ICompositionDrawingSurfaceInterop2_BeginDraw(s, &rect, &IID_ID2D1DeviceContext, (void **)&object, &offset);
    printf("  %s: BeginDraw(rectangle, ID2D1DeviceContext) %#lx, offset %ld,%ld\n", what, hr, offset.x, offset.y);
    if (SUCCEEDED(hr)) { IUnknown_Release(object); ICompositionDrawingSurfaceInterop2_EndDraw(s); }
    object = NULL;
    hr = ICompositionDrawingSurfaceInterop2_BeginDraw(s, &rect, &IID_ID3D11Texture2D, (void **)&object, &offset);
    printf("  %s: BeginDraw(rectangle, ID3D11Texture2D) %#lx, offset %ld,%ld\n", what, hr, offset.x, offset.y);
    if (SUCCEEDED(hr)) { IUnknown_Release(object); ICompositionDrawingSurfaceInterop2_EndDraw(s); }
    ICompositionDrawingSurfaceInterop2_Release(s);
}

static ICompositorInterop *compositor_interop;

static void diagnose(void)
{
    ICompositionGraphicsDevice *device3d = NULL;
    ICompositionDrawingSurface *surface = NULL;
    surface_t *s = NULL;
    Size size = {128, 64};
    RECT part = {0, 0, 32, 32}, whole = {0, 0, 128, 64};
    POINT offset;
    IUnknown *object;
    HRESULT hr;

    printf("the first draw of a new surface:\n");
    hr = ICompositionGraphicsDevice_CreateDrawingSurface(graphics, size, DirectXPixelFormat_B8G8R8A8UIntNormalized,
            DirectXAlphaMode_Premultiplied, &surface);
    if (SUCCEEDED(hr))
    {
        ICompositionDrawingSurface_QueryInterface(surface, &IID_ICompositionDrawingSurfaceInterop2, (void **)&s);
        ICompositionDrawingSurface_Release(surface);
    }
    if (s)
    {
        object = NULL;
        hr = ICompositionDrawingSurfaceInterop2_BeginDraw(s, &part, &IID_ID2D1DeviceContext, (void **)&object, &offset);
        printf("  part (0,0)-(32,32): %#lx\n", hr);
        if (SUCCEEDED(hr)) { IUnknown_Release(object); ICompositionDrawingSurfaceInterop2_EndDraw(s); }
        object = NULL;
        hr = ICompositionDrawingSurfaceInterop2_BeginDraw(s, &whole, &IID_ID2D1DeviceContext, (void **)&object, &offset);
        printf("  all of it as a rectangle: %#lx\n", hr);
        if (SUCCEEDED(hr)) { IUnknown_Release(object); ICompositionDrawingSurfaceInterop2_EndDraw(s); }
        object = NULL;
        hr = ICompositionDrawingSurfaceInterop2_BeginDraw(s, &part, &IID_ID2D1DeviceContext, (void **)&object, &offset);
        printf("  then the part: %#lx\n", hr);
        if (SUCCEEDED(hr)) { IUnknown_Release(object); ICompositionDrawingSurfaceInterop2_EndDraw(s); }
        ICompositionDrawingSurfaceInterop2_Release(s);
    }

    printf("what BeginDraw takes:\n");
    diagnose_surface("Direct2D device", graphics);
    hr = ICompositorInterop_CreateGraphicsDevice(compositor_interop, (IUnknown *)d3d, &device3d);
    printf("  CreateGraphicsDevice(Direct3D device) %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        diagnose_surface("Direct3D device", device3d);
        ICompositionGraphicsDevice_Release(device3d);
    }
}

static void release(surface_t *s)
{
    if (s) ICompositionDrawingSurfaceInterop2_Release(s);
}

int main(void)
{
    static const POINT two[] = {{16, 16}, {48, 16}};
    static const POINT three[] = {{16, 16}, {48, 16}, {80, 16}};
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    D2D1_CREATION_PROPERTIES props = {D2D1_THREADING_MODE_MULTI_THREADED, D2D1_DEBUG_LEVEL_NONE, 0};
    HRESULT (WINAPI *create_device)(IDXGIDevice *, const D2D1_CREATION_PROPERTIES *, ID2D1Device **);
    ICompositorController *controller = NULL;
    ICompositorInterop *interop = NULL;
    ICompositor *compositor = NULL;
    IInspectable *inspectable = NULL;
    ID2D1Device *d2d = NULL;
    IDXGIDevice *dxgi = NULL;
    surface_t *s1, *s2;
    HSTRING str;
    HRESULT hr;

    RoInitialize(RO_INIT_SINGLETHREADED);
    {
        /* a desktop application gets a compositor only on a thread with a dispatcher queue */
        HRESULT (WINAPI *create)(DispatcherQueueOptions_, IUnknown **) =
            (void *)GetProcAddress(LoadLibraryW(L"coremessaging.dll"), "CreateDispatcherQueueController");
        DispatcherQueueOptions_ options = {sizeof(options), 2 /* DQTYPE_THREAD_CURRENT */, 0 /* DQTAT_COM_NONE */};
        IUnknown *queue = NULL;

        hr = create ? create(options, &queue) : E_NOTIMPL;
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
    ICompositor_QueryInterface(compositor, &IID_ICompositorInterop, (void **)&interop);
    compositor_interop = interop;

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                           D3D11_SDK_VERSION, &d3d, &level, &immediate);
    if (FAILED(hr))
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                               D3D11_SDK_VERSION, &d3d, &level, &immediate);
    printf("D3D11CreateDevice %#lx\n", hr);
    if (FAILED(hr)) return 1;
    ID3D11Device_QueryInterface(d3d, &IID_IDXGIDevice, (void **)&dxgi);
    create_device = (void *)GetProcAddress(LoadLibraryW(L"d2d1.dll"), "D2D1CreateDevice");
    hr = create_device(dxgi, &props, &d2d);
    printf("D2D1CreateDevice %#lx\n", hr);
    /* AirSpace hands the composition its Direct2D device */
    hr = ICompositorInterop_CreateGraphicsDevice(interop, (IUnknown *)d2d, &graphics);
    printf("CreateGraphicsDevice %#lx\n", hr);
    if (FAILED(hr)) return 1;

    diagnose();

    printf("begin twice on one surface:\n");
    s1 = make_surface(128, 64);
    begin("s1", s1, 0, 0, 32, 32, 0xffff0000);
    begin("s1 again", s1, 32, 0, 64, 32, 0xff00ff00);
    call("s1", OP_END, s1);
    call("s1 again", OP_END, s1);
    pixels(s1, two, 2);
    release(s1);

    printf("begin on a second surface while the first draws:\n");
    s1 = make_surface(128, 64); s2 = make_surface(128, 64);
    begin("s1", s1, 0, 0, 32, 32, 0xffff0000);
    begin("s2", s2, 0, 0, 32, 32, 0xff00ff00);
    call("s1", OP_END, s1);
    call("s2", OP_END, s2);
    pixels(s1, two, 1); pixels(s2, two, 1);
    release(s1); release(s2);

    printf("suspend, draw a second surface, resume:\n");
    s1 = make_surface(128, 64); s2 = make_surface(128, 64);
    begin("s1", s1, 0, 0, 32, 32, 0xffff0000);
    call("s1", OP_SUSPEND, s1);
    begin("s2", s2, 0, 0, 32, 32, 0xff00ff00);
    call("s2", OP_END, s2);
    call("s1", OP_RESUME, s1);
    call("s1", OP_END, s1);
    pixels(s1, two, 1); pixels(s2, two, 1);
    release(s1); release(s2);

    printf("AirSpace: suspend, begin the same surface again, end on another thread:\n");
    s1 = make_surface(128, 64);
    begin("s1", s1, 0, 0, 32, 32, 0xffff0000);
    call("s1", OP_SUSPEND, s1);
    begin("s1 again", s1, 32, 0, 64, 32, 0xff00ff00);
    begin("s1 third", s1, 64, 0, 96, 32, 0xff0000ff);
    call_other_thread("s1", OP_END, s1);
    call_other_thread("s1", OP_END, s1);
    call("s1", OP_RESUME, s1);
    call("s1", OP_END, s1);
    pixels(s1, three, 3);
    release(s1);

    printf("suspend, begin the same surface again, end twice on this thread:\n");
    s1 = make_surface(128, 64);
    begin("s1", s1, 0, 0, 32, 32, 0xffff0000);
    call("s1", OP_SUSPEND, s1);
    begin("s1 again", s1, 32, 0, 64, 32, 0xff00ff00);
    call("s1", OP_END, s1);
    call("s1", OP_END, s1);
    call("s1", OP_RESUME, s1);
    pixels(s1, two, 2);
    release(s1);

    printf("end a suspended draw without resuming:\n");
    s1 = make_surface(128, 64);
    begin("s1", s1, 0, 0, 32, 32, 0xffff0000);
    call("s1", OP_SUSPEND, s1);
    call("s1", OP_END, s1);
    call("s1", OP_RESUME, s1);
    pixels(s1, two, 1);
    release(s1);

    printf("begin on one thread, end on another:\n");
    s1 = make_surface(128, 64);
    begin("s1", s1, 0, 0, 32, 32, 0xffff0000);
    call_other_thread("s1", OP_END, s1);
    pixels(s1, two, 1);
    release(s1);

    printf("calls out of order:\n");
    s1 = make_surface(128, 64);
    call("idle s1", OP_END, s1);
    call("idle s1", OP_SUSPEND, s1);
    call("idle s1", OP_RESUME, s1);
    begin("s1", s1, 0, 0, 32, 32, 0xffff0000);
    call("drawing s1", OP_RESUME, s1);
    call("drawing s1", OP_SUSPEND, s1);
    call("suspended s1", OP_SUSPEND, s1);
    call("suspended s1", OP_RESUME, s1);
    call("s1", OP_END, s1);
    release(s1);

    printf("resize and scroll while drawing and while suspended:\n");
    s1 = make_surface(128, 64);
    begin("s1", s1, 0, 0, 32, 32, 0xffff0000);
    call("drawing s1", OP_RESIZE, s1);
    call("drawing s1", OP_SCROLL, s1);
    call("drawing s1", OP_SUSPEND, s1);
    call("suspended s1", OP_RESIZE, s1);
    call("suspended s1", OP_SCROLL, s1);
    call("s1", OP_RESUME, s1);
    call("s1", OP_END, s1);
    call("idle s1", OP_RESIZE, s1);
    call("idle s1", OP_SCROLL, s1);
    release(s1);

    printf("begin a second surface while the first is suspended, then the first again:\n");
    s1 = make_surface(128, 64); s2 = make_surface(128, 64);
    begin("s1", s1, 0, 0, 32, 32, 0xffff0000);
    call("s1", OP_SUSPEND, s1);
    begin("s2", s2, 0, 0, 32, 32, 0xff00ff00);
    begin("s1 again", s1, 32, 0, 64, 32, 0xffffff00);
    call("s2", OP_END, s2);
    begin("s1 after s2 ended", s1, 32, 0, 64, 32, 0xffffff00);
    call("s1", OP_END, s1);
    call("s1", OP_END, s1);
    pixels(s1, two, 2); pixels(s2, two, 1);
    release(s1); release(s2);

    printf("done\n");
    ICompositionGraphicsDevice_Release(graphics);
    ID2D1Device_Release(d2d);
    IDXGIDevice_Release(dxgi);
    ID3D11DeviceContext_Release(immediate);
    ID3D11Device_Release(d3d);
    ICompositorInterop_Release(interop);
    ICompositor_Release(compositor);
    ICompositorController_Release(controller);
    return 0;
}
