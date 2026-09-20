/*
 * xmlprobe -- what MSXML makes of a namespace prefix nobody declared.
 *
 * Office's Click-to-Run merges AppX manifests, and the merge fails under Wine:
 *
 *   C2R::Orchestration::TryRefreshMergedManifest
 *     HResultOnly (removeChild failed , Error:0x80070057)
 *
 * with, in the same run, 584 of
 *
 *   warn:msxml:doparse Namespace prefix appv is not defined
 *
 * AppXManifest.xml does declare xmlns:appv on its root, so whatever is being
 * parsed is a fragment that carries no declaration.  MSXML is not
 * namespace-strict: it keeps "appv:Foo" as the node's name and reports the
 * prefix.  libxml2 raises the undefined-prefix error and recovers, and what it
 * recovers to is the question this answers -- because if the recovered tree is
 * shaped differently, a node the caller holds is no longer a child of the
 * parent it means to remove it from, which is exactly the E_INVALIDARG that
 * dlls/msxml3/node.c returns.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -municode \
 *       -o xmlprobe.exe xmlprobe.c -lole32 -loleaut32 -luuid
 *
 *   xmlprobe            run every case
 *   xmlprobe <file>     also parse that file and report its root's children
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <objbase.h>
#include <msxml2.h>
#include <stdio.h>
#include <stdarg.h>

static void out(const WCHAR *s)
{
    char u[8192];
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, u, sizeof(u), NULL, NULL);
    if (n > 0) fwrite(u, 1, n - 1, stdout);
    fflush(stdout);
}

static void outf(const WCHAR *fmt, ...)
{
    WCHAR b[8192];
    va_list ap;
    va_start(ap, fmt);
    vswprintf(b, ARRAYSIZE(b), fmt, ap);
    va_end(ap);
    out(b);
}

/* Every string MSXML hands back has to come back to it. */
static void say_bstr(const WCHAR *label, BSTR s)
{
    outf(L"%ls=%ls ", label, s ? s : L"<null>");
}

static IXMLDOMDocument *new_doc(void)
{
    IXMLDOMDocument *doc = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_DOMDocument60, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IXMLDOMDocument, (void **)&doc);
    if (FAILED(hr))
        hr = CoCreateInstance(&CLSID_DOMDocument, NULL, CLSCTX_INPROC_SERVER,
                              &IID_IXMLDOMDocument, (void **)&doc);
    if (FAILED(hr)) { outf(L"  CoCreateInstance(DOMDocument) 0x%08lx\n", hr); return NULL; }
    return doc;
}

/* Print one element: what it is called, and who it says its parent is. */
static void describe(IXMLDOMNode *node, const WCHAR *indent)
{
    BSTR name = NULL, prefix = NULL, uri = NULL, base = NULL, pname = NULL;
    IXMLDOMNode *parent = NULL;

    IXMLDOMNode_get_nodeName(node, &name);
    IXMLDOMNode_get_prefix(node, &prefix);
    IXMLDOMNode_get_namespaceURI(node, &uri);
    IXMLDOMNode_get_baseName(node, &base);
    if (SUCCEEDED(IXMLDOMNode_get_parentNode(node, &parent)) && parent)
        IXMLDOMNode_get_nodeName(parent, &pname);

    outf(L"%ls", indent);
    say_bstr(L"nodeName", name);
    say_bstr(L"prefix", prefix);
    say_bstr(L"baseName", base);
    say_bstr(L"nsURI", uri);
    say_bstr(L"parent", pname);
    outf(L"\n");

    SysFreeString(name); SysFreeString(prefix); SysFreeString(uri);
    SysFreeString(base); SysFreeString(pname);
    if (parent) IXMLDOMNode_Release(parent);
}

/* Load some XML, walk the root's children, then try to remove each one --
 * which is the call Click-to-Run's merge makes and Wine refuses. */
