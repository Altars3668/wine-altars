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

    printf("\n%s  失败 %d 项\n", fails ? "有问题" : "全部通过", fails);
    return fails != 0;
}
