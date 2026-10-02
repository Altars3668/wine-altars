/* etw4: what is left of a program's own event tracing sessions -- finding a session by its name, for a private and a
 * real-time one, with the name given, in the properties, or both; the activity IDs EventActivityIdControl makes, one
 * after another and in another process; and reading back the log file a private session wrote, with OpenTrace and
 * ProcessTrace, as EVENT_RECORDs and as EVENT_TRACEs: the header event, each kind of event, the buffers and what the
 * log file structure holds afterwards.  The log file goes to the temporary directory and is removed; its path is not
 * printed.  Prints results only. */
#define INITGUID
#include <windows.h>
#include <evntprov.h>
#include <evntrace.h>
#include <evntcons.h>
#include <stdio.h>
#include <wchar.h>

static const GUID null_guid;
DEFINE_GUID(prov_guid,    0x5a1e3c30, 0x7c11, 0x4b2a, 0x9e, 0x51, 0x30, 0x6b, 0x2f, 0x41, 0x8a, 0x77);
DEFINE_GUID(ctrl_guid,    0x5a1e3c31, 0x7c11, 0x4b2a, 0x9e, 0x51, 0x30, 0x6b, 0x2f, 0x41, 0x8a, 0x77);
DEFINE_GUID(classic_guid, 0x5a1e3c32, 0x7c11, 0x4b2a, 0x9e, 0x51, 0x30, 0x6b, 0x2f, 0x41, 0x8a, 0x77);
DEFINE_GUID(class_guid,   0x5a1e3c33, 0x7c11, 0x4b2a, 0x9e, 0x51, 0x30, 0x6b, 0x2f, 0x41, 0x8a, 0x77);
DEFINE_GUID(msg_guid,     0x5a1e3c34, 0x7c11, 0x4b2a, 0x9e, 0x51, 0x30, 0x6b, 0x2f, 0x41, 0x8a, 0x77);
DEFINE_GUID(act_guid,     0x5a1e3c35, 0x7c11, 0x4b2a, 0x9e, 0x51, 0x30, 0x6b, 0x2f, 0x41, 0x8a, 0x77);
DEFINE_GUID(rel_guid,     0x5a1e3c36, 0x7c11, 0x4b2a, 0x9e, 0x51, 0x30, 0x6b, 0x2f, 0x41, 0x8a, 0x77);
DEFINE_GUID(event_trace_guid, 0x68fdd900, 0x4a3e, 0x11d1, 0x84, 0xf4, 0x00, 0x00, 0xf8, 0x04, 0x64, 0xe3);

#define PRIVATE_MODE (EVENT_TRACE_PRIVATE_LOGGER_MODE | EVENT_TRACE_PRIVATE_IN_PROC)

static EVENT_TRACE_PROPERTIES *new_props(ULONG mode, const GUID *guid, const WCHAR *file, const WCHAR *name)
{
    ULONG size = sizeof(EVENT_TRACE_PROPERTIES) + 2 * 1024 * sizeof(WCHAR);
    EVENT_TRACE_PROPERTIES *props = calloc(1, size);

    props->Wnode.BufferSize = size;
    props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    props->Wnode.ClientContext = 2;
    if (guid) props->Wnode.Guid = *guid;
    props->LogFileMode = mode;
    props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    if (name) wcscpy((WCHAR *)((BYTE *)props + props->LoggerNameOffset), name);
    if (file)
    {
        props->LogFileNameOffset = sizeof(EVENT_TRACE_PROPERTIES) + 1024 * sizeof(WCHAR);
        wcscpy((WCHAR *)((BYTE *)props + props->LogFileNameOffset), file);
    }
    return props;
}

