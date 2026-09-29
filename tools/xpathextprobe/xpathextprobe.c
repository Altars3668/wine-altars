/* xpathextprobe - what MSXML 6's XPath extension functions answer in selectNodes().
 *
 * Office's App-V runtime selects the opaque directories of a package with
 * ms:string-compare(@Long, "root\VFS\...", "", "i"); this prints what each call returns, as the
 * value that a predicate comparing it with -1, 0 and 1 lets through, or the error of the query.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later. */
#define COBJMACROS
#include <windows.h>
#include <ole2.h>
#include <initguid.h>
#include <msxml6.h>
#include <stdio.h>

static IXMLDOMDocument2 *doc;

static void eval(const WCHAR *expr)
{
    static const WCHAR *values[] = { L"-1", L"0", L"1" };
    WCHAR query[512];
    IXMLDOMNodeList *list;
    const WCHAR *answer = L"none";
    LONG len;
    HRESULT hr;
    int i;

    for (i = 0; i < ARRAY_SIZE(values); i++)
    {
        BSTR q;
        swprintf(query, ARRAY_SIZE(query), L"/r[%s = %s]", expr, values[i]);
        q = SysAllocString(query);
        hr = IXMLDOMDocument2_selectNodes(doc, q, &list);
        SysFreeString(q);
        if (FAILED(hr))
        {
            IErrorInfo *info;
            BSTR desc = NULL;
            printf("%-60ls error %#lx", expr, hr);
            if (GetErrorInfo(0, &info) == S_OK)
            {
                IErrorInfo_GetDescription(info, &desc);
                printf(" %ls", desc ? desc : L"");
                SysFreeString(desc);
                IErrorInfo_Release(info);
            }
            printf("\n");
            return;
        }
        IXMLDOMNodeList_get_length(list, &len);
        IXMLDOMNodeList_Release(list);
        if (len) answer = values[i];
    }
    printf("%-60ls %ls\n", expr, answer);
}

int main(void)
{
    static const WCHAR *exprs[] =
    {
        L"ms:string-compare('a', 'b')",
        L"ms:string-compare('b', 'a')",
        L"ms:string-compare('a', 'a')",
        L"ms:string-compare('a', 'A')",
        L"ms:string-compare('A', 'a')",
        L"ms:string-compare('a', 'B')",
        L"ms:string-compare('B', 'a')",
        L"ms:string-compare('a', 'A', '', 'i')",
        L"ms:string-compare('a', 'B', '', 'i')",
        L"ms:string-compare('a', 'A', '', 'u')",
        L"ms:string-compare('A', 'a', '', 'u')",
        L"ms:string-compare('a', 'A', '', 'iu')",
        L"ms:string-compare('a', 'A', '', 'x')",
        L"ms:string-compare('a', 'A', '', 'I')",
        L"ms:string-compare('a', 'A', '', '')",
        L"ms:string-compare('root\\VFS\\Common AppData', 'ROOT\\vfs\\common appdata', '', 'i')",
        L"ms:string-compare('a', 'b', 'en-US')",
        L"ms:string-compare('\x00e4', 'z', 'de-DE')",
        L"ms:string-compare('\x00e4', 'z', 'sv-SE')",
        L"ms:string-compare('\x00e4', 'z')",
        L"ms:string-compare('\x00e4', 'z', '')",
        L"ms:string-compare('a', 'b', 'xx-XX')",
        L"ms:string-compare('a', 'b', 'en')",
        L"ms:string-compare('10', '9')",
        L"ms:string-compare(10, 9)",
        L"ms:string-compare('', 'a')",
        L"ms:string-compare('', '')",
        L"ms:string-compare('a-b', 'ab')",
        L"ms:string-compare('co-op', 'coop')",
        L"ms:string-compare('a')",
        L"ms:string-compare('a', 'b', '', 'i', 'x')",
        L"ms:string-compare(/r/@x, 'b')",
        L"ms:string-compare(/r/e, 'B', '', 'i')",
        L"ms:string-compare('a', 'b', 'de-DE')",
        L"ms:string-compare('a', 'b', 'sv-SE')",
        L"ms:string-compare('a', 'b', 'fr-FR')",
        L"ms:string-compare('a', 'b', 'zh-CN')",
        L"ms:string-compare('a', 'b', 'EN-us')",
        L"ms:string-compare('a', 'b', 'en_US')",
        L"ms:string-compare('a', 'b', 'de')",
        L"ms:string-compare('\x00e4', 'z', 'en-US')",
        L"ms:string-compare('b', 'a', 'de-DE')",
        L"ms:string-compare('a', 'A', 'de-DE', 'u')",
        L"ms:string-compare('a', 'A', 'en-US', 'u')",
        L"ms:string-compare('\x00e4', 'a', '', 'i')",
        L"ms:string-compare('a', '\x00e1')",
        L"ms:string-compare('\x00e1', 'b')",
        L"ms:string-compare('a ', 'a')",
        L"ms:string-compare('A', 'b', '', 'u')",
        L"ms:string-compare('B', 'a', '', 'u')",
        L"ms:string-compare('ab', 'Ab', '', 'u')",
        L"ms:string-compare('Ab', 'ab', '', 'i')",
        L"ms:utc('2026-09-29T10:00:00')",
        L"ms:local-name('a:b')",
        L"ms:namespace-uri('a:b')",
        L"ms:number('1.5')",
        L"ms:type-is('string')",
    };
    VARIANT_BOOL ok;
    VARIANT v;
    int i;

    CoInitialize(NULL);
    if (FAILED(CoCreateInstance(&CLSID_DOMDocument60, NULL, CLSCTX_INPROC_SERVER, &IID_IXMLDOMDocument2, (void **)&doc)))
    {
        printf("no DOMDocument60\n");
        return 1;
    }
    IXMLDOMDocument2_loadXML(doc, (BSTR)L"<r x='b'><e>b</e></r>", &ok);
    V_VT(&v) = VT_BSTR;
    V_BSTR(&v) = SysAllocString(L"xmlns:ms='urn:schemas-microsoft-com:xslt'");
    printf("SelectionNamespaces: %#lx\n", IXMLDOMDocument2_setProperty(doc, (BSTR)L"SelectionNamespaces", v));
    for (i = 0; i < ARRAY_SIZE(exprs); i++) eval(exprs[i]);
    IXMLDOMDocument2_Release(doc);
    return 0;
}
