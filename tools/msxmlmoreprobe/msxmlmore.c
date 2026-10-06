/*
 * msxmlmore -- the parts of msxml's DOM documents that Wine still answers differently:
 *
 *   interfaces   what each document class answers to QueryInterface
 *   reasons      parseError code, reason text and position for broken documents and failing streams
 *   pending      loading from a stream that answers E_PENDING: what load returns, when it reads again
 *   blocks       the sizes msxml asks Read for, with streams that hand out the data in various pieces
 *   entities     the nodes a document holds for references to entities declared in its DTD
 *
 * Text is printed with everything outside printable ASCII as \uXXXX.  No files, no network; streams
 * are the probe's own objects, and no output pointer is passed that has not been seen to be safe.
 *
 *   msxmlmore [section...]
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <ole2.h>
#include <ocidl.h>
#include <objsafe.h>
#include <dispex.h>
#include <urlmon.h>
#include <docobj.h>
#include <initguid.h>
#include <msxml6.h>

DEFINE_GUID( CLSID_DOMDocument_probe, 0x2933bf90, 0x7b36, 0x11d2, 0xb2, 0x0e, 0x00, 0xc0, 0x4f, 0x98, 0x3e, 0x60 );
DEFINE_GUID( CLSID_DOMDocument26_probe, 0xf5078f1b, 0xc551, 0x11d3, 0x89, 0xb9, 0x00, 0x00, 0xf8, 0x1f, 0xe2, 0x21 );
DEFINE_GUID( CLSID_DOMDocument30_probe, 0xf5078f32, 0xc551, 0x11d3, 0x89, 0xb9, 0x00, 0x00, 0xf8, 0x1f, 0xe2, 0x21 );
DEFINE_GUID( CLSID_FreeThreadedDOMDocument30_probe, 0xf5078f33, 0xc551, 0x11d3, 0x89, 0xb9, 0x00, 0x00, 0xf8, 0x1f, 0xe2, 0x21 );
DEFINE_GUID( CLSID_FreeThreadedDOMDocument60_probe, 0x88d96a06, 0xf192, 0x11d4, 0xa6, 0x5f, 0x00, 0x40, 0x96, 0x32, 0x51, 0xe5 );

static const struct { const CLSID *clsid; const char *name; } docs[] =
{
    { &CLSID_DOMDocument_probe, "Msxml2.DOMDocument" },
    { &CLSID_DOMDocument26_probe, "Msxml2.DOMDocument.2.6" },
    { &CLSID_DOMDocument30_probe, "Msxml2.DOMDocument.3.0" },
    { &CLSID_FreeThreadedDOMDocument30_probe, "Msxml2.FreeThreadedDOMDocument.3.0" },
    { &CLSID_DOMDocument60, "Msxml2.DOMDocument.6.0" },
    { &CLSID_FreeThreadedDOMDocument60_probe, "Msxml2.FreeThreadedDOMDocument.6.0" },
};

/* the two versions the other sections compare */
static const struct { const CLSID *clsid; const char *name; } versions[] =
{
    { &CLSID_DOMDocument30_probe, "3.0" },
    { &CLSID_DOMDocument60, "6.0" },
};

static void put_text( const WCHAR *str, int len )
{
    int i;
    if (!str) { printf( "(null)" ); return; }
    if (len < 0) len = lstrlenW( str );
    for (i = 0; i < len; i++)
        if (str[i] >= 0x20 && str[i] < 0x7f && str[i] != '\\') putchar( str[i] );
        else printf( "\\u%04x", str[i] );
}

static void put_bstr( BSTR str )
{
    putchar( '"' );
    put_text( str, str ? (int)SysStringLen( str ) : 0 );
    putchar( '"' );
}

static IXMLDOMDocument2 *create_doc( const CLSID *clsid )
{
    IXMLDOMDocument2 *doc = NULL;
    VARIANT v;

    if (FAILED(CoCreateInstance( clsid, NULL, CLSCTX_INPROC_SERVER, &IID_IXMLDOMDocument2, (void **)&doc )))
        return NULL;
    /* MSXML 6 refuses a DTD unless asked, and the documents here have one */
    V_VT( &v ) = VT_BOOL;
    V_BOOL( &v ) = VARIANT_FALSE;
    IXMLDOMDocument2_setProperty( doc, (BSTR)L"ProhibitDTD", v );
    return doc;
}

