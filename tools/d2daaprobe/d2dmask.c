/*
 * What ID2D1RenderTarget::FillOpacityMask() draws: a 4 by 4 alpha mask, scaled up, through a white brush, onto
 * transparent black -- whole, a part of it, with no destination, with rectangles that are inverted or reach
 * outside the mask, through the 1.1 method, from a BGRA and a 192 DPI mask, and what happens when the target is
 * not aliased.  Alpha is printed along a row.  Last, whether DrawBitmap() treats an inverted source rectangle the
 * same way.
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <d2d1_1.h>
#include <wincodec.h>
#include <stdio.h>

static ID2D1Factory *factory;
static IWICImagingFactory *wic;
static ID2D1RenderTarget *rt;
static ID2D1DeviceContext *context;
static ID2D1SolidColorBrush *brush;
static IWICBitmap *bitmap;

static void row(const char *name, unsigned int y, unsigned int x0, unsigned int x1)
{
    WICRect rect = {0, 0, 32, 32};
    IWICBitmapLock *lock;
    UINT size, stride;
    unsigned int x;
    BYTE *data;

    IWICBitmap_Lock(bitmap, &rect, WICBitmapLockRead, &lock);
    IWICBitmapLock_GetDataPointer(lock, &size, &data);
    IWICBitmapLock_GetStride(lock, &stride);
    printf("%s y %u x %u..%u:", name, y, x0, x1);
    for (x = x0; x <= x1; ++x)
        printf(" %u", data[y * stride + x * 4 + 3]);
    printf("\n");
    IWICBitmapLock_Release(lock);
}

/* One fill through the 1.0 method, or the 1.1 method when content is -1. */
static void fill(const char *name, ID2D1Bitmap *mask, int content, const D2D1_RECT_F *dst, const D2D1_RECT_F *src)
{
    D2D1_COLOR_F clear = {0.0f, 0.0f, 0.0f, 0.0f};
    HRESULT hr;

    ID2D1RenderTarget_BeginDraw(rt);
    ID2D1RenderTarget_Clear(rt, &clear);
    if (content < 0)
        ID2D1DeviceContext_FillOpacityMask(context, mask, (ID2D1Brush *)brush, dst, src);
    else
        ID2D1RenderTarget_FillOpacityMask(rt, mask, (ID2D1Brush *)brush, content, dst, src);
    hr = ID2D1RenderTarget_EndDraw(rt, NULL, NULL);
    printf("%s: %#lx\n", name, hr);
}

static ID2D1Bitmap *create_mask(DXGI_FORMAT format, float dpi)
{
    static const BYTE mask_data[16] =
    {
          0,  64, 128, 255,
         64, 128, 255,   0,
        128, 255,   0,  64,
        255,   0,  64, 128,
    };
    D2D1_BITMAP_PROPERTIES desc;
    D2D1_SIZE_U size = {4, 4};
    DWORD bgra[16];
    ID2D1Bitmap *mask;
    unsigned int i;
    HRESULT hr;

    desc.pixelFormat.format = format;
    desc.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    desc.dpiX = desc.dpiY = dpi;
    for (i = 0; i < 16; ++i)
        bgra[i] = (DWORD)mask_data[i] << 24;
    if (format == DXGI_FORMAT_A8_UNORM)
        hr = ID2D1RenderTarget_CreateBitmap(rt, size, mask_data, 4, &desc, &mask);
    else
        hr = ID2D1RenderTarget_CreateBitmap(rt, size, bgra, 16, &desc, &mask);
    if (FAILED(hr))
    {
        printf("CreateBitmap %#x %#lx\n", format, hr);
        exit(1);
    }
    return mask;
}

