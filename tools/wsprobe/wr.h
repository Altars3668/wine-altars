/* wr.h: helpers shared by the Windows.Web.Http probes.  Each probe prints lines meant to be
 * diffed between Windows and Wine, so everything printed is a value, an HRESULT or a name. */
#define COBJMACROS
#define WINDOWS_FOUNDATION_UNIVERSALAPICONTRACT_VERSION 0x130000
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "windef.h"
#include "initguid.h"
#include "winbase.h"
#include "winstring.h"
#include "roapi.h"
#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_Storage_Streams
#include "windows.storage.streams.h"
#define WIDL_using_Windows_Networking
#include "windows.networking.h"
#define WIDL_using_Windows_Globalization
#include "windows.globalization.h"
#define WIDL_using_Windows_Web_Http
#define WIDL_using_Windows_Web_Http_Headers
#define WIDL_using_Windows_Web_Http_Filters
#include "windows.web.http.h"
#include "robuffer.h"

static HSTRING S(const WCHAR *s)
{
    HSTRING h = NULL;
    if (s) WindowsCreateString(s, wcslen(s), &h);
    return h;
}

/* A printable copy of an HSTRING: control characters escaped, NULL told apart from "". */
static const char *P(HSTRING h)
{
    static char bufs[8][2048];
    static int next;
    char *buf = bufs[next++ % 8], *p = buf;
    const WCHAR *s;
    UINT32 len, i;

    if (!h) return "(null)";
    s = WindowsGetStringRawBuffer(h, &len);
    *p++ = '"';
    for (i = 0; i < len && p < buf + sizeof(bufs[0]) - 12; i++)
    {
        if (s[i] == '\r') p += sprintf(p, "\\r");
        else if (s[i] == '\n') p += sprintf(p, "\\n");
        else if (s[i] < 0x20 || s[i] >= 0x7f) p += sprintf(p, "\\u%04x", s[i]);
        else *p++ = (char)s[i];
    }
    *p++ = '"';
    *p = 0;
    return buf;
}

static void *get_factory(const WCHAR *name, REFIID iid)
{
    IActivationFactory *factory = NULL;
    void *out = NULL;
    HSTRING str = S(name);
    HRESULT hr = RoGetActivationFactory(str, &IID_IActivationFactory, (void **)&factory);
    WindowsDeleteString(str);
    if (FAILED(hr)) { printf("factory %ls: %#lx\n", name, hr); return NULL; }
    hr = IActivationFactory_QueryInterface(factory, iid, &out);
    IActivationFactory_Release(factory);
    if (FAILED(hr)) { printf("factory %ls: QI %#lx\n", name, hr); return NULL; }
    return out;
}

static void *activate(const WCHAR *name, REFIID iid)
{
    IActivationFactory *factory = get_factory(name, &IID_IActivationFactory);
    IInspectable *inspectable = NULL;
    void *out = NULL;
    HRESULT hr;
    if (!factory) return NULL;
    hr = IActivationFactory_ActivateInstance(factory, &inspectable);
    IActivationFactory_Release(factory);
    if (FAILED(hr)) { printf("activate %ls: %#lx\n", name, hr); return NULL; }
    hr = IInspectable_QueryInterface(inspectable, iid, &out);
    IInspectable_Release(inspectable);
    if (FAILED(hr)) { printf("activate %ls: QI %#lx\n", name, hr); return NULL; }
    return out;
}

static void print_class(const char *what, void *obj)
{
    IInspectable *inspectable;
    HSTRING name = NULL;
    HRESULT hr;
    if (!obj) { printf("%s: (null object)\n", what); return; }
    IUnknown_QueryInterface((IUnknown *)obj, &IID_IInspectable, (void **)&inspectable);
    hr = IInspectable_GetRuntimeClassName(inspectable, &name);
    printf("%s: class %#lx %s\n", what, hr, P(name));
    WindowsDeleteString(name);
    IInspectable_Release(inspectable);
}

static void print_string(const char *what, void *obj)
{
    IStringable *stringable;
    HSTRING str = NULL;
    HRESULT hr;
    if (!obj) { printf("%s: (null object)\n", what); return; }
    hr = IUnknown_QueryInterface((IUnknown *)obj, &IID_IStringable, (void **)&stringable);
    if (FAILED(hr)) { printf("%s: IStringable %#lx\n", what, hr); return; }
    hr = IStringable_ToString(stringable, &str);
    printf("%s: ToString %#lx %s\n", what, hr, P(str));
    WindowsDeleteString(str);
    IStringable_Release(stringable);
}