/*** interfaces ***/

static void section_interfaces( void )
{
    static const struct { const IID *iid; const char *name; } iids[] =
    {
        { &IID_IXMLDOMNode, "IXMLDOMNode" },
        { &IID_IXMLDOMDocument, "IXMLDOMDocument" },
        { &IID_IXMLDOMDocument2, "IXMLDOMDocument2" },
        { &IID_IXMLDOMDocument3, "IXMLDOMDocument3" },
        { &IID_IDispatch, "IDispatch" },
        { &IID_IDispatchEx, "IDispatchEx" },
        { &IID_IPersistStream, "IPersistStream" },
        { &IID_IPersistStreamInit, "IPersistStreamInit" },
        { &IID_IPersistMoniker, "IPersistMoniker" },
        { &IID_IObjectWithSite, "IObjectWithSite" },
        { &IID_IObjectSafety, "IObjectSafety" },
        { &IID_ISupportErrorInfo, "ISupportErrorInfo" },
        { &IID_IConnectionPointContainer, "IConnectionPointContainer" },
        { &IID_IProvideClassInfo, "IProvideClassInfo" },
        { &IID_IOleCommandTarget, "IOleCommandTarget" },
        { &IID_IMarshal, "IMarshal" },
        { &IID_IServiceProvider, "IServiceProvider" },
    };
    unsigned int i, j;

    printf( "== interfaces\n" );
    for (i = 0; i < ARRAYSIZE(docs); i++)
    {
        IUnknown *unk = NULL, *obj;
        HRESULT hr = CoCreateInstance( docs[i].clsid, NULL, CLSCTX_INPROC_SERVER, &IID_IUnknown, (void **)&unk );

        printf( "%s: CoCreateInstance(IUnknown) %#lx\n", docs[i].name, hr );
        if (FAILED(hr)) continue;
        for (j = 0; j < ARRAYSIZE(iids); j++)
        {
            obj = (void *)0xdeadbeef;
            hr = IUnknown_QueryInterface( unk, iids[j].iid, (void **)&obj );
            printf( "  %-26s %s", iids[j].name, hr == S_OK ? "yes" : hr == E_NOINTERFACE ? "no" : "" );
            if (hr != S_OK && hr != E_NOINTERFACE) printf( "hr %#lx", hr );
            if (hr != S_OK && obj != NULL) printf( " (out %s)", obj == (void *)0xdeadbeef ? "untouched" : "set" );
            putchar( '\n' );
            if (hr == S_OK && obj) IUnknown_Release( obj );
        }
        IUnknown_Release( unk );
    }
}

/*** a stream that hands out a script of answers ***/

enum step_kind { STEP_DATA, STEP_HR };

struct step
{
    enum step_kind kind;
    const char *data;          /* STEP_DATA: these bytes (at most what is asked for, the rest next time) */
    unsigned int max;          /* STEP_DATA: hand out at most this many per Read, 0 for no limit */
    HRESULT hr;                /* STEP_HR: answer this, count untouched */
};

struct script_stream
{
    IStream IStream_iface;
    LONG ref;
    struct step steps[8];
    unsigned int step, offset, count;
    unsigned int calls;
    ULONG asked[64];
    DWORD when[64];
    HRESULT answered[64];
    ULONG given[64];
    DWORD start;
    BOOL log_qi;
};

static struct script_stream *impl_from_IStream( IStream *iface )
{
    return CONTAINING_RECORD( iface, struct script_stream, IStream_iface );
}

