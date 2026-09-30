/* compswapchainprobe: DXGI swapchains for composition, and the composition surfaces made of them.
 *
 * Excel's grid animations (tables, pivots, sorting, filters, validation, notes) present through a swapchain
 * made with IDXGIFactory2::CreateSwapChainForComposition and shown by a Windows.UI.Composition surface from
 * ICompositorInterop::CreateCompositionSurfaceForSwapChain.  Wine stubbed both, and Excel tried again on
 * every frame.  This prints what Windows answers: which descriptions it takes, what the methods that are
 * about a window say for a swapchain that has none, how long a present takes, and what the surface is.
 *
 *   scripts/build-probe.sh tools/compswapchainprobe/compswapchainprobe.c \
 *       tools/compswapchainprobe/compswapchainprobe.exe combase d3d11 dxgi user32 gdi32 uuid dxguid
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
#include "dxgi1_6.h"
#include "initguid.h"
#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Numerics
#define WIDL_using_Windows_Graphics
#define WIDL_using_Windows_Graphics_DirectX
#define WIDL_using_Windows_UI
#define WIDL_using_Windows_UI_Composition
#define WIDL_using_Windows_UI_Composition_Desktop
#include "windows.foundation.h"
#include "windows.ui.composition.h"
#include "windows.ui.composition.interop.h"

typedef struct { DWORD dwSize; int threadType; int apartmentType; } DispatcherQueueOptions_;

static ID3D11Device *d3d;
static IDXGIFactory2 *factory;

static DXGI_SWAP_CHAIN_DESC1 base_desc(void)
{
    DXGI_SWAP_CHAIN_DESC1 desc = {0};

    /* as Excel asks for them */
    desc.Width = 337;
    desc.Height = 230;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    return desc;
}

static void try_desc(const char *what, const DXGI_SWAP_CHAIN_DESC1 *desc)
{
    IDXGISwapChain1 *swapchain = NULL;
    HRESULT hr;

    hr = IDXGIFactory2_CreateSwapChainForComposition(factory, (IUnknown *)d3d, desc, NULL, &swapchain);
    printf("create %-28s %#lx%s\n", what, hr, SUCCEEDED(hr) && !swapchain ? " (no swapchain)" : "");
    if (swapchain) IDXGISwapChain1_Release(swapchain);
}

