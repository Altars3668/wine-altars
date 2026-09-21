/*
 * httpfilterprobe -- activate Windows.Web.Http.Filters.HttpBaseProtocolFilter
 * and put its interface through its paces.
 *
 * React Native's networking module builds one of these before it will come up,
 * and C++/WinRT turns a failed activation into an uncaught exception, so what
 * matters is not only that the class activates but that the interfaces it
 * promises are reachable and the settings round-trip.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -municode \
 *       -o httpfilterprobe.exe httpfilterprobe.c -lcombase -lole32 -luuid
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <roapi.h>
#include <winstring.h>
#include <inspectable.h>
#include <stdio.h>

DEFINE_GUID(IID_IHttpBaseProtocolFilter, 0x71c89b09,0xe131,0x4b54,0xa5,0x3c,0xeb,0x43,0xff,0x37,0xe9,0xbb);
DEFINE_GUID(IID_IHttpFilter,             0xa4cb6dd5,0x0902,0x439e,0xbf,0xd7,0xe1,0x25,0x52,0xb1,0x65,0xce);
DEFINE_GUID(IID_IClosable,               0x30d5a829,0x7fa4,0x4026,0x83,0xbb,0xd7,0x5b,0xae,0x4e,0xa9,0x9e);
DEFINE_GUID(IID_IAgileObject,            0x94ea2b94,0xe9cc,0x49e0,0xc0,0xff,0xee,0x64,0xca,0x8f,0x5b,0x90);
DEFINE_GUID(IID_IStringable,             0x96369f54,0x8eb6,0x48f0,0xab,0xce,0xc1,0xb2,0x11,0xe6,0x27,0xc3);
DEFINE_GUID(IID_IHttpMethod,             0x728d4022,0x700d,0x4fe0,0xaf,0xa5,0x40,0x29,0x9c,0x58,0xdb,0xfd);
DEFINE_GUID(IID_IHttpMethodStatics,      0x64d171f0,0xd99a,0x4153,0x8d,0xc6,0xd6,0x8c,0xc4,0xcc,0xe3,0x17);
DEFINE_GUID(IID_IHttpMethodFactory,      0x3c51d10d,0x36d7,0x40f8,0xa8,0x6d,0xe7,0x59,0xca,0xf2,0xf8,0x3f);
DEFINE_GUID(IID_IHttpClient,             0x7fda1151,0x3574,0x4880,0xa8,0xba,0xe6,0xb1,0xe0,0x06,0x1f,0x3d);
DEFINE_GUID(IID_IHttpClientFactory,      0xc30c4eca,0xe3fa,0x4f99,0xaf,0xb4,0x63,0xcc,0x65,0x00,0x94,0x62);

/* IHttpMethod: one property after IInspectable. */
typedef struct { void *q,*a,*r,*gi,*gn,*gt; HRESULT (STDMETHODCALLTYPE *get_Method)(void *, HSTRING *); } MethodVtbl;
typedef struct { const MethodVtbl *lpVtbl; } Method;
/* IHttpMethodStatics: seven known methods, in this order. */
typedef struct { void *q,*a,*r,*gi,*gn,*gt;
    HRESULT (STDMETHODCALLTYPE *Delete)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *Get)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *Head)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *Options)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *Patch)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *Post)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *Put)(void *, void **);
} MethodStaticsVtbl;
typedef struct { const MethodStaticsVtbl *lpVtbl; } MethodStatics;
typedef struct { void *q,*a,*r,*gi,*gn,*gt; HRESULT (STDMETHODCALLTYPE *Create)(void *, HSTRING, void **); } MethodFactoryVtbl;
typedef struct { const MethodFactoryVtbl *lpVtbl; } MethodFactory;
typedef struct { void *q,*a,*r,*gi,*gn,*gt; HRESULT (STDMETHODCALLTYPE *Create)(void *, void *, void **); } ClientFactoryVtbl;
typedef struct { const ClientFactoryVtbl *lpVtbl; } ClientFactory;

static int method_is(void *m, const WCHAR *expect)
{
    HSTRING h = NULL;
    Method *mm = m;
    if (!m || FAILED(mm->lpVtbl->get_Method(mm, &h)) || !h) return 0;
    return !wcscmp(WindowsGetStringRawBuffer(h, NULL), expect);
}

static int fails;
static void check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? " ok " : "FAIL", what);
    if (!ok) fails++;
}

