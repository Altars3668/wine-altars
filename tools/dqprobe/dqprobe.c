/* dqprobe -- Windows.System.DispatcherQueue 在 Wine 下是否真能用。
 *
 * Office 的 React Native host 在激活这个类失败时会抛 winrt::hresult_class_not_registered
 * 并 terminate，所以这里按它的用法走一遍：拿激活工厂、为当前线程建队列、
 * GetForCurrentThread 取回、TryEnqueue 一个回调、泵消息确认回调真的跑了。
 *
 * Build:
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -municode \
 *       -o dqprobe.exe dqprobe.c -lole32 -lruntimeobject -luuid
 */
#define COBJMACROS
#include <initguid.h>
#include <windows.h>
#include <objbase.h>
#include <roapi.h>
#include <winstring.h>
#include <stdio.h>

static void out( const char *s ) { DWORD w; WriteFile( GetStdHandle(STD_OUTPUT_HANDLE), s, (DWORD)strlen(s), &w, NULL ); }
static void outf( const char *f, ... ) { char b[512]; va_list a; va_start(a,f); vsnprintf(b,sizeof(b),f,a); va_end(a); out(b); }

DEFINE_GUID( IID_IDispatcherQueueStatics,  0xa96d83d7,0x9371,0x4517,0x92,0x45,0xd0,0x82,0x4a,0xc1,0x2c,0x74 );
DEFINE_GUID( IID_IDispatcherQueue,         0x603e88e4,0xa338,0x4ffe,0xa4,0x57,0xa5,0xcf,0xb9,0xce,0xb8,0x99 );
DEFINE_GUID( IID_IDispatcherQueue2,        0xc822c647,0x30ef,0x506e,0xbd,0x1e,0xa6,0x47,0xae,0x66,0x75,0xff );
DEFINE_GUID( IID_IDispatcherQueueHandler,  0xdfa2dc9c,0x1a2d,0x4917,0x98,0xf2,0x93,0x9a,0xf1,0xd6,0xe0,0xc8 );

/* 只用到 vtable 前几项，按 IDL 顺序手写 */
typedef struct IDQ IDQ;
struct IDQVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( IDQ *, REFIID, void ** );
    ULONG   (STDMETHODCALLTYPE *AddRef)( IDQ * );
    ULONG   (STDMETHODCALLTYPE *Release)( IDQ * );
    HRESULT (STDMETHODCALLTYPE *GetIids)( IDQ *, ULONG *, IID ** );
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)( IDQ *, HSTRING * );
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)( IDQ *, int * );
    HRESULT (STDMETHODCALLTYPE *CreateTimer)( IDQ *, void ** );
    HRESULT (STDMETHODCALLTYPE *TryEnqueue)( IDQ *, void *, boolean * );
    HRESULT (STDMETHODCALLTYPE *TryEnqueueWithPriority)( IDQ *, int, void *, boolean * );
};
struct IDQ { const struct IDQVtbl *lpVtbl; };

typedef struct IDQS IDQS;
struct IDQSVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( IDQS *, REFIID, void ** );
    ULONG   (STDMETHODCALLTYPE *AddRef)( IDQS * );
    ULONG   (STDMETHODCALLTYPE *Release)( IDQS * );
    HRESULT (STDMETHODCALLTYPE *GetIids)( IDQS *, ULONG *, IID ** );
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)( IDQS *, HSTRING * );
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)( IDQS *, int * );
    HRESULT (STDMETHODCALLTYPE *GetForCurrentThread)( IDQS *, IDQ ** );
};
struct IDQS { const struct IDQSVtbl *lpVtbl; };

typedef struct IDQ2 IDQ2;
struct IDQ2Vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( IDQ2 *, REFIID, void ** );
    ULONG   (STDMETHODCALLTYPE *AddRef)( IDQ2 * );
    ULONG   (STDMETHODCALLTYPE *Release)( IDQ2 * );
    HRESULT (STDMETHODCALLTYPE *GetIids)( IDQ2 *, ULONG *, IID ** );
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)( IDQ2 *, HSTRING * );
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)( IDQ2 *, int * );
    HRESULT (STDMETHODCALLTYPE *get_HasThreadAccess)( IDQ2 *, boolean * );
};
struct IDQ2 { const struct IDQ2Vtbl *lpVtbl; };

