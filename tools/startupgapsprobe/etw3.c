/* etw3: the rest of what an event tracing session of a program's own does -- which properties a private in-process
 * session takes (its GUID, clock, flush timer, how many at once), how EnableTraceEx2 tells a provider (a keyword mask
 * of 0, level 0, capture state, disabling what is not enabled, two registrations of one provider, two sessions), what
 * the records in its log file hold for each way of writing an event (an activity ID, a related one, a string, a
 * TraceLogging event, a classic provider's TraceEvent and TraceMessage, a payload in pieces, one too big for a buffer),
 * the activity ID calls, a real-time consumer's EVENT_RECORD and EVENT_TRACE, waiting for the enable callbacks with a
 * timeout, QueryAllTraces, updating a session, and RegisterEventSource with this computer's own name.  Log files go to
 * the temporary directory and are removed; their headers are not printed (they hold the file's path), and the user's
 * and the computer's names are blanked out of the records before they are.  Nothing is written to the event log.
 * Prints results only. */
#define INITGUID
#include <windows.h>
#include <evntprov.h>
#include <evntrace.h>
#include <evntcons.h>
#include <stdio.h>
#include <wchar.h>

static const GUID null_guid;
DEFINE_GUID(prov_guid,    0x5a1e3c20, 0x7c11, 0x4b2a, 0x9e, 0x51, 0x30, 0x6b, 0x2f, 0x41, 0x8a, 0x77);
DEFINE_GUID(ctrl_guid,    0x5a1e3c22, 0x7c11, 0x4b2a, 0x9e, 0x51, 0x30, 0x6b, 0x2f, 0x41, 0x8a, 0x77);
DEFINE_GUID(ctrl2_guid,   0x5a1e3c29, 0x7c11, 0x4b2a, 0x9e, 0x51, 0x30, 0x6b, 0x2f, 0x41, 0x8a, 0x77);
DEFINE_GUID(classic_guid, 0x5a1e3c23, 0x7c11, 0x4b2a, 0x9e, 0x51, 0x30, 0x6b, 0x2f, 0x41, 0x8a, 0x77);
DEFINE_GUID(class_guid,   0x5a1e3c24, 0x7c11, 0x4b2a, 0x9e, 0x51, 0x30, 0x6b, 0x2f, 0x41, 0x8a, 0x77);
DEFINE_GUID(msg_guid,     0x5a1e3c25, 0x7c11, 0x4b2a, 0x9e, 0x51, 0x30, 0x6b, 0x2f, 0x41, 0x8a, 0x77);
DEFINE_GUID(tl_guid,      0x5a1e3c26, 0x7c11, 0x4b2a, 0x9e, 0x51, 0x30, 0x6b, 0x2f, 0x41, 0x8a, 0x77);
DEFINE_GUID(act_guid,     0x5a1e3c27, 0x7c11, 0x4b2a, 0x9e, 0x51, 0x30, 0x6b, 0x2f, 0x41, 0x8a, 0x77);
DEFINE_GUID(rel_guid,     0x5a1e3c28, 0x7c11, 0x4b2a, 0x9e, 0x51, 0x30, 0x6b, 0x2f, 0x41, 0x8a, 0x77);

#define PRIVATE_MODE (EVENT_TRACE_PRIVATE_LOGGER_MODE | EVENT_TRACE_PRIVATE_IN_PROC)

static WCHAR user_name[256], computer_name[256];

static void blank(BYTE *data, ULONG size, const WCHAR *name)
{
    ULONG len = wcslen(name), i, j;
    char ansi[256];

    if (!len) return;
    WideCharToMultiByte(CP_ACP, 0, name, -1, ansi, sizeof(ansi), NULL, NULL);
    for (i = 0; i + len * 2 <= size; i++)
    {
        for (j = 0; j < len; j++)
            if (towlower(((WCHAR *)(data + i))[j]) != towlower(name[j])) break;
        if (j == len) memset(data + i, '*', len * 2);
    }
    for (i = 0; i + len <= size; i++)
        if (!_strnicmp((char *)data + i, ansi, len)) memset(data + i, '*', len);
}

static void hex(const char *prefix, const BYTE *data, ULONG size)
{
    ULONG i;

    for (i = 0; i < size; i++) printf("%s%02x", i % 32 ? "" : i ? "\n      " : prefix, data[i]);
    printf("\n");
}

/* every enable callback, with whose it was */
static struct
{
    const char *who;
    ULONG is_enabled;
    UCHAR level;
    ULONGLONG any, all;
    BOOL filter;
    DWORD thread;
} cbs[64];
static volatile LONG cb_count;

static void NTAPI enable_callback(const GUID *source, ULONG is_enabled, UCHAR level, ULONGLONG any, ULONGLONG all,
                                  EVENT_FILTER_DESCRIPTOR *filter, void *context)
{
    LONG i = InterlockedIncrement(&cb_count) - 1;

    if (i >= ARRAYSIZE(cbs)) return;
    cbs[i].who = context;
    cbs[i].is_enabled = is_enabled;
    cbs[i].level = level;
    cbs[i].any = any;
    cbs[i].all = all;
    cbs[i].filter = filter != NULL;
    cbs[i].thread = GetCurrentThreadId();
}

static void show_cbs(const char *what, ULONG ret, LONG from)
{
    LONG i, n = cb_count;

    printf("  %s: %lu, %ld callbacks on return", what, ret, n - from);
    for (i = from; i < n && i < ARRAYSIZE(cbs); i++)
        printf(" [%s %lu level %u any %llx all %llx%s%s]", cbs[i].who, cbs[i].is_enabled, cbs[i].level, cbs[i].any,
               cbs[i].all, cbs[i].filter ? " filter" : "", cbs[i].thread == GetCurrentThreadId() ? "" : " elsewhere");
    printf("\n");
}

static EVENT_TRACE_PROPERTIES *new_props(ULONG mode, const GUID *guid, ULONG clock, ULONG flush, const WCHAR *file)
{
    ULONG size = sizeof(EVENT_TRACE_PROPERTIES) + 2 * 1024 * sizeof(WCHAR);
    EVENT_TRACE_PROPERTIES *props = calloc(1, size);

    props->Wnode.BufferSize = size;
    props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    props->Wnode.ClientContext = clock;
    if (guid) props->Wnode.Guid = *guid;
    props->LogFileMode = mode;
    props->FlushTimer = flush;
    props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    if (file)
    {
        props->LogFileNameOffset = sizeof(EVENT_TRACE_PROPERTIES) + 1024 * sizeof(WCHAR);
        wcscpy((WCHAR *)((BYTE *)props + props->LogFileNameOffset), file);
    }
    return props;
}

static void session_name(WCHAR *name, const char *tag)
{
    swprintf(name, 64, L"WineAltarsEtw3-%hs-%lu", tag, GetCurrentProcessId());
}

static ULONG start(TRACEHANDLE *handle, const char *tag, EVENT_TRACE_PROPERTIES *props)
{
    WCHAR name[64];

    session_name(name, tag);
    *handle = 0;
    return StartTraceW(handle, name, props);
}

static ULONG stop(TRACEHANDLE handle)
{
    EVENT_TRACE_PROPERTIES *props = new_props(0, NULL, 0, 0, NULL);
    ULONG ret = ControlTraceW(handle, NULL, props, EVENT_TRACE_CONTROL_STOP);

    free(props);
    return ret;
}