static void descriptions(void)
{
    static const struct { const char *name; DXGI_FORMAT format; } formats[] =
    {
        {"R8G8B8A8_UNORM", DXGI_FORMAT_R8G8B8A8_UNORM},
        {"R16G16B16A16_FLOAT", DXGI_FORMAT_R16G16B16A16_FLOAT},
        {"R10G10B10A2_UNORM", DXGI_FORMAT_R10G10B10A2_UNORM},
        {"B8G8R8X8_UNORM", DXGI_FORMAT_B8G8R8X8_UNORM},
        {"B8G8R8A8_UNORM_SRGB", DXGI_FORMAT_B8G8R8A8_UNORM_SRGB},
        {"UNKNOWN", DXGI_FORMAT_UNKNOWN},
    };
    DXGI_SWAP_CHAIN_DESC1 desc;
    IDXGISwapChain1 *swapchain = NULL;
    char name[64];
    unsigned int i;
    HRESULT hr;

    desc = base_desc(); try_desc("as Excel", &desc);
    desc = base_desc(); desc.Scaling = DXGI_SCALING_NONE; try_desc("scaling none", &desc);
    desc = base_desc(); desc.Scaling = DXGI_SCALING_ASPECT_RATIO_STRETCH; try_desc("scaling aspect", &desc);
    desc = base_desc(); desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD; try_desc("effect discard", &desc);
    desc = base_desc(); desc.SwapEffect = DXGI_SWAP_EFFECT_SEQUENTIAL; try_desc("effect sequential", &desc);
    desc = base_desc(); desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; try_desc("effect flip discard", &desc);
    desc = base_desc(); desc.Width = 0; try_desc("width 0", &desc);
    desc = base_desc(); desc.Height = 0; try_desc("height 0", &desc);
    desc = base_desc(); desc.Width = desc.Height = 0; try_desc("size 0", &desc);
    desc = base_desc(); desc.BufferCount = 1; try_desc("buffers 1", &desc);
    desc = base_desc(); desc.BufferCount = 3; try_desc("buffers 3", &desc);
    desc = base_desc(); desc.BufferCount = 16; try_desc("buffers 16", &desc);
    desc = base_desc(); desc.BufferCount = 17; try_desc("buffers 17", &desc);
    desc = base_desc(); desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED; try_desc("alpha unspecified", &desc);
    desc = base_desc(); desc.AlphaMode = DXGI_ALPHA_MODE_STRAIGHT; try_desc("alpha straight", &desc);
    desc = base_desc(); desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE; try_desc("alpha ignore", &desc);
    desc = base_desc(); desc.AlphaMode = 4; try_desc("alpha 4", &desc);
    for (i = 0; i < ARRAY_SIZE(formats); i++)
    {
        desc = base_desc();
        desc.Format = formats[i].format;
        snprintf(name, sizeof(name), "format %s", formats[i].name);
        try_desc(name, &desc);
    }
    desc = base_desc(); desc.SampleDesc.Count = 4; try_desc("samples 4", &desc);
    desc = base_desc(); desc.Stereo = TRUE; try_desc("stereo", &desc);
    desc = base_desc(); desc.BufferUsage = 0; try_desc("usage 0", &desc);
    desc = base_desc(); desc.BufferUsage |= DXGI_USAGE_SHADER_INPUT; try_desc("usage shader input", &desc);
    desc = base_desc(); desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH; try_desc("flag mode switch", &desc);
    desc = base_desc(); desc.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT; try_desc("flag waitable", &desc);
    desc = base_desc(); desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING; try_desc("flag tearing", &desc);
    desc = base_desc(); desc.Flags = DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE; try_desc("flag gdi", &desc);

    desc = base_desc();
    hr = IDXGIFactory2_CreateSwapChainForComposition(factory, NULL, &desc, NULL, &swapchain);
    printf("create %-28s %#lx\n", "NULL device", hr);
    if (SUCCEEDED(hr)) IDXGISwapChain1_Release(swapchain);
    hr = IDXGIFactory2_CreateSwapChainForComposition(factory, (IUnknown *)d3d, NULL, NULL, &swapchain);
    printf("create %-28s %#lx\n", "NULL desc", hr);
    if (SUCCEEDED(hr)) IDXGISwapChain1_Release(swapchain);
    hr = IDXGIFactory2_CreateSwapChainForComposition(factory, (IUnknown *)d3d, &desc, NULL, NULL);
    printf("create %-28s %#lx\n", "NULL out", hr);
}

static double now_ms(void)
{
    LARGE_INTEGER counter, frequency;

    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return counter.QuadPart * 1000.0 / frequency.QuadPart;
}

static void presents(IDXGISwapChain1 *swapchain, const char *what, UINT interval, UINT flags, int count)
{
    HRESULT hr, first = S_OK;
    double start = now_ms();
    int i;

    for (i = 0; i < count; i++)
    {
        hr = IDXGISwapChain1_Present(swapchain, interval, flags);
        if (!i) first = hr;
    }
    printf("present %-26s %#lx, %.2f ms each\n", what, first, (now_ms() - start) / count);
}

