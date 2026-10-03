/* evtapi: what wevtapi does with the classic logs, read only -- the errors EvtQuery, EvtNext and EvtRender give,
 * which XPath it takes (timediff, band, not, structured queries with Suppress), what the user and value render
 * contexts give for a classic event, EvtFormatMessage for one, EvtOpenLog/EvtGetLogInfo, the channels and the
 * configuration of a classic one.  Only the EventLog service's own events of the System log are rendered; counts
 * are given as "some" or "none".  Writes nothing.  Prints results only. */
#include <windows.h>
#include <winevt.h>
#include <stdio.h>

static DWORD count_matches(const WCHAR *path, const WCHAR *query, DWORD flags)
{
    EVT_HANDLE results, events[16];
    DWORD returned, total = 0, i;

    SetLastError(0xdeadbeef);
    if (!(results = EvtQuery(NULL, path, query, flags))) return ~0u - GetLastError();
    while (EvtNext(results, 16, events, INFINITE, 0, &returned))
    {
        total += returned;
        for (i = 0; i < returned; i++) EvtClose(events[i]);
    }
    EvtClose(results);
    return total;
}

static void query(const char *what, const WCHAR *path, const WCHAR *text, DWORD flags)
{
    DWORD n;

    SetLastError(0xdeadbeef);
    n = count_matches(path, text, flags);
    if (n > ~0u - 100000) printf("%s: EvtQuery error %lu\n", what, ~0u - n);
    else printf("%s: %s (%s)\n", what, n ? "some" : "none", n > 1 ? "more than one" : n ? "one" : "zero");
}

static EVT_HANDLE first_event(const WCHAR *query_text)
{
    EVT_HANDLE results, event = NULL;
    DWORD returned;

    if (!(results = EvtQuery(NULL, L"System", query_text, EvtQueryChannelPath | EvtQueryReverseDirection))) return NULL;
    if (!EvtNext(results, 1, &event, INFINITE, 0, &returned)) event = NULL;
    EvtClose(results);
    return event;
}

static void show_values(const char *what, EVT_HANDLE context, EVT_HANDLE event)
{
    DWORD used = 0, count = 0, i;
    EVT_VARIANT *values;
    BOOL ret;

    SetLastError(0xdeadbeef);
    ret = EvtRender(context, event, EvtRenderEventValues, 0, NULL, &used, &count);
    printf("%s: %d error %lu used %lu count %lu", what, ret, ret ? 0 : GetLastError(), used, count);
    values = malloc(used);
    if (EvtRender(context, event, EvtRenderEventValues, used, values, &used, &count))
    {
        printf(", types");
        for (i = 0; i < count; i++)
        {
            printf(" %lu", values[i].Type);
            if (values[i].Type == EvtVarTypeString && values[i].StringVal && wcslen(values[i].StringVal) < 24)
                printf("(%ls)", values[i].StringVal);
            if (values[i].Type == EvtVarTypeUInt16) printf("(%u)", values[i].UInt16Val);
            if (values[i].Type == EvtVarTypeByte) printf("(%u)", values[i].ByteVal);
            if (values[i].Type == EvtVarTypeBinary) printf("(%lu bytes)", values[i].Count);
            if (values[i].Count) printf("[count %lu]", values[i].Count);
        }
    }
    free(values);
    printf("\n");
}