/* what a private in-process session takes */
static void try_private(const char *what, const GUID *guid, ULONG clock, ULONG flush, ULONG name_offset)
{
    EVENT_TRACE_PROPERTIES *props = new_props(PRIVATE_MODE | EVENT_TRACE_BUFFERING_MODE, guid, clock, flush, NULL);
    TRACEHANDLE handle;
    ULONG ret;

    if (name_offset) props->LoggerNameOffset = name_offset;
    ret = start(&handle, "try", props);
    printf("  %s: %lu", what, ret);
    if (!ret) printf(", clock after %lu flush after %lu, stop %lu", props->Wnode.ClientContext, props->FlushTimer,
                     stop(handle));
    printf("\n");
    free(props);
}

static void private_properties(void)
{
    REGHANDLE reg = 0;
    TRACEHANDLE handles[5];
    EVENT_TRACE_PROPERTIES *props[5];
    ULONG ret, i;

    printf("private in-process sessions:\n");
    try_private("an unregistered GUID, the system clock", &ctrl_guid, 2, 0, 0);
    EventRegister(&prov_guid, enable_callback, "reg", &reg);
    try_private("a provider's GUID, registered here", &prov_guid, 2, 0, 0);
    EventUnregister(reg);
    try_private("the same GUID, unregistered again", &prov_guid, 2, 0, 0);
    try_private("GUID_NULL", NULL, 2, 0, 0);
    try_private("the performance counter", &ctrl_guid, 1, 0, 0);
    try_private("the CPU cycle counter", &ctrl_guid, 3, 0, 0);
    try_private("clock 0", &ctrl_guid, 0, 0, 0);
    try_private("clock 4", &ctrl_guid, 4, 0, 0);
    try_private("a flush timer", &ctrl_guid, 2, 1, 0);
    try_private("the name right after the properties", &ctrl_guid, 2, 0, sizeof(EVENT_TRACE_PROPERTIES));
    {
        EVENT_TRACE_PROPERTIES *p = new_props(PRIVATE_MODE | EVENT_TRACE_BUFFERING_MODE, &ctrl_guid, 2, 0, NULL);
        TRACEHANDLE h;

        p->BufferSize = 64;
        p->MinimumBuffers = 2;
        p->MaximumBuffers = 4;
        ret = start(&h, "sizes", p);
        printf("  buffers of 64 KiB, 2 to 4: %lu, buffer %lu min %lu max %lu", ret, p->BufferSize, p->MinimumBuffers,
               p->MaximumBuffers);
        if (!ret) printf(", stop %lu", stop(h));
        printf("\n");
        p->BufferSize = 2048;
        p->MinimumBuffers = 0;
        p->MaximumBuffers = 0;
        ret = start(&h, "sizes", p);
        printf("  buffers of 2 MiB: %lu, buffer %lu min %lu max %lu", ret, p->BufferSize, p->MinimumBuffers,
               p->MaximumBuffers);
        if (!ret) printf(", stop %lu", stop(h));
        printf("\n");
        free(p);
    }

    /* how many at once, and one name twice */
    for (i = 0; i < 5; i++)
    {
        char tag[8];

        sprintf(tag, "n%lu", i);
        props[i] = new_props(PRIVATE_MODE | EVENT_TRACE_BUFFERING_MODE, &ctrl_guid, 2, 0, NULL);
        ret = start(&handles[i], tag, props[i]);
        printf("  session %lu at once: %lu\n", i + 1, ret);
        if (ret) handles[i] = 0;
    }
    {
        EVENT_TRACE_PROPERTIES *p = new_props(PRIVATE_MODE | EVENT_TRACE_BUFFERING_MODE, &ctrl2_guid, 2, 0, NULL);
        TRACEHANDLE h;

        ret = start(&h, "n0", p);
        printf("  the first one's name again: %lu\n", ret);
        if (!ret) stop(h);
        free(p);
    }
    for (i = 0; i < 5; i++)
    {
        if (handles[i]) stop(handles[i]);
        free(props[i]);
    }
}

