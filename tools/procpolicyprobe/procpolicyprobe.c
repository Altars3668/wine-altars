/*
 * procpolicyprobe - what the process queries Word makes as it starts answer:
 * mitigation policies of its own process and of another, the power
 * throttling state, heap information class 2, GUI resource counts and the
 * AppPolicy values of a desktop process.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define _WIN32_WINNT 0x0a00
#include <windows.h>
#include <appmodel.h>
#include <stdio.h>

static void policies(const char *who, HANDLE process)
{
    DWORD value[8], error;
    int policy;
    BOOL ret;

    for (policy = 0; policy <= 16; policy++)
    {
        memset(value, 0xcc, sizeof(value));
        SetLastError(0xdeadbeef);
        ret = GetProcessMitigationPolicy(process, policy, value, policy == 5 ? 16 : 4);
        error = GetLastError();
        printf("%s policy %2d: %d error %lu value %#lx\n", who, policy, ret, ret ? 0 : error, value[0]);
    }
}

int main(int argc, char **argv)
{
    PROCESS_MITIGATION_ASLR_POLICY aslr = {0};
    PROCESS_MITIGATION_IMAGE_LOAD_POLICY image = {0};
    PROCESS_POWER_THROTTLING_STATE state;
    STARTUPINFOA si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    char cmd[MAX_PATH + 16];
    BYTE buf[256];
    SIZE_T len;
    DWORD error, value;
    HBRUSH brushes[10];
    HWND hwnd;
    BOOL ret;
    int i;

    if (argc > 1) { Sleep(5000); return 0; }
    setvbuf(stdout, NULL, _IONBF, 0);

    policies("self", GetCurrentProcess());
    memset(&aslr, 0, sizeof(aslr));
    aslr.EnableForceRelocateImages = 1;
    SetLastError(0xdeadbeef);
    ret = SetProcessMitigationPolicy(ProcessASLRPolicy, &aslr, sizeof(aslr));
    error = GetLastError();
    printf("set ASLR force relocate: %d error %lu\n", ret, ret ? 0 : error);
    image.NoRemoteImages = 1;
    image.NoLowMandatoryLabelImages = 1;
    image.PreferSystem32Images = 1;
    SetLastError(0xdeadbeef);
    ret = SetProcessMitigationPolicy(ProcessImageLoadPolicy, &image, sizeof(image));
    error = GetLastError();
    printf("set image load no remote, no low label, prefer system32: %d error %lu\n", ret, ret ? 0 : error);
    image.NoRemoteImages = 0;
    SetLastError(0xdeadbeef);
    ret = SetProcessMitigationPolicy(ProcessImageLoadPolicy, &image, sizeof(image));
    error = GetLastError();
    printf("set image load, taking no remote back: %d error %lu\n", ret, ret ? 0 : error);
    SetLastError(0xdeadbeef);
    ret = SetProcessMitigationPolicy(ProcessImageLoadPolicy, &image, 3);
    error = GetLastError();
    printf("set image load, 3 bytes: %d error %lu\n", ret, ret ? 0 : error);
    SetLastError(0xdeadbeef);
    ret = SetProcessMitigationPolicy(ProcessDEPPolicy, &image, 8);
    error = GetLastError();
    printf("set DEP: %d error %lu\n", ret, ret ? 0 : error);
    policies("self after", GetCurrentProcess());

    sprintf(cmd, "\"%s\" child", argv[0]);
    CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    Sleep(500);
    policies("child", pi.hProcess);

    memset(&state, 0xcc, sizeof(state));
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    SetLastError(0xdeadbeef);
    ret = GetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));
    error = GetLastError();
    printf("power throttling: %d error %lu version %lu control %#lx state %#lx\n", ret, ret ? 0 : error,
           state.Version, state.ControlMask, state.StateMask);
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    state.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    state.StateMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    SetLastError(0xdeadbeef);
    ret = SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));
    error = GetLastError();
    printf("set power throttling execution speed: %d error %lu\n", ret, ret ? 0 : error);
    memset(&state, 0xcc, sizeof(state));
    state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    ret = GetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));
    error = GetLastError();
    printf("power throttling now: %d error %lu version %lu control %#lx state %#lx\n", ret, ret ? 0 : error,
           state.Version, state.ControlMask, state.StateMask);
    state.Version = 5;
    SetLastError(0xdeadbeef);
    ret = GetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));
    error = GetLastError();
    printf("power throttling, version 5: %d error %lu\n", ret, ret ? 0 : error);
    SetLastError(0xdeadbeef);
    ret = GetProcessInformation(pi.hProcess, ProcessPowerThrottling, &state, sizeof(state));
    error = GetLastError();
    printf("child power throttling: %d error %lu control %#lx state %#lx\n", ret, ret ? 0 : error,
           state.ControlMask, state.StateMask);
    TerminateProcess(pi.hProcess, 0);

    for (i = 0; i <= 3; i++)
    {
        memset(buf, 0xcc, sizeof(buf));
        len = 0xdead;
        SetLastError(0xdeadbeef);
        ret = HeapQueryInformation(GetProcessHeap(), i, buf, sizeof(buf), &len);
        error = GetLastError();
        printf("heap class %d: %d error %lu len %Iu first %02x %02x %02x %02x %02x %02x %02x %02x\n", i, ret,
               ret ? 0 : error, len, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
    }
    len = 0xdead;
    SetLastError(0xdeadbeef);
    ret = HeapQueryInformation(GetProcessHeap(), 2, buf, 0, &len);
    error = GetLastError();
    printf("heap class 2, no room: %d error %lu len %Iu\n", ret, ret ? 0 : error, len);

    printf("GUI resources: gdi %lu user %lu gdi peak %lu user peak %lu\n", GetGuiResources(GetCurrentProcess(), 0),
           GetGuiResources(GetCurrentProcess(), 1), GetGuiResources(GetCurrentProcess(), 2),
           GetGuiResources(GetCurrentProcess(), 4));
    for (i = 0; i < 10; i++) brushes[i] = CreateSolidBrush(i);
    hwnd = CreateWindowA("static", NULL, WS_POPUP, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
    printf("after 10 brushes and a window: gdi %lu user %lu gdi peak %lu user peak %lu\n",
           GetGuiResources(GetCurrentProcess(), 0), GetGuiResources(GetCurrentProcess(), 1),
           GetGuiResources(GetCurrentProcess(), 2), GetGuiResources(GetCurrentProcess(), 4));
    for (i = 0; i < 10; i++) DeleteObject(brushes[i]);
    DestroyWindow(hwnd);
    printf("after deleting them: gdi %lu user %lu gdi peak %lu user peak %lu\n",
           GetGuiResources(GetCurrentProcess(), 0), GetGuiResources(GetCurrentProcess(), 1),
           GetGuiResources(GetCurrentProcess(), 2), GetGuiResources(GetCurrentProcess(), 4));
    SetLastError(0xdeadbeef);
    value = GetGuiResources(GetCurrentProcess(), 3);
    error = GetLastError();
    printf("GUI resources, flag 3: %lu error %lu\n", value, error);
    SetLastError(0xdeadbeef);
    value = GetGuiResources(NULL, 0);
    error = GetLastError();
    printf("GUI resources, NULL process: %lu error %lu\n", value, error);

    {
        AppPolicyThreadInitializationType init = 0xdead;
        AppPolicyProcessTerminationMethod term = 0xdead;
        AppPolicyWindowingModel windowing = 0xdead;
        LONG r;

        r = AppPolicyGetThreadInitializationType(GetCurrentThreadEffectiveToken(), &init);
        printf("AppPolicyGetThreadInitializationType %ld %d\n", r, init);
        r = AppPolicyGetProcessTerminationMethod(GetCurrentThreadEffectiveToken(), &term);
        printf("AppPolicyGetProcessTerminationMethod %ld %d\n", r, term);
        r = AppPolicyGetWindowingModel(GetCurrentThreadEffectiveToken(), &windowing);
        printf("AppPolicyGetWindowingModel %ld %d\n", r, windowing);
    }
    printf("done\n");
    return 0;
}
