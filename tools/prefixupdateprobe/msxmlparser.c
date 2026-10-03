#define COBJMACROS
#define CONST_VTABLE
#include "probe.h"
#include <objbase.h>
#include <initguid.h>
#include <msxml2.h>

/* MinGW 的 msxml2.h 没有 XMLSchemaCache60；GUID 来自 msxml6.idl。 */
DEFINE_GUID(probe_schema_cache60, 0x88d96a07, 0xf192, 0x11d4,
            0xa6, 0x5f, 0x00, 0x40, 0x96, 0x32, 0x51, 0xe5);

static int probe_result;

static void error_fields(const char *label, IXMLDOMParseError *error)
{
    LONG code = (LONG)PROBE_SENTINEL, line = -1, position = -1;
    HRESULT code_hr, line_hr, position_hr;

    if (!error)
    {
        printf("parseError=%s present=0\n", label);
        return;
    }
    code_hr = IXMLDOMParseError_get_errorCode(error, &code);
    line_hr = IXMLDOMParseError_get_line(error, &line);
    position_hr = IXMLDOMParseError_get_linepos(error, &position);
    printf("parseError=%s present=1 code_hr=%#lx errorCode=%#lx line_hr=%#lx line=%ld"
           " linepos_hr=%#lx linepos=%ld\n", label, (ULONG)code_hr, (ULONG)code,
           (ULONG)line_hr, line, (ULONG)position_hr, position);
}

static void document_state(IXMLDOMDocument2 *document, const WCHAR *expected)
{
    IXMLDOMParseError *error = NULL;
    IXMLDOMElement *root = NULL;
    BSTR text = NULL;
    LONG ready = -1;
    HRESULT hr;

    hr = IXMLDOMDocument2_get_readyState(document, &ready);
    printf("readyState hr=%#lx value=%ld\n", (ULONG)hr, ready);
    hr = IXMLDOMDocument2_get_parseError(document, &error);
    printf("get_parseError hr=%#lx\n", (ULONG)hr);
    error_fields("load", error);
    if (error) IXMLDOMParseError_Release(error);
    hr = IXMLDOMDocument2_get_documentElement(document, &root);
    printf("documentElement hr=%#lx present=%u", (ULONG)hr, !!root);
    if (root)
    {
        hr = IXMLDOMElement_get_text(root, &text);
        printf(" text_hr=%#lx text_characters=%u expected_available=%u text_matches_expected=%u",
               (ULONG)hr, SysStringLen(text), !!expected,
               expected && text && !wcscmp(text, expected));
        SysFreeString(text);
        IXMLDOMElement_Release(root);
    }
    puts("");
}

static HRESULT boolean_property(IXMLDOMDocument2 *document, const WCHAR *name,
                                const char *label, VARIANT_BOOL value)
{
    BSTR property = SysAllocString(name);
    VARIANT input, output;
    HRESULT set_hr, get_hr;
    DWORD error;

    if (!property)
    {
        puts("property_allocation_failed");
        probe_result = 1;
        return E_OUTOFMEMORY;
    }
    VariantInit(&input);
    VariantInit(&output);
    V_VT(&input) = VT_BOOL;
    V_BOOL(&input) = value;
    SetLastError(PROBE_SENTINEL);
    set_hr = IXMLDOMDocument2_setProperty(document, property, input);
    error = GetLastError();
    get_hr = IXMLDOMDocument2_getProperty(document, property, &output);
    printf("property=%s input=%d set_hr=%#lx gle=%lu get_hr=%#lx output_vt=%u",
           label, value, (ULONG)set_hr, error, (ULONG)get_hr, V_VT(&output));
    if (V_VT(&output) == VT_BOOL) printf(" output_bool=%d", V_BOOL(&output));
    puts("");
    VariantClear(&output);
    SysFreeString(property);
    return set_hr;
}

