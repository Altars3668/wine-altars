/*
 * saxpropprobe - what the SAX reader's properties hold and do.
 *
 * For SAXXMLReader30 and SAXXMLReader60 this prints each property the reader
 * knows, through ISAXXMLReader and IVBSAXXMLReader, before and after parsing
 * documents with and without an XML declaration; which values max-xml-size and
 * max-element-depth take; and what parsing answers once they are set.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <msxml6.h>
#include <stdio.h>

DEFINE_GUID(CLSID_SAXXMLReader30_, 0x3124c396, 0xfb13, 0x4836, 0xa6, 0xad, 0x13, 0x17, 0xf1, 0x71, 0x36, 0x88);
DEFINE_GUID(CLSID_MXXMLWriter30_, 0x3d813dfe, 0x6c91, 0x4a4e, 0x8f, 0x41, 0x04, 0x34, 0x6a, 0x84, 0x1d, 0x9c);
DEFINE_GUID(CLSID_MXXMLWriter60_, 0x88d96a0f, 0xf192, 0x11d4, 0xa6, 0x5f, 0x00, 0x40, 0x96, 0x32, 0x51, 0xe5);
DEFINE_GUID(CLSID_DOMDocument60_, 0x88d96a05, 0xf192, 0x11d4, 0xa6, 0x5f, 0x00, 0x40, 0x96, 0x32, 0x51, 0xe5);
DEFINE_GUID(CLSID_XMLSchemaCache60_, 0x88d96a07, 0xf192, 0x11d4, 0xa6, 0x5f, 0x00, 0x40, 0x96, 0x32, 0x51, 0xe5);
/* interfaces Office asks MSXML objects for */
DEFINE_GUID(IID_office1, 0xe19c7100, 0x9709, 0x4db7, 0x93, 0x73, 0xe7, 0xb5, 0x18, 0xb4, 0x70, 0x86);
DEFINE_GUID(IID_office2, 0xc970c32d, 0x9ffd, 0x45e5, 0xbf, 0x20, 0xc3, 0xcb, 0xaa, 0xb2, 0x62, 0x22);
DEFINE_GUID(CLSID_SAXXMLReader60_, 0x88d96a0c, 0xf192, 0x11d4, 0xa6, 0x5f, 0x00, 0x40, 0x96, 0x32, 0x51, 0xe5);

static const WCHAR *props[] =
{
    L"xmldecl-version", L"xmldecl-encoding", L"xmldecl-standalone", L"charset", L"max-xml-size",
    L"max-element-depth", L"schema-declaration-handler", L"input-source",
    L"http://xml.org/sax/properties/dom-node", L"http://xml.org/sax/properties/lexical-handler",
    L"http://xml.org/sax/properties/declaration-handler", L"output",
};

static const char *vt_name(VARTYPE vt)
{
    static char buf[16];
    switch (vt)
    {
    case VT_EMPTY: return "VT_EMPTY";
    case VT_NULL: return "VT_NULL";
    case VT_I4: return "VT_I4";
    case VT_BSTR: return "VT_BSTR";
    case VT_BOOL: return "VT_BOOL";
    case VT_UNKNOWN: return "VT_UNKNOWN";
    case VT_DISPATCH: return "VT_DISPATCH";
    }
    sprintf(buf, "vt %u", vt);
    return buf;
}

static void print_value(HRESULT hr, VARIANT *v)
{
    if (FAILED(hr)) { printf("%#lx\n", hr); return; }
    printf("%#lx %s", hr, vt_name(V_VT(v)));
    switch (V_VT(v))
    {
    case VT_I4: printf(" %ld", V_I4(v)); break;
    case VT_BOOL: printf(" %d", V_BOOL(v)); break;
    case VT_BSTR: printf(V_BSTR(v) ? " \"%ls\"" : " NULL", V_BSTR(v)); break;
    case VT_UNKNOWN: case VT_DISPATCH: printf(V_UNKNOWN(v) ? " object" : " NULL"); break;
    }
    printf("\n");
    if (V_VT(v) == VT_BSTR || V_VT(v) == VT_UNKNOWN || V_VT(v) == VT_DISPATCH) VariantClear(v);
}