static void methods(IDXGISwapChain1 *swapchain)
{
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen;
    DXGI_MATRIX_3X2_F matrix = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    DXGI_PRESENT_PARAMETERS params = {0};
    RECT dirty = {0, 0, 16, 16};
    DXGI_FRAME_STATISTICS stats;
    DXGI_SWAP_CHAIN_DESC1 desc1;
    DXGI_SWAP_CHAIN_DESC desc;
    IDXGISwapChain2 *swapchain2;
    IDXGISwapChain3 *swapchain3;
    DXGI_MODE_DESC mode = {0};
    DXGI_RGBA color = {0};
    IDXGIOutput *output;
    IUnknown *unknown;
    UINT value, width, height;
    BOOL fullscreen_state;
    HANDLE waitable;
    HWND hwnd;
    HRESULT hr;
    int i;

    /* Excel asks this first, before it presents anything */
    memset(&stats, 0xcc, sizeof(stats));
    hr = IDXGISwapChain1_GetFrameStatistics(swapchain, &stats);
    printf("GetFrameStatistics before a present %#lx: present count %u, refresh %u, sync refresh %u, qpc %s, gpu %s\n",
           hr, stats.PresentCount, stats.PresentRefreshCount, stats.SyncRefreshCount,
           stats.SyncQPCTime.QuadPart == 0xcccccccccccccccc ? "untouched" : stats.SyncQPCTime.QuadPart ? "set" : "0",
           stats.SyncGPUTime.QuadPart == 0xcccccccccccccccc ? "untouched" : stats.SyncGPUTime.QuadPart ? "set" : "0");
    value = 0xcccccccc;
    hr = IDXGISwapChain1_GetLastPresentCount(swapchain, &value);
    printf("GetLastPresentCount before a present %#lx: %u\n", hr, value);

    hwnd = (HWND)0xdeadbeef;
    hr = IDXGISwapChain1_GetHwnd(swapchain, &hwnd);
    printf("GetHwnd %#lx, hwnd %s\n", hr, !hwnd ? "NULL" : hwnd == (HWND)0xdeadbeef ? "untouched" : "set");
    unknown = (IUnknown *)0xdeadbeef;
    hr = IDXGISwapChain1_GetCoreWindow(swapchain, &IID_IUnknown, (void **)&unknown);
    printf("GetCoreWindow %#lx, %s\n", hr, !unknown ? "NULL" : unknown == (IUnknown *)0xdeadbeef ? "untouched" : "set");
    if (SUCCEEDED(hr) && unknown) IUnknown_Release(unknown);

    memset(&desc, 0xcc, sizeof(desc));
    hr = IDXGISwapChain1_GetDesc(swapchain, &desc);
    printf("GetDesc %#lx: %ux%u format %#x refresh %u/%u scanline %u scaling %u, samples %u, usage %#x, buffers %u, "
           "window %s, windowed %d, effect %u, flags %#x\n", hr, desc.BufferDesc.Width, desc.BufferDesc.Height,
           desc.BufferDesc.Format, desc.BufferDesc.RefreshRate.Numerator, desc.BufferDesc.RefreshRate.Denominator,
           desc.BufferDesc.ScanlineOrdering, desc.BufferDesc.Scaling, desc.SampleDesc.Count, desc.BufferUsage,
           desc.BufferCount, !desc.OutputWindow ? "NULL" : "set", desc.Windowed, desc.SwapEffect, desc.Flags);
    memset(&desc1, 0xcc, sizeof(desc1));
    hr = IDXGISwapChain1_GetDesc1(swapchain, &desc1);
    printf("GetDesc1 %#lx: %ux%u format %#x stereo %d samples %u usage %#x buffers %u scaling %u effect %u alpha %u "
           "flags %#x\n", hr, desc1.Width, desc1.Height, desc1.Format, desc1.Stereo, desc1.SampleDesc.Count,
           desc1.BufferUsage, desc1.BufferCount, desc1.Scaling, desc1.SwapEffect, desc1.AlphaMode, desc1.Flags);
    memset(&fullscreen, 0xcc, sizeof(fullscreen));
    hr = IDXGISwapChain1_GetFullscreenDesc(swapchain, &fullscreen);
    printf("GetFullscreenDesc %#lx, windowed %d\n", hr, fullscreen.Windowed);
    fullscreen_state = 0xcc;
    output = (IDXGIOutput *)0xdeadbeef;
    hr = IDXGISwapChain1_GetFullscreenState(swapchain, &fullscreen_state, &output);
    printf("GetFullscreenState %#lx, fullscreen %d, output %s\n", hr, fullscreen_state,
           !output ? "NULL" : output == (IDXGIOutput *)0xdeadbeef ? "untouched" : "set");
    if (SUCCEEDED(hr) && output && output != (IDXGIOutput *)0xdeadbeef) IDXGIOutput_Release(output);
    hr = IDXGISwapChain1_SetFullscreenState(swapchain, TRUE, NULL);
    printf("SetFullscreenState(TRUE) %#lx\n", hr);
    if (SUCCEEDED(hr)) IDXGISwapChain1_SetFullscreenState(swapchain, FALSE, NULL);
    hr = IDXGISwapChain1_SetFullscreenState(swapchain, FALSE, NULL);
    printf("SetFullscreenState(FALSE) %#lx\n", hr);
    output = NULL;
    hr = IDXGISwapChain1_GetContainingOutput(swapchain, &output);
    printf("GetContainingOutput %#lx, output %s\n", hr, output ? "set" : "NULL");
    if (output) IDXGIOutput_Release(output);
    mode.Width = 400;
    mode.Height = 300;
    hr = IDXGISwapChain1_ResizeTarget(swapchain, &mode);
    printf("ResizeTarget %#lx\n", hr);
    output = (IDXGIOutput *)0xdeadbeef;
    hr = IDXGISwapChain1_GetRestrictToOutput(swapchain, &output);
    printf("GetRestrictToOutput %#lx, output %s\n", hr, !output ? "NULL" : "set");
    hr = IDXGISwapChain1_SetBackgroundColor(swapchain, &color);
    printf("SetBackgroundColor %#lx\n", hr);
    hr = IDXGISwapChain1_GetBackgroundColor(swapchain, &color);
    printf("GetBackgroundColor %#lx\n", hr);
    hr = IDXGISwapChain1_SetRotation(swapchain, DXGI_MODE_ROTATION_IDENTITY);
    printf("SetRotation(identity) %#lx\n", hr);
    for (i = 0; i < 3; i++)
    {
        unknown = NULL;
        hr = IDXGISwapChain1_GetBuffer(swapchain, i, &IID_ID3D11Texture2D, (void **)&unknown);
        printf("GetBuffer(%d) %#lx\n", i, hr);
        if (unknown) IUnknown_Release(unknown);
    }
    hr = IDXGISwapChain1_GetDevice(swapchain, &IID_ID3D11Device, (void **)&unknown);
    printf("GetDevice %#lx, %s\n", hr, SUCCEEDED(hr) && unknown == (IUnknown *)d3d ? "the device" : "another");
    if (SUCCEEDED(hr)) IUnknown_Release(unknown);

    if (SUCCEEDED(IDXGISwapChain1_QueryInterface(swapchain, &IID_IDXGISwapChain2, (void **)&swapchain2)))
    {
        DXGI_MATRIX_3X2_F got;

        hr = IDXGISwapChain2_SetMatrixTransform(swapchain2, &matrix);
        printf("SetMatrixTransform %#lx\n", hr);
        memset(&got, 0, sizeof(got));
        hr = IDXGISwapChain2_GetMatrixTransform(swapchain2, &got);
        printf("GetMatrixTransform %#lx: %.2f %.2f %.2f %.2f %.2f %.2f\n", hr, got._11, got._12, got._21, got._22,
               got._31, got._32);
        matrix._11 = 2.0f;
        hr = IDXGISwapChain2_SetMatrixTransform(swapchain2, &matrix);
        printf("SetMatrixTransform(scale 2) %#lx\n", hr);
        hr = IDXGISwapChain2_SetSourceSize(swapchain2, 100, 50);
        printf("SetSourceSize(100, 50) %#lx\n", hr);
        width = height = 0;
        hr = IDXGISwapChain2_GetSourceSize(swapchain2, &width, &height);
        printf("GetSourceSize %#lx: %ux%u\n", hr, width, height);
        hr = IDXGISwapChain2_SetSourceSize(swapchain2, 500, 50);
        printf("SetSourceSize(500, 50) %#lx\n", hr);
        hr = IDXGISwapChain2_SetSourceSize(swapchain2, 337, 230);
        printf("SetSourceSize(337, 230) %#lx\n", hr);
        value = 0;
        hr = IDXGISwapChain2_GetMaximumFrameLatency(swapchain2, &value);
        printf("GetMaximumFrameLatency %#lx: %u\n", hr, value);
        waitable = IDXGISwapChain2_GetFrameLatencyWaitableObject(swapchain2);
        printf("GetFrameLatencyWaitableObject %s\n", waitable ? "a handle" : "NULL");
        if (waitable) CloseHandle(waitable);
        IDXGISwapChain2_Release(swapchain2);
    }
    else printf("no IDXGISwapChain2\n");

    swapchain3 = NULL;
    IDXGISwapChain1_QueryInterface(swapchain, &IID_IDXGISwapChain3, (void **)&swapchain3);
    if (swapchain3) printf("back buffer index %u\n", IDXGISwapChain3_GetCurrentBackBufferIndex(swapchain3));
    presents(swapchain, "(0, 0) first", 0, 0, 1);
    if (swapchain3) printf("back buffer index %u\n", IDXGISwapChain3_GetCurrentBackBufferIndex(swapchain3));
    presents(swapchain, "(0, test)", 0, DXGI_PRESENT_TEST, 1);
    presents(swapchain, "(1, 0) x30", 1, 0, 30);
    presents(swapchain, "(0, 0) x30", 0, 0, 30);
    presents(swapchain, "(2, 0) x10", 2, 0, 10);
    presents(swapchain, "(0, do not wait) x30", 0, DXGI_PRESENT_DO_NOT_WAIT, 30);
    if (swapchain3) printf("back buffer index %u\n", IDXGISwapChain3_GetCurrentBackBufferIndex(swapchain3));
    params.DirtyRectsCount = 1;
    params.pDirtyRects = &dirty;
    hr = IDXGISwapChain1_Present1(swapchain, 0, 0, &params);
    printf("Present1 with a dirty rect %#lx\n", hr);
    value = 0xcccccccc;
    hr = IDXGISwapChain1_GetLastPresentCount(swapchain, &value);
    printf("GetLastPresentCount %#lx: %u\n", hr, value);
    for (i = 0; i < 2; i++)
    {
        LARGE_INTEGER now, frequency;

        memset(&stats, 0xcc, sizeof(stats));
        hr = IDXGISwapChain1_GetFrameStatistics(swapchain, &stats);
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&frequency);
        printf("GetFrameStatistics %#lx: present count %u, refresh %u, sync refresh %u, qpc %.1f ms ago, gpu %s\n",
               hr, stats.PresentCount, stats.PresentRefreshCount, stats.SyncRefreshCount,
               stats.SyncQPCTime.QuadPart == 0xcccccccccccccccc ? -1.0
               : (now.QuadPart - stats.SyncQPCTime.QuadPart) * 1000.0 / frequency.QuadPart,
               stats.SyncGPUTime.QuadPart == 0xcccccccccccccccc ? "untouched" : stats.SyncGPUTime.QuadPart ? "set" : "0");
        IDXGISwapChain1_Present(swapchain, 1, 0);
    }

    hr = IDXGISwapChain1_ResizeBuffers(swapchain, 0, 400, 300, DXGI_FORMAT_UNKNOWN, 0);
    printf("ResizeBuffers(400x300) %#lx\n", hr);
    IDXGISwapChain1_GetDesc1(swapchain, &desc1);
    printf("  now %ux%u buffers %u\n", desc1.Width, desc1.Height, desc1.BufferCount);
    hr = IDXGISwapChain1_ResizeBuffers(swapchain, 0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
    printf("ResizeBuffers(0x0) %#lx\n", hr);
    IDXGISwapChain1_GetDesc1(swapchain, &desc1);
    printf("  now %ux%u buffers %u\n", desc1.Width, desc1.Height, desc1.BufferCount);
    hr = IDXGISwapChain1_ResizeBuffers(swapchain, 3, 337, 230, DXGI_FORMAT_UNKNOWN, 0);
    printf("ResizeBuffers(3 buffers) %#lx\n", hr);
    if (swapchain3) IDXGISwapChain3_Release(swapchain3);
}

