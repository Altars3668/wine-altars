/* misc3: what misc left open -- how SetPrivateObjectSecurity(Ex) changes a private object's descriptor (explicit and
 * inherited ACEs, protection, owner and group, SACL and label), ConvertToAutoInheritPrivateObjectSecurity, a creator
 * descriptor with no token, the SystemPerformanceInformation sizes between the old and the new structure, the
 * mitigation options misc did not try and the child process policy (and whether a restricted child can start a
 * process), and what XmlWriter's CompactEmptyElement and conformance level do to the output.  Leaves out the options
 * that would keep the child from starting (no win32k calls, strict CFG, CET strictness).  Prints results only; a
 * user's SIDs come out as <machine>-RID and logon SIDs as <logon>.
 *
 *     misc3.exe                         everything
 *     misc3.exe section <name>          one section
 *     misc3.exe child <event> <create>  (the child of the process tests) */
#define COBJMACROS
#include <windows.h>
#include <winternl.h>
#include <sddl.h>
#include <initguid.h>
#include <xmllite.h>
#include <shlwapi.h>
#include <stdio.h>
#include <wchar.h>

static void *proc(const WCHAR *dll, const char *name)
{
    HMODULE module = LoadLibraryW(dll);
    return module ? (void *)GetProcAddress(module, name) : NULL;
}

/* SIDs in a string with the machine's part and logon ids taken out */
static void scrub_sids(WCHAR *s)
{
    WCHAR *p, *q, *end;
    unsigned int dashes;

    while ((p = wcsstr(s, L"S-1-5-21-")))
    {
        for (q = p + 9, dashes = 0, end = q; *end && (iswdigit(*end) || *end == '-'); end++)
            if (*end == '-') dashes++;
        if (dashes >= 3)
        {
            WCHAR *rid = end;
            while (rid > q && rid[-1] != '-') rid--;
            memmove(p + 9, rid, (wcslen(rid) + 1) * sizeof(WCHAR));
            memcpy(p, L"<machine>", 9 * sizeof(WCHAR));
            memmove(p + 9 + 1, p + 9, (wcslen(p + 9) + 1) * sizeof(WCHAR));
            p[9] = '-';
        }
        else break;
    }
    while ((p = wcsstr(s, L"S-1-5-5-")))
    {
        for (end = p + 8; *end && (iswdigit(*end) || *end == '-'); end++);
        memcpy(p, L"<logon>", 7 * sizeof(WCHAR));
        memmove(p + 7, end, (wcslen(end) + 1) * sizeof(WCHAR));
    }
}

static void print_sd(PSECURITY_DESCRIPTOR sd)
{
    WCHAR *sddl = NULL, copy[2048];
    SECURITY_DESCRIPTOR_CONTROL control;
    DWORD revision;

    if (!ConvertSecurityDescriptorToStringSecurityDescriptorW(sd, SDDL_REVISION_1,
            OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION |
            SACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION, &sddl, NULL))
    {
        printf(" (no SDDL, error %lu)", GetLastError());
        return;
    }
    wcsncpy(copy, sddl, ARRAYSIZE(copy) - 1);
    copy[ARRAYSIZE(copy) - 1] = 0;
    scrub_sids(copy);
    GetSecurityDescriptorControl(sd, &control, &revision);
    printf(" control %#x length %lu [%ls]", control, GetSecurityDescriptorLength(sd), copy);
    LocalFree(sddl);
}

static BOOL set_privilege(const WCHAR *name, BOOL enable)
{
    TOKEN_PRIVILEGES privs = { 1 };
    HANDLE token;
    BOOL ret;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES, &token)) return FALSE;
    LookupPrivilegeValueW(NULL, name, &privs.Privileges[0].Luid);
    privs.Privileges[0].Attributes = enable ? SE_PRIVILEGE_ENABLED : 0;
    ret = AdjustTokenPrivileges(token, FALSE, &privs, sizeof(privs), NULL, NULL) && GetLastError() == ERROR_SUCCESS;
    CloseHandle(token);
    return ret;
}

static GENERIC_MAPPING mapping = { FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS };

