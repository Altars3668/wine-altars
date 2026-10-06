/*
 * dwmframe -- what the Windows 11 DWM draws around a pop-up window: the corners, the border and the
 * shadow, for each corner preference and border colour, layered or not, with and without
 * CS_DROPSHADOW.
 *
 * Shows a mid-grey backdrop and on it twelve white pop-ups, topmost and not activated, with their
 * DWM transitions disabled, waits for the DWM to settle, and prints for each its window facts and
 * a PNG of the screen around it (PNG-BEGIN <name> <w>x<h> ... PNG-END, base64).  Everything is gone
 * after about two seconds.  It refuses to run while someone is using the machine (input in the last
 * two minutes, or a locked desktop) unless given -force.
 *
 *   dwmframe [-force]
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <dwmapi.h>
#include <initguid.h>
#include <wincodec.h>
#include <wincrypt.h>

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#define DWMWA_BORDER_COLOR 34
#define DWMWA_VISIBLE_FRAME_BORDER_THICKNESS 37
#endif
#define CORNER_DEFAULT 0
#define CORNER_DONOTROUND 1
#define CORNER_ROUND 2
#define CORNER_ROUNDSMALL 3
#ifndef CAPTUREBLT
#define CAPTUREBLT 0x40000000
#endif
#define COLOR_UNSET 0xfffffffd   /* the probe's own: do not set DWMWA_BORDER_COLOR */
#define COLOR_NONE 0xfffffffe    /* DWMWA_COLOR_NONE */

static const struct variant
{
    const char *name;
    DWORD ex_style;
    BOOL dropshadow;
    DWORD corner;
    DWORD border;
} variants[] =
{
    { "round", 0, FALSE, CORNER_ROUND, COLOR_UNSET },
    { "roundsmall", 0, FALSE, CORNER_ROUNDSMALL, COLOR_UNSET },
    { "round-color-none", 0, FALSE, CORNER_ROUND, COLOR_NONE },
    { "round-red", 0, FALSE, CORNER_ROUND, 0x000000ff },
    { "default", 0, FALSE, CORNER_DEFAULT, COLOR_UNSET },
    { "donotround", 0, FALSE, CORNER_DONOTROUND, COLOR_UNSET },
    { "donotround-red", 0, FALSE, CORNER_DONOTROUND, 0x000000ff },
    { "layered-roundsmall-616161", WS_EX_LAYERED, FALSE, CORNER_ROUNDSMALL, 0x00616161 },
    { "dropshadow-default", 0, TRUE, CORNER_DEFAULT, COLOR_UNSET },
    { "dropshadow-round", 0, TRUE, CORNER_ROUND, COLOR_UNSET },
    { "toolwindow-round", WS_EX_TOOLWINDOW, FALSE, CORNER_ROUND, COLOR_UNSET },
    { "round-616161", 0, FALSE, CORNER_ROUND, 0x00616161 },
};

static UINT dpi = 96;
static int scale( int px ) { return MulDiv( px, dpi, 96 ); }

static LRESULT CALLBACK wndproc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW( hwnd, msg, wp, lp );
}

static void print_png( const char *name, const BYTE *bgrx, int width, int height )
{
    IWICImagingFactory *factory = NULL;
    IWICBitmapEncoder *encoder = NULL;
    IWICBitmapFrameEncode *frame = NULL;
    IStream *stream = NULL;
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    UINT stride = (width * 3 + 3) & ~3;
    BYTE *bgr = malloc( stride * height ), *png;
    STATSTG stat;
    HGLOBAL global;
    DWORD len = 0;
    char *text;
    HRESULT hr;
    int x, y;

    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            memcpy( bgr + y * stride + x * 3, bgrx + (y * width + x) * 4, 3 );
    hr = CoCreateInstance( &CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&factory );
    if (SUCCEEDED(hr)) hr = CreateStreamOnHGlobal( NULL, TRUE, &stream );
    if (SUCCEEDED(hr)) hr = IWICImagingFactory_CreateEncoder( factory, &GUID_ContainerFormatPng, NULL, &encoder );
    if (SUCCEEDED(hr)) hr = IWICBitmapEncoder_Initialize( encoder, stream, WICBitmapEncoderNoCache );
    if (SUCCEEDED(hr)) hr = IWICBitmapEncoder_CreateNewFrame( encoder, &frame, NULL );
    if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_Initialize( frame, NULL );
    if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_SetSize( frame, width, height );
    if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_SetPixelFormat( frame, &format );
    if (SUCCEEDED(hr) && !IsEqualGUID( &format, &GUID_WICPixelFormat24bppBGR )) hr = E_FAIL;
    if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_WritePixels( frame, height, stride, stride * height, bgr );
    if (SUCCEEDED(hr)) hr = IWICBitmapFrameEncode_Commit( frame );
    if (SUCCEEDED(hr)) hr = IWICBitmapEncoder_Commit( encoder );
    if (SUCCEEDED(hr)) hr = IStream_Stat( stream, &stat, STATFLAG_NONAME );
    if (SUCCEEDED(hr)) hr = GetHGlobalFromStream( stream, &global );
    if (SUCCEEDED(hr) && (png = GlobalLock( global )))
    {
        CryptBinaryToStringA( png, stat.cbSize.LowPart, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCR, NULL, &len );
        if ((text = malloc( len )) && CryptBinaryToStringA( png, stat.cbSize.LowPart, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCR, text, &len ))
        {
            printf( "PNG-BEGIN %s %dx%d\n", name, width, height );
            fwrite( text, 1, len, stdout );
            printf( "PNG-END\n" );
        }
        free( text );
        GlobalUnlock( global );
    }
    else printf( "PNG %s: encoding failed, hr %#lx\n", name, hr );
    fflush( stdout );
    if (frame) IWICBitmapFrameEncode_Release( frame );
    if (encoder) IWICBitmapEncoder_Release( encoder );
    if (stream) IStream_Release( stream );
    if (factory) IWICImagingFactory_Release( factory );
    free( bgr );
}

