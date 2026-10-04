#define _WIN32_WINNT 0x0601
#ifndef UNICODE
#define UNICODE
#endif
#include "probe.h"
#include <rpc.h>
#include <sddl.h>
#include "rpcsd.h"

#if !defined(__x86_64__)
#error This probe requires x64 SEH.
#endif

typedef RPC_STATUS (RPC_ENTRY *register_if3_fn)(RPC_IF_HANDLE, UUID *, RPC_MGR_EPV *,
    unsigned int, unsigned int, unsigned int, RPC_IF_CALLBACK_FN *, void *);

static volatile DWORD rpc_exception;
static LONG server_calls, callback_calls;
static BOOL restrict_clients = TRUE;

void *__RPC_USER MIDL_user_allocate(SIZE_T size) { return malloc(size); }
void __RPC_USER MIDL_user_free(void *pointer) { free(pointer); }

long __cdecl s_ping(handle_t binding, long value)
{
    ULONG level = PROBE_SENTINEL, service = PROBE_SENTINEL, authorization = PROBE_SENTINEL;
    RPC_STATUS status;
    LONG calls = InterlockedIncrement(&server_calls);
    (void)binding;
    status = RpcBindingInqAuthClientW(NULL, NULL, NULL, &level, &service, &authorization);
    printf("phase=server executed=1 call_count=%ld auth_query_status=%ld"
           " authn_level=%lu authn_service=%lu authz_service=%lu\n",
           calls, status, level, service, authorization);
    return value + 1;
}

static RPC_STATUS RPC_ENTRY allow_call(RPC_IF_HANDLE if_handle, void *context)
{
    (void)if_handle;
    (void)context;
    InterlockedIncrement(&callback_calls);
    return RPC_S_OK;
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

/* GCC 不识别内联汇编的 SEH 边界，将必经调用放入非内联函数。 */
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

static BOOL client_call(const WCHAR *endpoint, BOOL authenticated)
{
    RPC_WSTR text = NULL;
    RPC_BINDING_HANDLE binding = NULL;
    RPC_SECURITY_QOS qos;
    RPC_STATUS status, free_status;
    LONG before = InterlockedCompareExchange(&server_calls, 0, 0);
    LONG callbacks_before = InterlockedCompareExchange(&callback_calls, 0, 0);
    long value;
    BOOL attempted = FALSE;

    status = RpcStringBindingComposeW(NULL, (RPC_WSTR)L"ncalrpc", NULL,
                                     (RPC_WSTR)endpoint, NULL, &text);
    printf("phase=client auth=%s compose=%ld\n", authenticated ? "WINNT" : "none", status);
    if (status) return FALSE;
    status = RpcBindingFromStringBindingW(text, &binding);
    RpcStringFreeW(&text);
    /* 这里只构造句柄；真正的线上绑定仍可能在第一次调用时失败。 */
    printf("phase=client binding_construct=%ld\n", status);
    if (status) return FALSE;
    status = RpcBindingSetOption(binding, RPC_C_OPT_CALL_TIMEOUT, 600);
    printf("phase=client call_timeout_ms=600 status=%ld\n", status);
    if (!status && authenticated)
    {
        memset(&qos, 0, sizeof(qos));
        qos.Version = RPC_C_SECURITY_QOS_VERSION;
        qos.Capabilities = RPC_C_QOS_CAPABILITIES_DEFAULT;
        qos.IdentityTracking = RPC_C_QOS_IDENTITY_STATIC;
        qos.ImpersonationType = RPC_C_IMP_LEVEL_IDENTIFY;
        status = RpcBindingSetAuthInfoExW(binding, NULL, RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
            RPC_C_AUTHN_WINNT, NULL, RPC_C_AUTHZ_NONE, &qos);
        printf("phase=client auth_setup=%ld authn=WINNT level=PKT_PRIVACY imp=IDENTIFY\n", status);
    }
    if (!status)
    {
        attempted = TRUE;
        value = invoke(binding, FALSE);
        printf("phase=client call_exception=%#lx call_result=%ld expected=42"
               " server_executed_delta=%ld callback_delta=%ld\n", rpc_exception, value,
               InterlockedCompareExchange(&server_calls, 0, 0) - before,
               InterlockedCompareExchange(&callback_calls, 0, 0) - callbacks_before);
    }
    else
        printf("phase=client skipped_before_call=1 earlier_status=%ld\n", status);
    free_status = RpcBindingFree(&binding);
    printf("phase=client binding_free=%ld call_attempted=%u\n", free_status, attempted);
    return attempted && !free_status;
}

static BOOL client_restricted(const WCHAR *endpoint, BOOL authenticated)
{
    BYTE admin_sid[SECURITY_MAX_SID_SIZE];
    DWORD sid_size = sizeof(admin_sid);
    SID_AND_ATTRIBUTES disabled;
    HANDLE primary = NULL, restricted = NULL, impersonation = NULL;
    BOOL admin_before = FALSE, admin_after = FALSE, result = FALSE;

    if (!CreateWellKnownSid(WinBuiltinAdministratorsSid, NULL, admin_sid, &sid_size)) return FALSE;
    CheckTokenMembership(NULL, admin_sid, &admin_before);
    disabled.Sid = admin_sid;
    disabled.Attributes = 0;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &primary) ||
        !CreateRestrictedToken(primary, 0, 1, &disabled, 0, NULL, 0, NULL, &restricted) ||
        !DuplicateToken(restricted, SecurityImpersonation, &impersonation) ||
        !SetThreadToken(NULL, impersonation))
    {
        printf("phase=client restrict_failed=%lu\n", GetLastError());
        goto done;
    }
    CheckTokenMembership(NULL, admin_sid, &admin_after);
    printf("phase=client admin_before=%u admin_after=%u\n", admin_before, admin_after);
    result = client_call(endpoint, authenticated);
    if (!RevertToSelf()) result = FALSE;