/* a file's private descriptor that inherited from its parent */
static PSECURITY_DESCRIPTOR new_object(HANDLE token)
{
    PSECURITY_DESCRIPTOR parent, sd = NULL;

    ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:SYD:(A;OICI;GA;;;WD)", SDDL_REVISION_1, &parent, NULL);
    CreatePrivateObjectSecurityEx(parent, NULL, &sd, NULL, FALSE, SEF_DACL_AUTO_INHERIT, token, &mapping);
    LocalFree(parent);
    return sd;
}

static void try_set(const char *what, SECURITY_INFORMATION info, const WCHAR *modification, ULONG flags, BOOL ex,
                    BOOL no_token)
{
    PSECURITY_DESCRIPTOR sd, before, mod = NULL;
    HANDLE token;
    BOOL ret;

    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token);
    before = sd = new_object(token);
    if (modification && !ConvertStringSecurityDescriptorToSecurityDescriptorW(modification, SDDL_REVISION_1, &mod, NULL))
    {
        printf("%s: the modification does not convert, error %lu\n", what, GetLastError());
        return;
    }
    SetLastError(0xdeadbeef);
    if (ex) ret = SetPrivateObjectSecurityEx(info, mod, &sd, flags, &mapping, no_token ? NULL : token);
    else ret = SetPrivateObjectSecurity(info, mod, &sd, &mapping, no_token ? NULL : token);
    printf("%s: %d error %lu, %s", what, ret, ret ? 0 : GetLastError(), sd == before ? "same pointer" : "new pointer");
    print_sd(sd);
    printf("\n");
    DestroyPrivateObjectSecurity(&sd);
    if (mod) LocalFree(mod);
    CloseHandle(token);
}

