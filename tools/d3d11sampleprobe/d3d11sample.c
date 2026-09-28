/*
 * Where Direct3D 11 interpolates a pixel shader's inputs when the shader runs once a sample: a shader that reads
 * SV_SampleIndex writes the square of an attribute's fraction to a multisampled target, which is resolved and read.
 * At the pixel's centre that is 0.25 in every sample; at the samples, their mean.  Also with the attribute marked
 * "sample", and with neither and no SV_SampleIndex -- that one first and last, as a renderer may carry
 * over drawing per sample -- and what SV_Position holds in each case.
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <stdio.h>

static const char vs_code[] =
    "void main(uint id : SV_VertexID, out float2 v : TEXCOORD, out float4 position : SV_Position)\n"
    "{\n"
    "    float2 p = float2((id << 1) & 2, id & 2);\n"
    "    position = float4(p * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);\n"
    "    v = p * 16.0f;\n"
    "}\n";

static const char *ps_codes[] =
{
    /* per pixel, first */
    "float4 main(float2 v : TEXCOORD, float4 position : SV_Position) : SV_Target\n"
    "{\n"
    "    float f = frac(v.x), g = frac(position.x);\n"
    "    return float4(f * f, g * g, 0.0f, 1.0f);\n"
    "}\n",
    /* per sample, plain attribute */
    "float4 main(float2 v : TEXCOORD, float4 position : SV_Position, uint s : SV_SampleIndex) : SV_Target\n"
    "{\n"
    "    float f = frac(v.x), g = frac(position.x);\n"
    "    return float4(f * f, g * g, s > 100 ? 1.0f : 0.0f, 1.0f);\n"
    "}\n",
    /* per sample, "sample" attribute */
    "float4 main(sample float2 v : TEXCOORD, float4 position : SV_Position, uint s : SV_SampleIndex) : SV_Target\n"
    "{\n"
    "    float f = frac(v.x), g = frac(position.x);\n"
    "    return float4(f * f, g * g, s > 100 ? 1.0f : 0.0f, 1.0f);\n"
    "}\n",
    /* "sample" attribute, no sample index */
    "float4 main(sample float2 v : TEXCOORD, float4 position : SV_Position) : SV_Target\n"
    "{\n"
    "    float f = frac(v.x), g = frac(position.x);\n"
    "    return float4(f * f, g * g, 0.0f, 1.0f);\n"
    "}\n",
    /* per pixel */
    "float4 main(float2 v : TEXCOORD, float4 position : SV_Position) : SV_Target\n"
    "{\n"
    "    float f = frac(v.x), g = frac(position.x);\n"
    "    return float4(f * f, g * g, 0.0f, 1.0f);\n"
    "}\n",
};
static const char *ps_names[] = {"per pixel first", "sample index", "sample attribute and index", "sample attribute",
        "per pixel"};

int main(void)
{
    static const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1};
    ID3D11Texture2D *ms, *resolved, *staging;
    D3D11_MAPPED_SUBRESOURCE map;
    D3D11_TEXTURE2D_DESC desc;
    ID3D11RenderTargetView *rtv;
    ID3D11DeviceContext *context;
    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps;
    ID3D10Blob *blob, *errors;
    ID3D11Device *device;
    D3D11_VIEWPORT vp;
    unsigned int i, count;
    const BYTE *row;
    HRESULT hr;

    if (FAILED(hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, levels, ARRAY_SIZE(levels),
            D3D11_SDK_VERSION, &device, NULL, &context)))
    {
        printf("D3D11CreateDevice %#lx\n", hr);
        return 1;
    }
    printf("feature level %#x\n", ID3D11Device_GetFeatureLevel(device));

    if (FAILED(hr = D3DCompile(vs_code, sizeof(vs_code) - 1, "vs", NULL, NULL, "main", "vs_4_1", 0, 0, &blob, &errors)))
    {
        printf("vs: %#lx %s\n", hr, errors ? (char *)ID3D10Blob_GetBufferPointer(errors) : "");
        return 1;
    }
    ID3D11Device_CreateVertexShader(device, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob),
            NULL, &vs);
    ID3D10Blob_Release(blob);

    for (count = 4; count <= 8; count += 4)
    {
        desc.Width = 16;
        desc.Height = 16;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = count;
        desc.SampleDesc.Quality = 0;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;
        if (FAILED(hr = ID3D11Device_CreateTexture2D(device, &desc, NULL, &ms)))
        {
            printf("%u samples: %#lx\n", count, hr);
            continue;
        }
        ID3D11Device_CreateRenderTargetView(device, (ID3D11Resource *)ms, NULL, &rtv);
        desc.SampleDesc.Count = 1;
        ID3D11Device_CreateTexture2D(device, &desc, NULL, &resolved);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ID3D11Device_CreateTexture2D(device, &desc, NULL, &staging);

        for (i = 0; i < ARRAY_SIZE(ps_codes); ++i)
        {
            if (FAILED(hr = D3DCompile(ps_codes[i], strlen(ps_codes[i]), "ps", NULL, NULL, "main", "ps_4_1",
                    0, 0, &blob, &errors)))
            {
                printf("%u samples, %s: compile %#lx %s\n", count, ps_names[i], hr,
                        errors ? (char *)ID3D10Blob_GetBufferPointer(errors) : "");
                continue;
            }
            ID3D11Device_CreatePixelShader(device, ID3D10Blob_GetBufferPointer(blob), ID3D10Blob_GetBufferSize(blob),
                    NULL, &ps);
            ID3D10Blob_Release(blob);

            vp.TopLeftX = 0.0f;
            vp.TopLeftY = 0.0f;
            vp.Width = 16.0f;
            vp.Height = 16.0f;
            vp.MinDepth = 0.0f;
            vp.MaxDepth = 1.0f;
            ID3D11DeviceContext_RSSetViewports(context, 1, &vp);
            ID3D11DeviceContext_OMSetRenderTargets(context, 1, &rtv, NULL);
            ID3D11DeviceContext_IASetPrimitiveTopology(context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ID3D11DeviceContext_VSSetShader(context, vs, NULL, 0);
            ID3D11DeviceContext_PSSetShader(context, ps, NULL, 0);
            ID3D11DeviceContext_Draw(context, 3, 0);
            ID3D11DeviceContext_ResolveSubresource(context, (ID3D11Resource *)resolved, 0, (ID3D11Resource *)ms, 0,
                    DXGI_FORMAT_R8G8B8A8_UNORM);
            ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging, (ID3D11Resource *)resolved);
            ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map);
            row = (const BYTE *)map.pData + 5 * map.RowPitch;
            /* the attribute and the position at the centre: 0.25, 64 */
            printf("%u samples, %s: attribute %u position %u\n", count, ps_names[i], row[5 * 4], row[5 * 4 + 1]);
            ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0);
            ID3D11PixelShader_Release(ps);
        }

        ID3D11Texture2D_Release(staging);
        ID3D11Texture2D_Release(resolved);
        ID3D11RenderTargetView_Release(rtv);
        ID3D11Texture2D_Release(ms);
    }

    ID3D11VertexShader_Release(vs);
    ID3D11DeviceContext_Release(context);
    ID3D11Device_Release(device);
    printf("done\n");
    return 0;
}