static IXMLDOMDocument2 *new_document(VARIANT_BOOL new_parser, VARIANT_BOOL validate,
                                     VARIANT_BOOL resolve, VARIANT_BOOL async)
{
    IXMLDOMDocument2 *document = NULL;
    VARIANT_BOOL readback = 1;
    HRESULT hr;
    DWORD error;

    SetLastError(PROBE_SENTINEL);
    hr = CoCreateInstance(&CLSID_DOMDocument60, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IXMLDOMDocument2, (void **)&document);
    error = GetLastError();
    printf("DOMDocument60 create_hr=%#lx gle=%lu present=%u\n", (ULONG)hr, error, !!document);
    if (FAILED(hr) || !document) return NULL;
    boolean_property(document, L"NewParser", "NewParser", new_parser);
    boolean_property(document, L"ProhibitDTD", "ProhibitDTD", VARIANT_FALSE);
    hr = IXMLDOMDocument2_put_validateOnParse(document, validate);
    printf("validateOnParse input=%d put_hr=%#lx\n", validate, (ULONG)hr);
    hr = IXMLDOMDocument2_put_resolveExternals(document, resolve);
    printf("ResolveExternals input=%d put_hr=%#lx\n", resolve, (ULONG)hr);
    hr = IXMLDOMDocument2_put_async(document, async);
    printf("async input=%d put_hr=%#lx\n", async, (ULONG)hr);
    hr = IXMLDOMDocument2_get_async(document, &readback);
    printf("async get_hr=%#lx output=%d\n", (ULONG)hr, readback);
    return document;
}

static BOOL load_xml(IXMLDOMDocument2 *document, const WCHAR *xml, const WCHAR *expected)
{
    BSTR source = SysAllocString(xml);
    VARIANT_BOOL loaded = 1;
    HRESULT hr;
    DWORD error;

    if (!source)
    {
        puts("XML_allocation_failed");
        probe_result = 1;
        return FALSE;
    }
    SetLastError(PROBE_SENTINEL);
    hr = IXMLDOMDocument2_loadXML(document, source, &loaded);
    error = GetLastError();
    printf("loadXML input_characters=%u hr=%#lx gle=%lu loaded=%d\n",
           SysStringLen(source), (ULONG)hr, error, loaded);
    SysFreeString(source);
    document_state(document, expected);
    return SUCCEEDED(hr) && loaded == VARIANT_TRUE;
}

static void validate_document(IXMLDOMDocument2 *document)
{
    IXMLDOMParseError *error = NULL;
    HRESULT hr;
    DWORD last_error;

    SetLastError(PROBE_SENTINEL);
    hr = IXMLDOMDocument2_validate(document, &error);
    last_error = GetLastError();
    printf("validate hr=%#lx gle=%lu\n", (ULONG)hr, last_error);
    error_fields("validate", error);
    if (error) IXMLDOMParseError_Release(error);
}

/* CREATE_NEW 不覆盖已有文件；只使用当前目录内的随机相对文件名。 */
static BOOL write_fixture(const WCHAR *name, const char *label, const char *contents)
{
    HANDLE file;
    DWORD written = 0, error;
    size_t length = strlen(contents);
    BOOL ok, deleted;

    SetLastError(PROBE_SENTINEL);
    file = CreateFileW(name, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW,
                       FILE_ATTRIBUTE_TEMPORARY, NULL);
    error = GetLastError();
    printf("fixture=%s create=%u gle=%lu location=<current-directory>\n", label,
           file != INVALID_HANDLE_VALUE, error);
    if (file == INVALID_HANDLE_VALUE) return FALSE;
    SetLastError(PROBE_SENTINEL);
    ok = WriteFile(file, contents, (DWORD)length, &written, NULL);
    error = GetLastError();
    printf("fixture=%s write=%u gle=%lu input_bytes=%llu written=%lu\n", label, ok,
           error, (unsigned long long)length, written);
    CloseHandle(file);
    if (ok && written == length) return TRUE;
    SetLastError(PROBE_SENTINEL);
    deleted = DeleteFileW(name);
    error = GetLastError();
    printf("fixture=%s failed_write_delete=%u gle=%lu\n", label, deleted, error);
    return FALSE;
}

