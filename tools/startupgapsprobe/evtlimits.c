/* evtlimits: how deep and how long an event log XPath query may be on Windows 11 -- parentheses within each
 * other, predicates within each other, operands joined by "or" and by "and" -- in small steps around the limits.
 * Do not grow these: a query of 5000 operands makes the Windows Event Log service crash (RPC 1726, then 1722
 * until the service control manager restarts it), so the probe stops at the first RPC error.  Read-only; prints
 * whether each query is taken and whether it selects anything. */
#include <windows.h>
#include <winevt.h>
#include <stdio.h>
#include <stdlib.h>

static void try(const char *what, int n, const WCHAR *text)
{
    EVT_HANDLE results;
    EVT_HANDLE events[16];
    DWORD returned, total = 0, i;

    SetLastError(0xdeadbeef);
    results = EvtQuery(NULL, L"System", text, EvtQueryChannelPath);
    if (!results)
    {
        DWORD error = GetLastError();
        printf("%s %d: EvtQuery error %lu\n", what, n, error);
        if (error == RPC_S_CALL_FAILED || error == RPC_S_SERVER_UNAVAILABLE) { printf("stopping\n"); exit(1); }
        return;
    }
    while (EvtNext(results, 16, events, INFINITE, 0, &returned))
    {
        total += returned;
        for (i = 0; i < returned; i++) EvtClose(events[i]);
    }
    printf("%s %d: %s, then error %lu\n", what, n, total ? "some" : "none", GetLastError());
    EvtClose(results);
}

int main(void)
{
    static const int depths[] = { 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30 };
    static const int lengths[] = { 10, 15, 18, 19, 20, 21, 22, 23, 24, 25, 26, 28, 30, 40 };
    WCHAR *text = malloc(1 << 22);
    int i, j;

    setvbuf(stdout, NULL, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    for (i = 0; i < ARRAYSIZE(depths); i++)
    {
        WCHAR *p = text;
        p += swprintf(p, 64, L"*[System[");
        for (j = 0; j < depths[i]; j++) *p++ = '(';
        p += swprintf(p, 64, L"EventID=6005");
        for (j = 0; j < depths[i]; j++) *p++ = ')';
        wcscpy(p, L"]]");
        try("parentheses", depths[i], text);
    }
    for (i = 0; i < ARRAYSIZE(depths); i++)
    {
        WCHAR *p = text;
        p += swprintf(p, 64, L"*");
        for (j = 0; j < depths[i]; j++) p += swprintf(p, 64, L"[System");
        for (j = 0; j < depths[i]; j++) *p++ = ']';
        *p = 0;
        try("nested predicates", depths[i], text);
    }
    for (i = 0; i < ARRAYSIZE(lengths); i++)
    {
        WCHAR *p = text;
        p += swprintf(p, 64, L"*[System[");
        for (j = 0; j < lengths[i]; j++) p += swprintf(p, 64, L"EventID=%d or ", 100000 + j);
        wcscpy(p, L"EventID=6005]]");
        try("or chain", lengths[i], text);
    }
    for (i = 0; i < ARRAYSIZE(lengths); i++)
    {
        WCHAR *p = text;
        p += swprintf(p, 64, L"*[System[");
        for (j = 0; j < lengths[i]; j++) p += swprintf(p, 64, L"EventID!=%d and ", 100000 + j);
        wcscpy(p, L"EventID=6005]]");
        try("and chain", lengths[i], text);
    }
    free(text);
    return 0;
}