static void run_case(const WCHAR *title, const WCHAR *xml)
{
    IXMLDOMDocument *doc;
    IXMLDOMElement *root = NULL;
    IXMLDOMNodeList *kids = NULL;
    VARIANT_BOOL ok = VARIANT_FALSE;
    BSTR src;
    LONG i, n = 0;
    HRESULT hr;

    outf(L"== %ls\n", title);
    if (!(doc = new_doc())) return;

    src = SysAllocString(xml);
    hr = IXMLDOMDocument_loadXML(doc, src, &ok);
    SysFreeString(src);
    outf(L"  loadXML 0x%08lx  成功=%ls\n", hr, ok == VARIANT_TRUE ? L"是" : L"否");

    if (ok != VARIANT_TRUE)
    {
        IXMLDOMParseError *err = NULL;
        if (SUCCEEDED(IXMLDOMDocument_get_parseError(doc, &err)) && err)
        {
            BSTR reason = NULL;
            LONG code = 0;
            IXMLDOMParseError_get_errorCode(err, &code);
            IXMLDOMParseError_get_reason(err, &reason);
            outf(L"  parseError 0x%08lx %ls", code, reason ? reason : L"");
            SysFreeString(reason);
            IXMLDOMParseError_Release(err);
        }
        outf(L"\n\n");
        IXMLDOMDocument_Release(doc);
        return;
    }

    if (FAILED(IXMLDOMDocument_get_documentElement(doc, &root)) || !root)
    {
        outf(L"  没有 documentElement\n\n");
        IXMLDOMDocument_Release(doc);
        return;
    }

    outf(L"  根:\n");
    describe((IXMLDOMNode *)root, L"    ");

    if (SUCCEEDED(IXMLDOMElement_get_childNodes(root, &kids)) && kids)
    {
        IXMLDOMNodeList_get_length(kids, &n);
        outf(L"  子节点 %ld 个:\n", n);
        for (i = 0; i < n; i++)
        {
            IXMLDOMNode *kid = NULL;
            if (SUCCEEDED(IXMLDOMNodeList_get_item(kids, i, &kid)) && kid)
            {
                describe(kid, L"    ");
                IXMLDOMNode_Release(kid);
            }
        }

        /* removeChild each one, taking them from the front as the list shrinks. */
        outf(L"  removeChild:\n");
        for (i = 0; i < n; i++)
        {
            IXMLDOMNode *kid = NULL, *gone = NULL;
            BSTR name = NULL;

            if (FAILED(IXMLDOMNodeList_get_item(kids, 0, &kid)) || !kid) break;
            IXMLDOMNode_get_nodeName(kid, &name);
            hr = IXMLDOMNode_removeChild((IXMLDOMNode *)root, kid, &gone);
            outf(L"    %-24ls 0x%08lx%ls\n", name ? name : L"?", hr,
                 hr == E_INVALIDARG ? L"   <- E_INVALIDARG，就是这个" : L"");
            SysFreeString(name);
            if (gone) IXMLDOMNode_Release(gone);
            IXMLDOMNode_Release(kid);
            if (FAILED(hr)) break;
        }
        IXMLDOMNodeList_Release(kids);
    }

    IXMLDOMElement_Release(root);
    IXMLDOMDocument_Release(doc);
    outf(L"\n");
}


/* Merging two manifests means moving nodes between documents.  MSXML lets
 * appendChild adopt a node from another document; if the adopted node's parent
 * is not updated to the new one, the next removeChild on that parent answers
 * "not a child" -- the E_INVALIDARG Click-to-Run reports. */
