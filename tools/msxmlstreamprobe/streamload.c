/*
 * streamload -- what IXMLDOMDocument::load and IPersistStreamInit::Load do when the source stream's
 * Read fails.
 *
 * Word's References tab loads XML through IXMLDOMDocument::load(VARIANT IStream) from a stream of
 * its own whose Read returns E_FAIL and leaves *pcbRead alone.  Wine's msxml3 copied the stream
 * with the result ignored and the count uninitialised, and wrote gigabytes from the stack.  This
 * asks the Windows msxml the same: for several Read behaviours, in both msxml3 and msxml6, it
 * prints load's HRESULT and result, the parse error, and every Read call -- the size asked for,
 * whether pcbRead was given, and what *pcbRead held on entry (to see whether the caller sets it).
 *
 * It also reads the IStream of an empty and of a non-empty document, and loads a document from
 * another one (VT_UNKNOWN and VT_DISPATCH) -- what Word does, its source document being empty.
 *
 * Safety: when a mode would leave *pcbRead untouched but its entry value is not 0, the probe sets it
 * to 0 itself and says so ("entry_nonzero_zeroed"), so a caller that does not initialise it cannot
 * be made to copy garbage.  No files, no network.
 *
 *   x86_64-w64-mingw32-gcc -O1 -Wall -Werror -o streamload.exe streamload.c -lole32 -loleaut32 -luuid
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <windows.h>
#include <ole2.h>
#include <ocidl.h>
#include <msxml2.h>
#include <stdio.h>

enum mode
{
    M_DATA_THEN_SFALSE,     /* "<root/>" in one call, then S_FALSE with 0: the normal end */
    M_FAIL_FIRST,           /* E_FAIL at once, *pcbRead untouched */
    M_FAIL_FIRST_ZERO,      /* E_FAIL at once, *pcbRead = 0 */
    M_DATA_THEN_FAIL,       /* "<root/>" with S_OK, then E_FAIL untouched */
    M_PART_THEN_FAIL,       /* "<root><a>" with S_OK, then E_FAIL untouched */
    M_EMPTY_SFALSE,         /* S_FALSE with 0 at once */
    M_EMPTY_SOK,            /* S_OK with 0 at once */
    M_PENDING,              /* E_PENDING untouched, at most 3 times, then E_FAIL */
    M_COUNT
};
static const char *mode_names[M_COUNT] = {
    "data_then_sfalse", "fail_first", "fail_first_zero", "data_then_fail", "part_then_fail",
    "empty_sfalse", "empty_sok", "pending",
};

struct stream
{
    IStream IStream_iface;
    LONG ref;
    enum mode mode;
    int calls;
    char log[2048];
};

static struct stream *impl(IStream *iface) { return CONTAINING_RECORD(iface, struct stream, IStream_iface); }

static void note(struct stream *s, const char *fmt, ...)
{
    size_t len = strlen(s->log);
    va_list ap;
    va_start(ap, fmt);
    if (len < sizeof(s->log) - 1) vsnprintf(s->log + len, sizeof(s->log) - len, fmt, ap);
    va_end(ap);
}

static HRESULT WINAPI st_QueryInterface(IStream *iface, REFIID riid, void **obj)
{
    struct stream *s = impl(iface);
    WCHAR guid[40];
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ISequentialStream) || IsEqualIID(riid, &IID_IStream))
    {
        *obj = iface;
        IStream_AddRef(iface);
        return S_OK;
    }
    StringFromGUID2(riid, guid, 40);
    note(s, " QI(%ls)=E_NOINTERFACE", guid);
    *obj = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI st_AddRef(IStream *iface) { return InterlockedIncrement(&impl(iface)->ref); }
static ULONG WINAPI st_Release(IStream *iface) { return InterlockedDecrement(&impl(iface)->ref); }

static HRESULT WINAPI st_Read(IStream *iface, void *buf, ULONG cb, ULONG *pcb)
{
    struct stream *s = impl(iface);
    static const char whole[] = "<root/>", part[] = "<root><a>";
    const char *data = NULL;
    HRESULT hr = S_OK;
    ULONG entry = pcb ? *pcb : 0, out = 0;
    BOOL untouched = FALSE;

    s->calls++;
    switch (s->mode)
    {
    case M_DATA_THEN_SFALSE:
        if (s->calls == 1) data = whole; else hr = S_FALSE;
        break;
    case M_FAIL_FIRST: hr = E_FAIL; untouched = TRUE; break;
    case M_FAIL_FIRST_ZERO: hr = E_FAIL; break;
    case M_DATA_THEN_FAIL:
        if (s->calls == 1) data = whole; else { hr = E_FAIL; untouched = TRUE; }
        break;
    case M_PART_THEN_FAIL:
        if (s->calls == 1) data = part; else { hr = E_FAIL; untouched = TRUE; }
        break;
    case M_EMPTY_SFALSE: hr = S_FALSE; break;
    case M_EMPTY_SOK: hr = S_OK; break;
    case M_PENDING:
        if (s->calls <= 3) hr = E_PENDING; else hr = E_FAIL;
        untouched = TRUE;
        break;
    default: break;
    }
    if (data)
    {
        out = strlen(data);
        if (out > cb) out = cb;
        memcpy(buf, data, out);
    }
    note(s, " read#%d(cb=%lu pcb=%s entry=%#lx)->%#lx", s->calls, cb, pcb ? "set" : "NULL", entry, hr);
    if (pcb)
    {
        if (!untouched) { *pcb = out; note(s, " n=%lu", out); }
        else if (entry) { *pcb = 0; note(s, " entry_nonzero_zeroed"); }
        else note(s, " untouched");
    }
    return s->calls > 50 ? E_FAIL : hr;
}