static void delete_fixture(const WCHAR *name, const char *label)
{
    BOOL ok;
    DWORD error;
    SetLastError(PROBE_SENTINEL);
    ok = DeleteFileW(name);
    error = GetLastError();
    printf("fixture=%s delete=%u gle=%lu\n", label, ok, error);
    if (!ok) probe_result = 1;
}

static void load_file(IXMLDOMDocument2 *document, const WCHAR *name,
                      const WCHAR *expected, BOOL asynchronous)
{
    VARIANT source;
    VARIANT_BOOL loaded = 1;
    HRESULT hr, ready_hr;
    LONG ready = -1, previous = -1;
    MSG message;
    DWORD error, begin, elapsed;

    VariantInit(&source);
    V_VT(&source) = VT_BSTR;
    V_BSTR(&source) = SysAllocString(name);
    if (!V_BSTR(&source))
    {
        puts("filename_allocation_failed");
        probe_result = 1;
        return;
    }
    begin = GetTickCount();
    SetLastError(PROBE_SENTINEL);
    hr = IXMLDOMDocument2_load(document, source, &loaded);
    error = GetLastError();
    elapsed = GetTickCount() - begin;
    printf("load source=<current-directory-file> hr=%#lx gle=%lu loaded=%d elapsed_ms=%lu\n",
           (ULONG)hr, error, loaded, elapsed);
    VariantClear(&source);
    ready_hr = IXMLDOMDocument2_get_readyState(document, &ready);
    printf("load immediate_ready_hr=%#lx immediate_ready=%ld\n", (ULONG)ready_hr, ready);
    /* STA 消息泵有界；停止后释放 DOM，再删除输入文件。 */
    if (asynchronous && SUCCEEDED(hr))
    {
        while (SUCCEEDED(ready_hr) && ready != 4 && GetTickCount() - begin < 1500)
        {
            if (ready != previous) printf("async ready_transition=%ld\n", ready);
            previous = ready;
            MsgWaitForMultipleObjects(0, NULL, FALSE, 20, QS_ALLINPUT);
            while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            ready_hr = IXMLDOMDocument2_get_readyState(document, &ready);
        }
        printf("async final_ready_hr=%#lx final_ready=%ld elapsed_ms=%lu\n",
               (ULONG)ready_hr, ready, GetTickCount() - begin);
        if (SUCCEEDED(ready_hr) && ready != 4)
        {
            hr = IXMLDOMDocument2_abort(document);
            printf("async completion_timeout=1 abort_hr=%#lx\n", (ULONG)hr);
            probe_result = 1;
        }
    }
    document_state(document, expected);
}

static void parser_cases(VARIANT_BOOL new_parser, const WCHAR *async_name,
                         const WCHAR *external_name)
{
    static const struct
    {
        const char *label;
        const WCHAR *xml;
        const WCHAR *expected;
        VARIANT_BOOL validate;
    } cases[] =
    {
        {"internal_DTD_entity", L"<!DOCTYPE root [<!ELEMENT root (#PCDATA)>"
             L"<!ENTITY value 'internal-entity'>]><root>&value;</root>",
             L"internal-entity", VARIANT_FALSE},
        {"DTD_valid", L"<!DOCTYPE root [<!ELEMENT root (child)>"
             L"<!ELEMENT child EMPTY>]><root><child/></root>", L"", VARIANT_TRUE},
        {"DTD_invalid_validation_off", L"<!DOCTYPE root [<!ELEMENT root (child)>"
             L"<!ELEMENT child EMPTY>]><root/>", L"", VARIANT_FALSE},
        {"DTD_invalid_validation_on", L"<!DOCTYPE root [<!ELEMENT root (child)>"
             L"<!ELEMENT child EMPTY>]><root/>", L"", VARIANT_TRUE},
        {"malformed_XML", L"<root>\n  <child>value</root>", NULL, VARIANT_FALSE},
    };
    IXMLDOMDocument2 *document;
    unsigned int i;
    VARIANT_BOOL resolve;

    for (i = 0; i < ARRAYSIZE(cases); ++i)
    {
        printf("case=%s NewParser=%d\n", cases[i].label, new_parser);
        document = new_document(new_parser, cases[i].validate, VARIANT_FALSE, VARIANT_FALSE);
        if (!document) continue;
        load_xml(document, cases[i].xml, cases[i].expected);
        IXMLDOMDocument2_Release(document);
    }
    if (async_name)
    {
        printf("case=async_file NewParser=%d\n", new_parser);
        document = new_document(new_parser, VARIANT_FALSE, VARIANT_FALSE, VARIANT_TRUE);
        if (document)
        {
            load_file(document, async_name, L"async-local", TRUE);
            IXMLDOMDocument2_Release(document);
        }
    }
    if (external_name)
        for (i = 0; i < 2; ++i)
        {
            resolve = i ? VARIANT_TRUE : VARIANT_FALSE;
            printf("case=external_entity NewParser=%d ResolveExternals=%d\n", new_parser, resolve);
            document = new_document(new_parser, VARIANT_FALSE, resolve, VARIANT_FALSE);
            if (!document) continue;
            load_file(document, external_name, L"external-local", FALSE);
            IXMLDOMDocument2_Release(document);
        }
}

