#define UNICODE
#include "probe.h"
#include <userenv.h>
#include <lmcons.h>
#include <sddl.h>
#include <stddef.h>

typedef LONG (WINAPI *query_key_fn)(HANDLE, ULONG, void *, ULONG, ULONG *);
typedef struct { ULONG length; WCHAR name[1024]; } key_name_information;

typedef struct
{
    TOKEN_PRIVILEGES previous;
    BOOL restore;
} privilege_state;

static void enable_privilege(HANDLE token, const WCHAR *name, const char *label,
                             privilege_state *state)
{
    TOKEN_PRIVILEGES requested;
    DWORD returned = PROBE_SENTINEL, error;
    BOOL ok;

    memset(state, 0, sizeof(*state));
    memset(&requested, 0, sizeof(requested));
    SetLastError(PROBE_SENTINEL);
    ok = LookupPrivilegeValueW(NULL, name, &requested.Privileges[0].Luid);
    error = GetLastError();
    printf("privilege=%s lookup=%u gle=%lu\n", label, ok, error);
    if (!ok) return;
    requested.PrivilegeCount = 1;
    requested.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    SetLastError(ERROR_SUCCESS);
    ok = AdjustTokenPrivileges(token, FALSE, &requested, sizeof(state->previous),
                               &state->previous, &returned);
    error = GetLastError();
    state->restore = ok && state->previous.PrivilegeCount != 0;
    printf("privilege=%s adjust=%u gle=%lu returned=%lu changed_count=%lu\n",
           label, ok, error, returned, ok ? state->previous.PrivilegeCount : 0);
}

static void restore_privilege(HANDLE token, const char *label, privilege_state *state)
{
    BOOL ok;
    DWORD error;
    if (!state->restore) return;
    SetLastError(ERROR_SUCCESS);
    ok = AdjustTokenPrivileges(token, FALSE, &state->previous, 0, NULL, NULL);
    error = GetLastError();
    printf("privilege=%s restore=%u gle=%lu\n", label, ok, error);
}

static void profile_key(HANDLE profile, const WCHAR *sid)
{
    key_name_information info;
    ULONG returned = PROBE_SENTINEL;
    query_key_fn query;
    HMODULE module = GetModuleHandleW(L"ntdll.dll");
    LONG status;
    LSTATUS reg_status;
    DWORD subkeys = PROBE_SENTINEL, values = PROBE_SENTINEL;
    size_t characters, prefix_len, sid_len;
    const WCHAR prefix[] = L"\\REGISTRY\\USER\\";

    query = module ? (query_key_fn)(void *)GetProcAddress(module, "NtQueryKey") : NULL;
    printf("hProfile nonnull=%u predefined_HKCU=%u\n", !!profile, profile == HKEY_CURRENT_USER);
    if (query)
    {
        memset(&info, 0, sizeof(info));
        status = query(profile, 3, &info, sizeof(info) - sizeof(WCHAR), &returned);
        printf("hProfile NtQueryKey ntstatus=%#lx returned=%lu", (ULONG)status, returned);
        if (status >= 0 && info.length <= sizeof(info.name) - sizeof(WCHAR))
        {
            characters = info.length / sizeof(WCHAR);
            info.name[characters] = 0;
            prefix_len = wcslen(prefix);
            sid_len = wcslen(sid);
            if (characters >= prefix_len + sid_len &&
                !_wcsnicmp(info.name, prefix, prefix_len) &&
                !_wcsnicmp(info.name + prefix_len, sid, sid_len) &&
                (info.name[prefix_len + sid_len] == 0 ||
                 !wcscmp(info.name + prefix_len + sid_len, L"_Classes")))
                printf(" shape=\\REGISTRY\\USER\\<sid>%s",
                       info.name[prefix_len + sid_len] ? "_Classes" : "");
            else printf(" shape=<key> characters=%llu", (unsigned long long)characters);
        }
        puts("");
    }
    reg_status = RegQueryInfoKeyW((HKEY)profile, NULL, NULL, NULL, &subkeys, NULL,
                                 NULL, &values, NULL, NULL, NULL, NULL);
    printf("hProfile RegQueryInfoKey status=%ld subkeys=%lu values=%lu\n",
           reg_status, subkeys, values);
}

