/* etw2: the private in-process event tracing session Word starts for itself, made the same way -- a circular log
 * file, ADDTO_TRIAGE_DUMP, the system clock, the control GUID in the properties -- and what happens around it: whether
 * StartTrace takes it, what EnableTraceEx2 returns and when the provider's enable callback runs (on which thread),
 * what EventProviderEnabled and EventWrite say, what a query reports, whether the log file is written, and how
 * stopping goes; also the variants without the log file, without triage and with buffering, and the private logger
 * that is not in-process.  Run as it is and as a restricted child (not elevated), as Word runs.  The log file goes to
 * the temporary directory and is removed.  Prints results only. */
#define INITGUID
#include <windows.h>
#include <evntprov.h>
#include <evntrace.h>
#include <stdio.h>
#include <wchar.h>

DEFINE_GUID(control_guid, 0x5a1e3c10, 0x7c11, 0x4b2a, 0x9e, 0x51, 0x30, 0x6b, 0x2f, 0x41, 0x8a, 0x77);
DEFINE_GUID(provider_guid, 0x5a1e3c11, 0x7c11, 0x4b2a, 0x9e, 0x51, 0x30, 0x6b, 0x2f, 0x41, 0x8a, 0x77);

#define EVENT_TRACE_ADDTO_TRIAGE_DUMP_ 0x80000000

static volatile LONG callbacks;
static DWORD callback_thread;
static ULONG last_enabled;
static UCHAR last_level;
static ULONGLONG last_any;

static void NTAPI enable_callback(const GUID *source, ULONG is_enabled, UCHAR level, ULONGLONG any, ULONGLONG all,
                                  EVENT_FILTER_DESCRIPTOR *filter, void *context)
{
    InterlockedIncrement(&callbacks);
    callback_thread = GetCurrentThreadId();
    last_enabled = is_enabled;
    last_level = level;
    last_any = any;
}

static ULONG max_file;

static EVENT_TRACE_PROPERTIES *word_props(ULONG mode, const WCHAR *logfile)
{
    ULONG size = 1160;
    EVENT_TRACE_PROPERTIES *props = calloc(1, size);

    props->MaximumFileSize = max_file;
    props->Wnode.BufferSize = size;
    props->Wnode.Guid = control_guid;
    props->Wnode.ClientContext = 2;
    props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    props->LogFileMode = mode;
    props->LogFileNameOffset = logfile ? 120 : 0;
    props->LoggerNameOffset = 640;
    if (logfile) wcscpy((WCHAR *)((BYTE *)props + 120), logfile);
    return props;
}

