/*
 * msxmlpropprobe - what the DOM document properties Office sets do.
 *
 * Word sets ProhibitDTD and MaxElementDepth on its documents, which Wine
 * ignored.  For DOMDocument30 and DOMDocument60 this prints each property's
 * default, and loads documents with a DTD, with an entity, and nested
 * deeper than a limit, with the parse error each gives.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <msxml6.h>
#include <stdio.h>

DEFINE_GUID(CLSID_DOMDocument30_, 0xf5078f32, 0xc551, 0x11d3, 0x89, 0xb9, 0x00, 0x00, 0xf8, 0x1f, 0xe2, 0x21);

static const WCHAR *props[] =
{
    L"ProhibitDTD", L"MaxElementDepth", L"ResolveExternals", L"NewParser", L"AllowXsltScript",
    L"NormalizeAttributeValues", L"AllowDocumentFunction", L"UseInlineSchema", L"MaxXMLSize",
};

static void load(IXMLDOMDocument2 *doc, const char *what, const WCHAR *xml)
{
    IXMLDOMParseError *err;
    VARIANT_BOOL ok = VARIANT_TRUE;
    BSTR str = SysAllocString(xml), reason = NULL, text = NULL;
    long code = 0, line = 0, pos = 0;
    IXMLDOMElement *root = NULL;
    HRESULT hr;

    hr = IXMLDOMDocument2_loadXML(doc, str, &ok);
    printf("    %-36s %#lx %s", what, hr, ok ? "loaded" : "not loaded");
    if (!ok && SUCCEEDED(IXMLDOMDocument2_get_parseError(doc, &err)))
    {
        IXMLDOMParseError_get_errorCode(err, &code);
        IXMLDOMParseError_get_reason(err, &reason);
        IXMLDOMParseError_get_line(err, &line);
        IXMLDOMParseError_get_linepos(err, &pos);
        printf(", error %#lx at %ld:%ld: %ls", code, line, pos, reason ? reason : L"");
        SysFreeString(reason);
        IXMLDOMParseError_Release(err);
    }
    else if (ok && SUCCEEDED(IXMLDOMDocument2_get_documentElement(doc, &root)) && root)
    {
        IXMLDOMElement_get_text(root, &text);
        printf(", text \"%ls\"", text ? text : L"");
        SysFreeString(text);
        IXMLDOMElement_Release(root);
    }
    printf("\n");
    SysFreeString(str);
}

static void probe(const char *name, REFCLSID clsid)
{
    IXMLDOMDocument2 *doc;
    unsigned int i;
    VARIANT v;
    HRESULT hr;
    BSTR prop;

    hr = CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, &IID_IXMLDOMDocument2, (void **)&doc);
    printf("%s: %#lx\n", name, hr);
    if (FAILED(hr)) return;
    IXMLDOMDocument2_put_async(doc, VARIANT_FALSE);
    for (i = 0; i < ARRAYSIZE(props); i++)
    {
        prop = SysAllocString(props[i]);
        VariantInit(&v);
        hr = IXMLDOMDocument2_getProperty(doc, prop, &v);
        printf("  %-26ls %#lx", props[i], hr);
        if (SUCCEEDED(hr))
        {
            if (V_VT(&v) == VT_BOOL) printf(" VT_BOOL %d", V_BOOL(&v));
            else if (V_VT(&v) == VT_I4) printf(" VT_I4 %ld", V_I4(&v));
            else printf(" vt %d", V_VT(&v));
        }
        printf("\n");
        VariantClear(&v);
        SysFreeString(prop);
    }

    load(doc, "a DTD, by default", L"<!DOCTYPE a [<!ELEMENT a (#PCDATA)>]><a>x</a>");
    load(doc, "an entity, by default", L"<!DOCTYPE a [<!ENTITY e 'y'>]><a>&e;</a>");
    prop = SysAllocString(L"ProhibitDTD");
    V_VT(&v) = VT_BOOL; V_BOOL(&v) = VARIANT_TRUE;
    hr = IXMLDOMDocument2_setProperty(doc, prop, v);
    printf("  set ProhibitDTD true %#lx\n", hr);
    load(doc, "a DTD, prohibited", L"<!DOCTYPE a [<!ELEMENT a (#PCDATA)>]><a>x</a>");
    load(doc, "an empty DTD, prohibited", L"<!DOCTYPE a><a>x</a>");
    load(doc, "no DTD, prohibited", L"<a>x</a>");
    V_BOOL(&v) = VARIANT_FALSE;
    hr = IXMLDOMDocument2_setProperty(doc, prop, v);
    printf("  set ProhibitDTD false %#lx\n", hr);
    load(doc, "an entity, allowed", L"<!DOCTYPE a [<!ENTITY e 'y'>]><a>&e;</a>");
    V_VT(&v) = VT_I4; V_I4(&v) = 1;
    hr = IXMLDOMDocument2_setProperty(doc, prop, v);
    printf("  set ProhibitDTD VT_I4 1 %#lx\n", hr);
    VariantInit(&v);
    IXMLDOMDocument2_getProperty(doc, prop, &v);
    printf("  now vt %d value %d\n", V_VT(&v), V_VT(&v) == VT_BOOL ? V_BOOL(&v) : -1);
    SysFreeString(prop);

    prop = SysAllocString(L"MaxElementDepth");
    V_VT(&v) = VT_I4; V_I4(&v) = 3;
    hr = IXMLDOMDocument2_setProperty(doc, prop, v);
    printf("  set MaxElementDepth 3 %#lx\n", hr);
    load(doc, "3 deep, at most 3", L"<a><b><c/></b></a>");
    load(doc, "4 deep, at most 3", L"<a><b><c><d/></c></b></a>");
    V_I4(&v) = 0;
    hr = IXMLDOMDocument2_setProperty(doc, prop, v);
    printf("  set MaxElementDepth 0 %#lx\n", hr);
    V_I4(&v) = -1;
    hr = IXMLDOMDocument2_setProperty(doc, prop, v);
    printf("  set MaxElementDepth -1 %#lx\n", hr);
    V_VT(&v) = VT_BSTR; V_BSTR(&v) = SysAllocString(L"5");
    hr = IXMLDOMDocument2_setProperty(doc, prop, v);
    printf("  set MaxElementDepth \"5\" %#lx\n", hr);
    VariantClear(&v);
    VariantInit(&v);
    IXMLDOMDocument2_getProperty(doc, prop, &v);
    printf("  now vt %d value %ld\n", V_VT(&v), V_VT(&v) == VT_I4 ? V_I4(&v) : -1);
    SysFreeString(prop);
    IXMLDOMDocument2_Release(doc);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    CoInitialize(NULL);
    probe("DOMDocument30", &CLSID_DOMDocument30_);
    probe("DOMDocument60", &CLSID_DOMDocument60);
    printf("done\n");
    return 0;
}