/* The vtable, in the order the ABI puts it. */
typedef struct IHttpBaseProtocolFilterVtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void *, REFIID, void **);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void *);
    ULONG   (STDMETHODCALLTYPE *Release)(void *);
    HRESULT (STDMETHODCALLTYPE *GetIids)(void *, ULONG *, IID **);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(void *, HSTRING *);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(void *, TrustLevel *);
    HRESULT (STDMETHODCALLTYPE *get_AllowAutoRedirect)(void *, unsigned char *);
    HRESULT (STDMETHODCALLTYPE *put_AllowAutoRedirect)(void *, unsigned char);
    HRESULT (STDMETHODCALLTYPE *get_AllowUI)(void *, unsigned char *);
    HRESULT (STDMETHODCALLTYPE *put_AllowUI)(void *, unsigned char);
    HRESULT (STDMETHODCALLTYPE *get_AutomaticDecompression)(void *, unsigned char *);
    HRESULT (STDMETHODCALLTYPE *put_AutomaticDecompression)(void *, unsigned char);
    HRESULT (STDMETHODCALLTYPE *get_CacheControl)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *get_CookieManager)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *get_ClientCertificate)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *put_ClientCertificate)(void *, void *);
    HRESULT (STDMETHODCALLTYPE *get_IgnorableServerCertificateErrors)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *get_MaxConnectionsPerServer)(void *, UINT32 *);
    HRESULT (STDMETHODCALLTYPE *put_MaxConnectionsPerServer)(void *, UINT32);
    HRESULT (STDMETHODCALLTYPE *get_ProxyCredential)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *put_ProxyCredential)(void *, void *);
    HRESULT (STDMETHODCALLTYPE *get_ServerCredential)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *put_ServerCredential)(void *, void *);
    HRESULT (STDMETHODCALLTYPE *get_UseProxy)(void *, unsigned char *);
    HRESULT (STDMETHODCALLTYPE *put_UseProxy)(void *, unsigned char);
} IHttpBaseProtocolFilterVtbl;

typedef struct { const IHttpBaseProtocolFilterVtbl *lpVtbl; } Filter;

