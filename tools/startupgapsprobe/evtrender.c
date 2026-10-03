/* evtrender: where EvtRender puts the values' data on Windows 11 -- the system, user and path contexts, the strings
 * of a classic event as an array, binary data, what is between the variants and the data, how much of the buffer
 * is written -- and which sets of render paths make a context.  Renders the System log's EventLog events (6005, 6006, 6009, 6013); read-only,
 * prints types, counts, offsets and lengths, never the values. */
#include <windows.h>
#include <winevt.h>
#include <stdio.h>

static BYTE buffer[65536];

static EVT_HANDLE first(DWORD id)
{
    EVT_HANDLE results, event = NULL;
    WCHAR query[128];
    DWORD returned;

    swprintf(query, 128, L"*[System[Provider[@Name='EventLog'] and EventID=%lu]]", id);
    results = EvtQuery(NULL, L"System", query, EvtQueryChannelPath | EvtQueryReverseDirection);
    if (results && !EvtNext(results, 1, &event, INFINITE, 0, &returned)) event = NULL;
    if (results) EvtClose(results);
    return event;
}

static void dump(const char *what, EVT_HANDLE context, EVT_HANDLE event)
{
    DWORD used = 0, count = 0, i, j;
    BOOL ret;

    ret = EvtRender(context, event, EvtRenderEventValues, 0, NULL, &used, &count);
    printf("  %s: size query %d error %lu used %lu count %lu\n", what, ret, ret ? 0 : GetLastError(), used, count);
    memset(buffer, 0xcc, sizeof(buffer));
    ret = EvtRender(context, event, EvtRenderEventValues, sizeof(buffer), buffer, &used, &count);
    printf("    render %d error %lu used %lu count %lu\n", ret, ret ? 0 : GetLastError(), used, count);
    if (!ret) return;
    for (i = 0; i < count; i++)
    {
        EVT_VARIANT *v = (EVT_VARIANT *)buffer + i;
        printf("    %lu: type %lu count %lu", i, v->Type, v->Count);
        if (v->Type == EvtVarTypeString && v->StringVal)
            printf(" at %ld len %u", (long)((BYTE *)v->StringVal - buffer), (unsigned)wcslen(v->StringVal));
        if ((v->Type == EvtVarTypeBinary || v->Type == EvtVarTypeSid) && v->BinaryVal)
            printf(" at %ld", (long)(v->BinaryVal - buffer));
        if (v->Type == (EvtVarTypeString | EVT_VARIANT_TYPE_ARRAY) && v->StringArr)
        {
            printf(" table at %ld:", (long)((BYTE *)v->StringArr - buffer));
            for (j = 0; j < v->Count; j++)
                printf(" %ld/%u", v->StringArr[j] ? (long)((BYTE *)v->StringArr[j] - buffer) : -1L,
                       v->StringArr[j] ? (unsigned)wcslen(v->StringArr[j]) : 0);
        }
        printf("\n");
    }
    /* what the bytes after the variants are: pointers into the buffer, zeros, untouched, or something else */
    if (count)
    {
        ULONG_PTR *p = (ULONG_PTR *)((EVT_VARIANT *)buffer + count);
        printf("    after the variants:");
        for (j = 0; j < 6 && (BYTE *)(p + j) < buffer + used; j++)
        {
            if (p[j] >= (ULONG_PTR)buffer && p[j] < (ULONG_PTR)buffer + used) printf(" ->%ld", (long)(p[j] - (ULONG_PTR)buffer));
            else if (p[j] == 0) printf(" 0");
            else if (p[j] == (ULONG_PTR)0xcccccccccccccccc) printf(" cc");
            else printf(" ?");
        }
        printf(", byte at used: %02x\n", buffer[used]);
    }
}

