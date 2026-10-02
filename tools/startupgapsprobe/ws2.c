/* ws2: the Windows Web Services reader's faults and error strings, call by call -- which call notices a mismatched end
 * tag or a character XML does not allow, what each later call answers once the reader is faulted, whether end tags
 * match by case, what WsReadToStartElement's name and namespace decide, and the error strings of each failure both in
 * the user's language and in English (WS_ERROR_PROPERTY_LANGID 0x409).  Prints results only. */
#include <windows.h>
#include <webservices.h>
#include <stdio.h>

static WS_ERROR *error;

static void show(const char *what, HRESULT hr)
{
    ULONG count = 0, i;

    WsGetErrorProperty(error, WS_ERROR_PROPERTY_STRING_COUNT, &count, sizeof(count));
    printf("%s: %#lx", what, hr);
    for (i = 0; i < count && i < 8; i++)
    {
        WS_STRING string = { 0 };
        if (SUCCEEDED(WsGetErrorString(error, i, &string)))
        {
            ULONG j;
            printf(" [");
            for (j = 0; j < string.length; j++)
                if (string.chars[j] >= 0x20 && string.chars[j] < 0x7f) printf("%c", string.chars[j]);
                else printf("\\u%04x", string.chars[j]);
            printf("]");
        }
    }
    printf("\n");
    WsResetError(error);
}

static WS_XML_READER *reader_for(const char *xml)
{
    WS_XML_READER_TEXT_ENCODING text = { { WS_XML_READER_ENCODING_TYPE_TEXT }, WS_CHARSET_UTF8 };
    WS_XML_READER_BUFFER_INPUT input = { { WS_XML_READER_INPUT_TYPE_BUFFER }, (void *)xml, strlen(xml) };
    WS_XML_READER *reader = NULL;

    WsCreateReader(NULL, 0, &reader, NULL);
    WsSetInput(reader, &text.encoding, &input.input, NULL, 0, NULL);
    return reader;
}

static void node_type(WS_XML_READER *reader)
{
    const WS_XML_NODE *node = NULL;
    HRESULT hr = WsGetReaderNode(reader, &node, error);

    printf("    WsGetReaderNode %#lx", hr);
    if (SUCCEEDED(hr) && node)
    {
        printf(" type %d", node->nodeType);
        if (node->nodeType == WS_XML_NODE_TYPE_ELEMENT)
        {
            const WS_XML_ELEMENT_NODE *element = (const WS_XML_ELEMENT_NODE *)node;
            printf(" %.*s", (int)element->localName->length, element->localName->bytes);
        }
    }
    printf("\n");
    WsResetError(error);
}

static void mismatch(const char *xml)
{
    WS_XML_READER *reader = reader_for(xml);
    BOOL found = 0xdead;

    printf("%s\n", xml);
    show("  WsReadToStartElement", WsReadToStartElement(reader, NULL, NULL, &found, error));
    show("  WsReadStartElement", WsReadStartElement(reader, error));
    node_type(reader);
    show("  WsReadToStartElement", WsReadToStartElement(reader, NULL, NULL, &found, error));
    show("  WsReadStartElement", WsReadStartElement(reader, error));
    node_type(reader);
    show("  WsReadEndElement", WsReadEndElement(reader, error));
    show("  WsReadNode", WsReadNode(reader, error));
    show("  WsFillReader", WsFillReader(reader, 1, NULL, error));
    {
        ULONG depth = 0xdead;
        HRESULT hr = WsGetReaderProperty(reader, WS_XML_READER_PROPERTY_MAX_DEPTH, &depth, sizeof(depth), error);
        printf("  WsGetReaderProperty max depth: %#lx %lu\n", hr, depth);
        WsResetError(error);
    }
    {
        WS_XML_READER_TEXT_ENCODING text = { { WS_XML_READER_ENCODING_TYPE_TEXT }, WS_CHARSET_UTF8 };
        WS_XML_READER_BUFFER_INPUT input = { { WS_XML_READER_INPUT_TYPE_BUFFER }, (void *)"<c/>", 4 };
        show("  WsSetInput again", WsSetInput(reader, &text.encoding, &input.input, NULL, 0, error));
        show("  WsReadToStartElement after", WsReadToStartElement(reader, NULL, NULL, &found, error));
    }
    WsFreeReader(reader);
}

static void to_start(const char *xml, const char *local, const char *ns)
{
    WS_XML_STRING local_str = { local ? strlen(local) : 0, (BYTE *)local }, ns_str = { ns ? strlen(ns) : 0, (BYTE *)ns };
    WS_XML_READER *reader = reader_for(xml);
    BOOL found = 0xdead;
    HRESULT hr;

    hr = WsReadToStartElement(reader, local ? &local_str : NULL, ns ? &ns_str : NULL, &found, error);
    printf("%s, %s/%s: %#lx found %d;", xml, local ? local : "-", ns ? ns : "-", hr, found);
    WsResetError(error);
    node_type(reader);
    WsFreeReader(reader);
}