static HRESULT WINAPI stream_QueryInterface( IStream *iface, REFIID iid, void **out )
{
    struct script_stream *stream = impl_from_IStream( iface );

    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IStream ) || IsEqualGUID( iid, &IID_ISequentialStream ))
    {
        *out = iface;
        IStream_AddRef( iface );
        return S_OK;
    }
    if (stream->log_qi)
    {
        WCHAR str[40];
        StringFromGUID2( iid, str, ARRAYSIZE(str) );
        printf( "    QI " );
        put_text( str, -1 );
        printf( "\n" );
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI stream_AddRef( IStream *iface )
{
    return InterlockedIncrement( &impl_from_IStream( iface )->ref );
}

static ULONG WINAPI stream_Release( IStream *iface )
{
    return InterlockedDecrement( &impl_from_IStream( iface )->ref ); /* the probe frees it */
}

static HRESULT WINAPI stream_Read( IStream *iface, void *buf, ULONG size, ULONG *read )
{
    struct script_stream *stream = impl_from_IStream( iface );
    unsigned int call = stream->calls++;
    const struct step *step;
    HRESULT hr = S_FALSE;
    ULONG given = 0;

    if (read) *read = 0;
    while ((step = &stream->steps[stream->step])->kind == STEP_DATA && step->data &&
           stream->offset >= strlen( step->data ))
    {
        stream->step++;
        stream->offset = 0;
    }
    if (step->kind == STEP_HR && step->hr)
    {
        hr = step->hr;
        stream->step++;
    }
    else if (step->kind == STEP_DATA && step->data)
    {
        given = min( size, strlen( step->data ) - stream->offset );
        if (step->max && given > step->max) given = step->max;
        memcpy( buf, step->data + stream->offset, given );
        stream->offset += given;
        if (read) *read = given;
        hr = S_OK;
    }
    if (call < ARRAYSIZE(stream->asked))
    {
        stream->asked[call] = size;
        stream->when[call] = GetTickCount() - stream->start;
        stream->answered[call] = hr;
        stream->given[call] = given;
    }
    return hr;
}

static HRESULT WINAPI stream_Write( IStream *iface, const void *buf, ULONG size, ULONG *written ) { return E_NOTIMPL; }
static HRESULT WINAPI stream_Seek( IStream *iface, LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER *pos ) { return E_NOTIMPL; }
static HRESULT WINAPI stream_SetSize( IStream *iface, ULARGE_INTEGER size ) { return E_NOTIMPL; }
static HRESULT WINAPI stream_CopyTo( IStream *iface, IStream *dst, ULARGE_INTEGER size, ULARGE_INTEGER *read, ULARGE_INTEGER *written ) { return E_NOTIMPL; }
static HRESULT WINAPI stream_Commit( IStream *iface, DWORD flags ) { return E_NOTIMPL; }
static HRESULT WINAPI stream_Revert( IStream *iface ) { return E_NOTIMPL; }
static HRESULT WINAPI stream_LockRegion( IStream *iface, ULARGE_INTEGER offset, ULARGE_INTEGER size, DWORD type ) { return E_NOTIMPL; }
static HRESULT WINAPI stream_UnlockRegion( IStream *iface, ULARGE_INTEGER offset, ULARGE_INTEGER size, DWORD type ) { return E_NOTIMPL; }
static HRESULT WINAPI stream_Stat( IStream *iface, STATSTG *stat, DWORD flag ) { return E_NOTIMPL; }
static HRESULT WINAPI stream_Clone( IStream *iface, IStream **out ) { return E_NOTIMPL; }

static const IStreamVtbl stream_vtbl =
{
    stream_QueryInterface, stream_AddRef, stream_Release, stream_Read, stream_Write, stream_Seek,
    stream_SetSize, stream_CopyTo, stream_Commit, stream_Revert, stream_LockRegion, stream_UnlockRegion,
    stream_Stat, stream_Clone,
};

/* never freed: msxml may hold on to a stream after the call that loads from it */
static struct script_stream *new_stream( const struct step *steps )
{
    struct script_stream *stream = calloc( 1, sizeof(*stream) );
    unsigned int i;

    stream->IStream_iface.lpVtbl = &stream_vtbl;
    stream->ref = 1;
    for (i = 0; i < ARRAYSIZE(stream->steps) - 1; i++)
    {
        stream->steps[i] = steps[i];
        if (steps[i].kind == STEP_DATA && !steps[i].data) break;
    }
    stream->start = GetTickCount();
    return stream;
}

static void stream_print_calls( const struct script_stream *stream, BOOL times )
{
    unsigned int i, n = min( stream->calls, ARRAYSIZE(stream->asked) );

    printf( "    reads %u:", stream->calls );
    for (i = 0; i < n; i++)
    {
        printf( " %lu", stream->asked[i] );
        if (stream->answered[i] == S_OK) printf( "->%lu", stream->given[i] );
        else if (stream->answered[i] == S_FALSE) printf( "->end" );
        else printf( "->%#lx", stream->answered[i] );
        if (times) printf( "@%lu", stream->when[i] );
    }
    printf( "\n" );
}

static void print_parse_error( IXMLDOMDocument2 *doc )
{
    IXMLDOMParseError *error = NULL;
    LONG code = 0, line = 0, linepos = 0, filepos = 0;
    BSTR reason = NULL, src = NULL;

    if (FAILED(IXMLDOMDocument2_get_parseError( doc, &error )) || !error)
    {
        printf( "    no parseError\n" );
        return;
    }
    IXMLDOMParseError_get_errorCode( error, &code );
    IXMLDOMParseError_get_reason( error, &reason );
    IXMLDOMParseError_get_line( error, &line );
    IXMLDOMParseError_get_linepos( error, &linepos );
    IXMLDOMParseError_get_filepos( error, &filepos );
    IXMLDOMParseError_get_srcText( error, &src );
    printf( "    error %#lx line %ld linepos %ld filepos %ld reason ", code, line, linepos, filepos );
    put_bstr( reason );
    printf( " src " );
    put_bstr( src );
    printf( "\n" );
    SysFreeString( reason );
    SysFreeString( src );
    IXMLDOMParseError_Release( error );
}

/*** reasons ***/

static void section_reasons( void )
{
    static const struct { const char *name; const WCHAR *xml; } broken[] =
    {
        { "empty", L"" },
        { "unclosed", L"<a>" },
        { "mismatch", L"<a></b>" },
        { "undefined entity", L"<a>&x;</a>" },
        { "duplicate attribute", L"<a b='1' b='2'/>" },
        { "control character", L"<a>\x01</a>" },
        { "version 2.0", L"<?xml version='2.0'?><a/>" },
        { "two roots", L"<a/><b/>" },
        { "text at top", L"text" },
        { "end in tag", L"<a" },
        { "bad name", L"<1a/>" },
        { "lt in attribute", L"<a b='<'/>" },
        { "unquoted attribute", L"<a b=1/>" },
        { "second line", L"<a>\n  <b>\n</a>" },
    };
    static const HRESULT failures[] = { E_FAIL, STG_E_ACCESSDENIED, E_OUTOFMEMORY, E_ABORT, E_INVALIDARG, 0x80070005 };
    unsigned int v, i;

    printf( "== reasons\n" );
    for (v = 0; v < ARRAYSIZE(versions); v++)
    {
        for (i = 0; i < ARRAYSIZE(broken); i++)
        {
            IXMLDOMDocument2 *doc = create_doc( versions[v].clsid );
            VARIANT_BOOL ok = 0x55;
            HRESULT hr;

            if (!doc) { printf( "%s: no document\n", versions[v].name ); break; }
            hr = IXMLDOMDocument2_loadXML( doc, (BSTR)broken[i].xml, &ok );
            printf( "%s %-20s loadXML hr %#lx ok %d\n", versions[v].name, broken[i].name, hr, ok );
            print_parse_error( doc );
            IXMLDOMDocument2_Release( doc );
        }
        for (i = 0; i < ARRAYSIZE(failures); i++)
        {
            struct step steps[] = { { STEP_HR, NULL, 0, failures[i] }, { STEP_DATA, NULL } };
            IXMLDOMDocument2 *doc = create_doc( versions[v].clsid );
            struct script_stream *stream;
            VARIANT_BOOL ok = 0x55;
            VARIANT src;
            HRESULT hr;

            if (!doc) break;
            stream = new_stream( steps );
            V_VT( &src ) = VT_UNKNOWN;
            V_UNKNOWN( &src ) = (IUnknown *)&stream->IStream_iface;
            hr = IXMLDOMDocument2_load( doc, src, &ok );
            printf( "%s stream fails %#lx: load hr %#lx ok %d\n", versions[v].name, failures[i], hr, ok );
            print_parse_error( doc );
            IXMLDOMDocument2_Release( doc );
        }
    }
}

/*** pending ***/

static void pump( DWORD ms )
{
    DWORD end = GetTickCount() + ms;
    MSG msg;

    while ((LONG)(end - GetTickCount()) > 0)
    {
        while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE ))
        {
            TranslateMessage( &msg );
            DispatchMessageW( &msg );
        }
        MsgWaitForMultipleObjects( 0, NULL, FALSE, 20, QS_ALLINPUT );
    }
}

