/*
 * d2dflushprobe - what ID2D1RenderTarget::Flush does on Windows.
 *
 * Office calls Flush on its Direct2D device contexts dozens of times a second
 * while it zooms, and Wine's was a stub that presented and failed.  This asks
 * what it returns inside and outside BeginDraw, whether it reports and clears
 * an error the way EndDraw does, with which tags, and whether a DC render
 * target's pixels reach the DC before EndDraw.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <d2d1.h>
#include <stdio.h>

static DWORD *bits;

static void flush(ID2D1RenderTarget *rt, const char *what)
{
    D2D1_TAG tag1 = 0xdead, tag2 = 0xbeef;
    HRESULT hr = ID2D1RenderTarget_Flush(rt, &tag1, &tag2);
    printf("  %-44s Flush %#lx tags %I64u %I64u, pixel %06lx\n", what, hr, tag1, tag2, bits[0] & 0xffffff);
}

static void end(ID2D1RenderTarget *rt, const char *what)
{
    D2D1_TAG tag1 = 0xdead, tag2 = 0xbeef;
    HRESULT hr = ID2D1RenderTarget_EndDraw(rt, &tag1, &tag2);
    printf("  %-44s EndDraw %#lx tags %I64u %I64u, pixel %06lx\n", what, hr, tag1, tag2, bits[0] & 0xffffff);
}

int main(void)
{
    D2D1_RENDER_TARGET_PROPERTIES props = {D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                           {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE}, 0.0f, 0.0f,
                                           D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT};
    BITMAPINFO bmi = {{sizeof(BITMAPINFOHEADER), 8, -8, 1, 32, BI_RGB}};
    D2D1_COLOR_F red = {1, 0, 0, 1}, green = {0, 1, 0, 1};
    RECT rect = {0, 0, 8, 8};
    ID2D1DCRenderTarget *dc_rt;
    ID2D1RenderTarget *rt;
    ID2D1Factory *factory;
    HBITMAP dib;
    HRESULT hr;
    HDC hdc;

    setvbuf(stdout, NULL, _IONBF, 0);
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory, NULL, (void **)&factory);
    printf("D2D1CreateFactory %#lx\n", hr);
    if (FAILED(hr)) return 1;

    hdc = CreateCompatibleDC(NULL);
    dib = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
    SelectObject(hdc, dib);
    memset(bits, 0, 8 * 8 * 4);

    for (int software = 0; software < 2; software++)
    {
        props.type = software ? D2D1_RENDER_TARGET_TYPE_SOFTWARE : D2D1_RENDER_TARGET_TYPE_DEFAULT;
        hr = ID2D1Factory_CreateDCRenderTarget(factory, &props, &dc_rt);
        printf("%s DC render target %#lx\n", software ? "software" : "default", hr);
        if (FAILED(hr)) continue;
        ID2D1DCRenderTarget_QueryInterface(dc_rt, &IID_ID2D1RenderTarget, (void **)&rt);
        hr = ID2D1DCRenderTarget_BindDC(dc_rt, hdc, &rect);
        printf("  BindDC %#lx\n", hr);

        memset(bits, 0, 8 * 8 * 4);
        flush(rt, "outside BeginDraw");
        ID2D1RenderTarget_BeginDraw(rt);
        ID2D1RenderTarget_SetTags(rt, 1, 2);
        ID2D1RenderTarget_Clear(rt, &red);
        flush(rt, "after a clear");
        ID2D1RenderTarget_Clear(rt, &green);
        end(rt, "after another clear");

        memset(bits, 0, 8 * 8 * 4);
        ID2D1RenderTarget_BeginDraw(rt);
        ID2D1RenderTarget_SetTags(rt, 3, 4);
        ID2D1RenderTarget_PopAxisAlignedClip(rt);
        ID2D1RenderTarget_SetTags(rt, 5, 6);
        flush(rt, "after a pop with nothing pushed");
        flush(rt, "and again");
        ID2D1RenderTarget_Clear(rt, &red);
        end(rt, "then a clear");

        ID2D1RenderTarget_BeginDraw(rt);
        ID2D1RenderTarget_SetTags(rt, 7, 8);
        ID2D1RenderTarget_PopAxisAlignedClip(rt);
        end(rt, "a pop with nothing pushed, no flush");
        flush(rt, "after that EndDraw");

        ID2D1RenderTarget_BeginDraw(rt);
        ID2D1RenderTarget_BeginDraw(rt);
        flush(rt, "inside BeginDraw twice");
        end(rt, "EndDraw once");
        end(rt, "EndDraw twice");

        /* outside drawing, the tags handed back: the last error's, or the ones set now */
        ID2D1RenderTarget_SetTags(rt, 9, 10);
        flush(rt, "tags set to 9 10 outside drawing");
        end(rt, "tags set to 9 10 outside drawing");
        ID2D1RenderTarget_BeginDraw(rt);
        end(rt, "a clean BeginDraw and EndDraw");
        flush(rt, "after it, outside drawing");
        /* what drawing outside BeginDraw does */
        ID2D1RenderTarget_SetTags(rt, 11, 12);
        ID2D1RenderTarget_Clear(rt, &green);
        ID2D1RenderTarget_SetTags(rt, 13, 14);
        ID2D1RenderTarget_BeginDraw(rt);
        end(rt, "after a clear outside drawing");
        /* whether an error blocks what comes after it, and a second error's tags */
        ID2D1RenderTarget_BeginDraw(rt);
        ID2D1RenderTarget_SetTags(rt, 15, 16);
        ID2D1RenderTarget_PopAxisAlignedClip(rt);
        ID2D1RenderTarget_SetTags(rt, 17, 18);
        ID2D1RenderTarget_PopAxisAlignedClip(rt);
        ID2D1RenderTarget_Clear(rt, &green);
        end(rt, "two pops then a clear");
        /* a push and a pop match */
        ID2D1RenderTarget_BeginDraw(rt);
        {
            D2D1_RECT_F clip = {0, 0, 4, 4};
            ID2D1RenderTarget_PushAxisAlignedClip(rt, &clip, D2D1_ANTIALIAS_MODE_ALIASED);
            ID2D1RenderTarget_PopAxisAlignedClip(rt);
            end(rt, "a push and a pop");
            ID2D1RenderTarget_BeginDraw(rt);
            ID2D1RenderTarget_PushAxisAlignedClip(rt, &clip, D2D1_ANTIALIAS_MODE_ALIASED);
            end(rt, "a push left pushed");
            /* a pop of the other kind */
            ID2D1RenderTarget_BeginDraw(rt);
            ID2D1RenderTarget_PushAxisAlignedClip(rt, &clip, D2D1_ANTIALIAS_MODE_ALIASED);
            ID2D1RenderTarget_PopLayer(rt);
            end(rt, "a clip pushed, a layer popped");
            ID2D1RenderTarget_BeginDraw(rt);
            ID2D1RenderTarget_PopLayer(rt);
            end(rt, "a layer popped with nothing pushed");
            ID2D1RenderTarget_BeginDraw(rt);
            ID2D1RenderTarget_PushAxisAlignedClip(rt, &clip, D2D1_ANTIALIAS_MODE_ALIASED);
            ID2D1RenderTarget_PopLayer(rt);
            ID2D1RenderTarget_PopAxisAlignedClip(rt);
            end(rt, "and the clip popped after it");
        }

        ID2D1RenderTarget_Release(rt);
        ID2D1DCRenderTarget_Release(dc_rt);
    }
    printf("done\n");
    return 0;
}
