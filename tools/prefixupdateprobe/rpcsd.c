#define _WIN32_WINNT 0x0601
#define UNICODE
#include "probe.h"
#include <rpc.h>
#include <sddl.h>
#include "rpcsd.h"

typedef RPC_STATUS (RPC_ENTRY *register_if3_fn)(RPC_IF_HANDLE, UUID *, RPC_MGR_EPV *,
    unsigned int, unsigned int, unsigned int, RPC_IF_CALLBACK_FN *, void *);

static volatile DWORD rpc_exception;

void *__RPC_USER MIDL_user_allocate(SIZE_T size) { return malloc(size); }
void __RPC_USER MIDL_user_free(void *pointer) { free(pointer); }

long __cdecl s_ping(handle_t binding, long value)
{
    ULONG level = PROBE_SENTINEL, service = PROBE_SENTINEL, authorization = PROBE_SENTINEL;
    RPC_AUTHZ_HANDLE privileges = NULL;
    RPC_WSTR principal = NULL;
    RPC_STATUS status;
    (void)binding;
    status = RpcBindingInqAuthClientW(NULL, &privileges, &principal, &level,
                                     &service, &authorization);
    printf("server auth_query_status=%ld authn_level=%lu authn_service=%lu authz_service=%lu"
           " principal_characters=%llu identity_present=%u\n",
           status, level, service, authorization,
           (unsigned long long)(principal ? wcslen((WCHAR *)principal) : 0), !!privileges);
    if (principal) RpcStringFreeW(&principal);
    return value + 1;
}

