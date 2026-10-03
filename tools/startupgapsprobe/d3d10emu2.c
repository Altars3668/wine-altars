/* d3d10emu2: more of what a D3D11 device drops from the interface its context state does not emulate -- writing
 * maps, ClearState, GenerateMips, queries, the ID3D11DeviceContext1 methods; ID3D10 maps, ClearState and
 * GenerateMips.  Own device and resources only; prints results only. */
#define COBJMACROS
#include <windows.h>
#include <d3d11_1.h>
#include <d3d10_1.h>
#include <stdio.h>

static ID3D11Device1 *device;
static ID3D11DeviceContext1 *context;
static ID3DDeviceContextState *state10, *state11;

static void use(ID3DDeviceContextState *state)
{
    ID3D11DeviceContext1_SwapDeviceContextState(context, state, NULL);
}

static DWORD read_pixel(ID3D11Texture2D *texture, UINT sub)
{
    D3D11_TEXTURE2D_DESC desc;
    ID3D11Texture2D *staging;
    D3D11_MAPPED_SUBRESOURCE map;
    DWORD value = 0xdeadbeef;

    ID3D11Texture2D_GetDesc(texture, &desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    if (FAILED(ID3D11Device1_CreateTexture2D(device, &desc, NULL, &staging))) return value;
    use(state11);
    ID3D11DeviceContext1_CopySubresourceRegion(context, (ID3D11Resource *)staging, sub, 0, 0, 0, (ID3D11Resource *)texture, sub, NULL);
    if (SUCCEEDED(ID3D11DeviceContext1_Map(context, (ID3D11Resource *)staging, sub, D3D11_MAP_READ, 0, &map)))
    {
        value = *(DWORD *)map.pData;
        ID3D11DeviceContext1_Unmap(context, (ID3D11Resource *)staging, sub);
    }
    ID3D11Texture2D_Release(staging);
    return value;
}

int main(void)
{
    static const float red[4] = { 1, 0, 0, 1 }, blue[4] = { 0, 0, 1, 1 }, zero[4] = { 0 };
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0, level10 = D3D_FEATURE_LEVEL_10_1;
    D3D11_TEXTURE2D_DESC desc = { 4, 4, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, { 1, 0 }, D3D11_USAGE_DEFAULT,
                                  D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0 };
    ID3D11Texture2D *texture, *staging, *mipped;
    ID3D11RenderTargetView *rtv, *mip_rtv, *got;
    ID3D11ShaderResourceView *mip_srv;
    ID3D11Device *device0;
    ID3D11DeviceContext *context0;
    ID3D10Device *d3d10;
    D3D11_MAPPED_SUBRESOURCE map;
    D3D11_QUERY_DESC query_desc = { D3D11_QUERY_EVENT, 0 };
    ID3D11Query *query;
    DWORD data[16], i, value;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (FAILED(hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, &level, 1, D3D11_SDK_VERSION,
                                      &device0, NULL, &context0)) &&
        FAILED(hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0, &level, 1, D3D11_SDK_VERSION,
                                      &device0, NULL, &context0)))
        return 1;
    ID3D11Device_QueryInterface(device0, &IID_ID3D11Device1, (void **)&device);
    ID3D11DeviceContext_QueryInterface(context0, &IID_ID3D11DeviceContext1, (void **)&context);
    ID3D11Device1_CreateDeviceContextState(device, 0, &level, 1, D3D11_SDK_VERSION, &IID_ID3D11Device1, NULL, &state11);
    ID3D11Device1_CreateDeviceContextState(device, 0, &level10, 1, D3D11_SDK_VERSION, &IID_ID3D10Device1, NULL, &state10);
    ID3D11Device_QueryInterface(device0, &IID_ID3D10Device, (void **)&d3d10);

    ID3D11Device1_CreateTexture2D(device, &desc, NULL, &texture);
    ID3D11Device1_CreateRenderTargetView(device, (ID3D11Resource *)texture, NULL, &rtv);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
    ID3D11Device1_CreateTexture2D(device, &desc, NULL, &staging);
    desc.Width = desc.Height = 2;
    desc.MipLevels = 2;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    ID3D11Device1_CreateTexture2D(device, &desc, NULL, &mipped);
    ID3D11Device1_CreateRenderTargetView(device, (ID3D11Resource *)mipped, NULL, &mip_rtv);
    ID3D11Device1_CreateShaderResourceView(device, (ID3D11Resource *)mipped, NULL, &mip_srv);
    ID3D11Device1_CreateQuery(device, &query_desc, &query);

    printf("D3D11 calls with the D3D10.1 state:\n");
    /* a written map of a staging texture, copied with the D3D11 state */
    use(state10);
    hr = ID3D11DeviceContext1_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_WRITE, 0, &map);
    if (SUCCEEDED(hr)) { for (i = 0; i < 4; i++) ((DWORD *)map.pData)[i] = 0xff00ff00; ID3D11DeviceContext1_Unmap(context, (ID3D11Resource *)staging, 0); }
    use(state11);
    ID3D11DeviceContext1_ClearRenderTargetView(context, rtv, zero);
    ID3D11DeviceContext1_CopyResource(context, (ID3D11Resource *)texture, (ID3D11Resource *)staging);
    printf("  map for writing: %#lx, then copied: %08lx\n", hr, read_pixel(texture, 0));
    /* ClearState */
    use(state11);
    ID3D11DeviceContext1_OMSetRenderTargets(context, 1, &rtv, NULL);
    use(state10);
    ID3D11DeviceContext1_ClearState(context);
    use(state11);
    got = NULL;
    ID3D11DeviceContext1_OMGetRenderTargets(context, 1, &got, NULL);
    printf("  ClearState: the D3D11 state's target %s\n", got ? "kept" : "cleared");
    if (got) ID3D11RenderTargetView_Release(got);
    ID3D11DeviceContext1_ClearState(context);
    /* GenerateMips */
    use(state11);
    ID3D11DeviceContext1_ClearRenderTargetView(context, mip_rtv, red);
    {
        ID3D11RenderTargetView *mip1;
        D3D11_RENDER_TARGET_VIEW_DESC rd = { DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_RTV_DIMENSION_TEXTURE2D };
        rd.Texture2D.MipSlice = 1;
        ID3D11Device1_CreateRenderTargetView(device, (ID3D11Resource *)mipped, &rd, &mip1);
        ID3D11DeviceContext1_ClearRenderTargetView(context, mip1, zero);
        ID3D11RenderTargetView_Release(mip1);
    }
    use(state10);
    ID3D11DeviceContext1_GenerateMips(context, mip_srv);
    printf("  GenerateMips: mip 1 %08lx\n", read_pixel(mipped, 1));
    /* a query ended and asked for */
    use(state10);
    ID3D11DeviceContext1_End(context, (ID3D11Asynchronous *)query);
    value = 0xdeadbeef;
    for (i = 0; i < 100; i++)
        if ((hr = ID3D11DeviceContext1_GetData(context, (ID3D11Asynchronous *)query, &value, sizeof(value), 0)) != S_FALSE) break;
        else Sleep(10);
    printf("  End and GetData: %#lx value %#lx\n", hr, value);
    /* the ID3D11DeviceContext1 methods */
    for (i = 0; i < 16; i++) data[i] = 0xffff0000;
    use(state11);
    ID3D11DeviceContext1_ClearRenderTargetView(context, rtv, zero);
    use(state10);
    ID3D11DeviceContext1_UpdateSubresource1(context, (ID3D11Resource *)texture, 0, NULL, data, 16, 64, 0);
    printf("  UpdateSubresource1: %08lx\n", read_pixel(texture, 0));
    use(state10);
    ID3D11DeviceContext1_ClearView(context, (ID3D11View *)rtv, blue, NULL, 0);
    printf("  ClearView: %08lx\n", read_pixel(texture, 0));

    if (d3d10)
    {
        ID3D10Texture2D *staging10, *texture10;
        ID3D10ShaderResourceView *srv10;
        D3D10_MAPPED_TEXTURE2D map10;
        ID3D10RenderTargetView *rtv10, *got10;

        ID3D11Texture2D_QueryInterface(staging, &IID_ID3D10Texture2D, (void **)&staging10);
        ID3D11Texture2D_QueryInterface(texture, &IID_ID3D10Texture2D, (void **)&texture10);
        printf("D3D10 calls with the D3D11 state:\n");
        use(state11);
        hr = ID3D10Texture2D_Map(staging10, 0, D3D10_MAP_WRITE, 0, &map10);
        if (SUCCEEDED(hr)) { for (i = 0; i < 4; i++) ((DWORD *)map10.pData)[i] = 0xff0000ff; ID3D10Texture2D_Unmap(staging10, 0); }
        ID3D11DeviceContext1_ClearRenderTargetView(context, rtv, zero);
        ID3D11DeviceContext1_CopyResource(context, (ID3D11Resource *)texture, (ID3D11Resource *)staging);
        printf("  texture map for writing: %#lx, then copied: %08lx\n", hr, read_pixel(texture, 0));
        /* ClearState and GetPredication */
        use(state10);
        ID3D10Device_CreateRenderTargetView(d3d10, (ID3D10Resource *)texture10, NULL, &rtv10);
        ID3D10Device_OMSetRenderTargets(d3d10, 1, &rtv10, NULL);
        use(state11);
        ID3D10Device_ClearState(d3d10);
        use(state10);
        got10 = NULL;
        ID3D10Device_OMGetRenderTargets(d3d10, 1, &got10, NULL);
        printf("  ClearState: the D3D10 state's target %s\n", got10 ? "kept" : "cleared");
        if (got10) ID3D10RenderTargetView_Release(got10);
        /* GenerateMips */
        use(state11);
        ID3D11DeviceContext1_ClearRenderTargetView(context, mip_rtv, red);
        {
            ID3D11RenderTargetView *mip1;
            D3D11_RENDER_TARGET_VIEW_DESC rd = { DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_RTV_DIMENSION_TEXTURE2D };
            ID3D10Texture2D *mipped10;
            rd.Texture2D.MipSlice = 1;
            ID3D11Device1_CreateRenderTargetView(device, (ID3D11Resource *)mipped, &rd, &mip1);
            ID3D11DeviceContext1_ClearRenderTargetView(context, mip1, zero);
            ID3D11RenderTargetView_Release(mip1);
            ID3D11Texture2D_QueryInterface(mipped, &IID_ID3D10Texture2D, (void **)&mipped10);
            ID3D10Device_CreateShaderResourceView(d3d10, (ID3D10Resource *)mipped10, NULL, &srv10);
        }
        ID3D10Device_GenerateMips(d3d10, srv10);
        printf("  GenerateMips: mip 1 %08lx\n", read_pixel(mipped, 1));
    }
    printf("done\n");
    return 0;
}
