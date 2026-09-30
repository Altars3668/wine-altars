/*
 * rpcbench - how many ncalrpc round trips a second, between two processes.
 *
 * Click-to-Run's App-V registry hook asks its service over ncalrpc for every key lookup, ~10000 times per Office
 * start; under Wine each call is a named-pipe exchange through the wineserver.  "rpcbench server" listens on the
 * endpoint rpcbench; "rpcbench client [calls] [bytes]" makes that many calls with that much payload (default
 * 10000 calls of 48 bytes, the size App-V's IsDuplicatedKey sends), prints calls per second and microseconds per
 * call, then tells the server to stop.  "rpcbench both [calls] [bytes] [impersonate]" starts the server itself as a
 * child; with "impersonate" the server impersonates and reverts in every call, as App-V's does.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <windows.h>
#include <rpc.h>
#include <stdio.h>
#include <stdlib.h>
#include "rpcbench.h"

static HANDLE stop_event;
static BOOL impersonate;

int __cdecl s_ping(int value, int size, const unsigned char *data)
{
    /* App-V's registry server impersonates its caller for each call */
    if (impersonate)
    {
        RpcImpersonateClient(NULL);
        RpcRevertToSelf();
    }
    return value + (size ? data[size - 1] : 0);
}

void __cdecl s_stop(void)
{
    SetEvent(stop_event);
}

void __RPC_FAR *__RPC_USER MIDL_user_allocate(SIZE_T size) { return malloc(size); }
void __RPC_USER MIDL_user_free(void __RPC_FAR *p) { free(p); }

static DWORD WINAPI stopper(void *arg)
{
    WaitForSingleObject(stop_event, INFINITE);
    RpcMgmtStopServerListening(NULL);
    return 0;
}

static int server(void)
{
    RPC_STATUS status;

    stop_event = CreateEventW(NULL, TRUE, FALSE, NULL);
    status = RpcServerUseProtseqEpA((unsigned char *)"ncalrpc", RPC_C_PROTSEQ_MAX_REQS_DEFAULT, (unsigned char *)"rpcbench", NULL);
    if (status) { printf("RpcServerUseProtseqEp %ld\n", status); return 1; }
    status = RpcServerRegisterIf(s_rpcbench_v1_0_s_ifspec, NULL, NULL);
    if (status) { printf("RpcServerRegisterIf %ld\n", status); return 1; }
    CloseHandle(CreateThread(NULL, 0, stopper, NULL, 0, NULL));
    SetEvent(CreateEventA(NULL, TRUE, FALSE, "rpcbench_ready"));
    status = RpcServerListen(1, RPC_C_LISTEN_MAX_CALLS_DEFAULT, FALSE);
    printf("server: listen ended %ld\n", status);
    return 0;
}

static int client(int calls, int size)
{
    unsigned char *binding_string = NULL, *data;
    HANDLE ready = NULL;
    LARGE_INTEGER freq, start, end;
    RPC_STATUS status;
    int i, sum = 0;
    double seconds;

    status = RpcStringBindingComposeA(NULL, (unsigned char *)"ncalrpc", NULL, (unsigned char *)"rpcbench", NULL, &binding_string);
    if (!status) status = RpcBindingFromStringBindingA(binding_string, &rpcbench_binding);
    if (status) { printf("binding %ld\n", status); return 1; }
    data = calloc(1, size ? size : 1);
    for (i = 0; i < 100 && !(ready = OpenEventA(SYNCHRONIZE, FALSE, "rpcbench_ready")); i++) Sleep(100);
    if (!ready || WaitForSingleObject(ready, 10000))
    {
        printf("no server\n");
        return 1;
    }
    CloseHandle(ready);
    sum += ping(0, size, data);
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    for (i = 0; i < calls; i++) sum += ping(i, size, data);
    QueryPerformanceCounter(&end);
    seconds = (double)(end.QuadPart - start.QuadPart) / freq.QuadPart;
    printf("%d calls of %d bytes in %.3f s: %.0f calls/s, %.1f us per call (checksum %d)\n", calls, size, seconds,
           calls / seconds, seconds * 1e6 / calls, sum);
    stop();
    RpcBindingFree(&rpcbench_binding);
    RpcStringFreeA(&binding_string);
    free(data);
    return 0;
}

int main(int argc, char **argv)
{
    int calls = argc > 2 ? atoi(argv[2]) : 10000, size = argc > 3 ? atoi(argv[3]) : 48;

    setvbuf(stdout, NULL, _IONBF, 0);
    impersonate = argc > 4 && !strcmp(argv[4], "impersonate");
    if (argc > 1 && !strcmp(argv[1], "server")) return server();
    if (argc > 1 && !strcmp(argv[1], "client")) return client(calls, size);
    if (argc > 1 && !strcmp(argv[1], "both"))
    {
        STARTUPINFOA si = {sizeof(si)};
        PROCESS_INFORMATION pi;
        char cmd[MAX_PATH + 16];
        int ret;

        GetModuleFileNameA(NULL, cmd, MAX_PATH);
        strcat(cmd, impersonate ? " server 0 0 impersonate" : " server");
        if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
        {
            printf("CreateProcess %lu\n", GetLastError());
            return 1;
        }
        Sleep(500);
        ret = client(calls, size);
        WaitForSingleObject(pi.hProcess, 10000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return ret;
    }
    printf("usage: rpcbench server | client [calls] [bytes] | both [calls] [bytes] [impersonate]\n");
    return 2;
}