static void print_qi(const char *what, void *obj, REFIID iid, const char *iid_name)
{
    IUnknown *unk = NULL;
    HRESULT hr = IUnknown_QueryInterface((IUnknown *)obj, iid, (void **)&unk);
    printf("%s: QI %s %#lx\n", what, iid_name, hr);
    if (unk) IUnknown_Release(unk);
}
#define QI(what, obj, iface) print_qi(what, obj, &IID_##iface, #iface)

/* Every name/value pair a string map gives when iterated, in the order it gives them. */
static void dump_map(const char *what, void *obj)
{
    IIterable_IKeyValuePair_HSTRING_HSTRING *iterable;
    IIterator_IKeyValuePair_HSTRING_HSTRING *iterator;
    IKeyValuePair_HSTRING_HSTRING *pair;
    IMap_HSTRING_HSTRING *map;
    boolean has = FALSE;
    UINT32 size = 0;
    HRESULT hr;

    if (!obj) { printf("%s: (null object)\n", what); return; }
    if (SUCCEEDED(IUnknown_QueryInterface((IUnknown *)obj, &IID_IMap_HSTRING_HSTRING, (void **)&map)))
    {
        hr = IMap_HSTRING_HSTRING_get_Size(map, &size);
        IMap_HSTRING_HSTRING_Release(map);
        printf("%s: size %#lx %u\n", what, hr, size);
    }
    hr = IUnknown_QueryInterface((IUnknown *)obj, &IID_IIterable_IKeyValuePair_HSTRING_HSTRING, (void **)&iterable);
    if (FAILED(hr)) { printf("%s: no IIterable %#lx\n", what, hr); return; }
    hr = IIterable_IKeyValuePair_HSTRING_HSTRING_First(iterable, &iterator);
    IIterable_IKeyValuePair_HSTRING_HSTRING_Release(iterable);
    if (FAILED(hr)) { printf("%s: First %#lx\n", what, hr); return; }
    IIterator_IKeyValuePair_HSTRING_HSTRING_get_HasCurrent(iterator, &has);
    while (has)
    {
        HSTRING key = NULL, value = NULL;
        if (FAILED(hr = IIterator_IKeyValuePair_HSTRING_HSTRING_get_Current(iterator, &pair)))
        {
            printf("  Current %#lx\n", hr);
            break;
        }
        IKeyValuePair_HSTRING_HSTRING_get_Key(pair, &key);
        IKeyValuePair_HSTRING_HSTRING_get_Value(pair, &value);
        printf("  %s = %s\n", P(key), P(value));
        WindowsDeleteString(key);
        WindowsDeleteString(value);
        IKeyValuePair_HSTRING_HSTRING_Release(pair);
        if (FAILED(hr = IIterator_IKeyValuePair_HSTRING_HSTRING_MoveNext(iterator, &has))) { printf("  MoveNext %#lx\n", hr); break; }
    }
    IIterator_IKeyValuePair_HSTRING_HSTRING_Release(iterator);
}

static void lookup(const char *what, void *obj, const WCHAR *name)
{
    IMap_HSTRING_HSTRING *map;
    HSTRING key = S(name), value = NULL;
    HRESULT hr;
    IUnknown_QueryInterface((IUnknown *)obj, &IID_IMap_HSTRING_HSTRING, (void **)&map);
    hr = IMap_HSTRING_HSTRING_Lookup(map, key, &value);
    printf("%s: Lookup(%ls) %#lx %s\n", what, name, hr, P(value));
    WindowsDeleteString(value);
    WindowsDeleteString(key);
    IMap_HSTRING_HSTRING_Release(map);
}

static void insert(const char *what, void *obj, const WCHAR *name, const WCHAR *value)
{
    IMap_HSTRING_HSTRING *map;
    HSTRING key = S(name), val = S(value);
    boolean replaced = 2;
    HRESULT hr;
    IUnknown_QueryInterface((IUnknown *)obj, &IID_IMap_HSTRING_HSTRING, (void **)&map);
    hr = IMap_HSTRING_HSTRING_Insert(map, key, val, &replaced);
    printf("%s: Insert(%ls, %ls) %#lx replaced %d\n", what, name ? name : L"NULL", value ? value : L"NULL", hr, replaced);
    WindowsDeleteString(key);
    WindowsDeleteString(val);
    IMap_HSTRING_HSTRING_Release(map);
}

