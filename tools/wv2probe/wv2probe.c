/* wv2probe -- WebView2 环境创建到底返回什么。
 *
 * 点「登录或创建帐户」之后，Office 加载了 WebView2Loader.dll 和运行时的
 * EmbeddedBrowserWebView.dll，却从不拉起 msedgewebview2.exe，然后反复重试。
 * 这个探针直接调那两个导出，把 HRESULT 拿出来，省得从 trace 里猜。
 *
 * Build:
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -municode \
 *       -o wv2probe.exe wv2probe.c -lole32
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <objbase.h>
#include <stdio.h>

static void out( const char *s ) { DWORD w; WriteFile( GetStdHandle(STD_OUTPUT_HANDLE), s, (DWORD)strlen(s), &w, NULL ); }
static void outf( const char *f, ... ) { char b[1024]; va_list a; va_start(a,f); vsnprintf(b,sizeof(b),f,a); va_end(a); out(b); }

/* ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler */
DEFINE_GUID( IID_env_handler, 0x4e8a3389, 0xc9d8, 0x4bd2, 0xb6, 0xb5, 0x12, 0x4f, 0xee, 0x6c, 0xc1, 0x4d );

struct handler_vtbl;
struct handler { const struct handler_vtbl *lpVtbl; LONG ref; volatile LONG done; HRESULT hr; void *env; };
struct handler_vtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( struct handler *, REFIID, void ** );
    ULONG   (STDMETHODCALLTYPE *AddRef)( struct handler * );
    ULONG   (STDMETHODCALLTYPE *Release)( struct handler * );
    HRESULT (STDMETHODCALLTYPE *Invoke)( struct handler *, HRESULT, void * );
};

static HRESULT STDMETHODCALLTYPE h_qi( struct handler *t, REFIID iid, void **obj )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_env_handler ))
    { *obj = t; t->ref++; return S_OK; }
    *obj = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE h_addref( struct handler *t ) { return ++t->ref; }
static ULONG STDMETHODCALLTYPE h_release( struct handler *t ) { return --t->ref; }
static HRESULT STDMETHODCALLTYPE h_invoke( struct handler *t, HRESULT hr, void *env )
{
    t->hr = hr; t->env = env; t->done = 1;
    return S_OK;
}
static const struct handler_vtbl handler_vtbl = { h_qi, h_addref, h_release, h_invoke };

typedef HRESULT (STDMETHODCALLTYPE *fn_version)( PCWSTR, LPWSTR * );
typedef HRESULT (STDMETHODCALLTYPE *fn_create)( PCWSTR, PCWSTR, IUnknown *, struct handler * );

int wmain( int argc, WCHAR **argv )
{
    WCHAR data[MAX_PATH];
    struct handler h = { &handler_vtbl, 1, 0, E_PENDING, NULL };
    fn_version pVersion;
    fn_create pCreate;
    LPWSTR version = NULL;
    HMODULE mod;
    HRESULT hr;
    MSG msg;
    int i;

    CoInitializeEx( NULL, COINIT_APARTMENTTHREADED );

    if (!(mod = LoadLibraryW( L"WebView2Loader.dll" )))
    { outf( "LoadLibrary WebView2Loader.dll : %lu\n", GetLastError() ); return 1; }
    out( "WebView2Loader.dll             : loaded\n" );

    pVersion = (fn_version)(void *)GetProcAddress( mod, "GetAvailableCoreWebView2BrowserVersionString" );
    pCreate  = (fn_create)(void *)GetProcAddress( mod, "CreateCoreWebView2EnvironmentWithOptions" );
    if (!pVersion || !pCreate) { out( "missing exports\n" ); return 1; }

    hr = pVersion( NULL, &version );
    outf( "GetAvailableBrowserVersion     : 0x%08lx", (unsigned long)hr );
    if (SUCCEEDED(hr) && version)
    {
        char buf[128];
        WideCharToMultiByte( CP_UTF8, 0, version, -1, buf, sizeof(buf), NULL, NULL );
        outf( "  version=%s", buf );
        CoTaskMemFree( version );
    }
    out( "\n" );

    if (argc > 1) wcscpy( data, argv[1] );
    else
    {
        GetTempPathW( ARRAYSIZE(data), data );
        wcscat( data, L"wv2probe-data" );
    }
    CreateDirectoryW( data, NULL );
    { char b[512]; WideCharToMultiByte( CP_UTF8, 0, data, -1, b, sizeof(b), NULL, NULL );
      outf( "user data folder               : %s\n", b ); }

    hr = pCreate( NULL, data, NULL, &h );
    outf( "CreateEnvironmentWithOptions   : 0x%08lx (同步返回)\n", (unsigned long)hr );
    if (FAILED(hr)) return 1;

    for (i = 0; i < 600 && !h.done; i++)
    {
        while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE )) { TranslateMessage( &msg ); DispatchMessageW( &msg ); }
        Sleep( 100 );
    }
    if (h.done) outf( "handler Invoke                 : hr=0x%08lx env=%p\n", (unsigned long)h.hr, h.env );
    else        out( "handler Invoke                 : 60 秒内未回调（挂住）\n" );

    out( "done\n" );
    return 0;
}
