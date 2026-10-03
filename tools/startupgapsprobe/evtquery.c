/* evtquery: what the event log's XPath selects on Windows 11 -- positions, != < > with them, numbers compared with
 * strings, tests of whether an element is there -- as how many of the System log's EventLog "version" events
 * (6009) each query selects, relative to how many there are ("base"; "twice base" when the 6005 events are
 * selected too).  Read-only; prints counts relative to each other, no event data. */
#include <windows.h>
#include <winevt.h>
#include <stdio.h>

static long count(const WCHAR *text, DWORD *error)
{
    EVT_HANDLE results, events[16];
    DWORD returned, i;
    long total = 0;

    *error = 0;
    if (!(results = EvtQuery(NULL, L"System", text, EvtQueryChannelPath)))
    {
        *error = GetLastError();
        return -1;
    }
    while (EvtNext(results, 16, events, INFINITE, 0, &returned))
    {
        total += returned;
        for (i = 0; i < returned; i++) EvtClose(events[i]);
    }
    if (GetLastError() != ERROR_NO_MORE_ITEMS) *error = GetLastError();
    EvtClose(results);
    return total;
}

int main(void)
{
    static const WCHAR *queries[] =
    {
        L"*[System[EventID=6009]]",
        L"*[System[Provider[@Name='EventLog'] and EventID=6005]]",
        L"*[System[EventID=6009] and EventData[Data]]",
        L"*[System[EventID=6009] and EventData[Data[1]]]",
        L"*[System[EventID=6009] and EventData[Data[2]]]",
        L"*[System[EventID=6009] and EventData[Data[3]]]",
        L"*[System[EventID=6009] and EventData[Data[5]]]",
        L"*[System[EventID=6009] and EventData[Data[6]]]",
        L"*[System[EventID=6009] and EventData[Data[4]='Multiprocessor Free']]",
        L"*[System[EventID=6009] and EventData[Data='Multiprocessor Free']]",
        L"*[System[EventID=6009] and EventData[Data='Uniprocessor Free']]",
        L"*[System[EventID=6009] and EventData[Data='Multiprocessor Free' or Data='Uniprocessor Free']]",
        L"*[System[EventID=6009] and EventData[(Data='Multiprocessor Free') or (Data='Uniprocessor Free')]]",
        L"*[System[EventID=6009] and (EventData[Data='Multiprocessor Free'] or EventData[Data='Uniprocessor Free'])]",
        L"*[System[EventID=6009] and EventData[Data='multiprocessor free']]",
        L"*[System[EventID=6009] and EventData[Data='0']]",
        L"*[System[EventID=6009] and EventData[Data='']]",
        L"*[System[EventID=6009] and EventData[Data[3]='']]",
        L"*[System[EventID=6009] and EventData[Data[2]!='']]",
        L"*[System[EventID=6009 or EventID=6005]]",
        L"*[System[(EventID=6009 or EventID=6005) and Provider[@Name='EventLog']]]",
        L"*[System[EventID=6009] and EventData[Data[4]]]",
        L"*[System[EventID=6009] and EventData[Data[position()=1]]]",
        L"*[System[EventID=6009] and EventData[Data[position()=2]]]",
        L"*[System[EventID=6009] and EventData/Data[2]]",
        L"*[System[EventID=6009] and EventData/Data]",
        L"*[System[EventID=6009] and EventData[Data[0]]]",
        L"*[System[EventID=6009] and EventData[Data[1]='10.00.']]",
        L"*[System[EventID=6009] and EventData[Data[2]='29671']]",
        L"*[System[EventID=6009] and EventData[Data[2]='10.00.']]",
        L"*[System[EventID=6009] and EventData[Data[5]='0']]",
        L"*[System[EventID=6009] and EventData[Data[3]='Multiprocessor Free']]",
        L"*[System[EventID=6009] and EventData[Data[1]='Multiprocessor Free']]",
        L"*[System[EventID=6009] and EventData[Data[1]='']]",
        L"*[System[EventID=6009] and EventData[Data[6]='']]",
        L"*[System[EventID=6009] and EventData[Data='29671']]",
        L"*[System[EventID=6009] and EventData[Data='10.00.']]",
        L"*[System[EventID=6009] and EventData[Data!='']]",
        L"*[System[EventID=6009] and EventData[Data!='x']]",
        L"*[System[EventID=6009] and EventData[Data[1]!='']]",
        L"*[System[EventID=6009] and EventData[Data[1]!='x']]",
        L"*[System[EventID=6009] and EventData[Data[2]!='x']]",
        L"*[System[EventID=6009] and EventData[Data[4]!='x']]",
        L"*[System[EventID=6009] and EventData[Data[4]!='Multiprocessor Free']]",
        L"*[System[EventID=6009] and EventData[Data!='Multiprocessor Free']]",
        L"*[System[EventID=6009] and EventData[Data[3]!='']]",
        L"*[System[EventID=6009] and EventData[Data[3]!='x']]",
        L"*[System[EventID=6009] and EventData[Data[5]!='x']]",
        L"*[System[EventID=6009] and EventData[Data[5]=0]]",
        L"*[System[EventID=6009] and EventData[Data[2]=29671]]",
        L"*[System[EventID=6009] and EventData[Data[2]>1000]]",
        L"*[System[EventID=6009] and EventData[Data>1000]]",
        L"*[System[EventID=6009] and EventData[Data=0]]",
        L"*[System[EventID=6009] and EventData[Data<1]]",
        L"*[System[EventID=6009] and System[Correlation]]",
        L"*[System[EventID=6009] and System[Security]]",
        L"*[System[EventID=6009] and System[Nothing]]",
        L"*[System[EventID=6009] and System[Security[@UserID]]]",
        L"*[System[EventID=6009] and System[Execution[@ProcessID]]]",
        L"*[System[EventID=6009] and System[Execution[@ThreadID=0]]]",
        L"*[System[EventID=6009] and System[TimeCreated]]",
        L"*[System[EventID=6009] and System[EventRecordID]]",
        L"*[System[EventID=6009] and System[EventRecordID>0]]",
        L"*[System[EventID=6009] and System[EventID[1]=6009]]",
        L"*[System[EventID=6009] and System[EventID[2]=6009]]",
        L"*[System[EventID=6009] and System[Provider[@Guid]]]",
        L"*[System[EventID=6009] and System[Provider[@EventSourceName]]]",
        L"*[System[EventID=6009] and System[Correlation[@ActivityID]]]",
        L"*[System[EventID=6009] and EventData[Data[3]>1000]]",
        L"*[System[EventID=6009] and EventData[Data[5]>1000]]",
        L"*[System[EventID=6009] and EventData[Data[1]>1000]]",
        L"*[System[EventID=6009] and EventData[Data[3]<100000]]",
        L"*[System[EventID=6009] and EventData[Data[3]>=29671]]",
        L"*[System[EventID=6009] and EventData[Data[3]<=29671]]",
        L"*[System[EventID=6009] and EventData[Data[2]>=29671]]",
        L"*[System[EventID=6009] and EventData[Data[2]<100000]]",
        L"*[System[EventID=6009] and EventData[Data[7]!='x']]",
        L"*[System[EventID=6009] and EventData[Data[9]!='x']]",
        L"*[System[EventID=6009] and EventData[Data[11]!='x']]",
        L"*[System[EventID=6009] and EventData[Data[7]!='Multiprocessor Free']]",
        L"*[System[EventID=6009] and EventData[Data[9]!='0']]",
        L"*[System[EventID=6009] and EventData[Data[3]!='29671']]",
        L"*[System[EventID=6009] and EventData[Data[5]!='']]",
        L"*[System[EventID=6009] and EventData[Data[6]!='x']]",
        L"*[System[EventID=6009] and EventData[Data[position()=2]='29671']]",
        L"*[System[EventID=6009] and EventData[Data[position()=3]!='29671']]",
        L"*[System[EventID=6009] and EventData[Data[position()>1]]]",
        L"*[System[EventID=6009] and EventData[Data[position()=4]='Multiprocessor Free']]",
        L"*[System[EventID=6009] and EventData[Data[position()=1]='10.00.']]",
        L"*[System[EventID=6009] and EventData[Data[1] and Data[2]]]",
        L"*[System[EventID=6009] and EventData[Data[2] or Data[1]]]",
        L"*[System[EventID=6009] and EventData[Data[1] and Data[1]]]",
        L"*[System[EventID=6009] and EventData[Data[1]][EventData[Data[1]]]]",
        L"*[System[EventID=6009] and EventData[Data[2]='29671' and Data[4]='Multiprocessor Free']]",
        L"*[System[EventID=6009] and EventData[Data[4]='Multiprocessor Free' and Data[2]='29671']]",
        L"*[System[EventID=6009] and EventData[Data[2]='29671' or Data[2]='x']]",
        L"*[System[EventID=6009] and EventData[Data[3]='' and Data[3]!='']]",
        L"*[System[EventID=6009] and EventData[Data[1]!='10.00.']]",
        L"*[System[EventID=6009] and EventData[Data[5]!='0']]",
        L"*[System[EventID=6009] and EventData[Data[3]>29670]]",
        L"*[System[EventID=6009] and EventData[Data[3]>29671]]",
        L"*[System[EventID=6009] and EventData[Data[3]<29672]]",
        L"*[System[EventID=6009] and EventData[Data[3]<29671]]",
        L"*[System[EventID=6009] and EventData[Data[1]<1000]]",
        L"*[System[EventID=6009] and EventData[Data[1]=10]]"
    };
    DWORD error, i;
    long base;

    setvbuf(stdout, NULL, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

    base = count(L"*[System[Provider[@Name='EventLog'] and EventID=6009]]", &error);
    printf("base: %s error %lu\n", base > 0 ? "some" : "none", error);
    for (i = 0; i < ARRAYSIZE(queries); i++)
    {
        long n = count(queries[i], &error);
        if (n < 0) printf("%2lu: EvtQuery error %lu  %ls\n", i, error, queries[i]);
        else printf("%2lu: base%+ld%s error %lu  %ls\n", i, n - base, n == 2 * base ? " (twice base)" : "", error, queries[i]);
    }
    return 0;
}
