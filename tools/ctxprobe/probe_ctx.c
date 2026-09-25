/* probe_ctx: COM object contexts -- what CoGetContextToken/CoGetObjectContext hand out per
 * apartment, and where IContextCallback::ContextCallback runs its callback from each kind of
 * caller.  C++/WinRT resumes every co_await through ContextCallback. */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <ctxtcall.h>
#include <stdio.h>

static const IID IID_ICallbackWithNoReentrancyToApplicationSTA_ =
    {0x0a299774, 0x3e4e, 0xfc42, {0x1d, 0x9d, 0x72, 0xce, 0xe1, 0x05, 0xca, 0x57}};
static const IID IID_IEnterActivityWithNoLock_ =
    {0xd7174f82, 0x36b8, 0x4aa8, {0x80, 0x0a, 0xe9, 0x63, 0xab, 0x2d, 0xfa, 0xb9}};
static const IID IID_IObjContext_ = {0x000001c6, 0x0000, 0x0000, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
static const IID IID_random = {0x12345678, 0x1234, 0x1234, {1, 2, 3, 4, 5, 6, 7, 8}};

static DWORD main_tid, m1_tid, m2_tid, s2_tid;
static IContextCallback *main_ctx, *m1_ctx, *s2_ctx;
static ULONG_PTR main_token, m1_token, m2_token, s2_token;
static HANDLE m1_ready, m2_ready, s2_ready, s2_go, done_event, m1_go;
static volatile LONG main_pumping;

static const char *who(DWORD tid)
{
    static char buf[4][32];
    static int n;
    char *b = buf[n++ % 4];
    if (tid == main_tid) return "main";
    if (tid == m1_tid) return "m1";
    if (tid == m2_tid) return "m2";
    if (tid == s2_tid) return "s2";
    sprintf(b, "other");
    return b;
}

struct call { const char *what; DWORD caller; };

static HRESULT CALLBACK callback(ComCallData *data)
{
    struct call *call = data->pUserDefined;
    APTTYPE type = -1;
    APTTYPEQUALIFIER q = -1;
    ULONG_PTR token = 0;
    HRESULT hr = CoGetApartmentType(&type, &q);
    CoGetContextToken(&token);
    printf("    [%s] callback from %s runs on %s: apt %#lx %d %d, dispid %lu reserved %lu, token %s\n", call->what,
           who(call->caller), who(GetCurrentThreadId()), hr, type, q, data->dwDispid, data->dwReserved,
           token == main_token ? "main" : token == m1_token ? "mta" : token == s2_token ? "s2" : "other");
    return 0x80001234;
}

static void do_call(const char *what, IContextCallback *ctx, REFIID iid, int method)
{
    struct call call = {what, GetCurrentThreadId()};
    ComCallData data = {7, 9, &call};
    HRESULT hr;
    DWORD start = GetTickCount();
    hr = IContextCallback_ContextCallback(ctx, callback, &data, iid, method, NULL);
    printf("  %s: ContextCallback %#lx (%s)\n", what, hr, GetTickCount() - start > 1000 ? "slow" : "fast");
}

static void describe(const char *what, IContextCallback *ctx)
{
    IComThreadingInfo *info;
    IUnknown *unk;
    APTTYPE type = -1;
    THDTYPE thd = -1;
    HRESULT hr;

    hr = IContextCallback_QueryInterface(ctx, &IID_IComThreadingInfo, (void **)&info);
    printf("  %s: IComThreadingInfo %#lx", what, hr);
    if (SUCCEEDED(hr))
    {
        hr = IComThreadingInfo_GetCurrentApartmentType(info, &type);
        printf(", apartment type %#lx %d", hr, type);
        hr = IComThreadingInfo_GetCurrentThreadType(info, &thd);
        printf(", thread type %#lx %d", hr, thd);
        IComThreadingInfo_Release(info);
    }
    hr = IContextCallback_QueryInterface(ctx, &IID_IObjContext_, (void **)&unk);
    printf(", IObjContext %#lx", hr);
    if (SUCCEEDED(hr)) IUnknown_Release(unk);
    hr = IContextCallback_QueryInterface(ctx, &IID_IMarshal, (void **)&unk);
    printf(", IMarshal %#lx", hr);
    if (SUCCEEDED(hr)) IUnknown_Release(unk);
    hr = IContextCallback_QueryInterface(ctx, &IID_IAgileObject, (void **)&unk);
    printf(", IAgileObject %#lx\n", hr);
    if (SUCCEEDED(hr)) IUnknown_Release(unk);
}

static DWORD WINAPI m1_thread(void *arg)
{
    IContextCallback *again;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    CoGetContextToken(&m1_token);
    CoGetObjectContext(&IID_IContextCallback, (void **)&m1_ctx);
    CoGetObjectContext(&IID_IContextCallback, (void **)&again);
    printf("m1: token %s the object, twice the same %d\n", m1_token == (ULONG_PTR)m1_ctx ? "is" : "is not", again == m1_ctx);
    IContextCallback_Release(again);
    SetEvent(m1_ready);

    WaitForSingleObject(m1_go, INFINITE);
    /* MTA -> main STA, which pumps */
    do_call("mta->sta", main_ctx, &IID_ICallbackWithNoReentrancyToApplicationSTA_, 5);
    do_call("mta->mta(own)", m1_ctx, &IID_ICallbackWithNoReentrancyToApplicationSTA_, 5);
    SetEvent(done_event);
    WaitForSingleObject(m1_go, INFINITE);
    CoUninitialize();
    return 0;
}

static DWORD WINAPI m2_thread(void *arg)
{
    IContextCallback *ctx;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    CoGetContextToken(&m2_token);
    CoGetObjectContext(&IID_IContextCallback, (void **)&ctx);
    printf("m2: same token as m1 %d, same object as m1 %d\n", m2_token == m1_token, ctx == m1_ctx);
    do_call("mta(m2)->mta(m1's)", m1_ctx, &IID_ICallbackWithNoReentrancyToApplicationSTA_, 5);
    IContextCallback_Release(ctx);
    SetEvent(m2_ready);
    CoUninitialize();
    return 0;
}

static DWORD WINAPI s2_thread(void *arg)
{
    MSG msg;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    CoGetContextToken(&s2_token);
    CoGetObjectContext(&IID_IContextCallback, (void **)&s2_ctx);
    printf("s2: token differs from main %d\n", s2_token != main_token);
    SetEvent(s2_ready);
    WaitForSingleObject(s2_go, INFINITE);
    /* another STA -> main STA */
    do_call("sta2->sta(main)", main_ctx, &IID_ICallbackWithNoReentrancyToApplicationSTA_, 5);
    SetEvent(done_event);
    /* pump for a while so that main can call into this apartment */
    {
        DWORD end = GetTickCount() + 1500;
        while (GetTickCount() < end)
        {
            MsgWaitForMultipleObjects(0, NULL, FALSE, 50, QS_ALLINPUT);
            while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        }
    }
    CoUninitialize();
    return 0;
}

static DWORD WINAPI implicit_thread(void *arg)
{
    IContextCallback *ctx = NULL;
    APTTYPEQUALIFIER q = -1;
    ULONG_PTR token = 0;
    APTTYPE type = -1;
    HRESULT hr;

    hr = CoGetApartmentType(&type, &q);
    printf("implicit: apartment %#lx %d %d\n", hr, type, q);
    hr = CoGetContextToken(&token);
    printf("implicit: token %#lx %s\n", hr, token == m1_token ? "mta" : token ? "other" : "none");
    hr = CoGetObjectContext(&IID_IContextCallback, (void **)&ctx);
    printf("implicit: object context %#lx %s\n", hr, ctx == m1_ctx ? "mta" : ctx ? "other" : "none");
    if (ctx) IContextCallback_Release(ctx);
    do_call("implicit->mta", m1_ctx, &IID_ICallbackWithNoReentrancyToApplicationSTA_, 5);
    do_call("implicit->sta(main)", main_ctx, &IID_ICallbackWithNoReentrancyToApplicationSTA_, 5);
    SetEvent(done_event);
    return 0;
}

static void pump_until(HANDLE event)
{
    MSG msg;
    while (MsgWaitForMultipleObjects(1, &event, FALSE, 5000, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
}

int main(void)
{
    IContextCallback *again;
    ULONG_PTR token;
    HANDLE thread;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    main_tid = GetCurrentThreadId();
    m1_ready = CreateEventW(NULL, FALSE, FALSE, NULL);
    m2_ready = CreateEventW(NULL, FALSE, FALSE, NULL);
    s2_ready = CreateEventW(NULL, FALSE, FALSE, NULL);
    s2_go = CreateEventW(NULL, FALSE, FALSE, NULL);
    m1_go = CreateEventW(NULL, FALSE, FALSE, NULL);
    done_event = CreateEventW(NULL, FALSE, FALSE, NULL);

    hr = CoGetContextToken(&token);
    printf("uninitialized: CoGetContextToken %#lx\n", hr);
    hr = CoGetObjectContext(&IID_IContextCallback, (void **)&again);
    printf("uninitialized: CoGetObjectContext %#lx\n", hr);

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    CoGetContextToken(&main_token);
    CoGetObjectContext(&IID_IContextCallback, (void **)&main_ctx);
    CoGetObjectContext(&IID_IContextCallback, (void **)&again);
    printf("main: token %s the object, twice the same %d\n", main_token == (ULONG_PTR)main_ctx ? "is" : "is not",
           again == main_ctx);
    IContextCallback_Release(again);
    describe("main ctx", main_ctx);

    /* on its own thread, with every IID and method */
    do_call("sta->sta(own) NoReentrancy/5", main_ctx, &IID_ICallbackWithNoReentrancyToApplicationSTA_, 5);
    do_call("sta->sta(own) NoReentrancy/3", main_ctx, &IID_ICallbackWithNoReentrancyToApplicationSTA_, 3);
    do_call("sta->sta(own) EnterActivity/5", main_ctx, &IID_IEnterActivityWithNoLock_, 5);
    do_call("sta->sta(own) IUnknown/0", main_ctx, &IID_IUnknown, 0);
    do_call("sta->sta(own) IUnknown/5", main_ctx, &IID_IUnknown, 5);
    do_call("sta->sta(own) IDispatch/6", main_ctx, &IID_IDispatch, 6);
    do_call("sta->sta(own) random/5", main_ctx, &IID_random, 5);

    thread = CreateThread(NULL, 0, m1_thread, NULL, 0, &m1_tid);
    WaitForSingleObject(m1_ready, INFINITE);
    describe("mta ctx from main", m1_ctx);

    thread = CreateThread(NULL, 0, m2_thread, NULL, 0, &m2_tid);
    WaitForSingleObject(m2_ready, INFINITE);

    /* main STA -> MTA */
    do_call("sta->mta NoReentrancy/5", m1_ctx, &IID_ICallbackWithNoReentrancyToApplicationSTA_, 5);
    do_call("sta->mta IUnknown/0", m1_ctx, &IID_IUnknown, 0);

    /* MTA -> main STA while main pumps */
    SetEvent(m1_go);
    pump_until(done_event);

    /* a thread in no apartment while the MTA exists, as a thread pool thread is */
    CreateThread(NULL, 0, implicit_thread, NULL, 0, NULL);
    pump_until(done_event);

    thread = CreateThread(NULL, 0, s2_thread, NULL, 0, &s2_tid);
    WaitForSingleObject(s2_ready, INFINITE);
    describe("s2 ctx from main", s2_ctx);
    SetEvent(s2_go);
    pump_until(done_event);

    /* main STA -> another STA, which pumps */
    do_call("sta(main)->sta2 NoReentrancy/5", s2_ctx, &IID_ICallbackWithNoReentrancyToApplicationSTA_, 5);
    WaitForSingleObject(thread, 5000);
    /* its thread is gone */
    do_call("sta(main)->sta2 gone", s2_ctx, &IID_ICallbackWithNoReentrancyToApplicationSTA_, 5);

    SetEvent(m1_go);
    IContextCallback_Release(s2_ctx);
    IContextCallback_Release(m1_ctx);
    IContextCallback_Release(main_ctx);
    CoUninitialize();
    printf("done\n");
    return 0;
}
