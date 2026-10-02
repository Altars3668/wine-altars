/* etw5: two questions etw4 left -- how ControlTrace finds a session with no handle given: by the name, by the handle
 * a query left in the properties (Wnode.HistoricalContext), or both, for a private and a real-time session; and what
 * a private logger that is not in-process does: how long StartTrace takes to fail, whether its name stays taken, and
 * whether a provider of this process registered with its GUID, classic or not, makes a difference.  Prints results
 * only. */
#define INITGUID
#include <windows.h>
#include <evntprov.h>
#include <evntrace.h>
#include <stdio.h>
#include <wchar.h>

DEFINE_GUID(ctrl_guid,    0x5a1e3c50, 0x7c11, 0x4b2a, 0x9e, 0x51, 0x30, 0x6b, 0x2f, 0x41, 0x8a, 0x77);
DEFINE_GUID(classic_guid, 0x5a1e3c51, 0x7c11, 0x4b2a, 0x9e, 0x51, 0x30, 0x6b, 0x2f, 0x41, 0x8a, 0x77);
DEFINE_GUID(manifest_guid, 0x5a1e3c52, 0x7c11, 0x4b2a, 0x9e, 0x51, 0x30, 0x6b, 0x2f, 0x41, 0x8a, 0x77);
DEFINE_GUID(class_guid,   0x5a1e3c53, 0x7c11, 0x4b2a, 0x9e, 0x51, 0x30, 0x6b, 0x2f, 0x41, 0x8a, 0x77);

static EVENT_TRACE_PROPERTIES *new_props(ULONG mode, const GUID *guid, const WCHAR *file)
{
    ULONG size = sizeof(EVENT_TRACE_PROPERTIES) + 2 * 1024 * sizeof(WCHAR);
    EVENT_TRACE_PROPERTIES *props = calloc(1, size);

    props->Wnode.BufferSize = size;
    props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    props->Wnode.ClientContext = 2;
    if (guid) props->Wnode.Guid = *guid;
    props->LogFileMode = mode;
    props->LoggerNameOffset = sizeof(EVENT_TRACE_PROPERTIES);
    if (file)
    {
        props->LogFileNameOffset = sizeof(EVENT_TRACE_PROPERTIES) + 1024 * sizeof(WCHAR);
        wcscpy((WCHAR *)((BYTE *)props + props->LogFileNameOffset), file);
    }
    return props;
}

static void lookups(const char *what, ULONG mode, const GUID *guid)
{
    WCHAR name[64];
    EVENT_TRACE_PROPERTIES *props = new_props(mode, guid, NULL), *q;
    TRACEHANDLE handle = 0;
    ULONG ret;

    printf("%s:\n", what);
    swprintf(name, ARRAYSIZE(name), L"WineAltarsEtw5-%lu", GetCurrentProcessId());
    if ((ret = StartTraceW(&handle, name, props)))
    {
        printf("  StartTrace %lu\n", ret);
        free(props);
        return;
    }
    q = new_props(0, NULL, NULL);
    q->Wnode.HistoricalContext = handle;
    ret = ControlTraceW(0, name, q, EVENT_TRACE_CONTROL_QUERY);
    printf("  the name, the handle in the properties: %lu\n", ret);
    free(q);
    q = new_props(0, NULL, NULL);
    q->Wnode.HistoricalContext = handle;
    ret = ControlTraceW(0, L"WineAltarsEtw5Other", q, EVENT_TRACE_CONTROL_QUERY);
    printf("  another name, the handle in the properties: %lu\n", ret);
    free(q);
    q = new_props(0, NULL, NULL);
    q->Wnode.HistoricalContext = handle;
    ret = ControlTraceW(0, NULL, q, EVENT_TRACE_CONTROL_QUERY);
    printf("  no name, the handle in the properties: %lu\n", ret);
    free(q);
    q = new_props(0, NULL, NULL);
    q->Wnode.HistoricalContext = handle + 1;
    ret = ControlTraceW(handle, NULL, q, EVENT_TRACE_CONTROL_QUERY);
    printf("  the handle, another in the properties: %lu\n", ret);
    free(q);
    q = new_props(0, NULL, NULL);
    q->Wnode.HistoricalContext = handle;
    ret = ControlTraceW(0, name, q, EVENT_TRACE_CONTROL_STOP);
    printf("  stop by the name, the handle in the properties: %lu", ret);
    if (ret) printf(", by the handle %lu", ControlTraceW(handle, NULL, q, EVENT_TRACE_CONTROL_STOP));
    printf("\n");
    free(q);
    free(props);
}

