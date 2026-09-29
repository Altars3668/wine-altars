/* dxgizeroprobe - what size a DXGI swap chain takes from a window with no client area.
 *
 * Creates a D3D11 swap chain with a zero width and height for a window whose client area is
 * empty (a borderless popup of size 0, and a minimized overlapped window), then calls
 * ResizeBuffers(0, 0) on each, and on a normal window made empty afterwards, printing the size
 * GetDesc1 reports after each step and whether Present still succeeds.  With the flip model and
 * the blit model on a Direct3D 11 device, with the flip model on a Direct3D 12 queue, and then the
 * same questions for a Direct3D 9 device: the size it is created with and the size Reset(0, 0)
 * gives it.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later. */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3d9.h>
#include <d3d12.h>
#include <stdio.h>

static IDXGIFactory2 *factory;
static ID3D11Device *device;
static IUnknown *swapchain_device;

static void print_desc(const char *what, IDXGISwapChain1 *swapchain, HRESULT hr)
{
    DXGI_SWAP_CHAIN_DESC1 desc = { 0 };
    HWND hwnd = NULL;
    RECT rect = { 0 };

    IDXGISwapChain1_GetDesc1(swapchain, &desc);
    IDXGISwapChain1_GetHwnd(swapchain, &hwnd);
    GetClientRect(hwnd, &rect);
    printf("%-44s hr %#lx  buffers %ux%u  client %ldx%ld  iconic %d\n", what, hr, desc.Width, desc.Height,
           rect.right, rect.bottom, IsIconic(hwnd));
}

static DXGI_SWAP_EFFECT swap_effect;

static IDXGISwapChain1 *create(HWND hwnd, const char *what)
{
    DXGI_SWAP_CHAIN_DESC1 desc = { 0 };
    IDXGISwapChain1 *swapchain = NULL;
    HRESULT hr;

    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = swap_effect == DXGI_SWAP_EFFECT_DISCARD ? 1 : 2;
    desc.SwapEffect = swap_effect;
    hr = IDXGIFactory2_CreateSwapChainForHwnd(factory, swapchain_device, hwnd, &desc, NULL, NULL, &swapchain);
    if (FAILED(hr))
    {
        printf("%-44s create hr %#lx\n", what, hr);
        return NULL;
    }
    print_desc(what, swapchain, hr);
    return swapchain;
}

static void resize(IDXGISwapChain1 *swapchain, const char *what)
{
    HRESULT hr = IDXGISwapChain1_ResizeBuffers(swapchain, 0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
    print_desc(what, swapchain, hr);
    hr = IDXGISwapChain1_Present(swapchain, 0, 0);
    printf("%-44s present hr %#lx\n", "", hr);
}

static void test_dxgi(IUnknown *on, const char *name, DXGI_SWAP_EFFECT effect)
{
    IDXGISwapChain1 *swapchain;
    HWND popup, overlapped, normal;

    swapchain_device = on;
    swap_effect = effect;
    printf("%s, swap effect %u\n", name, effect);
    popup = CreateWindowA("static", "zero", WS_POPUP | WS_VISIBLE, 0, 0, 0, 0, NULL, NULL, NULL, NULL);
    if ((swapchain = create(popup, "popup 0x0: created")))
    {
        resize(swapchain, "popup 0x0: ResizeBuffers(0, 0)");
        IDXGISwapChain1_Release(swapchain);
    }

    overlapped = CreateWindowA("static", "minimized", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 300, 200,
                               NULL, NULL, NULL, NULL);
    ShowWindow(overlapped, SW_MINIMIZE);
    if ((swapchain = create(overlapped, "minimized: created")))
    {
        resize(swapchain, "minimized: ResizeBuffers(0, 0)");
        ShowWindow(overlapped, SW_RESTORE);
        resize(swapchain, "restored: ResizeBuffers(0, 0)");
        IDXGISwapChain1_Release(swapchain);
    }

    normal = CreateWindowA("static", "normal", WS_POPUP | WS_VISIBLE, 0, 0, 320, 240, NULL, NULL, NULL, NULL);
    if ((swapchain = create(normal, "320x240: created")))
    {
        SetWindowPos(normal, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOZORDER);
        resize(swapchain, "made 0x0: ResizeBuffers(0, 0)");
        SetWindowPos(normal, NULL, 0, 0, 100, 0, SWP_NOMOVE | SWP_NOZORDER);
        resize(swapchain, "made 100x0: ResizeBuffers(0, 0)");
        SetWindowPos(normal, NULL, 0, 0, 0, 100, SWP_NOMOVE | SWP_NOZORDER);
        resize(swapchain, "made 0x100: ResizeBuffers(0, 0)");
        SetWindowPos(normal, NULL, 0, 0, 320, 240, SWP_NOMOVE | SWP_NOZORDER);
        resize(swapchain, "made 320x240: ResizeBuffers(0, 0)");
        ShowWindow(normal, SW_MINIMIZE);
        resize(swapchain, "popup minimized: ResizeBuffers(0, 0)");
        IDXGISwapChain1_Release(swapchain);
    }

    DestroyWindow(popup);
    DestroyWindow(overlapped);
    DestroyWindow(normal);
}

static void test_d3d12(void)
{
    D3D12_COMMAND_QUEUE_DESC queue_desc = { 0 };
    ID3D12CommandQueue *queue;
    ID3D12Device *device12;
    HRESULT hr;

    hr = D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&device12);
    printf("D3D12CreateDevice %#lx\n", hr);
    if (FAILED(hr)) return;
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = ID3D12Device_CreateCommandQueue(device12, &queue_desc, &IID_ID3D12CommandQueue, (void **)&queue);
    if (SUCCEEDED(hr))
    {
        test_dxgi((IUnknown *)queue, "d3d12", DXGI_SWAP_EFFECT_FLIP_DISCARD);
        ID3D12CommandQueue_Release(queue);
    }
    ID3D12Device_Release(device12);
}