int wmain(void)
{
    static const WCHAR name[] = L"Windows.Web.Http.Filters.HttpBaseProtocolFilter";
    IActivationFactory *factory = NULL;
    IInspectable *inst = NULL;
    Filter *f;
    HSTRING cls = NULL, cname = NULL;
    HRESULT hr;
    unsigned char b;
    UINT32 n;
    void *p;

    RoInitialize(RO_INIT_MULTITHREADED);
    WindowsCreateString(name, (UINT32)(ARRAYSIZE(name) - 1), &cls);

    hr = RoGetActivationFactory(cls, &IID_IActivationFactory, (void **)&factory);
    printf("RoGetActivationFactory = 0x%08lx\n", hr);
    check(SUCCEEDED(hr) && factory, "拿到激活工厂");
    if (FAILED(hr)) return 1;

    hr = IActivationFactory_ActivateInstance(factory, &inst);
    printf("ActivateInstance       = 0x%08lx\n", hr);
    check(SUCCEEDED(hr) && inst, "激活出实例");
    if (FAILED(hr)) return 1;

    hr = IInspectable_GetRuntimeClassName(inst, &cname);
    check(SUCCEEDED(hr) && cname && !wcscmp(WindowsGetStringRawBuffer(cname, NULL), name),
          "GetRuntimeClassName 是这个类");

    check(SUCCEEDED(IInspectable_QueryInterface(inst, &IID_IHttpBaseProtocolFilter, &p)) && p,
          "QI IHttpBaseProtocolFilter");
    f = p;
    check(SUCCEEDED(IInspectable_QueryInterface(inst, &IID_IHttpFilter, &p)) && p, "QI IHttpFilter");
    check(SUCCEEDED(IInspectable_QueryInterface(inst, &IID_IClosable, &p)) && p, "QI IClosable");
    check(SUCCEEDED(IInspectable_QueryInterface(inst, &IID_IAgileObject, &p)) && p, "QI IAgileObject");

    printf("默认值:\n");
    f->lpVtbl->get_AllowAutoRedirect(f, &b);       printf("  AllowAutoRedirect       = %d\n", b); check(b == 1, "AllowAutoRedirect 默认 true");
    f->lpVtbl->get_AllowUI(f, &b);                 printf("  AllowUI                 = %d\n", b); check(b == 1, "AllowUI 默认 true");
    f->lpVtbl->get_AutomaticDecompression(f, &b);  printf("  AutomaticDecompression  = %d\n", b); check(b == 1, "AutomaticDecompression 默认 true");
    f->lpVtbl->get_UseProxy(f, &b);                printf("  UseProxy                = %d\n", b); check(b == 1, "UseProxy 默认 true");
    f->lpVtbl->get_MaxConnectionsPerServer(f, &n); printf("  MaxConnectionsPerServer = %u\n", n); check(n == 6, "MaxConnectionsPerServer 默认 6");

    printf("设置能存回去:\n");
    check(SUCCEEDED(f->lpVtbl->put_AllowAutoRedirect(f, 0)) &&
          SUCCEEDED(f->lpVtbl->get_AllowAutoRedirect(f, &b)) && b == 0, "AllowAutoRedirect 写 false 读回 false");
    check(SUCCEEDED(f->lpVtbl->put_MaxConnectionsPerServer(f, 12)) &&
          SUCCEEDED(f->lpVtbl->get_MaxConnectionsPerServer(f, &n)) && n == 12, "MaxConnectionsPerServer 写 12 读回 12");
    check(f->lpVtbl->put_MaxConnectionsPerServer(f, 0) == E_INVALIDARG, "MaxConnectionsPerServer 拒绝 0");

    printf("未实现的成员应当明确报错，而不是给个空对象:\n");
    p = (void *)(ULONG_PTR)0xdeadbeef;
    check(f->lpVtbl->get_CookieManager(f, &p) == E_NOTIMPL && !p, "CookieManager -> E_NOTIMPL 且置空");
    p = (void *)(ULONG_PTR)0xdeadbeef;
    check(f->lpVtbl->get_CacheControl(f, &p) == E_NOTIMPL && !p, "CacheControl -> E_NOTIMPL 且置空");

    /* ---- Windows.Web.Http.HttpMethod ---- */
    {
        static const WCHAR mname[] = L"Windows.Web.Http.HttpMethod";
        IActivationFactory *mf = NULL;
        MethodStatics *st; MethodFactory *mkf;
        HSTRING mcls = NULL, custom = NULL;
        void *m = NULL;

        printf("\nWindows.Web.Http.HttpMethod:\n");
        WindowsCreateString(mname, (UINT32)(ARRAYSIZE(mname) - 1), &mcls);
        hr = RoGetActivationFactory(mcls, &IID_IActivationFactory, (void **)&mf);
        check(SUCCEEDED(hr) && mf, "拿到激活工厂");
        if (SUCCEEDED(hr))
        {
            check(SUCCEEDED(IActivationFactory_QueryInterface(mf, &IID_IHttpMethodStatics, (void **)&st)) && st,
                  "QI IHttpMethodStatics");
            st->lpVtbl->Get(st, &m);    check(method_is(m, L"GET"), "HttpMethod.Get 是 GET");
            st->lpVtbl->Post(st, &m);   check(method_is(m, L"POST"), "HttpMethod.Post 是 POST");
            st->lpVtbl->Delete(st, &m); check(method_is(m, L"DELETE"), "HttpMethod.Delete 是 DELETE");
            st->lpVtbl->Patch(st, &m);  check(method_is(m, L"PATCH"), "HttpMethod.Patch 是 PATCH");
            check(SUCCEEDED(IActivationFactory_QueryInterface(mf, &IID_IHttpMethodFactory, (void **)&mkf)) && mkf,
                  "QI IHttpMethodFactory");
            WindowsCreateString(L"BREW", 4, &custom);
            check(SUCCEEDED(mkf->lpVtbl->Create(mkf, custom, &m)) && method_is(m, L"BREW"), "自定义方法名往返");
            check(mkf->lpVtbl->Create(mkf, NULL, &m) == E_INVALIDARG, "空方法名被拒绝");
            check(SUCCEEDED(IInspectable_QueryInterface((IInspectable *)m, &IID_IStringable, &p)) && p,
                  "HttpMethod 可 QI IStringable");
        }
    }

    /* ---- Windows.Web.Http.HttpClient ---- */
    {
        static const WCHAR cname2[] = L"Windows.Web.Http.HttpClient";
        IActivationFactory *cf = NULL;
        IInspectable *client = NULL;
        ClientFactory *ccf;
        HSTRING ccls = NULL, rcn = NULL;
        void *c2 = NULL;

        printf("\nWindows.Web.Http.HttpClient:\n");
        WindowsCreateString(cname2, (UINT32)(ARRAYSIZE(cname2) - 1), &ccls);
        hr = RoGetActivationFactory(ccls, &IID_IActivationFactory, (void **)&cf);
        check(SUCCEEDED(hr) && cf, "拿到激活工厂");
        if (SUCCEEDED(hr))
        {
            check(SUCCEEDED(IActivationFactory_ActivateInstance(cf, &client)) && client, "默认构造");
            check(SUCCEEDED(IInspectable_GetRuntimeClassName(client, &rcn)) && rcn &&
                  !wcscmp(WindowsGetStringRawBuffer(rcn, NULL), cname2), "GetRuntimeClassName 正确");
            check(SUCCEEDED(IInspectable_QueryInterface(client, &IID_IHttpClient, &p)) && p, "QI IHttpClient");
            check(SUCCEEDED(IInspectable_QueryInterface(client, &IID_IClosable, &p)) && p, "QI IClosable");
            check(SUCCEEDED(IInspectable_QueryInterface(client, &IID_IStringable, &p)) && p, "QI IStringable");
            check(SUCCEEDED(IActivationFactory_QueryInterface(cf, &IID_IHttpClientFactory, (void **)&ccf)) && ccf,
                  "QI IHttpClientFactory");
            /* the filter made at the top of this run is the real argument */
            check(SUCCEEDED(ccf->lpVtbl->Create(ccf, f, &c2)) && c2, "用 HttpBaseProtocolFilter 构造");
            check(ccf->lpVtbl->Create(ccf, NULL, &c2) == E_INVALIDARG, "没有 filter 时被拒绝");
        }
    }

    printf("\n%s  失败 %d 项\n", fails ? "有问题" : "全部通过", fails);
    return fails != 0;
}