static void by_name(const char *what, ULONG mode, const GUID *guid)
{
    static const struct { const char *how; BOOL give, in_props; ULONG control; } ways[] =
    {
        { "query, the name given", TRUE, FALSE, EVENT_TRACE_CONTROL_QUERY },
        { "query, the name in the properties too", TRUE, TRUE, EVENT_TRACE_CONTROL_QUERY },
        { "query, the name only in the properties", FALSE, TRUE, EVENT_TRACE_CONTROL_QUERY },
        { "flush, the name given", TRUE, FALSE, EVENT_TRACE_CONTROL_FLUSH },
        { "stop, the name given", TRUE, FALSE, EVENT_TRACE_CONTROL_STOP },
        { "stop, the name in the properties too", TRUE, TRUE, EVENT_TRACE_CONTROL_STOP },
        { "stop, the name only in the properties", FALSE, TRUE, EVENT_TRACE_CONTROL_STOP },
    };
    WCHAR name[64], other[64];
    unsigned int i;

    printf("%s by name:\n", what);
    swprintf(name, ARRAYSIZE(name), L"WineAltarsEtw4-%lu", GetCurrentProcessId());
    swprintf(other, ARRAYSIZE(other), L"WineAltarsEtw4x-%lu", GetCurrentProcessId());
    for (i = 0; i < ARRAYSIZE(ways); i++)
    {
        EVENT_TRACE_PROPERTIES *props = new_props(mode, guid, NULL, NULL), *q;
        TRACEHANDLE handle = 0;
        ULONG ret;

        if ((ret = StartTraceW(&handle, name, props)))
        {
            printf("  StartTrace %lu\n", ret);
            free(props);
            return;
        }
        q = new_props(0, NULL, NULL, ways[i].in_props ? name : NULL);
        ret = ControlTraceW(0, ways[i].give ? name : NULL, q, ways[i].control);
        printf("  %s: %lu", ways[i].how, ret);
        if (ways[i].control != EVENT_TRACE_CONTROL_STOP || ret)
        {
            EVENT_TRACE_PROPERTIES *s = new_props(0, NULL, NULL, NULL);
            printf(", stop by handle %lu", ControlTraceW(handle, NULL, s, EVENT_TRACE_CONTROL_STOP));
            free(s);
        }
        printf("\n");
        free(q);
        free(props);
    }
    {
        EVENT_TRACE_PROPERTIES *props = new_props(mode, guid, NULL, NULL), *q;
        TRACEHANDLE handle = 0;
        ULONG ret;

        if (!StartTraceW(&handle, name, props))
        {
            q = new_props(0, NULL, NULL, other);
            ret = ControlTraceW(0, name, q, EVENT_TRACE_CONTROL_QUERY);
            printf("  query, another name in the properties: %lu\n", ret);
            q = new_props(0, NULL, NULL, name);
            ret = StopTraceW(0, name, q);
            printf("  StopTraceW, the name in the properties too: %lu\n", ret);
            if (ret) ControlTraceW(handle, NULL, q, EVENT_TRACE_CONTROL_STOP);
            free(q);
        }
        free(props);
    }
}

static void activity_ids(void)
{
    GUID ids[40];
    unsigned int i, j;
    ULONG changed = 0;

    printf("activity IDs:\n");
    for (i = 0; i < ARRAYSIZE(ids); i++) EventActivityIdControl(EVENT_ACTIVITY_CTRL_CREATE_ID, &ids[i]);
    printf("  the first {%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}\n", ids[0].Data1, ids[0].Data2, ids[0].Data3,
           ids[0].Data4[0], ids[0].Data4[1], ids[0].Data4[2], ids[0].Data4[3], ids[0].Data4[4], ids[0].Data4[5],
           ids[0].Data4[6], ids[0].Data4[7]);
    printf("  the second {%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}\n", ids[1].Data1, ids[1].Data2, ids[1].Data3,
           ids[1].Data4[0], ids[1].Data4[1], ids[1].Data4[2], ids[1].Data4[3], ids[1].Data4[4], ids[1].Data4[5],
           ids[1].Data4[6], ids[1].Data4[7]);
    printf("  the ninth bytes:");
    for (i = 0; i < ARRAYSIZE(ids); i++) printf(" %02x", ids[i].Data4[0]);
    printf("\n");
    for (i = 1; i < ARRAYSIZE(ids); i++)
        for (j = 0; j < 16; j++)
            if (((BYTE *)&ids[i])[j] != ((BYTE *)&ids[0])[j]) changed |= 1 << j;
    printf("  bytes that change over 40: %#lx\n", changed);
    printf("  the tenth to sixteenth of the last:");
    for (j = 1; j < 8; j++) printf(" %02x", ids[ARRAYSIZE(ids) - 1].Data4[j]);
    printf("\n");
}

/* reading the log file back */
static struct
{
    EVENT_HEADER header;
    ETW_BUFFER_CONTEXT context;
    USHORT ext_count, ext_types[4], ext_sizes[4];
    ULONG user_len;
    BYTE user[48];
} recs[32];
static LONG rec_count, buffer_calls;
static LARGE_INTEGER start_qpc;

