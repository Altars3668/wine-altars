/* 测量 D3D11_MAP_FLAG_DO_NOT_WAIT 是否仅在资源忙时拒绝，而不伪报成功。 */
#define COBJMACROS
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdio.h>
#include <stdlib.h>

static const char *outcome(const void *pointer)
{
    return pointer == (void *)0xdeadbeef ? "untouched" : pointer ? "set" : "zero";
}

int main(void)
{
    D3D11_TEXTURE2D_DESC desc = {0};
    D3D11_BUFFER_DESC buffer_desc = {0};
    D3D11_MAPPED_SUBRESOURCE mapped;
    ID3D11Texture2D *source = NULL, *staging = NULL, *staging_rw = NULL;
    ID3D11Buffer *dynamic = NULL;
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    D3D_FEATURE_LEVEL level = 0;
    HRESULT hr;
    unsigned int i, busy = 0, ready = 0, other = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0, NULL, 0, D3D11_SDK_VERSION,
                           &device, &level, &context);
    printf("WARP device %#lx level %#x\n", hr, level);
    if (FAILED(hr)) return 1;

    buffer_desc.ByteWidth = 4096;
    buffer_desc.Usage = D3D11_USAGE_DYNAMIC;
    buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = ID3D11Device_CreateBuffer(device, &buffer_desc, NULL, &dynamic);
    printf("dynamic buffer %#lx\n", hr);
    if (FAILED(hr)) return 2;
    memset(&mapped, 0, sizeof(mapped));
    mapped.pData = (void *)0xdeadbeef;
    hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)dynamic, 0, D3D11_MAP_WRITE_DISCARD,
                                  D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
    printf("dynamic DISCARD | DO_NOT_WAIT %#lx output %s\n", hr, outcome(mapped.pData));
    if (SUCCEEDED(hr)) ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)dynamic, 0);
    memset(&mapped, 0, sizeof(mapped));
    mapped.pData = (void *)0xdeadbeef;
    hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)dynamic, 0, D3D11_MAP_WRITE_NO_OVERWRITE,
                                  D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
    printf("dynamic NO_OVERWRITE | DO_NOT_WAIT %#lx output %s\n", hr, outcome(mapped.pData));
    if (SUCCEEDED(hr)) ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)dynamic, 0);
    memset(&mapped, 0, sizeof(mapped));
    mapped.pData = (void *)0xdeadbeef;
    hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)dynamic, 0, D3D11_MAP_WRITE_DISCARD, 2, &mapped);
    printf("dynamic DISCARD | unknown flag 2 %#lx output %s\n", hr, outcome(mapped.pData));
    if (SUCCEEDED(hr)) ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)dynamic, 0);

    desc.Width = 2048;
    desc.Height = 2048;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    hr = ID3D11Device_CreateTexture2D(device, &desc, NULL, &source);
    printf("GPU source %#lx\n", hr);
    if (FAILED(hr)) return 3;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = ID3D11Device_CreateTexture2D(device, &desc, NULL, &staging);
    printf("staging read %#lx\n", hr);
    if (FAILED(hr)) return 4;
    memset(&mapped, 0, sizeof(mapped));
    hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ,
                                  D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
    printf("idle staging READ | DO_NOT_WAIT %#lx mapped %d\n", hr, mapped.pData != NULL);
    if (SUCCEEDED(hr)) ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0);
    memset(&mapped, 0, sizeof(mapped));
    mapped.pData = (void *)0xdeadbeef;
    hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 2, &mapped);
    printf("idle staging READ | unknown flag 2 %#lx output %s\n", hr, outcome(mapped.pData));
    if (SUCCEEDED(hr)) ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0);
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
    hr = ID3D11Device_CreateTexture2D(device, &desc, NULL, &staging_rw);
    printf("staging read-write %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        memset(&mapped, 0, sizeof(mapped));
        hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging_rw, 0, D3D11_MAP_WRITE,
                                      D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        printf("idle staging WRITE | DO_NOT_WAIT %#lx mapped %d\n", hr, mapped.pData != NULL);
        if (SUCCEEDED(hr)) ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging_rw, 0);
        memset(&mapped, 0, sizeof(mapped));
        hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging_rw, 0, D3D11_MAP_READ_WRITE,
                                      D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        printf("idle staging READ_WRITE | DO_NOT_WAIT %#lx mapped %d\n", hr, mapped.pData != NULL);
        if (SUCCEEDED(hr)) ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging_rw, 0);
    }

    for (i = 0; i < 10; i++)
    {
        ID3D11DeviceContext_CopyResource(context, (ID3D11Resource *)staging, (ID3D11Resource *)source);
        memset(&mapped, 0, sizeof(mapped));
        hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ,
                                      D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
        if (hr == DXGI_ERROR_WAS_STILL_DRAWING) busy++;
        else if (SUCCEEDED(hr)) { ready++; ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0); }
        else other++;
        if (hr == DXGI_ERROR_WAS_STILL_DRAWING)
        {
            hr = ID3D11DeviceContext_Map(context, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &mapped);
            if (SUCCEEDED(hr)) ID3D11DeviceContext_Unmap(context, (ID3D11Resource *)staging, 0);
            else { printf("blocking map failed %#lx\n", hr); break; }
        }
    }
    printf("copy then DO_NOT_WAIT: busy %u ready %u other %u\n", busy, ready, other);

    if (staging_rw) ID3D11Texture2D_Release(staging_rw);
    ID3D11Texture2D_Release(staging);
    ID3D11Texture2D_Release(source);
    ID3D11Buffer_Release(dynamic);
    ID3D11DeviceContext_Release(context);
    ID3D11Device_Release(device);
    printf("done\n");
    return 0;
}
