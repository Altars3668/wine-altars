/* msxslfuncprobe - what MSXML 6's extension functions (urn:schemas-microsoft-com:xslt) return.
 *
 * Part 1 prints, for each language name, what MLang makes of it (Rfc1766ToLcidW, the RFC 1766
 * table in the registry) next to whether ms:string-compare() takes it, to find the rule by which
 * Windows turns 'de-DE' down and takes 'de'.
 * Part 2 prints the value of each expression as an XSLT value-of, and whether the same expression
 * compiles in selectNodes() with the ms prefix declared in SelectionNamespaces.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later. */
#define COBJMACROS
#include <windows.h>
#include <ole2.h>
#include <initguid.h>
#include <msxml6.h>
#include <mlang.h>
#include <stdio.h>

static IXMLDOMDocument2 *doc;

static IXMLDOMDocument2 *new_doc(void)
{
    IXMLDOMDocument2 *d = NULL;
    CoCreateInstance(&CLSID_DOMDocument60, NULL, CLSCTX_INPROC_SERVER, &IID_IXMLDOMDocument2, (void **)&d);
    return d;
}

static BOOL load(IXMLDOMDocument2 *d, const WCHAR *xml)
{
    VARIANT_BOOL ok = VARIANT_FALSE;
    BSTR s = SysAllocString(xml);
    IXMLDOMDocument2_loadXML(d, s, &ok);
    SysFreeString(s);
    return ok == VARIANT_TRUE;
}

static void print_error(void)
{
    IErrorInfo *info;
    BSTR desc = NULL;
    int i;

    if (GetErrorInfo(0, &info) != S_OK) return;
    IErrorInfo_GetDescription(info, &desc);
    /* the description is localised; only its ASCII part tells anything here */
    printf(" [");
    for (i = 0; desc && desc[i]; i++) if (desc[i] >= 0x20 && desc[i] < 0x7f) putchar(desc[i]);
    printf("]");
    SysFreeString(desc);
    IErrorInfo_Release(info);
}

/* whether selectNodes() compiles and runs /r[expr] */
static void xpath_status(const WCHAR *expr)
{
    WCHAR query[512];
    IXMLDOMNodeList *list;
    LONG len = -1;
    HRESULT hr;
    BSTR q;

    swprintf(query, ARRAY_SIZE(query), L"/r[%s]", expr);
    q = SysAllocString(query);
    hr = IXMLDOMDocument2_selectNodes(doc, q, &list);
    SysFreeString(q);
    if (FAILED(hr))
    {
        printf("  xpath error %#lx", hr);
        print_error();
        return;
    }
    IXMLDOMNodeList_get_length(list, &len);
    IXMLDOMNodeList_Release(list);
    printf("  xpath %s", len ? "true" : "false");
}

/* the string an XSLT value-of makes of expr */
static void xslt_value(const WCHAR *expr)
{
    static const WCHAR fmt[] =
        L"<xsl:stylesheet version='1.0' xmlns:xsl='http://www.w3.org/1999/XSL/Transform'"
        L" xmlns:ms='urn:schemas-microsoft-com:xslt' xmlns:a='urn:style-a'>"
        L"<xsl:output method='text'/>"
        L"<xsl:template match='/'><xsl:for-each select='r'>[<xsl:value-of select=\"%s\"/>]</xsl:for-each></xsl:template>"
        L"</xsl:stylesheet>";
    WCHAR xsl[2048];
    IXMLDOMDocument2 *style = new_doc();
    BSTR out = NULL;
    HRESULT hr;
    int i;

    swprintf(xsl, ARRAY_SIZE(xsl), fmt, expr);
    if (!load(style, xsl))
    {
        printf("  xslt stylesheet does not load");
        IXMLDOMDocument2_Release(style);
        return;
    }
    hr = IXMLDOMDocument2_transformNode(doc, (IXMLDOMNode *)style, &out);
    if (FAILED(hr))
    {
        printf("  xslt error %#lx", hr);
        print_error();
    }
    else
    {
        printf("  xslt ");
        for (i = 0; out && out[i]; i++)
            if (out[i] >= 0x20 && out[i] < 0x7f) putchar(out[i]);
            else printf("\\u%04x", out[i]);
    }
    SysFreeString(out);
    IXMLDOMDocument2_Release(style);
}

static void languages(void)
{
    static const WCHAR *names[] =
    {
        L"en-US", L"EN-us", L"en", L"en-GB", L"en-AU", L"zh-CN", L"zh-TW", L"zh", L"zh-Hans",
        L"de", L"de-DE", L"de-AT", L"de-CH", L"fr", L"fr-FR", L"fr-CA", L"sv", L"sv-SE", L"sv-FI",
        L"ja", L"ja-JP", L"es", L"es-ES", L"es-MX", L"pt-BR", L"pt-PT", L"it", L"it-IT", L"ru",
        L"ru-RU", L"ko", L"ko-KR", L"nl", L"nl-NL", L"x-default", L"en_US", L"xx-XX", L"",
    };
    WCHAR query[256], value[256];
    IXMLDOMNodeList *list;
    int i;

    for (i = 0; i < ARRAY_SIZE(names); i++)
    {
        LCID lcid = 0, nlcid = LocaleNameToLCID(names[i], 0);
        HRESULT hr = Rfc1766ToLcidW(&lcid, names[i]);
        DWORD size = sizeof(value), type;
        WCHAR key[16];
        HRESULT qhr;
        BSTR q;

        swprintf(key, ARRAY_SIZE(key), L"%04X", nlcid);
        value[0] = 0;
        if (RegGetValueW(HKEY_CLASSES_ROOT, L"MIME\\Database\\Rfc1766", key, RRF_RT_REG_SZ, &type, value, &size))
            wcscpy(value, L"-");

        swprintf(query, ARRAY_SIZE(query), L"/r[ms:string-compare('a', 'b', '%s') = -1]", names[i]);
        q = SysAllocString(query);
        qhr = IXMLDOMDocument2_selectNodes(doc, q, &list);
        SysFreeString(q);
        if (SUCCEEDED(qhr)) IXMLDOMNodeList_Release(list);

        printf("%-10ls Rfc1766ToLcid %#lx %04lx  LocaleNameToLCID %04lx  Rfc1766[%04lx]=%ls  string-compare %s\n",
               names[i], hr, lcid, nlcid, nlcid, value, SUCCEEDED(qhr) ? "ok" : "fails");
    }
}