static void WINAPI record_callback(EVENT_RECORD *record)
{
    LONG i = rec_count++;
    USHORT j;

    if (i >= ARRAYSIZE(recs)) return;
    recs[i].header = record->EventHeader;
    recs[i].context = record->BufferContext;
    recs[i].ext_count = record->ExtendedDataCount;
    for (j = 0; j < record->ExtendedDataCount && j < 4; j++)
    {
        recs[i].ext_types[j] = record->ExtendedData[j].ExtType;
        recs[i].ext_sizes[j] = record->ExtendedData[j].DataSize;
    }
    recs[i].user_len = record->UserDataLength;
    memcpy(recs[i].user, record->UserData, min(record->UserDataLength, sizeof(recs[i].user)));
}

static struct
{
    EVENT_TRACE_HEADER header;
    ULONG mof_len, client;
    BYTE mof[48];
} traces[32];
static LONG trace_count;

static void WINAPI trace_callback(EVENT_TRACE *event)
{
    LONG i = trace_count++;

    if (i >= ARRAYSIZE(traces)) return;
    traces[i].header = event->Header;
    traces[i].mof_len = event->MofLength;
    traces[i].client = event->ClientContext;
    memcpy(traces[i].mof, event->MofData, min(event->MofLength, sizeof(traces[i].mof)));
}

static ULONG WINAPI classic_request(WMIDPREQUESTCODE code, void *context, ULONG *size, void *buffer)
{
    return ERROR_SUCCESS;
}

static ULONG WINAPI buffer_callback(EVENT_TRACE_LOGFILEW *logfile)
{
    buffer_calls++;
    return TRUE;
}

static const char *guid_name(const GUID *guid)
{
    if (IsEqualGUID(guid, &prov_guid)) return "ours";
    if (IsEqualGUID(guid, &class_guid)) return "the class";
    if (IsEqualGUID(guid, &msg_guid)) return "the message's";
    if (IsEqualGUID(guid, &event_trace_guid)) return "EventTraceGuid";
    if (IsEqualGUID(guid, &null_guid)) return "zero";
    return "other";
}

static const char *time_kind(LONGLONG t)
{
    FILETIME now;
    LARGE_INTEGER n;

    GetSystemTimeAsFileTime(&now);
    n.LowPart = now.dwLowDateTime;
    n.HighPart = now.dwHighDateTime;
    if (!t) return "0";
    if (t <= n.QuadPart && t > n.QuadPart - 600000000) return "system time";
    if (t >= start_qpc.QuadPart - 1000000000 && t <= start_qpc.QuadPart + 1000000000) return "the counter";
    return "other";
}

static void hex(const BYTE *data, ULONG size)
{
    ULONG i;
    for (i = 0; i < size; i++) printf("%02x", data[i]);
}

static void show_logfile(const EVENT_TRACE_LOGFILEW *logfile)
{
    const TRACE_LOGFILE_HEADER *h = &logfile->LogfileHeader;

    printf("    the log file structure: buffers read %lu, buffer size %lu, filled %lu, events lost %lu, mode %#lx,"
           " current time %s, kernel trace %lu, the name %s, the logger name %s;\n"
           "    header: buffer %lu version %#lx provider version %s processors %s end %s resolution %lu max file %lu"
           " mode %#lx buffers written %lu start buffers %lu pointer %lu lost %lu cpu %s logger name %s file name %s"
           " start %s reserved %lu perf freq %s boot %s buffers lost %lu\n",
           logfile->BuffersRead, logfile->BufferSize, logfile->Filled, logfile->EventsLost, logfile->LogFileMode,
           time_kind(logfile->CurrentTime), logfile->IsKernelTrace, logfile->LogFileName ? "set" : "NULL",
           logfile->LoggerName ? "set" : "NULL", h->BufferSize, h->Version, h->ProviderVersion ? "set" : "0",
           h->NumberOfProcessors ? "set" : "0", time_kind(h->EndTime.QuadPart), h->TimerResolution,
           h->MaximumFileSize, h->LogFileMode, h->BuffersWritten, h->StartBuffers, h->PointerSize, h->EventsLost,
           h->CpuSpeedInMHz ? "set" : "0", h->LoggerName ? "set" : "NULL", h->LogFileName ? "set" : "NULL",
           time_kind(h->StartTime.QuadPart), h->ReservedFlags, h->PerfFreq.QuadPart ? "set" : "0",
           h->BootTime.QuadPart ? "set" : "0", h->BuffersLost);
}

