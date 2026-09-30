/* wicscaleprobe - what each interpolation mode of WIC's bitmap scaler computes.
 *
 * PowerPoint scales pictures through IWICBitmapScaler with WICBitmapInterpolationModeFant, and Wine's
 * scaler only knows nearest neighbour.  This scales small patterns in several pixel formats up and down
 * with every mode, and prints the result's pixel format, CopyPixels' result and the pixels, whole and
 * for a rectangle that starts inside, so Wine's scaler can follow Windows: where pixel centres are, how
 * edges and rounding are treated, and which formats are interpolated at all.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <wincodec.h>
#include <stdio.h>

static IWICImagingFactory *factory;

static const char *mode_names[] = {"NearestNeighbor", "Linear", "Cubic", "Fant", "HighQualityCubic"};

struct format
{
    const GUID *guid;
    const char *name;
    UINT bpp;
};

static void print_pixels(const struct format *format, const BYTE *data, UINT width, UINT height, UINT stride)
{
    UINT x, y, i, bytes = format->bpp / 8;

    for (y = 0; y < height; y++)
    {
        printf("      ");
        for (x = 0; x < width; x++)
        {
            const BYTE *p = data + y * stride + x * bytes;

            if (IsEqualGUID(format->guid, &GUID_WICPixelFormat128bppRGBAFloat))
                printf(" (%.4g %.4g %.4g %.4g)", ((float *)p)[0], ((float *)p)[1], ((float *)p)[2], ((float *)p)[3]);
            else if (IsEqualGUID(format->guid, &GUID_WICPixelFormat64bppRGBA))
                printf(" (%u %u %u %u)", ((USHORT *)p)[0], ((USHORT *)p)[1], ((USHORT *)p)[2], ((USHORT *)p)[3]);
            else if (bytes == 1)
                printf(" %3u", p[0]);
            else
            {
                printf(" (");
                for (i = 0; i < bytes; i++) printf(i ? " %u" : "%u", p[i]);
                printf(")");
            }
        }
        printf("\n");
    }
}

static void scale(const struct format *format, const char *pattern, UINT width, UINT height,
                  const BYTE *pixels, UINT dst_width, UINT dst_height, IWICPalette *palette)
{
    UINT stride = (width * format->bpp + 7) / 8, dst_stride = (dst_width * format->bpp + 7) / 8;
    BYTE buffer[4096];
    unsigned int mode;

    printf("%s %s %ux%u -> %ux%u\n", format->name, pattern, width, height, dst_width, dst_height);
    for (mode = 0; mode < ARRAYSIZE(mode_names); mode++)
    {
        IWICBitmapScaler *scaler;
        IWICBitmap *bitmap;
        WICPixelFormatGUID out;
        WICRect rect;
        HRESULT hr;

        hr = IWICImagingFactory_CreateBitmapFromMemory(factory, width, height, format->guid, stride,
                stride * height, (BYTE *)pixels, &bitmap);
        if (FAILED(hr))
        {
            printf("  CreateBitmapFromMemory %#lx\n", hr);
            return;
        }
        if (palette) IWICBitmap_SetPalette(bitmap, palette);
        IWICImagingFactory_CreateBitmapScaler(factory, &scaler);
        hr = IWICBitmapScaler_Initialize(scaler, (IWICBitmapSource *)bitmap, dst_width, dst_height, mode);
        printf("  %s: Initialize %#lx", mode_names[mode], hr);
        if (SUCCEEDED(hr))
        {
            IWICBitmapScaler_GetPixelFormat(scaler, &out);
            printf(", format %s", IsEqualGUID(&out, format->guid) ? "same" : "DIFFERENT");
            memset(buffer, 0xcd, sizeof(buffer));
            hr = IWICBitmapScaler_CopyPixels(scaler, NULL, dst_stride, sizeof(buffer), buffer);
            printf(", CopyPixels %#lx\n", hr);
            if (SUCCEEDED(hr)) print_pixels(format, buffer, dst_width, dst_height, dst_stride);
            if (dst_width > 2)
            {
                /* a rectangle that starts inside: does a pixel depend on where the copy starts? */
                rect.X = 1;
                rect.Y = dst_height - 1;
                rect.Width = dst_width - 1;
                rect.Height = 1;
                memset(buffer, 0xcd, sizeof(buffer));
                hr = IWICBitmapScaler_CopyPixels(scaler, &rect, dst_stride, sizeof(buffer), buffer);
                printf("    last row from x=1: %#lx\n", hr);
                if (SUCCEEDED(hr)) print_pixels(format, buffer, dst_width - 1, 1, dst_stride);
            }
        }
        else printf("\n");
        IWICBitmapScaler_Release(scaler);
        IWICBitmap_Release(bitmap);
    }
}