int main(void)
{
    static const WCHAR *exprs[] =
    {
        L"ms:utc('2026-09-29T10:00:00')",
        L"ms:utc('2026-09-29T10:00:00Z')",
        L"ms:utc('2026-09-29T10:00:00+08:00')",
        L"ms:utc('2026-09-29T10:00:00.5-05:30')",
        L"ms:utc('2026-09-29T10:00:00.1234567')",
        L"ms:utc('2026-09-29')",
        L"ms:utc('10:00:00')",
        L"ms:utc('2026-09')",
        L"ms:utc('2026')",
        L"ms:utc('2026-02-30T00:00:00')",
        L"ms:utc('2026-09-29T24:00:00')",
        L"ms:utc(' 2026-09-29T10:00:00 ')",
        L"ms:utc('abc')",
        L"ms:utc('')",
        L"ms:utc(20260929)",
        L"ms:utc()",
        L"ms:local-name('a:b')",
        L"ms:local-name('b')",
        L"ms:local-name('a:b:c')",
        L"ms:local-name(':b')",
        L"ms:local-name('a:')",
        L"ms:local-name('')",
        L"ms:local-name(' a:b ')",
        L"ms:local-name()",
        L"ms:namespace-uri('a:b')",
        L"ms:namespace-uri('s:b')",
        L"ms:namespace-uri('b')",
        L"ms:namespace-uri('x:b')",
        L"ms:namespace-uri('xml:lang')",
        L"ms:namespace-uri('xsl:template')",
        L"ms:namespace-uri('')",
        L"ms:namespace-uri()",
        L"ms:number('1.5')",
        L"ms:number(' 1.5 ')",
        L"ms:number('1,5')",
        L"ms:number('1e3')",
        L"ms:number('1E-2')",
        L"ms:number('INF')",
        L"ms:number('-INF')",
        L"ms:number('NaN')",
        L"ms:number('+1')",
        L"ms:number('-0')",
        L"ms:number('.5')",
        L"ms:number('5.')",
        L"ms:number('abc')",
        L"ms:number('')",
        L"ms:number(1.5)",
        L"ms:number()",
        L"number('1e3')",
        L"number('INF')",
        L"ms:format-date('2026-09-29T10:05:03', 'yyyy-MM-dd')",
        L"ms:format-date('2026-09-29T10:05:03', 'dddd, MMMM d, yyyy', 'en-US')",
        L"ms:format-date('2026-09-29', 'ddd dd MMM yy', 'de')",
        L"ms:format-date('2026-09-29', 'ddd dd MMM yy', 'de-DE')",
        L"ms:format-date('2026-09-29', \"d 'of' M, gg\")",
        L"ms:format-date('2026-09-29', '')",
        L"ms:format-date('2026-09-29')",
        L"ms:format-date('2026-09-29', 'yyyy', 'xx-XX')",
        L"ms:format-date('bad', 'yyyy')",
        L"ms:format-date('2026-09-29T10:05:03+08:00', 'yyyy-MM-dd')",
        L"ms:format-time('2026-09-29T13:05:03', 'hh:mm:ss tt')",
        L"ms:format-time('2026-09-29T13:05:03', 'HH:mm', 'en-US')",
        L"ms:format-time('13:05:03', 'h:m:s t')",
        L"ms:format-time('2026-09-29T13:05:03.25', 'HH:mm:ss')",
        L"ms:format-time('2026-09-29T13:05:03')",
        L"ms:format-time('2026-09-29T13:05:03', '', 'zh-CN')",
        L"ms:format-time('bad', 'HH')",
        L"ms:string-compare('a', 'b')",
        L"ms:string-compare('a', 'A', '', 'i')",
        L"ms:type-is('string')",
        L"ms:type-local-name()",
        L"ms:type-namespace-uri()",
        L"ms:schema-info-available()",
    };
    VARIANT v;
    int i;

    CoInitialize(NULL);
    if (!(doc = new_doc()))
    {
        printf("no DOMDocument60\n");
        return 1;
    }
    load(doc, L"<r xmlns:s='urn:source-s' x='b'><e>b</e></r>");
    V_VT(&v) = VT_BSTR;
    V_BSTR(&v) = SysAllocString(L"xmlns:ms='urn:schemas-microsoft-com:xslt' xmlns:a='urn:selection-a'");
    printf("SelectionNamespaces: %#lx\n", IXMLDOMDocument2_setProperty(doc, (BSTR)L"SelectionNamespaces", v));
    VariantClear(&v);

    languages();
    for (i = 0; i < ARRAY_SIZE(exprs); i++)
    {
        printf("%-72ls", exprs[i]);
        xslt_value(exprs[i]);
        xpath_status(exprs[i]);
        printf("\n");
    }
    IXMLDOMDocument2_Release(doc);
    CoUninitialize();
    return 0;
}
