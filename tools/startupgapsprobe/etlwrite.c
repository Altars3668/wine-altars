/* etlwrite: a private session's log file with one event of each kind -- 300 of a manifest provider's, a string, a
 * TraceLogging event, a classic TraceEvent and a TraceMessage -- kept at the path given, for etlread.exe and tracerpt
 * to read on Windows.
 *
 *     etlwrite.exe file.etl */
#define INITGUID
#include <windows.h>
#include <evntprov.h>
#include <evntrace.h>
#include <stdio.h>

DEFINE_GUID(prov_guid, 0x5a1e3c60, 0x7c11, 0x4b2a, 0x9e, 0x51, 0x30, 0x6b, 0x2f, 0x41, 0x8a, 0x77);
DEFINE_GUID(tl_guid, 0x5a1e3c61, 0x7c11, 0x4b2a, 0x9e, 0x51, 0x30, 0x6b, 0x2f, 0x41, 0x8a, 0x77);
DEFINE_GUID(ctrl_guid, 0x5a1e3c62, 0x7c11, 0x4b2a, 0x9e, 0x51, 0x30, 0x6b, 0x2f, 0x41, 0x8a, 0x77);
DEFINE_GUID(class_guid, 0x5a1e3c63, 0x7c11, 0x4b2a, 0x9e, 0x51, 0x30, 0x6b, 0x2f, 0x41, 0x8a, 0x77);
DEFINE_GUID(msg_guid, 0x5a1e3c64, 0x7c11, 0x4b2a, 0x9e, 0x51, 0x30, 0x6b, 0x2f, 0x41, 0x8a, 0x77);

int wmain(int argc, WCHAR **argv)
{
    ULONG size = sizeof(EVENT_TRACE_PROPERTIES) + 4096;
    EVENT_TRACE_PROPERTIES *props = calloc(1, size);
    TRACEHANDLE h;
    REGHANDLE reg, tl;
    EVENT_DATA_DESCRIPTOR d[4];
    BYTE traits[] = { 15, 0, 'W','i','n','e','A','l','t','a','r','s','T','l',0 };
    BYTE meta[] = { 24, 0, 0, 'T','l','E','v','e','n','t',0, 'V','a','l','u','e',0, 8, 'T','e','x','t',0, 2 };
    ULONG value = 0x1234, i;
    char text[] = "tl";

    props->Wnode.BufferSize = size;
    props->Wnode.Flags = WNODE_FLAG_TRACED_GUID;
    props->Wnode.ClientContext = 1;
    props->Wnode.Guid = ctrl_guid;
    props->LogFileMode = EVENT_TRACE_PRIVATE_LOGGER_MODE | EVENT_TRACE_PRIVATE_IN_PROC | EVENT_TRACE_FILE_MODE_SEQUENTIAL;
    props->LoggerNameOffset = sizeof(*props);
    props->LogFileNameOffset = sizeof(*props) + 256;
    wcscpy((WCHAR *)((BYTE *)props + props->LogFileNameOffset), argv[1]);
    printf("StartTrace %lu\n", StartTraceW(&h, L"WineAltarsEtlWrite", props));
    EventRegister(&prov_guid, NULL, NULL, &reg);
    EventRegister(&tl_guid, NULL, NULL, &tl);
    EventSetInformation(tl, 2, traits, sizeof(traits));
    EnableTraceEx2(h, &prov_guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 5, 0, 0, 0, NULL);
    EnableTraceEx2(h, &tl_guid, EVENT_CONTROL_CODE_ENABLE_PROVIDER, 5, 0, 0, 0, NULL);
    for (i = 0; i < 300; i++)
    {
        char buf[64];
        sprintf(buf, "event %lu", i);
        EventDataDescCreate(&d[0], buf, strlen(buf) + 1);
        EventWrite(reg, &(EVENT_DESCRIPTOR){ (USHORT)(i % 5 + 1), 0, 0, 4, 0, 0, 1 }, 1, d);
    }
    EventWriteString(reg, 4, 1, L"a string");
    EventDataDescCreate(&d[0], traits, sizeof(traits)); d[0].Reserved = 2;
    EventDataDescCreate(&d[1], meta, sizeof(meta)); d[1].Reserved = 1;
    EventDataDescCreate(&d[2], &value, sizeof(value));
    EventDataDescCreate(&d[3], text, sizeof(text));
    EventWriteTransfer(tl, &(EVENT_DESCRIPTOR){ 0, 0, 11, 4, 0, 0, 2 }, NULL, NULL, 4, d);
    {
        struct { EVENT_TRACE_HEADER header; ULONG data[2]; } ev = { { 0 } };
        ev.header.Size = sizeof(ev);
        ev.header.Flags = WNODE_FLAG_TRACED_GUID;
        ev.header.Guid = class_guid;
        ev.header.Class.Type = 9;
        ev.header.Class.Level = 3;
        TraceEvent(h, &ev.header);
        TraceMessage(h, TRACE_MESSAGE_GUID | TRACE_MESSAGE_TIMESTAMP | TRACE_MESSAGE_SYSTEMINFO, &msg_guid, 7,
                     &value, sizeof(value), NULL);
    }
    printf("stop %lu\n", ControlTraceW(h, NULL, props, EVENT_TRACE_CONTROL_STOP));
    return 0;
}
