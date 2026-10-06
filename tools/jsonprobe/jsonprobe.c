/*
 * jsonprobe - what Windows.Data.Json does where its documentation is silent.
 *
 * Office's dialogs pass their settings through JsonObject, and Wine's implementation had to choose
 * an answer for everything the documentation leaves open.  A first run of Wine's conformance test
 * on Windows (build 29671) showed some choices were wrong -- an object keeps its members in the
 * order they came, not sorted; a failed TryParse still hands out an object -- and crashed on a
 * NULL out-pointer Windows does not check.  This asks the open questions directly and prints the
 * answers, passing no NULL out-pointer at all, so that it can be run on Windows safely and its
 * output diffed with Wine's:
 *
 *   member order    after Parse, SetNamedValue, a replacement, a removal and re-insertion, a
 *                   name given twice, many names; through Stringify and through iteration
 *   failed parses   what Parse and TryParse leave in their out-parameter, and what it is
 *   errors          the HRESULT of each kind of misuse: a missing name, a wrong type, an index
 *                   out of range, an iterator past its end or after a change
 *   identity        whether a value comes back as the same object that was put in
 *   views           whether GetView and First see later changes
 *   formatting      numbers and strings as Stringify writes them, and as Parse reads them
 *   limits          how deep arrays and objects nest, by Parse and by Stringify
 *   statuses        JsonError.GetJsonStatus for the HRESULTs that matter
 *   split           what IMapView.Split makes of views of a few sizes
 *   more            the depth Parse stops at, found by bisection; where numbers switch to
 *                   exponents, NaN of each kind, underflow; which changes invalidate an object's
 *                   and an array's views and iterators, and an iterator run past its end
 *
 * Text other than printable ASCII is printed as <xxxx>.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <initguid.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <windef.h>
#include <winbase.h>
#include <winstring.h>
#include <roapi.h>

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_Data_Json
#include "windows.data.json.h"

#define SENTINEL ((void *)(ULONG_PTR)0xdeadbeef)

static IJsonObjectStatics *object_statics;
static IJsonArrayStatics *array_statics;
static IJsonValueStatics *value_statics;
static IJsonValueStatics2 *value_statics2;
static IJsonErrorStatics2 *error_statics;

static HSTRING hs( const WCHAR *str )
{
    HSTRING ret = NULL;
    if (str) WindowsCreateString( str, wcslen( str ), &ret );
    return ret;
}

static void put_wide( const WCHAR *str, UINT32 len )
{
    UINT32 i;
    for (i = 0; i < len; i++)
        if (str[i] >= 0x20 && str[i] < 0x7f) putchar( str[i] );
        else printf( "<%04x>", str[i] );
}

static void put_hstring( HSTRING str )
{
    UINT32 len;
    const WCHAR *buf = WindowsGetStringRawBuffer( str, &len );
    put_wide( buf, len );
}

static const char *out_state( void *out )
{
    return out == SENTINEL ? "untouched" : out ? "set" : "NULL";
}

static void *get_statics( const WCHAR *name, const IID *iid )
{
    IActivationFactory *factory;
    HSTRING str = hs( name );
    void *out = NULL;
    HRESULT hr;

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    if (FAILED(hr)) { printf( "factory %ls: %#lx\n", name, hr ); return NULL; }
    hr = IActivationFactory_QueryInterface( factory, iid, &out );
    if (FAILED(hr)) printf( "statics %ls: %#lx\n", name, hr );
    IActivationFactory_Release( factory );
    return out;
}

static void *activate( const WCHAR *name, const IID *iid )
{
    IInspectable *inspectable = NULL;
    HSTRING str = hs( name );
    void *out = NULL;
    HRESULT hr;

    hr = RoActivateInstance( str, &inspectable );
    WindowsDeleteString( str );
    if (FAILED(hr)) { printf( "activate %ls: %#lx\n", name, hr ); return NULL; }
    hr = IInspectable_QueryInterface( inspectable, iid, &out );
    IInspectable_Release( inspectable );
    if (FAILED(hr)) printf( "activate %ls QI: %#lx\n", name, hr );
    return out;
}

/* a value's type and JSON text */
static void show( const char *label, void *obj )
{
    JsonValueType type = -1;
    IJsonValue *value;
    HSTRING str = NULL;
    HRESULT hr;

    printf( "%s: ", label );
    if (!obj) { printf( "(null)\n" ); return; }
    hr = IUnknown_QueryInterface( (IUnknown *)obj, &IID_IJsonValue, (void **)&value );
    if (FAILED(hr)) { printf( "no IJsonValue %#lx\n", hr ); return; }
    IJsonValue_get_ValueType( value, &type );
    hr = IJsonValue_Stringify( value, &str );
    printf( "type %d, stringify %#lx ", type, hr );
    if (SUCCEEDED(hr)) { put_hstring( str ); WindowsDeleteString( str ); }
    putchar( '\n' );
    IJsonValue_Release( value );
}

static IJsonValue *number( double d )
{
    IJsonValue *value = NULL;
    IJsonValueStatics_CreateNumberValue( value_statics, d, &value );
    return value;
}

static HRESULT set_number( IJsonObject *object, const WCHAR *name, double d )
{
    IJsonValue *value = number( d );
    HSTRING str = hs( name );
    HRESULT hr = IJsonObject_SetNamedValue( object, str, value );
    IJsonValue_Release( value );
    WindowsDeleteString( str );
    return hr;
}

static void keys_of_iterable( const char *label, IIterable_IKeyValuePair_HSTRING_IJsonValue *iterable )
{
    IIterator_IKeyValuePair_HSTRING_IJsonValue *iterator;
    IKeyValuePair_HSTRING_IJsonValue *pair;
    boolean has = FALSE;
    HSTRING key;
    HRESULT hr;

    printf( "%s:", label );
    hr = IIterable_IKeyValuePair_HSTRING_IJsonValue_First( iterable, &iterator );
    if (FAILED(hr)) { printf( " First %#lx\n", hr ); return; }
    IIterator_IKeyValuePair_HSTRING_IJsonValue_get_HasCurrent( iterator, &has );
    while (has)
    {
        hr = IIterator_IKeyValuePair_HSTRING_IJsonValue_get_Current( iterator, &pair );
        if (FAILED(hr)) { printf( " Current %#lx", hr ); break; }
        if (SUCCEEDED(IKeyValuePair_HSTRING_IJsonValue_get_Key( pair, &key )))
        {
            putchar( ' ' );
            put_hstring( key );
            WindowsDeleteString( key );
        }
        IKeyValuePair_HSTRING_IJsonValue_Release( pair );
        hr = IIterator_IKeyValuePair_HSTRING_IJsonValue_MoveNext( iterator, &has );
        if (FAILED(hr)) { printf( " MoveNext %#lx", hr ); break; }
    }
    putchar( '\n' );
    IIterator_IKeyValuePair_HSTRING_IJsonValue_Release( iterator );
}

