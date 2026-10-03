/* evtsub: event log subscriptions on Windows 11 -- whether the events already there set a pull subscription's
 * signal, what EvtNext waits for whatever its timeout, what it says when there is nothing more and when it is
 * asked again, whether Windows resets the signal, where and when a callback is called.  Subscribes to the System
 * log's EventLog "started" events (6005) only; read-only, prints no event data. */
#include <windows.h>
#include <winevt.h>
#include <stdio.h>

static const WCHAR started[] = L"*[System[Provider[@Name='EventLog'] and EventID=6005]]";
static ULONGLONG t0;

static const char *since(void)
{
    static char buf[32];
    ULONGLONG ms = GetTickCount64() - t0;
    if (ms < 100) return "<0.1s";
    sprintf(buf, "%.1fs", ms / 1000.0);
    return buf;
}

static void next(EVT_HANDLE sub, DWORD timeout, const char *what)
{
    EVT_HANDLE events[64];
    DWORD returned = 0xdeadbeef, i;
    BOOL ret;

    t0 = GetTickCount64();
    SetLastError(0xdeadbeef);
    ret = EvtNext(sub, 64, events, timeout, 0, &returned);
    printf("  %s: EvtNext(timeout %ld) %d error %lu returned %s after %s\n", what, (long)timeout, ret,
           ret ? 0 : GetLastError(), !ret ? "-" : returned ? "some" : "0", since());
    if (ret) for (i = 0; i < returned; i++) EvtClose(events[i]);
}

static void drain(EVT_HANDLE sub, DWORD timeout)
{
    EVT_HANDLE events[64];
    DWORD returned, i, calls = 0;
    ULONGLONG start = GetTickCount64();

    for (;;)
    {
        t0 = GetTickCount64();
        if (!EvtNext(sub, 64, events, timeout, 0, &returned)) break;
        calls++;
        for (i = 0; i < returned; i++) EvtClose(events[i]);
        if (GetTickCount64() - start > 60000) { printf("  drain: still going after 60s\n"); return; }
    }
    printf("  drain (timeout %ld): %s calls with events, then error %lu after %s\n", (long)timeout,
           calls ? "some" : "no", GetLastError(), since());
}

static LONG delivered, other_actions, other_thread;
static DWORD main_thread;

static DWORD WINAPI callback(EVT_SUBSCRIBE_NOTIFY_ACTION action, void *context, EVT_HANDLE event)
{
    if (action == EvtSubscribeActionDeliver) InterlockedIncrement(&delivered);
    else InterlockedIncrement(&other_actions);
    if (GetCurrentThreadId() != main_thread) InterlockedIncrement(&other_thread);
    return 0;
}

int main(void)
{
    EVT_HANDLE sub;
    HANDLE signal;

    setvbuf(stdout, NULL, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    main_thread = GetCurrentThreadId();

    printf("pull, oldest, signal not set:\n");
    signal = CreateEventW(NULL, TRUE, FALSE, NULL);
    sub = EvtSubscribe(NULL, signal, L"System", started, NULL, NULL, NULL, EvtSubscribeStartAtOldestRecord);
    printf("  EvtSubscribe %s error %lu\n", sub ? "ok" : "failed", sub ? 0 : GetLastError());
    if (!sub) return 1;
    printf("  signal at once %lu\n", WaitForSingleObject(signal, 0));
    Sleep(300);
    printf("  signal after 0.3s %lu\n", WaitForSingleObject(signal, 0));
    next(sub, 0, "first");
    printf("  signal %lu\n", WaitForSingleObject(signal, 0));
    drain(sub, 0);
    printf("  signal after drain %lu\n", WaitForSingleObject(signal, 0));
    next(sub, 2000, "drained");
    next(sub, INFINITE, "drained, infinite");
    EvtClose(sub);

    printf("pull, oldest, timeout 1000 first:\n");
    ResetEvent(signal);
    sub = EvtSubscribe(NULL, signal, L"System", started, NULL, NULL, NULL, EvtSubscribeStartAtOldestRecord);
    next(sub, 1000, "first");
    drain(sub, 1000);
    EvtClose(sub);

    printf("pull, oldest, at once with INFINITE:\n");
    sub = EvtSubscribe(NULL, signal, L"System", started, NULL, NULL, NULL, EvtSubscribeStartAtOldestRecord);
    next(sub, INFINITE, "first");
    drain(sub, INFINITE);
    EvtClose(sub);

    printf("pull, oldest, signal set before:\n");
    SetEvent(signal);
    sub = EvtSubscribe(NULL, signal, L"System", started, NULL, NULL, NULL, EvtSubscribeStartAtOldestRecord);
    printf("  signal at once %lu\n", WaitForSingleObject(signal, 0));
    drain(sub, INFINITE);
    printf("  signal after drain %lu\n", WaitForSingleObject(signal, 0));
    EvtClose(sub);
    printf("  signal after close %lu\n", WaitForSingleObject(signal, 0));

    printf("pull, future:\n");
    ResetEvent(signal);
    sub = EvtSubscribe(NULL, signal, L"System", started, NULL, NULL, NULL, EvtSubscribeToFutureEvents);
    printf("  EvtSubscribe %s error %lu\n", sub ? "ok" : "failed", sub ? 0 : GetLastError());
    Sleep(300);
    printf("  signal after 0.3s %lu\n", WaitForSingleObject(signal, 0));
    next(sub, 0, "none yet");
    next(sub, 1500, "none yet");
    next(sub, INFINITE, "none yet, infinite");
    EvtClose(sub);

    printf("push, oldest:\n");
    sub = EvtSubscribe(NULL, NULL, L"System", started, NULL, NULL, callback, EvtSubscribeStartAtOldestRecord);
    printf("  EvtSubscribe %s error %lu delivered so far %s\n", sub ? "ok" : "failed", sub ? 0 : GetLastError(),
           delivered ? "some" : "none");
    Sleep(3000);
    printf("  after 3s: delivered %s, other actions %ld, on another thread %s\n", delivered ? "some" : "none",
           other_actions, other_thread ? "yes" : "no");
    next(sub, 0, "push");
    EvtClose(sub);

    printf("push and signal:\n");
    sub = EvtSubscribe(NULL, signal, L"System", started, NULL, NULL, callback, EvtSubscribeStartAtOldestRecord);
    printf("  EvtSubscribe %s error %lu\n", sub ? "ok" : "failed", sub ? 0 : GetLastError());
    if (sub) EvtClose(sub);
    CloseHandle(signal);
    return 0;
}
