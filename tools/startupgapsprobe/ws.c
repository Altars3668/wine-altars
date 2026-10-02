/* ws: what the Windows Web Services API puts into a WS_ERROR when a call given one fails -- the properties of a new
 * error object, then the strings and original error code after failing calls of the kinds Word makes (creating a heap,
 * a reader, setting its input, reading malformed and unexpected XML, reading a type, opening a service proxy), and
 * what resetting does.  Prints results only. */
#include <windows.h>
#include <webservices.h>
#include <stdio.h>

/* not in mingw's header: channel, security and SSL binding properties, each a pointer and a count */
typedef struct
{
    struct { void *properties; ULONG count; } channelProperties, securityProperties, sslBindingProperties;
} WS_HTTP_SSL_POLICY_DESCRIPTION_;

static void show_error(const char *what, HRESULT hr, WS_ERROR *error)
{
    ULONG count = 0xdead, i;
    HRESULT original = 0xdead;

    WsGetErrorProperty(error, WS_ERROR_PROPERTY_STRING_COUNT, &count, sizeof(count));
    WsGetErrorProperty(error, WS_ERROR_PROPERTY_ORIGINAL_ERROR_CODE, &original, sizeof(original));
    printf("%s: %#lx, %lu strings, original %#lx\n", what, hr, count, original);
    for (i = 0; i < count && i < 8; i++)
    {
        WS_STRING string = { 0 };
        if (SUCCEEDED(WsGetErrorString(error, i, &string)))
        {
            ULONG j;
            printf("    [");
            for (j = 0; j < string.length; j++)
                if (string.chars[j] >= 0x20 && string.chars[j] < 0x7f) printf("%c", string.chars[j]);
                else printf("\\u%04x", string.chars[j]);
            printf("]\n");
        }
    }
    WsResetError(error);
}

static HRESULT read_xml(const char *xml, WS_XML_READER **out, WS_ERROR *error)
{
    WS_XML_READER_TEXT_ENCODING text = { { WS_XML_READER_ENCODING_TYPE_TEXT }, WS_CHARSET_UTF8 };
    WS_XML_READER_BUFFER_INPUT input = { { WS_XML_READER_INPUT_TYPE_BUFFER }, (void *)xml, strlen(xml) };
    HRESULT hr;

    if (FAILED(hr = WsCreateReader(NULL, 0, out, error))) return hr;
    return WsSetInput(*out, &text.encoding, &input.input, NULL, 0, error);
}