static IBuffer *make_buffer(const void *data, UINT32 len)
{
    IBufferFactory *factory = get_factory(L"Windows.Storage.Streams.Buffer", &IID_IBufferFactory);
    IBufferByteAccess *access;
    IBuffer *buffer;
    BYTE *p;
    IBufferFactory_Create(factory, len ? len : 1, &buffer);
    IBufferFactory_Release(factory);
    IBuffer_QueryInterface(buffer, &IID_IBufferByteAccess, (void **)&access);
    IBufferByteAccess_Buffer(access, &p);
    if (len) memcpy(p, data, len);
    IBufferByteAccess_Release(access);
    IBuffer_put_Length(buffer, len);
    return buffer;
}

static void dump_buffer(const char *what, IBuffer *buffer)
{
    IBufferByteAccess *access;
    UINT32 len = 0, i;
    BYTE *p;
    if (!buffer) { printf("%s: (null buffer)\n", what); return; }
    IBuffer_get_Length(buffer, &len);
    IBuffer_QueryInterface(buffer, &IID_IBufferByteAccess, (void **)&access);
    IBufferByteAccess_Buffer(access, &p);
    IBufferByteAccess_Release(access);
    printf("%s: %u bytes:", what, len);
    for (i = 0; i < len && i < 200; i++)
        if (p[i] >= 0x20 && p[i] < 0x7f && p[i] != '\\') printf("%c", p[i]); else printf("\\x%02x", p[i]);
    printf("\n");
}

static IUriRuntimeClass *make_uri(const WCHAR *text)
{
    IUriRuntimeClassFactory *factory = get_factory(L"Windows.Foundation.Uri", &IID_IUriRuntimeClassFactory);
    IUriRuntimeClass *uri = NULL;
    HSTRING str = S(text);
    HRESULT hr = IUriRuntimeClassFactory_CreateUri(factory, str, &uri);
    if (FAILED(hr)) printf("CreateUri(%ls) %#lx\n", text, hr);
    WindowsDeleteString(str);
    IUriRuntimeClassFactory_Release(factory);
    return uri;
}

static void print_uri(const char *what, IUriRuntimeClass *uri)
{
    HSTRING abs = NULL, raw = NULL;
    HRESULT hr1, hr2;
    if (!uri) { printf("%s: (null uri)\n", what); return; }
    hr1 = IUriRuntimeClass_get_AbsoluteUri(uri, &abs);
    hr2 = IUriRuntimeClass_get_RawUri(uri, &raw);
    printf("%s: AbsoluteUri %#lx %s RawUri %#lx %s\n", what, hr1, P(abs), hr2, P(raw));
    WindowsDeleteString(abs);
    WindowsDeleteString(raw);
}

static IPropertyValueStatics *property_value_statics(void)
{
    static IPropertyValueStatics *statics;
    if (!statics) statics = get_factory(L"Windows.Foundation.PropertyValue", &IID_IPropertyValueStatics);
    return statics;
}

static IReference_DateTime *make_datetime(INT64 ticks)
{
    IReference_DateTime *ref = NULL;
    IInspectable *inspectable;
    DateTime dt = { ticks };
    IPropertyValueStatics_CreateDateTime(property_value_statics(), dt, &inspectable);
    IInspectable_QueryInterface(inspectable, &IID_IReference_DateTime, (void **)&ref);
    IInspectable_Release(inspectable);
    return ref;
}

static IReference_TimeSpan *make_timespan(INT64 ticks)
{
    IReference_TimeSpan *ref = NULL;
    IInspectable *inspectable;
    TimeSpan ts = { ticks };
    IPropertyValueStatics_CreateTimeSpan(property_value_statics(), ts, &inspectable);
    IInspectable_QueryInterface(inspectable, &IID_IReference_TimeSpan, (void **)&ref);
    IInspectable_Release(inspectable);
    return ref;
}

static IReference_UINT64 *make_uint64(UINT64 value)
{
    IReference_UINT64 *ref = NULL;
    IInspectable *inspectable;
    IPropertyValueStatics_CreateUInt64(property_value_statics(), value, &inspectable);
    IInspectable_QueryInterface(inspectable, &IID_IReference_UINT64, (void **)&ref);
    IInspectable_Release(inspectable);
    return ref;
}