static void keys( const char *label, IJsonObject *object )
{
    IIterable_IKeyValuePair_HSTRING_IJsonValue *iterable;
    IMap_HSTRING_IJsonValue *map;
    IMapView_HSTRING_IJsonValue *view;
    char view_label[128];

    if (SUCCEEDED(IJsonObject_QueryInterface( object, &IID_IIterable_IKeyValuePair_HSTRING_IJsonValue, (void **)&iterable )))
    {
        keys_of_iterable( label, iterable );
        IIterable_IKeyValuePair_HSTRING_IJsonValue_Release( iterable );
    }
    if (SUCCEEDED(IJsonObject_QueryInterface( object, &IID_IMap_HSTRING_IJsonValue, (void **)&map )))
    {
        if (SUCCEEDED(IMap_HSTRING_IJsonValue_GetView( map, &view )))
        {
            if (SUCCEEDED(IMapView_HSTRING_IJsonValue_QueryInterface( view, &IID_IIterable_IKeyValuePair_HSTRING_IJsonValue, (void **)&iterable )))
            {
                snprintf( view_label, sizeof(view_label), "%s (view)", label );
                keys_of_iterable( view_label, iterable );
                IIterable_IKeyValuePair_HSTRING_IJsonValue_Release( iterable );
            }
            IMapView_HSTRING_IJsonValue_Release( view );
        }
        IMap_HSTRING_IJsonValue_Release( map );
    }
}

static void order(void)
{
    static const WCHAR *texts[] =
    {
        L"{\"b\":1,\"a\":2,\"c\":3}",
        L"{\"k5\":0,\"k1\":0,\"k9\":0,\"k3\":0,\"k7\":0,\"k2\":0,\"k8\":0,\"k4\":0,\"k6\":0,\"k0\":0,\"B\":0,\"a\":0,\"\":0}",
        L"{\"a\":1,\"b\":2,\"a\":3}",
        L"{\"z\":{\"y\":1,\"x\":2},\"w\":[{\"v\":1,\"u\":2}]}",
    };
    IJsonObject *object;
    IMap_HSTRING_IJsonValue *map;
    IJsonValue *value;
    boolean replaced;
    HSTRING str;
    HRESULT hr;
    UINT i;

    printf( "== order\n" );
    for (i = 0; i < ARRAY_SIZE(texts); i++)
    {
        str = hs( texts[i] );
        object = NULL;
        hr = IJsonObjectStatics_Parse( object_statics, str, &object );
        WindowsDeleteString( str );
        printf( "parse " ); put_wide( texts[i], wcslen( texts[i] ) ); printf( ": %#lx\n", hr );
        if (FAILED(hr)) continue;
        show( "  as text", object );
        keys( "  iterated", object );
        IJsonObject_Release( object );
    }

    if (!(object = activate( L"Windows.Data.Json.JsonObject", &IID_IJsonObject ))) return;
    set_number( object, L"c", 1 );
    set_number( object, L"a", 2 );
    set_number( object, L"b", 3 );
    show( "set c a b", object );
    keys( "  iterated", object );
    set_number( object, L"a", 4 );
    show( "set a again", object );
    str = hs( L"c" );
    hr = IJsonObject_QueryInterface( object, &IID_IMap_HSTRING_IJsonValue, (void **)&map );
    if (SUCCEEDED(hr))
    {
        hr = IMap_HSTRING_IJsonValue_Remove( map, str );
        printf( "remove c: %#lx\n", hr );
        show( "  after", object );
        set_number( object, L"c", 5 );
        show( "set c after removing it", object );
        value = number( 6 );
        WindowsDeleteString( str );
        str = hs( L"d" );
        replaced = 2;
        hr = IMap_HSTRING_IJsonValue_Insert( map, str, value, &replaced );
        printf( "insert d: %#lx replaced %d\n", hr, replaced );
        IJsonValue_Release( value );
        value = number( 7 );
        WindowsDeleteString( str );
        str = hs( L"b" );
        replaced = 2;
        hr = IMap_HSTRING_IJsonValue_Insert( map, str, value, &replaced );
        printf( "insert b: %#lx replaced %d\n", hr, replaced );
        IJsonValue_Release( value );
        show( "  after", object );
        keys( "  iterated", object );
        IMap_HSTRING_IJsonValue_Release( map );
    }
    WindowsDeleteString( str );
    IJsonObject_Release( object );

    /* fifty names in a scrambled order: a hash table would not keep it */
    if (!(object = activate( L"Windows.Data.Json.JsonObject", &IID_IJsonObject ))) return;
    for (i = 0; i < 50; i++)
    {
        WCHAR name[8];
        swprintf( name, ARRAY_SIZE(name), L"n%02u", (i * 37) % 50 );
        set_number( object, name, i );
    }
    show( "fifty", object );
    IJsonObject_Release( object );
}

