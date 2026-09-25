/*
 * scissorprobe - what Direct3D 11 draws through a scissor rectangle that is
 * inside out.
 *
 * Office's AirSpace sets scissor rectangles whose bottom is above their top
 * while it scrolls, and wined3d hands them to OpenGL as a negative height,
 * which OpenGL refuses: the scissor it had stays, and draws go where the
 * rectangle meant to stop them.  This draws a full-screen triangle through
 * each kind of rectangle and reads back what landed, on WARP so it asks
 * Windows the same question on any machine, in any session.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <d3d10_1.h>
#include <d3dcompiler.h>
#include <stdio.h>
#include <string.h>

static const char shaders[] =
    "float4 vs(uint id : SV_VertexID) : SV_Position\n"
    "{\n"
    "    float2 p = float2((id << 1) & 2, id & 2);\n"
    "    return float4(p * float2(2, -2) + float2(-1, 1), 0, 1);\n"
    "}\n"
    "float4 ps() : SV_Target { return float4(0, 1, 0, 1); }\n";

typedef HRESULT (WINAPI *compile_fn)(const void *, SIZE_T, const char *, const D3D_SHADER_MACRO *, ID3DInclude *,
                                     const char *, const char *, UINT, UINT, ID3DBlob **, ID3DBlob **);

static ID3D11Device *device;
static ID3D11DeviceContext *context;
static ID3D11Texture2D *target, *staging;
static ID3D11RenderTargetView *rtv;

static DWORD draw_through(const D3D11_RECT *rect)
{
    static const float red[4] = {1, 0, 0, 1};
    D3D11_MAPPED_SUBRESOURCE map;
    DWORD pixel;

    ID3D11DeviceContext_ClearRenderTargetView(context, rtv, red);
    ID3D11DeviceContext_RSSetScissorRects(context, 1, rect);
    ID3D11DeviceContext_Draw(context, 3, 0);
    ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging, (ID3D11Resource *)target);
    ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map);
    pixel = ((DWORD *)((BYTE *)map.pData + 8 * map.RowPitch))[8];
    ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0);
    return pixel & 0xffffff;
}

static void check(const char *what, LONG left, LONG top, LONG right, LONG bottom)
{
    D3D11_RECT rect = {left, top, right, bottom};
    DWORD pixel = draw_through(&rect);

    D3D11_RECT got = {-1, -1, -1, -1};
    UINT count = 1;

    ID3D11DeviceContext_RSGetScissorRects(context, &count, &got);
    printf("%-34s (%ld,%ld)-(%ld,%ld): pixel %06lx %s, reads back %u (%ld,%ld)-(%ld,%ld)\n", what, left, top, right,
           bottom, pixel, pixel == 0x00ff00 ? "drawn" : pixel == 0xff0000 ? "not drawn" : "?", count, got.left,
           got.top, got.right, got.bottom);
}

int main(int argc, char **argv)
{
    D3D11_TEXTURE2D_DESC desc = {16, 16, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT,
                                 D3D11_BIND_RENDER_TARGET};
    D3D11_RASTERIZER_DESC rs_desc = {D3D11_FILL_SOLID, D3D11_CULL_NONE, FALSE, 0, 0.0f, 0.0f, TRUE, TRUE};
    D3D11_VIEWPORT vp = {0, 0, 16, 16, 0, 1};
    ID3DBlob *vs_blob, *ps_blob, *errors;
    ID3D11RasterizerState *rs;
    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps;
    compile_fn compile;
    HMODULE module;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1 && !strcmp(argv[1], "hw")) hr = E_FAIL;
    else hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &device, NULL, &context);
    if (FAILED(hr))
    {
        printf("no WARP device (%#lx), trying hardware\n", hr);
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, D3D11_SDK_VERSION, &device, NULL,
                               &context);
    }
    printf("D3D11CreateDevice %#lx\n", hr);
    if (FAILED(hr)) return 1;
    if (!(module = LoadLibraryA("d3dcompiler_47.dll")) || !(compile = (void *)GetProcAddress(module, "D3DCompile")))
    {
        printf("no d3dcompiler_47\n");
        return 1;
    }
    if (FAILED(hr = compile(shaders, sizeof(shaders) - 1, NULL, NULL, NULL, "vs", "vs_4_0", 0, 0, &vs_blob, &errors)) ||
        FAILED(hr = compile(shaders, sizeof(shaders) - 1, NULL, NULL, NULL, "ps", "ps_4_0", 0, 0, &ps_blob, &errors)))
    {
        printf("compile %#lx\n", hr);
        return 1;
    }
    ID3D11Device_CreateVertexShader(device, ID3D10Blob_GetBufferPointer(vs_blob), ID3D10Blob_GetBufferSize(vs_blob),
                                    NULL, &vs);
    ID3D11Device_CreatePixelShader(device, ID3D10Blob_GetBufferPointer(ps_blob), ID3D10Blob_GetBufferSize(ps_blob),
                                   NULL, &ps);
    ID3D11Device_CreateTexture2D(device, &desc, NULL, &target);
    ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)target, NULL, &rtv);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Device_CreateTexture2D(device, &desc, NULL, &staging);
    ID3D11Device_CreateRasterizerState(device, &rs_desc, &rs);

    ID3D11DeviceContext_OMSetRenderTargets(context, 1, &rtv, NULL);
    ID3D11DeviceContext_RSSetViewports(context, 1, &vp);
    ID3D11DeviceContext_RSSetState(context, rs);
    ID3D11DeviceContext_IASetPrimitiveTopology(context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(context, vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(context, ps, NULL, 0);

    check("whole target", 0, 0, 16, 16);
    check("bottom above top", 0, 12, 16, 4);
    check("then the whole target again", 0, 0, 16, 16);
    check("right left of left", 12, 0, 4, 16);
    check("both inside out", 12, 12, 4, 4);
    check("empty", 8, 8, 8, 8);
    check("covering the pixel, then inside out", 0, 0, 16, 16);
    check("inside out", 0, 9, 16, 7);
    check("negative corner, inside out", -20, -5, 16, -30);
    check("larger than the target", -100, -100, 100, 100);
    /* which of ignored, put right, or the last one kept: a small one first, away from the pixel */
    check("away from the pixel", 0, 0, 4, 4);
    check("then inside out over it", 0, 12, 16, 4);
    check("away from the pixel", 0, 0, 4, 4);
    check("then inside out, put right away from it", 4, 12, 12, 10);
    check("away from the pixel", 0, 0, 4, 4);
    check("then right left of left, away from it", 10, 0, 9, 16);
    check("then zero width", 8, 0, 8, 16);
    {
        /* with more than one, whether one inside out drops the others with it */
        D3D11_RECT two[2] = {{1, 1, 5, 5}, {2, 2, 6, 6}}, mixed[2] = {{3, 3, 7, 7}, {9, 9, 8, 8}}, got[4];
        UINT count, i;

        ID3D11DeviceContext_RSSetScissorRects(context, 2, two);
        ID3D11DeviceContext_RSSetScissorRects(context, 2, mixed);
        count = 4;
        memset(got, 0xcc, sizeof(got));
        ID3D11DeviceContext_RSGetScissorRects(context, &count, got);
        printf("two, then one of two inside out: reads back %u", count);
        for (i = 0; i < count && i < 4; i++) printf(" (%ld,%ld)-(%ld,%ld)", got[i].left, got[i].top, got[i].right, got[i].bottom);
        printf("\n");
        ID3D11DeviceContext_RSSetScissorRects(context, 1, &mixed[1]);
        count = 4;
        ID3D11DeviceContext_RSGetScissorRects(context, &count, got);
        printf("then one inside out alone: reads back %u", count);
        for (i = 0; i < count && i < 4; i++) printf(" (%ld,%ld)-(%ld,%ld)", got[i].left, got[i].top, got[i].right, got[i].bottom);
        printf("\n");
        ID3D11DeviceContext_RSSetScissorRects(context, 0, NULL);
        count = 4;
        ID3D11DeviceContext_RSGetScissorRects(context, &count, got);
        printf("then none: reads back %u\n", count);
    }
    {
        /* Direct3D 10 takes rectangles the same way */
        HRESULT (WINAPI *create10)(IDXGIAdapter *, D3D10_DRIVER_TYPE, HMODULE, UINT, D3D10_FEATURE_LEVEL1, UINT,
                                   ID3D10Device1 **);
        D3D10_RECT valid = {1, 1, 5, 5}, inverted = {9, 9, 8, 8}, got = {-1, -1, -1, -1};
        ID3D10Device1 *device10;
        UINT count = 1;

        create10 = (void *)GetProcAddress(LoadLibraryA("d3d10_1.dll"), "D3D10CreateDevice1");
        hr = create10(NULL, D3D10_DRIVER_TYPE_WARP, NULL, 0, D3D10_FEATURE_LEVEL_10_0, D3D10_1_SDK_VERSION, &device10);
        if (FAILED(hr))
            hr = create10(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0, D3D10_FEATURE_LEVEL_10_0, D3D10_1_SDK_VERSION,
                          &device10);
        printf("D3D10CreateDevice1 %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            ID3D10Device1_RSSetScissorRects(device10, 1, &valid);
            ID3D10Device1_RSSetScissorRects(device10, 1, &inverted);
            ID3D10Device1_RSGetScissorRects(device10, &count, &got);
            printf("d3d10: valid, then inside out: reads back %u (%ld,%ld)-(%ld,%ld)\n", count, got.left, got.top,
                   got.right, got.bottom);
            {
                D3D10_RECT four[4];
                count = 4;
                memset(four, 0xcc, sizeof(four));
                ID3D10Device1_RSGetScissorRects(device10, &count, four);
                printf("d3d10: asked for 4 with room for them: count %u, second (%ld,%ld)-(%ld,%ld)\n", count,
                       four[1].left, four[1].top, four[1].right, four[1].bottom);
            }
            ID3D10Device1_Release(device10);
        }
    }
    printf("done\n");
    return 0;
}
