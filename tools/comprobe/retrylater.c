/*
 * What COM does with a call the message filter of the apartment it goes to rejects, or asks to be made again
 * later.  The main thread registers a filter that answers each incoming call with the next answer of a list it is
 * given (SERVERCALL_RETRYLATER, SERVERCALL_REJECTED, then SERVERCALL_ISHANDLED once the list runs out), and makes
 * an object; another thread, a single-threaded apartment with a filter of its own whose RetryRejectedCall answers
 * from a list too (0: retry now, 150: retry in 150 ms, -1: give up), calls IPersist::GetClassID on it, and prints
 * what the call returned, which filter methods were called with what, and roughly how long it took.  Then the
 * same from a single-threaded apartment without a filter, and from the multithreaded apartment, which cannot have
 * one; and then some of it again with the object in another process, a copy of this one started to hold it.
 */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>

static const IID iid_unknown = {0x00000000,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const IID iid_persist = {0x0000010c,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};

static DWORD server_tid, client_tid, client_pid;
static HANDLE quit_event;

/* the object */
static LONG object_refs = 1;

static HRESULT WINAPI object_QueryInterface(IPersist *iface, REFIID riid, void **obj)
{
    if (IsEqualGUID(riid, &iid_unknown) || IsEqualGUID(riid, &iid_persist))
    {
        *obj = iface;
        InterlockedIncrement(&object_refs);
        return S_OK;
    }
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI object_AddRef(IPersist *iface) { return InterlockedIncrement(&object_refs); }
static ULONG WINAPI object_Release(IPersist *iface) { return InterlockedDecrement(&object_refs); }

static HRESULT WINAPI object_GetClassID(IPersist *iface, CLSID *clsid)
{
    printf("    object GetClassID\n");
    *clsid = iid_persist;
    return S_OK;
}

static const IPersistVtbl object_vtbl = { object_QueryInterface, object_AddRef, object_Release, object_GetClassID };
static IPersist object = { &object_vtbl };

/* the filters: the called apartment's answers HandleInComingCall, the calling one's RetryRejectedCall */
static DWORD server_answers[8], server_answer_count, server_answer_index;
static LONG client_answers[8];
static DWORD client_answer_count, client_answer_index, pending_calls;

static const char *answer_name(DWORD answer)
{
    switch (answer)
    {
    case SERVERCALL_ISHANDLED: return "SERVERCALL_ISHANDLED";
    case SERVERCALL_REJECTED: return "SERVERCALL_REJECTED";
    case SERVERCALL_RETRYLATER: return "SERVERCALL_RETRYLATER";
    }
    return "?";
}

static const char *task_name(HTASK task)
{
    DWORD id = HandleToUlong(task);

    if (id == server_tid) return "the called thread";
    if (id == client_tid) return "the calling thread";
    if (id == GetCurrentProcessId()) return "the process";
    if (id == client_pid) return "the calling process";
    return id ? "something else" : "0";
}

static HRESULT WINAPI filter_QueryInterface(IMessageFilter *iface, REFIID riid, void **obj)
{
    *obj = iface;
    return S_OK;
}

static ULONG WINAPI filter_AddRef(IMessageFilter *iface) { return 2; }
static ULONG WINAPI filter_Release(IMessageFilter *iface) { return 1; }

static DWORD WINAPI server_HandleInComingCall(IMessageFilter *iface, DWORD type, HTASK caller, DWORD tick,
                                              INTERFACEINFO *info)
{
    DWORD answer = server_answer_index < server_answer_count ? server_answers[server_answer_index++]
                                                              : SERVERCALL_ISHANDLED;

    printf("    called side: HandleInComingCall type %lu, method %u, caller %s -> %s\n", type, info->wMethod,
           task_name(caller), answer_name(answer));
    return answer;
}

static DWORD WINAPI server_RetryRejectedCall(IMessageFilter *iface, HTASK callee, DWORD tick, DWORD type)
{
    printf("    called side: RetryRejectedCall\n");
    return -1;
}

static DWORD WINAPI server_MessagePending(IMessageFilter *iface, HTASK callee, DWORD tick, DWORD type)
{
    return PENDINGMSG_WAITDEFPROCESS;
}

static DWORD WINAPI client_HandleInComingCall(IMessageFilter *iface, DWORD type, HTASK caller, DWORD tick,
                                              INTERFACEINFO *info)
{
    printf("    calling side: HandleInComingCall\n");
    return SERVERCALL_ISHANDLED;
}

static DWORD WINAPI client_RetryRejectedCall(IMessageFilter *iface, HTASK callee, DWORD tick, DWORD type)
{
    LONG answer = client_answer_index < client_answer_count ? client_answers[client_answer_index++] : -1;

    printf("    calling side: RetryRejectedCall callee %s, type %s, tick %s -> %ld\n", task_name(callee),
           answer_name(type), tick < 1000 ? "under a second" : "a second or more", answer);
    return answer;
}

static DWORD WINAPI client_MessagePending(IMessageFilter *iface, HTASK callee, DWORD tick, DWORD type)
{
    pending_calls++;
    return PENDINGMSG_WAITDEFPROCESS;
}

static const IMessageFilterVtbl server_filter_vtbl =
{
    filter_QueryInterface, filter_AddRef, filter_Release, server_HandleInComingCall, server_RetryRejectedCall,
    server_MessagePending,
};
static const IMessageFilterVtbl client_filter_vtbl =
{
    filter_QueryInterface, filter_AddRef, filter_Release, client_HandleInComingCall, client_RetryRejectedCall,
    client_MessagePending,
};
static IMessageFilter server_filter = { &server_filter_vtbl };
static IMessageFilter client_filter = { &client_filter_vtbl };

static const char *hr_name(HRESULT hr)
{
    static char buffer[16];

    switch (hr)
    {
    case S_OK: return "S_OK";
    case RPC_E_CALL_REJECTED: return "RPC_E_CALL_REJECTED";
    case RPC_E_SERVERCALL_RETRYLATER: return "RPC_E_SERVERCALL_RETRYLATER";
    case RPC_E_SERVERCALL_REJECTED: return "RPC_E_SERVERCALL_REJECTED";
    case RPC_E_RETRY: return "RPC_E_RETRY";
    }
    snprintf(buffer, sizeof(buffer), "%#lx", hr);
    return buffer;
}

static const char *elapsed_name(DWORD ms)
{
    if (ms < 100) return "under 100 ms";
    if (ms < 140) return "100 to 140 ms";
    if (ms < 280) return "140 to 280 ms";
    if (ms < 1000) return "280 ms to a second";
    return "a second or more";
}

struct test
{
    const char *name;
    DWORD apartment;
    BOOL filter;
    DWORD server[4];
    DWORD server_count;
    LONG client[4];
    DWORD client_count;
};

static const struct test tests[] =
{
    { "retry later, then handled; retried at once", COINIT_APARTMENTTHREADED, TRUE,
      { SERVERCALL_RETRYLATER }, 1, { 0 }, 1 },
    { "retry later twice; retried after 150 ms, then at once", COINIT_APARTMENTTHREADED, TRUE,
      { SERVERCALL_RETRYLATER, SERVERCALL_RETRYLATER }, 2, { 150, 99 }, 2 },
    { "retry later; given up", COINIT_APARTMENTTHREADED, TRUE,
      { SERVERCALL_RETRYLATER }, 1, { -1 }, 1 },
    { "rejected, then handled; retried at once", COINIT_APARTMENTTHREADED, TRUE,
      { SERVERCALL_REJECTED }, 1, { 0 }, 1 },
    { "rejected; given up", COINIT_APARTMENTTHREADED, TRUE,
      { SERVERCALL_REJECTED }, 1, { -1 }, 1 },
    { "retry later, from an apartment without a filter", COINIT_APARTMENTTHREADED, FALSE,
      { SERVERCALL_RETRYLATER }, 1, { 0 }, 0 },
    { "rejected, from an apartment without a filter", COINIT_APARTMENTTHREADED, FALSE,
      { SERVERCALL_REJECTED }, 1, { 0 }, 0 },
    { "retry later, from the multithreaded apartment", COINIT_MULTITHREADED, FALSE,
      { SERVERCALL_RETRYLATER }, 1, { 0 }, 0 },
    { "rejected, from the multithreaded apartment", COINIT_MULTITHREADED, FALSE,
      { SERVERCALL_REJECTED }, 1, { 0 }, 0 },
};

static const struct test remote_tests[] =
{
    { "retry later, then handled; retried at once, across processes", COINIT_APARTMENTTHREADED, TRUE,
      { SERVERCALL_RETRYLATER }, 1, { 0 }, 1 },
    { "retry later; retried after 150 ms, across processes", COINIT_APARTMENTTHREADED, TRUE,
      { SERVERCALL_RETRYLATER }, 1, { 150 }, 1 },
    { "rejected; given up, across processes", COINIT_APARTMENTTHREADED, TRUE,
      { SERVERCALL_REJECTED }, 1, { -1 }, 1 },
    { "retry later, from an apartment without a filter, across processes", COINIT_APARTMENTTHREADED, FALSE,
      { SERVERCALL_RETRYLATER }, 1, { 0 }, 0 },
    { "retry later, from the multithreaded apartment, across processes", COINIT_MULTITHREADED, FALSE,
      { SERVERCALL_RETRYLATER }, 1, { 0 }, 0 },
};

static IStream *streams[ARRAYSIZE(tests)];

/* starts a copy of this program that makes the object, answers the calls as the test says, and hands out the
 * object marshaled in a file */
static HANDLE start_server(const struct test *test, IStream **stream, DWORD *thread)
{
    STARTUPINFOA si = { sizeof(si) };
    char path[MAX_PATH], file[MAX_PATH], cmdline[1024];
    PROCESS_INFORMATION pi;
    unsigned int i, waited;
    DWORD size;
    int len;

    GetModuleFileNameA(NULL, path, sizeof(path));
    GetTempPathA(sizeof(file), file);
    snprintf(file + strlen(file), sizeof(file) - strlen(file), "retrylater-%lu.bin", GetCurrentProcessId());
    DeleteFileA(file);
    len = snprintf(cmdline, sizeof(cmdline), "\"%s\" server \"%s\" %lu %lu", path, file, client_tid,
                   GetCurrentProcessId());
    for (i = 0; i < test->server_count; i++)
        len += snprintf(cmdline + len, sizeof(cmdline) - len, " %lu", test->server[i]);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi))
    {
        printf("    CreateProcess failed: %lu\n", GetLastError());
        return NULL;
    }
    CloseHandle(pi.hThread);
    *thread = pi.dwThreadId;
    for (waited = 0; waited < 20000; waited += 50)
    {
        HANDLE handle = CreateFileA(file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                                    0, NULL);

        if (handle != INVALID_HANDLE_VALUE)
        {
            HGLOBAL global;

            size = GetFileSize(handle, NULL);
            global = GlobalAlloc(GMEM_MOVEABLE, size);
            ReadFile(handle, GlobalLock(global), size, &size, NULL);
            GlobalUnlock(global);
            CloseHandle(handle);
            DeleteFileA(file);
            CreateStreamOnHGlobal(global, TRUE, stream);
            return pi.hProcess;
        }
        Sleep(50);
    }
    printf("    the server did not start\n");
    TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hProcess);
    return NULL;
}

