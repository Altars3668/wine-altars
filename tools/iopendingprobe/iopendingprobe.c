/*
 * iopendingprobe - what ThreadIsIoPending says of a thread.
 *
 * Office's thread pool asks it of its threads, and Wine answered FALSE
 * from a stub.  Windows keeps the I/O a thread issued on the thread, and
 * cancels it if the thread exits; a pool that retires threads must not
 * retire one with I/O still pending.  This asks of a thread with nothing
 * pending, one with an overlapped read pending on a pipe, the same after
 * the read completes, one with a read pending on a handle bound to a
 * completion port, one blocked in a synchronous read, one with a pending
 * read it cancelled, and through handles with less access.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>

static NTSTATUS (WINAPI *pNtQueryInformationThread)(HANDLE, THREADINFOCLASS, void *, ULONG, ULONG *);

struct job
{
    HANDLE pipe;        /* the end to read from */
    BOOL overlapped;
    BOOL cancel;
    HANDLE issued, done;
    OVERLAPPED ov;
    char buf[16];
};

static void ask(const char *what, HANDLE thread)
{
    ULONG value = 0xdead, len = 0xdead;
    NTSTATUS status;

    status = pNtQueryInformationThread(thread, 16 /* ThreadIsIoPending */, &value, sizeof(value), &len);
    printf("%-44s status %#lx value %lu len %lu\n", what, status, value, len);
}

static DWORD WINAPI reader(void *arg)
{
    struct job *job = arg;
    DWORD read;

    if (job->overlapped)
    {
        job->ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
        if (!ReadFile(job->pipe, job->buf, sizeof(job->buf), &read, &job->ov) && GetLastError() != ERROR_IO_PENDING)
            printf("ReadFile failed %lu\n", GetLastError());
        if (job->cancel) CancelIo(job->pipe);
        SetEvent(job->issued);
        WaitForSingleObject(job->done, INFINITE);
    }
    else
    {
        SetEvent(job->issued);
        ReadFile(job->pipe, job->buf, sizeof(job->buf), &read, NULL);
        WaitForSingleObject(job->done, INFINITE);
    }
    return 0;
}

static void pair(HANDLE *server, HANDLE *client, BOOL overlapped)
{
    static int n;
    char name[64];

    sprintf(name, "\\\\.\\pipe\\iopendingprobe%lu_%d", GetCurrentProcessId(), n++);
    *server = CreateNamedPipeA(name, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE, 1, 64, 64, 0, NULL);
    *client = CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                          overlapped ? FILE_FLAG_OVERLAPPED : 0, NULL);
}

static void run(const char *what, BOOL overlapped, BOOL port, BOOL cancel)
{
    struct job job = {0};
    HANDLE server, thread, limited, iocp = NULL;
    DWORD written;
    char line[80];

    pair(&server, &job.pipe, overlapped);
    if (port) iocp = CreateIoCompletionPort(job.pipe, NULL, 1, 0);
    job.overlapped = overlapped;
    job.cancel = cancel;
    job.issued = CreateEventA(NULL, TRUE, FALSE, NULL);
    job.done = CreateEventA(NULL, TRUE, FALSE, NULL);
    thread = CreateThread(NULL, 0, reader, &job, 0, NULL);
    WaitForSingleObject(job.issued, INFINITE);
    Sleep(200);
    sprintf(line, "%s", what);
    ask(line, thread);
    if (!cancel)
    {
        DuplicateHandle(GetCurrentProcess(), thread, GetCurrentProcess(), &limited,
                        THREAD_QUERY_LIMITED_INFORMATION, FALSE, 0);
        sprintf(line, "%s, limited access", what);
        ask(line, limited);
        CloseHandle(limited);
        WriteFile(server, "x", 1, &written, NULL);
        Sleep(200);
        sprintf(line, "%s, completed", what);
        ask(line, thread);
    }
    SetEvent(job.done);
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    CloseHandle(job.pipe);
    CloseHandle(server);
    if (iocp) CloseHandle(iocp);
}

int main(void)
{
    ULONG value, len;
    NTSTATUS status;

    setvbuf(stdout, NULL, _IONBF, 0);
    pNtQueryInformationThread = (void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");

    ask("this thread, nothing pending", GetCurrentThread());
    run("an overlapped read pending", TRUE, FALSE, FALSE);
    run("an overlapped read pending, completion port", TRUE, TRUE, FALSE);
    run("a synchronous read blocked", FALSE, FALSE, FALSE);
    run("an overlapped read cancelled", TRUE, FALSE, TRUE);

    status = pNtQueryInformationThread(GetCurrentThread(), 16, &value, sizeof(value) + 1, &len);
    printf("5 bytes: status %#lx\n", status);
    status = pNtQueryInformationThread((HANDLE)0xdeadbeef, 16, &value, sizeof(value), &len);
    printf("a bogus handle: status %#lx\n", status);
    status = pNtQueryInformationThread(GetCurrentThread(), 16, NULL, sizeof(value), &len);
    printf("NULL buffer: status %#lx\n", status);
    printf("done\n");
    return 0;
}