static void fill(IDXGISwapChain1 *swapchain, float r, float g, float b)
{
    const float colour[4] = {r, g, b, 1.0f};
    ID3D11DeviceContext *immediate;
    ID3D11RenderTargetView *view;
    ID3D11Texture2D *buffer;

    if (FAILED(IDXGISwapChain1_GetBuffer(swapchain, 0, &IID_ID3D11Texture2D, (void **)&buffer))) return;
    if (SUCCEEDED(ID3D11Device_CreateRenderTargetView(d3d, (ID3D11Resource *)buffer, NULL, &view)))
    {
        ID3D11Device_GetImmediateContext(d3d, &immediate);
        ID3D11DeviceContext_ClearRenderTargetView(immediate, view, colour);
        ID3D11DeviceContext_Release(immediate);
        ID3D11RenderTargetView_Release(view);
    }
    ID3D11Texture2D_Release(buffer);
}

static void pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG msg;

    while ((int)(end - GetTickCount()) > 0)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        MsgWaitForMultipleObjects(0, NULL, FALSE, 10, QS_ALLINPUT);
    }
}

static void screen_pixels(const char *what, HWND window)
{
    static const POINT points[] = {{100, 100}, {60, 60}, {140, 140}, {10, 10}};
    HDC screen = GetDC(NULL);
    unsigned int i;
    POINT p;

    printf("%s:", what);
    for (i = 0; i < ARRAY_SIZE(points); i++)
    {
        p = points[i];
        ClientToScreen(window, &p);
        printf(" (%ld,%ld) %06lx", points[i].x, points[i].y, GetPixel(screen, p.x, p.y));
    }
    printf("\n");
    ReleaseDC(NULL, screen);
}