int main(void)
{
    static const struct format gray8 = {&GUID_WICPixelFormat8bppGray, "Gray8", 8};
    static const struct format bgr24 = {&GUID_WICPixelFormat24bppBGR, "BGR24", 24};
    static const struct format bgra32 = {&GUID_WICPixelFormat32bppBGRA, "BGRA32", 32};
    static const struct format pbgra32 = {&GUID_WICPixelFormat32bppPBGRA, "PBGRA32", 32};
    static const struct format rgba64 = {&GUID_WICPixelFormat64bppRGBA, "RGBA64", 64};
    static const struct format float128 = {&GUID_WICPixelFormat128bppRGBAFloat, "RGBAFloat128", 128};
    static const struct format index8 = {&GUID_WICPixelFormat8bppIndexed, "Indexed8", 8};
    static const BYTE step[] = {0, 255};
    static const BYTE ramp8[] = {0, 64, 128, 192, 255, 0, 100, 200};
    static const BYTE ramp16[] = {0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240};
    static const BYTE checker[] =
    {
        0, 255, 0, 255,
        255, 0, 255, 0,
        0, 255, 0, 255,
        255, 0, 255, 0,
    };
    static const BYTE bgr_step[] = {255, 0, 0, 0, 0, 255};
    static const BYTE bgra_alpha[] = {0, 0, 255, 255, 0, 0, 0, 0};    /* opaque red, then transparent black */
    static const BYTE bgra_alpha2[] = {0, 0, 255, 255, 255, 0, 0, 0}; /* opaque red, then transparent blue */
    static const BYTE pbgra_alpha[] = {0, 0, 255, 255, 0, 0, 0, 0};
    static const USHORT rgba64_step[] = {0, 0, 0, 65535, 65535, 65535, 65535, 65535};
    static const float float_step[] = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    static const BYTE index_step[] = {0, 1};
    WICColor colors[2] = {0xff000000, 0xffffffff};
    IWICPalette *palette;
    HRESULT hr;

    CoInitialize(NULL);
    hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory,
                          (void **)&factory);
    if (FAILED(hr))
    {
        printf("CoCreateInstance(CLSID_WICImagingFactory) %#lx\n", hr);
        return 1;
    }

    scale(&gray8, "step", 2, 1, step, 3, 1, NULL);
    scale(&gray8, "step", 2, 1, step, 4, 1, NULL);
    scale(&gray8, "step", 2, 1, step, 8, 1, NULL);
    scale(&gray8, "ramp", 8, 1, ramp8, 4, 1, NULL);
    scale(&gray8, "ramp", 8, 1, ramp8, 3, 1, NULL);
    scale(&gray8, "ramp", 8, 1, ramp8, 5, 1, NULL);
    scale(&gray8, "ramp", 8, 1, ramp8, 1, 1, NULL);
    scale(&gray8, "ramp", 8, 1, ramp8, 12, 1, NULL);
    scale(&gray8, "ramp16", 16, 1, ramp16, 3, 1, NULL);
    scale(&gray8, "ramp16", 16, 1, ramp16, 5, 1, NULL);
    scale(&gray8, "checker", 4, 4, checker, 2, 2, NULL);
    scale(&gray8, "checker", 4, 4, checker, 3, 3, NULL);
    scale(&gray8, "checker", 4, 4, checker, 6, 6, NULL);
    scale(&gray8, "checker", 4, 4, checker, 2, 5, NULL);
    scale(&bgr24, "step", 2, 1, bgr_step, 4, 1, NULL);
    scale(&bgra32, "alpha", 2, 1, bgra_alpha, 4, 1, NULL);
    scale(&bgra32, "alpha2", 2, 1, bgra_alpha2, 4, 1, NULL);
    scale(&pbgra32, "alpha", 2, 1, pbgra_alpha, 4, 1, NULL);
    scale(&rgba64, "step", 2, 1, (const BYTE *)rgba64_step, 4, 1, NULL);
    scale(&float128, "step", 2, 1, (const BYTE *)float_step, 4, 1, NULL);

    IWICImagingFactory_CreatePalette(factory, &palette);
    IWICPalette_InitializeCustom(palette, colors, 2);
    scale(&index8, "step", 2, 1, index_step, 4, 1, palette);
    IWICPalette_Release(palette);

    IWICImagingFactory_Release(factory);
    CoUninitialize();
    return 0;
}