static void print_state( IXMLDOMDocument2 *doc, const char *when )
{
    IXMLDOMElement *root = NULL;
    LONG state = -1;

    IXMLDOMDocument2_get_readyState( doc, &state );
    IXMLDOMDocument2_get_documentElement( doc, &root );
    printf( "    %s: readyState %ld root %s\n", when, state, root ? "yes" : "no" );
    if (root) IXMLDOMElement_Release( root );
}

static void section_pending( void )
{
    static const char xml[] = "<?xml version=\"1.0\"?><r><a/></r>";
    static const struct step once[] = { { STEP_HR, NULL, 0, E_PENDING }, { STEP_DATA, xml }, { STEP_DATA, NULL } };
    static const struct step twice[] = { { STEP_HR, NULL, 0, E_PENDING }, { STEP_HR, NULL, 0, E_PENDING },
                                         { STEP_DATA, xml }, { STEP_DATA, NULL } };
    static const struct step five[] = { { STEP_HR, NULL, 0, E_PENDING }, { STEP_HR, NULL, 0, E_PENDING },
                                        { STEP_HR, NULL, 0, E_PENDING }, { STEP_HR, NULL, 0, E_PENDING },
                                        { STEP_HR, NULL, 0, E_PENDING }, { STEP_DATA, xml }, { STEP_DATA, NULL } };
    static const struct step middle[] = { { STEP_DATA, "<?xml version=\"1.0\"?><r>" }, { STEP_HR, NULL, 0, E_PENDING },
                                          { STEP_DATA, "<a/></r>" }, { STEP_DATA, NULL } };
    static const struct { const char *name; const struct step *steps; } scripts[] =
    {
        { "pending once", once }, { "pending twice", twice }, { "pending five times", five },
        { "pending in the middle", middle },
    };
    unsigned int v, i, async;

    printf( "== pending\n" );
    for (v = 0; v < ARRAYSIZE(versions); v++)
    for (i = 0; i < ARRAYSIZE(scripts); i++)
    for (async = 0; async < 2; async++)
    {
        IXMLDOMDocument2 *doc = create_doc( versions[v].clsid );
        struct script_stream *stream;
        VARIANT_BOOL ok = 0x55;
        VARIANT src;
        HRESULT hr;

        if (!doc) break;
        IXMLDOMDocument2_put_async( doc, async ? VARIANT_TRUE : VARIANT_FALSE );
        stream = new_stream( scripts[i].steps );
        V_VT( &src ) = VT_UNKNOWN;
        V_UNKNOWN( &src ) = (IUnknown *)&stream->IStream_iface;
        hr = IXMLDOMDocument2_load( doc, src, &ok );
        printf( "%s %-22s async %d: load hr %#lx ok %d, stream refs %ld\n", versions[v].name, scripts[i].name,
                async, hr, ok, stream->ref );
        stream_print_calls( stream, TRUE );
        print_state( doc, "after load" );
        pump( 1500 );
        print_state( doc, "after 1.5 s of messages" );
        stream_print_calls( stream, TRUE );
        print_parse_error( doc );
        IXMLDOMDocument2_Release( doc );
        printf( "    stream refs after the document is released %ld\n", stream->ref );

        /* the same through IPersistStreamInit */
        if (!async && (doc = create_doc( versions[v].clsid )))
        {
            IPersistStreamInit *persist = NULL;

            stream = new_stream( scripts[i].steps );
            if (SUCCEEDED(IXMLDOMDocument2_QueryInterface( doc, &IID_IPersistStreamInit, (void **)&persist )))
            {
                hr = IPersistStreamInit_Load( persist, &stream->IStream_iface );
                printf( "    IPersistStreamInit::Load hr %#lx, stream refs %ld\n", hr, stream->ref );
                stream_print_calls( stream, FALSE );
                print_state( doc, "after Load" );
                pump( 500 );
                print_state( doc, "after 0.5 s of messages" );
                stream_print_calls( stream, FALSE );
                IPersistStreamInit_Release( persist );
            }
            IXMLDOMDocument2_Release( doc );
        }
    }
}