static void failed_parses(void)
{
    static const WCHAR *object_texts[] = { L"[1]", L"{\"a\":}", L"", L"  ", L"{\"a\":1} x", NULL };
    static const WCHAR *array_texts[] = { L"{}", L"[1,", L"", NULL };
    static const WCHAR *value_texts[] = { L"{\"a\":}", L"tru", L"", NULL };
    IJsonObject *object;
    IJsonArray *array;
    IJsonValue *value;
    boolean succeeded;
    HSTRING str;
    HRESULT hr;
    UINT i;

    printf( "== failed parses\n" );
    for (i = 0; i < ARRAY_SIZE(object_texts); i++)
    {
        str = hs( object_texts[i] );
        object = SENTINEL;
        hr = IJsonObjectStatics_Parse( object_statics, str, &object );
        printf( "JsonObject.Parse(%ls): %#lx out %s\n", object_texts[i] ? object_texts[i] : L"NULL", hr, out_state( object ) );
        if (object && object != SENTINEL) { show( "  out", object ); IJsonObject_Release( object ); }
        object = SENTINEL;
        succeeded = 2;
        hr = IJsonObjectStatics_TryParse( object_statics, str, &object, &succeeded );
        printf( "JsonObject.TryParse(%ls): %#lx succeeded %d out %s\n", object_texts[i] ? object_texts[i] : L"NULL", hr,
                succeeded, out_state( object ) );
        if (object && object != SENTINEL) { show( "  out", object ); IJsonObject_Release( object ); }
        WindowsDeleteString( str );
    }
    for (i = 0; i < ARRAY_SIZE(array_texts); i++)
    {
        str = hs( array_texts[i] );
        array = SENTINEL;
        hr = IJsonArrayStatics_Parse( array_statics, str, &array );
        printf( "JsonArray.Parse(%ls): %#lx out %s\n", array_texts[i] ? array_texts[i] : L"NULL", hr, out_state( array ) );
        if (array && array != SENTINEL) { show( "  out", array ); IJsonArray_Release( array ); }
        array = SENTINEL;
        succeeded = 2;
        hr = IJsonArrayStatics_TryParse( array_statics, str, &array, &succeeded );
        printf( "JsonArray.TryParse(%ls): %#lx succeeded %d out %s\n", array_texts[i] ? array_texts[i] : L"NULL", hr,
                succeeded, out_state( array ) );
        if (array && array != SENTINEL) { show( "  out", array ); IJsonArray_Release( array ); }
        WindowsDeleteString( str );
    }
    for (i = 0; i < ARRAY_SIZE(value_texts); i++)
    {
        str = hs( value_texts[i] );
        value = SENTINEL;
        hr = IJsonValueStatics_Parse( value_statics, str, &value );
        printf( "JsonValue.Parse(%ls): %#lx out %s\n", value_texts[i] ? value_texts[i] : L"NULL", hr, out_state( value ) );
        if (value && value != SENTINEL) { show( "  out", value ); IJsonValue_Release( value ); }
        value = SENTINEL;
        succeeded = 2;
        hr = IJsonValueStatics_TryParse( value_statics, str, &value, &succeeded );
        printf( "JsonValue.TryParse(%ls): %#lx succeeded %d out %s\n", value_texts[i] ? value_texts[i] : L"NULL", hr,
                succeeded, out_state( value ) );
        if (value && value != SENTINEL) { show( "  out", value ); IJsonValue_Release( value ); }
        WindowsDeleteString( str );
    }
}

static void errors(void)
{
    IIterable_IKeyValuePair_HSTRING_IJsonValue *iterable;
    IIterator_IKeyValuePair_HSTRING_IJsonValue *iterator;
    IKeyValuePair_HSTRING_IJsonValue *pair;
    IMapView_HSTRING_IJsonValue *view;
    IMap_HSTRING_IJsonValue *map;
    IVector_IJsonValue *vector;
    IJsonValue *value, *got;
    IJsonObject *object;
    IJsonArray *array;
    boolean has, flag;
    HSTRING a, x, s;
    UINT32 size;
    double d;
    HRESULT hr;

    printf( "== errors\n" );
    if (!(object = activate( L"Windows.Data.Json.JsonObject", &IID_IJsonObject ))) return;
    a = hs( L"a" );
    x = hs( L"x" );
    set_number( object, L"a", 1 );
    hr = IJsonObject_GetNamedString( object, a, &s );
    printf( "GetNamedString of a number: %#lx\n", hr );
    if (SUCCEEDED(hr)) WindowsDeleteString( s );
    hr = IJsonObject_GetNamedValue( object, x, &got );
    printf( "GetNamedValue of a missing name: %#lx\n", hr );
    if (SUCCEEDED(hr)) IJsonValue_Release( got );
    hr = IJsonObject_GetNamedNumber( object, x, &d );
    printf( "GetNamedNumber of a missing name: %#lx\n", hr );
    hr = IJsonObject_QueryInterface( object, &IID_IMap_HSTRING_IJsonValue, (void **)&map );
    if (SUCCEEDED(hr))
    {
        got = SENTINEL;
        hr = IMap_HSTRING_IJsonValue_Lookup( map, x, &got );
        printf( "IMap.Lookup of a missing name: %#lx out %s\n", hr, out_state( got ) );
        if (got && got != SENTINEL) IJsonValue_Release( got );
        hr = IMap_HSTRING_IJsonValue_Remove( map, x );
        printf( "IMap.Remove of a missing name: %#lx\n", hr );
        hr = IMap_HSTRING_IJsonValue_GetView( map, &view );
        if (SUCCEEDED(hr))
        {
            got = SENTINEL;
            hr = IMapView_HSTRING_IJsonValue_Lookup( view, x, &got );
            printf( "IMapView.Lookup of a missing name: %#lx out %s\n", hr, out_state( got ) );
            if (got && got != SENTINEL) IJsonValue_Release( got );
            /* does the view follow the object? */
            set_number( object, L"v", 9 );
            hr = IMapView_HSTRING_IJsonValue_get_Size( view, &size );
            printf( "view size after a later insertion: %#lx %u\n", hr, size );
            IMapView_HSTRING_IJsonValue_Release( view );
        }
        IMap_HSTRING_IJsonValue_Release( map );
    }
    hr = IJsonObject_QueryInterface( object, &IID_IIterable_IKeyValuePair_HSTRING_IJsonValue, (void **)&iterable );
    if (SUCCEEDED(hr))
    {
        hr = IIterable_IKeyValuePair_HSTRING_IJsonValue_First( iterable, &iterator );
        if (SUCCEEDED(hr))
        {
            set_number( object, L"w", 10 );
            has = 2;
            hr = IIterator_IKeyValuePair_HSTRING_IJsonValue_get_HasCurrent( iterator, &has );
            printf( "iterator after a change: HasCurrent %#lx %d", hr, has );
            hr = IIterator_IKeyValuePair_HSTRING_IJsonValue_get_Current( iterator, &pair );
            printf( ", Current %#lx", hr );
            if (SUCCEEDED(hr)) IKeyValuePair_HSTRING_IJsonValue_Release( pair );
            has = 2;
            hr = IIterator_IKeyValuePair_HSTRING_IJsonValue_MoveNext( iterator, &has );
            printf( ", MoveNext %#lx %d\n", hr, has );
            do hr = IIterator_IKeyValuePair_HSTRING_IJsonValue_MoveNext( iterator, &has );
            while (SUCCEEDED(hr) && has);
            hr = IIterator_IKeyValuePair_HSTRING_IJsonValue_get_Current( iterator, &pair );
            printf( "iterator past its end: Current %#lx", hr );
            if (SUCCEEDED(hr)) IKeyValuePair_HSTRING_IJsonValue_Release( pair );
            hr = IIterator_IKeyValuePair_HSTRING_IJsonValue_MoveNext( iterator, &has );
            printf( ", MoveNext %#lx %d\n", hr, has );
            IIterator_IKeyValuePair_HSTRING_IJsonValue_Release( iterator );
        }
        IIterable_IKeyValuePair_HSTRING_IJsonValue_Release( iterable );
    }
    hr = IJsonObject_QueryInterface( object, &IID_IJsonValue, (void **)&value );
    if (SUCCEEDED(hr))
    {
        hr = IJsonValue_GetString( value, &s );
        printf( "an object's GetString: %#lx\n", hr );
        if (SUCCEEDED(hr)) WindowsDeleteString( s );
        hr = IJsonValue_GetBoolean( value, &flag );
        printf( "an object's GetBoolean: %#lx\n", hr );
        hr = IJsonValue_GetArray( value, &array );
        printf( "an object's GetArray: %#lx\n", hr );
        if (SUCCEEDED(hr)) IJsonArray_Release( array );
        IJsonValue_Release( value );
    }
    IJsonObject_Release( object );

    if (!(array = activate( L"Windows.Data.Json.JsonArray", &IID_IJsonArray ))) goto done;
    hr = IJsonArray_QueryInterface( array, &IID_IVector_IJsonValue, (void **)&vector );
    if (SUCCEEDED(hr))
    {
        value = number( 1 );
        IVector_IJsonValue_Append( vector, value );
        IJsonValue_Release( value );
        got = SENTINEL;
        hr = IVector_IJsonValue_GetAt( vector, 5, &got );
        printf( "IVector.GetAt out of range: %#lx out %s\n", hr, out_state( got ) );
        if (got && got != SENTINEL) IJsonValue_Release( got );
        hr = IVector_IJsonValue_RemoveAt( vector, 5 );
        printf( "IVector.RemoveAt out of range: %#lx\n", hr );
        value = number( 2 );
        hr = IVector_IJsonValue_SetAt( vector, 5, value );
        printf( "IVector.SetAt out of range: %#lx\n", hr );
        hr = IVector_IJsonValue_InsertAt( vector, 5, value );
        printf( "IVector.InsertAt out of range: %#lx\n", hr );
        hr = IVector_IJsonValue_InsertAt( vector, 1, value );
        printf( "IVector.InsertAt at the end: %#lx\n", hr );
        IJsonValue_Release( value );
        hr = IJsonArray_GetStringAt( array, 0, &s );
        printf( "GetStringAt of a number: %#lx\n", hr );
        if (SUCCEEDED(hr)) WindowsDeleteString( s );
        hr = IJsonArray_GetNumberAt( array, 7, &d );
        printf( "GetNumberAt out of range: %#lx\n", hr );
        show( "array", array );
        IVector_IJsonValue_Release( vector );
    }
    hr = IJsonArray_QueryInterface( array, &IID_IJsonValue, (void **)&value );
    if (SUCCEEDED(hr))
    {
        hr = IJsonValue_GetBoolean( value, &flag );
        printf( "an array's GetBoolean: %#lx\n", hr );
        hr = IJsonValue_GetObject( value, &object );
        printf( "an array's GetObject: %#lx\n", hr );
        if (SUCCEEDED(hr)) IJsonObject_Release( object );
        IJsonValue_Release( value );
    }
    IJsonArray_Release( array );

    value = number( 3 );
    hr = IJsonValue_GetString( value, &s );
    printf( "a number's GetString: %#lx\n", hr );
    if (SUCCEEDED(hr)) WindowsDeleteString( s );
    hr = IJsonValue_GetObject( value, &object );
    printf( "a number's GetObject: %#lx\n", hr );
    if (SUCCEEDED(hr)) IJsonObject_Release( object );
    IJsonValue_Release( value );
done:
    WindowsDeleteString( a );
    WindowsDeleteString( x );
}

