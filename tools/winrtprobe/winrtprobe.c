/*
 * 验证本轮新增的 WinRT 运行时类：激活得到、接口拿得到、答复是该有的答复。
 * 每个类一节，缺一个不会影响其它节。
 */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <inspectable.h>
#include <stdio.h>

static int fails;
static void check(int ok, const char *what)
{
    printf("  [%s] %s\n", ok ? " ok " : "失败", what);
    if (!ok) fails++;
}

DEFINE_GUID(IID_IRetailInfoStatics,  0x0712c6b8,0x8b92,0x4f2a,0x84,0x99,0x03,0x1f,0x17,0x98,0xd6,0xef);
DEFINE_GUID(IID_IMessageWebSocket,   0x33727d08,0x34d5,0x4746,0xad,0x7b,0x8d,0xde,0x5b,0xc2,0xef,0x88);
DEFINE_GUID(IID_IWebSocket,          0xf877396f,0x99b1,0x4e18,0xbc,0x08,0x85,0x0c,0x9a,0xdf,0x15,0x6e);
DEFINE_GUID(IID_IWebSocketControl,   0x2ec4bdc3,0xd9a5,0x455a,0x98,0x11,0xde,0x24,0xd4,0x53,0x37,0xe9);
DEFINE_GUID(IID_IWebSocketControl2,  0x79c3be03,0xf2ca,0x461e,0xaf,0x4e,0x96,0x65,0xbc,0x2d,0x06,0x20);
DEFINE_GUID(IID_IMsgWebSocketControl,0x8118388a,0xc629,0x4f0a,0x80,0xfb,0x81,0xfc,0x05,0x53,0x88,0x62);
DEFINE_GUID(IID_IClosable_,          0x30d5a829,0x7fa4,0x4026,0x83,0xbb,0xd7,0x5b,0xae,0x4e,0xa9,0x9e);
DEFINE_GUID(IID_IHttpRequestMessage, 0xf5762b3c,0x74d4,0x4811,0xb5,0xdc,0x9f,0x8b,0x4e,0x2f,0x9a,0xbf);
DEFINE_GUID(IID_IHttpResponseMessage,0xfee200fb,0x8664,0x44e0,0x95,0xd9,0x42,0x69,0x61,0x99,0xbf,0xfc);
DEFINE_GUID(IID_IMapSS,              0xf6d1f700,0x49c2,0x52ae,0x81,0x54,0x82,0x6f,0x99,0x08,0x77,0x3c);
DEFINE_GUID(IID_IIterableKVSS,       0xe9bdaaf0,0xcbf6,0x5c72,0xbe,0x90,0x29,0xcb,0xf3,0xa1,0x31,0x9b);
DEFINE_GUID(IID_IHttpContent,        0x6b14a441,0xfba7,0x4bd2,0xaf,0x0a,0x83,0x9d,0xe7,0xc2,0x95,0xda);
DEFINE_GUID(IID_IHttpStringContentF, 0x46649d5b,0x2e93,0x48eb,0x8e,0x61,0x19,0x67,0x78,0x78,0xe5,0x7f);
DEFINE_GUID(IID_IHttpMultipart,      0x64d337e2,0xe967,0x4624,0xb6,0xd1,0xcf,0x74,0x60,0x4a,0x4a,0x42);
DEFINE_GUID(IID_IStringable_,        0x96369f54,0x8eb6,0x48f0,0xab,0xce,0xc1,0xb2,0x11,0xe6,0x27,0xc3);
DEFINE_GUID(IID_IPPMStatics,         0xc0bffc66,0x8c3d,0x4d56,0x88,0x04,0xc6,0x8f,0x0a,0xd3,0x2e,0xc5);
DEFINE_GUID(IID_IPPMStatics2,        0xb68f9a8c,0x39e0,0x4649,0xb2,0xe4,0x07,0x0a,0xb8,0xa5,0x79,0xb3);
DEFINE_GUID(IID_IPPManager,          0xd5703e18,0xa08d,0x47e6,0xa2,0x40,0x99,0x34,0xd7,0x16,0x5e,0xb5);
DEFINE_GUID(IID_IPPManager2,         0xabf7527a,0x8435,0x417f,0x99,0xb6,0x51,0xbe,0xaf,0x36,0x58,0x88);
DEFINE_GUID(IID_IUARManagerStatics,  0xc0392df1,0x224a,0x432c,0x81,0xe5,0x0c,0x76,0xb4,0xc4,0xce,0xfa);
DEFINE_GUID(IID_IUARManager,         0x0c30be4e,0x903d,0x48d6,0x82,0xd4,0x40,0x43,0xed,0x57,0x79,0x1b);
DEFINE_GUID(IID_IAddPackageOptions,  0x05cee018,0xf68f,0x422b,0x95,0xa4,0x66,0x67,0x9e,0xc7,0x7f,0xc0);

