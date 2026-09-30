/* What WIC's bitmap scaler produces in each interpolation mode.
 *
 * Wine's scaler only knows nearest neighbour and uses it for every mode; Excel asks for cubic at
 * every start.  For a few small source bitmaps (non-premultiplied and premultiplied BGRA, BGR, 8-bit
 * gray, an 8-bit palette image) this scales to a smaller, the same, a larger and a twice as large
 * size in each of the five modes and prints: Initialize's result, the pixel format the scaler gives
 * out, and every output pixel in hex.  A copy of an inner rectangle is compared with the same pixels
 * of the full copy.  The kernels, the pixel centres, the rounding and the handling of alpha can all
 * be read off this.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror wicscalerprobe.c -o wicscalerprobe.exe -lole32 -lwindowscodecs -luuid
 */
#define COBJMACROS
#include <windows.h>
#include <wincodec.h>
#include <stdio.h>
#include <string.h>

static IWICImagingFactory *factory;

static const struct { const char *name; const GUID *format; UINT bpp; } formats[] =
{
    {"BGRA", &GUID_WICPixelFormat32bppBGRA, 32},
    {"PBGRA", &GUID_WICPixelFormat32bppPBGRA, 32},
    {"BGR", &GUID_WICPixelFormat24bppBGR, 24},
    {"Gray8", &GUID_WICPixelFormat8bppGray, 8},
    {"Indexed8", &GUID_WICPixelFormat8bppIndexed, 8},
};

static const char *format_name(const GUID *g)
{
    static const struct { const GUID *g; const char *name; } names[] =
    {
        {&GUID_WICPixelFormat32bppBGRA, "BGRA"}, {&GUID_WICPixelFormat32bppPBGRA, "PBGRA"},
        {&GUID_WICPixelFormat24bppBGR, "BGR"}, {&GUID_WICPixelFormat8bppGray, "Gray8"},
        {&GUID_WICPixelFormat8bppIndexed, "Indexed8"}, {&GUID_WICPixelFormat32bppBGR, "BGR32"},
        {&GUID_WICPixelFormat64bppRGBA, "RGBA64"}, {&GUID_WICPixelFormat64bppPRGBA, "PRGBA64"},
        {&GUID_WICPixelFormat128bppRGBAFloat, "RGBA128F"}, {&GUID_WICPixelFormat128bppPRGBAFloat, "PRGBA128F"},
        {&GUID_WICPixelFormat32bppGrayFloat, "Gray32F"}, {&GUID_WICPixelFormat24bppRGB, "RGB"},
        {&GUID_WICPixelFormat32bppRGBA, "RGBA"}, {&GUID_WICPixelFormat32bppPRGBA, "PRGBA"},
    };
    static char buf[64];
    unsigned int i;

    for (i = 0; i < ARRAYSIZE(names); i++) if (IsEqualGUID(g, names[i].g)) return names[i].name;
    sprintf(buf, "{%08lx-...}", g->Data1);
    return buf;
}

/* 7x5 pixels: a colour ramp with an alpha ramp and one hard edge */
static void make_pixels(unsigned int fmt, BYTE *bits, UINT stride)
{
    UINT x, y;

    for (y = 0; y < 5; y++)
        for (x = 0; x < 7; x++)
        {
            BYTE b = x * 40, g = y * 60, r = (x >= 4) ? 250 : 10, a = (BYTE)(255 - x * 30 - y * 10);
            BYTE *p = bits + y * stride;

            switch (fmt)
            {
            case 0: p[x * 4] = b; p[x * 4 + 1] = g; p[x * 4 + 2] = r; p[x * 4 + 3] = a; break;
            case 1: p[x * 4] = b * a / 255; p[x * 4 + 1] = g * a / 255; p[x * 4 + 2] = r * a / 255; p[x * 4 + 3] = a; break;
            case 2: p[x * 3] = b; p[x * 3 + 1] = g; p[x * 3 + 2] = r; break;
            case 3: p[x] = (BYTE)(b / 3 + g / 3 + r / 3); break;
            case 4: p[x] = (BYTE)((x + y) % 4); break;
            }
        }
}