static void identity(void)
{
    IJsonObject *object, *inner, *got_object;
    IJsonValue *value, *got, *inner_value, *parsed, *again;
    IMap_HSTRING_IJsonValue *map;
    IVector_IJsonValue *vector;
    IJsonArray *array;
    HSTRING a, o, str;
    HRESULT hr;

    printf( "== identity\n" );
    if (!(object = activate( L"Windows.Data.Json.JsonObject", &IID_IJsonObject ))) return;
    a = hs( L"a" );
    o = hs( L"o" );
    value = number( 1 );
    IJsonObject_SetNamedValue( object, a, value );
    hr = IJsonObject_GetNamedValue( object, a, &got );
    printf( "GetNamedValue gives the value set: %#lx %s\n", hr, SUCCEEDED(hr) && got == value ? "same" : "different" );
    if (SUCCEEDED(hr)) IJsonValue_Release( got );
    if (SUCCEEDED(IJsonObject_QueryInterface( object, &IID_IMap_HSTRING_IJsonValue, (void **)&map )))
    {
        hr = IMap_HSTRING_IJsonValue_Lookup( map, a, &got );
        printf( "IMap.Lookup gives the value set: %#lx %s\n", hr, SUCCEEDED(hr) && got == value ? "same" : "different" );
        if (SUCCEEDED(hr)) IJsonValue_Release( got );
        IMap_HSTRING_IJsonValue_Release( map );
    }
    IJsonValue_Release( value );

    if ((inner = activate( L"Windows.Data.Json.JsonObject", &IID_IJsonObject )))
    {
        IJsonObject_QueryInterface( inner, &IID_IJsonValue, (void **)&inner_value );
        IJsonObject_SetNamedValue( object, o, inner_value );
        hr = IJsonObject_GetNamedObject( object, o, &got_object );
        printf( "GetNamedObject gives the object set: %#lx %s\n", hr, SUCCEEDED(hr) && got_object == inner ? "same" : "different" );
        if (SUCCEEDED(hr)) IJsonObject_Release( got_object );
        hr = IJsonObject_GetNamedValue( object, o, &got );
        printf( "GetNamedValue gives the object's IJsonValue: %#lx %s\n", hr, SUCCEEDED(hr) && got == inner_value ? "same" : "different" );
        if (SUCCEEDED(hr)) IJsonValue_Release( got );
        /* a change through the object shows in its parent */
        set_number( inner, L"later", 2 );
        show( "parent after a change to the child", object );
        IJsonValue_Release( inner_value );
        IJsonObject_Release( inner );
    }
    IJsonObject_Release( object );

    str = hs( L"{\"a\":[1]}" );
    hr = IJsonValueStatics_Parse( value_statics, str, &parsed );
    WindowsDeleteString( str );
    if (SUCCEEDED(hr))
    {
        hr = IJsonValue_GetObject( parsed, &object );
        printf( "a parsed value's GetObject: %#lx\n", hr );
        if (SUCCEEDED(hr))
        {
            hr = IJsonObject_QueryInterface( object, &IID_IJsonValue, (void **)&again );
            printf( "  its object's IJsonValue is the parsed value: %s\n", SUCCEEDED(hr) && again == parsed ? "same" : "different" );
            if (SUCCEEDED(hr)) IJsonValue_Release( again );
            hr = IJsonValue_GetObject( parsed, &got_object );
            printf( "  GetObject twice gives one object: %s\n", SUCCEEDED(hr) && got_object == object ? "same" : "different" );
            if (SUCCEEDED(hr)) IJsonObject_Release( got_object );
            hr = IJsonObject_GetNamedArray( object, a, &array );
            if (SUCCEEDED(hr))
            {
                IJsonArray *array2;
                hr = IJsonObject_GetNamedArray( object, a, &array2 );
                printf( "  GetNamedArray twice gives one array: %s\n", SUCCEEDED(hr) && array2 == array ? "same" : "different" );
                if (SUCCEEDED(hr)) IJsonArray_Release( array2 );
                IJsonArray_Release( array );
            }
            IJsonObject_Release( object );
        }
        IJsonValue_Release( parsed );
    }

    if ((array = activate( L"Windows.Data.Json.JsonArray", &IID_IJsonArray )))
    {
        if (SUCCEEDED(IJsonArray_QueryInterface( array, &IID_IVector_IJsonValue, (void **)&vector )))
        {
            value = number( 1 );
            IVector_IJsonValue_Append( vector, value );
            hr = IVector_IJsonValue_GetAt( vector, 0, &got );
            printf( "IVector.GetAt gives the value appended: %#lx %s\n", hr, SUCCEEDED(hr) && got == value ? "same" : "different" );
            if (SUCCEEDED(hr)) IJsonValue_Release( got );
            IJsonValue_Release( value );
            IVector_IJsonValue_Release( vector );
        }
        IJsonArray_Release( array );
    }
    WindowsDeleteString( a );
    WindowsDeleteString( o );
}

