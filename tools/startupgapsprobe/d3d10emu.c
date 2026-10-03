/* d3d10emu: what a D3D11 device does with the calls of the interface its active context state does not emulate --
 * D3D11 context calls while a state emulating ID3D10Device1 is active, ID3D10Device calls while a D3D11 state is --
 * clears, updates, copies, maps, state set and got.  Uses only its own device and resources; prints results only. */
#define COBJMACROS
#include <windows.h>
#include <d3d11_1.h>
#include <d3d10_1.h>
#include <stdio.h>

static ID3D11Device1 *device;
static ID3D11DeviceContext1 *context;
static ID3DDeviceContextState *state10, *state11;
static ID3D11Texture2D *texture, *staging, *source;
static ID3D11RenderTargetView *rtv;

/* the first pixel of the texture, read with a D3D11 state active */
static DWORD pixel(void)
{
    D3D11_MAPPED_SUBRESOURCE map;
    DWORD value = 0xdeadbeef;
    ID3DDeviceContextState *prev = NULL;

    ID3D11DeviceContext1_SwapDeviceContextState(context, state11, &prev);
    ID3D11DeviceContext1_CopyResource(context, (ID3D11Resource *)staging, (ID3D11Resource *)texture);
    if (SUCCEEDED(ID3D11DeviceContext1_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map)))
    {
        value = *(DWORD *)map.pData;
        ID3D11DeviceContext1_Unmap(context, (ID3D11Resource *)staging, 0);
    }
    ID3D11DeviceContext1_SwapDeviceContextState(context, prev, NULL);
    if (prev) ID3DDeviceContextState_Release(prev);
    return value;
}

static void reset(DWORD value)
{
    float colour[4] = { (value & 0xff) / 255.0f, ((value >> 8) & 0xff) / 255.0f, ((value >> 16) & 0xff) / 255.0f,
                        (value >> 24) / 255.0f };
    ID3DDeviceContextState *prev = NULL;

    ID3D11DeviceContext1_SwapDeviceContextState(context, state11, &prev);
    ID3D11DeviceContext1_ClearRenderTargetView(context, rtv, colour);
    ID3D11DeviceContext1_SwapDeviceContextState(context, prev, NULL);
    if (prev) ID3DDeviceContextState_Release(prev);
}