int main(void)
{
    static const D2D1_RECT_F dst = {4.0f, 4.0f, 20.0f, 20.0f}, src = {1.0f, 1.0f, 3.0f, 3.0f};
    static const D2D1_RECT_F src_outside = {2.0f, 2.0f, 6.0f, 6.0f}, src_inverted = {3.0f, 1.0f, 1.0f, 3.0f};
    static const D2D1_RECT_F dst_inverted = {20.0f, 4.0f, 4.0f, 20.0f};
    static const D2D1_RECT_F src_beyond = {5.0f, 5.0f, 8.0f, 8.0f}, src_empty = {1.0f, 1.0f, 1.0f, 3.0f};
    D2D1_COLOR_F clear = {0.0f, 0.0f, 0.0f, 0.0f};
    D2D1_RENDER_TARGET_PROPERTIES desc = {0};
    D2D1_COLOR_F white = {1.0f, 1.0f, 1.0f, 1.0f};
    ID2D1Bitmap *mask, *mask_bgra, *mask_dpi;
    HRESULT hr;

    CoInitialize(NULL);
    CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&wic);
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory, NULL, (void **)&factory);
    IWICImagingFactory_CreateBitmap(wic, 32, 32, &GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &bitmap);
    desc.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    if (FAILED(hr = ID2D1Factory_CreateWicBitmapRenderTarget(factory, bitmap, &desc, &rt)))
    {
        printf("CreateWicBitmapRenderTarget %#lx\n", hr);
        return 1;
    }
    if (FAILED(hr = ID2D1RenderTarget_QueryInterface(rt, &IID_ID2D1DeviceContext, (void **)&context)))
    {
        printf("no ID2D1DeviceContext %#lx\n", hr);
        return 1;
    }
    ID2D1RenderTarget_CreateSolidColorBrush(rt, &white, NULL, &brush);
    mask = create_mask(DXGI_FORMAT_A8_UNORM, 96.0f);
    mask_bgra = create_mask(DXGI_FORMAT_B8G8R8A8_UNORM, 96.0f);
    mask_dpi = create_mask(DXGI_FORMAT_A8_UNORM, 192.0f);
    ID2D1RenderTarget_SetAntialiasMode(rt, D2D1_ANTIALIAS_MODE_ALIASED);

    /* Whole, four times as big. */
    fill("whole", mask, D2D1_OPACITY_MASK_CONTENT_GRAPHICS, &dst, NULL);
    row("whole", 5, 2, 21);
    row("whole", 9, 2, 21);
    row("whole", 18, 2, 21);

    /* The middle of it. */
    fill("part", mask, D2D1_OPACITY_MASK_CONTENT_GRAPHICS, &dst, &src);
    row("part", 5, 2, 21);
    row("part", 13, 2, 21);

    /* No destination: the mask's size, at the origin. */
    fill("no destination", mask, D2D1_OPACITY_MASK_CONTENT_TEXT_NATURAL, NULL, NULL);
    row("no destination", 0, 0, 5);
    row("no destination", 2, 0, 5);

    /* A source and no destination. */
    fill("source only", mask, D2D1_OPACITY_MASK_CONTENT_GRAPHICS, NULL, &src);
    row("source only", 0, 0, 5);
    row("source only", 1, 0, 5);
    row("source only", 2, 0, 5);

    /* A source reaching past the mask. */
    fill("source outside", mask, D2D1_OPACITY_MASK_CONTENT_GRAPHICS, &dst, &src_outside);
    row("source outside", 5, 2, 21);
    row("source outside", 17, 2, 21);

    /* A source entirely outside the mask, and an empty one. */
    fill("source beyond", mask, D2D1_OPACITY_MASK_CONTENT_GRAPHICS, &dst, &src_beyond);
    row("source beyond", 5, 2, 21);
    row("source beyond", 17, 2, 21);
    fill("empty source", mask, D2D1_OPACITY_MASK_CONTENT_GRAPHICS, &dst, &src_empty);
    row("empty source", 5, 2, 21);

    /* Inverted rectangles. */
    fill("inverted destination", mask, D2D1_OPACITY_MASK_CONTENT_GRAPHICS, &dst_inverted, NULL);
    row("inverted destination", 5, 2, 21);
    fill("inverted source", mask, D2D1_OPACITY_MASK_CONTENT_GRAPHICS, &dst, &src_inverted);
    row("inverted source", 5, 2, 21);

    /* The other kinds of content, and one that is not. */
    fill("gdi text", mask, D2D1_OPACITY_MASK_CONTENT_TEXT_GDI_COMPATIBLE, NULL, NULL);
    row("gdi text", 0, 0, 5);
    fill("bad content", mask, 3, NULL, NULL);

    /* The second method. */
    fill("second", mask, -1, &dst, &src);
    row("second", 5, 2, 21);
    fill("second, no rectangles", mask, -1, NULL, NULL);
    row("second, no rectangles", 1, 0, 5);

    /* A BGRA mask: only its alpha counts. */
    fill("bgra", mask_bgra, D2D1_OPACITY_MASK_CONTENT_GRAPHICS, NULL, NULL);
    row("bgra", 0, 0, 5);
    row("bgra", 3, 0, 5);

    /* A 192 DPI mask: 2 by 2 DIPs. */
    fill("192 dpi", mask_dpi, D2D1_OPACITY_MASK_CONTENT_GRAPHICS, NULL, NULL);
    row("192 dpi", 0, 0, 5);
    row("192 dpi", 1, 0, 5);
    row("192 dpi", 3, 0, 5);

    /* DrawBitmap() with the same source, upright and inverted. */
    ID2D1RenderTarget_BeginDraw(rt);
    ID2D1RenderTarget_Clear(rt, &clear);
    ID2D1RenderTarget_DrawBitmap(rt, mask_bgra, &dst, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &src);
    printf("drawbitmap: %#lx\n", ID2D1RenderTarget_EndDraw(rt, NULL, NULL));
    row("drawbitmap", 5, 2, 21);
    ID2D1RenderTarget_BeginDraw(rt);
    ID2D1RenderTarget_Clear(rt, &clear);
    ID2D1RenderTarget_DrawBitmap(rt, mask_bgra, &dst, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &src_inverted);
    printf("drawbitmap inverted source: %#lx\n", ID2D1RenderTarget_EndDraw(rt, NULL, NULL));
    row("drawbitmap inverted source", 5, 2, 21);

    /* Not aliased, through both methods. */
    ID2D1RenderTarget_SetAntialiasMode(rt, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    fill("antialiased", mask, D2D1_OPACITY_MASK_CONTENT_GRAPHICS, &dst, NULL);
    row("antialiased", 9, 2, 21);
    fill("second, antialiased", mask, -1, &dst, NULL);
    row("second, antialiased", 9, 2, 21);

    ID2D1Bitmap_Release(mask_dpi);
    ID2D1Bitmap_Release(mask_bgra);
    ID2D1Bitmap_Release(mask);
    ID2D1SolidColorBrush_Release(brush);
    ID2D1DeviceContext_Release(context);
    ID2D1RenderTarget_Release(rt);
    IWICBitmap_Release(bitmap);
    ID2D1Factory_Release(factory);
    IWICImagingFactory_Release(wic);
    printf("done\n");
    return 0;
}