static void formatting(void)
{
    static const double numbers[] =
    {
        0, -0.0, 1, -1.5, 0.1, 3.14159, 1e21, 1e20, 123456789012345680000.0, 1e-6, 1e-7, 1.5e300,
        9007199254740993.0, 5e-324, 1.7976931348623157e308, 0.000001234, 100, 1e100, -1e-100,
    };
    static const WCHAR *strings[] =
    {
        L"a\"b\\c/\b\f\n\r\t\x01\x1f\x7f\x00e9\x2028\x2029",
        L"\xd800 lone, \xdc00 lone, \xd83d\xde00 pair",
        L"<>&'",
    };
    static const WCHAR *parses[] =
    {
        L"1E2", L"-0", L"0.5e-3", L"1e400", L"-1e400", L"01", L"1.", L".5", L"-", L"+1", L"1e", L"0x10",
        L"\"\\u0041\\/\\uD83D\\uDE00\"", L"\"\\uD800\"", L"\"\\x\"", L"\"a\x01b\"", L"\"a\tb\"", L"\"\\u00\"",
        L" true ", L"null", L"nul", L"[1,]", L"{,}", L"{\"a\":1,}", L"1 2", L"\xfeff{}", L"{\"a\" 1}",
        L"[1]\n", L"\t{}\r\n", L"{\"a\":1}\x00a0",
    };
    IJsonValue *value;
    HSTRING str;
    HRESULT hr;
    UINT i;

    printf( "== formatting\n" );
    for (i = 0; i < ARRAY_SIZE(numbers); i++)
    {
        char label[64];
        hr = IJsonValueStatics_CreateNumberValue( value_statics, numbers[i], &value );
        snprintf( label, sizeof(label), "number %.17g (%#lx)", numbers[i], hr );
        if (SUCCEEDED(hr)) { show( label, value ); IJsonValue_Release( value ); }
        else printf( "%s\n", label );
    }
    {
        const double values[] = { NAN, INFINITY, -INFINITY };
        for (i = 0; i < ARRAY_SIZE(values); i++)
        {
            char label[64];
            hr = IJsonValueStatics_CreateNumberValue( value_statics, values[i], &value );
            snprintf( label, sizeof(label), "number %g (%#lx)", values[i], hr );
            if (SUCCEEDED(hr)) { show( label, value ); IJsonValue_Release( value ); }
            else printf( "%s\n", label );
        }
    }
    for (i = 0; i < ARRAY_SIZE(strings); i++)
    {
        str = hs( strings[i] );
        hr = IJsonValueStatics_CreateStringValue( value_statics, str, &value );
        printf( "string " ); put_wide( strings[i], wcslen( strings[i] ) ); printf( " (%#lx)", hr );
        if (SUCCEEDED(hr)) { show( "", value ); IJsonValue_Release( value ); }
        else putchar( '\n' );
        WindowsDeleteString( str );
    }
    WindowsCreateString( L"a\0b", 3, &str );
    hr = IJsonValueStatics_CreateStringValue( value_statics, str, &value );
    printf( "string a<0000>b (%#lx)", hr );
    if (SUCCEEDED(hr)) { show( "", value ); IJsonValue_Release( value ); }
    else putchar( '\n' );
    WindowsDeleteString( str );
    hr = IJsonValueStatics_CreateStringValue( value_statics, NULL, &value );
    printf( "string NULL (%#lx)", hr );
    if (SUCCEEDED(hr)) { show( "", value ); IJsonValue_Release( value ); }
    else putchar( '\n' );

    for (i = 0; i < ARRAY_SIZE(parses); i++)
    {
        str = hs( parses[i] );
        value = SENTINEL;
        hr = IJsonValueStatics_Parse( value_statics, str, &value );
        printf( "parse " ); put_wide( parses[i], wcslen( parses[i] ) ); printf( ": %#lx", hr );
        if (SUCCEEDED(hr) && value && value != SENTINEL)
        {
            JsonValueType type = -1;
            HSTRING text;
            double d;
            IJsonValue_get_ValueType( value, &type );
            if (type == JsonValueType_Number && SUCCEEDED(IJsonValue_GetNumber( value, &d ))) printf( " number %.17g", d );
            if (type == JsonValueType_String && SUCCEEDED(IJsonValue_GetString( value, &text )))
            {
                printf( " string " );
                put_hstring( text );
                WindowsDeleteString( text );
            }
            show( "", value );
            IJsonValue_Release( value );
        }
        else printf( " out %s\n", out_state( value ) );
        WindowsDeleteString( str );
    }
}