/* A sprite of 100x100 at (50,50) in a 200x200 window shows the swapchain stretched; what the swapchain presents
 * should reach the screen with no commit.  Read from the screen, which is what the desktop composed. */
static void show(ICompositor *compositor, ICompositorInterop *interop)
{
    ICompositionSurfaceBrush *brush = NULL;
    ICompositorDesktopInterop *desktop = NULL;
    DXGI_SWAP_CHAIN_DESC1 desc = base_desc();
    IDesktopWindowTarget *target = NULL;
    IDXGISwapChain1 *swapchain = NULL;
    ICompositionSurface *surface = NULL;
    IContainerVisual *container = NULL;
    ICompositionTarget *ctarget = NULL;
    IVisualCollection *children = NULL;
    ICompositionBrush *cbrush = NULL;
    IVisual *root = NULL, *visual = NULL;
    ISpriteVisual *sprite = NULL;
    Vector3 offset = {50.0f, 50.0f, 0.0f};
    Vector2 size = {200.0f, 200.0f};
    HWND window;
    HRESULT hr;

    window = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOPMOST, L"static", L"compswapchainprobe show",
                             WS_POPUP | WS_VISIBLE, 100, 100, 200, 200, NULL, NULL, NULL, NULL);
    hr = ICompositor_QueryInterface(compositor, &IID_ICompositorDesktopInterop, (void **)&desktop);
    if (SUCCEEDED(hr)) hr = ICompositorDesktopInterop_CreateDesktopWindowTarget(desktop, window, FALSE, &target);
    printf("show: CreateDesktopWindowTarget %#lx\n", hr);
    if (FAILED(hr)) goto done;
    IDesktopWindowTarget_QueryInterface(target, &IID_ICompositionTarget, (void **)&ctarget);
    ICompositor_CreateContainerVisual(compositor, &container);
    IContainerVisual_QueryInterface(container, &IID_IVisual, (void **)&root);
    IVisual_put_Size(root, size);
    ICompositionTarget_put_Root(ctarget, root);
    ICompositor_CreateSpriteVisual(compositor, &sprite);
    ISpriteVisual_QueryInterface(sprite, &IID_IVisual, (void **)&visual);
    size.X = size.Y = 100.0f;
    IVisual_put_Size(visual, size);
    IVisual_put_Offset(visual, offset);
    IContainerVisual_get_Children(container, &children);
    IVisualCollection_InsertAtTop(children, visual);

    hr = IDXGIFactory2_CreateSwapChainForComposition(factory, (IUnknown *)d3d, &desc, NULL, &swapchain);
    if (SUCCEEDED(hr)) hr = ICompositorInterop_CreateCompositionSurfaceForSwapChain(interop, (IUnknown *)swapchain, &surface);
    printf("show: swapchain and surface %#lx\n", hr);
    if (FAILED(hr)) goto done;
    ICompositor_CreateSurfaceBrushWithSurface(compositor, surface, &brush);
    ICompositionSurfaceBrush_put_Stretch(brush, CompositionStretch_Fill);
    ICompositionSurfaceBrush_QueryInterface(brush, &IID_ICompositionBrush, (void **)&cbrush);
    ISpriteVisual_put_Brush(sprite, cbrush);
    pump(500);
    screen_pixels("show: before a present", window);

    fill(swapchain, 1.0f, 0.0f, 0.0f);
    hr = IDXGISwapChain1_Present(swapchain, 1, 0);
    pump(500);
    screen_pixels("show: red presented", window);
    fill(swapchain, 0.0f, 1.0f, 0.0f);
    hr = IDXGISwapChain1_Present(swapchain, 1, 0);
    pump(500);
    screen_pixels("show: green presented", window);
    /* what a premultiplied swapchain shows of half a red at half coverage */
    fill(swapchain, 0.5f, 0.0f, 0.0f);
    {
        const float colour[4] = {0.5f, 0.0f, 0.0f, 0.5f};
        ID3D11DeviceContext *immediate;
        ID3D11RenderTargetView *view;
        ID3D11Texture2D *buffer;

        IDXGISwapChain1_GetBuffer(swapchain, 0, &IID_ID3D11Texture2D, (void **)&buffer);
        ID3D11Device_CreateRenderTargetView(d3d, (ID3D11Resource *)buffer, NULL, &view);
        ID3D11Device_GetImmediateContext(d3d, &immediate);
        ID3D11DeviceContext_ClearRenderTargetView(immediate, view, colour);
        ID3D11DeviceContext_Release(immediate);
        ID3D11RenderTargetView_Release(view);
        ID3D11Texture2D_Release(buffer);
    }
    hr = IDXGISwapChain1_Present(swapchain, 1, 0);
    pump(500);
    screen_pixels("show: half red at half alpha presented", window);