static void dump_props(ISAXXMLReader *reader, IVBSAXXMLReader *vb, const char *when)
{
    unsigned int i;
    VARIANT v;
    HRESULT hr;

    printf("  %s\n", when);
    for (i = 0; i < ARRAY_SIZE(props); i++)
    {
        BSTR name = SysAllocString(props[i]);

        V_VT(&v) = VT_EMPTY;
        V_I4(&v) = 0x1234;
        hr = ISAXXMLReader_getProperty(reader, props[i], &v);
        printf("    %-50ls ", props[i]);
        print_value(hr, &v);
        V_VT(&v) = VT_EMPTY;
        V_I4(&v) = 0x1234;
        hr = IVBSAXXMLReader_getProperty(vb, name, &v);
        printf("    %-50s ", "  VB");
        print_value(hr, &v);
        SysFreeString(name);
    }
}

static HRESULT parse(ISAXXMLReader *reader, const WCHAR *xml)
{
    VARIANT v;
    HRESULT hr;

    V_VT(&v) = VT_BSTR;
    V_BSTR(&v) = SysAllocString(xml);
    hr = ISAXXMLReader_parse(reader, v);
    VariantClear(&v);
    return hr;
}

static HRESULT parse_stream(ISAXXMLReader *reader, const char *data, size_t len)
{
    IStream *stream;
    VARIANT v;
    HRESULT hr;

    CreateStreamOnHGlobal(NULL, TRUE, &stream);
    IStream_Write(stream, data, len, NULL);
    IStream_Seek(stream, (LARGE_INTEGER){{0}}, STREAM_SEEK_SET, NULL);
    V_VT(&v) = VT_UNKNOWN;
    V_UNKNOWN(&v) = (IUnknown *)stream;
    hr = ISAXXMLReader_parse(reader, v);
    IStream_Release(stream);
    return hr;
}

static void put(ISAXXMLReader *reader, const WCHAR *name, VARIANT v, const char *what)
{
    VARIANT got;
    HRESULT hr;

    hr = ISAXXMLReader_putProperty(reader, name, v);
    V_VT(&got) = VT_EMPTY;
    printf("  put %ls %-12s %#lx, now ", name, what, hr);
    print_value(ISAXXMLReader_getProperty(reader, name, &got), &got);
}


/* a content and an error handler printing what they are told */
static HRESULT WINAPI eh_QueryInterface(ISAXErrorHandler *iface, REFIID riid, void **obj) { *obj = iface; return S_OK; }
static ULONG WINAPI eh_AddRef(ISAXErrorHandler *iface) { return 2; }
static ULONG WINAPI eh_Release(ISAXErrorHandler *iface) { return 1; }
static void print_error(const char *what, ISAXLocator *locator, const WCHAR *message, HRESULT hr)
{
    int line = -1, column = -1;
    if (locator)
    {
        ISAXLocator_getLineNumber(locator, &line);
        ISAXLocator_getColumnNumber(locator, &column);
    }
    char text[512] = "(null)";
    if (message) WideCharToMultiByte(CP_UTF8, 0, message, -1, text, sizeof(text), NULL, NULL);
    printf("      %s %#lx at %d:%d: %s\n", what, hr, line, column, text);
}
static HRESULT WINAPI eh_error(ISAXErrorHandler *iface, ISAXLocator *locator, const WCHAR *message, HRESULT hr)
{
    print_error("error", locator, message, hr);
    return S_OK;
}
static HRESULT WINAPI eh_fatalError(ISAXErrorHandler *iface, ISAXLocator *locator, const WCHAR *message, HRESULT hr)
{
    print_error("fatalError", locator, message, hr);
    return S_OK;
}
static HRESULT WINAPI eh_ignorableWarning(ISAXErrorHandler *iface, ISAXLocator *locator, const WCHAR *message, HRESULT hr)
{
    print_error("ignorableWarning", locator, message, hr);
    return S_OK;
}
static const ISAXErrorHandlerVtbl eh_vtbl = { eh_QueryInterface, eh_AddRef, eh_Release, eh_error, eh_fatalError, eh_ignorableWarning };
static ISAXErrorHandler error_handler = { &eh_vtbl };

