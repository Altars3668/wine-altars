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

    printf("\n%s  失败 %d 项\n", fails ? "有问题" : "全部通过", fails);
    return fails != 0;
}