static WCHAR *nested_arrays( UINT depth )
{
    WCHAR *json = malloc( (2 * depth + 1) * sizeof(WCHAR) ), *p = json;
    UINT i;

    for (i = 0; i < depth; i++) *p++ = '[';
    for (i = 0; i < depth; i++) *p++ = ']';
    *p = 0;
    return json;
}

static void limits(void)
{
    static const UINT depths[] = { 512, 1023, 1024, 1025, 2000 };
    JsonErrorStatus status;
    IJsonArray *array, *child;
    IJsonValue *value;
    HSTRING str;
    HRESULT hr;
    UINT i;

    printf( "== limits\n" );
    for (i = 0; i < ARRAY_SIZE(depths); i++)
    {
        WCHAR *json = nested_arrays( depths[i] );
        str = hs( json );
        hr = IJsonValueStatics_Parse( value_statics, str, &value );
        status = -1;
        if (error_statics) IJsonErrorStatics2_GetJsonStatus( error_statics, hr, &status );
        printf( "arrays %u deep: %#lx status %d\n", depths[i], hr, status );
        if (SUCCEEDED(hr)) IJsonValue_Release( value );
        WindowsDeleteString( str );
        free( json );
    }
    for (i = 0; i < ARRAY_SIZE(depths); i++)
    {
        WCHAR *json = malloc( (depths[i] * 6 + 8) * sizeof(WCHAR) ), *p = json;
        UINT j;
        for (j = 0; j < depths[i]; j++) { memcpy( p, L"{\"a\":", 5 * sizeof(WCHAR) ); p += 5; }
        *p++ = '1';
        for (j = 0; j < depths[i]; j++) *p++ = '}';
        *p = 0;
        str = hs( json );
        hr = IJsonValueStatics_Parse( value_statics, str, &value );
        status = -1;
        if (error_statics) IJsonErrorStatics2_GetJsonStatus( error_statics, hr, &status );
        printf( "objects %u deep: %#lx status %d\n", depths[i], hr, status );
        if (SUCCEEDED(hr)) IJsonValue_Release( value );
        WindowsDeleteString( str );
        free( json );
    }

    /* the same depths built through the API, then written out */
    for (i = 0; i < ARRAY_SIZE(depths); i++)
    {
        IJsonArray *top = activate( L"Windows.Data.Json.JsonArray", &IID_IJsonArray );
        IVector_IJsonValue *vector;
        HSTRING text = NULL;
        UINT j;

        if (!top) return;
        array = top;
        IJsonArray_AddRef( array );
        for (j = 1; j < depths[i]; j++)
        {
            child = activate( L"Windows.Data.Json.JsonArray", &IID_IJsonArray );
            IJsonArray_QueryInterface( array, &IID_IVector_IJsonValue, (void **)&vector );
            IJsonArray_QueryInterface( child, &IID_IJsonValue, (void **)&value );
            IVector_IJsonValue_Append( vector, value );
            IJsonValue_Release( value );
            IVector_IJsonValue_Release( vector );
            IJsonArray_Release( array );
            array = child;
        }
        IJsonArray_Release( array );
        IJsonArray_QueryInterface( top, &IID_IJsonValue, (void **)&value );
        hr = IJsonValue_Stringify( value, &text );
        status = -1;
        if (error_statics) IJsonErrorStatics2_GetJsonStatus( error_statics, hr, &status );
        printf( "built %u deep, stringify: %#lx status %d length %u\n", depths[i], hr, status,
                SUCCEEDED(hr) ? WindowsGetStringLen( text ) : 0 );
        if (SUCCEEDED(hr)) WindowsDeleteString( text );
        IJsonValue_Release( value );
        IJsonArray_Release( top );
    }
}

static void statuses(void)
{
    static const HRESULT codes[] =
    {
        S_OK, E_FAIL, E_INVALIDARG, E_POINTER, E_BOUNDS, E_ILLEGAL_METHOD_CALL, WEB_E_INVALID_JSON_STRING,
        WEB_E_INVALID_JSON_NUMBER, WEB_E_JSON_VALUE_NOT_FOUND, __HRESULT_FROM_WIN32( ERROR_IMPLEMENTATION_LIMIT ),
        0x83750007, 0x83750008, 0x80070057,
    };
    JsonErrorStatus status;
    HRESULT hr;
    UINT i;

    printf( "== statuses\n" );
    if (!error_statics) return;
    for (i = 0; i < ARRAY_SIZE(codes); i++)
    {
        status = -1;
        hr = IJsonErrorStatics2_GetJsonStatus( error_statics, codes[i], &status );
        printf( "GetJsonStatus(%#lx): %#lx status %d\n", codes[i], hr, status );
    }
}

static HRESULT parse_text( const WCHAR *json )
{
    IJsonValue *value = NULL;
    HSTRING str = hs( json );
    HRESULT hr = IJsonValueStatics_Parse( value_statics, str, &value );

    if (SUCCEEDED(hr) && value) IJsonValue_Release( value );
    WindowsDeleteString( str );
    return hr;
}

/* nesting of the given depth: "[[...]]", "{"a":{"a":...1}}", or arrays and objects in turn */
static WCHAR *nesting( UINT kind, UINT depth )
{
    WCHAR *json = malloc( (depth * 6 + 8) * sizeof(WCHAR) ), *p = json;
    UINT i;

    for (i = 0; i < depth; i++)
    {
        if (kind == 0 || (kind == 2 && !(i & 1))) *p++ = '[';
        else { memcpy( p, L"{\"a\":", 5 * sizeof(WCHAR) ); p += 5; }
    }
    *p++ = '1';
    for (i = depth; i--;) *p++ = (kind == 0 || (kind == 2 && !(i & 1))) ? ']' : '}';
    *p = 0;
    return json;
}

