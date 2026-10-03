/* evtmsg: what EvtFormatMessage gives for classic events on Windows 11 -- the message, level, task and keyword
 * names in the user's language and in English, their terminators, a message by its number, the rendering added to
 * the XML -- and the messages of the Event log service's events in netevent.dll.  Formats the System log's
 * EventLog events (6005, 6006, 6009) and the first classic error and warning; read-only.  The XML is printed from
 * the end of the System element on, with the event's binary data replaced by its length. */
#include <windows.h>
#include <winevt.h>
#include <stdio.h>

static void escaped(const WCHAR *p, DWORD n)
{
    DWORD i;

    for (i = 0; i < n; i++)
    {
        if (!wcsncmp(p + i, L"<Binary>", 8))
        {
            const WCHAR *end = wcsstr(p + i, L"</Binary>");
            if (end)
            {
                printf("<Binary>(%u digits)</Binary>", (unsigned)(end - (p + i + 8)));
                i = end + 9 - p - 1;
                continue;
            }
        }
        if (!p[i]) printf("\\0");
        else if (p[i] == '\r') printf("\\r");
        else if (p[i] == '\n') printf("\\n");
        else if (p[i] == '\\') printf("\\\\");
        else if (p[i] >= 0x20 && p[i] < 0x7f) printf("%c", p[i]);
        else printf("\\u%04x", p[i]);
    }
}

static EVT_HANDLE first(const WCHAR *query)
{
    EVT_HANDLE results, event = NULL;
    DWORD returned;

    results = EvtQuery(NULL, L"System", query, EvtQueryChannelPath | EvtQueryReverseDirection);
    if (results && !EvtNext(results, 1, &event, INFINITE, 0, &returned)) event = NULL;
    if (results) EvtClose(results);
    return event;
}

static EVT_HANDLE eventlog_event(DWORD id)
{
    WCHAR query[128];

    swprintf(query, 128, L"*[System[Provider[@Name='EventLog'] and EventID=%lu]]", id);
    return first(query);
}

static void netevent(void)
{
    static const DWORD ids[] = { 6005, 6006, 6008, 6009, 6011, 6013 };
    static const LANGID langs[] = { 0x409, 0x804 };
    HMODULE module = LoadLibraryExW(L"netevent.dll", NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    DWORD i, j;

    printf("netevent.dll: %s\n", module ? "loaded" : "not loaded");
    for (i = 0; module && i < ARRAYSIZE(ids); i++)
    {
        for (j = 0; j < ARRAYSIZE(langs); j++)
        {
            WCHAR *text = NULL;

            if (!FormatMessageW(FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS,
                                module, 0x80000000 | ids[i], langs[j], (WCHAR *)&text, 0, NULL))
            {
                printf("  %08lx %04x: error %lu\n", 0x80000000 | ids[i], langs[j], GetLastError());
                continue;
            }
            printf("  %08lx %04x: ", 0x80000000 | ids[i], langs[j]);
            escaped(text, wcslen(text));
            printf("\n");
            LocalFree(text);
        }
    }
    if (module) FreeLibrary(module);
}

static void format(DWORD id, LCID locale)
{
    static const DWORD flags[] = { EvtFormatMessageEvent, EvtFormatMessageLevel, EvtFormatMessageTask,
                                   EvtFormatMessageKeyword, EvtFormatMessageOpcode, EvtFormatMessageChannel,
                                   EvtFormatMessageProvider };
    static WCHAR buffer[8192];
    EVT_HANDLE event, publisher;
    DWORD used, i;
    BOOL ret;

    if (!(event = eventlog_event(id))) { printf("%lu: no event\n", id); return; }
    publisher = EvtOpenPublisherMetadata(NULL, L"EventLog", NULL, locale, 0);
    for (i = 0; i < ARRAYSIZE(flags); i++)
    {
        memset(buffer, 0xcc, sizeof(buffer));
        used = 0;
        ret = EvtFormatMessage(publisher, event, 0, 0, NULL, flags[i], ARRAYSIZE(buffer), buffer, &used);
        printf("%lu %04lx format %lu: %d error %lu used %lu [", id, locale, flags[i], ret, ret ? 0 : GetLastError(),
               ret ? used : 0);
        if (ret) escaped(buffer, used);
        printf("]\n");
    }
    used = 0;
    ret = EvtFormatMessage(publisher, event, 0, 0, NULL, EvtFormatMessageEvent, 0, NULL, &used);
    printf("%lu %04lx size query: %d error %lu used %lu\n", id, locale, ret, ret ? 0 : GetLastError(), used);
    if (id == 6005)
    {
        WCHAR *p;

        ret = EvtFormatMessage(publisher, event, 0, 0, NULL, EvtFormatMessageXml, ARRAYSIZE(buffer), buffer, &used);
        printf("%lu %04lx xml: %d error %lu\n  ", id, locale, ret, ret ? 0 : GetLastError());
        if (ret && (p = wcsstr(buffer, L"</System>"))) escaped(p + 9, used - (p + 9 - buffer));
        printf("\n");
        ret = EvtFormatMessage(publisher, NULL, 0x80001775, 0, NULL, EvtFormatMessageId, ARRAYSIZE(buffer), buffer, &used);
        printf("%04lx id 80001775: %d error %lu used %lu [", locale, ret, ret ? 0 : GetLastError(), ret ? used : 0);
        if (ret) escaped(buffer, used);
        printf("]\n");
        ret = EvtFormatMessage(publisher, NULL, 0x80001779, 0, NULL, EvtFormatMessageId, ARRAYSIZE(buffer), buffer, &used);
        printf("%04lx id 80001779: %d error %lu used %lu [", locale, ret, ret ? 0 : GetLastError(), ret ? used : 0);
        if (ret) escaped(buffer, used);
        printf("]\n");
    }
    EvtClose(publisher);
    if (id == 6005 && !locale)
    {
        WCHAR *p;

        ret = EvtFormatMessage(NULL, event, 0, 0, NULL, EvtFormatMessageXml, ARRAYSIZE(buffer), buffer, &used);
        printf("6005 xml without publisher: %d error %lu\n  ", ret, ret ? 0 : GetLastError());
        if (ret && (p = wcsstr(buffer, L"</System>"))) escaped(p + 9, used - (p + 9 - buffer));
        printf("\n");
    }
    EvtClose(event);
}

int main(void)
{
    static const WCHAR *levels[] = { L"*[System[Level=2 and band(Keywords,36028797018963968)]]",
                                     L"*[System[Level=3 and band(Keywords,36028797018963968)]]" };
    WCHAR buffer[256];
    EVT_HANDLE event;
    DWORD used, i;
    BOOL ret;

    setvbuf(stdout, NULL, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    printf("ui language %04x\n", GetUserDefaultUILanguage());
    netevent();
    format(6005, 0);
    format(6005, 0x409);
    format(6009, 0);
    format(6009, 0x409);
    format(6006, 0x804);
    for (i = 0; i < ARRAYSIZE(levels); i++)
    {
        if (!(event = first(levels[i]))) { printf("level %lu: no classic event\n", i + 2); continue; }
        ret = EvtFormatMessage(NULL, event, 0, 0, NULL, EvtFormatMessageLevel, ARRAYSIZE(buffer), buffer, &used);
        printf("level %lu, no publisher: %d error %lu used %lu [", i + 2, ret, ret ? 0 : GetLastError(), ret ? used : 0);
        if (ret) escaped(buffer, used);
        printf("]\n");
        EvtClose(event);
    }
    return 0;
}