done:
    if (cbrush) ICompositionBrush_Release(cbrush);
    if (brush) ICompositionSurfaceBrush_Release(brush);
    if (surface) ICompositionSurface_Release(surface);
    if (swapchain) IDXGISwapChain1_Release(swapchain);
    if (children) IVisualCollection_Release(children);
    if (visual) IVisual_Release(visual);
    if (sprite) ISpriteVisual_Release(sprite);
    if (root) IVisual_Release(root);
    if (container) IContainerVisual_Release(container);
    if (ctarget) ICompositionTarget_Release(ctarget);
    if (target) IDesktopWindowTarget_Release(target);
    if (desktop) ICompositorDesktopInterop_Release(desktop);
    DestroyWindow(window);
}

static HSTRING S(const WCHAR *s)
{
    HSTRING h = NULL;
    WindowsCreateString(s, wcslen(s), &h);
    return h;
}

static void surfaces(IDXGISwapChain1 *swapchain)
{
    ICompositionSurface *surface = NULL, *again = NULL;
    DXGI_SWAP_CHAIN_DESC1 desc = base_desc();
    IDXGISwapChain1 *hwnd_swapchain = NULL;
    ICompositorInterop *interop = NULL;
    IInspectable *inspectable = NULL;
    ICompositor *compositor = NULL;
    HSTRING str, name = NULL;
    IUnknown *unknown;
    ULONG count = 0;
    IID *iids = NULL;
    HWND window;
    HRESULT hr;

    {
        /* a desktop application gets a compositor only on a thread with a dispatcher queue */
        HRESULT (WINAPI *create)(DispatcherQueueOptions_, IUnknown **) =
            (void *)GetProcAddress(LoadLibraryW(L"coremessaging.dll"), "CreateDispatcherQueueController");
        DispatcherQueueOptions_ options = {sizeof(options), 2 /* DQTYPE_THREAD_CURRENT */, 0 /* DQTAT_COM_NONE */};
        IUnknown *queue = NULL;

        hr = create ? create(options, &queue) : E_NOTIMPL;
        printf("CreateDispatcherQueueController %#lx\n", hr);
    }
    str = S(L"Windows.UI.Composition.Compositor");
    hr = RoActivateInstance(str, &inspectable);
    WindowsDeleteString(str);
    printf("activate Compositor %#lx\n", hr);
    if (FAILED(hr)) return;
    IInspectable_QueryInterface(inspectable, &IID_ICompositor, (void **)&compositor);
    IInspectable_Release(inspectable);
    hr = ICompositor_QueryInterface(compositor, &IID_ICompositorInterop, (void **)&interop);
    printf("QI ICompositorInterop %#lx\n", hr);
    if (FAILED(hr)) return;

    hr = ICompositorInterop_CreateCompositionSurfaceForSwapChain(interop, (IUnknown *)swapchain, &surface);
    printf("CreateCompositionSurfaceForSwapChain %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = ICompositionSurface_GetRuntimeClassName(surface, &name);
        printf("  class %#lx %ls\n", hr, name ? WindowsGetStringRawBuffer(name, NULL) : L"(none)");
        WindowsDeleteString(name);
        hr = ICompositionSurface_GetIids(surface, &count, &iids);
        printf("  GetIids %#lx, %lu\n", hr, count);
        CoTaskMemFree(iids);
#define QI(iid) \
        hr = ICompositionSurface_QueryInterface(surface, &IID_##iid, (void **)&unknown); \
        printf("  QI %-32s %#lx\n", #iid, hr); \
        if (SUCCEEDED(hr)) IUnknown_Release(unknown);
        QI(ICompositionObject)
        QI(ICompositionObject2)
        QI(ICompositionObject4)
        QI(IClosable)
        QI(ICompositionDrawingSurface)
        QI(ICompositionDrawingSurfaceInterop)
        QI(IDXGISwapChain1)
        QI(IAgileObject)
        QI(IMarshal)
#undef QI
        hr = ICompositorInterop_CreateCompositionSurfaceForSwapChain(interop, (IUnknown *)swapchain, &again);
        printf("again on the same swapchain %#lx, %s\n", hr, again == surface ? "the same surface" : "another");
        if (SUCCEEDED(hr)) ICompositionSurface_Release(again);
        presents(swapchain, "(1, 0) x30 in a surface", 1, 0, 30);
        presents(swapchain, "(0, 0) x30 in a surface", 0, 0, 30);
        ICompositionSurface_Release(surface);
    }
    surface = NULL;
    hr = ICompositorInterop_CreateCompositionSurfaceForSwapChain(interop, NULL, &surface);
    printf("CreateCompositionSurfaceForSwapChain(NULL) %#lx\n", hr);
    if (SUCCEEDED(hr) && surface) ICompositionSurface_Release(surface);
    surface = NULL;
    hr = ICompositorInterop_CreateCompositionSurfaceForSwapChain(interop, (IUnknown *)d3d, &surface);
    printf("CreateCompositionSurfaceForSwapChain(device) %#lx\n", hr);
    if (SUCCEEDED(hr) && surface) ICompositionSurface_Release(surface);

    window = CreateWindowExW(0, L"static", L"compswapchainprobe", WS_POPUP, 0, 0, 337, 230, NULL, NULL, NULL, NULL);
    hr = IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown *)d3d, window, &desc, NULL, NULL, &hwnd_swapchain);
    printf("CreateSwapChainForHwnd %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        surface = NULL;
        hr = ICompositorInterop_CreateCompositionSurfaceForSwapChain(interop, (IUnknown *)hwnd_swapchain, &surface);
        printf("CreateCompositionSurfaceForSwapChain(a window's) %#lx\n", hr);
        if (SUCCEEDED(hr) && surface) ICompositionSurface_Release(surface);
        IDXGISwapChain1_Release(hwnd_swapchain);
    }
    DestroyWindow(window);
    show(compositor, interop);
    ICompositorInterop_Release(interop);
    ICompositor_Release(compositor);
}