/* 我们自己的 handler 委托 */
static int handler_ran;
struct handler { const struct handler_vtbl *lpVtbl; LONG ref; };
struct handler_vtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)( struct handler *, REFIID, void ** );
    ULONG   (STDMETHODCALLTYPE *AddRef)( struct handler * );
    ULONG   (STDMETHODCALLTYPE *Release)( struct handler * );
    HRESULT (STDMETHODCALLTYPE *Invoke)( struct handler * );
};
static HRESULT STDMETHODCALLTYPE h_qi( struct handler *t, REFIID iid, void **o )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IDispatcherQueueHandler ))
    { *o = t; t->ref++; return S_OK; }
    *o = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE h_addref( struct handler *t ) { return ++t->ref; }
static ULONG STDMETHODCALLTYPE h_release( struct handler *t ) { return --t->ref; }
static HRESULT STDMETHODCALLTYPE h_invoke( struct handler *t ) { (void)t; handler_ran = 1; return S_OK; }
static const struct handler_vtbl handler_vtbl = { h_qi, h_addref, h_release, h_invoke };

struct dq_options { DWORD dwSize; int threadType; int apartmentType; };
typedef HRESULT (WINAPI *fn_create_controller)( struct dq_options, void ** );

int wmain( void )
{
    HSTRING_HEADER hdr;
    HSTRING cls;
    IDQS *statics = NULL;
    IDQ *queue = NULL, *queue2 = NULL;
    IDQ2 *dq2 = NULL;
    struct handler h = { &handler_vtbl, 1 };
    HRESULT hr;
    boolean b = 0;
    HMODULE cm;
    fn_create_controller create;
    struct dq_options opts = { sizeof(opts), 2 /* DQTYPE_THREAD_CURRENT */, 2 /* DQTAT_COM_STA */ };
    void *controller = NULL;
    MSG msg;
    int i;

    RoInitialize( RO_INIT_SINGLETHREADED );

    WindowsCreateStringReference( L"Windows.System.DispatcherQueue", 30, &hdr, &cls );
    hr = RoGetActivationFactory( cls, &IID_IDispatcherQueueStatics, (void **)&statics );
    outf( "RoGetActivationFactory(DispatcherQueue) : 0x%08lx %s\n", (unsigned long)hr, SUCCEEDED(hr) ? "" : "<-- 失败" );
    if (FAILED(hr)) return 1;

    hr = statics->lpVtbl->GetForCurrentThread( statics, &queue );
    outf( "GetForCurrentThread (建队列之前)       : 0x%08lx queue=%p（应为 NULL）\n", (unsigned long)hr, queue );

    if (!(cm = LoadLibraryW( L"coremessaging.dll" ))) { out( "加载 coremessaging 失败\n" ); return 1; }
    create = (fn_create_controller)(void *)GetProcAddress( cm, "CreateDispatcherQueueController" );
    if (!create) { out( "没有 CreateDispatcherQueueController\n" ); return 1; }
    hr = create( opts, &controller );
    outf( "CreateDispatcherQueueController         : 0x%08lx controller=%p\n", (unsigned long)hr, controller );

    hr = statics->lpVtbl->GetForCurrentThread( statics, &queue2 );
    outf( "GetForCurrentThread (建队列之后)       : 0x%08lx queue=%p %s\n", (unsigned long)hr, queue2,
          queue2 ? "" : "<-- 应当非空" );
    if (!queue2) return 1;

    hr = queue2->lpVtbl->QueryInterface( queue2, &IID_IDispatcherQueue2, (void **)&dq2 );
    if (SUCCEEDED(hr))
    {
        dq2->lpVtbl->get_HasThreadAccess( dq2, &b );
        outf( "IDispatcherQueue2::HasThreadAccess      : %s\n", b ? "TRUE（本线程，正确）" : "FALSE <-- 错" );
        dq2->lpVtbl->Release( dq2 );
    }
    else outf( "QI IDispatcherQueue2                    : 0x%08lx\n", (unsigned long)hr );

    hr = queue2->lpVtbl->TryEnqueue( queue2, &h, &b );
    outf( "TryEnqueue                              : 0x%08lx result=%d\n", (unsigned long)hr, b );

    for (i = 0; i < 50 && !handler_ran; i++)
    {
        while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE )) { TranslateMessage( &msg ); DispatchMessageW( &msg ); }
        Sleep( 20 );
    }
    outf( "泵消息后回调是否执行                    : %s\n", handler_ran ? "是（TryEnqueue 真的派发了）" : "否 <-- 没跑" );

    queue2->lpVtbl->Release( queue2 );
    statics->lpVtbl->Release( statics );
    out( handler_ran ? "\n全部通过\n" : "\n有失败\n" );
    return handler_ran ? 0 : 1;
}
