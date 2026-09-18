/*
 * Check that AMSI reaches a real scanner and reports what it actually found.
 *
 * The point of these cases is the difference between "scanned and clean" and
 * "not scanned at all": the first must come back NOT_DETECTED, the second must
 * come back as a failure, and known-bad content must come back DETECTED.  A
 * build that answers NOT_DETECTED without an engine behind it fails here.
 *
 * The test string is the EICAR standard antivirus test file -- not malware,
 * but every engine is required to recognise it.  It is assembled at runtime so
 * this source file does not itself trip a scanner.
 *
 * Copyright 2026 the Wine Altars project.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <objbase.h>
#include <amsi.h>
#include <stdio.h>
#include <string.h>

/* amsi.dll ships no import library, so reach it the way callers do. */
static HRESULT (WINAPI *pAmsiInitialize)( const WCHAR *, HAMSICONTEXT * );
static void    (WINAPI *pAmsiUninitialize)( HAMSICONTEXT );
static HRESULT (WINAPI *pAmsiOpenSession)( HAMSICONTEXT, HAMSISESSION * );
static void    (WINAPI *pAmsiCloseSession)( HAMSICONTEXT, HAMSISESSION );
static HRESULT (WINAPI *pAmsiScanBuffer)( HAMSICONTEXT, void *, ULONG, const WCHAR *,
                                          HAMSISESSION, AMSI_RESULT * );
static HRESULT (WINAPI *pAmsiScanString)( HAMSICONTEXT, const WCHAR *, const WCHAR *,
                                          HAMSISESSION, AMSI_RESULT * );


static int failures;

static void check( int ok, const char *what, const char *detail )
{
    printf( "%-46s %s%s%s\n", what, ok ? "ok" : "FAIL",
            detail && *detail ? " -- " : "", detail ? detail : "" );
    if (!ok) failures++;
}

static const char *result_name( AMSI_RESULT r )
{
    if (r == AMSI_RESULT_CLEAN) return "CLEAN";
    if (r == AMSI_RESULT_NOT_DETECTED) return "NOT_DETECTED";
    if (r >= AMSI_RESULT_BLOCKED_BY_ADMIN_START && r <= AMSI_RESULT_BLOCKED_BY_ADMIN_END)
        return "BLOCKED_BY_ADMIN";
    if (r == AMSI_RESULT_DETECTED) return "DETECTED";
    return "?";
}

/* Built in pieces so the literal never appears whole in the binary either. */
static void build_eicar( char *out, size_t size )
{
    static const char a[] = "X5O!P%@AP[4\\PZX54(P^)7CC)7}$";
    static const char b[] = "EICAR-STANDARD-ANTIVIRUS-TEST-FILE!";
    static const char c[] = "$H+H*";

    snprintf( out, size, "%s%s%s", a, b, c );
}

/* A stream over a fixed buffer, which is all IAntimalware::Scan needs. */
struct test_stream
{
    IAmsiStream IAmsiStream_iface;
    LONG ref;
    const char *data;
    ULONG size;
};

static struct test_stream *impl_from_IAmsiStream( IAmsiStream *iface )
{
    return CONTAINING_RECORD( iface, struct test_stream, IAmsiStream_iface );
}