static HRESULT WINAPI st_Write(IStream *iface, const void *buf, ULONG cb, ULONG *pcb) { note(impl(iface), " Write"); return STG_E_ACCESSDENIED; }
static HRESULT WINAPI st_Seek(IStream *iface, LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER *pos)
{
    note(impl(iface), " Seek(%lld,%lu)", move.QuadPart, origin);
    return E_NOTIMPL;
}
static HRESULT WINAPI st_SetSize(IStream *iface, ULARGE_INTEGER size) { return E_NOTIMPL; }
static HRESULT WINAPI st_CopyTo(IStream *iface, IStream *dst, ULARGE_INTEGER cb, ULARGE_INTEGER *r, ULARGE_INTEGER *w) { note(impl(iface), " CopyTo"); return E_NOTIMPL; }
static HRESULT WINAPI st_Commit(IStream *iface, DWORD flags) { return E_NOTIMPL; }
static HRESULT WINAPI st_Revert(IStream *iface) { return E_NOTIMPL; }
static HRESULT WINAPI st_LockRegion(IStream *iface, ULARGE_INTEGER o, ULARGE_INTEGER cb, DWORD t) { return E_NOTIMPL; }
static HRESULT WINAPI st_UnlockRegion(IStream *iface, ULARGE_INTEGER o, ULARGE_INTEGER cb, DWORD t) { return E_NOTIMPL; }
static HRESULT WINAPI st_Stat(IStream *iface, STATSTG *stat, DWORD flag) { note(impl(iface), " Stat"); return E_NOTIMPL; }
static HRESULT WINAPI st_Clone(IStream *iface, IStream **out) { return E_NOTIMPL; }

static const IStreamVtbl st_vtbl = {
    st_QueryInterface, st_AddRef, st_Release, st_Read, st_Write, st_Seek, st_SetSize, st_CopyTo,
    st_Commit, st_Revert, st_LockRegion, st_UnlockRegion, st_Stat, st_Clone,
};

static void report_doc(IXMLDOMDocument *doc)
{
    IXMLDOMParseError *err = NULL;
    IXMLDOMElement *root = NULL;
    LONG code = 0;
    BSTR reason = NULL;
    HRESULT hr;

    if (SUCCEEDED(IXMLDOMDocument_get_parseError(doc, &err)) && err)
    {
        IXMLDOMParseError_get_errorCode(err, &code);
        IXMLDOMParseError_get_reason(err, &reason);
        IXMLDOMParseError_Release(err);
    }
    hr = IXMLDOMDocument_get_documentElement(doc, &root);
    printf(" parseError=%#lx reason_len=%u root=%s", code, reason ? SysStringLen(reason) : 0,
           hr == S_OK && root ? "yes" : "no");
    if (root) IXMLDOMElement_Release(root);
    SysFreeString(reason);
}

static void run(const WCHAR *progid, enum mode mode, BOOL persist)
{
    /* on the heap and never freed: msxml must not be able to reach a dead stack frame through it */
    struct stream *sp = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*sp));
    IXMLDOMDocument *doc;
    CLSID clsid;
    HRESULT hr;

    sp->IStream_iface.lpVtbl = (IStreamVtbl *)&st_vtbl;
    sp->ref = 1;
    sp->mode = mode;
    if (FAILED(CLSIDFromProgID(progid, &clsid)) ||
        FAILED(hr = CoCreateInstance(&clsid, NULL, CLSCTX_INPROC_SERVER, &IID_IXMLDOMDocument, (void **)&doc)))
    {
        printf("%ls %s: no document\n", progid, mode_names[mode]);
        return;
    }
    IXMLDOMDocument_put_async(doc, VARIANT_FALSE);
    if (persist)
    {
        IPersistStreamInit *psi;
        hr = IXMLDOMDocument_QueryInterface(doc, &IID_IPersistStreamInit, (void **)&psi);
        if (SUCCEEDED(hr))
        {
            hr = IPersistStreamInit_Load(psi, &sp->IStream_iface);
            IPersistStreamInit_Release(psi);
        }
        printf("%ls %-17s PersistStreamInit::Load hr=%#lx", progid, mode_names[mode], hr);
    }
    else
    {
        VARIANT v;
        VARIANT_BOOL ok = 0x55;
        V_VT(&v) = VT_UNKNOWN;
        V_UNKNOWN(&v) = (IUnknown *)&sp->IStream_iface;
        hr = IXMLDOMDocument_load(doc, v, &ok);
        printf("%ls %-17s load hr=%#lx ok=%d", progid, mode_names[mode], hr, ok);
    }
    report_doc(doc);
    printf(" calls=%d refs_left=%ld\n   %s\n", sp->calls, sp->ref, sp->log);
    IXMLDOMDocument_Release(doc);
}