/*** blocks ***/

static char big[30000];

static void section_blocks( void )
{
    static const unsigned int limits[] = { 0, 1, 7, 100, 4095, 5000 };
    unsigned int v, i, persist;

    printf( "== blocks\n" );
    /* 30000 bytes: a declaration, a root, and comments to fill it */
    {
        char *p = big;
        p += sprintf( p, "<?xml version=\"1.0\"?><r>" );
        while (p - big < (int)sizeof(big) - 40) p += sprintf( p, "<!--0123456789abcdef-->" );
        sprintf( p, "</r>" );
    }
    for (v = 0; v < ARRAYSIZE(versions); v++)
    for (i = 0; i < ARRAYSIZE(limits); i++)
    for (persist = 0; persist < 2; persist++)
    {
        struct step steps[] = { { STEP_DATA, big, limits[i] }, { STEP_DATA, NULL } };
        IXMLDOMDocument2 *doc = create_doc( versions[v].clsid );
        struct script_stream *stream;
        VARIANT_BOOL ok = 0x55;
        HRESULT hr;

        if (!doc) break;
        stream = new_stream( steps );
        stream->log_qi = (i == 0);
        if (persist)
        {
            IPersistStreamInit *init = NULL;
            hr = IXMLDOMDocument2_QueryInterface( doc, &IID_IPersistStreamInit, (void **)&init );
            if (SUCCEEDED(hr))
            {
                printf( "%s at most %u per Read, IPersistStreamInit::Load:\n", versions[v].name, limits[i] );
                hr = IPersistStreamInit_Load( init, &stream->IStream_iface );
                IPersistStreamInit_Release( init );
            }
        }
        else
        {
            VARIANT src;
            V_VT( &src ) = VT_UNKNOWN;
            V_UNKNOWN( &src ) = (IUnknown *)&stream->IStream_iface;
            printf( "%s at most %u per Read, load:\n", versions[v].name, limits[i] );
            hr = IXMLDOMDocument2_load( doc, src, &ok );
        }
        printf( "    hr %#lx ok %d\n", hr, ok );
        stream_print_calls( stream, FALSE );
        IXMLDOMDocument2_Release( doc );
    }
}

