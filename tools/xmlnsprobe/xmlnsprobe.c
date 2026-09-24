/*
 * xmlnsprobe - how XmlLite's writer handles namespace declarations written
 * as attributes, next to the ones it makes for an element's own namespace.
 *
 * Office writes the FSSHTTP SOAP envelope for saving to OneDrive as
 *     WriteStartElement(L"s", L"Envelope", uri)
 *     WriteAttributeString(L"xmlns", L"s", NULL, uri)
 * and copies elements carrying xmlns="" from a reader into a writer.  Run on
 * Windows and under Wine and diff the output.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -municode \
 *       -o xmlnsprobe.exe xmlnsprobe.c -lxmllite -lole32 -lshlwapi
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <xmllite.h>
#include <shlwapi.h>
#include <stdio.h>

static const WCHAR soap[] = L"http://schemas.xmlsoap.org/soap/envelope/";
static const WCHAR xmlnsuri[] = L"http://www.w3.org/2000/xmlns/";

static IXmlWriter *writer;
static IStream *stream;

static void begin(const char *name)
{
    printf("== %s\n", name);
    CreateStreamOnHGlobal(NULL, TRUE, &stream);
    CreateXmlWriter(&IID_IXmlWriter, (void **)&writer, NULL);
    IXmlWriter_SetOutput(writer, (IUnknown *)stream);
    IXmlWriter_SetProperty(writer, XmlWriterProperty_OmitXmlDeclaration, TRUE);
}

static void step(const char *what, HRESULT hr)
{
    printf("  %-60s %#lx\n", what, hr);
}

static void end(void)
{
    HGLOBAL h; char *p; SIZE_T size;
    step("WriteEndDocument", IXmlWriter_WriteEndDocument(writer));
    step("Flush", IXmlWriter_Flush(writer));
    GetHGlobalFromStream(stream, &h);
    size = GlobalSize(h); p = GlobalLock(h);
    {
        ULARGE_INTEGER pos; LARGE_INTEGER zero = {{0}};
        IStream_Seek(stream, zero, STREAM_SEEK_CUR, &pos);
        size = pos.QuadPart;
    }
    printf("  -> [%.*s]\n", (int)size, p);
    GlobalUnlock(h);
    IXmlWriter_Release(writer);
    IStream_Release(stream);
}

static void copy_from_reader(const char *name, const char *xml)
{
    IXmlReader *reader; IStream *in;
    begin(name);
    in = SHCreateMemStream((const BYTE *)xml, strlen(xml));
    CreateXmlReader(&IID_IXmlReader, (void **)&reader, NULL);
    IXmlReader_SetInput(reader, (IUnknown *)in);
    step("WriteNode(reader)", IXmlWriter_WriteNode(writer, reader, TRUE));
    end();
    IXmlReader_Release(reader);
    IStream_Release(in);
}

int wmain(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    CoInitialize(NULL);

    begin("element namespace, then the same declaration as an attribute (Office's SOAP envelope)");
    step("WriteStartElement(s, Envelope, soap)", IXmlWriter_WriteStartElement(writer, L"s", L"Envelope", soap));
    step("WriteAttributeString(xmlns, s, NULL, soap)", IXmlWriter_WriteAttributeString(writer, L"xmlns", L"s", NULL, soap));
    step("WriteStartElement(s, Body, soap)", IXmlWriter_WriteStartElement(writer, L"s", L"Body", soap));
    end();

    begin("declaration as an attribute first, then an element in it");
    step("WriteStartElement(s, Envelope, soap)", IXmlWriter_WriteStartElement(writer, L"s", L"Envelope", soap));
    step("WriteAttributeString(xmlns, s, xmlnsuri, soap)", IXmlWriter_WriteAttributeString(writer, L"xmlns", L"s", xmlnsuri, soap));
    end();

    begin("element namespace, then a different uri for the same prefix");
    step("WriteStartElement(s, Envelope, soap)", IXmlWriter_WriteStartElement(writer, L"s", L"Envelope", soap));
    step("WriteAttributeString(xmlns, s, NULL, urn:other)", IXmlWriter_WriteAttributeString(writer, L"xmlns", L"s", NULL, L"urn:other"));
    end();

    begin("default namespace element, then xmlns attribute with the same uri");
    step("WriteStartElement(NULL, e, urn:a)", IXmlWriter_WriteStartElement(writer, NULL, L"e", L"urn:a"));
    step("WriteAttributeString(NULL, xmlns, NULL, urn:a)", IXmlWriter_WriteAttributeString(writer, NULL, L"xmlns", NULL, L"urn:a"));
    end();

    begin("default namespace element, then xmlns attribute through the xmlns uri");
    step("WriteStartElement(NULL, e, urn:a)", IXmlWriter_WriteStartElement(writer, NULL, L"e", L"urn:a"));
    step("WriteAttributeString(\"\", xmlns, xmlnsuri, urn:a)", IXmlWriter_WriteAttributeString(writer, L"", L"xmlns", xmlnsuri, L"urn:a"));
    end();

    begin("empty default namespace through the xmlns uri (reader copy)");
    step("WriteStartElement(NULL, e, NULL)", IXmlWriter_WriteStartElement(writer, NULL, L"e", NULL));
    step("WriteAttributeString(\"\", DCa, \"\", DC PSU)", IXmlWriter_WriteAttributeString(writer, L"", L"DCa", L"", L"DC PSU"));
    step("WriteAttributeString(\"\", xmlns, xmlnsuri, \"\")", IXmlWriter_WriteAttributeString(writer, L"", L"xmlns", xmlnsuri, L""));
    end();

    begin("xmlns prefix through the xmlns uri, no element namespace");
    step("WriteStartElement(NULL, e, NULL)", IXmlWriter_WriteStartElement(writer, NULL, L"e", NULL));
    step("WriteAttributeString(xmlns, p, xmlnsuri, urn:p)", IXmlWriter_WriteAttributeString(writer, L"xmlns", L"p", xmlnsuri, L"urn:p"));
    step("WriteStartElement(p, c, urn:p)", IXmlWriter_WriteStartElement(writer, L"p", L"c", L"urn:p"));
    end();

    begin("the same declaration written twice");
    step("WriteStartElement(p, e, urn:p)", IXmlWriter_WriteStartElement(writer, L"p", L"e", L"urn:p"));
    step("WriteAttributeString(xmlns, p, NULL, urn:p)", IXmlWriter_WriteAttributeString(writer, L"xmlns", L"p", NULL, L"urn:p"));
    step("WriteAttributeString(xmlns, p, NULL, urn:p)", IXmlWriter_WriteAttributeString(writer, L"xmlns", L"p", NULL, L"urn:p"));
    end();

    begin("an unused prefix declared twice");
    step("WriteStartElement(NULL, e, NULL)", IXmlWriter_WriteStartElement(writer, NULL, L"e", NULL));
    step("WriteAttributeString(xmlns, q, NULL, urn:q)", IXmlWriter_WriteAttributeString(writer, L"xmlns", L"q", NULL, L"urn:q"));
    step("WriteAttributeString(xmlns, q, NULL, urn:q)", IXmlWriter_WriteAttributeString(writer, L"xmlns", L"q", NULL, L"urn:q"));
    step("WriteAttributeString(xmlns, q, NULL, urn:other)", IXmlWriter_WriteAttributeString(writer, L"xmlns", L"q", NULL, L"urn:other"));
    end();

    begin("default namespace element, then a different default namespace attribute");
    step("WriteStartElement(NULL, e, urn:a)", IXmlWriter_WriteStartElement(writer, NULL, L"e", L"urn:a"));
    step("WriteAttributeString(NULL, xmlns, NULL, urn:b)", IXmlWriter_WriteAttributeString(writer, NULL, L"xmlns", NULL, L"urn:b"));
    end();

    begin("default namespace attribute first, then a child in it");
    step("WriteStartElement(NULL, e, NULL)", IXmlWriter_WriteStartElement(writer, NULL, L"e", NULL));
    step("WriteAttributeString(NULL, xmlns, NULL, urn:a)", IXmlWriter_WriteAttributeString(writer, NULL, L"xmlns", NULL, L"urn:a"));
    step("WriteStartElement(NULL, c, urn:a)", IXmlWriter_WriteStartElement(writer, NULL, L"c", L"urn:a"));
    end();

    begin("xmlns local name in the xmlns namespace, NULL prefix");
    step("WriteStartElement(NULL, e, urn:a)", IXmlWriter_WriteStartElement(writer, NULL, L"e", L"urn:a"));
    step("WriteAttributeString(NULL, xmlns, xmlnsuri, urn:a)", IXmlWriter_WriteAttributeString(writer, NULL, L"xmlns", xmlnsuri, L"urn:a"));
    end();

    begin("prefix declared through the xmlns uri with a NULL prefix, element in it");
    step("WriteStartElement(s, Envelope, soap)", IXmlWriter_WriteStartElement(writer, L"s", L"Envelope", soap));
    step("WriteAttributeString(NULL, s, xmlnsuri, soap)", IXmlWriter_WriteAttributeString(writer, NULL, L"s", xmlnsuri, soap));
    end();

    begin("declaration of a prefix the element does not use, then an attribute in it");
    step("WriteStartElement(s, Envelope, soap)", IXmlWriter_WriteStartElement(writer, L"s", L"Envelope", soap));
    step("WriteAttributeString(xmlns, xop, NULL, urn:xop)", IXmlWriter_WriteAttributeString(writer, L"xmlns", L"xop", NULL, L"urn:xop"));
    step("WriteAttributeString(xop, href, urn:xop, v)", IXmlWriter_WriteAttributeString(writer, L"xop", L"href", L"urn:xop", L"v"));
    step("WriteStartElement(xop, Include, urn:xop)", IXmlWriter_WriteStartElement(writer, L"xop", L"Include", L"urn:xop"));
    end();

    copy_from_reader("reader copy of a prefixed element with its declaration",
                     "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body/></s:Envelope>");
    copy_from_reader("reader copy of xmlns=\"\"", "<r xmlns=\"urn:r\"><e DCa=\"DC PSU\" xmlns=\"\"/></r>");
    copy_from_reader("reader copy of a default namespace", "<e xmlns=\"urn:a\"><c/></e>");

    CoUninitialize();
    return 0;
}