static HRESULT WINAPI stream_QueryInterface( IAmsiStream *iface, REFIID iid, void **out )
{
    if (IsEqualIID( iid, &IID_IUnknown ) || IsEqualIID( iid, &IID_IAmsiStream ))
    {
        *out = iface;
        IAmsiStream_AddRef( iface );
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI stream_AddRef( IAmsiStream *iface )
{
    return InterlockedIncrement( &impl_from_IAmsiStream( iface )->ref );
}

static ULONG WINAPI stream_Release( IAmsiStream *iface )
{
    return InterlockedDecrement( &impl_from_IAmsiStream( iface )->ref );
}

static HRESULT WINAPI stream_GetAttribute( IAmsiStream *iface, AMSI_ATTRIBUTE attribute,
                                           ULONG data_size, unsigned char *data, ULONG *ret_size )
{
    struct test_stream *impl = impl_from_IAmsiStream( iface );

    if (attribute == AMSI_ATTRIBUTE_CONTENT_SIZE)
    {
        ULONGLONG size = impl->size;

        if (data_size < sizeof(size)) return E_NOT_SUFFICIENT_BUFFER;
        memcpy( data, &size, sizeof(size) );
        *ret_size = sizeof(size);
        return S_OK;
    }
    if (attribute == AMSI_ATTRIBUTE_CONTENT_NAME)
    {
        static const WCHAR name[] = L"amsiprobe";

        if (data_size < sizeof(name)) return E_NOT_SUFFICIENT_BUFFER;
        memcpy( data, name, sizeof(name) );
        *ret_size = sizeof(name);
        return S_OK;
    }
    *ret_size = 0;
    return E_NOTIMPL;
}

static HRESULT WINAPI stream_Read( IAmsiStream *iface, ULONGLONG position, ULONG size,
                                   unsigned char *buffer, ULONG *read_size )
{
    struct test_stream *impl = impl_from_IAmsiStream( iface );
    ULONG available;

    if (position > impl->size) return E_INVALIDARG;
    available = impl->size - (ULONG)position;
    if (size > available) size = available;
    memcpy( buffer, impl->data + position, size );
    *read_size = size;
    return S_OK;
}

static const IAmsiStreamVtbl stream_vtbl =
{
    stream_QueryInterface,
    stream_AddRef,
    stream_Release,
    stream_GetAttribute,
    stream_Read,
};

static void test_com_surface( const char *eicar, const char *clean )
{
    struct test_stream stream = { { (IAmsiStreamVtbl *)&stream_vtbl }, 1, NULL, 0 };
    IAntimalware *antimalware = NULL;
    AMSI_RESULT result;
    HRESULT hr;
    char detail[128];

    hr = CoCreateInstance( &CLSID_Antimalware, NULL, CLSCTX_INPROC_SERVER,
                           &IID_IAntimalware, (void **)&antimalware );
    sprintf( detail, "hr %#lx", hr );
    check( SUCCEEDED(hr) && antimalware, "COM: CoCreateInstance(Antimalware)", detail );
    if (FAILED(hr) || !antimalware) return;

    stream.data = eicar;
    stream.size = strlen( eicar );
    result = AMSI_RESULT_NOT_DETECTED;
    hr = IAntimalware_Scan( antimalware, &stream.IAmsiStream_iface, &result, NULL );
    sprintf( detail, "hr %#lx, %s", hr, result_name(result) );
    check( SUCCEEDED(hr) && result == AMSI_RESULT_DETECTED, "COM: Scan(test signature) -> DETECTED", detail );

    stream.data = clean;
    stream.size = strlen( clean );
    result = AMSI_RESULT_DETECTED;
    hr = IAntimalware_Scan( antimalware, &stream.IAmsiStream_iface, &result, NULL );
    sprintf( detail, "hr %#lx, %s", hr, result_name(result) );
    check( SUCCEEDED(hr) && result == AMSI_RESULT_NOT_DETECTED, "COM: Scan(harmless macro) -> NOT_DETECTED", detail );

    IAntimalware_Release( antimalware );
}

int main( int argc, char **argv )
{
    static const char clean[] = "Sub Workbook_Open()\r\n    MsgBox \"hello\"\r\nEnd Sub\r\n";
    BOOL expect_engine = !(argc > 1 && !strcmp( argv[1], "--expect-no-engine" ));
    HAMSICONTEXT context = NULL;
    HAMSISESSION session = NULL;
    AMSI_RESULT result;
    char eicar[128], detail[160];
    WCHAR wide[256];
    HRESULT hr;

    HMODULE amsi;

    build_eicar( eicar, sizeof(eicar) );
    CoInitializeEx( NULL, COINIT_APARTMENTTHREADED );

    if (!(amsi = LoadLibraryW( L"amsi.dll" )))
    {
        printf( "amsi.dll could not be loaded (%lu)\n", GetLastError() );
        return 2;
    }
#define GETPROC(f) \
    if (!(p##f = (void *)GetProcAddress( amsi, #f ))) { printf( "missing %s\n", #f ); return 2; }
    GETPROC(AmsiInitialize)
    GETPROC(AmsiUninitialize)
    GETPROC(AmsiOpenSession)
    GETPROC(AmsiCloseSession)
    GETPROC(AmsiScanBuffer)
    GETPROC(AmsiScanString)
#undef GETPROC

    hr = pAmsiInitialize( L"amsiprobe", &context );
    sprintf( detail, "hr %#lx", hr );

    if (!expect_engine)
    {
        /* With no scanner reachable the only honest answer is a failure.
         * Succeeding here would tell the caller its later scans mean
         * something, and they would not. */
        check( FAILED(hr), "no engine: AmsiInitialize fails", detail );
        check( context == NULL, "no engine: no context handed out", "" );
        printf( "\n%d failure(s)\n", failures );
        CoUninitialize();
        return failures ? 1 : 0;
    }

    check( SUCCEEDED(hr) && context, "AmsiInitialize", detail );
    if (FAILED(hr) || !context)
    {
        printf( "\n%d failure(s)\n", failures );
        CoUninitialize();
        return 1;
    }

    hr = pAmsiOpenSession( context, &session );
    sprintf( detail, "hr %#lx", hr );
    check( SUCCEEDED(hr) && session, "AmsiOpenSession", detail );

    result = AMSI_RESULT_NOT_DETECTED;
    hr = pAmsiScanBuffer( context, eicar, strlen( eicar ), L"eicar", session, &result );
    sprintf( detail, "hr %#lx, %s", hr, result_name(result) );
    check( SUCCEEDED(hr) && result == AMSI_RESULT_DETECTED, "AmsiScanBuffer(test signature) -> DETECTED", detail );

    result = AMSI_RESULT_DETECTED;
    hr = pAmsiScanBuffer( context, (void *)clean, strlen( clean ), L"macro", session, &result );
    sprintf( detail, "hr %#lx, %s", hr, result_name(result) );
    check( SUCCEEDED(hr) && result == AMSI_RESULT_NOT_DETECTED, "AmsiScanBuffer(harmless macro) -> NOT_DETECTED", detail );

    MultiByteToWideChar( CP_UTF8, 0, eicar, -1, wide, ARRAYSIZE(wide) );
    result = AMSI_RESULT_NOT_DETECTED;
    hr = pAmsiScanString( context, wide, L"eicar", session, &result );
    sprintf( detail, "hr %#lx, %s", hr, result_name(result) );
    check( SUCCEEDED(hr) && result == AMSI_RESULT_DETECTED, "AmsiScanString(test signature) -> DETECTED", detail );

    /* An empty buffer is a legitimate thing to ask about and must not error. */
    result = AMSI_RESULT_DETECTED;
    hr = pAmsiScanBuffer( context, (void *)clean, 0, L"empty", session, &result );
    sprintf( detail, "hr %#lx, %s", hr, result_name(result) );
    check( SUCCEEDED(hr) && result == AMSI_RESULT_NOT_DETECTED, "AmsiScanBuffer(empty) -> NOT_DETECTED", detail );

    /* Argument checking must not reach the engine at all. */
    hr = pAmsiScanBuffer( context, NULL, 16, L"bad", session, &result );
    sprintf( detail, "hr %#lx", hr );
    check( hr == E_INVALIDARG, "AmsiScanBuffer(NULL, 16) -> E_INVALIDARG", detail );

    hr = pAmsiScanBuffer( (HAMSICONTEXT)0xdeadbeef, (void *)clean, 4, L"bad", session, &result );
    sprintf( detail, "hr %#lx", hr );
    check( hr == E_INVALIDARG, "AmsiScanBuffer(bogus context) -> E_INVALIDARG", detail );

    test_com_surface( eicar, clean );

    pAmsiCloseSession( context, session );
    pAmsiUninitialize( context );
    CoUninitialize();

    printf( "\n%d failure(s)\n", failures );
    return failures ? 1 : 0;
}
