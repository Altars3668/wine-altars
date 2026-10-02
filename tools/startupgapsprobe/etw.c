/* etw: event tracing sessions as a program uses them for itself -- starting a real-time session (plain, private,
 * private in-process), enabling a provider of its own and what the provider's enable callback is told, consuming the
 * session's events with OpenTrace and ProcessTrace, what reaches the consumer of what the provider writes at which
 * level and keyword, querying and stopping the session; then RegisterEventSource and DeregisterEventSource (nothing is
 * reported, the event log is not written).  Prints results only. */
#define INITGUID
#include <windows.h>
#include <evntprov.h>
#include <evntrace.h>
#include <evntcons.h>
#include <stdio.h>
#include <wchar.h>

DEFINE_GUID(provider_guid, 0x5a1e3c0d, 0x7c11, 0x4b2a, 0x9e, 0x51, 0x30, 0x6b, 0x2f, 0x41, 0x8a, 0x77);
DEFINE_GUID(other_guid, 0x5a1e3c0e, 0x7c11, 0x4b2a, 0x9e, 0x51, 0x30, 0x6b, 0x2f, 0x41, 0x8a, 0x77);

static struct
{
    ULONG is_enabled;
    UCHAR level;
    ULONGLONG any, all;
    BOOL filter;
} callbacks[16];
static volatile LONG callback_count;

static void NTAPI enable_callback(const GUID *source, ULONG is_enabled, UCHAR level, ULONGLONG any, ULONGLONG all,
                                  EVENT_FILTER_DESCRIPTOR *filter, void *context)
{
    LONG i = InterlockedIncrement(&callback_count) - 1;
    if (i >= ARRAYSIZE(callbacks)) return;
    callbacks[i].is_enabled = is_enabled;
    callbacks[i].level = level;
    callbacks[i].any = any;
    callbacks[i].all = all;
    callbacks[i].filter = filter != NULL;
}

static void show_callbacks(const char *what, LONG from)
{
    LONG i;

    printf("  %s, %ld callbacks:", what, callback_count - from);
    for (i = from; i < callback_count && i < ARRAYSIZE(callbacks); i++)
        printf(" [enabled %lu level %u any %llx all %llx filter %d]", callbacks[i].is_enabled, callbacks[i].level,
               callbacks[i].any, callbacks[i].all, callbacks[i].filter);
    printf("\n");
}

static struct
{
    USHORT id;
    UCHAR level;
    ULONGLONG keyword;
    BOOL ours, same_process;
    USHORT flags, property;
    ULONG data_len;
    char data[32];
} received[64];
static volatile LONG received_count, other_count;

static void WINAPI event_callback(EVENT_RECORD *record)
{
    LONG i;

    if (!IsEqualGUID(&record->EventHeader.ProviderId, &provider_guid))
    {
        InterlockedIncrement(&other_count);
        return;
    }
    i = InterlockedIncrement(&received_count) - 1;
    if (i >= ARRAYSIZE(received)) return;
    received[i].id = record->EventHeader.EventDescriptor.Id;
    received[i].level = record->EventHeader.EventDescriptor.Level;
    received[i].keyword = record->EventHeader.EventDescriptor.Keyword;
    received[i].same_process = record->EventHeader.ProcessId == GetCurrentProcessId();
    received[i].flags = record->EventHeader.Flags;
    received[i].property = record->EventHeader.EventProperty;
    received[i].data_len = record->UserDataLength;
    memcpy(received[i].data, record->UserData, min(record->UserDataLength, sizeof(received[i].data) - 1));
}

static TRACEHANDLE consumer;
static ULONG process_status = 0xdead;

static DWORD WINAPI process_thread(void *arg)
{
    process_status = ProcessTrace(&consumer, 1, NULL, NULL);
    return 0;
}

static EVENT_TRACE_PROPERTIES *new_props(ULONG mode)
{
    ULONG size = sizeof(EVENT_TRACE_PROPERTIES) + 2 * 1024 * sizeof(WCHAR);
    EVENT_TRACE_PROPERTIES *props = calloc(1, size);

    props->Wnode.BufferSize = size;
    props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    props->Wnode.ClientContext = 1;
    props->LogFileMode = mode;
    props->FlushTimer = 1;
    props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    props->LogFileNameOffset = 0;
    return props;
}