done:
    if (impersonation) CloseHandle(impersonation);
    if (restricted) CloseHandle(restricted);
    if (primary) CloseHandle(primary);
    return result;
}

static BOOL client_process(const WCHAR *endpoint, BOOL authenticated)
{
    WCHAR arguments[160];
    LONG before = InterlockedCompareExchange(&server_calls, 0, 0);
    DWORD result;

    swprintf(arguments, ARRAYSIZE(arguments), L"--client-%ls%ls %ls", restrict_clients ? L"" : L"normal-",
             authenticated ? L"winnt" : L"none", endpoint);
    result = probe_child(arguments, 2500);
    printf("phase=parent client_exit=%lu server_executed_delta=%ld\n", result,
           InterlockedCompareExchange(&server_calls, 0, 0) - before);
    return !result;
}

int wmain(int argc, WCHAR **argv)
{
    static const UINT flags[] =
    {
        0, RPC_IF_ALLOW_LOCAL_ONLY,
        RPC_IF_ALLOW_LOCAL_ONLY | RPC_IF_ALLOW_CALLBACKS_WITH_NO_AUTH,
        RPC_IF_ALLOW_LOCAL_ONLY | RPC_IF_ALLOW_CALLBACKS_WITH_NO_AUTH | RPC_IF_SEC_NO_CACHE,
    };
    /* 优先合法的本地、允许未显式认证回调、无缓存组合；只按本机注册结果选择。 */
    static const unsigned int choices[][2] = {{2,0}, {3,1}, {2,1}, {1,0}, {0,0}, {1,1}, {0,1}, {3,0}};
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
        {"deny_execute_then_allow_all", L"D:(D;;0x00000001;;;WD)(A;;GA;;;WD)"},
        {"deny_generic_execute_then_allow_all", L"D:(D;;GX;;;WD)(A;;GA;;;WD)"},
        {"deny_execute_then_allow_execute", L"D:(D;;0x00000001;;;WD)(A;;0x00000001;;;WD)"},
        {"deny_all_then_allow_all", L"D:(D;;GA;;;WD)(A;;GA;;;WD)"},
        {"read_control_only", L"D:(A;;RC;;;WD)"},
        {"write_dac_only", L"D:(A;;WD;;;WD)"},
        {"synchronize_only", L"D:(A;;0x00100000;;;WD)"},
        {"bit_4_only", L"D:(A;;0x00000004;;;WD)"},
        {"user_owned_empty_DACL", L"D:"},
        {"system_owned_empty_DACL", L"O:SYG:SYD:"},
    };
    BOOL legal[ARRAYSIZE(flags)][2] = {{FALSE}}, listening = FALSE, registered = FALSE;
    UINT interface_flags = 0;
    RPC_IF_CALLBACK_FN *callback = NULL;
    WCHAR endpoint[100];
    DWORD random[4], size, error;
    PSECURITY_DESCRIPTOR sd = NULL;
    PACL dacl;
    BOOL present, defaulted, chosen = FALSE;
    HMODULE module;
    register_if3_fn register_if3;
    RPC_STATUS status, auth_status, stop_status, wait_status, unregister_status;
    unsigned int i, cb, first_case = 0, last_case = ARRAYSIZE(cases);
    BOOL isolated_case = FALSE;
    int result = 0;

    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    if (!probe_start()) return 1;
    probe_scene_begin(4000);
    invoke(NULL, TRUE);
    printf("SEH_selftest exception=%#lx expected=%#lx\n", rpc_exception, (ULONG)RPC_S_ACCESS_DENIED);
    if (rpc_exception != RPC_S_ACCESS_DENIED) return probe_done(1);
    if (argc == 3 && (!wcscmp(argv[1], L"--client-none") || !wcscmp(argv[1], L"--client-winnt") ||
        !wcscmp(argv[1], L"--client-normal-none") || !wcscmp(argv[1], L"--client-normal-winnt")))
    {
        BOOL authenticated = wcsstr(argv[1], L"winnt") != NULL;
        BOOL normal = wcsstr(argv[1], L"normal") != NULL;
        result = (normal ? client_call(argv[2], authenticated) : client_restricted(argv[2], authenticated)) ? 0 : 1;
        probe_scene_end();
        return probe_done(result);
    }
    module = GetModuleHandleW(L"rpcrt4.dll");
    register_if3 = module ? (register_if3_fn)(void *)GetProcAddress(module, "RpcServerRegisterIf3") : NULL;
    printf("phase=setup RpcServerRegisterIf3_exported=%u\n", !!register_if3);
    if (!register_if3) return probe_done(1);
    probe_scene_end();

    if (argc == 3 && (!wcscmp(argv[1], L"--case") || !wcscmp(argv[1], L"--case-callback") ||
        !wcscmp(argv[1], L"--case-normal")))
    {
        first_case = wcstoul(argv[2], NULL, 10);
        if (first_case >= ARRAYSIZE(cases)) return probe_done(1);
        last_case = first_case + 1;
        isolated_case = TRUE;
        restrict_clients = wcscmp(argv[1], L"--case-normal") != 0;
        chosen = TRUE;
        callback = !wcscmp(argv[1], L"--case-callback") ? allow_call : NULL;
        interface_flags = callback ? 0x70 : 0;
    }

    for (i = 0; !isolated_case && i < ARRAYSIZE(flags); ++i)
    {
        for (cb = 0; cb < 2; ++cb)
        {
            probe_scene_begin(4000);
            SetLastError(PROBE_SENTINEL);
            status = register_if3(s_prefixupdate_rpcsd_v1_0_s_ifspec, NULL, NULL,
                                 flags[i], 2, 4096, cb ? allow_call : NULL, NULL);
            error = GetLastError();
            printf("phase=registration sd=default_NULL flags=%#x callback=%s register_if3=%ld gle=%lu\n",
                   flags[i], cb ? "allow" : "NULL", status, error);
            legal[i][cb] = !status;
            if (!status)
            {
                unregister_status = RpcServerUnregisterIf(s_prefixupdate_rpcsd_v1_0_s_ifspec, NULL, TRUE);
                printf("phase=registration unregister_interface=%ld\n", unregister_status);
                if (unregister_status) return probe_done(1);
            }
            probe_scene_end();
        }
    }
    for (i = 0; !isolated_case && i < ARRAYSIZE(choices); ++i)
    {
        if (legal[choices[i][0]][choices[i][1]])
        {
            interface_flags = flags[choices[i][0]];
            callback = choices[i][1] ? allow_call : NULL;
            chosen = TRUE;
            break;
        }
    }
    printf("phase=select legal_observed=%u flags=%#x callback=%s\n", chosen, interface_flags,
           callback ? "allow" : "NULL");
    if (!chosen) return probe_done(1);
    if (!isolated_case)
    {
        for (i = 0; i < ARRAYSIZE(cases); ++i)
        {
            WCHAR arguments[40];
            DWORD child_status;
            swprintf(arguments, ARRAYSIZE(arguments), L"--case %u", i);
            child_status = probe_child(arguments, 6000);
            printf("phase=isolated case=%s child_exit=%lu\n", cases[i].label, child_status);
            if (child_status) result = 1;
        }
        printf("phase=summary isolated_cases=%u exit=%d\n", (unsigned int)ARRAYSIZE(cases), result);
        return probe_done(result);
    }

    probe_scene_begin(4000);
    if (!probe_random(random, sizeof(random))) return probe_done(1);
    swprintf(endpoint, ARRAYSIZE(endpoint), L"prefixupdate_rpcsd_%08lx%08lx%08lx%08lx",
             random[0], random[1], random[2], random[3]);
    status = RpcServerUseProtseqEpW((RPC_WSTR)L"ncalrpc", 2, (RPC_WSTR)endpoint, NULL);
    printf("phase=setup protocol=ncalrpc use_protseq=%ld\n", status);
    if (status) return probe_done(1);
    auth_status = RpcServerRegisterAuthInfoW(NULL, RPC_C_AUTHN_WINNT, NULL, NULL);
    printf("phase=setup register_auth_WINNT=%ld\n", auth_status);
    probe_scene_end();

    for (i = first_case; i < last_case; ++i)
    {
        probe_scene_begin(4000);
        sd = NULL;
        size = 0;
        printf("phase=sd case=%s interface_flags=%#x callback=%s\n", cases[i].label,
               interface_flags, callback ? "allow" : "NULL");
        if (cases[i].sddl)
        {
            WCHAR owned_sddl[256], *sid_text = NULL;
            const WCHAR *sddl = cases[i].sddl;
            if (!strcmp(cases[i].label, "user_owned_empty_DACL"))
            {
                union { TOKEN_USER user; BYTE bytes[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE]; } token_user;
                HANDLE token;
                DWORD required;
                if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return probe_done(1);
                present = GetTokenInformation(token, TokenUser, &token_user, sizeof(token_user), &required);
                CloseHandle(token);
                if (!present || !ConvertSidToStringSidW(token_user.user.User.Sid, &sid_text)) return probe_done(1);
                swprintf(owned_sddl, ARRAYSIZE(owned_sddl), L"O:%lsD:", sid_text);
                LocalFree(sid_text);
                sddl = owned_sddl;
            }
            SetLastError(PROBE_SENTINEL);
            present = ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl,
                            SDDL_REVISION_1, &sd, &size);
            error = GetLastError();
            printf("phase=sd convert=%u gle=%lu size=%lu\n", present, error, size);
            if (!present)
            {
                result = 1;
                probe_scene_end();
                continue;
            }
            if (GetSecurityDescriptorDacl(sd, &present, &dacl, &defaulted))
                printf("phase=sd valid=%u DACL_present=%u DACL_null=%u DACL_defaulted=%u ACE_count=%u\n",
                       IsValidSecurityDescriptor(sd), present, !dacl, defaulted, dacl ? dacl->AceCount : 0);
        }
        SetLastError(PROBE_SENTINEL);
        status = register_if3(s_prefixupdate_rpcsd_v1_0_s_ifspec, NULL, NULL,
                             interface_flags, 2, 4096, callback, sd);
        error = GetLastError();
        printf("phase=sd register_if3=%ld gle=%lu\n", status, error);
        if (status)
        {
            printf("phase=sd skipped_before_call=1 earlier_status=%ld\n", status);
            result = 1;
        }
        else
        {
            registered = TRUE;
            if (!listening)
            {
                status = RpcServerListen(1, 2, TRUE);
                printf("phase=setup listen_dont_wait=%ld\n", status);
                listening = !status;
                if (status)
                {
                    result = 1;
                    break;
                }
            }
            if (!client_process(endpoint, FALSE)) result = 1;
            if (!auth_status)
            {
                if (!client_process(endpoint, TRUE)) result = 1;
            }
            else
            {
                printf("phase=client auth=WINNT skipped_before_call=1 earlier_status=%ld\n", auth_status);
                result = 1;
            }
            unregister_status = RpcServerUnregisterIf(s_prefixupdate_rpcsd_v1_0_s_ifspec, NULL, TRUE);
            printf("phase=sd unregister_interface=%ld\n", unregister_status);
            if (unregister_status)
            {
                result = 1;
                break;
            }
            registered = FALSE;
        }
        if (sd) LocalFree(sd);
        sd = NULL;
        probe_scene_end();
    }

    probe_scene_begin(4000);
    if (registered)
    {
        unregister_status = RpcServerUnregisterIf(s_prefixupdate_rpcsd_v1_0_s_ifspec, NULL, TRUE);
        printf("phase=cleanup unregister_interface=%ld\n", unregister_status);
        if (unregister_status) result = 1;
    }
    if (sd) LocalFree(sd);
    if (listening)
    {
        stop_status = RpcMgmtStopServerListening(NULL);
        printf("phase=cleanup stop_listening=%ld\n", stop_status);
        if (stop_status) result = 1;
        else
        {
            wait_status = RpcMgmtWaitServerListen();
            printf("phase=cleanup wait_listen=%ld\n", wait_status);
            if (wait_status) result = 1;
        }
    }
    printf("phase=summary server_calls=%ld callback_calls=%ld exit=%d\n",
           InterlockedCompareExchange(&server_calls, 0, 0),
           InterlockedCompareExchange(&callback_calls, 0, 0), result);
    probe_scene_end();
    return probe_done(result);
}