static void capture( const char *name, RECT rc )
{
    int width = rc.right - rc.left, height = rc.bottom - rc.top;
    BITMAPINFO info = { { sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB } };
    HDC screen = GetDC( NULL ), mem = CreateCompatibleDC( screen );
    void *bits;
    HBITMAP bitmap = CreateDIBSection( screen, &info, DIB_RGB_COLORS, &bits, NULL, 0 );
    HGDIOBJ old = SelectObject( mem, bitmap );

    BitBlt( mem, 0, 0, width, height, screen, rc.left, rc.top, SRCCOPY | CAPTUREBLT );
    GdiFlush();
    printf( "capture %s: screen (%ld,%ld)-(%ld,%ld)\n", name, rc.left, rc.top, rc.right, rc.bottom );
    print_png( name, bits, width, height );
    SelectObject( mem, old );
    DeleteObject( bitmap );
    DeleteDC( mem );
    ReleaseDC( NULL, screen );
}

static void print_setting( const WCHAR *subkey, const WCHAR *value )
{
    DWORD data = 0, len = sizeof(data);
    LSTATUS status = RegGetValueW( HKEY_CURRENT_USER, subkey, value, RRF_RT_REG_DWORD, NULL, &data, &len );

    if (status) printf( "setting %ls: absent (%ld)\n", value, status );
    else printf( "setting %ls: %#lx\n", value, data );
}

static void pump( DWORD ms )
{
    DWORD end = GetTickCount() + ms;
    MSG msg;
    while ((LONG)(end - GetTickCount()) > 0)
    {
        while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE )) DispatchMessageW( &msg );
        Sleep( 10 );
    }
}