static void write_events(REGHANDLE reg)
{
    static const struct { USHORT id; UCHAR level; ULONGLONG keyword; } events[] =
    {
        { 1, 4, 0x40 }, { 2, 5, 0x40 }, { 3, 4, 0x80 }, { 4, 2, 0x40 }, { 5, 4, 0 }, { 6, 0, 0x40 },
        { 7, 4, 0x40 | 0x80 },
    };
    EVENT_DATA_DESCRIPTOR data;
    unsigned int i;

    for (i = 0; i < ARRAYSIZE(events); i++)
    {
        EVENT_DESCRIPTOR desc = { events[i].id, 0, 0, events[i].level, 0, 0, events[i].keyword };
        char text[16];
        ULONG ret;

        sprintf(text, "event %u", events[i].id);
        EventDataDescCreate(&data, text, strlen(text) + 1);
        ret = EventWrite(reg, &desc, 1, &data);
        printf("  EventWrite id %u level %u keyword %llx: %lu, EventEnabled %d\n", events[i].id, events[i].level,
               events[i].keyword, ret, EventEnabled(reg, &desc));
    }
}

static void session(const char *what, ULONG mode)
{
    WCHAR name[64];
    EVENT_TRACE_PROPERTIES *props = new_props(mode), *query;

    if (mode & EVENT_TRACE_PRIVATE_LOGGER_MODE) props->Wnode.Guid = provider_guid;
    EVENT_TRACE_LOGFILEW logfile;
    TRACEHANDLE handle = 0;
    REGHANDLE reg = 0, other = 0;
    HANDLE thread = NULL;
    LONG from;
    ULONG ret;
    unsigned int i;

    printf("%s (mode %#lx):\n", what, mode);
    callback_count = 0;
    received_count = other_count = 0;
    ret = EventRegister(&provider_guid, enable_callback, NULL, &reg);
    printf("  EventRegister: %lu, provider enabled %d\n", ret, EventProviderEnabled(reg, 4, 0x40));
    swprintf(name, ARRAYSIZE(name), L"WineAltarsProbe-%lu", GetCurrentProcessId());
    ret = StartTraceW(&handle, name, props);
    printf("  StartTrace: %lu handle %s, logger name %s\n", ret, handle ? "set" : "0",
           !wcscmp((WCHAR *)((BYTE *)props + props->LoggerNameOffset), name) ? "copied" : "not copied");
    if (ret)
    {
        EventUnregister(reg);
        free(props);
        return;
    }
    {
        EVENT_TRACE_PROPERTIES *again = new_props(mode);
        TRACEHANDLE handle2 = 0;
        ret = StartTraceW(&handle2, name, again);
        printf("  StartTrace the same name: %lu\n", ret);
        if (!ret) ControlTraceW(handle2, NULL, again, EVENT_TRACE_CONTROL_STOP);
        free(again);
    }

    memset(&logfile, 0, sizeof(logfile));
    logfile.LoggerName = name;
    logfile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    logfile.EventRecordCallback = event_callback;
    consumer = OpenTraceW(&logfile);
    printf("  OpenTrace: %s error %lu\n", consumer == INVALID_PROCESSTRACE_HANDLE ? "invalid" : "opened",
           consumer == INVALID_PROCESSTRACE_HANDLE ? GetLastError() : 0);
    if (consumer != INVALID_PROCESSTRACE_HANDLE) thread = CreateThread(NULL, 0, process_thread, NULL, 0, NULL);

    from = callback_count;
    ret = EnableTraceEx2(handle, &provider_guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 4, 0x40, 0, 0, NULL);
    show_callbacks("EnableTraceEx2 level 4 any 40", from);
    printf("    returned %lu, provider enabled at 4/40 %d, 5/40 %d, 4/80 %d, 4/0 %d\n", ret,
           EventProviderEnabled(reg, 4, 0x40), EventProviderEnabled(reg, 5, 0x40), EventProviderEnabled(reg, 4, 0x80),
           EventProviderEnabled(reg, 4, 0));
    from = callback_count;
    ret = EnableTraceEx2(handle, &provider_guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 4, 0x40, 0x40, 0, NULL);
    show_callbacks("again with all 40", from);
    from = callback_count;
    ret = EnableTraceEx2(handle, &other_guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 2, 0x10c38, 0, 0, NULL);
    printf("  EnableTraceEx2 a provider nobody registered: %lu\n", ret);
    ret = EventRegister(&other_guid, enable_callback, NULL, &other);
    show_callbacks("registering it afterwards", from);
    ret = EnableTraceEx2(handle, &provider_guid, 99, 4, 0x40, 0, 0, NULL);
    printf("  EnableTraceEx2 control code 99: %lu\n", ret);
    ret = EnableTraceEx2(0x12345, &provider_guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 4, 0x40, 0, 0, NULL);
    printf("  EnableTraceEx2 a bad handle: %lu\n", ret);

    write_events(reg);
    query = new_props(0);
    ret = ControlTraceW(handle, NULL, query, EVENT_TRACE_CONTROL_FLUSH);
    printf("  ControlTrace flush: %lu\n", ret);
    for (i = 0; i < 30 && received_count < 3; i++) Sleep(100);
    Sleep(300);
    printf("  received %ld of ours, %ld others:", received_count, other_count);
    for (i = 0; i < received_count && i < ARRAYSIZE(received); i++)
        printf(" [id %u level %u keyword %llx process %s flags %#x property %#x data %lu '%s']", received[i].id,
               received[i].level, received[i].keyword, received[i].same_process ? "ours" : "other", received[i].flags,
               received[i].property, received[i].data_len, received[i].data);
    printf("\n");

    ret = ControlTraceW(handle, NULL, query, EVENT_TRACE_CONTROL_QUERY);
    printf("  ControlTrace query: %lu, buffer %lu KiB, buffers min %lu max %lu now %lu free %lu written %lu,"
           " events lost %lu, mode %#lx, flush %lu, logger thread %s, name %s\n", ret, query->BufferSize,
           query->MinimumBuffers, query->MaximumBuffers, query->NumberOfBuffers, query->FreeBuffers,
           query->BuffersWritten, query->EventsLost, query->LogFileMode, query->FlushTimer,
           query->LoggerThreadId ? "set" : "0",
           query->LoggerNameOffset && !wcscmp((WCHAR *)((BYTE *)query + query->LoggerNameOffset), name) ? "ours" : "other");
    {
        EVENT_TRACE_PROPERTIES *by_name = new_props(0);
        ret = ControlTraceW(0, name, by_name, EVENT_TRACE_CONTROL_QUERY);
        printf("  ControlTrace query by name: %lu\n", ret);
        free(by_name);
    }

    from = callback_count;
    ret = EnableTraceEx2(handle, &provider_guid, EVENT_CONTROL_CODE_DISABLE_PROVIDER, 0, 0, 0, 0, NULL);
    show_callbacks("disabling", from);
    printf("    returned %lu, provider enabled %d\n", ret, EventProviderEnabled(reg, 4, 0x40));
    ret = EnableTraceEx2(handle, &provider_guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 5, 0, 0, 0, NULL);
    from = callback_count;
    ret = ControlTraceW(handle, NULL, query, EVENT_TRACE_CONTROL_STOP);
    printf("  StopTrace: %lu, events lost %lu\n", ret, query->EventsLost);
    show_callbacks("stopping with the provider enabled", from);
    if (thread)
    {
        ret = WaitForSingleObject(thread, 5000);
        printf("  ProcessTrace %s: %lu\n", ret ? "still running" : "returned", process_status);
        if (!ret) CloseHandle(thread);
        ret = CloseTrace(consumer);
        printf("  CloseTrace: %lu\n", ret);
    }
    ret = ControlTraceW(handle, NULL, query, EVENT_TRACE_CONTROL_STOP);
    printf("  StopTrace again: %lu\n", ret);
    from = callback_count;
    EventUnregister(reg);
    EventUnregister(other);
    show_callbacks("unregistering", from);
    free(query);
    free(props);
}