static void schema_cases(VARIANT_BOOL new_parser)
{
    const WCHAR namespace_uri[] = L"urn:prefixupdateprobe";
    const WCHAR xsd[] = L"<xs:schema xmlns:xs='http://www.w3.org/2001/XMLSchema'"
        L" targetNamespace='urn:prefixupdateprobe' elementFormDefault='qualified'>"
        L"<xs:element name='root' type='xs:int'/></xs:schema>";
    const WCHAR *instances[] = {L"<root xmlns='urn:prefixupdateprobe'>42</root>",
        L"<root xmlns='urn:prefixupdateprobe'>not-an-integer</root>"};
    IXMLDOMDocument2 *schema, *document;
    IXMLDOMSchemaCollection2 *cache = NULL;
    VARIANT input, output;
    BSTR uri;
    HRESULT hr;
    LONG count = -1;
    DWORD error;
    unsigned int validate, valid;

    printf("case=schema_cache_setup NewParser=%d\n", new_parser);
    schema = new_document(new_parser, VARIANT_FALSE, VARIANT_FALSE, VARIANT_FALSE);
    if (!schema) return;
    if (!load_xml(schema, xsd, NULL))
    {
        puts("schema_cases skipped: inline XSD could not be parsed");
        IXMLDOMDocument2_Release(schema);
        return;
    }
    SetLastError(PROBE_SENTINEL);
    hr = CoCreateInstance(&probe_schema_cache60, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IXMLDOMSchemaCollection2, (void **)&cache);
    error = GetLastError();
    printf("XMLSchemaCache60 create_hr=%#lx gle=%lu present=%u\n", (ULONG)hr, error, !!cache);
    if (FAILED(hr) || !cache)
    {
        IXMLDOMDocument2_Release(schema);
        return;
    }
    uri = SysAllocString(namespace_uri);
    if (!uri)
    {
        probe_result = 1;
        IXMLDOMSchemaCollection2_Release(cache);
        IXMLDOMDocument2_Release(schema);
        return;
    }
    VariantInit(&input);
    V_VT(&input) = VT_DISPATCH;
    V_DISPATCH(&input) = (IDispatch *)schema;
    IXMLDOMDocument2_AddRef(schema);
    SetLastError(PROBE_SENTINEL);
    hr = IXMLDOMSchemaCollection2_add(cache, uri, input);
    error = GetLastError();
    printf("schema_cache add_inline_XSD_hr=%#lx gle=%lu\n", (ULONG)hr, error);
    VariantClear(&input);
    SysFreeString(uri);
    IXMLDOMDocument2_Release(schema);
    if (FAILED(hr))
    {
        IXMLDOMSchemaCollection2_Release(cache);
        puts("schema_cases skipped: cache rejected inline XSD");
        return;
    }
    hr = IXMLDOMSchemaCollection2_get_length(cache, &count);
    printf("schema_cache length_hr=%#lx length=%ld\n", (ULONG)hr, count);
    hr = IXMLDOMSchemaCollection2_validate(cache);
    printf("schema_cache validate_hr=%#lx\n", (ULONG)hr);
    for (validate = 0; validate < 2; ++validate)
        for (valid = 0; valid < 2; ++valid)
        {
            printf("case=XSD_instance NewParser=%d validateOnParse=%d instance=%s\n",
                   new_parser, validate ? VARIANT_TRUE : VARIANT_FALSE, valid ? "valid" : "invalid");
            document = new_document(new_parser, validate ? VARIANT_TRUE : VARIANT_FALSE,
                                    VARIANT_FALSE, VARIANT_FALSE);
            if (!document) continue;
            VariantInit(&input);
            VariantInit(&output);
            V_VT(&input) = VT_DISPATCH;
            V_DISPATCH(&input) = (IDispatch *)cache;
            IXMLDOMSchemaCollection2_AddRef(cache);
            SetLastError(PROBE_SENTINEL);
            hr = IXMLDOMDocument2_putref_schemas(document, input);
            error = GetLastError();
            printf("schemas putref_hr=%#lx gle=%lu input_vt=%u\n", (ULONG)hr, error, V_VT(&input));
            VariantClear(&input);
            hr = IXMLDOMDocument2_get_schemas(document, &output);
            printf("schemas get_hr=%#lx output_vt=%u\n", (ULONG)hr, V_VT(&output));
            VariantClear(&output);
            load_xml(document, instances[valid ? 0 : 1], valid ? L"42" : L"not-an-integer");
            validate_document(document);
            IXMLDOMDocument2_Release(document);
        }
    IXMLDOMSchemaCollection2_Release(cache);
}