typedef struct { void *q,*a,*r,*gi,*gn,*gt;
    HRESULT (STDMETHODCALLTYPE *IsIdentityManaged)(void *, HSTRING, unsigned char *);
    HRESULT (STDMETHODCALLTYPE *TryApplyProcessUIPolicy)(void *, HSTRING, unsigned char *);
    HRESULT (STDMETHODCALLTYPE *ClearProcessUIPolicy)(void *);
    HRESULT (STDMETHODCALLTYPE *CreateCurrentThreadNetworkContext)(void *, HSTRING, void **);
    HRESULT (STDMETHODCALLTYPE *GetPrimaryManagedIdentityForNetworkEndpointAsync)(void *, void *, void **);
    HRESULT (STDMETHODCALLTYPE *RevokeContent)(void *, HSTRING);
    HRESULT (STDMETHODCALLTYPE *GetForCurrentView)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *add1)(void *, void *, INT64 *);
    HRESULT (STDMETHODCALLTYPE *rm1)(void *, INT64);
    HRESULT (STDMETHODCALLTYPE *add2)(void *, void *, INT64 *);
    HRESULT (STDMETHODCALLTYPE *rm2)(void *, INT64);
    HRESULT (STDMETHODCALLTYPE *add3)(void *, void *, INT64 *);
    HRESULT (STDMETHODCALLTYPE *rm3)(void *, INT64);
    HRESULT (STDMETHODCALLTYPE *CheckAccess)(void *, HSTRING, HSTRING, int *);
    HRESULT (STDMETHODCALLTYPE *RequestAccessAsync)(void *, HSTRING, HSTRING, void **);
} PPMSVtbl;
typedef struct { const PPMSVtbl *lpVtbl; } PPMS;

typedef struct { void *q,*a,*r,*gi,*gn,*gt;
    HRESULT (STDMETHODCALLTYPE *HasContentBeenRevokedSince)(void *, HSTRING, INT64, unsigned char *);
    HRESULT (STDMETHODCALLTYPE *CheckAccessForApp)(void *, HSTRING, HSTRING, int *);
    HRESULT (STDMETHODCALLTYPE *RequestAccessForAppAsync)(void *, HSTRING, HSTRING, void **);
    HRESULT (STDMETHODCALLTYPE *GetEnforcementLevel)(void *, HSTRING, int *);
    HRESULT (STDMETHODCALLTYPE *IsUserDecryptionAllowed)(void *, HSTRING, unsigned char *);
    HRESULT (STDMETHODCALLTYPE *IsProtectionUnderLockRequired)(void *, HSTRING, unsigned char *);
    HRESULT (STDMETHODCALLTYPE *addPC)(void *, void *, INT64 *);
    HRESULT (STDMETHODCALLTYPE *rmPC)(void *, INT64);
    HRESULT (STDMETHODCALLTYPE *get_IsProtectionEnabled)(void *, unsigned char *);
} PPMS2Vtbl;
typedef struct { const PPMS2Vtbl *lpVtbl; } PPMS2;

typedef struct { void *q,*a,*r,*gi,*gn,*gt;
    HRESULT (STDMETHODCALLTYPE *GetForCurrentView)(void *, void **);
} UARSVtbl;
typedef struct { const UARSVtbl *lpVtbl; } UARS;

typedef struct { void *q,*a,*r,*gi,*gn,*gt;
    HRESULT (STDMETHODCALLTYPE *add_UserActivityRequested)(void *, void *, INT64 *);
    HRESULT (STDMETHODCALLTYPE *remove_UserActivityRequested)(void *, INT64);
} UARVtbl;
typedef struct { const UARVtbl *lpVtbl; } UAR;

/* IAddPackageOptions：前八项是集合与两个对象属性，之后是 StubPackageOption 与一串开关 */
typedef struct { void *q,*a,*r,*gi,*gn,*gt;
    HRESULT (STDMETHODCALLTYPE *get_DependencyPackageUris)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *get_TargetVolume)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *put_TargetVolume)(void *, void *);
    HRESULT (STDMETHODCALLTYPE *get_OptionalPackageFamilyNames)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *get_OptionalPackageUris)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *get_RelatedPackageUris)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *get_ExternalLocationUri)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *put_ExternalLocationUri)(void *, void *);
    HRESULT (STDMETHODCALLTYPE *get_StubPackageOption)(void *, int *);
    HRESULT (STDMETHODCALLTYPE *put_StubPackageOption)(void *, int);
    HRESULT (STDMETHODCALLTYPE *get_DeveloperMode)(void *, unsigned char *);
    HRESULT (STDMETHODCALLTYPE *put_DeveloperMode)(void *, unsigned char);
} APOVtbl;
typedef struct { const APOVtbl *lpVtbl; } APO;

