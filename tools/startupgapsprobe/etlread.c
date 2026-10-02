/* etlread: what a log file holds, by provider: how many events, and the names of the TraceLogging ones; with the
 * header OpenTrace gives.  No payloads are printed.
 *
 *     etlread.exe file.etl */
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <stdio.h>

static struct { GUID guid; unsigned count; char names[512]; } provs[64];
static unsigned nprovs, total, ext_tl, buffers;

static void WINAPI cb(EVENT_RECORD *r)
{
    unsigned i;
    total++;
    for (i = 0; i < nprovs; i++) if (IsEqualGUID(&provs[i].guid, &r->EventHeader.ProviderId)) break;
    if (i == nprovs && nprovs < 64) provs[nprovs++].guid = r->EventHeader.ProviderId;
    if (i == 64) return;
    provs[i].count++;
    for (USHORT j = 0; j < r->ExtendedDataCount; j++)
    {
        if (r->ExtendedData[j].ExtType != 11 /* EVENT_HEADER_EXT_TYPE_EVENT_SCHEMA_TL */) continue;
        {
            const BYTE *meta = (const BYTE *)(ULONG_PTR)r->ExtendedData[j].DataPtr;
            ULONG pos = 2, size = r->ExtendedData[j].DataSize;
            ext_tl++;
            while (pos < size && (meta[pos] & 0x80)) pos++;
            pos++;
            if (pos < size && strlen(provs[i].names) + strlen((const char *)meta + pos) + 2 < sizeof(provs[i].names) &&
                !strstr(provs[i].names, (const char *)meta + pos))
            {
                strcat(provs[i].names, " ");
                strcat(provs[i].names, (const char *)meta + pos);
            }
        }
    }
}

static ULONG WINAPI bufcb(EVENT_TRACE_LOGFILEW *l) { buffers++; return TRUE; }

int wmain(int argc, WCHAR **argv)
{
    EVENT_TRACE_LOGFILEW lf = { 0 };
    TRACEHANDLE h;
    ULONG ret;
    unsigned i;

    lf.LogFileName = argv[1];
    lf.ProcessTraceMode = PROCESS_TRACE_MODE_EVENT_RECORD;
    lf.EventRecordCallback = cb;
    lf.BufferCallback = bufcb;
    h = OpenTraceW(&lf);
    if (h == INVALID_PROCESSTRACE_HANDLE) { printf("OpenTrace failed %lu\n", GetLastError()); return 1; }
    printf("header: buffer %lu, mode %#lx, buffers written %lu, events lost %lu, clock %lu, max file %lu, pointer %lu\n",
           lf.LogfileHeader.BufferSize, lf.LogfileHeader.LogFileMode, lf.LogfileHeader.BuffersWritten,
           lf.LogfileHeader.EventsLost, lf.LogfileHeader.ReservedFlags, lf.LogfileHeader.MaximumFileSize,
           lf.LogfileHeader.PointerSize);
    ret = ProcessTrace(&h, 1, NULL, NULL);
    printf("ProcessTrace %lu: %u events in %u buffers, %u with a TraceLogging schema\n", ret, total, buffers, ext_tl);
    for (i = 0; i < nprovs; i++)
        printf("  {%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}: %u%s\n", provs[i].guid.Data1, provs[i].guid.Data2,
               provs[i].guid.Data3, provs[i].guid.Data4[0], provs[i].guid.Data4[1], provs[i].guid.Data4[2],
               provs[i].guid.Data4[3], provs[i].guid.Data4[4], provs[i].guid.Data4[5], provs[i].guid.Data4[6],
               provs[i].guid.Data4[7], provs[i].count, provs[i].names);
    CloseTrace(h);
    return 0;
}