static void run_test(const struct test *test, IStream *stream)
{
    IMessageFilter *old;
    IPersist *proxy;
    CLSID clsid;
    DWORD start;
    HRESULT hr;

    printf("%s:\n", test->name);
    CoInitializeEx(NULL, test->apartment);
    if (test->filter)
    {
        hr = CoRegisterMessageFilter(&client_filter, &old);
        if (hr != S_OK) printf("    CoRegisterMessageFilter: %#lx\n", hr);
    }
    if (FAILED(hr = CoGetInterfaceAndReleaseStream(stream, &iid_persist, (void **)&proxy)))
    {
        printf("    unmarshaling failed: %#lx\n", hr);
        CoUninitialize();
        return;
    }
    memcpy(server_answers, test->server, sizeof(test->server));
    server_answer_count = test->server_count;
    server_answer_index = 0;
    memcpy(client_answers, test->client, sizeof(test->client));
    client_answer_count = test->client_count;
    client_answer_index = 0;
    pending_calls = 0;
    start = GetTickCount();
    hr = IPersist_GetClassID(proxy, &clsid);
    printf("    GetClassID: %s, %s%s\n", hr_name(hr), elapsed_name(GetTickCount() - start),
           pending_calls ? ", MessagePending called" : "");
    server_answer_count = 0;
    IPersist_Release(proxy);
    if (test->filter) CoRegisterMessageFilter(NULL, &old);
    CoUninitialize();
}