static void run_cross_document(void)
{
    IXMLDOMDocument *a = new_doc(), *b = new_doc();
    IXMLDOMElement *ra = NULL, *rb = NULL;
    IXMLDOMNode *moved = NULL, *kid = NULL, *gone = NULL, *parent = NULL;
    VARIANT_BOOL ok = VARIANT_FALSE;
    BSTR s;
    HRESULT hr;

    outf(L"== 跨文档搬节点（合并清单的做法）\n");
    if (!a || !b) return;

    s = SysAllocString(L"<Package><Extensions/></Package>");
    IXMLDOMDocument_loadXML(a, s, &ok); SysFreeString(s);
    s = SysAllocString(L"<Package xmlns:appv=\"urn:appv\"><appv:Extension Id=\"x\"/></Package>");
    IXMLDOMDocument_loadXML(b, s, &ok); SysFreeString(s);

    IXMLDOMDocument_get_documentElement(a, &ra);
    IXMLDOMDocument_get_documentElement(b, &rb);
    if (!ra || !rb) return;

    IXMLDOMElement_get_firstChild(rb, &kid);
    if (!kid) return;
    outf(L"  来源（B 文档）:\n");
    describe(kid, L"    ");

    hr = IXMLDOMElement_appendChild(ra, kid, &moved);
    outf(L"  appendChild 到 A  0x%08lx\n", hr);
    if (moved)
    {
        outf(L"  搬过去之后:\n");
        describe(moved, L"    ");
        IXMLDOMNode_get_parentNode(moved, &parent);
        outf(L"  parentNode 指针 %ls A 的根\n", parent == (IXMLDOMNode *)ra ? L"==" : L"!=");
        if (parent) IXMLDOMNode_Release(parent);

        hr = IXMLDOMElement_removeChild(ra, moved, &gone);
        outf(L"  A.removeChild(搬来的)  0x%08lx%ls\n", hr,
             hr == E_INVALIDARG ? L"   <- 复现了" : L"");
        if (gone) IXMLDOMNode_Release(gone);
        IXMLDOMNode_Release(moved);
    }

    /* The same move done the other way: clone into A, then remove the original
     * from B -- also a merge, also a removeChild. */
    IXMLDOMNode_Release(kid);
    kid = NULL;
    IXMLDOMElement_get_firstChild(rb, &kid);
    if (kid)
    {
        hr = IXMLDOMElement_removeChild(rb, kid, &gone);
        outf(L"  B.removeChild(原件)    0x%08lx\n", hr);
        if (gone) IXMLDOMNode_Release(gone);
        IXMLDOMNode_Release(kid);
    }
    else outf(L"  B 的根已经没有子节点了（appendChild 把它搬走了）\n");

    IXMLDOMElement_Release(ra); IXMLDOMElement_Release(rb);
    IXMLDOMDocument_Release(a); IXMLDOMDocument_Release(b);
    outf(L"\n");
}


/* Replay what Click-to-Run does to the real manifest:
 *
 *   elem.selectSingleNode("//appv:Extensions")     -- searches the WHOLE document
 *   elem.removeChild(that node)                    -- assumes it is a direct child
 *
 * "//" is anchored at the document root whatever node it is called on, so the
 * match need not be a child of elem at all.  This prints where the match
 * actually sits, which is what the E_INVALIDARG is about. */