static void private_sets(void)
{
    PSECURITY_DESCRIPTOR sd, parent, current, result = NULL;
    HANDLE token;
    BOOL ret;

    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token);
    sd = new_object(token);
    printf("the object:");
    print_sd(sd);
    printf("\n");
    DestroyPrivateObjectSecurity(&sd);
    CloseHandle(token);

    try_set("SetPrivateObjectSecurity DACL", DACL_SECURITY_INFORMATION, L"D:(A;;GR;;;BU)", 0, FALSE, FALSE);
    try_set("Ex DACL", DACL_SECURITY_INFORMATION, L"D:(A;;GR;;;BU)", 0, TRUE, FALSE);
    try_set("Ex DACL, auto inherit", DACL_SECURITY_INFORMATION, L"D:(A;;GR;;;BU)", SEF_DACL_AUTO_INHERIT, TRUE, FALSE);
    try_set("Ex protected DACL, auto inherit", DACL_SECURITY_INFORMATION, L"D:P(A;;GR;;;BU)", SEF_DACL_AUTO_INHERIT,
            TRUE, FALSE);
    try_set("Ex DACL to pass on and for the creator owner, auto inherit", DACL_SECURITY_INFORMATION,
            L"D:(A;OICI;GR;;;BU)(A;;GA;;;CO)(A;OICIIO;GW;;;CO)", SEF_DACL_AUTO_INHERIT, TRUE, FALSE);
    try_set("Ex DACL with an inherited ACE, auto inherit", DACL_SECURITY_INFORMATION, L"D:(A;;GR;;;BU)(A;ID;GX;;;AU)",
            SEF_DACL_AUTO_INHERIT, TRUE, FALSE);
    try_set("Ex DACL with an inherited ACE", DACL_SECURITY_INFORMATION, L"D:(A;;GR;;;BU)(A;ID;GX;;;AU)", 0, TRUE,
            FALSE);
    try_set("Ex DACL, the descriptor has none", DACL_SECURITY_INFORMATION, L"O:BA", 0, TRUE, FALSE);
    try_set("Ex NULL DACL", DACL_SECURITY_INFORMATION, L"D:NO_ACCESS_CONTROL", 0, TRUE, FALSE);
    try_set("Ex empty DACL, auto inherit", DACL_SECURITY_INFORMATION, L"D:", SEF_DACL_AUTO_INHERIT, TRUE, FALSE);
    try_set("Ex owner SY", OWNER_SECURITY_INFORMATION, L"O:SY", 0, TRUE, FALSE);
    try_set("Ex owner BA", OWNER_SECURITY_INFORMATION, L"O:BA", 0, TRUE, FALSE);
    try_set("Ex owner BU", OWNER_SECURITY_INFORMATION, L"O:BU", 0, TRUE, FALSE);
    try_set("Ex owner SY, avoid owner check", OWNER_SECURITY_INFORMATION, L"O:SY", SEF_AVOID_OWNER_CHECK, TRUE, FALSE);
    try_set("Ex owner, the descriptor has none", OWNER_SECURITY_INFORMATION, L"D:(A;;GR;;;BU)", 0, TRUE, FALSE);
    try_set("Ex group SY", GROUP_SECURITY_INFORMATION, L"G:SY", 0, TRUE, FALSE);
    try_set("Ex group, the descriptor has none", GROUP_SECURITY_INFORMATION, L"D:(A;;GR;;;BU)", 0, TRUE, FALSE);
    try_set("Ex owner and DACL, no token", OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
            L"O:BAD:(A;;GR;;;BU)", 0, TRUE, TRUE);
    try_set("Ex DACL, no token", DACL_SECURITY_INFORMATION, L"D:(A;;GR;;;BU)", 0, TRUE, TRUE);
    try_set("Ex SACL", SACL_SECURITY_INFORMATION, L"S:(AU;SA;GA;;;WD)", 0, TRUE, FALSE);
    try_set("Ex label", LABEL_SECURITY_INFORMATION, L"S:(ML;;NW;;;LW)", 0, TRUE, FALSE);
    try_set("Ex nothing", 0, L"D:(A;;GR;;;BU)", 0, TRUE, FALSE);
    try_set("Ex flags 0x80000000", DACL_SECURITY_INFORMATION, L"D:(A;;GR;;;BU)", 0x80000000, TRUE, FALSE);
    printf("SeSecurityPrivilege enabled: %d\n", set_privilege(SE_SECURITY_NAME, TRUE));
    try_set("Ex SACL, privilege enabled", SACL_SECURITY_INFORMATION, L"S:(AU;SA;GA;;;WD)", 0, TRUE, FALSE);
    try_set("Ex SACL and label, privilege enabled", SACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION,
            L"S:(AU;SA;GA;;;WD)(ML;;NW;;;LW)", 0, TRUE, FALSE);
    set_privilege(SE_SECURITY_NAME, FALSE);

    ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:SYD:(A;OICI;GA;;;WD)", SDDL_REVISION_1, &parent, NULL);
    ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:SYD:(A;;FA;;;WD)(A;;FR;;;BU)", SDDL_REVISION_1,
                                                         &current, NULL);
    SetLastError(0xdeadbeef);
    ret = ConvertToAutoInheritPrivateObjectSecurity(parent, current, &result, NULL, FALSE, &mapping);
    printf("ConvertToAutoInheritPrivateObjectSecurity file: %d error %lu", ret, ret ? 0 : GetLastError());
    if (ret)
    {
        print_sd(result);
        DestroyPrivateObjectSecurity(&result);
    }
    printf("\n");
    LocalFree(current);
    ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAG:SYD:(A;;FA;;;WD)(A;OICIIO;GA;;;WD)(A;;FR;;;BU)",
                                                         SDDL_REVISION_1, &current, NULL);
    result = NULL;
    SetLastError(0xdeadbeef);
    ret = ConvertToAutoInheritPrivateObjectSecurity(parent, current, &result, NULL, TRUE, &mapping);
    printf("ConvertToAutoInheritPrivateObjectSecurity directory: %d error %lu", ret, ret ? 0 : GetLastError());
    if (ret)
    {
        print_sd(result);
        DestroyPrivateObjectSecurity(&result);
    }
    printf("\n");
    LocalFree(current);
    LocalFree(parent);
}

