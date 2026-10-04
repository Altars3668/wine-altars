#ifndef PREFIXUPDATEPROBE_H
#define PREFIXUPDATEPROBE_H

#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define PROBE_SENTINEL 0xdeadbeefUL

/* 看门狗只结束本探针；超时不是测量成功。 */
static HANDLE probe_stop_event, probe_watchdog_thread;
static CRITICAL_SECTION probe_scene_cs;
static BOOL probe_scene_active;
static DWORD probe_scene_deadline;

static inline void probe_scene_begin(DWORD timeout_ms)
{
    EnterCriticalSection(&probe_scene_cs);
    probe_scene_deadline = GetTickCount() + timeout_ms;
    probe_scene_active = TRUE;
    LeaveCriticalSection(&probe_scene_cs);
}

static inline void probe_scene_end(void)
{
    EnterCriticalSection(&probe_scene_cs);
    probe_scene_active = FALSE;
    LeaveCriticalSection(&probe_scene_cs);
}

static DWORD WINAPI probe_watchdog(void *unused)
{
    DWORD begin = GetTickCount(), now;
    BOOL scene_timeout;
    (void)unused;
    while (WaitForSingleObject(probe_stop_event, 100) == WAIT_TIMEOUT)
    {
        now = GetTickCount();
        EnterCriticalSection(&probe_scene_cs);
        scene_timeout = probe_scene_active && (LONG)(now - probe_scene_deadline) >= 0;
        LeaveCriticalSection(&probe_scene_cs);
        if (scene_timeout || now - begin >= 27000)
        {
            printf("safety_timeout=1 scene_timeout=%u exit=124\ndone\n", scene_timeout);
            ExitProcess(124);
        }
    }
    return 0;
}

static inline BOOL probe_start(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    InitializeCriticalSection(&probe_scene_cs);
    probe_stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (probe_stop_event)
        probe_watchdog_thread = CreateThread(NULL, 0, probe_watchdog, NULL, 0, NULL);
    if (!probe_stop_event || !probe_watchdog_thread)
    {
        printf("watchdog_setup_failed gle=%lu\ndone\n", GetLastError());
        if (probe_stop_event) CloseHandle(probe_stop_event);
        DeleteCriticalSection(&probe_scene_cs);
        return FALSE;
    }
    return TRUE;
}

static inline int probe_done(int status)
{
    SetEvent(probe_stop_event);
    WaitForSingleObject(probe_watchdog_thread, 1000);
    CloseHandle(probe_watchdog_thread);
    CloseHandle(probe_stop_event);
    DeleteCriticalSection(&probe_scene_cs);
    puts("done");
    return status;
}

static inline BOOL probe_random(DWORD *data, ULONG bytes)
{
    NTSTATUS status = BCryptGenRandom(NULL, (BYTE *)data, bytes,
                                     BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status < 0) printf("random_failed ntstatus=%#lx\n", (ULONG)status);
    return status >= 0;
}

/* 参数完全由探针生成；继承日志句柄，不新建控制台或显示路径。 */
static inline DWORD probe_child(const WCHAR *arguments, DWORD timeout_ms)
{
    WCHAR executable[32768], command[33280];
    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    DWORD length, wait, exit_code = PROBE_SENTINEL, error;
    BOOL ok;

    length = GetModuleFileNameW(NULL, executable, ARRAYSIZE(executable));
    if (!length || length >= ARRAYSIZE(executable))
    {
        printf("child_executable_unavailable gle=%lu\n", GetLastError());
        return 125;
    }
    swprintf(command, ARRAYSIZE(command), L"\"%ls\" %ls", executable, arguments);
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    SetLastError(PROBE_SENTINEL);
    ok = CreateProcessW(executable, command, NULL, NULL, TRUE, CREATE_NO_WINDOW,
                        NULL, NULL, &startup, &process);
    error = GetLastError();
    printf("child_create=%u gle=%lu timeout_ms=%lu\n", ok, error, timeout_ms);
    if (!ok) return 125;
    wait = WaitForSingleObject(process.hProcess, timeout_ms);
    if (wait != WAIT_OBJECT_0)
    {
        printf("child_wait=%lu child_timeout_or_wait_failure=1\n", wait);
        TerminateProcess(process.hProcess, 124);
        WaitForSingleObject(process.hProcess, 1500);
    }
    GetExitCodeProcess(process.hProcess, &exit_code);
    printf("child_exit=%lu\n", exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exit_code;
}

#endif