int main(void)
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    DWORD random[4];
    WCHAR async_name[128], entity_name[128], external_name[128];
    char entity_ascii[128], external_xml[512];
    BOOL async_created = FALSE, entity_created = FALSE, external_created = FALSE;
    HRESULT hr;
    unsigned int i;

    if (!probe_start()) return 1;
    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    printf("COM init_STA_hr=%#lx\n", (ULONG)hr);
    if (FAILED(hr)) return probe_done(1);
    if (probe_random(random, sizeof(random)))
    {
        swprintf(async_name, ARRAYSIZE(async_name), L"prefixupdate_msxml_%08lx%08lx%08lx%08lx_async.xml",
                 random[0], random[1], random[2], random[3]);
        swprintf(entity_name, ARRAYSIZE(entity_name), L"prefixupdate_msxml_%08lx%08lx%08lx%08lx_entity.txt",
                 random[0], random[1], random[2], random[3]);
        swprintf(external_name, ARRAYSIZE(external_name), L"prefixupdate_msxml_%08lx%08lx%08lx%08lx_external.xml",
                 random[0], random[1], random[2], random[3]);
        snprintf(entity_ascii, sizeof(entity_ascii), "prefixupdate_msxml_%08lx%08lx%08lx%08lx_entity.txt",
                 random[0], random[1], random[2], random[3]);
        snprintf(external_xml, sizeof(external_xml), "<!DOCTYPE root [<!ELEMENT root (#PCDATA)>"
                 "<!ENTITY value SYSTEM '%s'>]><root>&value;</root>", entity_ascii);
        async_created = write_fixture(async_name, "async_XML", "<root>async-local</root>");
        entity_created = write_fixture(entity_name, "external_entity", "external-local");
        if (entity_created) external_created = write_fixture(external_name, "external_XML", external_xml);
        if (!async_created || !entity_created || !external_created) probe_result = 1;
    }
    else probe_result = 1;
    for (i = 0; i < 2; ++i)
    {
        VARIANT_BOOL new_parser = i ? VARIANT_TRUE : VARIANT_FALSE;
        printf("suite NewParser=%d\n", new_parser);
        parser_cases(new_parser, async_created ? async_name : NULL,
                     external_created ? external_name : NULL);
        schema_cases(new_parser);
    }
    CoUninitialize();
    if (external_created) delete_fixture(external_name, "external_XML");
    if (entity_created) delete_fixture(entity_name, "external_entity");
    if (async_created) delete_fixture(async_name, "async_XML");
    return probe_done(probe_result);
}