int main( int argc, char **argv )
{
    WNDCLASSW cls = { 0 };
    HWND backdrop, windows[ARRAYSIZE(variants)];
    BOOL force = argc > 1 && !strcmp( argv[1], "-force" );
    RECT work, area;
    POINT origin;
    int i, cell_w, cell_h, win_w, win_h, margin, columns = 4;
    HDESK desk;

    SetProcessDpiAwarenessContext( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );
    CoInitialize( NULL );

    desk = OpenInputDesktop( 0, FALSE, DESKTOP_READOBJECTS );
    if (!desk) { printf( "the input desktop cannot be opened (locked or another session): not running\n" ); return 1; }
    CloseDesktop( desk );
    if (!force)
    {
        LASTINPUTINFO last = { sizeof(last) };
        if (GetLastInputInfo( &last ) && GetTickCount() - last.dwTime < 120000)
        {
            printf( "someone used the keyboard or mouse %lu s ago: not running\n", (GetTickCount() - last.dwTime) / 1000 );
            return 1;
        }
    }

    {
        /* GetVersionEx answers 6.2 to a program without a manifest */
        RTL_OSVERSIONINFOW version = { sizeof(version) };
        LONG (WINAPI *get_version)( RTL_OSVERSIONINFOW * ) =
            (void *)GetProcAddress( GetModuleHandleW( L"ntdll.dll" ), "RtlGetVersion" );
        if (get_version) get_version( &version );
        printf( "Windows %lu.%lu.%lu\n", version.dwMajorVersion, version.dwMinorVersion, version.dwBuildNumber );
    }
    print_setting( L"Software\\Microsoft\\Windows\\DWM", L"ColorPrevalence" );
    print_setting( L"Software\\Microsoft\\Windows\\DWM", L"AccentColor" );
    print_setting( L"Software\\Microsoft\\Windows\\DWM", L"ColorizationColor" );
    print_setting( L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme" );
    print_setting( L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"SystemUsesLightTheme" );
    print_setting( L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"EnableTransparency" );

    SystemParametersInfoW( SPI_GETWORKAREA, 0, &work, 0 );
    {
        POINT pt = { work.left + 50, work.top + 50 };
        HMONITOR monitor = MonitorFromPoint( pt, MONITOR_DEFAULTTOPRIMARY );
        UINT x, y;
        typedef HRESULT (WINAPI *get_dpi_func)( HMONITOR, int, UINT *, UINT * );
        get_dpi_func get_dpi = (get_dpi_func)GetProcAddress( LoadLibraryW( L"shcore.dll" ), "GetDpiForMonitor" );
        if (get_dpi && SUCCEEDED(get_dpi( monitor, 0, &x, &y ))) dpi = x;
    }
    printf( "work area (%ld,%ld)-(%ld,%ld), dpi %u\n", work.left, work.top, work.right, work.bottom, dpi );

    win_w = scale( 160 );
    win_h = scale( 110 );
    margin = scale( 32 );
    cell_w = win_w + 2 * margin;
    cell_h = win_h + 2 * margin;
    origin.x = work.left + scale( 60 );
    origin.y = work.top + scale( 60 );
    SetRect( &area, origin.x, origin.y, origin.x + columns * cell_w,
             origin.y + ((ARRAYSIZE(variants) + columns - 1) / columns) * cell_h );

    cls.lpfnWndProc = wndproc;
    cls.hCursor = LoadCursorW( NULL, (const WCHAR *)IDC_ARROW );
    cls.hbrBackground = CreateSolidBrush( RGB( 0x80, 0x80, 0x80 ) );
    cls.lpszClassName = L"DwmFrameBackdrop";
    RegisterClassW( &cls );
    cls.hbrBackground = GetStockObject( WHITE_BRUSH );
    cls.lpszClassName = L"DwmFramePopup";
    RegisterClassW( &cls );
    cls.style = CS_DROPSHADOW;
    cls.lpszClassName = L"DwmFramePopupShadow";
    RegisterClassW( &cls );

    backdrop = CreateWindowExW( WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"DwmFrameBackdrop", L"backdrop",
                                WS_POPUP, area.left, area.top, area.right - area.left, area.bottom - area.top,
                                NULL, NULL, NULL, NULL );
    {
        BOOL disable = TRUE;
        DWORD corner = CORNER_DONOTROUND;
        DwmSetWindowAttribute( backdrop, DWMWA_TRANSITIONS_FORCEDISABLED, &disable, sizeof(disable) );
        DwmSetWindowAttribute( backdrop, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner) );
    }
    ShowWindow( backdrop, SW_SHOWNOACTIVATE );

    for (i = 0; i < ARRAYSIZE(variants); i++)
    {
        const struct variant *v = &variants[i];
        int x = origin.x + (i % columns) * cell_w + margin, y = origin.y + (i / columns) * cell_h + margin;
        BOOL disable = TRUE;

        windows[i] = CreateWindowExW( WS_EX_TOPMOST | WS_EX_NOACTIVATE | v->ex_style,
                                      v->dropshadow ? L"DwmFramePopupShadow" : L"DwmFramePopup", L"popup",
                                      WS_POPUP | WS_CLIPSIBLINGS, x, y, win_w, win_h, backdrop, NULL, NULL, NULL );
        DwmSetWindowAttribute( windows[i], DWMWA_TRANSITIONS_FORCEDISABLED, &disable, sizeof(disable) );
        DwmSetWindowAttribute( windows[i], DWMWA_WINDOW_CORNER_PREFERENCE, &v->corner, sizeof(v->corner) );
        if (v->border != COLOR_UNSET)
            DwmSetWindowAttribute( windows[i], DWMWA_BORDER_COLOR, &v->border, sizeof(v->border) );
        if (v->ex_style & WS_EX_LAYERED) SetLayeredWindowAttributes( windows[i], 0, 255, LWA_ALPHA );
        ShowWindow( windows[i], SW_SHOWNOACTIVATE );
        UpdateWindow( windows[i] );
    }
    pump( 1200 );

    for (i = 0; i < ARRAYSIZE(variants); i++)
    {
        const struct variant *v = &variants[i];
        RECT rc, frame, cell;
        DWORD thickness = 0xdeadbeef;
        HRESULT hr;

        GetWindowRect( windows[i], &rc );
        hr = DwmGetWindowAttribute( windows[i], DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame) );
        printf( "%s: window (%ld,%ld)-(%ld,%ld) ex %#lx class %s corner %lu border ", v->name, rc.left, rc.top,
                rc.right, rc.bottom, v->ex_style, v->dropshadow ? "CS_DROPSHADOW" : "plain", v->corner );
        if (v->border == COLOR_UNSET) printf( "unset" );
        else if (v->border == COLOR_NONE) printf( "none" );
        else printf( "%#lx", v->border );
        printf( ", frame bounds hr %#lx (%ld,%ld)-(%ld,%ld)", hr, frame.left, frame.top, frame.right, frame.bottom );
        hr = DwmGetWindowAttribute( windows[i], DWMWA_VISIBLE_FRAME_BORDER_THICKNESS, &thickness, sizeof(thickness) );
        printf( ", visible border thickness hr %#lx %lu\n", hr, thickness );
        cell = rc;
        InflateRect( &cell, margin, margin );
        capture( v->name, cell );
    }

    for (i = 0; i < ARRAYSIZE(variants); i++) DestroyWindow( windows[i] );
    DestroyWindow( backdrop );
    CoUninitialize();
    return 0;
}
