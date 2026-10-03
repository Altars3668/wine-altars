#define COBJMACROS
#define CONST_VTABLE
#include "probe.h"
#include <objbase.h>
#include <initguid.h>

/* 不使用自由线程封送器，确保得到真正的跨套间 IStream 代理。 */
typedef struct { IStream iface; LONG references; } probe_stream;

static HRESULT WINAPI stream_query(IStream *iface, REFIID iid, void **object)
{
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_ISequentialStream) ||
        IsEqualIID(iid, &IID_IStream))
    {
        *object = iface;
        IStream_AddRef(iface);
        return S_OK;
    }
    *object = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI stream_addref(IStream *iface)
{
    return InterlockedIncrement(&((probe_stream *)iface)->references);
}
static ULONG WINAPI stream_release(IStream *iface)
{
    return InterlockedDecrement(&((probe_stream *)iface)->references);
}
static HRESULT WINAPI stream_read(IStream *iface, void *buffer, ULONG length, ULONG *read)
{
    (void)iface; (void)buffer; (void)length;
    if (read) *read = 0;
    return S_FALSE;
}
static HRESULT WINAPI stream_write(IStream *iface, const void *buffer, ULONG length, ULONG *written)
{
    (void)iface; (void)buffer; (void)length;
    if (written) *written = 0;
    return STG_E_ACCESSDENIED;
}
static HRESULT WINAPI stream_seek(IStream *iface, LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER *position)
{
    (void)iface; (void)move; (void)origin;
    if (position) position->QuadPart = 0;
    return S_OK;
}
static HRESULT WINAPI stream_size(IStream *iface, ULARGE_INTEGER size)
{
    (void)iface; (void)size;
    return STG_E_ACCESSDENIED;
}
static HRESULT WINAPI stream_copy(IStream *iface, IStream *other, ULARGE_INTEGER size,
                                 ULARGE_INTEGER *read, ULARGE_INTEGER *written)
{
    (void)iface; (void)other; (void)size;
    if (read) read->QuadPart = 0;
    if (written) written->QuadPart = 0;
    return E_NOTIMPL;
}
static HRESULT WINAPI stream_commit(IStream *iface, DWORD flags)
{
    (void)iface; (void)flags;
    return S_OK;
}
static HRESULT WINAPI stream_revert(IStream *iface)
{
    (void)iface;
    return STG_E_REVERTED;
}
static HRESULT WINAPI stream_lock(IStream *iface, ULARGE_INTEGER offset, ULARGE_INTEGER size, DWORD type)
{
    (void)iface; (void)offset; (void)size; (void)type;
    return E_NOTIMPL;
}
static HRESULT WINAPI stream_stat(IStream *iface, STATSTG *stat, DWORD flags)
{
    (void)iface; (void)flags;
    memset(stat, 0, sizeof(*stat));
    stat->type = STGTY_STREAM;
    stat->grfMode = STGM_READ;
    return S_OK;
}
static HRESULT WINAPI stream_clone(IStream *iface, IStream **other)
{
    (void)iface;
    *other = NULL;
    return E_NOTIMPL;
}
static const IStreamVtbl stream_vtable =
{
    stream_query, stream_addref, stream_release, stream_read, stream_write, stream_seek,
    stream_size, stream_copy, stream_commit, stream_revert, stream_lock, stream_lock,
    stream_stat, stream_clone
};

typedef struct
{
    IStream *marshaled;
    HANDLE complete;
    DWORD owner_thread;
} proxy_context;