static IReference_UINT32 *make_uint32(UINT32 value)
{
    IReference_UINT32 *ref = NULL;
    IInspectable *inspectable;
    IPropertyValueStatics_CreateUInt32(property_value_statics(), value, &inspectable);
    IInspectable_QueryInterface(inspectable, &IID_IReference_UINT32, (void **)&ref);
    IInspectable_Release(inspectable);
    return ref;
}

static IReference_DOUBLE *make_double(double value)
{
    IReference_DOUBLE *ref = NULL;
    IInspectable *inspectable;
    IPropertyValueStatics_CreateDouble(property_value_statics(), value, &inspectable);
    IInspectable_QueryInterface(inspectable, &IID_IReference_DOUBLE, (void **)&ref);
    IInspectable_Release(inspectable);
    return ref;
}

static void print_ref_datetime(const char *what, HRESULT hr, IReference_DateTime *ref)
{
    DateTime dt = {0};
    if (ref) IReference_DateTime_get_Value(ref, &dt);
    printf("%s: %#lx %s %I64d\n", what, hr, ref ? "value" : "(null)", dt.UniversalTime);
    if (ref) IReference_DateTime_Release(ref);
}

static void print_ref_timespan(const char *what, HRESULT hr, IReference_TimeSpan *ref)
{
    TimeSpan ts = {0};
    if (ref) IReference_TimeSpan_get_Value(ref, &ts);
    printf("%s: %#lx %s %I64d\n", what, hr, ref ? "value" : "(null)", ts.Duration);
    if (ref) IReference_TimeSpan_Release(ref);
}

static void print_ref_uint64(const char *what, HRESULT hr, IReference_UINT64 *ref)
{
    UINT64 v = 0;
    if (ref) IReference_UINT64_get_Value(ref, &v);
    printf("%s: %#lx %s %I64u\n", what, hr, ref ? "value" : "(null)", v);
    if (ref) IReference_UINT64_Release(ref);
}

static void print_ref_uint32(const char *what, HRESULT hr, IReference_UINT32 *ref)
{
    UINT32 v = 0;
    if (ref) IReference_UINT32_get_Value(ref, &v);
    printf("%s: %#lx %s %u\n", what, hr, ref ? "value" : "(null)", v);
    if (ref) IReference_UINT32_Release(ref);
}

static void print_ref_double(const char *what, HRESULT hr, IReference_DOUBLE *ref)
{
    double v = 0;
    if (ref) IReference_DOUBLE_get_Value(ref, &v);
    printf("%s: %#lx %s %g\n", what, hr, ref ? "value" : "(null)", v);
    if (ref) IReference_DOUBLE_Release(ref);
}

static AsyncStatus wait_async(void *op, HRESULT *error)
{
    IAsyncInfo *info;
    AsyncStatus status = Started;
    int i;
    *error = 0;
    if (FAILED(IUnknown_QueryInterface((IUnknown *)op, &IID_IAsyncInfo, (void **)&info))) return Error;
    for (i = 0; i < 3000; i++)
    {
        IAsyncInfo_get_Status(info, &status);
        if (status != Started) break;
        Sleep(10);
    }
    IAsyncInfo_get_ErrorCode(info, error);
    IAsyncInfo_Release(info);
    return status;
}

static void map_misc(const char *what, void *obj, const WCHAR *name)
{
    IMap_HSTRING_HSTRING *map;
    IMapView_HSTRING_HSTRING *view = NULL;
    HSTRING key = S(name);
    boolean has = 2;
    UINT32 size = 0;
    HRESULT hr;
    IUnknown_QueryInterface((IUnknown *)obj, &IID_IMap_HSTRING_HSTRING, (void **)&map);
    hr = IMap_HSTRING_HSTRING_HasKey(map, key, &has);
    printf("%s: HasKey(%ls) %#lx %d\n", what, name, hr, has);
    hr = IMap_HSTRING_HSTRING_GetView(map, &view);
    printf("%s: GetView %#lx\n", what, hr);
    if (view)
    {
        print_class("  view", view);
        IMapView_HSTRING_HSTRING_get_Size(view, &size);
        printf("  view size %u\n", size);
        IMapView_HSTRING_HSTRING_Release(view);
    }
    WindowsDeleteString(key);
    IMap_HSTRING_HSTRING_Release(map);
}

