/*
 * saxdtdprobe - what the SAX reader's prohibit-dtd feature does.
 *
 * Word turns prohibit-dtd on for the SAX readers it makes, and Wine kept the
 * value and parsed DTDs anyway.  For SAXXMLReader30 and 60 this prints the
 * feature's default, then parses a document with a DTD with the feature off
 * and on, and prints what the error handler hears and what parse returns.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <msxml6.h>
#include <stdio.h>

DEFINE_GUID(CLSID_SAXXMLReader30_, 0x3124c396, 0xfb13, 0x4836, 0xa6, 0xad, 0x13, 0x17, 0xf1, 0x71, 0x36, 0x88);

static HRESULT WINAPI eh_QueryInterface(ISAXErrorHandler *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ISAXErrorHandler)) { *out = iface; return S_OK; }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI eh_AddRef(ISAXErrorHandler *iface) { return 2; }
static ULONG WINAPI eh_Release(ISAXErrorHandler *iface) { return 1; }
static void report(const char *kind, ISAXLocator *locator, HRESULT code)
{
    int line = -1, column = -1;
    if (locator)
    {
        ISAXLocator_getLineNumber(locator, &line);
        ISAXLocator_getColumnNumber(locator, &column);
    }
    printf("    %s %#lx at %d:%d\n", kind, code, line, column);
}
static HRESULT WINAPI eh_error(ISAXErrorHandler *iface, ISAXLocator *locator, const WCHAR *message, HRESULT code)
{ report("error", locator, code); return S_OK; }
static HRESULT WINAPI eh_fatalError(ISAXErrorHandler *iface, ISAXLocator *locator, const WCHAR *message, HRESULT code)
{ report("fatalError", locator, code); return S_OK; }
static HRESULT WINAPI eh_ignorableWarning(ISAXErrorHandler *iface, ISAXLocator *locator, const WCHAR *message, HRESULT code)
{ report("ignorableWarning", locator, code); return S_OK; }
static const ISAXErrorHandlerVtbl eh_vtbl = { eh_QueryInterface, eh_AddRef, eh_Release, eh_error, eh_fatalError, eh_ignorableWarning };
static ISAXErrorHandler error_handler = { &eh_vtbl };

static void parse(ISAXXMLReader *reader, const char *what, const WCHAR *xml)
{
    VARIANT v;
    HRESULT hr;

    V_VT(&v) = VT_BSTR;
    V_BSTR(&v) = SysAllocString(xml);
    printf("  %s:\n", what);
    hr = ISAXXMLReader_parse(reader, v);
    printf("    parse %#lx\n", hr);
    VariantClear(&v);
}

static void probe(const char *name, REFCLSID clsid)
{
    ISAXXMLReader *reader;
    VARIANT_BOOL value = 2;
    HRESULT hr;

    hr = CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, &IID_ISAXXMLReader, (void **)&reader);
    printf("%s: %#lx\n", name, hr);
    if (FAILED(hr)) return;
    ISAXXMLReader_putErrorHandler(reader, &error_handler);
    hr = ISAXXMLReader_getFeature(reader, L"prohibit-dtd", &value);
    printf("  prohibit-dtd %#lx %d\n", hr, value);
    parse(reader, "a DTD, by default", L"<!DOCTYPE a [<!ELEMENT a (#PCDATA)>]><a>x</a>");
    hr = ISAXXMLReader_putFeature(reader, L"prohibit-dtd", VARIANT_TRUE);
    printf("  put prohibit-dtd true %#lx\n", hr);
    parse(reader, "a DTD, prohibited", L"<!DOCTYPE a [<!ELEMENT a (#PCDATA)>]><a>x</a>");
    parse(reader, "an empty DTD, prohibited", L"<!DOCTYPE a><a>x</a>");
    parse(reader, "no DTD, prohibited", L"<a>x</a>");
    hr = ISAXXMLReader_putFeature(reader, L"prohibit-dtd", VARIANT_FALSE);
    printf("  put prohibit-dtd false %#lx\n", hr);
    parse(reader, "a DTD, allowed", L"<!DOCTYPE a [<!ELEMENT a (#PCDATA)>]><a>x</a>");
    ISAXXMLReader_Release(reader);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    CoInitialize(NULL);
    probe("SAXXMLReader30", &CLSID_SAXXMLReader30_);
    probe("SAXXMLReader60", &CLSID_SAXXMLReader60);
    printf("done\n");
    return 0;
}