static DWORD WINAPI inspect_proxy(void *parameter)
{
    proxy_context *context = parameter;
    IStream *proxy = NULL;
    IUnknown *security = NULL;
    DWORD authn = PROBE_SENTINEL, authz = PROBE_SENTINEL, level = PROBE_SENTINEL;
    DWORD impersonation = PROBE_SENTINEL, capabilities = PROBE_SENTINEL;
    RPC_AUTH_IDENTITY_HANDLE identity = NULL;
    WCHAR *principal = NULL;
    STATSTG stat;
    HRESULT init, hr;

    init = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    printf("proxy_MTA init_hr=%#lx different_thread=%u\n", (ULONG)init,
           GetCurrentThreadId() != context->owner_thread);
    if (SUCCEEDED(init))
    {
        hr = CoGetInterfaceAndReleaseStream(context->marshaled, &IID_IStream, (void **)&proxy);
        context->marshaled = NULL;
        printf("proxy unmarshal_hr=%#lx nonnull=%u\n", (ULONG)hr, !!proxy);
        if (SUCCEEDED(hr) && proxy)
        {
            hr = IStream_QueryInterface(proxy, &IID_IClientSecurity, (void **)&security);
            printf("proxy QI_IClientSecurity_hr=%#lx nonnull=%u\n", (ULONG)hr, !!security);
            if (security) IUnknown_Release(security);
            hr = CoQueryProxyBlanket((IUnknown *)proxy, &authn, &authz, &principal,
                                    &level, &impersonation, &identity, &capabilities);
            printf("proxy blanket_hr=%#lx authn_service=%lu authz_service=%lu authn_level=%lu"
                   " imp_level=%lu capabilities=%#lx principal_characters=%llu identity_present=%u\n",
                   (ULONG)hr, authn, authz, level, impersonation, capabilities,
                   (unsigned long long)(principal ? wcslen(principal) : 0), !!identity);
            if (principal) CoTaskMemFree(principal);
            memset(&stat, 0xa5, sizeof(stat));
            hr = IStream_Stat(proxy, &stat, STATFLAG_NONAME);
            printf("proxy Stat_hr=%#lx type=%lu size=%llu\n", (ULONG)hr,
                   stat.type, (unsigned long long)stat.cbSize.QuadPart);
            if (SUCCEEDED(hr) && stat.pwcsName) CoTaskMemFree(stat.pwcsName);
            IStream_Release(proxy);
        }
        CoUninitialize();
    }
    SetEvent(context->complete);
    return 0;
}

static HRESULT initialize_security(DWORD level, DWORD impersonation, DWORD capabilities)
{
    HRESULT hr;
    DWORD error;
    SetLastError(PROBE_SENTINEL);
    hr = CoInitializeSecurity(NULL, -1, NULL, NULL, level, impersonation, NULL, capabilities, NULL);
    error = GetLastError();
    printf("CoInitializeSecurity cAuthSvc=-1 authn_level=%lu imp_level=%lu capabilities=%#lx"
           " hr=%#lx gle=%lu\n", level, impersonation, capabilities, (ULONG)hr, error);
    return hr;
}

static void cross_apartment(probe_stream *object, IStream *already_marshaled)
{
    proxy_context context;
    HANDLE thread;
    MSG message;
    HRESULT hr;
    DWORD wait, begin = GetTickCount();

    memset(&context, 0, sizeof(context));
    context.owner_thread = GetCurrentThreadId();
    context.marshaled = already_marshaled;
    if (!context.marshaled)
    {
        hr = CoMarshalInterThreadInterfaceInStream(&IID_IStream, (IUnknown *)&object->iface,
                                                   &context.marshaled);
        printf("marshal_for_proxy_hr=%#lx\n", (ULONG)hr);
        if (FAILED(hr)) return;
    }
    context.complete = CreateEventW(NULL, TRUE, FALSE, NULL);
    thread = context.complete ? CreateThread(NULL, 0, inspect_proxy, &context, 0, NULL) : NULL;
    if (!thread)
    {
        printf("proxy_thread_failed gle=%lu\n", GetLastError());
        if (context.complete) CloseHandle(context.complete);
        /* 尚未交给另一个套间的封送数据仍由当前套间释放。 */
        {
            LARGE_INTEGER zero;
            zero.QuadPart = 0;
            IStream_Seek(context.marshaled, zero, STREAM_SEEK_SET, NULL);
            CoReleaseMarshalData(context.marshaled);
            IStream_Release(context.marshaled);
        }
        return;
    }
    do
    {
        wait = MsgWaitForMultipleObjects(1, &context.complete, FALSE, 50, QS_ALLINPUT);
        while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    } while (wait != WAIT_OBJECT_0 && GetTickCount() - begin < 2200);
    if (wait != WAIT_OBJECT_0)
    {
        puts("proxy_timeout=1\ndone");
        ExitProcess(124);
    }
    WaitForSingleObject(thread, 200);
    CloseHandle(thread);
    CloseHandle(context.complete);
    if (context.marshaled)
    {
        LARGE_INTEGER zero;
        zero.QuadPart = 0;
        IStream_Seek(context.marshaled, zero, STREAM_SEEK_SET, NULL);
        CoReleaseMarshalData(context.marshaled);
        IStream_Release(context.marshaled);
    }
    printf("object_reference_count_after_proxy=%ld\n", object->references);
}