static void enable_semantics(void)
{
    EVENT_TRACE_PROPERTIES *props = new_props(PRIVATE_MODE | EVENT_TRACE_BUFFERING_MODE, &ctrl_guid, 2, 0, NULL);
    EVENT_TRACE_PROPERTIES *props2 = new_props(PRIVATE_MODE | EVENT_TRACE_BUFFERING_MODE, &ctrl2_guid, 2, 0, NULL);
    ENABLE_TRACE_PARAMETERS params = { ENABLE_TRACE_PARAMETERS_VERSION_2 };
    TRACEHANDLE s, t;
    REGHANDLE reg1 = 0, reg2 = 0;
    ULONG ret;
    LONG from;

    printf("enabling a provider for a private session:\n");
    if ((ret = start(&s, "en", props)))
    {
        printf("  StartTrace %lu\n", ret);
        return;
    }
    EventRegister(&prov_guid, enable_callback, "first", &reg1);
    EventRegister(&prov_guid, enable_callback, "second", &reg2);

    from = cb_count;
    ret = EnableTraceEx2(s, &prov_guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 4, 0, 0, 0, NULL);
    show_cbs("level 4, keywords 0", ret, from);
    printf("    enabled at 4/80 %d, 5/0 %d, 4/0 %d, 0/1 %d; EventEnabled 4/80 %d\n", EventProviderEnabled(reg1, 4, 0x80),
           EventProviderEnabled(reg1, 5, 0), EventProviderEnabled(reg1, 4, 0), EventProviderEnabled(reg1, 0, 1),
           EventEnabled(reg1, &(EVENT_DESCRIPTOR){ 1, 0, 0, 4, 0, 0, 0x80 }));
    from = cb_count;
    ret = EnableTraceEx2(s, &prov_guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 0, 0x10, 0, 0, NULL);
    show_cbs("level 0, any 10", ret, from);
    printf("    enabled at 255/10 %d, 1/20 %d, 1/0 %d\n", EventProviderEnabled(reg1, 255, 0x10),
           EventProviderEnabled(reg1, 1, 0x20), EventProviderEnabled(reg1, 1, 0));
    from = cb_count;
    ret = EnableTraceEx2(s, &prov_guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 3, 0x30, 0x10, 0, NULL);
    show_cbs("level 3, any 30 all 10", ret, from);
    printf("    enabled at 3/20 %d, 3/10 %d, 3/30 %d\n", EventProviderEnabled(reg1, 3, 0x20),
           EventProviderEnabled(reg1, 3, 0x10), EventProviderEnabled(reg1, 3, 0x30));
    from = cb_count;
    ret = EnableTraceEx2(s, &prov_guid, EVENT_CONTROL_CODE_CAPTURE_STATE, 2, 0x40, 0, 0, NULL);
    show_cbs("capture state, level 2 any 40", ret, from);
    printf("    enabled at 3/20 %d, 2/40 %d\n", EventProviderEnabled(reg1, 3, 0x20), EventProviderEnabled(reg1, 2, 0x40));
    from = cb_count;
    ret = EnableTraceEx2(s, &prov_guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 5, 0, 0, 0, &params);
    show_cbs("with parameters, version 2", ret, from);
    params.Version = 1;
    from = cb_count;
    ret = EnableTraceEx2(s, &prov_guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 5, 0, 0, 0, &params);
    show_cbs("with parameters, version 1", ret, from);
    params.Version = 0;
    from = cb_count;
    ret = EnableTraceEx2(s, &prov_guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 5, 0, 0, 0, &params);
    show_cbs("with parameters, version 0", ret, from);
    params.Version = 3;
    from = cb_count;
    ret = EnableTraceEx2(s, &prov_guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 5, 0, 0, 0, &params);
    show_cbs("with parameters, version 3", ret, from);

    if (!(ret = start(&t, "en2", props2)))
    {
        from = cb_count;
        ret = EnableTraceEx2(t, &prov_guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 2, 0x1, 0, 0, NULL);
        show_cbs("a second session, level 2 any 1", ret, from);
        printf("    enabled at 5/2 %d, 2/1 %d, 3/1 %d\n", EventProviderEnabled(reg1, 5, 0x2),
               EventProviderEnabled(reg1, 2, 0x1), EventProviderEnabled(reg1, 3, 0x1));
        from = cb_count;
        ret = stop(t);
        show_cbs("stopping the second session", ret, from);
        printf("    enabled at 5/2 %d\n", EventProviderEnabled(reg1, 5, 0x2));
    }
    else printf("  a second session: StartTrace %lu\n", ret);

    from = cb_count;
    ret = EnableTraceEx2(s, &prov_guid, EVENT_CONTROL_CODE_DISABLE_PROVIDER, 0, 0, 0, 0, NULL);
    show_cbs("disabling", ret, from);
    printf("    enabled at 5/2 %d\n", EventProviderEnabled(reg1, 5, 0x2));
    from = cb_count;
    ret = EnableTraceEx2(s, &prov_guid, EVENT_CONTROL_CODE_DISABLE_PROVIDER, 0, 0, 0, 0, NULL);
    show_cbs("disabling again", ret, from);
    from = cb_count;
    ret = EnableTraceEx2(s, &prov_guid, EVENT_CONTROL_CODE_CAPTURE_STATE, 4, 1, 0, 0, NULL);
    show_cbs("capture state while disabled", ret, from);
    printf("    enabled at 4/1 %d\n", EventProviderEnabled(reg1, 4, 1));
    ret = EnableTraceEx2(s, NULL, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 4, 1, 0, 0, NULL);
    printf("  no provider: %lu\n", ret);
    ret = EnableTraceEx2(s, &null_guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 4, 1, 0, 0, NULL);
    printf("  GUID_NULL: %lu\n", ret);
    ret = EnableTraceEx2(0, &prov_guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 4, 1, 0, 0, NULL);
    printf("  handle 0: %lu\n", ret);

    {
        EVENT_TRACE_PROPERTIES *q = new_props(0, NULL, 0, 0, NULL);
        WCHAR name[64];

        session_name(name, "en");
        ret = QueryTraceW(s, NULL, q);
        printf("  QueryTraceW: %lu, mode %#lx logger thread %s\n", ret, q->LogFileMode, q->LoggerThreadId ? "set" : "0");
        q->FlushTimer = 3;
        q->EnableFlags = 0x10;
        ret = UpdateTraceW(s, NULL, q);
        printf("  UpdateTraceW a flush timer and enable flags: %lu, flush %lu flags %#lx\n", ret, q->FlushTimer,
               q->EnableFlags);
        ret = FlushTraceW(s, NULL, q);
        printf("  FlushTraceW: %lu\n", ret);
        memset(q, 0, sizeof(*q));
        ret = ControlTraceW(s, NULL, q, EVENT_TRACE_CONTROL_QUERY);
        printf("  query with a zero size: %lu\n", ret);
        ret = ControlTraceW(s, NULL, NULL, EVENT_TRACE_CONTROL_QUERY);
        printf("  query without properties: %lu\n", ret);
        free(q);
        q = new_props(0, NULL, 0, 0, NULL);
        ret = ControlTraceW(0, L"WineAltarsNoSuchSession", q, EVENT_TRACE_CONTROL_QUERY);
        printf("  query a name nobody uses: %lu\n", ret);
        ret = ControlTraceW(s, NULL, q, 9);
        printf("  control code 9: %lu\n", ret);
        ret = ControlTraceW(s, NULL, q, EVENT_TRACE_CONTROL_INCREMENT_FILE);
        printf("  increment file: %lu\n", ret);
        ret = StopTraceW(0, name, q);
        printf("  StopTraceW by name: %lu, buffers written %lu\n", ret, q->BuffersWritten);
        free(q);
    }
    from = cb_count;
    EventUnregister(reg1);
    EventUnregister(reg2);
    show_cbs("unregistering", 0, from);
    free(props);
    free(props2);
}

/* the activity ID calls, by shape: whether a GUID is zero and of which version and variant */
static const char *shape(const GUID *guid)
{
    static char buf[4][48];
    static int n;
    char *ret = buf[n++ % 4];

    if (IsEqualGUID(guid, &null_guid)) return "zero";
    sprintf(ret, "version %x variant %x", guid->Data3 >> 12, guid->Data4[0] >> 5);
    return ret;
}

static void activity_ids(void)
{
    GUID guid, first, second;
    GUID *teb = (GUID *)((BYTE *)NtCurrentTeb() + 0x1710);
    ULONG ret;

    printf("activity IDs:\n");
    memset(&guid, 0xcc, sizeof(guid));
    ret = EventActivityIdControl(EVENT_ACTIVITY_CTRL_GET_ID, &guid);
    printf("  get: %lu %s, the TEB's %s\n", ret, shape(&guid), shape(teb));
    memset(&first, 0, sizeof(first));
    ret = EventActivityIdControl(EVENT_ACTIVITY_CTRL_CREATE_ID, &first);
    memset(&second, 0, sizeof(second));
    EventActivityIdControl(EVENT_ACTIVITY_CTRL_CREATE_ID, &second);
    printf("  create: %lu %s; the next %s, Data1 %s, the rest %s; the thread's still %s\n", ret, shape(&first),
           shape(&second), first.Data1 == second.Data1 ? "the same" : "differs",
           memcmp(&first.Data2, &second.Data2, 12) ? "differs" : "the same", shape(teb));
    {
        ULONG diff = 0, i;
        for (i = 0; i < 16; i++) if (((BYTE *)&first)[i] != ((BYTE *)&second)[i]) diff |= 1 << i;
        printf("    the bytes that differ: %#lx\n", diff);
    }
    guid = act_guid;
    ret = EventActivityIdControl(EVENT_ACTIVITY_CTRL_SET_ID, &guid);
    printf("  set: %lu, the TEB's %s\n", ret, IsEqualGUID(teb, &act_guid) ? "ours" : shape(teb));
    guid = rel_guid;
    ret = EventActivityIdControl(EVENT_ACTIVITY_CTRL_GET_SET_ID, &guid);
    printf("  get and set: %lu, got %s, now %s\n", ret, IsEqualGUID(&guid, &act_guid) ? "the old one" : shape(&guid),
           IsEqualGUID(teb, &rel_guid) ? "the new one" : shape(teb));
    memset(&guid, 0, sizeof(guid));
    ret = EventActivityIdControl(EVENT_ACTIVITY_CTRL_CREATE_SET_ID, &guid);
    printf("  create and set: %lu, got %s, now %s\n", ret, IsEqualGUID(&guid, &rel_guid) ? "the old one" : shape(&guid),
           shape(teb));
    ret = EventActivityIdControl(0, &guid);
    printf("  code 0: %lu\n", ret);
    ret = EventActivityIdControl(6, &guid);
    printf("  code 6: %lu\n", ret);
    ret = EventActivityIdControl(EVENT_ACTIVITY_CTRL_GET_ID, NULL);
    printf("  get into NULL: %lu\n", ret);
    memset(&guid, 0, sizeof(guid));
    ret = EventActivityIdControl(EVENT_ACTIVITY_CTRL_SET_ID, &guid);
    printf("  set zero: %lu, the TEB's %s\n", ret, shape(teb));
}

