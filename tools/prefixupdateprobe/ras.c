#define _WIN32_WINNT 0x0601
#define UNICODE
#include "probe.h"
#include <ras.h>

static BOOL rasman_running(void)
{
    SC_HANDLE manager, service;
    SERVICE_STATUS_PROCESS state;
    DWORD returned = PROBE_SENTINEL, error;
    BOOL ok;

    SetLastError(PROBE_SENTINEL);
    manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    error = GetLastError();
    printf("SCM open=%u gle=%lu access=SC_MANAGER_CONNECT\n", !!manager, error);
    if (!manager) return FALSE;
    SetLastError(PROBE_SENTINEL);
    service = OpenServiceW(manager, L"RasMan", SERVICE_QUERY_STATUS);
    error = GetLastError();
    printf("RasMan open=%u gle=%lu access=SERVICE_QUERY_STATUS\n", !!service, error);
    if (!service)
    {
        CloseServiceHandle(manager);
        return FALSE;
    }
    memset(&state, 0xa5, sizeof(state));
    SetLastError(PROBE_SENTINEL);
    ok = QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, (BYTE *)&state,
                             sizeof(state), &returned);
    error = GetLastError();
    printf("RasMan query=%u gle=%lu returned=%lu", ok, error, returned);
    if (ok)
        printf(" type=%#lx state=%lu controls=%#lx win32_exit=%lu service_exit=%lu flags=%#lx",
               state.dwServiceType, state.dwCurrentState, state.dwControlsAccepted,
               state.dwWin32ExitCode, state.dwServiceSpecificExitCode, state.dwServiceFlags);
    puts("");
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
    return ok && state.dwCurrentState == SERVICE_RUNNING;
}

static void notification(const char *connection_kind, HRASCONN connection,
                         BOOL invalid_event, DWORD flags)
{
    HANDLE event;
    DWORD status, error, signaled;

    event = invalid_event ? INVALID_HANDLE_VALUE : CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!event)
    {
        printf("event_create_failed gle=%lu\n", GetLastError());
        return;
    }
    SetLastError(PROBE_SENTINEL);
    status = RasConnectionNotificationW(connection, event, flags);
    error = GetLastError();
    signaled = invalid_event ? PROBE_SENTINEL : WaitForSingleObject(event, 0);
    printf("notification connection=%s event=%s flags=%#lx status=%lu gle=%lu event_wait=%lu\n",
           connection_kind, invalid_event ? "INVALID_HANDLE_VALUE" : "valid",
           flags, status, error, signaled);
    if (!invalid_event) CloseHandle(event);
}

int main(void)
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    static const DWORD flags[] = {0, RASCN_Connection, RASCN_Disconnection,
        RASCN_Connection | RASCN_Disconnection, RASCN_BandwidthAdded,
        RASCN_BandwidthRemoved, 15, 0x80000000U};
    const DWORD lengths[] = {0, 1, sizeof(RASCONNW) - 1, sizeof(RASCONNW),
        sizeof(RASCONNW) + 1, 2 * sizeof(RASCONNW), 65536};
    const DWORD structure_sizes[] = {0, sizeof(RASCONNW) - 1, sizeof(RASCONNW) + 1};
    RASCONNW *connections;
    HRASCONN real_connection = NULL;
    DWORD length, count, status, error, i;

    if (!probe_start()) return 1;
    if (!rasman_running())
    {
        puts("RAS skipped: RasMan is not confirmed running; no service start requested");
        return probe_done(0);
    }
    connections = malloc(65536);
    if (!connections)
    {
        puts("allocation_failed");
        return probe_done(1);
    }
    printf("RASCONNW size=%llu allocation=65536\n", (unsigned long long)sizeof(RASCONNW));
    for (i = 0; i < ARRAYSIZE(lengths); ++i)
    {
        memset(connections, 0xa5, 65536);
        connections[0].dwSize = sizeof(RASCONNW);
        count = PROBE_SENTINEL;
        length = lengths[i];
        SetLastError(PROBE_SENTINEL);
        status = RasEnumConnectionsW(connections, &length, &count);
        error = GetLastError();
        printf("enum input_len=%lu dwSize=%llu status=%lu gle=%lu output_len=%lu count=%lu first_dwSize=%lu\n",
               lengths[i], (unsigned long long)sizeof(RASCONNW), status, error,
               length, count, connections[0].dwSize);
        if (!status && count && count <= 65536 / sizeof(RASCONNW) &&
            lengths[i] >= sizeof(RASCONNW)) real_connection = connections[0].hrasconn;
    }
    for (i = 0; i < ARRAYSIZE(structure_sizes); ++i)
    {
        memset(connections, 0xa5, 65536);
        connections[0].dwSize = structure_sizes[i];
        count = PROBE_SENTINEL;
        length = 65536;
        SetLastError(PROBE_SENTINEL);
        status = RasEnumConnectionsW(connections, &length, &count);
        error = GetLastError();
        printf("enum input_len=65536 dwSize=%lu status=%lu gle=%lu output_len=%lu count=%lu\n",
               structure_sizes[i], status, error, length, count);
    }
    for (i = 0; i < ARRAYSIZE(flags); ++i)
        notification("all(INVALID_HANDLE_VALUE)", (HRASCONN)INVALID_HANDLE_VALUE, FALSE, flags[i]);
    notification("all(INVALID_HANDLE_VALUE)", (HRASCONN)INVALID_HANDLE_VALUE, TRUE, RASCN_Connection);
    printf("real_connection_available=%u\n", !!real_connection);
    if (real_connection)
    {
        notification("existing", real_connection, FALSE, RASCN_Disconnection);
        notification("existing", real_connection, FALSE, 0x80000000U);
    }
    free(connections);
    return probe_done(0);
}
