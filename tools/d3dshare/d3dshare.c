/* d3dshare -- does a shared D3D11 texture actually carry pixels between two
 * devices?
 *
 * Office's UI compositor renders on one D3D device and composites on another
 * through a texture created with D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX.  Under
 * Wine that path can fail three separate ways -- no shared handle, no keyed
 * mutex, no shared storage -- and the first two are loud while the third is
 * completely silent: the calls succeed and the pixels simply do not cross.
 *
 * So ask with no application involved.  Device B clears the shared surface to
 * a known colour, device A reads it back through a staging texture.  Either
 * the colour crossed or it did not.
 */
#include <windows.h>
#include <initguid.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <stdio.h>

#define OK(hr, what) do { if (FAILED(hr)) { printf("  %-44s FAILED %#lx\n", what, (unsigned long)(hr)); goto done; } \
                          else printf("  %-44s ok\n", what); } while (0)

static const D3D11_TEXTURE2D_DESC shared_desc =
{
    .Width = 64, .Height = 64, .MipLevels = 1, .ArraySize = 1,
    .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
    .SampleDesc = { 1, 0 },
    .Usage = D3D11_USAGE_DEFAULT,
    .BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
    .MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX,
};

int main(void)
{
    ID3D11Device *dev_a = NULL, *dev_b = NULL;
    ID3D11DeviceContext *ctx_a = NULL, *ctx_b = NULL;
    ID3D11Texture2D *shared_a = NULL, *shared_b = NULL, *staging = NULL;
    ID3D11RenderTargetView *rtv = NULL;
    IDXGIResource *dxgi_res = NULL;
    IDXGIKeyedMutex *km_a = NULL, *km_b = NULL;
    D3D11_TEXTURE2D_DESC desc;
    D3D11_MAPPED_SUBRESOURCE map;
    /* ClearRenderTargetView takes RGBA whatever the format is; the surface is
     * B8G8R8A8, so opaque red lands in memory as B=0 G=0 R=ff A=ff and reads
     * back through a little-endian UINT as 0xffff0000. */
    const float red[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
    HANDLE handle = NULL;
    unsigned int px;
    HRESULT hr;

    printf("== two devices ==\n");
    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0,
            D3D11_SDK_VERSION, &dev_a, NULL, &ctx_a);
    OK(hr, "D3D11CreateDevice(A)");
    hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0,
            D3D11_SDK_VERSION, &dev_b, NULL, &ctx_b);
    OK(hr, "D3D11CreateDevice(B)");
    printf("  device A %p, device B %p%s\n", dev_a, dev_b, dev_a == dev_b ? "  (SAME!)" : "");

    printf("== share the surface ==\n");
    hr = dev_a->lpVtbl->CreateTexture2D(dev_a, &shared_desc, NULL, &shared_a);
    OK(hr, "A: CreateTexture2D(SHARED_KEYEDMUTEX)");
    hr = shared_a->lpVtbl->QueryInterface(shared_a, &IID_IDXGIResource, (void **)&dxgi_res);
    OK(hr, "A: QueryInterface(IDXGIResource)");
    hr = dxgi_res->lpVtbl->GetSharedHandle(dxgi_res, &handle);
    OK(hr, "A: GetSharedHandle");
    printf("  handle %p\n", handle);
    hr = dev_b->lpVtbl->OpenSharedResource(dev_b, handle, &IID_ID3D11Texture2D, (void **)&shared_b);
    OK(hr, "B: OpenSharedResource");
    printf("  A's texture %p, B's texture %p%s\n", shared_a, shared_b,
            (void *)shared_a == (void *)shared_b ? "  (aliased -- same object)" : "  (distinct objects)");

    printf("== keyed mutex ==\n");
    hr = shared_a->lpVtbl->QueryInterface(shared_a, &IID_IDXGIKeyedMutex, (void **)&km_a);
    OK(hr, "A: QueryInterface(IDXGIKeyedMutex)");
    hr = shared_b->lpVtbl->QueryInterface(shared_b, &IID_IDXGIKeyedMutex, (void **)&km_b);
    OK(hr, "B: QueryInterface(IDXGIKeyedMutex)");
    printf("  A's mutex %p, B's mutex %p%s\n", km_a, km_b,
            (void *)km_a == (void *)km_b ? "  (one object)"
                                         : "  (one per device, as on Windows)");

    printf("== B writes, A reads ==\n");
    hr = km_b->lpVtbl->AcquireSync(km_b, 0, 1000);
    OK(hr, "B: AcquireSync(key 0)");
    hr = dev_b->lpVtbl->CreateRenderTargetView(dev_b, (ID3D11Resource *)shared_b, NULL, &rtv);
    OK(hr, "B: CreateRenderTargetView");
    ctx_b->lpVtbl->ClearRenderTargetView(ctx_b, rtv, red);
    printf("  %-44s issued\n", "B: ClearRenderTargetView(red)");
    ctx_b->lpVtbl->Flush(ctx_b);
    hr = km_b->lpVtbl->ReleaseSync(km_b, 1);
    OK(hr, "B: ReleaseSync(key 1)");

    hr = km_a->lpVtbl->AcquireSync(km_a, 1, 1000);
    OK(hr, "A: AcquireSync(key 1)");
    desc = shared_desc;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    hr = dev_a->lpVtbl->CreateTexture2D(dev_a, &desc, NULL, &staging);
    OK(hr, "A: CreateTexture2D(STAGING)");
    ctx_a->lpVtbl->CopyResource(ctx_a, (ID3D11Resource *)staging, (ID3D11Resource *)shared_a);
    hr = ctx_a->lpVtbl->Map(ctx_a, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &map);
    OK(hr, "A: Map(staging)");
    px = *(unsigned int *)map.pData;
    ctx_a->lpVtbl->Unmap(ctx_a, (ID3D11Resource *)staging, 0);
    km_a->lpVtbl->ReleaseSync(km_a, 0);

    printf("\n  pixel A reads back: %08x   (want ffff0000 = opaque red)\n", px);
    printf("  VERDICT: shared storage %s\n", px == 0xffff0000 ? "WORKS" : "DOES NOT CARRY PIXELS");

done:
    if (staging) staging->lpVtbl->Release(staging);
    if (rtv) rtv->lpVtbl->Release(rtv);
    if (km_b) km_b->lpVtbl->Release(km_b);
    if (km_a) km_a->lpVtbl->Release(km_a);
    if (shared_b) shared_b->lpVtbl->Release(shared_b);
    if (dxgi_res) dxgi_res->lpVtbl->Release(dxgi_res);
    if (shared_a) shared_a->lpVtbl->Release(shared_a);
    if (ctx_b) ctx_b->lpVtbl->Release(ctx_b);
    if (dev_b) dev_b->lpVtbl->Release(dev_b);
    if (ctx_a) ctx_a->lpVtbl->Release(ctx_a);
    if (dev_a) dev_a->lpVtbl->Release(dev_a);
    return 0;
}