/* IHttpContent：Headers 之后四个异步读，再是 TryComputeLength */
typedef struct { void *q,*a,*r,*gi,*gn,*gt;
    HRESULT (STDMETHODCALLTYPE *get_Headers)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *BufferAllAsync)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *ReadAsBufferAsync)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *ReadAsInputStreamAsync)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *ReadAsStringAsync)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *TryComputeLength)(void *, UINT64 *, unsigned char *);
} ContentVtbl;
typedef struct { const ContentVtbl *lpVtbl; } Content;

typedef struct { void *q,*a,*r,*gi,*gn,*gt;
    HRESULT (STDMETHODCALLTYPE *CreateFromString)(void *, HSTRING, void **);
    HRESULT (STDMETHODCALLTYPE *CreateFromStringWithEncoding)(void *, HSTRING, int, void **);
    HRESULT (STDMETHODCALLTYPE *CreateFromStringWithEncodingAndMediaType)(void *, HSTRING, int, HSTRING, void **);
} StrContentFVtbl;
typedef struct { const StrContentFVtbl *lpVtbl; } StrContentF;

typedef struct { void *q,*a,*r,*gi,*gn,*gt;
    HRESULT (STDMETHODCALLTYPE *Add)(void *, void *);
    HRESULT (STDMETHODCALLTYPE *AddWithName)(void *, void *, HSTRING);
} MultipartVtbl;
typedef struct { const MultipartVtbl *lpVtbl; } Multipart;

typedef struct { void *q,*a,*r,*gi,*gn,*gt;
    HRESULT (STDMETHODCALLTYPE *ToString)(void *, HSTRING *);
} StringableVtbl;
typedef struct { const StringableVtbl *lpVtbl; } Stringable;

/* IMap<HSTRING,HSTRING>：Lookup / Size / HasKey / GetView / Insert / Remove / Clear */
typedef struct { void *q,*a,*r,*gi,*gn,*gt;
    HRESULT (STDMETHODCALLTYPE *Lookup)(void *, HSTRING, HSTRING *);
    HRESULT (STDMETHODCALLTYPE *get_Size)(void *, UINT32 *);
    HRESULT (STDMETHODCALLTYPE *HasKey)(void *, HSTRING, unsigned char *);
    HRESULT (STDMETHODCALLTYPE *GetView)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *Insert)(void *, HSTRING, HSTRING, unsigned char *);
    HRESULT (STDMETHODCALLTYPE *Remove)(void *, HSTRING);
    HRESULT (STDMETHODCALLTYPE *Clear)(void *);
} MapSSVtbl;
typedef struct { const MapSSVtbl *lpVtbl; } MapSS;

/* IHttpRequestMessage：Content / Headers / Method / ... */
typedef struct { void *q,*a,*r,*gi,*gn,*gt;
    HRESULT (STDMETHODCALLTYPE *get_Content)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *put_Content)(void *, void *);
    HRESULT (STDMETHODCALLTYPE *get_Headers)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *get_Method)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *put_Method)(void *, void *);
} ReqVtbl;
typedef struct { const ReqVtbl *lpVtbl; } Req;

/* IHttpResponseMessage：前四项 + 状态码 */
typedef struct { void *q,*a,*r,*gi,*gn,*gt;
    HRESULT (STDMETHODCALLTYPE *get_Content)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *put_Content)(void *, void *);
    HRESULT (STDMETHODCALLTYPE *get_Headers)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *get_IsSuccessStatusCode)(void *, unsigned char *);
    HRESULT (STDMETHODCALLTYPE *get_ReasonPhrase)(void *, HSTRING *);
    HRESULT (STDMETHODCALLTYPE *put_ReasonPhrase)(void *, HSTRING);
    HRESULT (STDMETHODCALLTYPE *get_RequestMessage)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *put_RequestMessage)(void *, void *);
    HRESULT (STDMETHODCALLTYPE *get_Source)(void *, int *);
    HRESULT (STDMETHODCALLTYPE *put_Source)(void *, int);
    HRESULT (STDMETHODCALLTYPE *get_StatusCode)(void *, int *);
    HRESULT (STDMETHODCALLTYPE *put_StatusCode)(void *, int);
} RespVtbl;
typedef struct { const RespVtbl *lpVtbl; } Resp;

/* IMessageWebSocket：Control / Information / 两个事件 */
typedef struct { void *q,*a,*r,*gi,*gn,*gt;
    HRESULT (STDMETHODCALLTYPE *get_Control)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *get_Information)(void *, void **);
} MsgSockVtbl;
typedef struct { const MsgSockVtbl *lpVtbl; } MsgSock;