int main(void)
{
    WS_ERROR *error;
    WS_HEAP *heap;
    WS_XML_READER *reader;
    LANGID langid = 0xdead;
    ULONG count = 0xdead;
    BOOL found;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    hr = WsCreateError(NULL, 0, &error);
    WsGetErrorProperty(error, WS_ERROR_PROPERTY_STRING_COUNT, &count, sizeof(count));
    hr = WsGetErrorProperty(error, WS_ERROR_PROPERTY_LANGID, &langid, sizeof(langid));
    printf("WsCreateError: strings %lu, langid %#x (%#lx), user default ui %#x\n", count, langid, hr,
           GetUserDefaultUILanguage());

    hr = WsCreateHeap(0, 0, NULL, 0, &heap, error);
    show_error("WsCreateHeap max size 0", hr, error);
    hr = WsCreateHeap(1 << 16, 0, NULL, 0, NULL, error);
    show_error("WsCreateHeap NULL", hr, error);
    hr = WsCreateHeap(1 << 16, 0, NULL, 0, &heap, error);
    show_error("WsCreateHeap", hr, error);
    {
        void *ptr;
        hr = WsAlloc(heap, 1 << 20, &ptr, error);
        show_error("WsAlloc more than the heap", hr, error);
        hr = WsResetHeap(heap, error);
        show_error("WsResetHeap", hr, error);
    }
    {
        WS_XML_READER_PROPERTY prop = { 0xdead, NULL, 0 };
        hr = WsCreateReader(&prop, 1, &reader, error);
        show_error("WsCreateReader a bad property", hr, error);
    }
    hr = WsCreateReader(NULL, 0, &reader, error);
    show_error("WsCreateReader", hr, error);
    {
        WS_XML_READER_TEXT_ENCODING text = { { 0xdead }, WS_CHARSET_UTF8 };
        WS_XML_READER_BUFFER_INPUT input = { { WS_XML_READER_INPUT_TYPE_BUFFER }, (void *)"<a/>", 4 };
        hr = WsSetInput(reader, &text.encoding, &input.input, NULL, 0, error);
        show_error("WsSetInput a bad encoding", hr, error);
    }
    WsFreeReader(reader);

    hr = read_xml("<a><b></a>", &reader, error);
    hr = WsReadToStartElement(reader, NULL, NULL, &found, error);
    show_error("WsReadToStartElement <a><b></a>", hr, error);
    hr = WsReadStartElement(reader, error);
    hr = WsReadToStartElement(reader, NULL, NULL, &found, error);
    hr = WsReadStartElement(reader, error);
    hr = WsReadEndElement(reader, error);
    show_error("WsReadEndElement at the mismatched end", hr, error);
    WsFreeReader(reader);

    hr = read_xml("text only", &reader, error);
    hr = WsReadStartElement(reader, error);
    show_error("WsReadStartElement on text", hr, error);
    WsFreeReader(reader);

    hr = read_xml("<a>not a number</a>", &reader, error);
    {
        INT32 value;
        WS_XML_STRING local = { 1, (BYTE *)"a" }, ns = { 0, (BYTE *)"" };
        hr = WsReadToStartElement(reader, &local, &ns, &found, error);
        hr = WsReadType(reader, WS_ELEMENT_TYPE_MAPPING, WS_INT32_TYPE, NULL, WS_READ_REQUIRED_VALUE, heap, &value,
                        sizeof(value), error);
        show_error("WsReadType an int32 from 'not a number'", hr, error);
    }
    WsFreeReader(reader);

    hr = read_xml("<a xmlns:p='urn:x'><p:b/></a>", &reader, error);
    {
        WS_XML_STRING local = { 1, (BYTE *)"c" }, ns = { 0, (BYTE *)"" };
        hr = WsReadToStartElement(reader, &local, &ns, &found, error);
        show_error("WsReadToStartElement a name that is not there", hr, error);
        hr = WsReadStartElement(reader, error);
        hr = WsReadToStartElement(reader, &local, &ns, &found, error);
        printf("  found %d\n", found);
        hr = WsFillReader(reader, 100, NULL, error);
        show_error("WsFillReader on a buffer", hr, error);
    }
    WsFreeReader(reader);

    hr = read_xml("<a>\x01</a>", &reader, error);
    hr = WsReadToStartElement(reader, NULL, NULL, &found, error);
    hr = WsReadStartElement(reader, error);
    hr = WsReadNode(reader, error);
    show_error("WsReadNode a control character", hr, error);
    WsFreeReader(reader);

    {
        WS_HTTP_SSL_BINDING_TEMPLATE templ = { { 0 } };
        WS_HTTP_SSL_POLICY_DESCRIPTION_ desc = { { 0 } };
        WS_SERVICE_PROXY *proxy = NULL;
        WS_ENDPOINT_ADDRESS address = { { 0 } };
        WS_SERVICE_PROXY_STATE state = 0xdead;

        hr = WsCreateServiceProxyFromTemplate(WS_CHANNEL_TYPE_REQUEST, NULL, 0, WS_HTTP_SSL_BINDING_TEMPLATE_TYPE,
                                              &templ, sizeof(templ), &desc, sizeof(desc), &proxy, error);
        show_error("WsCreateServiceProxyFromTemplate https", hr, error);
        if (proxy)
        {
            WsGetServiceProxyProperty(proxy, WS_PROXY_PROPERTY_STATE, &state, sizeof(state), error);
            printf("  state %d\n", state);
            address.url.chars = (WCHAR *)L"https://127.0.0.1:1/probe";
            address.url.length = wcslen(address.url.chars);
            hr = WsOpenServiceProxy(proxy, &address, NULL, error);
            show_error("WsOpenServiceProxy https://127.0.0.1:1/", hr, error);
            WsGetServiceProxyProperty(proxy, WS_PROXY_PROPERTY_STATE, &state, sizeof(state), error);
            printf("  state %d\n", state);
            hr = WsCloseServiceProxy(proxy, NULL, error);
            show_error("WsCloseServiceProxy", hr, error);
            WsFreeServiceProxy(proxy);
        }
        hr = WsCreateServiceProxyFromTemplate(WS_CHANNEL_TYPE_REQUEST, NULL, 0, WS_HTTP_SSL_BINDING_TEMPLATE_TYPE,
                                              &templ, sizeof(templ), NULL, 0, &proxy, error);
        show_error("WsCreateServiceProxyFromTemplate no description", hr, error);
        hr = WsCreateServiceProxyFromTemplate(WS_CHANNEL_TYPE_REQUEST, NULL, 0, 0xdead, &templ, sizeof(templ), &desc,
                                              sizeof(desc), &proxy, error);
        show_error("WsCreateServiceProxyFromTemplate a bad type", hr, error);
    }

    {
        WS_STRING string = { 5, (WCHAR *)L"added" };
        hr = WsAddErrorString(error, &string);
        hr = WsAddErrorString(error, &string);
        show_error("WsAddErrorString twice", hr, error);
        hr = WsGetErrorString(error, 0, &string);
        printf("WsGetErrorString after a reset: %#lx\n", hr);
    }
    WsFreeHeap(heap);
    WsFreeError(error);
    return 0;
}