static DWORD WINAPI client_thread(void *arg)
{
    DWORD in_process_server = server_tid;
    unsigned int i;

    client_tid = GetCurrentThreadId();
    for (i = 0; i < ARRAYSIZE(tests); i++) run_test(&tests[i], streams[i]);
    for (i = 0; i < ARRAYSIZE(remote_tests); i++)
    {
        IStream *stream;
        HANDLE process;

        if (!(process = start_server(&remote_tests[i], &stream, &server_tid))) continue;
        run_test(&remote_tests[i], stream);
        PostThreadMessageA(server_tid, WM_QUIT, 0, 0);
        if (WaitForSingleObject(process, 10000)) printf("    the server did not quit\n");
        CloseHandle(process);
    }
    server_tid = in_process_server;
    SetEvent(quit_event);
    return 0;
}

/* the copy that holds the object for the calls across processes */
static int server_main(int argc, char **argv)
{
    char temp[MAX_PATH];
    IMessageFilter *old;
    IStream *stream;
    HGLOBAL global;
    HANDLE file;
    DWORD size;
    MSG msg;
    int i;

    CoInitialize(NULL);
    server_tid = GetCurrentThreadId();
    client_tid = strtoul(argv[3], NULL, 10);
    client_pid = strtoul(argv[4], NULL, 10);
    for (i = 5; i < argc && server_answer_count < ARRAYSIZE(server_answers); i++)
        server_answers[server_answer_count++] = strtoul(argv[i], NULL, 10);
    CoRegisterMessageFilter(&server_filter, &old);
    CreateStreamOnHGlobal(NULL, TRUE, &stream);
    CoMarshalInterface(stream, &iid_persist, (IUnknown *)&object, MSHCTX_LOCAL, NULL, MSHLFLAGS_NORMAL);
    GetHGlobalFromStream(stream, &global);
    size = GlobalSize(global);
    snprintf(temp, sizeof(temp), "%s.tmp", argv[2]);
    file = CreateFileA(temp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    WriteFile(file, GlobalLock(global), size, &size, NULL);
    GlobalUnlock(global);
    CloseHandle(file);
    MoveFileA(temp, argv[2]);
    while (GetMessageA(&msg, NULL, 0, 0)) DispatchMessageA(&msg);
    CoRegisterMessageFilter(NULL, &old);
    IStream_Release(stream);
    CoUninitialize();
    return 0;
}

int main(int argc, char **argv)
{
    IMessageFilter *old;
    unsigned int i;
    HANDLE thread;
    DWORD index;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 4 && !strcmp(argv[1], "server")) return server_main(argc, argv);
    CoInitialize(NULL);
    server_tid = GetCurrentThreadId();
    hr = CoRegisterMessageFilter(&server_filter, &old);
    printf("CoRegisterMessageFilter: %#lx\n", hr);
    for (i = 0; i < ARRAYSIZE(tests); i++)
        CoMarshalInterThreadInterfaceInStream(&iid_persist, (IUnknown *)&object, &streams[i]);
    quit_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    thread = CreateThread(NULL, 0, client_thread, NULL, 0, NULL);
    hr = CoWaitForMultipleHandles(0, 60000, 1, &quit_event, &index);
    if (hr != S_OK) printf("waiting: %#lx\n", hr);
    WaitForSingleObject(thread, 5000);
    CoRegisterMessageFilter(NULL, &old);
    CoUninitialize();
    printf("done\n");
    return 0;
}
