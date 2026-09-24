/*
 * wsproxyprobe - what a service proxy made from a binding template puts on
 * the wire, depending on where its channel properties come from.
 *
 * Office talks to its roaming settings service through
 * WsCreateServiceProxyFromTemplate; the SOAP 1.1 envelope version it needs is
 * in the template *description* (generated from the WSDL), not in the
 * template value.  A local listener prints each request's Content-Type,
 * SOAPAction and the start of its envelope.  Run on Windows and under Wine
 * and diff the output.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -o wsproxyprobe.exe \
 *       wsproxyprobe.c -lwebservices -lws2_32
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <winsock2.h>
#include <windows.h>
#include <webservices.h>
#include <stdio.h>
#include <string.h>

#define PORT 7561

static HANDLE ready, served;

static void print_header(const char *buf, const char *name)
{
    const char *p = strstr(buf, name), *e;
    if (!p) { printf("    %s (none)\n", name); return; }
    e = strstr(p, "\r\n");
    printf("    %.*s\n", (int)(e - p), p);
}

static DWORD WINAPI server(void *arg)
{
    WSADATA wsa; SOCKET s, c; struct sockaddr_in sa; int on = 1;
    static char buf[8192];
    static const char resp_body[] =
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body/></s:Envelope>";

    WSAStartup(MAKEWORD(2, 2), &wsa);
    s = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on));
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET; sa.sin_port = htons(PORT); sa.sin_addr.s_addr = inet_addr("127.0.0.1");
    bind(s, (struct sockaddr *)&sa, sizeof(sa));
    listen(s, 4);
    SetEvent(ready);
    for (;;)
    {
        int n = 0, len = 0, i;
        char *body, *p, resp[512];
        if ((c = accept(s, NULL, NULL)) == INVALID_SOCKET) break;
        for (;;)
        {
            int r = recv(c, buf + n, sizeof(buf) - 1 - n, 0);
            if (r <= 0) break;
            n += r; buf[n] = 0;
            if ((body = strstr(buf, "\r\n\r\n")))
            {
                if ((p = strstr(buf, "Content-Length: "))) len = atoi(p + 16);
                if (n - (body + 4 - buf) >= len) break;
            }
        }
        buf[n] = 0;
        body = strstr(buf, "\r\n\r\n");
        printf("  request:\n");
        print_header(buf, "Content-Type:");
        print_header(buf, "SOAPAction:");
        if (body)
        {
            int text = 1, blen = n - (int)(body + 4 - buf);
            body += 4;
            for (i = 0; i < blen && i < 64; i++) if ((unsigned char)body[i] < 0x20 && body[i] != '\r' && body[i] != '\n') text = 0;
            if (text)
            {
                for (i = 0; body[i] && i < 400; i++) if (body[i] == '\r' || body[i] == '\n') body[i] = ' ';
                printf("    body: %.400s\n", body);
            }
            else
            {
                printf("    body (%d bytes):", blen);
                for (i = 0; i < blen && i < 48; i++) printf(" %02x", (unsigned char)body[i]);
                printf("\n");
            }
        }
        sprintf(resp, "HTTP/1.1 200 OK\r\nContent-Type: text/xml; charset=utf-8\r\nContent-Length: %u\r\n\r\n%s",
                (unsigned int)strlen(resp_body), resp_body);
        send(c, resp, strlen(resp), 0);
        closesocket(c);
        SetEvent(served);
    }
    return 0;
}

static void call(const char *name, const WS_CHANNEL_PROPERTY *desc_props, ULONG desc_count,
                 const WS_CHANNEL_PROPERTY *value_props, ULONG value_count)
{
    static WS_XML_STRING ns = {6, (BYTE *)"urn:p1"}, req = {3, (BYTE *)"req"}, resp = {4, (BYTE *)"resp"};
    static WS_XML_STRING req_elem = {4, (BYTE *)"ping"}, resp_elem = {4, (BYTE *)"pong"};
    static WS_XML_STRING req_action = {10, (BYTE *)"urn:p1/req"}, resp_action = {11, (BYTE *)"urn:p1/resp"};
    static WS_XML_STRING val = {3, (BYTE *)"val"};
    WS_HTTP_POLICY_DESCRIPTION desc;
    WS_HTTP_BINDING_TEMPLATE value;
    WS_SERVICE_PROXY *proxy = NULL;
    WS_ENDPOINT_ADDRESS addr;
    WS_FIELD_DESCRIPTION f, *fields[1];
    WS_STRUCT_DESCRIPTION input_struct, output_struct;
    WS_ELEMENT_DESCRIPTION input_elem, output_elem;
    WS_MESSAGE_DESCRIPTION input_msg, output_msg;
    WS_PARAMETER_DESCRIPTION param[1];
    WS_OPERATION_DESCRIPTION op;
    const void *args[1];
    WS_HEAP *heap;
    INT32 v = 1;
    HRESULT hr;

    printf("== %s\n", name);
    memset(&desc, 0, sizeof(desc));
    desc.channelProperties.properties = (WS_CHANNEL_PROPERTY *)desc_props;
    desc.channelProperties.propertyCount = desc_count;
    memset(&value, 0, sizeof(value));
    value.channelProperties.properties = (WS_CHANNEL_PROPERTY *)value_props;
    value.channelProperties.propertyCount = value_count;

    hr = WsCreateServiceProxyFromTemplate(WS_CHANNEL_TYPE_REQUEST, NULL, 0, WS_HTTP_BINDING_TEMPLATE_TYPE,
                                          value_count ? &value : NULL, value_count ? sizeof(value) : 0,
                                          &desc, sizeof(desc), &proxy, NULL);
    printf("  WsCreateServiceProxyFromTemplate -> %#lx\n", hr);
    if (FAILED(hr)) return;

    memset(&addr, 0, sizeof(addr));
    addr.url.chars = (WCHAR *)L"http://127.0.0.1:7561/";
    addr.url.length = wcslen(addr.url.chars);
    hr = WsOpenServiceProxy(proxy, &addr, NULL, NULL);
    printf("  WsOpenServiceProxy -> %#lx\n", hr);

    memset(&f, 0, sizeof(f));
    f.mapping = WS_ELEMENT_FIELD_MAPPING; f.localName = &val; f.ns = &ns; f.type = WS_INT32_TYPE;
    fields[0] = &f;
    memset(&input_struct, 0, sizeof(input_struct));
    input_struct.size = sizeof(INT32); input_struct.alignment = 4; input_struct.fields = fields;
    input_struct.fieldCount = 1; input_struct.typeLocalName = &req; input_struct.typeNs = &ns;
    memset(&output_struct, 0, sizeof(output_struct));
    output_struct.size = sizeof(INT32); output_struct.alignment = 4;
    output_struct.typeLocalName = &resp; output_struct.typeNs = &ns;
    input_elem.elementLocalName = &req_elem; input_elem.elementNs = &ns;
    input_elem.type = WS_STRUCT_TYPE; input_elem.typeDescription = &input_struct;
    output_elem.elementLocalName = &resp_elem; output_elem.elementNs = &ns;
    output_elem.type = WS_STRUCT_TYPE; output_elem.typeDescription = &output_struct;
    input_msg.action = &req_action; input_msg.bodyElementDescription = &input_elem;
    output_msg.action = &resp_action; output_msg.bodyElementDescription = &output_elem;
    param[0].parameterType = WS_PARAMETER_TYPE_NORMAL;
    param[0].inputMessageIndex = 0; param[0].outputMessageIndex = 0xffff;
    memset(&op, 0, sizeof(op));
    op.versionInfo = 1; op.inputMessageDescription = &input_msg; op.outputMessageDescription = &output_msg;
    op.parameterCount = 1; op.parameterDescription = param;
    args[0] = &v;

    WsCreateHeap(1 << 16, 0, NULL, 0, &heap, NULL);
    ResetEvent(served);
    hr = WsCall(proxy, &op, args, heap, NULL, 0, NULL, NULL);
    WaitForSingleObject(served, 2000);
    printf("  WsCall -> %#lx\n", hr);
    WsCloseServiceProxy(proxy, NULL, NULL);
    WsFreeServiceProxy(proxy);
    WsFreeHeap(heap);
}

int main(void)
{
    WS_ENVELOPE_VERSION soap11 = WS_ENVELOPE_VERSION_SOAP_1_1, soap12 = WS_ENVELOPE_VERSION_SOAP_1_2;
    WS_ADDRESSING_VERSION transport = WS_ADDRESSING_VERSION_TRANSPORT;
    WS_ENCODING utf8 = WS_ENCODING_XML_UTF8;
    ULONG max_buffered = 0x800000;
    WS_CHANNEL_PROPERTY office_desc[3] =
    {
        { WS_CHANNEL_PROPERTY_ENCODING, &utf8, sizeof(utf8) },
        { WS_CHANNEL_PROPERTY_ADDRESSING_VERSION, &transport, sizeof(transport) },
        { WS_CHANNEL_PROPERTY_ENVELOPE_VERSION, &soap11, sizeof(soap11) },
    };
    WS_CHANNEL_PROPERTY office_value[1] =
    {
        { WS_CHANNEL_PROPERTY_MAX_BUFFERED_MESSAGE_SIZE, &max_buffered, sizeof(max_buffered) },
    };
    WS_CHANNEL_PROPERTY soap12_value[1] = { { WS_CHANNEL_PROPERTY_ENVELOPE_VERSION, &soap12, sizeof(soap12) } };
    WS_CHANNEL_PROPERTY soap11_transport[2] =
    {
        { WS_CHANNEL_PROPERTY_ENVELOPE_VERSION, &soap11, sizeof(soap11) },
        { WS_CHANNEL_PROPERTY_ADDRESSING_VERSION, &transport, sizeof(transport) },
    };

    WS_ADDRESSING_VERSION wsa10 = WS_ADDRESSING_VERSION_1_0, wsa09 = WS_ADDRESSING_VERSION_0_9;
    WS_CHANNEL_PROPERTY soap11_only[1] = { { WS_CHANNEL_PROPERTY_ENVELOPE_VERSION, &soap11, sizeof(soap11) } };
    WS_CHANNEL_PROPERTY soap12_transport[2] =
    {
        { WS_CHANNEL_PROPERTY_ENVELOPE_VERSION, &soap12, sizeof(soap12) },
        { WS_CHANNEL_PROPERTY_ADDRESSING_VERSION, &transport, sizeof(transport) },
    };
    WS_CHANNEL_PROPERTY soap11_wsa10[2] =
    {
        { WS_CHANNEL_PROPERTY_ENVELOPE_VERSION, &soap11, sizeof(soap11) },
        { WS_CHANNEL_PROPERTY_ADDRESSING_VERSION, &wsa10, sizeof(wsa10) },
    };
    WS_CHANNEL_PROPERTY soap12_wsa09[2] =
    {
        { WS_CHANNEL_PROPERTY_ENVELOPE_VERSION, &soap12, sizeof(soap12) },
        { WS_CHANNEL_PROPERTY_ADDRESSING_VERSION, &wsa09, sizeof(wsa09) },
    };

    WS_ENCODING binary = WS_ENCODING_XML_BINARY_1, utf16le = WS_ENCODING_XML_UTF16LE, mtom = WS_ENCODING_XML_MTOM_UTF8;
    WS_CHANNEL_PROPERTY enc_binary12[1] = { { WS_CHANNEL_PROPERTY_ENCODING, &binary, sizeof(binary) } };
    WS_CHANNEL_PROPERTY enc_binary11[3] =
    {
        { WS_CHANNEL_PROPERTY_ENCODING, &binary, sizeof(binary) },
        { WS_CHANNEL_PROPERTY_ENVELOPE_VERSION, &soap11, sizeof(soap11) },
        { WS_CHANNEL_PROPERTY_ADDRESSING_VERSION, &transport, sizeof(transport) },
    };
    WS_CHANNEL_PROPERTY enc_utf16le11[3] =
    {
        { WS_CHANNEL_PROPERTY_ENCODING, &utf16le, sizeof(utf16le) },
        { WS_CHANNEL_PROPERTY_ENVELOPE_VERSION, &soap11, sizeof(soap11) },
        { WS_CHANNEL_PROPERTY_ADDRESSING_VERSION, &transport, sizeof(transport) },
    };
    WS_CHANNEL_PROPERTY enc_mtom12[1] = { { WS_CHANNEL_PROPERTY_ENCODING, &mtom, sizeof(mtom) } };
    WS_ENCODING utf16be = WS_ENCODING_XML_UTF16BE;
    WS_CHANNEL_PROPERTY enc_utf16be12[1] = { { WS_CHANNEL_PROPERTY_ENCODING, &utf16be, sizeof(utf16be) } };
    WS_CHANNEL_PROPERTY enc_binary12t[2] =
    {
        { WS_CHANNEL_PROPERTY_ENCODING, &binary, sizeof(binary) },
        { WS_CHANNEL_PROPERTY_ADDRESSING_VERSION, &transport, sizeof(transport) },
    };

    setvbuf(stdout, NULL, _IONBF, 0);
    ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    served = CreateEventW(NULL, TRUE, FALSE, NULL);
    CreateThread(NULL, 0, server, NULL, 0, NULL);
    WaitForSingleObject(ready, 5000);

    call("description only: SOAP 1.1, transport addressing, UTF-8", office_desc, 3, NULL, 0);
    call("Office: that description, and a buffer size in the template value", office_desc, 3, office_value, 1);
    call("description SOAP 1.1, template value SOAP 1.2", soap11_transport, 2, soap12_value, 1);
    call("template value only: SOAP 1.1, transport addressing", NULL, 0, soap11_transport, 2);
    call("neither", NULL, 0, NULL, 0);
    call("description SOAP 1.1, template value SOAP 1.1 too", soap11_transport, 2, soap11_only, 1);
    call("SOAP 1.2, transport addressing", NULL, 0, soap12_transport, 2);
    call("SOAP 1.1, WS-Addressing 1.0", NULL, 0, soap11_wsa10, 2);
    call("SOAP 1.2, WS-Addressing 0.9", NULL, 0, soap12_wsa09, 2);
    call("binary, SOAP 1.2", NULL, 0, enc_binary12, 1);
    call("binary, SOAP 1.1, transport addressing", NULL, 0, enc_binary11, 3);
    call("UTF-16LE, SOAP 1.1, transport addressing", NULL, 0, enc_utf16le11, 3);
    call("MTOM UTF-8, SOAP 1.2", NULL, 0, enc_mtom12, 1);
    call("UTF-16BE, SOAP 1.2", NULL, 0, enc_utf16be12, 1);
    call("binary, SOAP 1.2, transport addressing", NULL, 0, enc_binary12t, 2);
    return 0;
}