/* a classic provider */
static struct
{
    WMIDPREQUESTCODE code;
    ULONG size, wnode_size, flags;
    ULONG64 context;
    BOOL guid_ours;
    TRACEHANDLE logger;
    UCHAR level;
    ULONG enable_flags, error;
    BOOL same_thread;
} wmi[16];
static volatile LONG wmi_count;
static TRACEHANDLE classic_logger;

static ULONG WINAPI classic_callback(WMIDPREQUESTCODE code, void *context, ULONG *size, void *buffer)
{
    LONG i = InterlockedIncrement(&wmi_count) - 1;
    WNODE_HEADER *wnode = buffer;

    if (i >= ARRAYSIZE(wmi)) return ERROR_SUCCESS;
    wmi[i].code = code;
    wmi[i].size = size ? *size : ~0u;
    wmi[i].same_thread = TRUE;
    if (wnode)
    {
        wmi[i].wnode_size = wnode->BufferSize;
        wmi[i].flags = wnode->Flags;
        wmi[i].context = wnode->HistoricalContext;
        wmi[i].guid_ours = IsEqualGUID(&wnode->Guid, &classic_guid);
    }
    if (code == WMI_ENABLE_EVENTS)
    {
        SetLastError(0xdeadbeef);
        wmi[i].logger = classic_logger = GetTraceLoggerHandle(buffer);
        wmi[i].level = GetTraceEnableLevel(classic_logger);
        wmi[i].enable_flags = GetTraceEnableFlags(classic_logger);
        wmi[i].error = GetLastError();
    }
    return ERROR_SUCCESS;
}

static void show_wmi(const char *what, ULONG ret, LONG from, TRACEHANDLE session)
{
    LONG i, n = wmi_count;

    printf("  %s: %lu, %ld calls", what, ret, n - from);
    for (i = from; i < n && i < ARRAYSIZE(wmi); i++)
    {
        printf(" [code %u size %lu wnode %lu flags %#lx context %#llx guid %s", wmi[i].code, wmi[i].size,
               wmi[i].wnode_size, wmi[i].flags, wmi[i].context, wmi[i].guid_ours ? "ours" : "other");
        if (wmi[i].code == WMI_ENABLE_EVENTS)
            printf(" logger %#llx (the session's %#llx) level %u flags %#lx error %lu", wmi[i].logger, session,
                   wmi[i].level, wmi[i].enable_flags, wmi[i].error);
        printf("]");
    }
    printf("\n");
}

static void file_records(const char *what, const WCHAR *file)
{
    HANDLE f = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, 0, NULL);
    DWORD size, got = 0, pos, buffer_size;
    BYTE *data;

    if (f == INVALID_HANDLE_VALUE)
    {
        printf("  %s: no file, error %lu\n", what, GetLastError());
        return;
    }
    size = GetFileSize(f, NULL);
    data = malloc(size);
    ReadFile(f, data, size, &got, NULL);
    CloseHandle(f);
    buffer_size = *(ULONG *)data;
    printf("  %s: %lu bytes, buffers of %lu\n", what, size, buffer_size);
    blank(data, got, user_name);
    blank(data, got, computer_name);
    printf("    the header buffer's own header:");
    hex(" ", data, 0x48);
    for (pos = buffer_size; buffer_size && pos + buffer_size <= got; pos += buffer_size)
    {
        ULONG used = *(ULONG *)(data + pos + 4);

        printf("    buffer at %#lx, %lu used:\n", pos, used);
        hex("      ", data + pos, min(used, 0x900));
    }
    free(data);
}

static void write_tl_event(REGHANDLE reg, const BYTE *traits, ULONG traits_size)
{
    static const BYTE meta[] =
    {
        0, 0,        /* the size, below */
        0,           /* tags */
        'T','l','E','v','e','n','t',0,
        'V','a','l','u','e',0, 8 /* UINT32 */,
        'T','e','x','t',0, 2 /* ANSI string */,
    };
    EVENT_DESCRIPTOR desc = { 0, 0, 11, 4, 0, 0, 0x2 };
    EVENT_DATA_DESCRIPTOR data[4];
    BYTE meta_copy[sizeof(meta)];
    ULONG value = 0x1234;
    char text[] = "tl";
    ULONG ret;

    memcpy(meta_copy, meta, sizeof(meta));
    *(USHORT *)meta_copy = sizeof(meta);
    EventDataDescCreate(&data[0], traits, traits_size);
    data[0].Reserved = 2;
    EventDataDescCreate(&data[1], meta_copy, sizeof(meta_copy));
    data[1].Reserved = 1;
    EventDataDescCreate(&data[2], &value, sizeof(value));
    EventDataDescCreate(&data[3], text, sizeof(text));
    ret = EventWriteTransfer(reg, &desc, NULL, NULL, 4, data);
    printf("  a TraceLogging event: %lu\n", ret);
}