static void run_manifest(const WCHAR *path)
{
    static const WCHAR *queries[] = { L"//appv:Extensions", L"//appv:Applications",
                                      L"//appv:UsedKnownFolders" };
    IXMLDOMDocument *doc = new_doc();
    IXMLDOMElement *root = NULL;
    VARIANT_BOOL ok = VARIANT_FALSE;
    VARIANT v;
    size_t q;
    HRESULT hr;

    outf(L"== 真实清单 %ls\n", path);
    if (!doc) return;

    /* C2R sets this, with office mapped and appv not.  setProperty lives on
     * IXMLDOMDocument2. */
    {
        IXMLDOMDocument2 *doc2 = NULL;
        if (SUCCEEDED(IXMLDOMDocument_QueryInterface(doc, &IID_IXMLDOMDocument2, (void **)&doc2)) && doc2)
        {
            BSTR prop = SysAllocString(L"SelectionNamespaces");
            V_VT(&v) = VT_BSTR;
            V_BSTR(&v) = SysAllocString(L"xmlns:appx=\"http://schemas.microsoft.com/appx/2010/manifest\" xmlns:appv=\"http://schemas.microsoft.com/appv/2010/manifest\"");
            hr = IXMLDOMDocument2_setProperty(doc2, prop, v);
            outf(L"  setProperty(SelectionNamespaces) 0x%08lx\n", hr);
            VariantClear(&v);
            SysFreeString(prop);
            IXMLDOMDocument2_Release(doc2);
        }
        else outf(L"  取不到 IXMLDOMDocument2\n");
    }

    V_VT(&v) = VT_BSTR;
    V_BSTR(&v) = SysAllocString(path);
    hr = IXMLDOMDocument_load(doc, v, &ok);
    VariantClear(&v);
    outf(L"  load 0x%08lx 成功=%ls\n", hr, ok == VARIANT_TRUE ? L"是" : L"否");
    if (ok != VARIANT_TRUE) { IXMLDOMDocument_Release(doc); outf(L"\n"); return; }

    IXMLDOMDocument_get_documentElement(doc, &root);
    if (!root) { IXMLDOMDocument_Release(doc); return; }
    outf(L"  documentElement: "); describe((IXMLDOMNode *)root, L"");

    for (q = 0; q < ARRAYSIZE(queries); q++)
    {
        IXMLDOMNode *found = NULL, *walk = NULL, *gone = NULL;
        BSTR bq = SysAllocString(queries[q]), nm = NULL;
        int depth = 0;

        hr = IXMLDOMElement_selectSingleNode(root, bq, &found);
        SysFreeString(bq);
        outf(L"  %-26ls 0x%08lx %ls\n", queries[q], hr, found ? L"找到" : L"没找到");
        if (!found) continue;

        outf(L"    "); describe(found, L"");

        /* How far above the root is it?  If it is not a direct child,
         * removeChild from the root cannot work. */
        IXMLDOMNode_get_parentNode(found, &walk);
        while (walk && depth < 8)
        {
            IXMLDOMNode *up = NULL;
            nm = NULL;
            IXMLDOMNode_get_nodeName(walk, &nm);
            outf(L"    上%d 层: %ls\n", ++depth, nm ? nm : L"?");
            SysFreeString(nm);
            IXMLDOMNode_get_parentNode(walk, &up);
            IXMLDOMNode_Release(walk);
            walk = up;
        }
        if (walk) IXMLDOMNode_Release(walk);

        hr = IXMLDOMElement_removeChild(root, found, &gone);
        outf(L"    根.removeChild() 0x%08lx%ls\n", hr,
             hr == E_INVALIDARG ? L"   <- 与 C2R 看到的一致" : L"");
        if (gone) IXMLDOMNode_Release(gone);
        IXMLDOMNode_Release(found);
    }

    IXMLDOMElement_Release(root);
    IXMLDOMDocument_Release(doc);
    outf(L"\n");
}


/* XSLPattern, which is what a plain DOMDocument selects with, matches
 * "prefix:name" against the tag as written -- it predates namespaces and does
 * no URI resolution at all.  XPath does the opposite.  A node whose prefix was
 * never declared has a name and no URI, so which language is in force decides
 * whether it can be found again after being put into a document, which is
 * exactly what Click-to-Run does to the pieces of an AppX manifest. */