static void null_token(void)
{
    static const WCHAR *creators[] = { L"O:BAG:SYD:(A;;GA;;;WD)", L"O:BAD:(A;;GA;;;WD)", L"D:(A;;GA;;;WD)",
                                       L"O:BAG:SY" };
    PSECURITY_DESCRIPTOR creator, sd;
    unsigned int i;
    BOOL ret;

    for (i = 0; i < ARRAYSIZE(creators); i++)
    {
        ConvertStringSecurityDescriptorToSecurityDescriptorW(creators[i], SDDL_REVISION_1, &creator, NULL);
        sd = NULL;
        SetLastError(0xdeadbeef);
        ret = CreatePrivateObjectSecurityEx(NULL, creator, &sd, NULL, FALSE, 0, NULL, &mapping);
        printf("CreatePrivateObjectSecurityEx creator %ls, no token: %d error %lu", creators[i], ret,
               ret ? 0 : GetLastError());
        if (ret)
        {
            print_sd(sd);
            DestroyPrivateObjectSecurity(&sd);
        }
        printf("\n");
        LocalFree(creator);
    }
}

static void performance_sizes(void)
{
    static const ULONG sizes[] = { 311, 312, 313, 314, 316, 320, 368, 375, 376, 377, 384 };
    BYTE buffer[512];
    unsigned int i;

    for (i = 0; i < ARRAYSIZE(sizes); i++)
    {
        ULONG len = 0xdead;
        NTSTATUS status = NtQuerySystemInformation(SystemPerformanceInformation, buffer, sizes[i], &len);
        printf("SystemPerformanceInformation size %lu: %#lx len %lu\n", sizes[i], status, len);
    }
}

static void print_policies(const char *who, HANDLE process)
{
    BOOL (WINAPI *pGetProcessMitigationPolicy)(HANDLE, PROCESS_MITIGATION_POLICY, void *, SIZE_T) =
            proc(L"kernel32.dll", "GetProcessMitigationPolicy");
    unsigned int policy;

    printf("%s:", who);
    for (policy = 0; policy < 20; policy++)
    {
        DWORD64 flags[2] = { 0xdeadbeef, 0 };
        SIZE_T size = policy == 0 ? sizeof(PROCESS_MITIGATION_DEP_POLICY) : sizeof(DWORD);
        if (pGetProcessMitigationPolicy(process, policy, flags, size))
            printf(" %u=%llx", policy, flags[0] & (policy == 0 ? ~0ull : 0xffffffffull));
        else printf(" %u:err%lu", policy, GetLastError());
    }
    printf("\n");
}