static void run(const char *what, ULONG mode, BOOL with_file)
{
    WCHAR name[64], file[MAX_PATH], temp[MAX_PATH];
    EVENT_TRACE_PROPERTIES *props, *query;
    TRACEHANDLE handle = 0;
    REGHANDLE reg = 0;
    LONG before;
    ULONG ret, i;
    WIN32_FILE_ATTRIBUTE_DATA attrs;

    GetTempPathW(ARRAYSIZE(temp), temp);
    swprintf(file, ARRAYSIZE(file), L"%lsWineAltarsEtw-%lu.etl", temp, GetCurrentProcessId());
    swprintf(name, ARRAYSIZE(name), L"v16_PROBE:%lu:1", GetCurrentProcessId());
    props = word_props(mode, with_file ? file : NULL);
    callbacks = 0;
    callback_thread = 0;
    EventRegister(&provider_guid, enable_callback, NULL, &reg);

    ret = StartTraceW(&handle, name, props);
    printf("%s (mode %#lx%s): StartTrace %lu handle %s, buffer %lu min %lu max %lu, mode after %#lx\n", what, mode,
           with_file ? ", a log file" : "", ret, handle ? "set" : "0", props->BufferSize, props->MinimumBuffers,
           props->MaximumBuffers, props->LogFileMode);
    if (ret)
    {
        EventUnregister(reg);
        free(props);
        return;
    }

    before = callbacks;
    ret = EnableTraceEx2(handle, &provider_guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 5, 0xffffffffffffffffull, 0,
                         0, NULL);
    printf("  EnableTraceEx2: %lu, callbacks on return %ld, provider enabled %d\n", ret, callbacks - before,
           EventProviderEnabled(reg, 5, 1));
    for (i = 0; i < 20 && callbacks == before; i++) Sleep(50);
    printf("  callbacks after a wait %ld, %s, enabled %lu level %u any %llx\n", callbacks - before,
           !callback_thread ? "none" : callback_thread == GetCurrentThreadId() ? "on this thread" : "on another thread",
           last_enabled, last_level, last_any);
    ret = EnableTraceEx2(handle, &provider_guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 5, 0xffffffffffffffffull, 0,
                         5000, NULL);
    printf("  EnableTraceEx2 with a timeout: %lu\n", ret);

    for (i = 0; i < 3; i++)
    {
        EVENT_DESCRIPTOR desc = { (USHORT)(i + 1), 0, 0, 4, 0, 0, 1 };
        EVENT_DATA_DESCRIPTOR data;
        char text[] = "probe event";

        EventDataDescCreate(&data, text, sizeof(text));
        ret = EventWrite(reg, &desc, 1, &data);
        if (!i) printf("  EventWrite: %lu, EventEnabled %d\n", ret, EventEnabled(reg, &desc));
    }

    query = word_props(0, NULL);
    ret = ControlTraceW(handle, NULL, query, EVENT_TRACE_CONTROL_QUERY);
    printf("  query: %lu, buffer %lu KiB, min %lu max %lu now %lu free %lu written %lu lost %lu, mode %#lx, "
           "clock %lu, log file %s\n", ret, query->BufferSize, query->MinimumBuffers, query->MaximumBuffers,
           query->NumberOfBuffers, query->FreeBuffers, query->BuffersWritten, query->EventsLost, query->LogFileMode,
           query->Wnode.ClientContext, query->LogFileNameOffset &&
           !wcscmp((WCHAR *)((BYTE *)query + query->LogFileNameOffset), file) ? "ours" : "other or none");
    ret = ControlTraceW(handle, NULL, query, EVENT_TRACE_CONTROL_FLUSH);
    printf("  flush: %lu\n", ret);
    ret = ControlTraceW(0, name, query, EVENT_TRACE_CONTROL_QUERY);
    printf("  query by name: %lu\n", ret);
    before = callbacks;
    ret = ControlTraceW(handle, NULL, query, EVENT_TRACE_CONTROL_STOP);
    printf("  stop: %lu, events lost %lu, callbacks on return %ld\n", ret, query->EventsLost, callbacks - before);
    if (with_file)
    {
        if (GetFileAttributesExW(file, GetFileExInfoStandard, &attrs))
            printf("  the log file: %lu bytes\n", attrs.nFileSizeLow);
        else
            printf("  the log file: none, error %lu\n", GetLastError());
        DeleteFileW(file);
    }
    EventUnregister(reg);
    free(query);
    free(props);
}

static void all(void)
{
    max_file = 10;
    run("Word's, 10 MB", 0x80020802, TRUE);
    run("no triage, 10 MB", 0x20802, TRUE);
    run("sequential, 10 MB", 0x20801, TRUE);
    max_file = 0;
    run("Word's", 0x80020802, TRUE);
    run("no triage", 0x20802, TRUE);
    run("no log file", 0x80020802, FALSE);
    run("buffering", 0x20c00, FALSE);
    run("sequential", 0x20801, TRUE);
    run("not in process", 0x80000802, TRUE);
}

int main(int argc, char **argv)
{
    HANDLE token, limited;
    TOKEN_ELEVATION elevation = { 0 };
    DWORD size;

    setvbuf(stdout, NULL, _IONBF, 0);
    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token);
    GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    printf("%selevated %lu\n", argc > 1 ? "restricted child: " : "", elevation.TokenIsElevated);
    CloseHandle(token);
    all();
    if (argc > 1) return 0;

    {
        SID_IDENTIFIER_AUTHORITY nt = { SECURITY_NT_AUTHORITY };
        SID_AND_ATTRIBUTES deny = { 0 };
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        WCHAR exe[MAX_PATH], cmdline[MAX_PATH + 32];

        AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                                 &deny.Sid);
        OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_QUERY, &token);
        if (CreateRestrictedToken(token, DISABLE_MAX_PRIVILEGE, 1, &deny, 0, NULL, 0, NULL, &limited))
        {
            GetModuleFileNameW(NULL, exe, ARRAYSIZE(exe));
            swprintf(cmdline, ARRAYSIZE(cmdline), L"\"%ls\" restricted", exe);
            if (CreateProcessAsUserW(limited, exe, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
            {
                WaitForSingleObject(pi.hProcess, 60000);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            }
            CloseHandle(limited);
        }
        CloseHandle(token);
        FreeSid(deny.Sid);
    }
    return 0;
}