int main(void)
{
    static const DWORD ids[] = { 6005, 6006, 6009, 6013 };
    static const WCHAR *paths[] = { L"Event/System/Provider/@Name", L"Event/System/Channel", L"Event/System/Computer",
                                    L"Event/System/EventID", L"Event/EventData/Data" };
    static const WCHAR *single[] = { L"Event/EventData/Data", L"Event/EventData/Binary", L"Event/EventData",
                                     L"Event/System/Provider/@Name", L"Event/EventData/Data[@Name='x']" };
    static const WCHAR *sets[][4] =
    {
        { L"Event/EventData/Data", L"Event/EventData/Binary" },
        { L"Event/EventData/Data", L"Event/EventData" },
        { L"Event/EventData/Binary", L"Event/EventData" },
        { L"Event/EventData", L"Event/System/Provider/@Name" },
        { L"Event/EventData/Data", L"Event/EventData/Binary", L"Event/EventData" },
        { L"Event/System/Provider/@Name", L"Event/System/Provider/@Name" },
        { L"Event/System", L"Event/System/Provider/@Name" },
        { L"Event/System/Provider/@Name", L"Event/System" },
        { L"Event/System/Provider", L"Event/System/Provider/@Name" },
        { L"Event/EventData/Data", L"Event/EventData/Data" },
        { L"Event", L"Event/System/EventID" },
        { L"Event/System/EventID", L"Event/System/Level" },
    };
    EVT_HANDLE event, context;
    DWORD i, j, n, used, count;
    BOOL ret;

    setvbuf(stdout, NULL, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

    for (i = 0; i < ARRAYSIZE(ids); i++)
    {
        char name[64];

        if (!(event = first(ids[i]))) { printf("%lu: no event\n", ids[i]); continue; }
        printf("%lu:\n", ids[i]);
        context = EvtCreateRenderContext(0, NULL, EvtRenderContextUser);
        dump("user", context, event);
        EvtClose(context);
        if (ids[i] == 6009)
        {
            context = EvtCreateRenderContext(0, NULL, EvtRenderContextSystem);
            dump("system", context, event);
            EvtClose(context);
            context = EvtCreateRenderContext(ARRAYSIZE(paths), paths, EvtRenderContextValues);
            dump("five paths", context, event);
            EvtClose(context);
        }
        for (j = 0; j < ARRAYSIZE(single); j++)
        {
            sprintf(name, "%ls", single[j]);
            if (!(context = EvtCreateRenderContext(1, &single[j], EvtRenderContextValues)))
            {
                printf("  %s: context error %lu\n", name, GetLastError());
                continue;
            }
            dump(name, context, event);
            EvtClose(context);
        }
        EvtClose(event);
    }

    printf("sets of paths, rendering 6009:\n");
    if (!(event = first(6009))) return 0;
    /* how much of the buffer is written: the values, the XML */
    for (i = 0; i < 2; i++)
    {
        static const DWORD sizes[] = { 65536, 400 };
        DWORD end;

        context = EvtCreateRenderContext(0, NULL, EvtRenderContextSystem);
        memset(buffer, 0xcc, sizeof(buffer));
        ret = EvtRender(context, event, EvtRenderEventValues, sizes[i], buffer, &used, &count);
        for (end = used; end < sizeof(buffer) && buffer[end] != 0xcc; end++);
        printf("  system values into %lu bytes: %d used %lu, written up to %lu\n", sizes[i], ret, used, end);
        EvtClose(context);
    }
    memset(buffer, 0xcc, sizeof(buffer));
    ret = EvtRender(NULL, event, EvtRenderEventXml, 8000, buffer, &used, &count);
    for (n = used; n < sizeof(buffer) && buffer[n] != 0xcc; n++);
    printf("  xml into 8000 bytes: %d, written up to used: %s\n", ret, n == used ? "yes" : "no");
    for (i = 0; i < ARRAYSIZE(sets); i++)
    {
        printf("  ");
        for (n = 0; n < 4 && sets[i][n]; n++) printf("%s%ls", n ? " + " : "", sets[i][n]);
        SetLastError(0xdeadbeef);
        if (!(context = EvtCreateRenderContext(n, sets[i], EvtRenderContextValues)))
        {
            printf(": context error %lu\n", GetLastError());
            continue;
        }
        ret = EvtRender(context, event, EvtRenderEventValues, sizeof(buffer), buffer, &used, &count);
        printf(": render %d error %lu used %lu count %lu", ret, ret ? 0 : GetLastError(), used, count);
        if (ret) for (j = 0; j < count; j++) printf(" %lu/%lu", ((EVT_VARIANT *)buffer)[j].Type, ((EVT_VARIANT *)buffer)[j].Count);
        printf("\n");
        EvtClose(context);
    }
    EvtClose(event);
    return 0;
}