static void render(void)
{
    static const WCHAR *paths[] =
    {
        L"Event/System/Provider/@Name", L"Event/System/EventID", L"Event/System/EventID/@Qualifiers",
        L"Event/System/Level", L"Event/System/TimeCreated/@SystemTime", L"Event/System/Channel",
        L"Event/EventData/Data[1]", L"Event/EventData/Binary", L"Event/System/Nothing",
    };
    EVT_HANDLE event, context;
    DWORD used, count;
    WCHAR small[8];
    BOOL ret;

    if (!(event = first_event(L"*[System[Provider[@Name='EventLog'] and EventID=6009]]")))
    {
        printf("no 6009 event\n");
        return;
    }
    used = count = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    ret = EvtRender(NULL, event, EvtRenderEventXml, sizeof(small), small, &used, &count);
    printf("XML into 16 bytes: %d error %lu used %s count %lu\n", ret, GetLastError(), used > 100 ? "the size" : "?",
           count);
    SetLastError(0xdeadbeef);
    ret = EvtRender(NULL, event, EvtRenderEventValues, 0, NULL, &used, &count);
    printf("values without a context: %d error %lu\n", ret, GetLastError());

    context = EvtCreateRenderContext(0, NULL, EvtRenderContextUser);
    show_values("user values of 6009", context, event);
    EvtClose(context);
    context = EvtCreateRenderContext(ARRAYSIZE(paths), paths, EvtRenderContextValues);
    show_values("path values of 6009", context, event);
    EvtClose(context);
    SetLastError(0xdeadbeef);
    context = EvtCreateRenderContext(1, (const WCHAR *[]){ L"Event/System/[" }, EvtRenderContextValues);
    printf("bad path context: %s error %lu\n", context ? "made" : "none", GetLastError());
    if (context) EvtClose(context);

    {
        WCHAR buffer[512];
        static const DWORD flags[] = { EvtFormatMessageEvent, EvtFormatMessageLevel, EvtFormatMessageTask,
                                       EvtFormatMessageOpcode, EvtFormatMessageKeyword, EvtFormatMessageChannel,
                                       EvtFormatMessageProvider, EvtFormatMessageXml };
        EVT_HANDLE publisher;
        DWORD i;

        SetLastError(0xdeadbeef);
        publisher = EvtOpenPublisherMetadata(NULL, L"EventLog", NULL, 0, 0);
        printf("publisher metadata EventLog: %s error %lu\n", publisher ? "opened" : "none", GetLastError());
        for (i = 0; i < ARRAYSIZE(flags); i++)
        {
            used = 0;
            SetLastError(0xdeadbeef);
            ret = EvtFormatMessage(publisher, event, 0, 0, NULL, flags[i], ARRAYSIZE(buffer), buffer, &used);
            printf("  format %lu: %d error %lu used %lu", flags[i], ret, ret ? 0 : GetLastError(), used);
            if (ret && flags[i] != EvtFormatMessageXml) printf(" [%ls]", buffer);
            printf("\n");
        }
        SetLastError(0xdeadbeef);
        ret = EvtFormatMessage(NULL, event, 0, 0, NULL, EvtFormatMessageEvent, ARRAYSIZE(buffer), buffer, &used);
        printf("  format event without publisher: %d error %lu\n", ret, ret ? 0 : GetLastError());
        if (publisher) EvtClose(publisher);
        SetLastError(0xdeadbeef);
        publisher = EvtOpenPublisherMetadata(NULL, L"wine-altars-no-such-source", NULL, 0, 0);
        printf("publisher metadata of no source: %s error %lu\n", publisher ? "opened" : "none", GetLastError());
        if (publisher) EvtClose(publisher);
    }
    EvtClose(event);
}

static void log_info(void)
{
    static const EVT_LOG_PROPERTY_ID ids[] = { EvtLogCreationTime, EvtLogLastAccessTime, EvtLogLastWriteTime,
                                               EvtLogFileSize, EvtLogAttributes, EvtLogNumberOfLogRecords,
                                               EvtLogOldestRecordNumber, EvtLogFull };
    EVT_VARIANT value[4];
    EVT_HANDLE log;
    DWORD used, i;

    SetLastError(0xdeadbeef);
    log = EvtOpenLog(NULL, L"System", EvtOpenChannelPath);
    printf("EvtOpenLog System: %s error %lu\n", log ? "opened" : "none", GetLastError());
    if (!log) return;
    for (i = 0; i < ARRAYSIZE(ids); i++)
    {
        SetLastError(0xdeadbeef);
        if (EvtGetLogInfo(log, ids[i], sizeof(value), value, &used))
            printf("  property %u: type %lu used %lu %s\n", ids[i], value[0].Type, used,
                   value[0].Type == EvtVarTypeBoolean ? (value[0].BooleanVal ? "true" : "false") : "");
        else printf("  property %u: error %lu\n", ids[i], GetLastError());
    }
    EvtClose(log);
    SetLastError(0xdeadbeef);
    log = EvtOpenLog(NULL, L"wine-altars-no-such-log", EvtOpenChannelPath);
    printf("EvtOpenLog of no log: %s error %lu\n", log ? "opened" : "none", GetLastError());
}