static void print_d3d9(const char *what, IDirect3DDevice9 *device, const D3DPRESENT_PARAMETERS *pp, HRESULT hr)
{
    IDirect3DSurface9 *backbuffer;
    D3DSURFACE_DESC desc = { 0 };

    if (device && SUCCEEDED(IDirect3DDevice9_GetBackBuffer(device, 0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer)))
    {
        IDirect3DSurface9_GetDesc(backbuffer, &desc);
        IDirect3DSurface9_Release(backbuffer);
    }
    printf("%-44s hr %#lx  parameters %ux%u  buffer %ux%u\n", what, hr, pp->BackBufferWidth, pp->BackBufferHeight,
           desc.Width, desc.Height);
}

static IDirect3DDevice9 *create_d3d9(IDirect3D9 *d3d, int window_width, int window_height,
                                     UINT width, UINT height, HWND *window)
{
    IDirect3DDevice9 *device = NULL;
    D3DPRESENT_PARAMETERS pp;
    char what[64];
    HRESULT hr;

    *window = CreateWindowA("static", "d3d9", WS_POPUP | WS_VISIBLE, 0, 0, window_width, window_height,
                            NULL, NULL, NULL, NULL);
    memset(&pp, 0, sizeof(pp));
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = *window;
    pp.BackBufferWidth = width;
    pp.BackBufferHeight = height;
    hr = IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, *window,
                                 D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device);
    sprintf(what, "d3d9 window %dx%d, created %ux%u", window_width, window_height, width, height);
    print_d3d9(what, device, &pp, hr);
    return device;
}

static void reset_d3d9(IDirect3DDevice9 *device, HWND window, int window_width, int window_height)
{
    D3DPRESENT_PARAMETERS pp;
    char what[64];
    HRESULT hr;

    SetWindowPos(window, NULL, 0, 0, window_width, window_height, SWP_NOMOVE | SWP_NOZORDER);
    memset(&pp, 0, sizeof(pp));
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window;
    hr = IDirect3DDevice9_Reset(device, &pp);
    sprintf(what, "d3d9 made %dx%d: Reset(0, 0)", window_width, window_height);
    print_d3d9(what, device, &pp, hr);
    hr = IDirect3DDevice9_Present(device, NULL, NULL, NULL, NULL);
    printf("%-44s present hr %#lx\n", "", hr);
}

static void test_d3d9(void)
{
    IDirect3DDevice9 *device;
    IDirect3D9 *d3d;
    HWND window;

    if (!(d3d = Direct3DCreate9(D3D_SDK_VERSION)))
    {
        printf("Direct3DCreate9 failed\n");
        return;
    }
    if ((device = create_d3d9(d3d, 0, 0, 0, 0, &window))) IDirect3DDevice9_Release(device);
    DestroyWindow(window);
    if ((device = create_d3d9(d3d, 100, 0, 0, 0, &window))) IDirect3DDevice9_Release(device);
    DestroyWindow(window);
    if ((device = create_d3d9(d3d, 100, 0, 0, 50, &window))) IDirect3DDevice9_Release(device);
    DestroyWindow(window);
    if ((device = create_d3d9(d3d, 0, 0, 64, 64, &window))) IDirect3DDevice9_Release(device);
    DestroyWindow(window);
    if ((device = create_d3d9(d3d, 320, 240, 0, 0, &window)))
    {
        reset_d3d9(device, window, 0, 0);
        reset_d3d9(device, window, 100, 0);
        reset_d3d9(device, window, 200, 150);
        IDirect3DDevice9_Release(device);
    }
    DestroyWindow(window);
    IDirect3D9_Release(d3d);
}

int main(void)
{
    IDXGIDevice *dxgi;
    IDXGIAdapter *adapter;
    HRESULT hr;

    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                           D3D11_SDK_VERSION, &device, NULL, NULL);
    if (FAILED(hr)) hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0,
                                           D3D11_SDK_VERSION, &device, NULL, NULL);
    printf("D3D11CreateDevice %#lx\n", hr);
    if (FAILED(hr)) return 1;
    ID3D11Device_QueryInterface(device, &IID_IDXGIDevice, (void **)&dxgi);
    IDXGIDevice_GetAdapter(dxgi, &adapter);
    IDXGIAdapter_GetParent(adapter, &IID_IDXGIFactory2, (void **)&factory);

    test_dxgi((IUnknown *)device, "d3d11", DXGI_SWAP_EFFECT_FLIP_DISCARD);
    test_dxgi((IUnknown *)device, "d3d11", DXGI_SWAP_EFFECT_DISCARD);
    test_d3d12();

    IDXGIFactory2_Release(factory);
    IDXGIAdapter_Release(adapter);
    IDXGIDevice_Release(dxgi);
    ID3D11Device_Release(device);

    test_d3d9();
    return 0;
}