static void records(void)
{
    WCHAR temp[MAX_PATH], file1[MAX_PATH], file2[MAX_PATH];
    EVENT_TRACE_PROPERTIES *p1, *p2, *q;
    TRACEHANDLE s1, s2;
    REGHANDLE reg = 0, tl = 0;
    TRACEHANDLE classic = 0;
    TRACE_GUID_REGISTRATION classes[1] = { { &class_guid } };
    BYTE traits[] = { 0, 0, 'W','i','n','e','A','l','t','a','r','s','T','l',0 };
    EVENT_DATA_DESCRIPTOR data[3];
    char text[] = "event";
    ULONG ret, value = 0x11223344;
    LONG from;

    printf("records in a private session's file:\n");
    GetTempPathW(ARRAYSIZE(temp), temp);
    swprintf(file1, ARRAYSIZE(file1), L"%lsWineAltarsEtw3a-%lu.etl", temp, GetCurrentProcessId());
    swprintf(file2, ARRAYSIZE(file2), L"%lsWineAltarsEtw3b-%lu.etl", temp, GetCurrentProcessId());
    p1 = new_props(PRIVATE_MODE | EVENT_TRACE_FILE_MODE_SEQUENTIAL, &ctrl_guid, 2, 0, file1);
    p2 = new_props(PRIVATE_MODE | EVENT_TRACE_FILE_MODE_SEQUENTIAL, &ctrl2_guid, 2, 0, file2);
    if ((ret = start(&s1, "rec1", p1)) || (ret = start(&s2, "rec2", p2)))
    {
        printf("  StartTrace %lu\n", ret);
        return;
    }
    EventRegister(&prov_guid, enable_callback, "records", &reg);
    EnableTraceEx2(s1, &prov_guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 5, 0, 0, 0, NULL);
    EnableTraceEx2(s2, &prov_guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 2, 0, 0, 0, NULL);

    {
        GUID zero = { 0 };
        EventActivityIdControl(EVENT_ACTIVITY_CTRL_SET_ID, &zero);
    }
    EventDataDescCreate(&data[0], text, sizeof(text));
    ret = EventWrite(reg, &(EVENT_DESCRIPTOR){ 1, 1, 0, 4, 2, 3, 0x1 }, 1, data);
    printf("  EventWrite id 1 level 4, no activity: %lu\n", ret);
    {
        GUID guid = act_guid;
        EventActivityIdControl(EVENT_ACTIVITY_CTRL_SET_ID, &guid);
    }
    ret = EventWrite(reg, &(EVENT_DESCRIPTOR){ 2, 0, 0, 2, 0, 0, 0x1 }, 1, data);
    printf("  EventWrite id 2 level 2, the thread's activity set: %lu\n", ret);
    ret = EventWriteTransfer(reg, &(EVENT_DESCRIPTOR){ 3, 0, 0, 4, 0, 0, 0x1 }, &rel_guid, &act_guid, 1, data);
    printf("  EventWriteTransfer id 3, an activity and a related one: %lu\n", ret);
    ret = EventWriteString(reg, 4, 0x1, L"a string");
    printf("  EventWriteString: %lu\n", ret);
    ret = EventWriteEx(reg, &(EVENT_DESCRIPTOR){ 5, 0, 0, 4, 0, 0, 0x1 }, 0, 0, NULL, &rel_guid, 1, data);
    printf("  EventWriteEx id 5, a related activity: %lu\n", ret);
    EventDataDescCreate(&data[0], &value, sizeof(value));
    EventDataDescCreate(&data[1], NULL, 0);
    EventDataDescCreate(&data[2], text, 3);
    ret = EventWrite(reg, &(EVENT_DESCRIPTOR){ 6, 0, 0, 4, 0, 0, 0x1 }, 3, data);
    printf("  EventWrite id 6, three pieces, one empty: %lu\n", ret);
    {
        static BYTE big[5000];
        EventDataDescCreate(&data[0], big, sizeof(big));
        ret = EventWrite(reg, &(EVENT_DESCRIPTOR){ 7, 0, 0, 4, 0, 0, 0x1 }, 1, data);
        printf("  EventWrite id 7, 5000 bytes: %lu\n", ret);
        EventDataDescCreate(&data[0], big, 4000);
        ret = EventWrite(reg, &(EVENT_DESCRIPTOR){ 8, 0, 0, 4, 0, 0, 0x1 }, 1, data);
        printf("  EventWrite id 8, 4000 bytes: %lu\n", ret);
    }

    ret = EventRegister(&tl_guid, enable_callback, "tl", &tl);
    ret = EventSetInformation(tl, (EVENT_INFO_CLASS)2 /* EventProviderSetTraits */, traits, sizeof(traits));
    *(USHORT *)traits = sizeof(traits);
    ret = EventSetInformation(tl, (EVENT_INFO_CLASS)2, traits, sizeof(traits));
    printf("  TraceLogging provider traits: %lu\n", ret);
    EnableTraceEx2(s1, &tl_guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 5, 0, 0, 0, NULL);
    write_tl_event(tl, traits, sizeof(traits));

    ret = RegisterTraceGuidsW(classic_callback, (void *)"classic", &classic_guid, 1, classes, NULL, NULL, &classic);
    printf("  RegisterTraceGuids: %lu, the class's handle %s\n", ret, classes[0].RegHandle ? "set" : "0");
    from = wmi_count;
    ret = EnableTraceEx2(s1, &classic_guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 3, 0xabcd1234ull | (0x5ull << 32), 0, 0,
                         NULL);
    show_wmi("EnableTraceEx2 a classic provider, level 3 any 5abcd1234", ret, from, s1);
    {
        struct { EVENT_TRACE_HEADER header; ULONG data[2]; } ev;
        struct { EVENT_TRACE_HEADER header; MOF_FIELD mof[2]; } ev2;
        ULONG mof_data[2] = { 0x55667788, 0x99aabbcc };
        short word = 0x4242;

        memset(&ev, 0, sizeof(ev));
        ev.header.Size = sizeof(ev);
        ev.header.Flags = WNODE_FLAG_TRACED_GUID;
        ev.header.Guid = class_guid;
        ev.header.Class.Type = 1;
        ev.header.Class.Level = 3;
        ev.header.Class.Version = 2;
        ev.data[0] = 0xa1a2a3a4;
        ev.data[1] = 0xb1b2b3b4;
        ret = TraceEvent(classic_logger, &ev.header);
        printf("  TraceEvent: %lu\n", ret);
        memset(&ev2, 0, sizeof(ev2));
        ev2.header.Size = sizeof(ev2);
        ev2.header.Flags = WNODE_FLAG_TRACED_GUID | WNODE_FLAG_USE_MOF_PTR;
        ev2.header.Guid = class_guid;
        ev2.header.Class.Type = 2;
        ev2.mof[0].DataPtr = (ULONG64)(ULONG_PTR)mof_data;
        ev2.mof[0].Length = sizeof(mof_data);
        ev2.mof[1].DataPtr = (ULONG64)(ULONG_PTR)&word;
        ev2.mof[1].Length = sizeof(word);
        ret = TraceEvent(classic_logger, &ev2.header);
        printf("  TraceEvent with MOF pointers: %lu\n", ret);
        ev.header.Flags = WNODE_FLAG_TRACED_GUID | WNODE_FLAG_USE_GUID_PTR;
        ev.header.GuidPtr = (ULONG64)(ULONG_PTR)&class_guid;
        ev.header.Class.Type = 3;
        ret = TraceEvent(classic_logger, &ev.header);
        printf("  TraceEvent with a GUID pointer: %lu\n", ret);
        ev.header.Flags = 0;
        ret = TraceEvent(classic_logger, &ev.header);
        printf("  TraceEvent without the traced GUID flag: %lu\n", ret);
        ev.header.Flags = WNODE_FLAG_TRACED_GUID;
        ev.header.Size = 4;
        ret = TraceEvent(classic_logger, &ev.header);
        printf("  TraceEvent of size 4: %lu\n", ret);
        ev.header.Size = sizeof(ev);
        ret = TraceEvent(0x12345, &ev.header);
        printf("  TraceEvent to a bad logger: %lu\n", ret);
        ret = TraceEvent(s1, &ev.header);
        printf("  TraceEvent to the session's handle: %lu\n", ret);
        ret = TraceMessage(classic_logger, TRACE_MESSAGE_SEQUENCE | TRACE_MESSAGE_GUID | TRACE_MESSAGE_TIMESTAMP |
                           TRACE_MESSAGE_SYSTEMINFO, &msg_guid, 7, &value, sizeof(value), NULL);
        printf("  TraceMessage: %lu\n", ret);
        ret = TraceMessage(classic_logger, TRACE_MESSAGE_GUID, &msg_guid, 8, NULL);
        printf("  TraceMessage without arguments: %lu\n", ret);
        SetLastError(0xdeadbeef);
        printf("  the logger's flags %#lx level %u error %lu; a bad handle's flags %#lx", GetTraceEnableFlags(classic_logger),
               GetTraceEnableLevel(classic_logger), GetLastError(), (SetLastError(0xdeadbeef), GetTraceEnableFlags(0x12345)));
        printf(" error %lu\n", GetLastError());
    }
    from = wmi_count;
    ret = EnableTraceEx2(s1, &classic_guid, EVENT_CONTROL_CODE_DISABLE_PROVIDER, 0, 0, 0, 0, NULL);
    show_wmi("disabling it", ret, from, s1);
    from = wmi_count;
    ret = EnableTrace(TRUE, 0x77, 4, &classic_guid, s1);
    show_wmi("EnableTrace flags 77 level 4", ret, from, s1);
    from = wmi_count;
    ret = EnableTraceEx(&classic_guid, NULL, s1, TRUE, 5, 0x88, 0, 0, NULL);
    show_wmi("EnableTraceEx level 5 any 88", ret, from, s1);

    {
        static const struct { ULONG name, file; } offsets[] =
        {
            { 0, 0 }, { sizeof(EVENT_TRACE_PROPERTIES), 0 }, { 0, sizeof(EVENT_TRACE_PROPERTIES) },
            { sizeof(EVENT_TRACE_PROPERTIES), sizeof(EVENT_TRACE_PROPERTIES) + 1024 },
            { sizeof(EVENT_TRACE_PROPERTIES) + 1024, sizeof(EVENT_TRACE_PROPERTIES) },
        };
        WCHAR name[64];
        ULONG k;

        session_name(name, "rec1");
        for (k = 0; k < ARRAYSIZE(offsets); k++)
        {
            ULONG size = sizeof(EVENT_TRACE_PROPERTIES) + 2048;
            EVENT_TRACE_PROPERTIES *o = calloc(1, size);
            BYTE *b = (BYTE *)o;

            o->Wnode.BufferSize = size;
            o->LoggerNameOffset = offsets[k].name;
            o->LogFileNameOffset = offsets[k].file;
            ret = ControlTraceW(s1, NULL, o, EVENT_TRACE_CONTROL_QUERY);
            printf("  query with name offset %lu, file offset %lu: %lu, offsets after %lu %lu, name %s, file %s\n",
                   offsets[k].name, offsets[k].file, ret, o->LoggerNameOffset, o->LogFileNameOffset,
                   o->LoggerNameOffset && o->LoggerNameOffset < size && !wcscmp((WCHAR *)(b + o->LoggerNameOffset), name) ? "ours" : "not there",
                   o->LogFileNameOffset && o->LogFileNameOffset < size && !wcscmp((WCHAR *)(b + o->LogFileNameOffset), file1) ? "ours" : "not there");
            free(o);
        }
        {
            ULONG size = sizeof(EVENT_TRACE_PROPERTIES) + 40;
            EVENT_TRACE_PROPERTIES *o = calloc(1, size);

            o->Wnode.BufferSize = size;
            ret = ControlTraceW(s1, NULL, o, EVENT_TRACE_CONTROL_QUERY);
            printf("  query with room for 20 characters: %lu, offsets after %lu %lu, the size %lu\n", ret,
                   o->LoggerNameOffset, o->LogFileNameOffset, o->Wnode.BufferSize);
            free(o);
        }
    }
    q = new_props(0, NULL, 0, 0, NULL);
    ret = ControlTraceW(s1, NULL, q, EVENT_TRACE_CONTROL_QUERY);
    printf("  query: %lu, events lost %lu, buffers written %lu, free %lu, logger thread %s\n", ret, q->EventsLost,
           q->BuffersWritten, q->FreeBuffers, q->LoggerThreadId ? "set" : "0");
    from = wmi_count;
    ret = ControlTraceW(s1, NULL, q, EVENT_TRACE_CONTROL_STOP);
    show_wmi("stopping", ret, from, s1);
    printf("    events lost %lu, buffers written %lu\n", q->EventsLost, q->BuffersWritten);
    ControlTraceW(s2, NULL, q, EVENT_TRACE_CONTROL_STOP);
    printf("  the second session: buffers written %lu\n", q->BuffersWritten);
    UnregisterTraceGuids(classic);
    EventUnregister(tl);
    EventUnregister(reg);
    {
        GUID zero = { 0 };
        EventActivityIdControl(EVENT_ACTIVITY_CTRL_SET_ID, &zero);
    }
    file_records("the first file", file1);
    file_records("the second file", file2);
    DeleteFileW(file1);
    DeleteFileW(file2);
    free(q);
    free(p1);
    free(p2);
}