int main(void)
{
    static const float red[4] = { 1.0f, 0.0f, 0.0f, 1.0f }, green[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0, level10 = D3D_FEATURE_LEVEL_10_1, chosen;
    D3D11_TEXTURE2D_DESC desc = { 4, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT,
                                  D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    ID3D11Device *device0;
    ID3D11DeviceContext *context0;
    ID3D10Device *d3d10 = NULL;
    ID3D10RenderTargetView *rtv10 = NULL;
    ID3D10Texture2D *texture10 = NULL, *source10 = NULL;
    ID3DDeviceContextState *prev = NULL;
    D3D11_MAPPED_SUBRESOURCE map;
    DWORD data[16], i;
    HRESULT hr;
    D3D_DRIVER_TYPE types[] = { D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP };

    setvbuf(stdout, NULL, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    for (i = 0; i < 2; i++)
        if (SUCCEEDED(hr = D3D11CreateDevice(NULL, types[i], NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, &level, 1,
                                             D3D11_SDK_VERSION, &device0, NULL, &context0))) break;
    printf("device: %s %#lx\n", i ? "warp" : "hardware", hr);
    if (FAILED(hr)) return 1;
    ID3D11Device_QueryInterface(device0, &IID_ID3D11Device1, (void **)&device);
    ID3D11DeviceContext_QueryInterface(context0, &IID_ID3D11DeviceContext1, (void **)&context);
    hr = ID3D11Device_QueryInterface(device0, &IID_ID3D10Device, (void **)&d3d10);
    printf("ID3D10Device before a D3D10 state: %#lx\n", hr);
    if (d3d10) { ID3D10Device_Release(d3d10); d3d10 = NULL; }
    hr = ID3D11Device1_CreateDeviceContextState(device, 0, &level, 1, D3D11_SDK_VERSION, &IID_ID3D11Device1, &chosen, &state11);
    printf("D3D11 state %#lx\n", hr);
    hr = ID3D11Device1_CreateDeviceContextState(device, 0, &level10, 1, D3D11_SDK_VERSION, &IID_ID3D10Device1, &chosen, &state10);
    printf("D3D10.1 state %#lx, level %#x\n", hr, chosen);
    hr = ID3D11Device_QueryInterface(device0, &IID_ID3D10Device, (void **)&d3d10);
    printf("ID3D10Device after: %#lx\n", hr);

    ID3D11Device1_CreateTexture2D(device, &desc, NULL, &texture);
    ID3D11Device1_CreateTexture2D(device, &desc, NULL, &source);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
    ID3D11Device1_CreateTexture2D(device, &desc, NULL, &staging);
    ID3D11Device1_CreateRenderTargetView(device, (ID3D11Resource *)texture, NULL, &rtv);
    if (d3d10)
    {
        ID3D11Texture2D_QueryInterface(texture, &IID_ID3D10Texture2D, (void **)&texture10);
        ID3D11Texture2D_QueryInterface(source, &IID_ID3D10Texture2D, (void **)&source10);
        if (texture10) ID3D10Device_CreateRenderTargetView(d3d10, (ID3D10Resource *)texture10, NULL, &rtv10);
    }
    printf("resources: rtv %d, d3d10 texture %d, d3d10 rtv %d\n", !!rtv, !!texture10, !!rtv10);
    /* the source holds green */
    {
        ID3D11RenderTargetView *source_rtv;
        ID3D11Device1_CreateRenderTargetView(device, (ID3D11Resource *)source, NULL, &source_rtv);
        ID3D11DeviceContext1_SwapDeviceContextState(context, state11, &prev);
        ID3D11DeviceContext1_ClearRenderTargetView(context, source_rtv, green);
        ID3D11RenderTargetView_Release(source_rtv);
    }

    printf("with the D3D11 state, as a check:\n");
    reset(0);
    ID3D11DeviceContext1_ClearRenderTargetView(context, rtv, red);
    printf("  d3d11 clear: %08lx\n", pixel());

    ID3D11DeviceContext1_SwapDeviceContextState(context, state10, NULL);
    printf("with the D3D10.1 state, D3D11 calls:\n");
    reset(0);
    ID3D11DeviceContext1_ClearRenderTargetView(context, rtv, red);
    printf("  d3d11 clear: %08lx\n", pixel());
    reset(0);
    for (i = 0; i < 16; i++) data[i] = 0xff0000ff;
    ID3D11DeviceContext1_UpdateSubresource(context, (ID3D11Resource *)texture, 0, NULL, data, 16, 64);
    printf("  d3d11 update: %08lx\n", pixel());
    reset(0);
    ID3D11DeviceContext1_CopyResource(context, (ID3D11Resource *)texture, (ID3D11Resource *)source);
    printf("  d3d11 copy resource: %08lx\n", pixel());
    reset(0);
    ID3D11DeviceContext1_CopySubresourceRegion(context, (ID3D11Resource *)texture, 0, 0, 0, 0, (ID3D11Resource *)source, 0, NULL);
    printf("  d3d11 copy region: %08lx\n", pixel());
    hr = ID3D11DeviceContext1_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map);
    printf("  d3d11 map: %#lx %s\n", hr, SUCCEEDED(hr) && map.pData ? "data" : "no data");
    if (SUCCEEDED(hr)) ID3D11DeviceContext1_Unmap(context, (ID3D11Resource *)staging, 0);
    {
        ID3D11RenderTargetView *got = (ID3D11RenderTargetView *)0xdeadbeef;
        ID3D11DeviceContext1_OMSetRenderTargets(context, 1, &rtv, NULL);
        ID3D11DeviceContext1_OMGetRenderTargets(context, 1, &got, NULL);
        printf("  d3d11 set and get render target: %s\n", got == rtv ? "the view" : got ? "something else" : "NULL");
        if (got && got != (ID3D11RenderTargetView *)0xdeadbeef) ID3D11RenderTargetView_Release(got);
    }
    if (d3d10)
    {
        printf("with the D3D10.1 state, D3D10 calls:\n");
        reset(0);
        if (rtv10) ID3D10Device_ClearRenderTargetView(d3d10, rtv10, red);
        printf("  d3d10 clear: %08lx\n", pixel());
        reset(0);
        if (texture10) ID3D10Device_UpdateSubresource(d3d10, (ID3D10Resource *)texture10, 0, NULL, data, 16, 64);
        printf("  d3d10 update: %08lx\n", pixel());
        reset(0);
        if (texture10 && source10) ID3D10Device_CopyResource(d3d10, (ID3D10Resource *)texture10, (ID3D10Resource *)source10);
        printf("  d3d10 copy: %08lx\n", pixel());
    }

    ID3D11DeviceContext1_SwapDeviceContextState(context, state11, NULL);
    if (d3d10)
    {
        printf("with the D3D11 state, D3D10 calls:\n");
        reset(0);
        if (rtv10) ID3D10Device_ClearRenderTargetView(d3d10, rtv10, red);
        printf("  d3d10 clear: %08lx\n", pixel());
        reset(0);
        if (texture10) ID3D10Device_UpdateSubresource(d3d10, (ID3D10Resource *)texture10, 0, NULL, data, 16, 64);
        printf("  d3d10 update: %08lx\n", pixel());
        reset(0);
        if (texture10 && source10) ID3D10Device_CopyResource(d3d10, (ID3D10Resource *)texture10, (ID3D10Resource *)source10);
        printf("  d3d10 copy: %08lx\n", pixel());
        {
            ID3D10RenderTargetView *got = (ID3D10RenderTargetView *)0xdeadbeef;
            ID3D10Device_OMSetRenderTargets(d3d10, 1, &rtv10, NULL);
            ID3D10Device_OMGetRenderTargets(d3d10, 1, &got, NULL);
            printf("  d3d10 set and get render target: %s\n", got == rtv10 ? "the view" : got ? "something else" : "NULL");
            if (got && got != (ID3D10RenderTargetView *)0xdeadbeef) ID3D10RenderTargetView_Release(got);
        }
    }
    printf("done\n");
    return 0;
}