static HRESULT WINAPI ch_QueryInterface(ISAXContentHandler *iface, REFIID riid, void **obj) { *obj = iface; return S_OK; }
static ULONG WINAPI ch_AddRef(ISAXContentHandler *iface) { return 2; }
static ULONG WINAPI ch_Release(ISAXContentHandler *iface) { return 1; }
static HRESULT WINAPI ch_putDocumentLocator(ISAXContentHandler *iface, ISAXLocator *locator) { return S_OK; }
static HRESULT WINAPI ch_startDocument(ISAXContentHandler *iface) { printf("      startDocument\n"); return S_OK; }
static HRESULT WINAPI ch_endDocument(ISAXContentHandler *iface) { printf("      endDocument\n"); return S_OK; }
static HRESULT WINAPI ch_startPrefixMapping(ISAXContentHandler *iface, const WCHAR *prefix, int prefix_len, const WCHAR *uri, int uri_len) { return S_OK; }
static HRESULT WINAPI ch_endPrefixMapping(ISAXContentHandler *iface, const WCHAR *prefix, int len) { return S_OK; }
static HRESULT WINAPI ch_startElement(ISAXContentHandler *iface, const WCHAR *uri, int uri_len, const WCHAR *local, int local_len,
        const WCHAR *qname, int qname_len, ISAXAttributes *attributes)
{
    printf("      startElement %.*ls\n", qname_len, qname);
    return S_OK;
}
static HRESULT WINAPI ch_endElement(ISAXContentHandler *iface, const WCHAR *uri, int uri_len, const WCHAR *local, int local_len,
        const WCHAR *qname, int qname_len)
{
    printf("      endElement %.*ls\n", qname_len, qname);
    return S_OK;
}
static HRESULT WINAPI ch_characters(ISAXContentHandler *iface, const WCHAR *chars, int len) { printf("      characters %d\n", len); return S_OK; }
static HRESULT WINAPI ch_ignorableWhitespace(ISAXContentHandler *iface, const WCHAR *chars, int len) { return S_OK; }
static HRESULT WINAPI ch_processingInstruction(ISAXContentHandler *iface, const WCHAR *target, int target_len, const WCHAR *data, int data_len) { return S_OK; }
static HRESULT WINAPI ch_skippedEntity(ISAXContentHandler *iface, const WCHAR *name, int len) { return S_OK; }
static const ISAXContentHandlerVtbl ch_vtbl = { ch_QueryInterface, ch_AddRef, ch_Release, ch_putDocumentLocator, ch_startDocument,
    ch_endDocument, ch_startPrefixMapping, ch_endPrefixMapping, ch_startElement, ch_endElement, ch_characters,
    ch_ignorableWhitespace, ch_processingInstruction, ch_skippedEntity };
static ISAXContentHandler content_handler = { &ch_vtbl };

static void print_decl(ISAXXMLReader *reader, const char *when)
{
    static const WCHAR *names[] = { L"xmldecl-version", L"xmldecl-encoding", L"xmldecl-standalone", L"charset", L"input-source" };
    unsigned int i;
    VARIANT v;

    printf("    %s\n", when);
    for (i = 0; i < ARRAY_SIZE(names); i++)
    {
        V_VT(&v) = VT_EMPTY;
        printf("      %-20ls ", names[i]);
        print_value(ISAXXMLReader_getProperty(reader, names[i], &v), &v);
    }
}