/*** entities ***/

static const char *type_name( DOMNodeType type )
{
    static const char *names[] = { "?", "element", "attribute", "text", "cdata", "entityref", "entity", "pi",
                                   "comment", "document", "doctype", "fragment", "notation" };
    return type >= 0 && type < ARRAYSIZE(names) ? names[type] : "?";
}

static void dump_node( IXMLDOMNode *node, int depth )
{
    IXMLDOMNamedNodeMap *attrs = NULL;
    IXMLDOMNode *child = NULL, *next;
    DOMNodeType type = 0;
    BSTR name = NULL;
    VARIANT value;

    IXMLDOMNode_get_nodeType( node, &type );
    IXMLDOMNode_get_nodeName( node, &name );
    VariantInit( &value );
    IXMLDOMNode_get_nodeValue( node, &value );
    printf( "    %*s%s ", depth * 2, "", type_name( type ) );
    put_bstr( name );
    if (V_VT( &value ) == VT_BSTR)
    {
        printf( " value " );
        put_bstr( V_BSTR( &value ) );
    }
    if (type == NODE_ENTITY_REFERENCE || type == NODE_ATTRIBUTE)
    {
        BSTR text = NULL, xml = NULL;
        IXMLDOMNode_get_text( node, &text );
        IXMLDOMNode_get_xml( node, &xml );
        printf( " text " );
        put_bstr( text );
        printf( " xml " );
        put_bstr( xml );
        SysFreeString( text );
        SysFreeString( xml );
    }
    printf( "\n" );
    VariantClear( &value );
    SysFreeString( name );

    if (type == NODE_ELEMENT && SUCCEEDED(IXMLDOMNode_get_attributes( node, &attrs )) && attrs)
    {
        LONG count = 0, i;
        IXMLDOMNamedNodeMap_get_length( attrs, &count );
        for (i = 0; i < count; i++)
        {
            IXMLDOMNode *attr = NULL;
            if (SUCCEEDED(IXMLDOMNamedNodeMap_get_item( attrs, i, &attr )) && attr)
            {
                dump_node( attr, depth + 1 );
                IXMLDOMNode_Release( attr );
            }
        }
        IXMLDOMNamedNodeMap_Release( attrs );
    }
    if (depth > 6) return;
    IXMLDOMNode_get_firstChild( node, &child );
    while (child)
    {
        dump_node( child, depth + 1 );
        next = NULL;
        IXMLDOMNode_get_nextSibling( child, &next );
        IXMLDOMNode_Release( child );
        child = next;
    }
}