static int run_case(const char *label)
{
    probe_stream object;
    IStream *marshaled = NULL;
    HRESULT init, hr;
    BOOL before = !strcmp(label, "before"), marshal_first = !strcmp(label, "marshal_first");
    DWORD level = RPC_C_AUTHN_LEVEL_DEFAULT, impersonation = RPC_C_IMP_LEVEL_IMPERSONATE;
    DWORD capabilities = EOAC_NONE;

    object.iface.lpVtbl = &stream_vtable;
    object.references = 1;
    printf("case=%s\n", label);
    if (before) initialize_security(level, impersonation, capabilities);
    init = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    printf("owner_STA init_hr=%#lx\n", (ULONG)init);
    if (FAILED(init)) return 0;
    if (marshal_first)
    {
        hr = CoMarshalInterThreadInterfaceInStream(&IID_IStream, (IUnknown *)&object.iface, &marshaled);
        printf("marshal_before_security_hr=%#lx\n", (ULONG)hr);
    }
    if (!strcmp(label, "bad_authn")) level = 0x7fffffffU;
    if (!strcmp(label, "bad_capabilities")) capabilities = 0x80000000U;
    if (!strcmp(label, "explicit"))
    {
        level = RPC_C_AUTHN_LEVEL_PKT_PRIVACY;
        impersonation = RPC_C_IMP_LEVEL_IDENTIFY;
    }
    if (!before) initialize_security(level, impersonation, capabilities);
    if (!strcmp(label, "second")) initialize_security(level, impersonation, capabilities);
    cross_apartment(&object, marshaled);
    IStream_Release(&object.iface);
    CoUninitialize();
    return 0;
}

int main(int argc, char **argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    static const char *cases[] = {"office", "second", "before", "marshal_first",
                                  "bad_authn", "bad_capabilities", "explicit"};
    WCHAR arguments[100];
    unsigned int i;
    DWORD begin, exit_code;
    int result = 0;

    if (!probe_start()) return 1;
    if (argc == 3 && !strcmp(argv[1], "--case"))
    {
        for (i = 0; i < ARRAYSIZE(cases); ++i)
            if (!strcmp(argv[2], cases[i])) return probe_done(run_case(cases[i]));
        puts("unknown_case");
        return probe_done(2);
    }
    if (argc != 1)
    {
        puts("usage: cosecurity.exe [--case office|second|before|marshal_first|bad_authn|bad_capabilities|explicit]");
        return probe_done(2);
    }
    begin = GetTickCount();
    for (i = 0; i < ARRAYSIZE(cases); ++i)
    {
        if (GetTickCount() - begin > 23000)
        {
            puts("suite_budget_exhausted=1");
            result = 124;
            break;
        }
        swprintf(arguments, ARRAYSIZE(arguments), L"--case %hs", cases[i]);
        printf("spawn_case=%s\n", cases[i]);
        exit_code = probe_child(arguments, 3200);
        if (exit_code) result = 1;
    }
    return probe_done(result);
}