static void channels(void)
{
    static const EVT_CHANNEL_CONFIG_PROPERTY_ID ids[] =
    {
        EvtChannelConfigEnabled, EvtChannelConfigIsolation, EvtChannelConfigType, EvtChannelConfigOwningPublisher,
        EvtChannelConfigClassicEventlog, EvtChannelConfigAccess, EvtChannelLoggingConfigRetention,
        EvtChannelLoggingConfigAutoBackup, EvtChannelLoggingConfigMaxSize, EvtChannelLoggingConfigLogFilePath,
    };
    BYTE buffer[2048];
    EVT_VARIANT *value = (EVT_VARIANT *)buffer;
    WCHAR name[512];
    EVT_HANDLE channel_enum, config;
    DWORD used, n = 0, classic = 0, i;

    channel_enum = EvtOpenChannelEnum(NULL, 0);
    while (channel_enum && EvtNextChannelPath(channel_enum, ARRAYSIZE(name), name, &used))
    {
        n++;
        if (!wcschr(name, '/') && !wcschr(name, '-')) classic++;
    }
    printf("channels: %s, %lu without / or -, last error %lu\n", n ? "some" : "none", classic, GetLastError());
    if (channel_enum) EvtClose(channel_enum);

    config = EvtOpenChannelConfig(NULL, L"Application", 0);
    printf("Application config: %s\n", config ? "opened" : "none");
    for (i = 0; config && i < ARRAYSIZE(ids); i++)
    {
        SetLastError(0xdeadbeef);
        if (EvtGetChannelConfigProperty(config, ids[i], 0, sizeof(buffer), value, &used))
        {
            printf("  property %u: type %lu used %lu", ids[i], value->Type, used);
            if (value->Type == EvtVarTypeString && value->StringVal) printf(" [%ls]", value->StringVal);
            if (value->Type == EvtVarTypeUInt32) printf(" %lu", value->UInt32Val);
            if (value->Type == EvtVarTypeUInt64) printf(" %llu", value->UInt64Val);
            if (value->Type == EvtVarTypeBoolean) printf(" %d", value->BooleanVal);
            printf("\n");
        }
        else printf("  property %u: error %lu\n", ids[i], GetLastError());
    }
    if (config) EvtClose(config);
    SetLastError(0xdeadbeef);
    config = EvtOpenChannelConfig(NULL, L"wine-altars-no-such-log", 0);
    printf("config of no channel: %s error %lu\n", config ? "opened" : "none", GetLastError());
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

    query("6005 by XPath", L"System", L"*[System[Provider[@Name='EventLog'] and EventID=6005]]", EvtQueryChannelPath);
    query("6005 with Qualifiers", L"System", L"*[System[EventID[@Qualifiers='32768']=6005]]", EvtQueryChannelPath);
    query("6005 under Event/System", L"System", L"Event/System[EventID=6005]", EvtQueryChannelPath);
    query("6005, timediff in a year", L"System",
          L"*[System[EventID=6005 and TimeCreated[timediff(@SystemTime) <= 31536000000]]]", EvtQueryChannelPath);
    query("6005, timediff in a millisecond", L"System",
          L"*[System[EventID=6005 and TimeCreated[timediff(@SystemTime) <= 1]]]", EvtQueryChannelPath);
    query("6005 after 2000", L"System", L"*[System[EventID=6005 and TimeCreated[@SystemTime>='2000-01-01T00:00:00.000Z']]]",
          EvtQueryChannelPath);
    query("6005, keywords band classic", L"System", L"*[System[EventID=6005 and band(Keywords,36028797018963968)]]",
          EvtQueryChannelPath);
    query("6005, not", L"System", L"*[System[EventID=6005 and not(Level=2)]]", EvtQueryChannelPath);
    query("6005, !=", L"System", L"*[System[EventID=6005 and Level!=2]]", EvtQueryChannelPath);
    query("6009 data", L"System", L"*[System[EventID=6009] and EventData[Data='Multiprocessor Free']]", EvtQueryChannelPath);
    query("6009 data 2", L"System", L"*[EventData[Data[4]='Multiprocessor Free']]", EvtQueryChannelPath);
    query("structured, 6005", NULL,
          L"<QueryList><Query Id=\"0\" Path=\"System\"><Select Path=\"System\">*[System[EventID=6005]]</Select></Query></QueryList>",
          EvtQueryChannelPath);
    query("structured, 6005 suppressed", NULL,
          L"<QueryList><Query Id='0'><Select Path='System'>*[System[EventID=6005]]</Select>"
          L"<Suppress Path='System'>*[System[Level=4]]</Suppress></Query></QueryList>", EvtQueryChannelPath);
    query("structured, path argument too", L"System",
          L"<QueryList><Query Id='0' Path='System'><Select>*[System[EventID=6005]]</Select></Query></QueryList>",
          EvtQueryChannelPath);
    query("bad XPath", L"System", L"*[System[EventID=", EvtQueryChannelPath);
    query("unknown function", L"System", L"*[System[foo(EventID)]]", EvtQueryChannelPath);
    query("no such channel", L"wine-altars-no-such-log", L"*", EvtQueryChannelPath);
    query("no such file", L"C:\\wine-altars-no-such-file.evtx", L"*", EvtQueryFilePath);
    query("both path flags", L"System", L"*", EvtQueryChannelPath | EvtQueryFilePath);
    query("no path, XPath", NULL, L"*[System[EventID=6005]]", EvtQueryChannelPath);
    {
        EVT_HANDLE results = EvtQuery(NULL, L"System", L"*[System[EventID=6005]]", EvtQueryChannelPath);
        EVT_HANDLE events[2];
        DWORD returned = 0xdeadbeef;
        BOOL ret;

        SetLastError(0xdeadbeef);
        ret = EvtNext(results, 0, events, INFINITE, 0, &returned);
        printf("EvtNext 0 events: %d error %lu returned %lu\n", ret, ret ? 0 : GetLastError(), returned);
        while (EvtNext(results, 2, events, INFINITE, 0, &returned))
        {
            EvtClose(events[0]);
            if (returned > 1) EvtClose(events[1]);
        }
        printf("EvtNext at the end: error %lu returned %lu\n", GetLastError(), returned);
        EvtClose(results);
    }
    render();
    log_info();
    channels();
    return 0;
}
