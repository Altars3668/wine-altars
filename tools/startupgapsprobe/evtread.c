/* evtread: how Windows lays out and reads back classic event log records, read only.  It opens the System and
 * Application logs, prints counts, the layout of the EventLog service's "started" record (6005) and of a few
 * others (offsets and lengths, never the strings), how sequential and seek reads move, and the shape of the XML
 * wevtapi renders for a classic event with every value masked to its length.  Writes nothing, reports nothing.
 * Prints results only. */
#include <windows.h>
#include <winevt.h>
#include <stdio.h>

static void show_layout(const char *what, EVENTLOGRECORD *rec)
{
    WCHAR name[256];
    DWORD size = ARRAYSIZE(name);
    const WCHAR *source = (const WCHAR *)(rec + 1);
    const WCHAR *computer = source + lstrlenW(source) + 1;
    DWORD after_computer = (DWORD)((const BYTE *)(computer + lstrlenW(computer) + 1) - (const BYTE *)rec);
    DWORD i;

    GetComputerNameW(name, &size);
    printf("%s: length %lu type %u category %u strings %u flags %u closing %lu\n", what, rec->Length,
           rec->EventType, rec->EventCategory, rec->NumStrings, rec->ReservedFlags, rec->ClosingRecordNumber);
    printf("  source length %d, computer length %d (%s the NetBIOS name)", lstrlenW(source), lstrlenW(computer),
           lstrcmpiW(computer, name) ? "not" : "is");
    size = ARRAYSIZE(name);
    if (GetComputerNameExW(ComputerNameDnsFullyQualified, name, &size))
        printf(" (%s the DNS name)", lstrcmpiW(computer, name) ? "not" : "is");
    printf("\n  after computer %lu, sid offset %lu length %lu, strings offset %lu, data offset %lu length %lu\n",
           after_computer, rec->UserSidOffset, rec->UserSidLength, rec->StringOffset, rec->DataOffset,
           rec->DataLength);
    if (rec->NumStrings)
    {
        const WCHAR *s = (const WCHAR *)((BYTE *)rec + rec->StringOffset);
        for (i = 0; i < rec->NumStrings; i++) s += lstrlenW(s) + 1;
        printf("  strings end %lu\n", (DWORD)((const BYTE *)s - (const BYTE *)rec));
    }
    printf("  data end %lu, closing length %lu\n", rec->DataOffset + rec->DataLength,
           *(DWORD *)((BYTE *)rec + rec->Length - 4));
}

static BOOL read_one(HANDLE log, DWORD flags, DWORD offset, BYTE **buf, DWORD *size)
{
    DWORD read, needed;

    if (ReadEventLogW(log, flags, offset, *buf, *size, &read, &needed)) return TRUE;
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return FALSE;
    *buf = realloc(*buf, *size = needed);
    return ReadEventLogW(log, flags, offset, *buf, *size, &read, &needed);
}

static void cursor(HANDLE log, const char *what, DWORD flags, DWORD offset, DWORD oldest, BYTE **buf, DWORD *size)
{
    SetLastError(0xdeadbeef);
    if (read_one(log, flags, offset, buf, size))
        printf("  %s: record oldest+%ld\n", what, (long)(((EVENTLOGRECORD *)*buf)->RecordNumber - oldest));
    else
        printf("  %s: error %lu\n", what, GetLastError());
}

