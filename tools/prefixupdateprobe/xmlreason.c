#define COBJMACROS
#include <windows.h>
#include <ole2.h>
#include <initguid.h>
#include <msxml6.h>
#include <stdio.h>

int main(void)
{
    static const WCHAR *xmls[] =
    {
        L"<root/>",
        L"<!DOCTYPE root [<!ELEMENT root EMPTY>]><root/>",
        L"<!DOCTYPE root [<!ELEMENT root (child)><!ELEMENT child EMPTY>]><root/>",
        L"<root>\n  <child>value</root>",
        L"<?xml version='1.0'?>\n<!DOCTYPE a [\n<!NOTATION notation0 PUBLIC 'pub-id' 'sys-id'>\n<!ENTITY ent1 SYSTEM 'sys-id' NDATA notation0>\n<!ENTITY % ent1 'sys-id'>\n<!ENTITY % ent2 SYSTEM 'sys-id-2'>\n<!NOTATION notation1 PUBLIC 'pub-id' 'sys-id'>\n<!ELEMENT a ANY>\n]>\n<a></a>",
    };
    static const WCHAR *progids[] = {L"Msxml2.DOMDocument.3.0", L"Msxml2.DOMDocument.6.0"};
    unsigned int version, newer, i;

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    CoInitialize(NULL);
    for (version = 0; version < ARRAYSIZE(progids); ++version)
    for (newer = 0; newer < (version ? 2u : 1u); ++newer)
    for (i = 0; i < ARRAYSIZE(xmls); ++i)
    {
        IXMLDOMDocument2 *doc;
        IXMLDOMParseError *error;
        CLSID clsid;
        VARIANT_BOOL loaded;
        VARIANT option;
        LONG code, line, column;
        BSTR name, xml, reason = NULL;
        HRESULT hr, lh, ph;

        if (FAILED(CLSIDFromProgID(progids[version], &clsid)) ||
            FAILED(CoCreateInstance(&clsid, NULL, CLSCTX_INPROC_SERVER, &IID_IXMLDOMDocument2, (void **)&doc))) continue;
        VariantInit(&option);
        V_VT(&option) = VT_BOOL;
        V_BOOL(&option) = VARIANT_FALSE;
        name = SysAllocString(L"ProhibitDTD");
        IXMLDOMDocument2_setProperty(doc, name, option);
        SysFreeString(name);
        V_BOOL(&option) = newer ? VARIANT_TRUE : VARIANT_FALSE;
        name = SysAllocString(L"NewParser");
        IXMLDOMDocument2_setProperty(doc, name, option);
        SysFreeString(name);
        IXMLDOMDocument2_put_validateOnParse(doc, VARIANT_TRUE);
        xml = SysAllocString(xmls[i]);
        hr = IXMLDOMDocument2_loadXML(doc, xml, &loaded);
        SysFreeString(xml);
        IXMLDOMDocument2_get_parseError(doc, &error);
        IXMLDOMParseError_get_errorCode(error, &code);
        lh = IXMLDOMParseError_get_line(error, &line);
        ph = IXMLDOMParseError_get_linepos(error, &column);
        IXMLDOMParseError_get_reason(error, &reason);
        printf("version=%u new=%u case=%u hr=%#lx loaded=%d code=%#lx line=%ld/%#lx column=%ld/%#lx reason=%ls\n",
               version ? 6u : 3u, newer, i, hr, loaded, (DWORD)code, line, lh, column, ph, reason ? reason : L"");
        SysFreeString(reason);
        IXMLDOMParseError_Release(error);
        IXMLDOMDocument2_Release(doc);
    }
    CoUninitialize();
    puts("done");
    return 0;
}