/* IWebSocket：SetRequestHeader 排在 OutputStream / ConnectAsync 之后 */
typedef struct { void *q,*a,*r,*gi,*gn,*gt;
    HRESULT (STDMETHODCALLTYPE *get_OutputStream)(void *, void **);
    HRESULT (STDMETHODCALLTYPE *ConnectAsync)(void *, void *, void **);
    HRESULT (STDMETHODCALLTYPE *SetRequestHeader)(void *, HSTRING, HSTRING);
} SockVtbl;
typedef struct { const SockVtbl *lpVtbl; } Sock;

/* IMessageWebSocketControl：两组属性 */
typedef struct { void *q,*a,*r,*gi,*gn,*gt;
    HRESULT (STDMETHODCALLTYPE *get_MaxMessageSize)(void *, UINT32 *);
    HRESULT (STDMETHODCALLTYPE *put_MaxMessageSize)(void *, UINT32);
    HRESULT (STDMETHODCALLTYPE *get_MessageType)(void *, int *);
    HRESULT (STDMETHODCALLTYPE *put_MessageType)(void *, int);
} MsgCtlVtbl;
typedef struct { const MsgCtlVtbl *lpVtbl; } MsgCtl;

/* IWebSocketControl：出站缓冲区大小 */
typedef struct { void *q,*a,*r,*gi,*gn,*gt;
    HRESULT (STDMETHODCALLTYPE *get_OutboundBufferSizeInBytes)(void *, UINT32 *);
    HRESULT (STDMETHODCALLTYPE *put_OutboundBufferSizeInBytes)(void *, UINT32);
} CtlVtbl;
typedef struct { const CtlVtbl *lpVtbl; } Ctl;

/* IRetailInfoStatics：IInspectable 之后两个属性 */
typedef struct { void *q,*a,*r,*gi,*gn,*gt;
    HRESULT (STDMETHODCALLTYPE *get_IsDemoModeEnabled)(void *, unsigned char *);
    HRESULT (STDMETHODCALLTYPE *get_Properties)(void *, void **);
} RetailStaticsVtbl;
typedef struct { const RetailStaticsVtbl *lpVtbl; } RetailStatics;

/* IMapView<HSTRING,IInspectable> 只用到 Size，它排在 Lookup/HasKey 之后 */
typedef struct { void *q,*a,*r,*gi,*gn,*gt;
    HRESULT (STDMETHODCALLTYPE *Lookup)(void *, HSTRING, void **);
    HRESULT (STDMETHODCALLTYPE *get_Size)(void *, UINT32 *);
} MapViewVtbl;
typedef struct { const MapViewVtbl *lpVtbl; } MapView;

static IActivationFactory *get_factory(const WCHAR *name, const char *label)
{
    IActivationFactory *f = NULL;
    HSTRING cls = NULL;
    HRESULT hr;
    WindowsCreateString(name, (UINT32)wcslen(name), &cls);
    hr = RoGetActivationFactory(cls, &IID_IActivationFactory, (void **)&f);
    WindowsDeleteString(cls);
    check(SUCCEEDED(hr) && f, label);
    return f;
}