static void more(void)
{
    static const char *kinds[] = { "arrays", "objects", "arrays and objects" };
    static const double numbers[] =
    {
        0.0001, 0.00012, 0.00001, 0.000015, 1e14, 1e15, 1e16, 123456789012345.0, 1234567890123456.0,
        0.1 + 0.2, 1 / 3.0, 2 / 3.0, 100 / 3.0, 1e-300, 1.23e-310, 2.5, -2.5e-5, 1e21 + 1e5,
    };
    static const ULONGLONG nans[] =
    {
        0x7ff8000000000000ull, 0xfff8000000000000ull, 0x7ff0000000000001ull, 0xfff8000000000001ull,
        0x7ff8000000000001ull, 0x7ff4000000000000ull,
    };
    static const WCHAR *parses[] =
    {
        L"1e-400", L"-1e-400", L"1E400", L"1e-324", L"2.4703282292062328e-324", L"123456789012345678901234567890",
        L"0.0000000000000000000001", L"1e+2", L"1E-2", L"-0.0", L"0e5", L"[-]", L"[1e]", L"[1.5e+]",
    };
    IIterator_IKeyValuePair_HSTRING_IJsonValue *iterator;
    IIterable_IKeyValuePair_HSTRING_IJsonValue *iterable;
    IKeyValuePair_HSTRING_IJsonValue *pair;
    IMapView_HSTRING_IJsonValue *map_view;
    IMap_HSTRING_IJsonValue *map;
    IVectorView_IJsonValue *vector_view;
    IIterable_IJsonValue *values;
    IIterator_IJsonValue *value_iterator;
    IVector_IJsonValue *vector;
    IJsonObject *object;
    IJsonArray *array;
    IJsonValue *value;
    boolean has;
    UINT32 size;
    HSTRING key;
    HRESULT hr;
    UINT i, kind;

    printf( "== more\n" );
    for (kind = 0; kind < ARRAY_SIZE(kinds); kind++)
    {
        UINT low = 1, high = 4096;  /* low parses, high does not */
        WCHAR *json;

        json = nesting( kind, low );
        hr = parse_text( json );
        free( json );
        if (FAILED(hr)) { printf( "%s 1 deep: %#lx\n", kinds[kind], hr ); continue; }
        while (high - low > 1)
        {
            UINT mid = (low + high) / 2;
            json = nesting( kind, mid );
            if (SUCCEEDED(parse_text( json ))) low = mid;
            else high = mid;
            free( json );
        }
        json = nesting( kind, high );
        hr = parse_text( json );
        free( json );
        printf( "%s: %u deep parses, %u deep gives %#lx\n", kinds[kind], low, high, hr );
    }

    for (i = 0; i < ARRAY_SIZE(numbers); i++)
    {
        char label[64];
        hr = IJsonValueStatics_CreateNumberValue( value_statics, numbers[i], &value );
        snprintf( label, sizeof(label), "number %.17g (%#lx)", numbers[i], hr );
        if (SUCCEEDED(hr)) { show( label, value ); IJsonValue_Release( value ); }
        else printf( "%s\n", label );
    }
    for (i = 0; i < ARRAY_SIZE(nans); i++)
    {
        char label[64];
        double d;
        memcpy( &d, &nans[i], sizeof(d) );
        hr = IJsonValueStatics_CreateNumberValue( value_statics, d, &value );
        snprintf( label, sizeof(label), "number bits %#llx (%#lx)", nans[i], hr );
        if (SUCCEEDED(hr)) { show( label, value ); IJsonValue_Release( value ); }
        else printf( "%s\n", label );
    }
    for (i = 0; i < ARRAY_SIZE(parses); i++)
    {
        HSTRING str = hs( parses[i] );
        value = SENTINEL;
        hr = IJsonValueStatics_Parse( value_statics, str, &value );
        printf( "parse " ); put_wide( parses[i], wcslen( parses[i] ) ); printf( ": %#lx", hr );
        if (SUCCEEDED(hr) && value && value != SENTINEL)
        {
            double d;
            if (SUCCEEDED(IJsonValue_GetNumber( value, &d ))) printf( " number %.17g", d );
            show( "", value );
            IJsonValue_Release( value );
        }
        else printf( " out %s\n", out_state( value ) );
        WindowsDeleteString( str );
    }

    /* which changes invalidate an object's iterators and views */
    if (!(object = activate( L"Windows.Data.Json.JsonObject", &IID_IJsonObject ))) return;
    set_number( object, L"a", 1 );
    set_number( object, L"b", 2 );
    IJsonObject_QueryInterface( object, &IID_IIterable_IKeyValuePair_HSTRING_IJsonValue, (void **)&iterable );
    IJsonObject_QueryInterface( object, &IID_IMap_HSTRING_IJsonValue, (void **)&map );
    if (SUCCEEDED(IIterable_IKeyValuePair_HSTRING_IJsonValue_First( iterable, &iterator )))
    {
        has = 2;
        do hr = IIterator_IKeyValuePair_HSTRING_IJsonValue_MoveNext( iterator, &has );
        while (SUCCEEDED(hr) && has);
        printf( "untouched iterator run to its end: MoveNext %#lx %d", hr, has );
        hr = IIterator_IKeyValuePair_HSTRING_IJsonValue_get_Current( iterator, &pair );
        printf( ", Current %#lx", hr );
        if (SUCCEEDED(hr)) IKeyValuePair_HSTRING_IJsonValue_Release( pair );
        has = 2;
        hr = IIterator_IKeyValuePair_HSTRING_IJsonValue_MoveNext( iterator, &has );
        printf( ", MoveNext again %#lx %d\n", hr, has );
        IIterator_IKeyValuePair_HSTRING_IJsonValue_Release( iterator );
    }
    {
        static const char *changes[] = { "replacing a value", "setting the same value", "removing a missing name",
                                         "removing a name", "clearing" };
        for (i = 0; i < ARRAY_SIZE(changes); i++)
        {
            set_number( object, L"a", 1 );
            set_number( object, L"b", 2 );
            if (FAILED(IIterable_IKeyValuePair_HSTRING_IJsonValue_First( iterable, &iterator ))) continue;
            if (FAILED(IMap_HSTRING_IJsonValue_GetView( map, &map_view ))) { IIterator_IKeyValuePair_HSTRING_IJsonValue_Release( iterator ); continue; }
            switch (i)
            {
            case 0: set_number( object, L"a", 5 ); break;
            case 1:
                key = hs( L"a" );
                if (SUCCEEDED(IJsonObject_GetNamedValue( object, key, &value )))
                {
                    IJsonObject_SetNamedValue( object, key, value );
                    IJsonValue_Release( value );
                }
                WindowsDeleteString( key );
                break;
            case 2: key = hs( L"zz" ); IMap_HSTRING_IJsonValue_Remove( map, key ); WindowsDeleteString( key ); break;
            case 3: key = hs( L"b" ); IMap_HSTRING_IJsonValue_Remove( map, key ); WindowsDeleteString( key ); break;
            case 4: IMap_HSTRING_IJsonValue_Clear( map ); break;
            }
            hr = IIterator_IKeyValuePair_HSTRING_IJsonValue_get_Current( iterator, &pair );
            printf( "after %s: iterator Current %#lx", changes[i], hr );
            if (SUCCEEDED(hr)) IKeyValuePair_HSTRING_IJsonValue_Release( pair );
            hr = IMapView_HSTRING_IJsonValue_get_Size( map_view, &size );
            printf( ", view Size %#lx\n", hr );
            IMapView_HSTRING_IJsonValue_Release( map_view );
            IIterator_IKeyValuePair_HSTRING_IJsonValue_Release( iterator );
        }
    }
    IMap_HSTRING_IJsonValue_Release( map );
    IIterable_IKeyValuePair_HSTRING_IJsonValue_Release( iterable );
    IJsonObject_Release( object );

    /* and an array's */
    if (!(array = activate( L"Windows.Data.Json.JsonArray", &IID_IJsonArray ))) return;
    IJsonArray_QueryInterface( array, &IID_IVector_IJsonValue, (void **)&vector );
    IJsonArray_QueryInterface( array, &IID_IIterable_IJsonValue, (void **)&values );
    value = number( 1 );
    IVector_IJsonValue_Append( vector, value );
    IVector_IJsonValue_Append( vector, value );
    if (SUCCEEDED(IIterable_IJsonValue_First( values, &value_iterator )))
    {
        IJsonValue *item;
        has = 2;
        do hr = IIterator_IJsonValue_MoveNext( value_iterator, &has );
        while (SUCCEEDED(hr) && has);
        printf( "untouched array iterator run to its end: MoveNext %#lx %d", hr, has );
        hr = IIterator_IJsonValue_get_Current( value_iterator, &item );
        printf( ", Current %#lx\n", hr );
        if (SUCCEEDED(hr)) IJsonValue_Release( item );
        IIterator_IJsonValue_Release( value_iterator );
    }
    {
        static const char *changes[] = { "appending", "setting an item", "removing an item", "clearing" };
        for (i = 0; i < ARRAY_SIZE(changes); i++)
        {
            IJsonValue *item;
            while (SUCCEEDED(IVector_IJsonValue_get_Size( vector, &size )) && size < 2) IVector_IJsonValue_Append( vector, value );
            if (FAILED(IIterable_IJsonValue_First( values, &value_iterator ))) continue;
            if (FAILED(IVector_IJsonValue_GetView( vector, &vector_view ))) { IIterator_IJsonValue_Release( value_iterator ); continue; }
            switch (i)
            {
            case 0: IVector_IJsonValue_Append( vector, value ); break;
            case 1: IVector_IJsonValue_SetAt( vector, 0, value ); break;
            case 2: IVector_IJsonValue_RemoveAt( vector, 0 ); break;
            case 3: IVector_IJsonValue_Clear( vector ); break;
            }
            hr = IIterator_IJsonValue_get_Current( value_iterator, &item );
            printf( "after %s: array iterator Current %#lx", changes[i], hr );
            if (SUCCEEDED(hr)) IJsonValue_Release( item );
            hr = IVectorView_IJsonValue_get_Size( vector_view, &size );
            printf( ", view Size %#lx %u\n", hr, SUCCEEDED(hr) ? size : 0 );
            IVectorView_IJsonValue_Release( vector_view );
            IIterator_IJsonValue_Release( value_iterator );
        }
    }
    IJsonValue_Release( value );
    IIterable_IJsonValue_Release( values );
    IVector_IJsonValue_Release( vector );
    IJsonArray_Release( array );
}

