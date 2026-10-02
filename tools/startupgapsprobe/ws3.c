/* ws3: which failures of WsReadType fault the Windows Web Services reader, and whether a faulted reader still checks
 * a call's arguments first -- a value that does not parse, a required element missing, and malformed XML, each
 * followed by WsReadNode and by a WsReadType with no description for a structure.  Prints results only. */
#include <windows.h>
#include <webservices.h>
#include <stdio.h>

static WS_XML_READER *reader_for(const char *xml)
{
    WS_XML_READER_TEXT_ENCODING text = { { WS_XML_READER_ENCODING_TYPE_TEXT }, WS_CHARSET_UTF8 };
    WS_XML_READER_BUFFER_INPUT input = { { WS_XML_READER_INPUT_TYPE_BUFFER }, (void *)xml, strlen(xml) };
    WS_XML_READER *reader = NULL;

    WsCreateReader(NULL, 0, &reader, NULL);
    WsSetInput(reader, &text.encoding, &input.input, NULL, 0, NULL);
    return reader;
}

static void after(WS_XML_READER *reader, WS_HEAP *heap)
{
    void *ptr = NULL;
    const WS_XML_NODE *node = NULL;
    HRESULT hr;

    hr = WsReadType(reader, WS_ELEMENT_TYPE_MAPPING, WS_STRUCT_TYPE, NULL, WS_READ_REQUIRED_POINTER, heap, &ptr,
                    sizeof(ptr), NULL);
    printf("  then WsReadType of a structure without a description: %#lx\n", hr);
    hr = WsGetReaderNode(reader, &node, NULL);
    printf("  then WsGetReaderNode: %#lx type %d\n", hr, node ? (int)node->nodeType : -1);
    hr = WsReadNode(reader, NULL);
    printf("  then WsReadNode: %#lx\n", hr);
}

int main(void)
{
    WS_XML_STRING local = { 1, (BYTE *)"a" }, ns = { 0, (BYTE *)"" };
    WS_XML_READER *reader;
    WS_HEAP *heap;
    BOOL found;
    INT32 value;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    WsCreateHeap(1 << 16, 0, NULL, 0, &heap, NULL);

    reader = reader_for("<a>not a number</a>");
    WsReadToStartElement(reader, &local, &ns, &found, NULL);
    hr = WsReadType(reader, WS_ELEMENT_TYPE_MAPPING, WS_INT32_TYPE, NULL, WS_READ_REQUIRED_VALUE, heap, &value,
                    sizeof(value), NULL);
    printf("an int32 that does not parse: %#lx\n", hr);
    after(reader, heap);
    WsFreeReader(reader);

    reader = reader_for("<b>1</b>");
    WsReadToStartElement(reader, NULL, NULL, &found, NULL);
    hr = WsReadType(reader, WS_ELEMENT_CONTENT_TYPE_MAPPING, WS_INT32_TYPE, NULL, WS_READ_REQUIRED_VALUE, heap, &value,
                    sizeof(value), NULL);
    printf("an int32 element's content: %#lx value %d\n", hr, value);
    WsFreeReader(reader);

    reader = reader_for("<b>1</b>");
    WsReadToStartElement(reader, NULL, NULL, &found, NULL);
    {
        WS_XML_STRING name = { 1, (BYTE *)"c" };
        WS_FIELD_DESCRIPTION field = { WS_ELEMENT_FIELD_MAPPING, &name, &ns, WS_INT32_TYPE };
        WS_FIELD_DESCRIPTION *fields[] = { &field };
        WS_STRUCT_DESCRIPTION desc = { sizeof(INT32), sizeof(INT32), fields, 1 };
        void *ptr = NULL;

        hr = WsReadType(reader, WS_ELEMENT_CONTENT_TYPE_MAPPING, WS_STRUCT_TYPE, &desc, WS_READ_REQUIRED_POINTER, heap,
                        &ptr, sizeof(ptr), NULL);
        printf("a required element that is not there: %#lx\n", hr);
        after(reader, heap);
    }
    WsFreeReader(reader);

    reader = reader_for("<a><b></a>");
    WsReadToStartElement(reader, NULL, NULL, &found, NULL);
    WsReadStartElement(reader, NULL);
    WsReadToStartElement(reader, NULL, NULL, &found, NULL);
    hr = WsReadStartElement(reader, NULL);
    printf("a mismatched end tag: %#lx\n", hr);
    after(reader, heap);
    WsFreeReader(reader);

    WsFreeHeap(heap);
    return 0;
}