/* a real-time consumer */
static struct
{
    EVENT_HEADER header;
    ETW_BUFFER_CONTEXT context;
    USHORT ext_count, ext_types[4], ext_sizes[4];
    ULONG user_len;
    BYTE user[16];
    void *user_context;
    LARGE_INTEGER system_time, qpc;
} recs[32];
static volatile LONG rec_count;

static void WINAPI record_callback(EVENT_RECORD *record)
{
    LONG i = InterlockedIncrement(&rec_count) - 1;
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
    recs[i].user_context = record->UserContext;
    GetSystemTimeAsFileTime((FILETIME *)&recs[i].system_time);
    QueryPerformanceCounter(&recs[i].qpc);
}

static struct
{
    EVENT_TRACE_HEADER header;
    ULONG instance, parent;
    BOOL parent_guid_zero;
    ULONG mof_len, client;
} classic_recs[32];
static volatile LONG classic_count;

static void WINAPI classic_record_callback(EVENT_TRACE *event)
{
    LONG i = InterlockedIncrement(&classic_count) - 1;

    if (i >= ARRAYSIZE(classic_recs)) return;
    classic_recs[i].header = event->Header;
    classic_recs[i].instance = event->InstanceId;
    classic_recs[i].parent = event->ParentInstanceId;
    classic_recs[i].parent_guid_zero = IsEqualGUID(&event->ParentGuid, &null_guid);
    classic_recs[i].mof_len = event->MofLength;
    classic_recs[i].client = event->ClientContext;
}

static volatile LONG buffer_calls;

static ULONG WINAPI buffer_callback(EVENT_TRACE_LOGFILEW *logfile)
{
    InterlockedIncrement(&buffer_calls);
    return TRUE;
}

static TRACEHANDLE consumers[2];
static ULONG process_status[2] = { 0xdead, 0xdead };

static DWORD WINAPI process_thread(void *arg)
{
    int i = (int)(ULONG_PTR)arg;
    process_status[i] = ProcessTrace(&consumers[i], 1, NULL, NULL);
    return 0;
}

static void show_logfile(const char *what, const EVENT_TRACE_LOGFILEW *logfile)
{
    const TRACE_LOGFILE_HEADER *h = &logfile->LogfileHeader;

    printf("    %s: buffers read %lu, buffer size %lu, filled %lu, events lost %lu, mode %#lx, current time %s;"
           " header: buffer %lu version %#lx provider version %lu processors %lu end %s resolution %lu max file %lu"
           " mode %#lx buffers written %lu pointer %lu lost %lu cpu %s start %s reserved %lu perf freq %s boot %s"
           " buffers lost %lu\n", what, logfile->BuffersRead, logfile->BufferSize, logfile->Filled, logfile->EventsLost,
           logfile->LogFileMode, logfile->CurrentTime ? "set" : "0", h->BufferSize, h->Version, h->ProviderVersion,
           h->NumberOfProcessors, h->EndTime.QuadPart ? "set" : "0", h->TimerResolution, h->MaximumFileSize,
           h->LogFileMode, h->BuffersWritten, h->PointerSize, h->EventsLost, h->CpuSpeedInMHz ? "set" : "0",
           h->StartTime.QuadPart ? "set" : "0", h->ReservedFlags, h->PerfFreq.QuadPart ? "set" : "0",
           h->BootTime.QuadPart ? "set" : "0", h->BuffersLost);
}