static IWICBitmap *make_bitmap(unsigned int fmt)
{
    UINT stride = (7 * formats[fmt].bpp + 7) / 8;
    BYTE bits[5 * 7 * 4];
    IWICBitmap *bitmap;
    HRESULT hr;

    make_pixels(fmt, bits, stride);
    hr = IWICImagingFactory_CreateBitmapFromMemory(factory, 7, 5, formats[fmt].format, stride, stride * 5, bits, &bitmap);
    if (FAILED(hr)) return NULL;
    if (fmt == 4)
    {
        WICColor colors[4] = {0xff000000, 0xffff0000, 0x8000ff00, 0x000000ff};
        IWICPalette *palette;

        IWICImagingFactory_CreatePalette(factory, &palette);
        IWICPalette_InitializeCustom(palette, colors, 4);
        IWICBitmap_SetPalette(bitmap, palette);
        IWICPalette_Release(palette);
    }
    return bitmap;
}

static void scale(unsigned int fmt, UINT width, UINT height, WICBitmapInterpolationMode mode)
{
    IWICBitmapScaler *scaler;
    IWICBitmap *bitmap = make_bitmap(fmt);
    WICPixelFormatGUID out;
    BYTE full[16 * 16 * 16], part[16 * 16];
    UINT bpp = 0, stride, w = 0, h = 0, x, y, i;
    IWICComponentInfo *info;
    IWICPixelFormatInfo *pfinfo;
    WICRect rect;
    HRESULT hr;

    printf("%s %ux%u mode %u:", formats[fmt].name, width, height, mode);
    if (!bitmap)
    {
        printf(" no bitmap\n");
        return;
    }
    IWICImagingFactory_CreateBitmapScaler(factory, &scaler);
    hr = IWICBitmapScaler_Initialize(scaler, (IWICBitmapSource *)bitmap, width, height, mode);
    printf(" init %#lx", hr);
    if (SUCCEEDED(hr))
    {
        IWICBitmapScaler_GetPixelFormat(scaler, &out);
        IWICBitmapScaler_GetSize(scaler, &w, &h);
        if (SUCCEEDED(IWICImagingFactory_CreateComponentInfo(factory, &out, &info)))
        {
            if (SUCCEEDED(IWICComponentInfo_QueryInterface(info, &IID_IWICPixelFormatInfo, (void **)&pfinfo)))
            {
                IWICPixelFormatInfo_GetBitsPerPixel(pfinfo, &bpp);
                IWICPixelFormatInfo_Release(pfinfo);
            }
            IWICComponentInfo_Release(info);
        }
        stride = (w * bpp + 7) / 8;
        hr = IWICBitmapScaler_CopyPixels(scaler, NULL, stride, sizeof(full), full);
        printf(", out %s %ux%u, copy %#lx\n", format_name(&out), w, h, hr);
        if (SUCCEEDED(hr))
        {
            for (y = 0; y < h; y++)
            {
                printf("   ");
                for (x = 0; x < w; x++)
                {
                    printf(" ");
                    for (i = 0; i < bpp / 8; i++) printf("%02x", full[y * stride + x * bpp / 8 + i]);
                }
                printf("\n");
            }
            /* one row of an inner rectangle against the full copy */
            if (w > 2 && h > 2)
            {
                rect.X = 1; rect.Y = 1; rect.Width = w - 2; rect.Height = 1;
                hr = IWICBitmapScaler_CopyPixels(scaler, &rect, sizeof(part), sizeof(part), part);
                printf("    inner row: %#lx, %s\n", hr, SUCCEEDED(hr) && !memcmp(part, full + stride + bpp / 8, (w - 2) * bpp / 8)
                       ? "same" : "differs");
            }
        }
    }
    else printf("\n");
    IWICBitmapScaler_Release(scaler);
    IWICBitmap_Release(bitmap);
}

int main(void)
{
    static const UINT sizes[][2] = {{3, 2}, {7, 5}, {11, 9}, {14, 10}};
    unsigned int f, s, m;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&factory)))
    {
        printf("no WIC\n");
        return 1;
    }
    for (f = 0; f < ARRAYSIZE(formats); f++)
        for (s = 0; s < ARRAYSIZE(sizes); s++)
            for (m = 0; m <= 4; m++) /* nearest, linear, cubic, Fant, high quality cubic */
            {
                /* the gray, BGR and palette images only at the sizes that tell the most */
                if (f >= 2 && (s == 1 || s == 3)) continue;
                scale(f, sizes[s][0], sizes[s][1], m);
            }
    IWICImagingFactory_Release(factory);
    CoUninitialize();
    return 0;
}