static void start_only(const char *what, ULONG mode)
{
    EVENT_TRACE_PROPERTIES *props = new_props(mode);
    TRACEHANDLE handle = 0;
    REGHANDLE reg = 0;
    WCHAR name[64];
    ULONG ret;

    if (mode & EVENT_TRACE_PRIVATE_LOGGER_MODE) props->Wnode.Guid = provider_guid;
    EventRegister(&provider_guid, enable_callback, NULL, &reg);
    swprintf(name, ARRAYSIZE(name), L"WineAltarsProbe-%lu", GetCurrentProcessId());
    ret = StartTraceW(&handle, name, props);
    printf("  %s: StartTrace %lu", what, ret);
    if (!ret)
    {
        ret = EnableTraceEx2(handle, &provider_guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 4, 0x40, 0, 0, NULL);
        printf(", EnableTraceEx2 %lu", ret);
        ret = ControlTraceW(handle, NULL, props, EVENT_TRACE_CONTROL_STOP);
        printf(", stop %lu", ret);
    }
    printf("\n");
    EventUnregister(reg);
    free(props);
}

static void restricted(void)
{
    SID_IDENTIFIER_AUTHORITY nt = { SECURITY_NT_AUTHORITY };
    SID_AND_ATTRIBUTES deny = { 0 };
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    WCHAR exe[MAX_PATH], cmdline[MAX_PATH + 32];
    HANDLE token, limited;
    BOOL ret;

    AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &deny.Sid);
    OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY | TOKEN_QUERY, &token);
    ret = CreateRestrictedToken(token, DISABLE_MAX_PRIVILEGE, 1, &deny, 0, NULL, 0, NULL, &limited);
    printf("CreateRestrictedToken: %d error %lu\n", ret, ret ? 0 : GetLastError());
    if (!ret) return;
    GetModuleFileNameW(NULL, exe, ARRAYSIZE(exe));
    swprintf(cmdline, ARRAYSIZE(cmdline), L"\"%ls\" restricted", exe);
    ret = CreateProcessAsUserW(limited, exe, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    printf("CreateProcessAsUser: %d error %lu\n", ret, ret ? 0 : GetLastError());
    if (ret)
    {
        WaitForSingleObject(pi.hProcess, 30000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    CloseHandle(limited);
    CloseHandle(token);
    FreeSid(deny.Sid);
}

int main(int argc, char **argv)
{
    HANDLE token, source;
    TOKEN_ELEVATION elevation = { 0 };
    DWORD size;

    setvbuf(stdout, NULL, _IONBF, 0);
    OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token);
    GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    printf("%selevated %lu\n", argc > 1 ? "restricted child: " : "", elevation.TokenIsElevated);
    CloseHandle(token);
    if (argc > 1)
    {
        start_only("real time", EVENT_TRACE_REAL_TIME_MODE);
        start_only("private, real time", EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_PRIVATE_LOGGER_MODE);
        start_only("private in process, real time", EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_PRIVATE_LOGGER_MODE |
                   EVENT_TRACE_PRIVATE_IN_PROC);
        return 0;
    }

    session("real time", EVENT_TRACE_REAL_TIME_MODE);
    session("private, real time", EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_PRIVATE_LOGGER_MODE);
    session("private in process, real time", EVENT_TRACE_REAL_TIME_MODE | EVENT_TRACE_PRIVATE_LOGGER_MODE |
            EVENT_TRACE_PRIVATE_IN_PROC);
    session("private in process, buffering", EVENT_TRACE_BUFFERING_MODE | EVENT_TRACE_PRIVATE_LOGGER_MODE |
            EVENT_TRACE_PRIVATE_IN_PROC);

    SetLastError(0xdeadbeef);
    source = RegisterEventSourceW(NULL, L"Outlook");
    printf("RegisterEventSource Outlook: %s error %lu\n", source ? "handle" : "NULL", source ? 0 : GetLastError());
    if (source) printf("DeregisterEventSource: %d\n", DeregisterEventSource(source));
    SetLastError(0xdeadbeef);
    source = RegisterEventSourceW(L"", L"WineAltarsNoSuchSource");
    printf("RegisterEventSource an unknown source: %s error %lu\n", source ? "handle" : "NULL", source ? 0 : GetLastError());
    if (source) DeregisterEventSource(source);
    SetLastError(0xdeadbeef);
    source = RegisterEventSourceA(NULL, "Outlook");
    printf("RegisterEventSourceA: %s error %lu\n", source ? "handle" : "NULL", source ? 0 : GetLastError());
    if (source) DeregisterEventSource(source);
    SetLastError(0xdeadbeef);
    printf("DeregisterEventSource NULL: %d error %lu\n", DeregisterEventSource(NULL), GetLastError());
    SetLastError(0xdeadbeef);
    source = RegisterEventSourceW(L"\\\\no-such-host-wine-altars", L"Outlook");
    printf("RegisterEventSource on another server: %s error %lu\n", source ? "handle" : "NULL",
           source ? 0 : GetLastError());
    if (source) DeregisterEventSource(source);
    restricted();
    return 0;
}