static IXMLDOMDocument *new_doc(const WCHAR *progid, const WCHAR *xml)
{
    IXMLDOMDocument *doc = NULL;
    VARIANT_BOOL ok;
    CLSID clsid;
    BSTR b;

    if (FAILED(CLSIDFromProgID(progid, &clsid)) ||
        FAILED(CoCreateInstance(&clsid, NULL, CLSCTX_INPROC_SERVER, &IID_IXMLDOMDocument, (void **)&doc)))
        return NULL;
    IXMLDOMDocument_put_async(doc, VARIANT_FALSE);
    if (xml)
    {
        b = SysAllocString(xml);
        IXMLDOMDocument_loadXML(doc, b, &ok);
        SysFreeString(b);
    }
    return doc;
}

/* returns whether Read on the empty document's stream set the count */
static BOOL docstream_reads(const WCHAR *progid)
{
    static const WCHAR *xmls[] = { NULL, L"<root/>" };
    BOOL empty_sets_count = FALSE;
    unsigned int i;

    for (i = 0; i < ARRAYSIZE(xmls); i++)
    {
        IXMLDOMDocument *doc = new_doc(progid, xmls[i]);
        IStream *st = NULL;
        STATSTG stat;
        char buf[64];
        ULONG n;
        HRESULT hr;
        int r;

        if (!doc) { printf("%ls docstream: no document\n", progid); return FALSE; }
        hr = IXMLDOMDocument_QueryInterface(doc, &IID_IStream, (void **)&st);
        printf("%ls docstream(%s) QI(IStream)=%#lx", progid, xmls[i] ? "<root/>" : "empty", hr);
        if (FAILED(hr)) { printf("\n"); IXMLDOMDocument_Release(doc); continue; }
        memset(&stat, 0, sizeof(stat));
        hr = IStream_Stat(st, &stat, STATFLAG_NONAME);
        printf(" Stat=%#lx type=%lu size=%llu", hr, stat.type, (unsigned long long)stat.cbSize.QuadPart);
        for (r = 1; r <= 2; r++)
        {
            n = 0xdeadbeef;
            memset(buf, 0, sizeof(buf));
            hr = IStream_Read(st, buf, sizeof(buf) - 1, &n);
            printf(" Read#%d=%#lx n=%#lx", r, hr, n);
            if (n != 0xdeadbeef && n && n < sizeof(buf)) printf(" \"%.20s\"", buf);
            if (!xmls[i] && r == 1 && n != 0xdeadbeef) empty_sets_count = TRUE;
        }
        printf("\n");
        IStream_Release(st);
        IXMLDOMDocument_Release(doc);
    }
    return empty_sets_count;
}

static void load_from_doc(const WCHAR *progid, BOOL empty_ok)
{
    static const WCHAR *xmls[] = { NULL, L"<root/>" };
    unsigned int i, vt;

    for (i = 0; i < ARRAYSIZE(xmls); i++)
        for (vt = 0; vt < 2; vt++)
        {
            IXMLDOMDocument *src, *dst;
            VARIANT v;
            VARIANT_BOOL ok = 0x55;
            HRESULT hr;

            if (!xmls[i] && !empty_ok)
            {
                printf("%ls load(%s empty doc): skipped, Read on an empty document's stream leaves the count\n",
                       progid, vt ? "VT_DISPATCH" : "VT_UNKNOWN");
                continue;
            }
            src = new_doc(progid, xmls[i]);
            dst = new_doc(progid, L"<old/>");
            if (!src || !dst) { printf("%ls load from doc: no document\n", progid); return; }
            V_VT(&v) = vt ? VT_DISPATCH : VT_UNKNOWN;
            V_UNKNOWN(&v) = (IUnknown *)src;
            hr = IXMLDOMDocument_load(dst, v, &ok);
            printf("%ls load(%s %s doc) hr=%#lx ok=%d", progid, vt ? "VT_DISPATCH" : "VT_UNKNOWN",
                   xmls[i] ? "<root/>" : "empty", hr, ok);
            report_doc(dst);
            printf("\n");
            IXMLDOMDocument_Release(src);
            IXMLDOMDocument_Release(dst);
        }
}

int main(void)
{
    static const WCHAR *progids[] = { L"Msxml2.DOMDocument.3.0", L"Msxml2.DOMDocument.6.0" };
    unsigned int p, m;

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    setvbuf(stdout, NULL, _IONBF, 0);
    CoInitialize(NULL);
    for (p = 0; p < ARRAYSIZE(progids); p++)
    {
        BOOL empty_ok = docstream_reads(progids[p]);
        load_from_doc(progids[p], empty_ok);
        for (m = 0; m < M_COUNT; m++)
        {
            run(progids[p], m, FALSE);
            run(progids[p], m, TRUE);
        }
    }
    CoUninitialize();
    return 0;
}
