#define COBJMACROS
#include <windows.h>
#include <ole2.h>
#include <initguid.h>
#include <msxml6.h>
#include <stdio.h>

int main(void)
{
    static const WCHAR *sources[] =
    {
        L"<xs:schema xmlns:xs='http://www.w3.org/2001/XMLSchema' targetNamespace='urn:test'><xs:element name='root' type='xs:int'/></xs:schema>",
        L"<xs:schema xmlns:xs='http://www.w3.org/2001/XMLSchema' targetNamespace='urn:test'><xs:element name='root' type='xs:undefinedType'/></xs:schema>",
    };
    unsigned int mode, i;

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    CoInitialize(NULL);
    for (mode = 0; mode < 2; ++mode)
    for (i = 0; i < ARRAYSIZE(sources); ++i)
    {
        IXMLDOMSchemaCollection2 *cache;
        IXMLDOMDocument2 *doc;
        CLSID clsid;
        VARIANT value;
        VARIANT_BOOL loaded;
        BSTR text, uri;
        HRESULT hr, add_hr, validate_hr;

        CLSIDFromProgID(L"Msxml2.XMLSchemaCache.6.0", &clsid);
        if (FAILED(CoCreateInstance(&clsid, NULL, CLSCTX_INPROC_SERVER, &IID_IXMLDOMSchemaCollection2, (void **)&cache))) return 1;
        hr = IXMLDOMSchemaCollection2_put_validateOnLoad(cache, mode ? VARIANT_TRUE : VARIANT_FALSE);
        CLSIDFromProgID(L"Msxml2.DOMDocument.6.0", &clsid);
        if (FAILED(CoCreateInstance(&clsid, NULL, CLSCTX_INPROC_SERVER, &IID_IXMLDOMDocument2, (void **)&doc))) return 1;
        text = SysAllocString(sources[i]);
        IXMLDOMDocument2_loadXML(doc, text, &loaded);
        SysFreeString(text);
        VariantInit(&value);
        V_VT(&value) = VT_DISPATCH;
        V_DISPATCH(&value) = (IDispatch *)doc;
        uri = SysAllocString(L"urn:test");
        add_hr = IXMLDOMSchemaCollection2_add(cache, uri, value);
        validate_hr = IXMLDOMSchemaCollection2_validate(cache);
        printf("mode=%u invalid=%u set=%#lx add=%#lx validate=%#lx\n", mode, i, hr, add_hr, validate_hr);
        SysFreeString(uri);
        IXMLDOMDocument2_Release(doc);
        IXMLDOMSchemaCollection2_Release(cache);
    }
    CoUninitialize();
    puts("done");
    return 0;
}