static ULONG WINAPI request(WMIDPREQUESTCODE code, void *context, ULONG *size, void *buffer)
{
    printf("    the provider is asked: code %u\n", code);
    return ERROR_SUCCESS;
}

static void not_in_proc(const char *what, const GUID *guid, const char *tag)
{
    WCHAR name[64], temp[MAX_PATH], file[MAX_PATH];
    EVENT_TRACE_PROPERTIES *props;
    TRACEHANDLE handle = 0;
    ULONGLONG start;
    ULONG ret;

    GetTempPathW(ARRAYSIZE(temp), temp);
    swprintf(file, ARRAYSIZE(file), L"%lsWineAltarsEtw5-%hs-%lu.etl", temp, tag, GetCurrentProcessId());
    swprintf(name, ARRAYSIZE(name), L"WineAltarsEtw5-%hs-%lu", tag, GetCurrentProcessId());
    props = new_props(EVENT_TRACE_PRIVATE_LOGGER_MODE | EVENT_TRACE_FILE_MODE_SEQUENTIAL, guid, file);
    start = GetTickCount64();
    ret = StartTraceW(&handle, name, props);
    printf("%s: StartTrace %lu after %llu ms, handle %s", what, ret, (GetTickCount64() - start) / 1000 * 1000,
           handle ? "set" : "0");
    if (!ret)
    {
        printf(", mode after %#lx", props->LogFileMode);
        printf(", stop %lu", ControlTraceW(handle, NULL, props, EVENT_TRACE_CONTROL_STOP));
    }
    else
    {
        EVENT_TRACE_PROPERTIES *again = new_props(EVENT_TRACE_PRIVATE_LOGGER_MODE | EVENT_TRACE_PRIVATE_IN_PROC |
                                                  EVENT_TRACE_BUFFERING_MODE, &class_guid, NULL);
        TRACEHANDLE h2;
        ULONG r2 = StartTraceW(&h2, name, again);

        printf(", the name again in-process: %lu", r2);
        if (!r2) ControlTraceW(h2, NULL, again, EVENT_TRACE_CONTROL_STOP);
        free(again);
    }
    printf("\n");
    DeleteFileW(file);
    free(props);
}

int main(void)
{
    TRACE_GUID_REGISTRATION classes[1] = { { &class_guid } };
    TRACEHANDLE classic = 0;
    REGHANDLE reg = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    lookups("a private session", EVENT_TRACE_PRIVATE_LOGGER_MODE | EVENT_TRACE_PRIVATE_IN_PROC |
            EVENT_TRACE_BUFFERING_MODE, &ctrl_guid);
    lookups("a real-time session", EVENT_TRACE_REAL_TIME_MODE, NULL);

    not_in_proc("a private logger not in-process, nobody's GUID", &ctrl_guid, "a");
    RegisterTraceGuidsW(request, NULL, &classic_guid, 1, classes, NULL, NULL, &classic);
    not_in_proc("the GUID of a classic provider registered here", &classic_guid, "b");
    UnregisterTraceGuids(classic);
    EventRegister(&manifest_guid, NULL, NULL, &reg);
    not_in_proc("the GUID of a provider registered here with EventRegister", &manifest_guid, "c");
    EventUnregister(reg);
    return 0;
}