static void probe_limits(const GUID *clsid, const char *name)
{
    static const char *streams[] =
    {
        "<?xml version=\"1.0\"?><e/>",
        "<?xml version=\"1.0\" encoding=\"iso-8859-1\" standalone=\"yes\"?><e/>",
        "\xef\xbb\xbf<?xml version=\"1.0\" encoding=\"UTF-8\"?><e/>",
        "<e/>",
    };
    static const unsigned int stream_sizes[] = { 1000, 1023, 1024, 1025, 1100 };
    static const unsigned int string_sizes[] = { 500, 511, 512, 513, 1000, 1023, 1024, 1025 };
    ISAXXMLReader *reader;
    unsigned int i, j;
    char *data;
    WCHAR *wdata;
    VARIANT v;
    HRESULT hr;

    hr = CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, &IID_ISAXXMLReader, (void **)&reader);
    printf("%s limits: %#lx\n", name, hr);
    if (FAILED(hr)) return;

    for (i = 0; i < ARRAY_SIZE(streams); i++)
    {
        char when[160];
        hr = parse_stream(reader, streams[i], strlen(streams[i]));
        sprintf(when, "after the stream %s: %#lx", streams[i][0] == '\xef' ? "(BOM) <?xml ... UTF-8?><e/>" : streams[i], hr);
        print_decl(reader, when);
    }

    V_VT(&v) = VT_I4; V_I4(&v) = 4194305; put(reader, L"max-xml-size", v, "4194305");
    V_VT(&v) = VT_I4; V_I4(&v) = 8388607; put(reader, L"max-xml-size", v, "8388607");
    V_VT(&v) = VT_I4; V_I4(&v) = 5000000; put(reader, L"max-xml-size", v, "5000000");
    V_VT(&v) = VT_I4; V_I4(&v) = 0; put(reader, L"max-xml-size", v, "0");

    ISAXXMLReader_putErrorHandler(reader, &error_handler);
    V_VT(&v) = VT_I4; V_I4(&v) = 1; put(reader, L"max-xml-size", v, "1");
    data = malloc(2000);
    wdata = malloc(2000 * sizeof(WCHAR));
    for (i = 0; i < ARRAY_SIZE(stream_sizes); i++)
    {
        unsigned int n = stream_sizes[i];
        memcpy(data, "<a>", 3);
        memset(data + 3, 'a', n - 7);
        memcpy(data + n - 4, "</a>", 4);
        printf("    %u bytes in a stream: %#lx\n", n, parse_stream(reader, data, n));
    }
    for (i = 0; i < ARRAY_SIZE(string_sizes); i++)
    {
        unsigned int n = string_sizes[i];
        memcpy(wdata, L"<a>", 3 * sizeof(WCHAR));
        for (j = 3; j < n - 4; j++) wdata[j] = 'a';
        memcpy(wdata + n - 4, L"</a>", 5 * sizeof(WCHAR));
        printf("    %u characters in a string: %#lx\n", n, parse(reader, wdata));
    }
    ISAXXMLReader_putContentHandler(reader, &content_handler);
    /* where parsing stops */
    memcpy(data, "<a><b>", 6);
    memset(data + 6, 'a', 1500);
    memcpy(data + 1506, "</b><c/></a>", 13);
    printf("    <a><b>1500 characters</b><c/></a> in a stream, events:\n");
    hr = parse_stream(reader, data, strlen(data));
    printf("    = %#lx\n", hr);
    V_VT(&v) = VT_I4; V_I4(&v) = 0; put(reader, L"max-xml-size", v, "0");

    V_VT(&v) = VT_BSTR; V_BSTR(&v) = SysAllocString(L"abc"); put(reader, L"max-element-depth", v, "\"abc\""); VariantClear(&v);
    V_VT(&v) = VT_R8; V_R8(&v) = 2.5; put(reader, L"max-element-depth", v, "R8 2.5");
    V_VT(&v) = VT_R8; V_R8(&v) = 3.5; put(reader, L"max-element-depth", v, "R8 3.5");
    V_VT(&v) = VT_EMPTY; put(reader, L"max-element-depth", v, "EMPTY");
    V_VT(&v) = VT_BOOL; V_BOOL(&v) = VARIANT_TRUE; put(reader, L"max-element-depth", v, "BOOL true");
    V_VT(&v) = VT_R8; V_R8(&v) = 2.5; put(reader, L"max-xml-size", v, "R8 2.5");
    V_VT(&v) = VT_R8; V_R8(&v) = 3.5; put(reader, L"max-xml-size", v, "R8 3.5");
    V_VT(&v) = VT_I4; V_I4(&v) = 0; put(reader, L"max-xml-size", v, "0");
    V_VT(&v) = VT_I4; V_I4(&v) = 2; put(reader, L"max-element-depth", v, "2");
    printf("    <a><b>x</b><b><c>y</c></b></a>, events:\n");
    hr = parse(reader, L"<a><b>x</b><b><c>y</c></b></a>");
    printf("    = %#lx\n", hr);
    printf("    <a><b/></a>, events:\n");
    hr = parse(reader, L"<a><b/></a>");
    printf("    = %#lx\n", hr);

    free(data);
    free(wdata);
    ISAXXMLReader_Release(reader);
}

