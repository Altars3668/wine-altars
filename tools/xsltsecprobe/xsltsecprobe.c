/* xsltsecprobe - what AllowXsltScript and AllowDocumentFunction do to a transformation.
 *
 * MSXML 6 has both off by default and MSXML 3 both on.  For each version this transforms a
 * one-element document with a stylesheet that calls a JScript function from an msxsl:script block --
 * once with the script as plain text and once in a CDATA section -- and with one that counts the
 * nodes of document(''), first with the stylesheet document's defaults, then with the property set
 * to true and to false on it, and prints what transformNode() answers: the result text, or the error
 * and its description.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <msxml6.h>
#include <stdio.h>

DEFINE_GUID(CLSID_DOMDocument30_, 0xf5078f32, 0xc551, 0x11d3, 0x89, 0xb9, 0x00, 0x00, 0xf8, 0x1f, 0xe2, 0x21);

static const WCHAR script_xsl[] =
    L"<xsl:stylesheet version='1.0' xmlns:xsl='http://www.w3.org/1999/XSL/Transform'"
    L" xmlns:msxsl='urn:schemas-microsoft-com:xslt' xmlns:u='urn:user'>"
    L"<msxsl:script language='JScript' implements-prefix='u'>function f() { return 'scripted'; }</msxsl:script>"
    L"<xsl:output method='text'/>"
    L"<xsl:template match='/'>[<xsl:value-of select='u:f()'/>]</xsl:template>"
    L"</xsl:stylesheet>";

static const WCHAR script_cdata_xsl[] =
    L"<xsl:stylesheet version='1.0' xmlns:xsl='http://www.w3.org/1999/XSL/Transform'"
    L" xmlns:msxsl='urn:schemas-microsoft-com:xslt' xmlns:u='urn:user'>"
    L"<msxsl:script language='JScript' implements-prefix='u'><![CDATA[function f() { return 'scripted'; }]]>"
    L"</msxsl:script>"
    L"<xsl:output method='text'/>"
    L"<xsl:template match='/'>[<xsl:value-of select='u:f()'/>]</xsl:template>"
    L"</xsl:stylesheet>";

static const WCHAR document_xsl[] =
    L"<xsl:stylesheet version='1.0' xmlns:xsl='http://www.w3.org/1999/XSL/Transform'>"
    L"<xsl:output method='text'/>"
    L"<xsl:template match='/'>[<xsl:value-of select=\"count(document('')/*/*)\"/>]</xsl:template>"
    L"</xsl:stylesheet>";

static IXMLDOMDocument2 *load(REFCLSID clsid, const WCHAR *xml)
{
    IXMLDOMDocument2 *doc = NULL;
    VARIANT_BOOL ok = VARIANT_FALSE;
    BSTR text;

    if (FAILED(CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, &IID_IXMLDOMDocument2, (void **)&doc)))
        return NULL;
    IXMLDOMDocument2_put_async(doc, VARIANT_FALSE);
    text = SysAllocString(xml);
    IXMLDOMDocument2_loadXML(doc, text, &ok);
    SysFreeString(text);
    if (ok != VARIANT_TRUE) printf("  load failed\n");
    return doc;
}

static void transform(REFCLSID clsid, const WCHAR *xsl, const char *what, const WCHAR *property, int value)
{
    IXMLDOMDocument2 *source, *style;
    IErrorInfo *info;
    BSTR out = NULL, desc = NULL, name;
    VARIANT v;
    HRESULT hr;

    source = load(clsid, L"<r><a/></r>");
    style = load(clsid, xsl);
    if (!source || !style) return;
    if (value >= 0)
    {
        name = SysAllocString(property);
        V_VT(&v) = VT_BOOL;
        V_BOOL(&v) = value ? VARIANT_TRUE : VARIANT_FALSE;
        hr = IXMLDOMDocument2_setProperty(style, name, v);
        SysFreeString(name);
        if (FAILED(hr)) printf("  setProperty %#lx\n", hr);
    }
    SetErrorInfo(0, NULL);
    hr = IXMLDOMDocument2_transformNode(source, (IXMLDOMNode *)style, &out);
    printf("  %-8s %-22ls %-8s transformNode %#lx", what, property, value < 0 ? "default" : value ? "true" : "false", hr);
    if (SUCCEEDED(hr)) printf("  result %ls", out ? out : L"<null>");
    else if (GetErrorInfo(0, &info) == S_OK && info)
    {
        IErrorInfo_GetDescription(info, &desc);
        printf("  description %ls", desc ? desc : L"<none>");
        SysFreeString(desc);
        IErrorInfo_Release(info);
    }
    printf("\n");
    SysFreeString(out);
    IXMLDOMDocument2_Release(style);
    IXMLDOMDocument2_Release(source);
}

static void probe(const char *name, REFCLSID clsid)
{
    int value;

    printf("%s\n", name);
    for (value = -1; value <= 1; value++) transform(clsid, script_xsl, "text", L"AllowXsltScript", value);
    for (value = -1; value <= 1; value++) transform(clsid, script_cdata_xsl, "CDATA", L"AllowXsltScript", value);
    for (value = -1; value <= 1; value++) transform(clsid, document_xsl, "document", L"AllowDocumentFunction", value);
}

int main(void)
{
    CoInitialize(NULL);
    probe("DOMDocument30", &CLSID_DOMDocument30_);
    probe("DOMDocument60", &CLSID_DOMDocument60);
    CoUninitialize();
    return 0;
}