static BOOL load_case(HANDLE token, const WCHAR *sid, WCHAR *username,
                      const char *label, DWORD size, DWORD flags)
{
    PROFILEINFOW info;
    BOOL ok, unloaded;
    DWORD error, unload_error;

    memset(&info, 0, sizeof(info));
    info.dwSize = size;
    info.dwFlags = flags;
    info.lpUserName = username;
    SetLastError(PROBE_SENTINEL);
    ok = LoadUserProfileW(token, &info);
    error = GetLastError();
    printf("load case=%s dwSize=%lu flags=%#lx username_length=%llu ok=%u gle=%lu hProfile_nonnull=%u\n",
           label, size, flags, (unsigned long long)wcslen(username), ok, error, !!info.hProfile);
    if (ok && info.hProfile)
    {
        profile_key(info.hProfile, sid);
        SetLastError(PROBE_SENTINEL);
        unloaded = UnloadUserProfile(token, info.hProfile);
        unload_error = GetLastError();
        printf("unload case=%s ok=%u gle=%lu\n", label, unloaded, unload_error);
        return unloaded;
    }
    if (ok) puts("unload skipped: success returned no profile handle");
    return !ok;
}

int main(void)
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    union { ULONGLONG align; BYTE bytes[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE]; } user;
    WCHAR username[UNLEN + 1], empty[] = L"";
    DWORD name_length = ARRAYSIZE(username), returned = PROBE_SENTINEL;
    DWORD flags = PROBE_SENTINEL, error;
    HANDLE token = NULL;
    HKEY mounted;
    WCHAR *sid = NULL;
    privilege_state backup, restore;
    BOOL ok;
    LSTATUS reg_status;

    if (!probe_start()) return 1;
    SetLastError(PROBE_SENTINEL);
    ok = GetProfileType(&flags);
    error = GetLastError();
    printf("GetProfileType ok=%u gle=%lu flags=%#lx\n", ok, error, flags);
    SetLastError(PROBE_SENTINEL);
    ok = OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE |
                          TOKEN_IMPERSONATE | TOKEN_ADJUST_PRIVILEGES, &token);
    error = GetLastError();
    printf("OpenProcessToken ok=%u gle=%lu\n", ok, error);
    if (!ok) return probe_done(0);
    SetLastError(PROBE_SENTINEL);
    ok = GetTokenInformation(token, TokenUser, user.bytes, sizeof(user.bytes), &returned);
    error = GetLastError();
    printf("TokenUser ok=%u gle=%lu returned=%lu\n", ok, error, returned);
    if (!ok || !ConvertSidToStringSidW(((TOKEN_USER *)user.bytes)->User.Sid, &sid))
    {
        printf("current_sid_unavailable gle=%lu\n", GetLastError());
        CloseHandle(token);
        return probe_done(0);
    }
    SetLastError(PROBE_SENTINEL);
    ok = GetUserNameW(username, &name_length);
    error = GetLastError();
    printf("GetUserName ok=%u gle=%lu buffer_characters=%lu (name withheld)\n", ok, error, name_length);
    reg_status = RegOpenKeyExW(HKEY_USERS, sid, 0, KEY_READ, &mounted);
    printf("current_profile_already_mounted=%u registry_status=%ld\n", reg_status == ERROR_SUCCESS, reg_status);
    if (reg_status == ERROR_SUCCESS) RegCloseKey(mounted);
    if (!ok || reg_status != ERROR_SUCCESS)
    {
        puts("LoadUserProfile skipped: only an already mounted current-user profile is permitted");
        LocalFree(sid);
        CloseHandle(token);
        return probe_done(0);
    }
    enable_privilege(token, SE_BACKUP_NAME, "SeBackupPrivilege", &backup);
    enable_privilege(token, SE_RESTORE_NAME, "SeRestorePrivilege", &restore);
    /* 意外成功也立即卸载；卸载失败时不再增加配置文件引用。 */
    if (load_case(token, sid, username, "size_zero", 0, PI_NOUI) &&
        load_case(token, sid, username, "size_minus_one", sizeof(PROFILEINFOW) - 1, PI_NOUI) &&
        load_case(token, sid, username, "size_plus_one", sizeof(PROFILEINFOW) + 1, PI_NOUI) &&
        load_case(token, sid, empty, "empty_username", sizeof(PROFILEINFOW), PI_NOUI) &&
        load_case(token, sid, username, "unknown_flag", sizeof(PROFILEINFOW), PI_NOUI | 0x80000000U))
        load_case(token, sid, username, "current_user", sizeof(PROFILEINFOW), PI_NOUI);
    else puts("remaining_loads_skipped: an earlier successful load could not be unloaded");
    restore_privilege(token, "SeRestorePrivilege", &restore);
    restore_privilege(token, "SeBackupPrivilege", &backup);
    LocalFree(sid);
    CloseHandle(token);
    return probe_done(0);
}