int wmain(void)
{
    IActivationFactory *f;
    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(hr)) { printf("RoInitialize 失败 0x%08lx\n", (unsigned long)hr); return 2; }

    printf("Windows.System.Profile.RetailInfo:\n");
    if ((f = get_factory(L"Windows.System.Profile.RetailInfo", "拿到激活工厂")))
    {
        RetailStatics *st = NULL;
        unsigned char demo = 0xcc;
        void *props = NULL;
        check(SUCCEEDED(IActivationFactory_QueryInterface(f, &IID_IRetailInfoStatics, (void **)&st)) && st,
              "QI IRetailInfoStatics");
        if (st)
        {
            check(SUCCEEDED(st->lpVtbl->get_IsDemoModeEnabled(st, &demo)) && demo == 0,
                  "IsDemoModeEnabled 为假（这台不是展示机）");
            if (SUCCEEDED(st->lpVtbl->get_Properties(st, &props)) && props)
            {
                UINT32 n = 0xcccc;
                MapView *mv = props;
                check(1, "Properties 返回了集合");
                check(SUCCEEDED(mv->lpVtbl->get_Size(mv, &n)) && n == 0, "该集合为空");
            }
            else check(0, "Properties 返回了集合");
        }
    }

    printf("\nWindows.Networking.Sockets.MessageWebSocket:\n");
    if ((f = get_factory(L"Windows.Networking.Sockets.MessageWebSocket", "拿到激活工厂")))
    {
        IInspectable *sock = NULL;
        void *p2 = NULL;
        check(SUCCEEDED(IActivationFactory_ActivateInstance(f, &sock)) && sock, "构造一个套接字");
        if (sock)
        {
            MsgSock *ms = NULL; Sock *ws = NULL; MsgCtl *mc = NULL; Ctl *c = NULL;
            void *ctl = NULL;
            HSTRING n = NULL, v = NULL;
            UINT32 u = 0; int mt = -1;

            check(SUCCEEDED(IInspectable_QueryInterface(sock, &IID_IMessageWebSocket, (void **)&ms)) && ms,
                  "QI IMessageWebSocket");
            check(SUCCEEDED(IInspectable_QueryInterface(sock, &IID_IWebSocket, (void **)&ws)) && ws,
                  "QI IWebSocket");
            check(SUCCEEDED(IInspectable_QueryInterface(sock, &IID_IClosable_, &p2)) && p2, "QI IClosable");

            check(ms && SUCCEEDED(ms->lpVtbl->get_Control(ms, &ctl)) && ctl, "拿到 Control");
            if (ctl)
            {
                check(SUCCEEDED(IInspectable_QueryInterface((IInspectable *)ctl, &IID_IMsgWebSocketControl, (void **)&mc)) && mc,
                      "Control 可 QI IMessageWebSocketControl");
                check(SUCCEEDED(IInspectable_QueryInterface((IInspectable *)ctl, &IID_IWebSocketControl, (void **)&c)) && c,
                      "Control 可 QI IWebSocketControl");
                check(SUCCEEDED(IInspectable_QueryInterface((IInspectable *)ctl, &IID_IWebSocketControl2, &p2)) && p2,
                      "Control 可 QI IWebSocketControl2");
                if (mc)
                {
                    check(SUCCEEDED(mc->lpVtbl->get_MessageType(mc, &mt)) && mt == 0, "MessageType 默认是 Binary");
                    check(SUCCEEDED(mc->lpVtbl->put_MessageType(mc, 1)) &&
                          SUCCEEDED(mc->lpVtbl->get_MessageType(mc, &mt)) && mt == 1, "MessageType 写 Utf8 读回 Utf8");
                    check(mc->lpVtbl->put_MessageType(mc, 7) != S_OK, "MessageType 拒绝非法值");
                    check(SUCCEEDED(mc->lpVtbl->put_MaxMessageSize(mc, 65536)) &&
                          SUCCEEDED(mc->lpVtbl->get_MaxMessageSize(mc, &u)) && u == 65536, "MaxMessageSize 往返");
                }
                if (c)
                {
                    check(SUCCEEDED(c->lpVtbl->put_OutboundBufferSizeInBytes(c, 4096)) &&
                          SUCCEEDED(c->lpVtbl->get_OutboundBufferSizeInBytes(c, &u)) && u == 4096,
                          "OutboundBufferSizeInBytes 往返");
                }
            }
            if (ws)
            {
                WindowsCreateString(L"X-Probe", 7, &n);
                WindowsCreateString(L"yes", 3, &v);
                check(SUCCEEDED(ws->lpVtbl->SetRequestHeader(ws, n, v)), "SetRequestHeader 接受请求头");
                check(ws->lpVtbl->SetRequestHeader(ws, NULL, v) == E_INVALIDARG, "空头名被拒绝");
                check(ws->lpVtbl->ConnectAsync(ws, NULL, &p2) == E_INVALIDARG, "ConnectAsync 拒绝空 URI");
            }
        }
    }

    printf("\nWindows.Web.Http.HttpRequestMessage / HttpResponseMessage:\n");
    if ((f = get_factory(L"Windows.Web.Http.HttpRequestMessage", "请求消息：拿到激活工厂")))
    {
        IInspectable *req = NULL;
        Req *r = NULL; MapSS *h = NULL;
        void *p3 = NULL;
        HSTRING k = NULL, v = NULL, back = NULL;
        UINT32 n = 0xcccc; unsigned char had = 2;

        check(SUCCEEDED(IActivationFactory_ActivateInstance(f, &req)) && req, "构造请求消息");
        if (req)
        {
            check(SUCCEEDED(IInspectable_QueryInterface(req, &IID_IHttpRequestMessage, (void **)&r)) && r,
                  "QI IHttpRequestMessage");
            check(SUCCEEDED(IInspectable_QueryInterface(req, &IID_IClosable_, &p3)) && p3, "QI IClosable");
            if (r && SUCCEEDED(r->lpVtbl->get_Headers(r, (void **)&h)) && h)
            {
                check(1, "拿到 Headers 集合");
                check(SUCCEEDED(IInspectable_QueryInterface((IInspectable *)h, &IID_IMapSS, &p3)) && p3,
                      "Headers 是 IMap<HSTRING,HSTRING>");
                check(SUCCEEDED(IInspectable_QueryInterface((IInspectable *)h, &IID_IIterableKVSS, &p3)) && p3,
                      "Headers 可 QI IIterable");
                WindowsCreateString(L"X-Probe", 7, &k);
                WindowsCreateString(L"yes", 3, &v);
                check(SUCCEEDED(h->lpVtbl->Insert(h, k, v, &had)) && had == 0, "插入一个头（新增）");
                check(SUCCEEDED(h->lpVtbl->get_Size(h, &n)) && n == 1, "Size 为 1");
                check(SUCCEEDED(h->lpVtbl->Lookup(h, k, &back)) && back &&
                      !wcscmp(WindowsGetStringRawBuffer(back, NULL), L"yes"), "查回同一个值");
                {
                    HSTRING k2 = NULL; unsigned char has = 0;
                    WindowsCreateString(L"x-PROBE", 7, &k2);
                    check(SUCCEEDED(h->lpVtbl->HasKey(h, k2, &has)) && has, "头名不分大小写");
                    check(SUCCEEDED(h->lpVtbl->Insert(h, k2, v, &had)) && had == 1, "同名再插入算替换");
                    check(SUCCEEDED(h->lpVtbl->get_Size(h, &n)) && n == 1, "替换后 Size 仍为 1");
                }
                check(SUCCEEDED(h->lpVtbl->Remove(h, k)) && SUCCEEDED(h->lpVtbl->get_Size(h, &n)) && n == 0,
                      "删除后为空");
                check(h->lpVtbl->Remove(h, k) == E_BOUNDS, "删除不存在的头报 E_BOUNDS");
            }
            else check(0, "拿到 Headers 集合");
        }
    }
    if ((f = get_factory(L"Windows.Web.Http.HttpResponseMessage", "响应消息：拿到激活工厂")))
    {
        IInspectable *resp = NULL;
        Resp *r = NULL;
        int code = -1; unsigned char ok = 2;

        check(SUCCEEDED(IActivationFactory_ActivateInstance(f, &resp)) && resp, "构造响应消息");
        if (resp && SUCCEEDED(IInspectable_QueryInterface(resp, &IID_IHttpResponseMessage, (void **)&r)) && r)
        {
            check(1, "QI IHttpResponseMessage");
            check(SUCCEEDED(r->lpVtbl->get_StatusCode(r, &code)) && code == 200, "默认状态码 200");
            check(SUCCEEDED(r->lpVtbl->get_IsSuccessStatusCode(r, &ok)) && ok, "200 算成功");
            check(SUCCEEDED(r->lpVtbl->put_StatusCode(r, 404)) &&
                  SUCCEEDED(r->lpVtbl->get_IsSuccessStatusCode(r, &ok)) && !ok, "404 不算成功");
            check(SUCCEEDED(r->lpVtbl->put_StatusCode(r, 299)) &&
                  SUCCEEDED(r->lpVtbl->get_IsSuccessStatusCode(r, &ok)) && ok, "299 也算成功（按 2xx 判定）");
        }
        else check(0, "QI IHttpResponseMessage");
    }

    printf("\nWindows.Web.Http 的 content 类:\n");
    if ((f = get_factory(L"Windows.Web.Http.HttpStringContent", "字符串内容：拿到激活工厂")))
    {
        StrContentF *cf = NULL;
        Content *c = NULL;
        void *obj = NULL, *p4 = NULL;
        HSTRING text = NULL, mt = NULL, back = NULL;
        UINT64 len = 0; unsigned char ok = 0;

        check(SUCCEEDED(IActivationFactory_QueryInterface(f, &IID_IHttpStringContentF, (void **)&cf)) && cf,
              "QI IHttpStringContentFactory");
        /* 4 个汉字 = UTF-16 四个码元，UTF-8 十二字节 */
        WindowsCreateString(L"你好世界", 4, &text);
        if (cf && SUCCEEDED(cf->lpVtbl->CreateFromString(cf, text, &obj)) && obj)
        {
            check(1, "从字符串构造");
            c = obj;
            check(SUCCEEDED(IInspectable_QueryInterface((IInspectable *)obj, &IID_IHttpContent, &p4)) && p4,
                  "QI IHttpContent");
            check(SUCCEEDED(c->lpVtbl->TryComputeLength(c, &len, &ok)) && ok && len == 12,
                  "长度按 UTF-8 字节算（4 个汉字 = 12 字节）");
            {
                MapSS *h = NULL; HSTRING k = NULL;
                WindowsCreateString(L"Content-Type", 12, &k);
                if (SUCCEEDED(c->lpVtbl->get_Headers(c, (void **)&h)) && h)
                    check(SUCCEEDED(h->lpVtbl->Lookup(h, k, &back)) && back &&
                          wcsstr(WindowsGetStringRawBuffer(back, NULL), L"utf-8") != NULL,
                          "默认 Content-Type 带 charset=utf-8");
                else check(0, "默认 Content-Type 带 charset=utf-8");
            }
            {
                Stringable *st = NULL;
                if (SUCCEEDED(IInspectable_QueryInterface((IInspectable *)obj, &IID_IStringable_, (void **)&st)) && st)
                    check(SUCCEEDED(st->lpVtbl->ToString(st, &back)) && back &&
                          !wcscmp(WindowsGetStringRawBuffer(back, NULL), L"你好世界"),
                          "ToString 给回原文");
                else check(0, "ToString 给回原文");
            }
            WindowsCreateString(L"application/json", 16, &mt);
            if (SUCCEEDED(cf->lpVtbl->CreateFromStringWithEncodingAndMediaType(cf, text, 0, mt, &obj)) && obj)
            {
                MapSS *h = NULL; HSTRING k = NULL;
                c = obj;
                WindowsCreateString(L"Content-Type", 12, &k);
                if (SUCCEEDED(c->lpVtbl->get_Headers(c, (void **)&h)) && h)
                    check(SUCCEEDED(h->lpVtbl->Lookup(h, k, &back)) && back &&
                          !wcscmp(WindowsGetStringRawBuffer(back, NULL), L"application/json"), "指定媒体类型生效");
                else check(0, "指定媒体类型生效");
            }
            else check(0, "指定媒体类型生效");
            check(cf->lpVtbl->CreateFromStringWithEncoding(cf, text, 1, &obj) == E_NOTIMPL,
                  "非 UTF-8 编码如实报未实现");
        }
        else check(0, "从字符串构造");
    }
    if ((f = get_factory(L"Windows.Web.Http.HttpMultipartFormDataContent", "多段内容：拿到激活工厂")))
    {
        IInspectable *mp = NULL;
        Multipart *m = NULL;
        void *p5 = NULL;
        check(SUCCEEDED(IActivationFactory_ActivateInstance(f, &mp)) && mp, "构造多段内容");
        if (mp)
        {
            check(SUCCEEDED(IInspectable_QueryInterface(mp, &IID_IHttpMultipart, (void **)&m)) && m,
                  "QI IHttpMultipartFormDataContent");
            check(SUCCEEDED(IInspectable_QueryInterface(mp, &IID_IHttpContent, &p5)) && p5,
                  "多段内容本身也是 IHttpContent");
            if (m) check(m->lpVtbl->Add(m, NULL) == E_INVALIDARG, "拒绝加入空内容");
        }
    }
    (void)get_factory(L"Windows.Web.Http.HttpBufferContent", "缓冲内容：拿到激活工厂");
    (void)get_factory(L"Windows.Web.Http.HttpStreamContent", "流内容：拿到激活工厂");

    printf("\nWindows.Security.EnterpriseData.ProtectionPolicyManager:\n");
    if ((f = get_factory(L"Windows.Security.EnterpriseData.ProtectionPolicyManager", "拿到激活工厂")))
    {
        PPMS *st = NULL; PPMS2 *st2 = NULL;
        HSTRING id = NULL;
        unsigned char b = 2; int r = -1;
        void *obj = NULL, *p6 = NULL;
        INT64 tok = 0;

        WindowsCreateString(L"contoso.com", 11, &id);
        check(SUCCEEDED(IActivationFactory_QueryInterface(f, &IID_IPPMStatics, (void **)&st)) && st,
              "QI IProtectionPolicyManagerStatics");
        check(SUCCEEDED(IActivationFactory_QueryInterface(f, &IID_IPPMStatics2, (void **)&st2)) && st2,
              "QI IProtectionPolicyManagerStatics2");
        if (st)
        {
            check(SUCCEEDED(st->lpVtbl->IsIdentityManaged(st, id, &b)) && !b, "没有身份被管理");
            check(SUCCEEDED(st->lpVtbl->CheckAccess(st, id, id, &r)) && r == 0, "CheckAccess 为 Allowed");
            check(SUCCEEDED(st->lpVtbl->TryApplyProcessUIPolicy(st, id, &b)) && !b, "不应用企业界面策略");
            check(SUCCEEDED(st->lpVtbl->ClearProcessUIPolicy(st)), "清除界面策略成功");
            check(SUCCEEDED(st->lpVtbl->RevokeContent(st, id)), "撤销内容成功（无内容可撤）");
            check(SUCCEEDED(st->lpVtbl->CreateCurrentThreadNetworkContext(st, id, &obj)) && obj,
                  "拿到线程网络上下文");
            if (obj) check(SUCCEEDED(IInspectable_QueryInterface((IInspectable *)obj, &IID_IClosable_, &p6)) && p6,
                           "网络上下文可 QI IClosable");
            check(SUCCEEDED(st->lpVtbl->GetForCurrentView(st, &obj)) && obj, "GetForCurrentView 给出管理器");
            if (obj)
            {
                check(SUCCEEDED(IInspectable_QueryInterface((IInspectable *)obj, &IID_IPPManager, &p6)) && p6,
                      "QI IProtectionPolicyManager");
                check(SUCCEEDED(IInspectable_QueryInterface((IInspectable *)obj, &IID_IPPManager2, &p6)) && p6,
                      "QI IProtectionPolicyManager2");
            }
            check(SUCCEEDED(st->lpVtbl->add1(st, (void *)st, &tok)) && tok != 0, "事件注册给出非零 token");
            check(SUCCEEDED(st->lpVtbl->rm1(st, tok)), "事件可注销");
        }
        if (st2)
        {
            check(SUCCEEDED(st2->lpVtbl->get_IsProtectionEnabled(st2, &b)) && !b, "保护未启用");
            check(SUCCEEDED(st2->lpVtbl->GetEnforcementLevel(st2, id, &r)) && r == 0, "强制级别为 NoProtection");
            check(SUCCEEDED(st2->lpVtbl->IsUserDecryptionAllowed(st2, id, &b)) && b, "允许用户解密");
            check(SUCCEEDED(st2->lpVtbl->IsProtectionUnderLockRequired(st2, id, &b)) && !b, "不要求锁下保护");
            check(SUCCEEDED(st2->lpVtbl->HasContentBeenRevokedSince(st2, id, 0, &b)) && !b, "没有内容被撤销");
            check(SUCCEEDED(st2->lpVtbl->RequestAccessForAppAsync(st2, id, id, &obj)) && obj,
                  "RequestAccessForAppAsync 给出异步操作");
        }
    }

    printf("\nWindows.ApplicationModel.UserActivities.UserActivityRequestManager:\n");
    if ((f = get_factory(L"Windows.ApplicationModel.UserActivities.UserActivityRequestManager", "拿到激活工厂")))
    {
        UARS *st = NULL; UAR *m = NULL;
        void *obj = NULL, *p7 = NULL; INT64 tok = 0;
        check(SUCCEEDED(IActivationFactory_QueryInterface(f, &IID_IUARManagerStatics, (void **)&st)) && st,
              "QI IUserActivityRequestManagerStatics");
        if (st && SUCCEEDED(st->lpVtbl->GetForCurrentView(st, &obj)) && obj)
        {
            check(1, "GetForCurrentView 给出管理器");
            m = obj;
            check(SUCCEEDED(IInspectable_QueryInterface((IInspectable *)obj, &IID_IUARManager, &p7)) && p7,
                  "QI IUserActivityRequestManager");
            check(SUCCEEDED(m->lpVtbl->add_UserActivityRequested(m, (void *)m, &tok)) && tok != 0,
                  "注册请求处理器拿到 token");
            check(m->lpVtbl->add_UserActivityRequested(m, NULL, &tok) == E_INVALIDARG, "拒绝空处理器");
            check(SUCCEEDED(m->lpVtbl->remove_UserActivityRequested(m, tok)), "可注销");
        }
        else check(0, "GetForCurrentView 给出管理器");
    }

    printf("\nWindows.Management.Deployment.AddPackageOptions:\n");
    if ((f = get_factory(L"Windows.Management.Deployment.AddPackageOptions", "拿到激活工厂")))
    {
        IInspectable *o = NULL; APO *a = NULL;
        int so = -1; unsigned char dm = 2;
        check(SUCCEEDED(IActivationFactory_ActivateInstance(f, &o)) && o, "构造选项对象");
        if (o && SUCCEEDED(IInspectable_QueryInterface(o, &IID_IAddPackageOptions, (void **)&a)) && a)
        {
            check(1, "QI IAddPackageOptions");
            check(SUCCEEDED(a->lpVtbl->get_StubPackageOption(a, &so)) && so == 0, "StubPackageOption 默认为 Default");
            check(SUCCEEDED(a->lpVtbl->put_StubPackageOption(a, 2)) &&
                  SUCCEEDED(a->lpVtbl->get_StubPackageOption(a, &so)) && so == 2, "StubPackageOption 往返");
            check(a->lpVtbl->put_StubPackageOption(a, 9) == E_INVALIDARG, "拒绝非法 StubPackageOption");
            check(SUCCEEDED(a->lpVtbl->put_DeveloperMode(a, 1)) &&
                  SUCCEEDED(a->lpVtbl->get_DeveloperMode(a, &dm)) && dm, "DeveloperMode 往返");
        }
        else check(0, "QI IAddPackageOptions");
    }

    printf("\n%s  失败 %d 项\n", fails ? "有问题" : "全部通过", fails);
    return fails != 0;
}