static void classic(void)
{
    static const WCHAR *logs[] = { L"System", L"Application" };
    DWORD count, oldest, size = 0x10000, read, needed, i, j;
    BYTE *buf = malloc(size);
    HANDLE log;

    for (i = 0; i < ARRAYSIZE(logs); i++)
    {
        int shown = 0;

        log = OpenEventLogW(NULL, logs[i]);
        if (!log)
        {
            printf("%ls: open error %lu\n", logs[i], GetLastError());
            continue;
        }
        GetNumberOfEventLogRecords(log, &count);
        GetOldestEventLogRecord(log, &oldest);
        printf("%ls: %s records, oldest %s\n", logs[i], count ? "some" : "no", oldest ? "set" : "0");

        /* the first records, as they come, and the service's own */
        for (j = 0; j < 400 && read_one(log, EVENTLOG_SEQUENTIAL_READ | EVENTLOG_BACKWARDS_READ, 0, &buf, &size); j++)
        {
            EVENTLOGRECORD *rec = (EVENTLOGRECORD *)buf;
            char what[64];

            if (rec->EventID == 0x80001775 && !(shown & 1))
            {
                show_layout("EventLog started (6005)", rec);
                printf("  data:");
                for (read = 0; read < rec->DataLength; read++) printf(" %02x", ((BYTE *)rec + rec->DataOffset)[read]);
                printf("\n  time generated %s time written\n", rec->TimeGenerated == rec->TimeWritten ? "==" : "!=");
                shown |= 1;
            }
            else if (rec->UserSidLength && rec->NumStrings && !(shown & 2))
            {
                sprintf(what, "a record with a sid and strings, type %u", rec->EventType);
                show_layout(what, rec);
                shown |= 2;
            }
            else if (!rec->UserSidLength && rec->NumStrings && rec->DataLength && !(shown & 4))
            {
                show_layout("a record with strings and data", rec);
                shown |= 4;
            }
        }
        CloseEventLog(log);

        /* how the cursor moves */
        log = OpenEventLogW(NULL, logs[i]);
        printf("%ls cursor:\n", logs[i]);
        cursor(log, "sequential forwards (offset 100)", EVENTLOG_SEQUENTIAL_READ | EVENTLOG_FORWARDS_READ, 100, oldest, &buf, &size);
        cursor(log, "again (offset 200)", EVENTLOG_SEQUENTIAL_READ | EVENTLOG_FORWARDS_READ, 200, oldest, &buf, &size);
        cursor(log, "sequential backwards", EVENTLOG_SEQUENTIAL_READ | EVENTLOG_BACKWARDS_READ, 300, oldest, &buf, &size);
        cursor(log, "again", EVENTLOG_SEQUENTIAL_READ | EVENTLOG_BACKWARDS_READ, 400, oldest, &buf, &size);
        cursor(log, "seek 0", EVENTLOG_SEEK_READ | EVENTLOG_FORWARDS_READ, 0, oldest, &buf, &size);
        cursor(log, "seek oldest+count", EVENTLOG_SEEK_READ | EVENTLOG_FORWARDS_READ, oldest + count, oldest, &buf, &size);
        cursor(log, "seek oldest-1", EVENTLOG_SEEK_READ | EVENTLOG_FORWARDS_READ, oldest - 1, oldest, &buf, &size);
        cursor(log, "seek oldest+3 forwards", EVENTLOG_SEEK_READ | EVENTLOG_FORWARDS_READ, oldest + 3, oldest, &buf, &size);
        cursor(log, "then sequential forwards", EVENTLOG_SEQUENTIAL_READ | EVENTLOG_FORWARDS_READ, 0, oldest, &buf, &size);
        cursor(log, "seek oldest+3 backwards", EVENTLOG_SEEK_READ | EVENTLOG_BACKWARDS_READ, oldest + 3, oldest, &buf, &size);
        cursor(log, "then sequential backwards", EVENTLOG_SEQUENTIAL_READ | EVENTLOG_BACKWARDS_READ, 0, oldest, &buf, &size);
        cursor(log, "then sequential forwards", EVENTLOG_SEQUENTIAL_READ | EVENTLOG_FORWARDS_READ, 0, oldest, &buf, &size);
        /* several records in one buffer */
        SetLastError(0xdeadbeef);
        if (ReadEventLogW(log, EVENTLOG_SEEK_READ | EVENTLOG_FORWARDS_READ, oldest, buf, 0x10000 > size ? size : 0x10000,
                          &read, &needed))
        {
            DWORD n = 0, pos = 0;
            while (pos < read) { pos += ((EVENTLOGRECORD *)(buf + pos))->Length; n++; }
            printf("  seek oldest into %lu bytes: %lu records, needed %lu\n", size, n, needed);
        }
        else printf("  seek oldest into a buffer: error %lu\n", GetLastError());
        /* the last record, then past it */
        cursor(log, "seek newest", EVENTLOG_SEEK_READ | EVENTLOG_FORWARDS_READ, oldest + count - 1, oldest, &buf, &size);
        cursor(log, "then sequential forwards", EVENTLOG_SEQUENTIAL_READ | EVENTLOG_FORWARDS_READ, 0, oldest, &buf, &size);
        CloseEventLog(log);
    }
    free(buf);
}