static void probe(const GUID *clsid, const char *name)
{
    static const WCHAR *docs[] =
    {
        L"<e/>",
        L"<?xml version=\"1.0\"?><e/>",
        L"<?xml version=\"1.0\" encoding=\"uTf-16\" standalone=\"yes\"?><e/>",
        L"<?xml version=\"1.0\" standalone=\"no\"?><e/>",
    };
    ISAXXMLReader *reader;
    IVBSAXXMLReader *vb;
    char *big, depth[64];
    unsigned int i;
    VARIANT v;
    HRESULT hr;

    hr = CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, &IID_ISAXXMLReader, (void **)&reader);
    printf("%s: %#lx\n", name, hr);
    if (FAILED(hr)) return;
    ISAXXMLReader_QueryInterface(reader, &IID_IVBSAXXMLReader, (void **)&vb);

    dump_props(reader, vb, "new reader");
    for (i = 0; i < ARRAY_SIZE(docs); i++)
    {
        char when[160];
        hr = parse(reader, docs[i]);
        sprintf(when, "after %ls: %#lx", docs[i], hr);
        dump_props(reader, vb, when);
    }
    hr = parse(reader, L"<?xml version=\"1.0\"?><e>");
    printf("  a broken document: %#lx\n", hr);
    V_VT(&v) = VT_EMPTY;
    print_value(ISAXXMLReader_getProperty(reader, L"xmldecl-version", &v), &v);

    /* max-xml-size, in kilobytes */
    V_VT(&v) = VT_R4; V_R4(&v) = 10.0f; put(reader, L"max-xml-size", v, "R4 10.0");
    V_VT(&v) = VT_BSTR; V_BSTR(&v) = SysAllocString(L"abc"); put(reader, L"max-xml-size", v, "\"abc\""); VariantClear(&v);
    V_VT(&v) = VT_BSTR; V_BSTR(&v) = SysAllocString(L"7"); put(reader, L"max-xml-size", v, "\"7\""); VariantClear(&v);
    V_VT(&v) = VT_I4; V_I4(&v) = -123; put(reader, L"max-xml-size", v, "-123");
    V_VT(&v) = VT_I4; V_I4(&v) = 4194303; put(reader, L"max-xml-size", v, "4194303");
    V_VT(&v) = VT_I4; V_I4(&v) = 4194304; put(reader, L"max-xml-size", v, "4194304");
    V_VT(&v) = VT_BOOL; V_BOOL(&v) = VARIANT_TRUE; put(reader, L"max-xml-size", v, "BOOL true");
    V_VT(&v) = VT_EMPTY; put(reader, L"max-xml-size", v, "EMPTY");

    big = malloc(3000);
    memcpy(big, "<a>", 3);
    memset(big + 3, 'a', 2000);
    memcpy(big + 2003, "</a>", 4);
    V_VT(&v) = VT_I4; V_I4(&v) = 1; put(reader, L"max-xml-size", v, "1");
    printf("  2007 bytes in a stream, at most 1K: %#lx\n", parse_stream(reader, big, 2007));
    printf("  1000 bytes in a stream, at most 1K: %#lx\n", parse_stream(reader, "<a>aaaaaaaaaa</a>", 17));
    {
        WCHAR *wbig = malloc(2008 * sizeof(WCHAR));
        for (i = 0; i < 2007; i++) wbig[i] = big[i];
        wbig[2007] = 0;
        printf("  2007 characters in a string, at most 1K: %#lx\n", parse(reader, wbig));
        wbig[3 + 500] = 0;
        memcpy(wbig + 503, L"</a>", 5 * sizeof(WCHAR));
        printf("  507 characters in a string, at most 1K: %#lx\n", parse(reader, wbig));
        free(wbig);
    }
    V_VT(&v) = VT_I4; V_I4(&v) = 3; put(reader, L"max-xml-size", v, "3");
    printf("  2007 bytes in a stream, at most 3K: %#lx\n", parse_stream(reader, big, 2007));
    V_VT(&v) = VT_I4; V_I4(&v) = 0; put(reader, L"max-xml-size", v, "0");
    free(big);

    /* max-element-depth */
    V_VT(&v) = VT_UI4; V_UI4(&v) = 2147483648u; put(reader, L"max-element-depth", v, "UI4 2^31");
    V_VT(&v) = VT_I4; V_I4(&v) = 2147483647; put(reader, L"max-element-depth", v, "2^31-1");
    V_VT(&v) = VT_I4; V_I4(&v) = -1; put(reader, L"max-element-depth", v, "-1");
    V_VT(&v) = VT_BSTR; V_BSTR(&v) = SysAllocString(L"4"); put(reader, L"max-element-depth", v, "\"4\""); VariantClear(&v);
    V_VT(&v) = VT_I4; V_I4(&v) = 0; put(reader, L"max-element-depth", v, "0");
    printf("  <a><b><c/></b></a>, unlimited: %#lx\n", parse(reader, L"<a><b><c/></b></a>"));
    V_VT(&v) = VT_I4; V_I4(&v) = 1; put(reader, L"max-element-depth", v, "1");
    printf("  <a>text</a>, at most 1: %#lx\n", parse(reader, L"<a>text</a>"));
    printf("  <a>text<!-- comment --></a>, at most 1: %#lx\n", parse(reader, L"<a>text<!-- comment --></a>"));
    printf("  <a>text<!-- comment --><b/></a>, at most 1: %#lx\n", parse(reader, L"<a>text<!-- comment --><b/></a>"));
    V_VT(&v) = VT_I4; V_I4(&v) = 2; put(reader, L"max-element-depth", v, "2");
    printf("  <a><b/></a>, at most 2: %#lx\n", parse(reader, L"<a><b/></a>"));
    printf("  <a><b><c/></b></a>, at most 2: %#lx\n", parse(reader, L"<a><b><c/></b></a>"));
    sprintf(depth, "%u", 0);
    V_VT(&v) = VT_I4; V_I4(&v) = 0; put(reader, L"max-element-depth", v, "0");

    IVBSAXXMLReader_Release(vb);
    ISAXXMLReader_Release(reader);
}