int main(void)
{
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    DXGI_SWAP_CHAIN_DESC1 desc = base_desc();
    IDXGISwapChain1 *swapchain = NULL;
    IDXGIAdapter *adapter = NULL;
    IDXGIDevice *dxgi = NULL;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    RoInitialize(RO_INIT_SINGLETHREADED);
    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                           D3D11_SDK_VERSION, &d3d, &level, NULL);
    if (FAILED(hr))
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                               D3D11_SDK_VERSION, &d3d, &level, NULL);
    printf("D3D11CreateDevice %#lx\n", hr);
    if (FAILED(hr)) return 1;
    ID3D11Device_QueryInterface(d3d, &IID_IDXGIDevice, (void **)&dxgi);
    IDXGIDevice_GetAdapter(dxgi, &adapter);
    IDXGIAdapter_GetParent(adapter, &IID_IDXGIFactory2, (void **)&factory);
    IDXGIAdapter_Release(adapter);
    IDXGIDevice_Release(dxgi);

    descriptions();

    hr = IDXGIFactory2_CreateSwapChainForComposition(factory, (IUnknown *)d3d, &desc, NULL, &swapchain);
    printf("the swapchain %#lx\n", hr);
    if (FAILED(hr)) return 1;
    methods(swapchain);
    IDXGISwapChain1_Release(swapchain);

    desc = base_desc();
    hr = IDXGIFactory2_CreateSwapChainForComposition(factory, (IUnknown *)d3d, &desc, NULL, &swapchain);
    printf("another swapchain %#lx\n", hr);
    if (FAILED(hr)) return 1;
    surfaces(swapchain);
    IDXGISwapChain1_Release(swapchain);

    IDXGIFactory2_Release(factory);
    ID3D11Device_Release(d3d);
    printf("done\n");
    return 0;
}