/* start a child with an attribute; it prints its policies, and tries to start a process if asked to */
static void start_child(const char *what, DWORD_PTR attribute, const void *value, SIZE_T size, BOOL create)
{
    STARTUPINFOEXW si = { { sizeof(si) } };
    WCHAR exe[MAX_PATH], cmdline[MAX_PATH + 64];
    PROCESS_INFORMATION pi;
    SIZE_T list_size = 0;
    HANDLE event;
    BOOL ret;

    GetModuleFileNameW(NULL, exe, ARRAYSIZE(exe));
    printf("%s:", what);
    event = CreateEventW(NULL, TRUE, FALSE, L"misc3-child");
    InitializeProcThreadAttributeList(NULL, 1, 0, &list_size);
    si.lpAttributeList = malloc(list_size);
    InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &list_size);
    SetLastError(0xdeadbeef);
    ret = UpdateProcThreadAttribute(si.lpAttributeList, 0, attribute, (void *)value, size, NULL, NULL);
    printf(" update %d error %lu", ret, ret ? 0 : GetLastError());
    if (ret)
    {
        swprintf(cmdline, ARRAYSIZE(cmdline), L"\"%ls\" child misc3-child %d", exe, create);
        SetLastError(0xdeadbeef);
        ret = CreateProcessW(exe, cmdline, NULL, NULL, FALSE, EXTENDED_STARTUPINFO_PRESENT, NULL, NULL,
                             &si.StartupInfo, &pi);
        printf(", create %d error %lu\n", ret, ret ? 0 : GetLastError());
        if (ret)
        {
            print_policies("  the child's, asked from outside", pi.hProcess);
            SetEvent(event);
            WaitForSingleObject(pi.hProcess, 20000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }
    else printf("\n");
    DeleteProcThreadAttributeList(si.lpAttributeList);
    free(si.lpAttributeList);
    CloseHandle(event);
}

static void mitigation_more(void)
{
    static const struct { DWORD64 value[2]; const char *what; } tries[] =
    {
        { { 3ull << 36 }, "dynamic code, threads may opt out" },
        { { 3ull << 44 }, "non-Microsoft binaries, store ones allowed" },
        { { 3ull << 48 }, "fonts, audit" },
        { { 1ull << 8 }, "force relocation" },
        { { 3ull << 8 }, "force relocation, relocations required" },
        { { 2ull << 16 }, "bottom-up ASLR off" },
        { { 2ull << 20 }, "high entropy ASLR off" },
        { { 1ull << 40 }, "control flow guard" },
        { { 2ull << 24 }, "strict handle checks off" },
        { { 2ull << 32 }, "extension points off" },
        { { 0x2 }, "ATL thunk emulation" },
        { { 0, 1ull << 4 }, "2: loader integrity continuity" },
        { { 0, 3ull << 4 }, "2: loader integrity continuity, audit" },
        { { 0, 1ull << 12 }, "2: module tampering protection" },
        { { 0, 3ull << 12 }, "2: module tampering protection, no inherit" },
        { { 0, 1ull << 20 }, "2: dynamic code downgrade allowed" },
        { { 0, 1ull << 28 }, "2: user shadow stacks" },
        { { 0, 1ull << 32 }, "2: set context IP validation" },
        { { 0, 3ull << 32 }, "2: set context IP validation, relaxed" },
        { { 0, 1ull << 40 }, "2: extended control flow guard" },
        { { 0, 1ull << 44 }, "2: pointer authentication" },
        { { 0, 1ull << 48 }, "2: CET dynamic APIs out of process only" },
        { { 0, 2ull << 48 }, "2: CET dynamic APIs out of process only, off" },
        { { 0, 1ull << 52 }, "2: restrict core sharing" },
        { { 0, 1ull << 56 }, "2: fsctl system calls" },
        { { 0, 1ull << 60 }, "2: bit 60" },
    };
    unsigned int i;

    print_policies("own policies", GetCurrentProcess());
    for (i = 0; i < ARRAYSIZE(tries); i++)
    {
        char what[128];
        snprintf(what, sizeof(what), "mitigation %s", tries[i].what);
        start_child(what, PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY, tries[i].value,
                    tries[i].value[1] ? 16 : 8, FALSE);
    }
}

static void child_policy(void)
{
    static const DWORD values[] = { 0, 1, 2, 4, 5, 8 };
    unsigned int i;

    for (i = 0; i < ARRAYSIZE(values); i++)
    {
        char what[64];
        snprintf(what, sizeof(what), "child process policy %#lx", values[i]);
        start_child(what, PROC_THREAD_ATTRIBUTE_CHILD_PROCESS_POLICY, &values[i], sizeof(DWORD), TRUE);
    }
    {
        DWORD64 value = 1;
        start_child("child process policy, 8 bytes", PROC_THREAD_ATTRIBUTE_CHILD_PROCESS_POLICY, &value,
                    sizeof(value), TRUE);
    }
}

static int child(const WCHAR *event_name, BOOL create)
{
    HANDLE event = OpenEventW(SYNCHRONIZE, FALSE, event_name);

    setvbuf(stdout, NULL, _IONBF, 0);
    print_policies("  the child's own", GetCurrentProcess());
    if (create)
    {
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        WCHAR cmdline[] = L"cmd.exe /c exit 7";
        BOOL ret;

        SetLastError(0xdeadbeef);
        ret = CreateProcessW(NULL, cmdline, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
        printf("  the child starts a process: %d error %lu", ret, ret ? 0 : GetLastError());
        if (ret)
        {
            DWORD code = 0;
            WaitForSingleObject(pi.hProcess, 10000);
            GetExitCodeProcess(pi.hProcess, &code);
            printf(", exit code %lu", code);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        printf("\n");
    }
    WaitForSingleObject(event, 20000);
    return 0;
}

static void write_with(const char *what, int property, LONG_PTR value)
{
    IXmlWriter *writer;
    IStream *stream;
    HGLOBAL global;
    HRESULT hr, hr2;
    char *data;
    SIZE_T size;

    if (FAILED(CreateXmlWriter(&IID_IXmlWriter, (void **)&writer, NULL))) return;
    CreateStreamOnHGlobal(NULL, TRUE, &stream);
    hr = property < 0 ? S_OK : IXmlWriter_SetProperty(writer, property, value);
    IXmlWriter_SetProperty(writer, XmlWriterProperty_OmitXmlDeclaration, TRUE);
    IXmlWriter_SetOutput(writer, (IUnknown *)stream);
    hr2 = IXmlWriter_WriteStartElement(writer, NULL, L"a", NULL);
    if (SUCCEEDED(hr2)) hr2 = IXmlWriter_WriteStartElement(writer, NULL, L"b", NULL);
    if (SUCCEEDED(hr2)) hr2 = IXmlWriter_WriteEndElement(writer);
    if (SUCCEEDED(hr2)) hr2 = IXmlWriter_WriteStartElement(writer, NULL, L"c", NULL);
    if (SUCCEEDED(hr2)) hr2 = IXmlWriter_WriteFullEndElement(writer);
    if (SUCCEEDED(hr2)) hr2 = IXmlWriter_WriteEndElement(writer);
    if (SUCCEEDED(hr2)) hr2 = IXmlWriter_Flush(writer);
    GetHGlobalFromStream(stream, &global);
    size = GlobalSize(global);
    data = GlobalLock(global);
    printf("XmlWriter %s: set %#lx, writing %#lx, output [", what, hr, hr2);
    if (data)
    {
        SIZE_T i;
        /* UTF-8 by default */
        for (i = 0; i < size && data[i]; i++) putchar(data[i] >= 0x20 && data[i] < 0x7f ? data[i] : '?');
        GlobalUnlock(global);
    }
    printf("]\n");
    IStream_Release(stream);
    IXmlWriter_Release(writer);
}

static void xml_writer(void)
{
    IXmlWriter *writer;
    LONG_PTR value;
    HRESULT hr;
    int i;

    write_with("default", -1, 0);
    write_with("CompactEmptyElement FALSE", 5, FALSE);
    write_with("CompactEmptyElement TRUE", 5, TRUE);
    write_with("CompactEmptyElement 2", 5, 2);
    if (SUCCEEDED(CreateXmlWriter(&IID_IXmlWriter, (void **)&writer, NULL)))
    {
        /* not 0, the MultiLanguage object */
        static const int properties[] = { 1, 2, 3, 4, 5 };
        static const LONG_PTR values[] = { -1, 0, 1, 2, 3, 0x100 };
        unsigned int j;

        for (i = 0; i < ARRAYSIZE(properties); i++)
        {
            printf("XmlWriter property %d:", properties[i]);
            for (j = 0; j < ARRAYSIZE(values); j++)
            {
                hr = IXmlWriter_SetProperty(writer, properties[i], values[j]);
                value = 0xdead;
                IXmlWriter_GetProperty(writer, properties[i], &value);
                printf(" set %lld %#lx now %lld;", (LONGLONG)values[j], hr, (LONGLONG)value);
            }
            printf("\n");
        }
        IXmlWriter_Release(writer);
    }
}

static const struct { const char *name; void (*run)(void); } sections[] =
{
    { "sets", private_sets }, { "nulltoken", null_token }, { "performance", performance_sizes },
    { "mitigation", mitigation_more }, { "childpolicy", child_policy }, { "xmlwriter", xml_writer },
};

int wmain(int argc, WCHAR **argv)
{
    unsigned int i;

    if (argc >= 4 && !wcscmp(argv[1], L"child")) return child(argv[2], _wtoi(argv[3]));
    setvbuf(stdout, NULL, _IONBF, 0);
    /* a child that cannot start says so to its parent instead of asking anyone; children inherit this */
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    for (i = 0; i < ARRAYSIZE(sections); i++)
    {
        char name[64];

        if (argc >= 3 && !wcscmp(argv[1], L"section"))
        {
            snprintf(name, sizeof(name), "%ls", argv[2]);
            if (strcmp(name, sections[i].name)) continue;
        }
        printf("== %s\n", sections[i].name);
        sections[i].run();
    }
    return 0;
}