static void probe_qi(const GUID *clsid, const char *name)
{
    IUnknown *unk, *obj;
    HRESULT hr;

    hr = CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, &IID_IUnknown, (void **)&unk);
    printf("%s: %#lx\n", name, hr);
    if (FAILED(hr)) return;
    obj = (IUnknown *)0xdeadbeef;
    hr = IUnknown_QueryInterface(unk, &IID_office1, (void **)&obj);
    printf("  {e19c7100-9709-4db7-9373-e7b518b47086} %#lx %s\n", hr, obj ? (obj == (IUnknown *)0xdeadbeef ? "untouched" : "object") : "NULL");
    if (SUCCEEDED(hr) && obj) IUnknown_Release(obj);
    obj = (IUnknown *)0xdeadbeef;
    hr = IUnknown_QueryInterface(unk, &IID_office2, (void **)&obj);
    printf("  {c970c32d-9ffd-45e5-bf20-c3cbaab26222} %#lx %s\n", hr, obj ? (obj == (IUnknown *)0xdeadbeef ? "untouched" : "object") : "NULL");
    if (SUCCEEDED(hr) && obj) IUnknown_Release(obj);
    IUnknown_Release(unk);
}

int main(void)
{
    CoInitialize(NULL);
    probe_qi(&CLSID_SAXXMLReader60_, "QI SAXXMLReader60");
    probe_qi(&CLSID_MXXMLWriter60_, "QI MXXMLWriter60");
    probe_qi(&CLSID_SAXXMLReader30_, "QI SAXXMLReader30");
    probe_qi(&CLSID_MXXMLWriter30_, "QI MXXMLWriter30");
    probe_qi(&CLSID_DOMDocument60_, "QI DOMDocument60");
    probe_qi(&CLSID_XMLSchemaCache60_, "QI XMLSchemaCache60");
    probe(&CLSID_SAXXMLReader30_, "SAXXMLReader30");
    probe(&CLSID_SAXXMLReader60_, "SAXXMLReader60");
    probe_limits(&CLSID_SAXXMLReader30_, "SAXXMLReader30");
    probe_limits(&CLSID_SAXXMLReader60_, "SAXXMLReader60");
    printf("done\n");
    CoUninitialize();
    return 0;
}