/* prints XML with every text and attribute value replaced by its length */
static void print_masked(const WCHAR *xml)
{
    const WCHAR *p = xml;

    while (*p)
    {
        if (*p == '>' && p[1] && p[1] != '<')
        {
            const WCHAR *e = wcschr(p + 1, '<');
            int len = e ? (int)(e - p - 1) : lstrlenW(p + 1);
            printf(">[%d]", len);
            p += 1 + len;
        }
        else if (*p == '"')
        {
            const WCHAR *e = wcschr(p + 1, '"');
            int len = e ? (int)(e - p - 1) : 0;
            printf("\"[%d]\"", len);
            p = e ? e + 1 : p + 1;
        }
        else
        {
            printf("%lc", *p);
            p++;
        }
    }
    printf("\n");
}

static void modern(void)
{
    static const struct { const WCHAR *path, *query; } queries[] =
    {
        { L"System", L"*[System[Provider[@Name='EventLog'] and EventID=6005]]" },
        { L"Application", L"*[System[(Level=2 or Level=3 or Level=4)]]" },
    };
    EVT_HANDLE query, event, context;
    DWORD returned, used, props, i;
    WCHAR *xml;

    for (i = 0; i < ARRAYSIZE(queries); i++)
    {
        query = EvtQuery(NULL, queries[i].path, queries[i].query, EvtQueryChannelPath | EvtQueryReverseDirection);
        if (!query)
        {
            printf("EvtQuery %ls: error %lu\n", queries[i].path, GetLastError());
            continue;
        }
        if (!EvtNext(query, 1, &event, INFINITE, 0, &returned))
        {
            printf("EvtNext %ls: error %lu\n", queries[i].path, GetLastError());
            EvtClose(query);
            continue;
        }
        used = 0;
        EvtRender(NULL, event, EvtRenderEventXml, 0, NULL, &used, &props);
        xml = malloc(used);
        if (EvtRender(NULL, event, EvtRenderEventXml, used, xml, &used, &props))
        {
            printf("%ls XML (%lu bytes):\n", queries[i].path, used);
            print_masked(xml);
        }
        free(xml);
        context = EvtCreateRenderContext(0, NULL, EvtRenderContextSystem);
        used = 0;
        EvtRender(context, event, EvtRenderEventValues, 0, NULL, &used, &props);
        {
            EVT_VARIANT *values = malloc(used);
            if (EvtRender(context, event, EvtRenderEventValues, used, values, &used, &props))
            {
                printf("%ls system values: %lu, types", queries[i].path, props);
                for (returned = 0; returned < props; returned++) printf(" %lu", values[returned].Type);
                printf("\n");
            }
            free(values);
        }
        EvtClose(context);
        EvtClose(event);
        EvtClose(query);
    }
}

static void registry(void)
{
    static const WCHAR *keys[] = { L"Application", L"System" };
    WCHAR name[128], path[256];
    BYTE data[512];
    DWORD i, j, len, size, type;
    HKEY key;

    for (i = 0; i < ARRAYSIZE(keys); i++)
    {
        swprintf(path, ARRAYSIZE(path), L"SYSTEM\\CurrentControlSet\\Services\\EventLog\\%ls", keys[i]);
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &key)) continue;
        printf("%ls values:", keys[i]);
        for (j = 0; len = ARRAYSIZE(name), size = sizeof(data),
             !RegEnumValueW(key, j, name, &len, NULL, &type, data, &size); j++)
        {
            printf(" %ls(%lu", name, type);
            if (type == REG_DWORD) printf("=%lu", *(DWORD *)data);
            else if ((type == REG_SZ || type == REG_EXPAND_SZ) && !wcsncmp(name, L"File", 4)) printf("=%ls", (WCHAR *)data);
            printf(")");
        }
        printf("\n");
        RegCloseKey(key);
    }
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    classic();
    modern();
    registry();
    return 0;
}
