/*
 * keepawake - keeps Windows from sleeping for a while, for a batch of tests run over ssh.
 *
 *   keepawake.exe [seconds]      (default 3600)
 *
 * The reference laptop goes to sleep (modern standby) when nobody uses it, and an ssh session does not keep it
 * awake: a batch stopped half-way when it did.  This holds a system-required, display-required and
 * execution-required power request, plus the continuous execution state, until the time is up or the process
 * is ended (the ssh session's end ends it); nothing about the power settings changes.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define _WIN32_WINNT 0x0602
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    REASON_CONTEXT reason = {POWER_REQUEST_CONTEXT_VERSION, POWER_REQUEST_CONTEXT_SIMPLE_STRING};
    DWORD seconds = argc > 1 ? strtoul(argv[1], NULL, 10) : 3600;
    HANDLE request;
    EXECUTION_STATE previous;

    reason.Reason.SimpleReasonString = (WCHAR *)L"wine-altars test batch";
    previous = SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    request = PowerCreateRequest(&reason);
    if (request != INVALID_HANDLE_VALUE)
    {
        PowerSetRequest(request, PowerRequestSystemRequired);
        PowerSetRequest(request, PowerRequestDisplayRequired);
        PowerSetRequest(request, PowerRequestExecutionRequired);
    }
    printf("keepawake: %lu seconds, execution state was %#lx, power request %s\n", seconds, previous,
           request != INVALID_HANDLE_VALUE ? "held" : "refused");
    fflush(stdout);
    Sleep(seconds * 1000);
    return 0;
}