static void print_xml_text( const char *what, IXMLDOMNode *node )
{
    BSTR xml = NULL, text = NULL;

    IXMLDOMNode_get_xml( node, &xml );
    IXMLDOMNode_get_text( node, &text );
    printf( "    %s xml ", what );
    put_bstr( xml );
    printf( "\n    %s text ", what );
    put_bstr( text );
    printf( "\n" );
    SysFreeString( xml );
    SysFreeString( text );
}

static void section_entities( void )
{
    static const struct { const char *name; const WCHAR *xml; } sources[] =
    {
        { "internal entities",
          L"<!DOCTYPE r [<!ENTITY e \"x\"><!ENTITY m \"<b>y</b>\"><!ENTITY n \"a&e;b\"><!ENTITY s \"\">]>"
          L"<r a=\"1&e;2\" c=\"&n;\">p&e;q&m;s&n;&s;&#65;&amp;</r>" },
        { "whitespace entity", L"<!DOCTYPE r [<!ENTITY w \" \">]><r>&w;<b/>&w;</r>" },
        { "entity at top level", L"<!DOCTYPE r [<!ENTITY e \"x\">]><r>&e;</r>" },
    };
    unsigned int v, i;

    printf( "== entities\n" );
    for (v = 0; v < ARRAYSIZE(versions); v++)
    for (i = 0; i < ARRAYSIZE(sources); i++)
    {
        IXMLDOMDocument2 *doc = create_doc( versions[v].clsid );
        IXMLDOMElement *root = NULL;
        VARIANT_BOOL ok = 0x55;
        HRESULT hr;

        if (!doc) break;
        hr = IXMLDOMDocument2_loadXML( doc, (BSTR)sources[i].xml, &ok );
        printf( "%s %s: loadXML hr %#lx ok %d\n", versions[v].name, sources[i].name, hr, ok );
        if (hr != S_OK) print_parse_error( doc );
        if (hr == S_OK && SUCCEEDED(IXMLDOMDocument2_get_documentElement( doc, &root )) && root)
        {
            IXMLDOMNode *clone = NULL;
            IXMLDOMNodeList *list = NULL;

            dump_node( (IXMLDOMNode *)root, 0 );
            print_xml_text( "root", (IXMLDOMNode *)root );
            if (i == 0)
            {
                IXMLDOMNode *first = NULL;
                LONG length = -1;

                if (SUCCEEDED(IXMLDOMElement_get_childNodes( root, &list )) && list)
                {
                    IXMLDOMNodeList_get_length( list, &length );
                    printf( "    childNodes length %ld\n", length );
                    IXMLDOMNodeList_Release( list );
                    list = NULL;
                }
                if (SUCCEEDED(IXMLDOMElement_cloneNode( root, VARIANT_TRUE, &clone )) && clone)
                {
                    print_xml_text( "deep clone", clone );
                    IXMLDOMNode_Release( clone );
                }
                if (SUCCEEDED(IXMLDOMElement_selectNodes( root, (BSTR)L"text()", &list )) && list)
                {
                    IXMLDOMNodeList_get_length( list, &length );
                    printf( "    selectNodes(text()) length %ld\n", length );
                    IXMLDOMNodeList_Release( list );
                    list = NULL;
                }
                /* the entity reference after "p": change it */
                IXMLDOMElement_get_firstChild( root, &first );
                if (first)
                {
                    IXMLDOMNode *ref = NULL;
                    IXMLDOMNode_get_nextSibling( first, &ref );
                    if (ref)
                    {
                        DOMNodeType type = 0;
                        IXMLDOMNode_get_nodeType( ref, &type );
                        if (type == NODE_ENTITY_REFERENCE)
                        {
                            IXMLDOMText *text = NULL;
                            IXMLDOMNode *added = NULL, *child = NULL;
                            IXMLDOMDocument2_createTextNode( doc, (BSTR)L"z", &text );
                            hr = IXMLDOMNode_appendChild( ref, (IXMLDOMNode *)text, &added );
                            printf( "    appendChild to the entity reference: hr %#lx\n", hr );
                            if (added) IXMLDOMNode_Release( added );
                            IXMLDOMNode_get_firstChild( ref, &child );
                            if (child)
                            {
                                VARIANT value;
                                V_VT( &value ) = VT_BSTR;
                                V_BSTR( &value ) = SysAllocString( L"changed" );
                                hr = IXMLDOMNode_put_nodeValue( child, value );
                                printf( "    put_nodeValue on its text: hr %#lx\n", hr );
                                VariantClear( &value );
                                IXMLDOMNode_Release( child );
                            }
                            if (text) IXMLDOMText_Release( text );
                        }
                        else printf( "    second child is %s, not an entity reference\n", type_name( type ) );
                        IXMLDOMNode_Release( ref );
                    }
                    IXMLDOMNode_Release( first );
                }
                /* createEntityReference, for a declared and an undeclared entity */
                {
                    static const WCHAR *names[] = { L"e", L"m", L"undeclared" };
                    unsigned int k;
                    for (k = 0; k < ARRAYSIZE(names); k++)
                    {
                        IXMLDOMEntityReference *created = NULL;
                        hr = IXMLDOMDocument2_createEntityReference( doc, (BSTR)names[k], &created );
                        printf( "    createEntityReference(" );
                        put_text( names[k], -1 );
                        printf( "): hr %#lx\n", hr );
                        if (created)
                        {
                            IXMLDOMElement *holder = NULL;
                            dump_node( (IXMLDOMNode *)created, 1 );
                            IXMLDOMDocument2_createElement( doc, (BSTR)L"h", &holder );
                            if (holder)
                            {
                                IXMLDOMNode *added = NULL;
                                IXMLDOMElement_appendChild( holder, (IXMLDOMNode *)created, &added );
                                if (added) IXMLDOMNode_Release( added );
                                print_xml_text( "in an element", (IXMLDOMNode *)holder );
                                IXMLDOMElement_Release( holder );
                            }
                            IXMLDOMEntityReference_Release( created );
                        }
                    }
                }
            }
            IXMLDOMElement_Release( root );
        }
        IXMLDOMDocument2_Release( doc );
    }
}

int main( int argc, char **argv )
{
    static const struct { const char *name; void (*func)(void); } sections[] =
    {
        { "interfaces", section_interfaces }, { "reasons", section_reasons }, { "pending", section_pending },
        { "blocks", section_blocks }, { "entities", section_entities },
    };
    unsigned int i;
    int j;

    CoInitialize( NULL );
    for (i = 0; i < ARRAYSIZE(sections); i++)
    {
        BOOL run = argc < 2;
        for (j = 1; j < argc; j++) if (!strcmp( argv[j], sections[i].name )) run = TRUE;
        if (run) sections[i].func();
    }
    CoUninitialize();
    return 0;
}