/* MinGW 的 RpcTryExcept 不支持 GCC；x64 __try1 安装真正的 SEH 展开范围。 */
LONG __attribute__((used)) rpc_filter(EXCEPTION_POINTERS *exception, void *frame)
{
    DWORD code = exception->ExceptionRecord->ExceptionCode;
    (void)frame;
    if (code < 0x10000U)
    {
        rpc_exception = code;
        return EXCEPTION_EXECUTE_HANDLER;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* GCC 不识别内联汇编的 SEH 边界，会把不返回的分支挪到边界外。
 * 将分支放入非内联函数，使受保护范围内只包含一个必经的调用。 */
static long __attribute__((noinline)) invoke_body(RPC_BINDING_HANDLE binding, BOOL selftest)
{
    if (selftest) RpcRaiseException(RPC_S_ACCESS_DENIED);
    return c_ping(binding, 41);
}

static long __attribute__((noinline)) invoke(RPC_BINDING_HANDLE binding, BOOL selftest)
{
    volatile long result = (long)PROBE_SENTINEL;
    rpc_exception = 0;
    __try1(rpc_filter)
    result = invoke_body(binding, selftest);
    __except1
    return result;
}

static void client_call(const WCHAR *endpoint, BOOL authenticated)
{
    RPC_WSTR text = NULL;
    RPC_BINDING_HANDLE binding = NULL;
    RPC_SECURITY_QOS qos;
    RPC_STATUS status, free_status;
    long value;

    status = RpcStringBindingComposeW(NULL, (RPC_WSTR)L"ncalrpc", NULL,
                                     (RPC_WSTR)endpoint, NULL, &text);
    printf("client auth=%s compose=%ld\n", authenticated ? "WINNT" : "none", status);
    if (status) return;
    status = RpcBindingFromStringBindingW(text, &binding);
    RpcStringFreeW(&text);
    printf("client binding=%ld\n", status);
    if (status) return;
    status = RpcBindingSetOption(binding, RPC_C_OPT_CALL_TIMEOUT, 600);
    printf("client call_timeout_ms=600 status=%ld\n", status);
    if (status)
    {
        RpcBindingFree(&binding);
        puts("client skipped: timeout option failed");
        return;
    }
    if (authenticated)
    {
        memset(&qos, 0, sizeof(qos));
        qos.Version = RPC_C_SECURITY_QOS_VERSION;
        qos.Capabilities = RPC_C_QOS_CAPABILITIES_DEFAULT;
        qos.IdentityTracking = RPC_C_QOS_IDENTITY_STATIC;
        qos.ImpersonationType = RPC_C_IMP_LEVEL_IDENTIFY;
        status = RpcBindingSetAuthInfoExW(binding, NULL, RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
            RPC_C_AUTHN_WINNT, NULL, RPC_C_AUTHZ_NONE, &qos);
        printf("client auth_setup=%ld authn=WINNT level=PKT_PRIVACY imp=IDENTIFY\n", status);
    }
    if (!status)
    {
        value = invoke(binding, FALSE);
        printf("client call_exception=%#lx call_result=%ld expected=42\n", rpc_exception, value);
    }
    free_status = RpcBindingFree(&binding);
    printf("client binding_free=%ld\n", free_status);
}

int main(void)
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    static const struct { const char *label; const WCHAR *sddl; } cases[] =
    {
        {"default_NULL", NULL},
        {"everyone", L"D:(A;;GA;;;WD)"},
        {"empty_DACL", L"D:"},
        {"SYSTEM_only", L"D:(A;;GA;;;SY)"},
        {"everyone_bit_1", L"D:(A;;0x00000001;;;WD)"},
        {"everyone_bit_2", L"D:(A;;0x00000002;;;WD)"},
        {"everyone_GENERIC_EXECUTE", L"D:(A;;GX;;;WD)"},
        {"everyone_GENERIC_ALL", L"D:(A;;GA;;;WD)"},
    };
    const UINT interface_flags = RPC_IF_ALLOW_LOCAL_ONLY |
        RPC_IF_ALLOW_CALLBACKS_WITH_NO_AUTH | RPC_IF_SEC_NO_CACHE;
    WCHAR endpoint[100];
    DWORD random[4], size, error;
    PSECURITY_DESCRIPTOR sd;
    PACL dacl;
    BOOL present, defaulted;
    HMODULE module;
    register_if3_fn register_if3;
    RPC_STATUS status, auth_status, stop_status, wait_status, unregister_status;
    unsigned int i;
    int result = 0;

    if (!probe_start()) return 1;
    /* 不先证明异常捕获器，就不能把 RPC 拒绝当作安全的测量。 */
    invoke(NULL, TRUE);
    printf("SEH_selftest exception=%#lx expected=%#lx\n", rpc_exception, (ULONG)RPC_S_ACCESS_DENIED);
    if (rpc_exception != RPC_S_ACCESS_DENIED) return probe_done(1);
    module = GetModuleHandleW(L"rpcrt4.dll");
    register_if3 = module ? (register_if3_fn)(void *)GetProcAddress(module, "RpcServerRegisterIf3") : NULL;
    printf("RpcServerRegisterIf3 exported=%u\n", !!register_if3);
    if (!register_if3) return probe_done(0);
    if (!probe_random(random, sizeof(random))) return probe_done(1);
    swprintf(endpoint, ARRAYSIZE(endpoint), L"prefixupdate_rpcsd_%08lx%08lx%08lx%08lx",
             random[0], random[1], random[2], random[3]);
    status = RpcServerUseProtseqEpW((RPC_WSTR)L"ncalrpc", 2, (RPC_WSTR)endpoint, NULL);
    printf("protocol=ncalrpc endpoint=<random> use_protseq=%ld\n", status);
    if (status) return probe_done(0);
    auth_status = RpcServerRegisterAuthInfoW(NULL, RPC_C_AUTHN_WINNT, NULL, NULL);
    printf("register_auth_WINNT=%ld\n", auth_status);
    for (i = 0; i < ARRAYSIZE(cases); ++i)
    {
        sd = NULL;
        size = 0;
        printf("case=%s interface_flags=%#x\n", cases[i].label, interface_flags);
        if (cases[i].sddl)
        {
            SetLastError(PROBE_SENTINEL);
            present = ConvertStringSecurityDescriptorToSecurityDescriptorW(cases[i].sddl,
                            SDDL_REVISION_1, &sd, &size);
            error = GetLastError();
            printf("SD convert=%u gle=%lu size=%lu\n", present, error, size);
            if (!present) continue;
            if (GetSecurityDescriptorDacl(sd, &present, &dacl, &defaulted))
                printf("SD valid=%u DACL_present=%u DACL_null=%u DACL_defaulted=%u ACE_count=%u\n",
                       IsValidSecurityDescriptor(sd), present, !dacl, defaulted, dacl ? dacl->AceCount : 0);
        }
        SetLastError(PROBE_SENTINEL);
        status = register_if3(s_prefixupdate_rpcsd_v1_0_s_ifspec, NULL, NULL,
                             interface_flags, 2, 4096, NULL, sd);
        error = GetLastError();
        printf("register_if3=%ld gle=%lu\n", status, error);
        if (!status)
        {
            status = RpcServerListen(1, 2, TRUE);
            printf("listen_dont_wait=%ld\n", status);
            if (!status)
            {
                client_call(endpoint, FALSE);
                if (!auth_status) client_call(endpoint, TRUE);
                else puts("WINNT_client skipped: server auth registration failed");
                stop_status = RpcMgmtStopServerListening(NULL);
                wait_status = RpcMgmtWaitServerListen();
                printf("stop_listening=%ld wait_listen=%ld\n", stop_status, wait_status);
            }
            unregister_status = RpcServerUnregisterIf(s_prefixupdate_rpcsd_v1_0_s_ifspec, NULL, TRUE);
            printf("unregister_interface=%ld\n", unregister_status);
            if (unregister_status)
            {
                result = 1;
                if (sd) LocalFree(sd);
                break;
            }
        }
        if (sd) LocalFree(sd);
    }
    return probe_done(result);
}