static void read_back(void)
{
    WCHAR temp[MAX_PATH], file[MAX_PATH], name[64];
    EVENT_TRACE_PROPERTIES *props, *q;
    EVENT_TRACE_LOGFILEW logfile;
    TRACE_GUID_REGISTRATION classes[1] = { { &class_guid } };
    TRACEHANDLE session, consumer, classic = 0;
    REGHANDLE reg = 0;
    LONG i;
    ULONG ret, value = 0x11223344;

    printf("reading back a private session's log file:\n");
    GetTempPathW(ARRAYSIZE(temp), temp);
    swprintf(file, ARRAYSIZE(file), L"%lsWineAltarsEtw4-%lu.etl", temp, GetCurrentProcessId());
    swprintf(name, ARRAYSIZE(name), L"WineAltarsEtw4r-%lu", GetCurrentProcessId());
    props = new_props(PRIVATE_MODE | EVENT_TRACE_FILE_MODE_SEQUENTIAL, &ctrl_guid, file, NULL);
    QueryPerformanceCounter(&start_qpc);
    if ((ret = StartTraceW(&session, name, props)))
    {
        printf("  StartTrace %lu\n", ret);
        return;
    }
    EventRegister(&prov_guid, NULL, NULL, &reg);
    EnableTraceEx2(session, &prov_guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 5, 0, 0, 0, NULL);
    {
        EVENT_DATA_DESCRIPTOR data;
        char text[] = "event";
        GUID guid = act_guid;

        EventActivityIdControl(EVENT_ACTIVITY_CTRL_SET_ID, &guid);
        EventDataDescCreate(&data, text, sizeof(text));
        EventWrite(reg, &(EVENT_DESCRIPTOR){ 1, 2, 3, 4, 5, 6, 0x7 }, 1, &data);
        EventWriteString(reg, 4, 1, L"a string");
        EventWriteTransfer(reg, &(EVENT_DESCRIPTOR){ 3, 0, 0, 4, 0, 0, 1 }, NULL, &rel_guid, 1, &data);
        memset(&guid, 0, sizeof(guid));
        EventActivityIdControl(EVENT_ACTIVITY_CTRL_SET_ID, &guid);
    }
    RegisterTraceGuidsW(classic_request, NULL, &classic_guid, 1, classes, NULL, NULL, &classic);
    {
        struct { EVENT_TRACE_HEADER header; ULONG data[2]; } ev = { { 0 } };

        ev.header.Size = sizeof(ev);
        ev.header.Flags = WNODE_FLAG_TRACED_GUID;
        ev.header.Guid = class_guid;
        ev.header.Class.Type = 9;
        ev.header.Class.Level = 3;
        ev.header.Class.Version = 1;
        ev.data[0] = 0xa1a2a3a4;
        TraceEvent(session, &ev.header);
        TraceMessage(session, TRACE_MESSAGE_GUID | TRACE_MESSAGE_TIMESTAMP | TRACE_MESSAGE_SYSTEMINFO, &msg_guid, 7,
                     &value, sizeof(value), NULL);
    }
    q = new_props(0, NULL, NULL, NULL);
    ret = ControlTraceW(session, NULL, q, EVENT_TRACE_CONTROL_STOP);
    printf("  stop %lu, buffers written %lu\n", ret, q->BuffersWritten);
    UnregisterTraceGuids(classic);
    EventUnregister(reg);

    memset(&logfile, 0, sizeof(logfile));
    logfile.LogFileName = file;
    logfile.ProcessTraceMode = PROCESS_TRACE_MODE_EVENT_RECORD;
    logfile.EventRecordCallback = record_callback;
    logfile.BufferCallback = buffer_callback;
    SetLastError(0xdeadbeef);
    consumer = OpenTraceW(&logfile);
    printf("  OpenTrace: %s error %lu\n", consumer == INVALID_PROCESSTRACE_HANDLE ? "invalid" : "opened",
           GetLastError());
    show_logfile(&logfile);
    if (consumer != INVALID_PROCESSTRACE_HANDLE)
    {
        ret = ProcessTrace(&consumer, 1, NULL, NULL);
        printf("  ProcessTrace: %lu, %ld records, %ld buffer callbacks\n", ret, rec_count, buffer_calls);
        for (i = 0; i < rec_count && i < ARRAYSIZE(recs); i++)
        {
            EVENT_HEADER *h = &recs[i].header;
            USHORT j;

            printf("    [size %u type %#x flags %#x property %#x thread %s process %s time %s provider %s id %u"
                   " version %u channel %u level %u opcode %u task %u keyword %#llx cpu %s activity %s"
                   " processor %u logger %s ext %u", h->Size, h->HeaderType, h->Flags, h->EventProperty,
                   h->ThreadId == GetCurrentThreadId() ? "ours" : h->ThreadId ? "other" : "0",
                   h->ProcessId == GetCurrentProcessId() ? "ours" : h->ProcessId ? "other" : "0",
                   time_kind(h->TimeStamp.QuadPart), guid_name(&h->ProviderId), h->EventDescriptor.Id,
                   h->EventDescriptor.Version, h->EventDescriptor.Channel, h->EventDescriptor.Level,
                   h->EventDescriptor.Opcode, h->EventDescriptor.Task, h->EventDescriptor.Keyword,
                   h->ProcessorTime ? (h->ProcessorTime > 0xffffffffull ? "a large count" : "small") : "0",
                   IsEqualGUID(&h->ActivityId, &act_guid) ? "ours" : guid_name(&h->ActivityId),
                   recs[i].context.ProcessorNumber, recs[i].context.LoggerId ? "set" : "0", recs[i].ext_count);
            for (j = 0; j < recs[i].ext_count && j < 4; j++)
                printf(" (type %u size %u)", recs[i].ext_types[j], recs[i].ext_sizes[j]);
            printf(" user %lu ", recs[i].user_len);
            if (IsEqualGUID(&h->ProviderId, &event_trace_guid))
                printf("(the header)");
            else
                hex(recs[i].user, min(recs[i].user_len, sizeof(recs[i].user)));
            printf("]\n");
        }
        show_logfile(&logfile);
        printf("  CloseTrace: %lu\n", CloseTrace(consumer));
    }

    memset(&logfile, 0, sizeof(logfile));
    logfile.LogFileName = file;
    logfile.EventCallback = trace_callback;
    consumer = OpenTraceW(&logfile);
    if (consumer != INVALID_PROCESSTRACE_HANDLE)
    {
        ret = ProcessTrace(&consumer, 1, NULL, NULL);
        printf("  as EVENT_TRACE: %lu, %ld records\n", ret, trace_count);
        for (i = 0; i < trace_count && i < ARRAYSIZE(traces); i++)
        {
            EVENT_TRACE_HEADER *h = &traces[i].header;

            printf("    [size %u type %#x marker %#x class type %u level %u version %u thread %s time %s guid %s"
                   " mof %lu client %#lx ", h->Size, h->HeaderType, h->MarkerFlags, h->Class.Type, h->Class.Level,
                   h->Class.Version, h->ThreadId == GetCurrentThreadId() ? "ours" : h->ThreadId ? "other" : "0",
                   time_kind(h->TimeStamp.QuadPart), guid_name(&h->Guid), traces[i].mof_len, traces[i].client);
            if (IsEqualGUID(&h->Guid, &event_trace_guid))
            {
                /* the log file header: everything but the names and time zone */
                printf("header ");
                hex(traces[i].mof, min(traces[i].mof_len, 48));
            }
            else hex(traces[i].mof, min(traces[i].mof_len, sizeof(traces[i].mof)));
            printf("]\n");
        }
        CloseTrace(consumer);
    }
    {
        EVENT_TRACE_LOGFILEW none = { 0 };
        none.LogFileName = (WCHAR *)L"C:\\WineAltarsNoSuchFile.etl";
        none.EventRecordCallback = record_callback;
        none.ProcessTraceMode = PROCESS_TRACE_MODE_EVENT_RECORD;
        SetLastError(0xdeadbeef);
        consumer = OpenTraceW(&none);
        printf("  OpenTrace a file that is not there: %s error %lu\n",
               consumer == INVALID_PROCESSTRACE_HANDLE ? "invalid" : "opened", GetLastError());
        if (consumer != INVALID_PROCESSTRACE_HANDLE) CloseTrace(consumer);
    }
    DeleteFileW(file);
    free(q);
    free(props);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1)
    {
        GUID id;
        EventActivityIdControl(EVENT_ACTIVITY_CTRL_CREATE_ID, &id);
        printf("another process's first activity ID {%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}\n", id.Data1,
               id.Data2, id.Data3, id.Data4[0], id.Data4[1], id.Data4[2], id.Data4[3], id.Data4[4], id.Data4[5],
               id.Data4[6], id.Data4[7]);
        return 0;
    }
    by_name("a private session", PRIVATE_MODE | EVENT_TRACE_BUFFERING_MODE, &ctrl_guid);
    by_name("a real-time session", EVENT_TRACE_REAL_TIME_MODE, NULL);
    activity_ids();
    {
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        WCHAR exe[MAX_PATH], cmdline[MAX_PATH + 16];

        GetModuleFileNameW(NULL, exe, ARRAYSIZE(exe));
        swprintf(cmdline, ARRAYSIZE(cmdline), L"\"%ls\" child", exe);
        if (CreateProcessW(exe, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
        {
            WaitForSingleObject(pi.hProcess, 30000);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
    }
    read_back();
    return 0;
}
