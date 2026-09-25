/*
 * cycleprobe - what the cycle times Windows keeps for threads and processes
 * hold, next to the time stamp counter and the times it keeps beside them.
 *
 * Office asks for its process's cycle time hundreds of times as it starts;
 * Wine answered a stub.  This prints both fields of the process's and of
 * threads' cycle time information - one running, one asleep, one spinning -
 * with __rdtsc() read around the query, and the kernel and user times of
 * each, of this process and of a child that sleeps.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <windows.h>
#include <winternl.h>
#include <intrin.h>
#include <stdio.h>

typedef struct { ULONGLONG AccumulatedCycles, CurrentCycleCount; } CYCLES;

static NTSTATUS (WINAPI *pNtQueryInformationThread)(HANDLE, THREADINFOCLASS, void *, ULONG, ULONG *);
static NTSTATUS (WINAPI *pNtQueryInformationProcess)(HANDLE, PROCESSINFOCLASS, void *, ULONG, ULONG *);
static volatile LONG stop;

static DWORD WINAPI spin(void *arg)
{
    while (!stop) YieldProcessor();
    return 0;
}

static DWORD WINAPI sleeper(void *arg)
{
    WaitForSingleObject(arg, INFINITE);
    return 0;
}

static void thread_cycles(const char *what, HANDLE thread)
{
    ULONGLONG before, after;
    FILETIME c, e, k, u;
    CYCLES cycles;
    NTSTATUS status;

    before = __rdtsc();
    status = pNtQueryInformationThread(thread, 23 /* ThreadCycleTime */, &cycles, sizeof(cycles), NULL);
    after = __rdtsc();
    GetThreadTimes(thread, &c, &e, &k, &u);
    printf("%-18s current - rdtsc before %lld, after - current %lld, current - accumulated %lld\n", what,
           (LONGLONG)(cycles.CurrentCycleCount - before), (LONGLONG)(after - cycles.CurrentCycleCount),
           (LONGLONG)(cycles.CurrentCycleCount - cycles.AccumulatedCycles));
    printf("%-18s status %#lx: accumulated %s, current %s the time stamp counter (%s), kernel+user %llu00ns, "
           "cycles per 100ns of it %.1f\n", what, status,
           cycles.AccumulatedCycles ? "nonzero" : "zero",
           cycles.CurrentCycleCount >= before && cycles.CurrentCycleCount <= after ? "is" :
           cycles.CurrentCycleCount == cycles.AccumulatedCycles ? "equals accumulated, not" : "is not",
           cycles.CurrentCycleCount ? "nonzero" : "zero",
           (((ULONGLONG)k.dwHighDateTime << 32 | k.dwLowDateTime) + ((ULONGLONG)u.dwHighDateTime << 32 | u.dwLowDateTime)) / 1,
           (double)cycles.AccumulatedCycles / max(1, (((ULONGLONG)k.dwHighDateTime << 32 | k.dwLowDateTime)
                                                     + ((ULONGLONG)u.dwHighDateTime << 32 | u.dwLowDateTime))));
}

static ULONGLONG ft(FILETIME f) { return (ULONGLONG)f.dwHighDateTime << 32 | f.dwLowDateTime; }

int main(int argc, char **argv)
{
    HANDLE spinner, sleeping, event, self;
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    ULONGLONG before, after, c1, c2;
    FILETIME c, e, k, u, k2, u2;
    CYCLES cycles;
    NTSTATUS status;
    char cmd[MAX_PATH + 16];
    DWORD start;

    if (argc > 1) { Sleep(3000); return 0; }
    setvbuf(stdout, NULL, _IONBF, 0);
    pNtQueryInformationThread = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");
    pNtQueryInformationProcess = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationProcess");

    event = CreateEventA(NULL, TRUE, FALSE, NULL);
    spinner = CreateThread(NULL, 0, spin, NULL, 0, NULL);
    sleeping = CreateThread(NULL, 0, sleeper, event, 0, NULL);
    start = GetTickCount();
    while (GetTickCount() - start < 300) YieldProcessor();

    thread_cycles("this thread", GetCurrentThread());
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &self, 0, FALSE, DUPLICATE_SAME_ACCESS);
    thread_cycles("this, by a handle", self);
    thread_cycles("a spinning thread", spinner);
    thread_cycles("a sleeping thread", sleeping);

    before = __rdtsc();
    status = pNtQueryInformationProcess(GetCurrentProcess(), 38 /* ProcessCycleTime */, &cycles, sizeof(cycles), NULL);
    after = __rdtsc();
    GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
    printf("this process       current - rdtsc before %lld, after - current %lld, current - accumulated %lld\n",
           (LONGLONG)(cycles.CurrentCycleCount - before), (LONGLONG)(after - cycles.CurrentCycleCount),
           (LONGLONG)(cycles.CurrentCycleCount - cycles.AccumulatedCycles));
    printf("this process       status %#lx: accumulated %s, current %s the time stamp counter, "
           "cycles per 100ns of kernel+user %.1f\n", status, cycles.AccumulatedCycles ? "nonzero" : "zero",
           cycles.CurrentCycleCount >= before && cycles.CurrentCycleCount <= after ? "is" :
           cycles.CurrentCycleCount == cycles.AccumulatedCycles ? "equals accumulated, not" : "is not",
           (double)cycles.AccumulatedCycles / max(1, ft(k) + ft(u)));
    QueryProcessCycleTime(GetCurrentProcess(), &c1);
    QueryProcessCycleTime(GetCurrentProcess(), &c2);
    printf("QueryProcessCycleTime twice: %s, equals accumulated %d\n", c2 > c1 ? "grows" : "does not grow",
           c1 >= cycles.AccumulatedCycles);
    QueryThreadCycleTime(GetCurrentThread(), &c1);
    QueryThreadCycleTime(GetCurrentThread(), &c2);
    printf("QueryThreadCycleTime twice: %s\n", c2 > c1 ? "grows" : "does not grow");

    sprintf(cmd, "\"%s\" child", argv[0]);
    CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    Sleep(500);
    start = GetTickCount();
    while (GetTickCount() - start < 300) YieldProcessor();
    GetProcessTimes(pi.hProcess, &c, &e, &k, &u);
    GetProcessTimes(GetCurrentProcess(), &c, &e, &k2, &u2);
    printf("a sleeping child: user time %s this process's, kernel+user %s 100ms\n",
           ft(u) < ft(u2) ? "below" : "not below", ft(k) + ft(u) < 1000000 ? "below" : "not below");
    status = pNtQueryInformationProcess(pi.hProcess, 38, &cycles, sizeof(cycles), NULL);
    QueryProcessCycleTime(GetCurrentProcess(), &c1);
    printf("the child's cycles: status %#lx, %s this process's\n", status,
           cycles.AccumulatedCycles < c1 ? "below" : "not below");
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetProcessTimes(pi.hProcess, &c, &e, &k, &u);
    status = pNtQueryInformationProcess(pi.hProcess, 38, &cycles, sizeof(cycles), NULL);
    printf("the child, gone: exit time %s, kernel+user %s, cycles status %#lx %s\n", ft(e) ? "set" : "zero",
           ft(k) + ft(u) ? "nonzero" : "zero", status, cycles.AccumulatedCycles ? "nonzero" : "zero");
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    stop = 1;
    SetEvent(event);
    WaitForSingleObject(spinner, INFINITE);
    thread_cycles("a finished thread", spinner);
    printf("done\n");
    return 0;
}