static void real_time(void)
{
    EVENT_TRACE_PROPERTIES *props = new_props(EVENT_TRACE_REAL_TIME_MODE, NULL, 0, 1, NULL), *q;
    EVENT_TRACE_LOGFILEW logfile, logfile2;
    WCHAR name[64];
    TRACEHANDLE s, classic = 0;
    TRACE_GUID_REGISTRATION classes[1] = { { &class_guid } };
    REGHANDLE reg = 0;
    HANDLE threads[2] = { 0 };
    LARGE_INTEGER freq, now_qpc, now;
    ULONG ret, i;
    LONG from;

    printf("a real-time session:\n");
    session_name(name, "rt");
    ret = start(&s, "rt", props);
    printf("  StartTrace: %lu, mode after %#lx, clock %lu, buffer %lu min %lu max %lu\n", ret, props->LogFileMode,
           props->Wnode.ClientContext, props->BufferSize, props->MinimumBuffers, props->MaximumBuffers);
    if (ret) return;

    memset(&logfile, 0, sizeof(logfile));
    logfile.LoggerName = name;
    logfile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    logfile.EventRecordCallback = record_callback;
    logfile.BufferCallback = buffer_callback;
    logfile.Context = (void *)0x1234;
    consumers[0] = OpenTraceW(&logfile);
    printf("  OpenTrace: %s, the log file name %s, mode %#lx\n",
           consumers[0] == INVALID_PROCESSTRACE_HANDLE ? "invalid" : "opened",
           logfile.LogFileName ? "set" : "NULL", logfile.LogFileMode);
    show_logfile("after OpenTrace", &logfile);
    memset(&logfile2, 0, sizeof(logfile2));
    logfile2.LoggerName = name;
    logfile2.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME;
    logfile2.EventCallback = classic_record_callback;
    consumers[1] = OpenTraceW(&logfile2);
    printf("  a second consumer, with EVENT_TRACE: %s\n",
           consumers[1] == INVALID_PROCESSTRACE_HANDLE ? "invalid" : "opened");
    for (i = 0; i < 2; i++)
        if (consumers[i] != INVALID_PROCESSTRACE_HANDLE)
            threads[i] = CreateThread(NULL, 0, process_thread, (void *)(ULONG_PTR)i, 0, NULL);

    EventRegister(&prov_guid, enable_callback, "rt", &reg);
    from = cb_count;
    ret = EnableTraceEx2(s, &prov_guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 4, 0x1, 0, 0, NULL);
    show_cbs("EnableTraceEx2 without a timeout", ret, from);
    printf("    enabled at 4/1 %d\n", EventProviderEnabled(reg, 4, 1));
    for (i = 0; i < 50 && cb_count == from; i++) Sleep(20);
    from = cb_count;
    ret = EnableTraceEx2(s, &prov_guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 5, 0x3, 0, 5000, NULL);
    show_cbs("EnableTraceEx2 with a timeout of 5 s", ret, from);
    printf("    enabled at 5/2 %d\n", EventProviderEnabled(reg, 5, 2));
    for (i = 0; i < 50 && cb_count == from; i++) Sleep(20);

    {
        EVENT_DATA_DESCRIPTOR data;
        char text[] = "rt event";
        GUID guid = act_guid;

        EventActivityIdControl(EVENT_ACTIVITY_CTRL_SET_ID, &guid);
        EventDataDescCreate(&data, text, sizeof(text));
        ret = EventWrite(reg, &(EVENT_DESCRIPTOR){ 1, 2, 3, 4, 5, 6, 0x1 }, 1, &data);
        printf("  EventWrite: %lu\n", ret);
        ret = EventWriteString(reg, 4, 0x1, L"rt string");
        printf("  EventWriteString: %lu\n", ret);
        ret = EventWriteTransfer(reg, &(EVENT_DESCRIPTOR){ 3, 0, 0, 4, 0, 0, 0x1 }, NULL, &rel_guid, 1, &data);
        printf("  EventWriteTransfer with a related activity: %lu\n", ret);
        memset(&guid, 0, sizeof(guid));
        EventActivityIdControl(EVENT_ACTIVITY_CTRL_SET_ID, &guid);
    }
    ret = RegisterTraceGuidsW(classic_callback, NULL, &classic_guid, 1, classes, NULL, NULL, &classic);
    from = wmi_count;
    ret = EnableTraceEx2(s, &classic_guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 3, 0x7, 0, 5000, NULL);
    show_wmi("EnableTraceEx2 a classic provider with a timeout", ret, from, s);
    {
        struct { EVENT_TRACE_HEADER header; ULONG data[2]; } ev;

        memset(&ev, 0, sizeof(ev));
        ev.header.Size = sizeof(ev);
        ev.header.Flags = WNODE_FLAG_TRACED_GUID;
        ev.header.Guid = class_guid;
        ev.header.Class.Type = 9;
        ev.header.Class.Level = 3;
        ev.data[0] = 0x1234;
        ret = TraceEvent(classic_logger, &ev.header);
        printf("  TraceEvent: %lu\n", ret);
    }

    q = new_props(0, NULL, 0, 0, NULL);
    ret = ControlTraceW(s, NULL, q, EVENT_TRACE_CONTROL_FLUSH);
    printf("  flush: %lu\n", ret);
    for (i = 0; i < 40 && (rec_count < 4 || classic_count < 4); i++) Sleep(100);
    GetSystemTimeAsFileTime((FILETIME *)&now);
    QueryPerformanceCounter(&now_qpc);
    QueryPerformanceFrequency(&freq);
    printf("  EVENT_RECORD consumer: %ld records, buffer callbacks %ld\n", rec_count, buffer_calls);
    for (i = 0; i < rec_count && i < ARRAYSIZE(recs); i++)
    {
        EVENT_HEADER *h = &recs[i].header;
        LONGLONG system_delta = recs[i].system_time.QuadPart - h->TimeStamp.QuadPart;

        printf("    [size %u type %#x flags %#x property %#x thread %s process %s time %s provider %s id %u version %u"
               " channel %u level %u opcode %u task %u keyword %#llx kernel %lu user %lu activity %s"
               " processor %u logger %s ext %u", h->Size, h->HeaderType, h->Flags, h->EventProperty,
               h->ThreadId == GetCurrentThreadId() ? "ours" : h->ThreadId ? "other" : "0",
               h->ProcessId == GetCurrentProcessId() ? "ours" : "other",
               system_delta >= 0 && system_delta < 100000000 ? "system time" :
               h->TimeStamp.QuadPart <= recs[i].qpc.QuadPart && h->TimeStamp.QuadPart > recs[i].qpc.QuadPart - 10 * freq.QuadPart ?
               "performance counter" : "other",
               IsEqualGUID(&h->ProviderId, &prov_guid) ? "ours" : IsEqualGUID(&h->ProviderId, &class_guid) ? "the class" :
               IsEqualGUID(&h->ProviderId, &classic_guid) ? "the classic one" : "other",
               h->EventDescriptor.Id, h->EventDescriptor.Version, h->EventDescriptor.Channel, h->EventDescriptor.Level,
               h->EventDescriptor.Opcode, h->EventDescriptor.Task, h->EventDescriptor.Keyword, h->KernelTime, h->UserTime,
               IsEqualGUID(&h->ActivityId, &act_guid) ? "ours" : shape(&h->ActivityId), recs[i].context.ProcessorNumber,
               recs[i].context.LoggerId ? "set" : "0", recs[i].ext_count);
        for (from = 0; from < recs[i].ext_count && from < 4; from++)
            printf(" (type %u size %u)", recs[i].ext_types[from], recs[i].ext_sizes[from]);
        printf(" user %lu", recs[i].user_len);
        hex(" ", recs[i].user, min(recs[i].user_len, sizeof(recs[i].user)));
        printf("      context %s]\n", recs[i].user_context == (void *)0x1234 ? "ours" : "other");
    }
    printf("  EVENT_TRACE consumer: %ld records\n", classic_count);
    for (i = 0; i < classic_count && i < ARRAYSIZE(classic_recs); i++)
    {
        EVENT_TRACE_HEADER *h = &classic_recs[i].header;

        printf("    [size %u type %#x marker %#x class type %u level %u version %u thread %s process %s time %s"
               " guid %s kernel %lu user %lu instance %lu parent %lu parent guid %s mof %lu client %lu]\n", h->Size,
               h->HeaderType, h->MarkerFlags, h->Class.Type, h->Class.Level, h->Class.Version,
               h->ThreadId == GetCurrentThreadId() ? "ours" : h->ThreadId ? "other" : "0",
               h->ProcessId == GetCurrentProcessId() ? "ours" : "other",
               h->TimeStamp.QuadPart <= now.QuadPart && h->TimeStamp.QuadPart > now.QuadPart - 600000000 ?
               "system time" : "other",
               IsEqualGUID(&h->Guid, &prov_guid) ? "ours" : IsEqualGUID(&h->Guid, &class_guid) ? "the class" :
               IsEqualGUID(&h->Guid, &EventTraceGuid) ? "EventTraceGuid" : "other", h->KernelTime, h->UserTime,
               classic_recs[i].instance, classic_recs[i].parent, classic_recs[i].parent_guid_zero ? "zero" : "set",
               classic_recs[i].mof_len, classic_recs[i].client);
    }

    ret = ControlTraceW(s, NULL, q, EVENT_TRACE_CONTROL_QUERY);
    printf("  query: %lu, mode %#lx, flush %lu, logger thread %s, buffers written %lu\n", ret, q->LogFileMode,
           q->FlushTimer, q->LoggerThreadId ? "set" : "0", q->BuffersWritten);
    {
        EVENT_TRACE_PROPERTIES *all[64];
        ULONG count = 0, found = 0, j;

        for (j = 0; j < 64; j++) all[j] = new_props(0, NULL, 0, 0, L"");
        ret = QueryAllTracesW(all, 64, &count);
        for (j = 0; j < count && j < 64; j++)
            if (!wcscmp((WCHAR *)((BYTE *)all[j] + all[j]->LoggerNameOffset), name))
            {
                found++;
                printf("  QueryAllTraces: ours: mode %#lx clock %lu handle %s\n", all[j]->LogFileMode,
                       all[j]->Wnode.ClientContext, all[j]->Wnode.HistoricalContext == s ? "the same" : "other");
            }
        printf("  QueryAllTraces: %lu, %lu sessions, ours found %lu\n", ret, count, found);
        count = 0;
        ret = QueryAllTracesW(all, 1, &count);
        printf("  QueryAllTraces into one: %lu, count %lu\n", ret, count);
        for (j = 0; j < 64; j++) free(all[j]);
    }
    q->FlushTimer = 2;
    ret = ControlTraceW(s, NULL, q, EVENT_TRACE_CONTROL_UPDATE);
    printf("  update the flush timer: %lu, flush %lu\n", ret, q->FlushTimer);

    from = cb_count;
    ret = ControlTraceW(s, NULL, q, EVENT_TRACE_CONTROL_STOP);
    printf("  stop: %lu\n", ret);
    for (i = 0; i < 2; i++)
    {
        if (!threads[i]) continue;
        ret = WaitForSingleObject(threads[i], 5000);
        printf("  ProcessTrace %lu %s: %lu\n", i, ret ? "still running" : "returned", process_status[i]);
        if (!ret) CloseHandle(threads[i]);
    }
    show_logfile("after ProcessTrace", &logfile);
    for (i = 0; i < 2; i++)
        if (consumers[i] != INVALID_PROCESSTRACE_HANDLE) printf("  CloseTrace %lu: %lu\n", i, CloseTrace(consumers[i]));
    printf("  CloseTrace again: %lu\n", CloseTrace(consumers[0]));
    UnregisterTraceGuids(classic);
    EventUnregister(reg);

    memset(&logfile, 0, sizeof(logfile));
    logfile.LoggerName = (WCHAR *)L"WineAltarsNoSuchSession";
    logfile.ProcessTraceMode = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD;
    logfile.EventRecordCallback = record_callback;
    SetLastError(0xdeadbeef);
    consumers[0] = OpenTraceW(&logfile);
    printf("  OpenTrace a session nobody started: %s error %lu", consumers[0] == INVALID_PROCESSTRACE_HANDLE ?
           "invalid" : "opened", GetLastError());
    if (consumers[0] != INVALID_PROCESSTRACE_HANDLE)
    {
        printf(", ProcessTrace %lu", ProcessTrace(&consumers[0], 1, NULL, NULL));
        printf(", CloseTrace %lu", CloseTrace(consumers[0]));
    }
    printf("\n");
    SetLastError(0xdeadbeef);
    printf("  OpenTrace NULL: %s error %lu\n", OpenTraceW(NULL) == INVALID_PROCESSTRACE_HANDLE ? "invalid" : "opened",
           GetLastError());
    {
        TRACEHANDLE bad = 0x12345;
        printf("  ProcessTrace a bad handle: %lu, none %lu\n", ProcessTrace(&bad, 1, NULL, NULL),
               ProcessTrace(&bad, 0, NULL, NULL));
        printf("  CloseTrace a bad handle: %lu\n", CloseTrace(bad));
    }
    free(q);
    free(props);
}