static void run_selection_language(void)
{
    static const WCHAR xml[] =
        L"<Package xmlns:appv=\"http://schemas.microsoft.com/appv/2010/manifest\">"
        L"<Holder/></Package>";
    static const WCHAR frag[] = L"<appv:Extensions><appv:Extension Id=\"x\"/></appv:Extensions>";
    const WCHAR *langs[] = { L"XSLPattern", L"XPath" };
    size_t l;

    for (l = 0; l < ARRAYSIZE(langs); l++)
    {
        IXMLDOMDocument *doc = NULL, *fdoc = NULL;
        IXMLDOMDocument2 *doc2 = NULL;
        IXMLDOMElement *root = NULL, *froot = NULL;
        IXMLDOMNode *found = NULL, *added = NULL;
        VARIANT_BOOL ok = VARIANT_FALSE;
        BSTR s;
        VARIANT v;
        HRESULT hr;

        outf(L"== 选择语言 %ls\n", langs[l]);

        /* A plain DOMDocument is XSLPattern by default, DOMDocument60 is XPath;
         * set it explicitly so the two runs differ only in this. */
        if (FAILED(CoCreateInstance(&CLSID_DOMDocument, NULL, CLSCTX_INPROC_SERVER,
                                    &IID_IXMLDOMDocument, (void **)&doc)))
            continue;
        if (SUCCEEDED(IXMLDOMDocument_QueryInterface(doc, &IID_IXMLDOMDocument2, (void **)&doc2)) && doc2)
        {
            BSTR prop = SysAllocString(L"SelectionLanguage");
            V_VT(&v) = VT_BSTR; V_BSTR(&v) = SysAllocString(langs[l]);
            hr = IXMLDOMDocument2_setProperty(doc2, prop, v);
            outf(L"  setProperty(SelectionLanguage) 0x%08lx\n", hr);
            VariantClear(&v); SysFreeString(prop);

            prop = SysAllocString(L"SelectionNamespaces");
            V_VT(&v) = VT_BSTR;
            V_BSTR(&v) = SysAllocString(L"xmlns:appv=\"http://schemas.microsoft.com/appv/2010/manifest\"");
            IXMLDOMDocument2_setProperty(doc2, prop, v);
            VariantClear(&v); SysFreeString(prop);
            IXMLDOMDocument2_Release(doc2);
        }

        s = SysAllocString(xml);
        IXMLDOMDocument_loadXML(doc, s, &ok);
        SysFreeString(s);
        IXMLDOMDocument_get_documentElement(doc, &root);

        /* The fragment, with no declaration -- exactly what C2R loads. */
        fdoc = new_doc();
        s = SysAllocString(frag);
        IXMLDOMDocument_loadXML(fdoc, s, &ok);
        SysFreeString(s);
        IXMLDOMDocument_get_documentElement(fdoc, &froot);
        if (froot)
        {
            outf(L"  片段的根: "); describe((IXMLDOMNode *)froot, L"");
            IXMLDOMElement_appendChild(root, (IXMLDOMNode *)froot, &added);
        }

        {
            BSTR q = SysAllocString(L"//appv:Extensions");
            hr = IXMLDOMElement_selectSingleNode(root, q, &found);
            outf(L"  放进文档后 selectSingleNode(\"//appv:Extensions\") 0x%08lx %ls\n",
                 hr, found ? L"找到" : L"没找到");
            SysFreeString(q);
        }
        if (found) IXMLDOMNode_Release(found);
        if (added) IXMLDOMNode_Release(added);
        if (froot) IXMLDOMElement_Release(froot);
        if (fdoc) IXMLDOMDocument_Release(fdoc);
        if (root) IXMLDOMElement_Release(root);
        IXMLDOMDocument_Release(doc);
        outf(L"\n");
    }
}

int wmain(int argc, WCHAR **argv)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) { outf(L"CoInitializeEx 0x%08lx\n", hr); return 1; }

    /* The shape AppXManifest.xml really has: prefix declared on the root. */
    run_case(L"前缀已声明（对照组）",
             L"<Package xmlns:appv=\"http://schemas.microsoft.com/appv/2010/manifest\">"
             L"<appv:TargetOSes/><Properties/><appv:AppVInProcExt>true</appv:AppVInProcExt>"
             L"</Package>");

    /* The shape a fragment of it has once the declaration is left behind --
     * this is what produces "Namespace prefix appv is not defined". */
    run_case(L"前缀未声明（C2R 合并片段时的形状）",
             L"<Package><appv:TargetOSes/><Properties/>"
             L"<appv:AppVInProcExt>true</appv:AppVInProcExt></Package>");

    /* Undeclared prefix on an attribute only, which the real manifest also has
     * (appv:PackageId, appv:IgnorableNamespaces). */
    run_case(L"仅属性上有未声明前缀",
             L"<Package appv:PackageId=\"9AC08E99\"><Identity/></Package>");

    run_cross_document();
    run_selection_language();

    if (argc > 1) { run_manifest(argv[1]); }

    if (0)
    {
        IXMLDOMDocument *doc = new_doc();
        VARIANT_BOOL ok = VARIANT_FALSE;
        VARIANT v;
        if (doc)
        {
            V_VT(&v) = VT_BSTR;
            V_BSTR(&v) = SysAllocString(argv[1]);
            outf(L"== 解析 %ls\n", argv[1]);
            hr = IXMLDOMDocument_load(doc, v, &ok);
            outf(L"  load 0x%08lx 成功=%ls\n", hr, ok == VARIANT_TRUE ? L"是" : L"否");
            VariantClear(&v);
            IXMLDOMDocument_Release(doc);
        }
    }

    CoUninitialize();
    return 0;
}