static void run(void)
{
    mismatch("<a><b></a>");
    mismatch("<a><b></B></a>");
    mismatch("<a>\x01</a>");
    mismatch("<a>x\x08y</a>");
    mismatch("<a>\t\r\n</a>");
    mismatch("<a b='\x01'/>");

    to_start("<a/>", "a", NULL);
    to_start("<a/>", "c", NULL);
    to_start("<a/>", "A", NULL);
    to_start("<a/>", "a", "");
    to_start("<a/>", "a", "urn:x");
    to_start("<p:a xmlns:p='urn:x'/>", "a", "urn:x");
    to_start("<p:a xmlns:p='urn:x'/>", "a", "");
    to_start("<p:a xmlns:p='urn:x'/>", NULL, "urn:x");
    to_start("<p:a xmlns:p='urn:x'/>", NULL, "urn:y");
    to_start("text<a/>", "a", NULL);
}

int main(void)
{
    WS_ERROR_PROPERTY property;
    LANGID english = 0x409;

    setvbuf(stdout, NULL, _IONBF, 0);
    WsCreateError(NULL, 0, &error);
    printf("== the user's language\n");
    run();
    WsFreeError(error);

    property.id = WS_ERROR_PROPERTY_LANGID;
    property.value = &english;
    property.valueSize = sizeof(english);
    printf("== English: WsCreateError %#lx\n", WsCreateError(&property, 1, &error));
    run();
    {
        WS_HEAP *heap;
        void *ptr;
        WS_XML_READER *reader;
        WS_XML_READER_PROPERTY prop = { 0xdead, NULL, 0 };

        show("WsCreateHeap NULL", WsCreateHeap(1 << 16, 0, NULL, 0, NULL, error));
        WsCreateHeap(1 << 16, 0, NULL, 0, &heap, NULL);
        show("WsAlloc more than the heap", WsAlloc(heap, 1 << 20, &ptr, error));
        show("WsCreateReader a bad property", WsCreateReader(&prop, 1, &reader, error));
        WsFreeHeap(heap);
    }
    {
        WS_XML_READER_TEXT_ENCODING text = { { 0xdead }, WS_CHARSET_UTF8 };
        WS_XML_READER_BUFFER_INPUT input = { { WS_XML_READER_INPUT_TYPE_BUFFER }, (void *)"<a/>", 4 };
        WS_XML_READER *reader;

        WsCreateReader(NULL, 0, &reader, NULL);
        show("WsSetInput a bad encoding", WsSetInput(reader, &text.encoding, &input.input, NULL, 0, error));
        WsFreeReader(reader);
    }
    {
        WS_XML_READER *reader = reader_for("<a>not a number</a>");
        WS_XML_STRING local = { 1, (BYTE *)"a" }, ns = { 0, (BYTE *)"" };
        WS_HEAP *heap;
        BOOL found;
        INT32 value;

        WsCreateHeap(1 << 16, 0, NULL, 0, &heap, NULL);
        WsReadToStartElement(reader, &local, &ns, &found, NULL);
        show("WsReadType an int32 from 'not a number'", WsReadType(reader, WS_ELEMENT_TYPE_MAPPING, WS_INT32_TYPE,
             NULL, WS_READ_REQUIRED_VALUE, heap, &value, sizeof(value), error));
        WsFreeReader(reader);
        WsFreeHeap(heap);
    }
    {
        WS_XML_READER *reader = reader_for("text only");
        show("WsReadStartElement on text", WsReadStartElement(reader, error));
        WsFreeReader(reader);
    }
    {
        WS_HTTP_SSL_BINDING_TEMPLATE templ = { { 0 } };
        WS_SERVICE_PROXY *proxy;
        BYTE desc[64] = { 0 };

        show("WsCreateServiceProxyFromTemplate no description", WsCreateServiceProxyFromTemplate(
             WS_CHANNEL_TYPE_REQUEST, NULL, 0, WS_HTTP_SSL_BINDING_TEMPLATE_TYPE, &templ, sizeof(templ), NULL, 0,
             &proxy, error));
        show("WsCreateServiceProxyFromTemplate a bad type", WsCreateServiceProxyFromTemplate(
             WS_CHANNEL_TYPE_REQUEST, NULL, 0, 0xdead, &templ, sizeof(templ), desc, sizeof(desc), &proxy, error));
    }
    WsFreeError(error);
    return 0;
}