static void event_sources(void)
{
    WCHAR server[300];
    HANDLE source;

    printf("event sources:\n");
    SetLastError(0xdeadbeef);
    source = RegisterEventSourceW(computer_name, L"WineAltarsNoSuchSource");
    printf("  this computer's name: %s error %lu\n", source ? "handle" : "NULL", source ? 0 : GetLastError());
    if (source) DeregisterEventSource(source);
    swprintf(server, ARRAYSIZE(server), L"\\\\%ls", computer_name);
    SetLastError(0xdeadbeef);
    source = RegisterEventSourceW(server, L"WineAltarsNoSuchSource");
    printf("  \\\\ and this computer's name: %s error %lu\n", source ? "handle" : "NULL", source ? 0 : GetLastError());
    if (source) DeregisterEventSource(source);
    SetLastError(0xdeadbeef);
    source = RegisterEventSourceW(L"localhost", L"WineAltarsNoSuchSource");
    printf("  localhost: %s error %lu\n", source ? "handle" : "NULL", source ? 0 : GetLastError());
    if (source) DeregisterEventSource(source);
    SetLastError(0xdeadbeef);
    source = RegisterEventSourceW(NULL, NULL);
    printf("  no source: %s error %lu\n", source ? "handle" : "NULL", source ? 0 : GetLastError());
    if (source) DeregisterEventSource(source);
    SetLastError(0xdeadbeef);
    source = RegisterEventSourceW(NULL, L"");
    printf("  an empty source: %s error %lu\n", source ? "handle" : "NULL", source ? 0 : GetLastError());
    if (source) DeregisterEventSource(source);
    SetLastError(0xdeadbeef);
    source = RegisterEventSourceW(NULL, L"Security");
    printf("  Security: %s error %lu\n", source ? "handle" : "NULL", source ? 0 : GetLastError());
    if (source) DeregisterEventSource(source);
    SetLastError(0xdeadbeef);
    source = OpenEventLogW(NULL, L"Application");
    printf("  OpenEventLog Application: %s error %lu", source ? "handle" : "NULL", source ? 0 : GetLastError());
    if (source)
    {
        DWORD count = 0;
        printf(", GetNumberOfEventLogRecords %d", GetNumberOfEventLogRecords(source, &count));
        printf(", CloseEventLog %d", CloseEventLog(source));
    }
    printf("\n");
}

int main(int argc, char **argv)
{
    DWORD size;

    setvbuf(stdout, NULL, _IONBF, 0);
    size = ARRAYSIZE(user_name);
    GetUserNameW(user_name, &size);
    size = ARRAYSIZE(computer_name);
    GetComputerNameW(computer_name, &size);
    private_properties();
    enable_semantics();
    activity_ids();
    records();
    real_time();
    event_sources();
    return 0;
}