/* what IMapView.Split makes of views of a few sizes */
static void split(void)
{
    static const UINT sizes[] = { 0, 1, 2, 3, 4, 8, 9, 10, 12, 16, 17, 24, 32, 33, 50 };
    IMapView_HSTRING_IJsonValue *view, *first, *second;
    IMap_HSTRING_IJsonValue *map;
    IJsonObject *object;
    UINT32 first_size, second_size;
    HRESULT hr;
    UINT i, j;

    printf( "== split\n" );
    for (i = 0; i < ARRAY_SIZE(sizes); i++)
    {
        if (!(object = activate( L"Windows.Data.Json.JsonObject", &IID_IJsonObject ))) return;
        for (j = 0; j < sizes[i]; j++)
        {
            WCHAR name[8];
            swprintf( name, ARRAY_SIZE(name), L"m%02u", j );
            set_number( object, name, j );
        }
        IJsonObject_QueryInterface( object, &IID_IMap_HSTRING_IJsonValue, (void **)&map );
        if (SUCCEEDED(IMap_HSTRING_IJsonValue_GetView( map, &view )))
        {
            first = second = SENTINEL;
            hr = IMapView_HSTRING_IJsonValue_Split( view, &first, &second );
            first_size = second_size = ~0u;
            if (SUCCEEDED(hr) && first && first != SENTINEL) IMapView_HSTRING_IJsonValue_get_Size( first, &first_size );
            if (SUCCEEDED(hr) && second && second != SENTINEL) IMapView_HSTRING_IJsonValue_get_Size( second, &second_size );
            printf( "split of %u: %#lx first %s %d second %s %d\n", sizes[i], hr, out_state( first ), (int)first_size,
                    out_state( second ), (int)second_size );
            if (SUCCEEDED(hr) && first && first != SENTINEL) IMapView_HSTRING_IJsonValue_Release( first );
            if (SUCCEEDED(hr) && second && second != SENTINEL) IMapView_HSTRING_IJsonValue_Release( second );
            IMapView_HSTRING_IJsonValue_Release( view );
        }
        IMap_HSTRING_IJsonValue_Release( map );
        IJsonObject_Release( object );
    }
}

int main(void)
{
    RoInitialize( RO_INIT_MULTITHREADED );
    object_statics = get_statics( L"Windows.Data.Json.JsonObject", &IID_IJsonObjectStatics );
    array_statics = get_statics( L"Windows.Data.Json.JsonArray", &IID_IJsonArrayStatics );
    value_statics = get_statics( L"Windows.Data.Json.JsonValue", &IID_IJsonValueStatics );
    value_statics2 = get_statics( L"Windows.Data.Json.JsonValue", &IID_IJsonValueStatics2 );
    error_statics = get_statics( L"Windows.Data.Json.JsonError", &IID_IJsonErrorStatics2 );
    if (!object_statics || !array_statics || !value_statics) return 1;

    order();
    failed_parses();
    errors();
    identity();
    formatting();
    limits();
    statuses();
    more();
    split();

    fflush( stdout );
    return 0;
}
