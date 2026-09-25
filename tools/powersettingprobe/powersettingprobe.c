/*
 * powersettingprobe - what a window is told when it registers for the power
 * settings Office registers for.
 *
 * Windows tells a window the current value of a setting soon after it
 * registers for it, with WM_POWERBROADCAST and PBT_POWERSETTINGCHANGE; Wine
 * handed back 0xdeadbeef and told nothing.  This registers for each of
 * Office's six settings and prints what arrives, how, and with what data;
 * then the same through powrprof's callbacks, the suspend and resume
 * registrations of both, and the effective power mode.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <windows.h>
#include <objbase.h>
#include <stdio.h>

#ifndef PBT_POWERSETTINGCHANGE
#define PBT_POWERSETTINGCHANGE 0x8013
typedef struct
{
    GUID PowerSetting;
    DWORD DataLength;
    UCHAR Data[1];
} POWERBROADCAST_SETTING;
#endif

static const struct { GUID guid; const char *name; } settings[] =
{
    {{0x02731015, 0x4510, 0x4526, {0x99, 0xe6, 0xe5, 0xa1, 0x7e, 0xbd, 0x1a, 0xea}}, "monitor power on"},
    {{0x245d8541, 0x3943, 0x4422, {0xb0, 0x25, 0x13, 0xa7, 0x84, 0xf6, 0x79, 0xb7}}, "power scheme personality"},
    {{0x3c0f4548, 0xc03f, 0x4c4d, {0xb9, 0xf2, 0x23, 0x7e, 0xde, 0x68, 0x63, 0x76}}, "idle background task"},
    {{0x5d3e9a59, 0xe9d5, 0x4b00, {0xa6, 0xbd, 0xff, 0x34, 0xff, 0x51, 0x65, 0x48}}, "AC/DC power source"},
    {{0x98a7f580, 0x01f7, 0x48aa, {0x9c, 0x0f, 0x44, 0x35, 0x2c, 0x29, 0xe5, 0xc0}}, "away mode"},
    {{0xa7ad8041, 0xb45a, 0x4cae, {0x87, 0xa3, 0xee, 0xcb, 0xb4, 0x68, 0xa9, 0xe1}}, "battery percentage remaining"},
    {{0x6fe69556, 0x704a, 0x47a0, {0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47}}, "console display state"},
    {{0xe00958c0, 0xc213, 0x4ace, {0xac, 0x77, 0xfe, 0xcc, 0xed, 0x2e, 0xee, 0xa5}}, "energy saver status"},
    {{0x31f9f286, 0x5084, 0x42fe, {0xb7, 0x20, 0x2b, 0x02, 0x64, 0x99, 0x37, 0x63}}, "active power scheme"},
};

#ifndef DEVICE_NOTIFY_CALLBACK
#define DEVICE_NOTIFY_CALLBACK 2
typedef ULONG CALLBACK DEVICE_NOTIFY_CALLBACK_ROUTINE(void *context, ULONG type, void *setting);
typedef struct
{
    DEVICE_NOTIFY_CALLBACK_ROUTINE *Callback;
    void *Context;
} DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS;
#endif

static DWORD main_thread;
static BOOL registering;

static const char *name_of(const GUID *guid)
{
    unsigned int i;
    for (i = 0; i < ARRAYSIZE(settings); i++) if (IsEqualGUID(guid, &settings[i].guid)) return settings[i].name;
    return "?";
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_POWERBROADCAST)
    {
        printf("  WM_POWERBROADCAST %#Ix (%s)", wp, InSendMessage() ? "sent" : "posted");
        if (wp == PBT_POWERSETTINGCHANGE)
        {
            const POWERBROADCAST_SETTING *s = (const POWERBROADCAST_SETTING *)lp;
            DWORD i;
            printf(" %s, %lu bytes:", name_of(&s->PowerSetting), s->DataLength);
            for (i = 0; i < s->DataLength && i < 16; i++) printf(" %02x", s->Data[i]);
        }
        printf("\n");
        return TRUE;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static const char *where(void)
{
    if (registering) return "during the call";
    return GetCurrentThreadId() == main_thread ? "on the registering thread" : "on another thread";
}

static ULONG CALLBACK setting_callback(void *context, ULONG type, void *setting)
{
    printf("  callback %s context %p type %#lx %s", context == (void *)0x1234 ? "right" : "wrong", context, type,
           where());
    if (type == PBT_POWERSETTINGCHANGE && setting)
    {
        const POWERBROADCAST_SETTING *s = setting;
        DWORD i;
        printf(" %s, %lu bytes:", name_of(&s->PowerSetting), s->DataLength);
        for (i = 0; i < s->DataLength && i < 16; i++) printf(" %02x", s->Data[i]);
    }
    else printf(" setting %p", setting);
    printf("\n");
    return 0;
}

static void WINAPI mode_callback(int mode, void *context)
{
    printf("  effective power mode %d, context %p %s\n", mode, context, where());
}

static void pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms;
    MSG msg;

    while (GetTickCount() < end)
    {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        Sleep(10);
    }
}

static void powrprof(HWND hwnd)
{
    DWORD (WINAPI *pPowerSettingRegisterNotification)(const GUID *, DWORD, HANDLE, HPOWERNOTIFY *);
    DWORD (WINAPI *pPowerSettingUnregisterNotification)(HPOWERNOTIFY);
    DWORD (WINAPI *pPowerRegisterSuspendResumeNotification)(DWORD, HANDLE, HPOWERNOTIFY *);
    DWORD (WINAPI *pPowerUnregisterSuspendResumeNotification)(HPOWERNOTIFY);
    HRESULT (WINAPI *pPowerRegisterForEffectivePowerModeNotifications)(ULONG, void *, void *, void **);
    HRESULT (WINAPI *pPowerUnregisterFromEffectivePowerModeNotifications)(void *);
    DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS params = {setting_callback, (void *)0x1234};
    HMODULE module = LoadLibraryA("powrprof.dll");
    HPOWERNOTIFY h, h2;
    void *mode, *mode2;
    unsigned int i;
    HRESULT hr;
    DWORD ret;

#define GET(f) p##f = (void *)GetProcAddress(module, #f); if (!p##f) printf(#f " is missing\n")
    GET(PowerSettingRegisterNotification);
    GET(PowerSettingUnregisterNotification);
    GET(PowerRegisterSuspendResumeNotification);
    GET(PowerUnregisterSuspendResumeNotification);
    GET(PowerRegisterForEffectivePowerModeNotifications);
    GET(PowerUnregisterFromEffectivePowerModeNotifications);
#undef GET

    if (pPowerSettingRegisterNotification)
    {
        for (i = 0; i < ARRAYSIZE(settings); i++)
        {
            h = (HPOWERNOTIFY)0xdeadbeef;
            registering = TRUE;
            ret = pPowerSettingRegisterNotification(&settings[i].guid, DEVICE_NOTIFY_CALLBACK, &params, &h);
            registering = FALSE;
            printf("PowerSettingRegisterNotification %s, callback: %lu, %s\n", settings[i].name, ret,
                   h == (HPOWERNOTIFY)0xdeadbeef ? "handle untouched" : h ? "a handle" : "NULL");
            pump(300);
            if (!ret) printf("PowerSettingUnregisterNotification %lu\n", pPowerSettingUnregisterNotification(h));
        }
        h = (HPOWERNOTIFY)0xdeadbeef;
        registering = TRUE;
        ret = pPowerSettingRegisterNotification(&settings[3].guid, DEVICE_NOTIFY_WINDOW_HANDLE, hwnd, &h);
        registering = FALSE;
        printf("PowerSettingRegisterNotification AC/DC, window: %lu, %s\n", ret,
               h == (HPOWERNOTIFY)0xdeadbeef ? "handle untouched" : h ? "a handle" : "NULL");
        pump(300);
        if (!ret) printf("PowerSettingUnregisterNotification %lu\n", pPowerSettingUnregisterNotification(h));
        h = (HPOWERNOTIFY)0xdeadbeef;
        ret = pPowerSettingRegisterNotification(&settings[3].guid, 7, &params, &h);
        printf("PowerSettingRegisterNotification flags 7: %lu, %s\n", ret,
               h == (HPOWERNOTIFY)0xdeadbeef ? "handle untouched" : h ? "a handle" : "NULL");
        pump(300);
        if (!ret) printf("PowerSettingUnregisterNotification %lu\n", pPowerSettingUnregisterNotification(h));
        printf("PowerSettingUnregisterNotification(NULL) %lu\n", pPowerSettingUnregisterNotification(NULL));
    }

    if (pPowerRegisterSuspendResumeNotification)
    {
        h = (HPOWERNOTIFY)0xdeadbeef;
        registering = TRUE;
        ret = pPowerRegisterSuspendResumeNotification(DEVICE_NOTIFY_CALLBACK, &params, &h);
        registering = FALSE;
        printf("PowerRegisterSuspendResumeNotification callback: %lu, %s\n", ret,
               h == (HPOWERNOTIFY)0xdeadbeef ? "handle untouched" : h ? "a handle" : "NULL");
        pump(300);
        if (!ret) printf("PowerUnregisterSuspendResumeNotification %lu\n", pPowerUnregisterSuspendResumeNotification(h));
        h = (HPOWERNOTIFY)0xdeadbeef;
        ret = pPowerRegisterSuspendResumeNotification(DEVICE_NOTIFY_WINDOW_HANDLE, hwnd, &h);
        printf("PowerRegisterSuspendResumeNotification window: %lu, %s\n", ret,
               h == (HPOWERNOTIFY)0xdeadbeef ? "handle untouched" : h ? "a handle" : "NULL");
        pump(300);
        if (!ret) printf("PowerUnregisterSuspendResumeNotification %lu\n", pPowerUnregisterSuspendResumeNotification(h));
        h = (HPOWERNOTIFY)0xdeadbeef;
        ret = pPowerRegisterSuspendResumeNotification(7, &params, &h);
        printf("PowerRegisterSuspendResumeNotification flags 7: %lu, %s\n", ret,
               h == (HPOWERNOTIFY)0xdeadbeef ? "handle untouched" : h ? "a handle" : "NULL");
        if (!ret) printf("PowerUnregisterSuspendResumeNotification %lu\n", pPowerUnregisterSuspendResumeNotification(h));
        printf("PowerUnregisterSuspendResumeNotification(NULL) %lu\n", pPowerUnregisterSuspendResumeNotification(NULL));
    }

    if (pPowerRegisterForEffectivePowerModeNotifications)
    {
        for (i = 0; i <= 3; i++)
        {
            mode = (void *)0xdeadbeef;
            registering = TRUE;
            hr = pPowerRegisterForEffectivePowerModeNotifications(i, mode_callback, (void *)0x5678, &mode);
            registering = FALSE;
            printf("PowerRegisterForEffectivePowerModeNotifications(%u) %#lx, %s\n", i, hr,
                   mode == (void *)0xdeadbeef ? "handle untouched" : mode ? "a handle" : "NULL");
            pump(300);
            if (SUCCEEDED(hr) && pPowerUnregisterFromEffectivePowerModeNotifications)
                printf("PowerUnregisterFromEffectivePowerModeNotifications %#lx\n",
                       pPowerUnregisterFromEffectivePowerModeNotifications(mode));
        }
        mode = mode2 = (void *)0xdeadbeef;
        hr = pPowerRegisterForEffectivePowerModeNotifications(1, mode_callback, (void *)0x5678, &mode);
        printf("two registrations: %#lx", hr);
        hr = pPowerRegisterForEffectivePowerModeNotifications(1, mode_callback, (void *)0x9abc, &mode2);
        printf(" %#lx, %s\n", hr, mode == mode2 ? "the same handle" : "two handles");
        pump(300);
        if (pPowerUnregisterFromEffectivePowerModeNotifications)
        {
            printf("PowerUnregisterFromEffectivePowerModeNotifications %#lx", pPowerUnregisterFromEffectivePowerModeNotifications(mode));
            printf(" %#lx\n", pPowerUnregisterFromEffectivePowerModeNotifications(mode2));
            printf("PowerUnregisterFromEffectivePowerModeNotifications(NULL) %#lx\n",
                   pPowerUnregisterFromEffectivePowerModeNotifications(NULL));
        }
        mode = (void *)0xdeadbeef;
        hr = pPowerRegisterForEffectivePowerModeNotifications(1, NULL, NULL, &mode);
        printf("PowerRegisterForEffectivePowerModeNotifications(NULL callback) %#lx, %s\n", hr,
               mode == (void *)0xdeadbeef ? "handle untouched" : mode ? "a handle" : "NULL");
    }
    (void)h2;
}

static GUID balanced = {0x381b4222, 0xf694, 0x41f0, {0x96, 0x85, 0xff, 0x5b, 0xb2, 0x60, 0xdf, 0x2e}};
static GUID *active_copy(void) { return &balanced; }

static void schemes(void)
{
    DWORD (WINAPI *pPowerGetActiveScheme)(HKEY, GUID **);
    DWORD (WINAPI *pPowerEnumerate)(HKEY, const GUID *, const GUID *, int, ULONG, UCHAR *, DWORD *);
    DWORD (WINAPI *pPowerReadFriendlyName)(HKEY, const GUID *, const GUID *, const GUID *, UCHAR *, DWORD *);
    HMODULE module = LoadLibraryA("powrprof.dll");
    WCHAR str[64], name[256];
    GUID *active, scheme;
    DWORD ret, size;
    ULONG i;

    pPowerGetActiveScheme = (void *)GetProcAddress(module, "PowerGetActiveScheme");
    pPowerEnumerate = (void *)GetProcAddress(module, "PowerEnumerate");
    pPowerReadFriendlyName = (void *)GetProcAddress(module, "PowerReadFriendlyName");

    active = (GUID *)0xdeadbeef;
    ret = pPowerGetActiveScheme(NULL, &active);
    if (!ret)
    {
        StringFromGUID2(active, str, 64);
        printf("PowerGetActiveScheme 0 %ls, LocalFree %p\n", str, LocalFree(active));
    }
    else printf("PowerGetActiveScheme %lu\n", ret);

    for (i = 0; ; i++)
    {
        size = sizeof(scheme);
        ret = pPowerEnumerate(NULL, NULL, NULL, 16 /* ACCESS_SCHEME */, i, (UCHAR *)&scheme, &size);
        if (ret) { printf("PowerEnumerate(%lu) %lu size %lu\n", i, ret, size); break; }
        StringFromGUID2(&scheme, str, 64);
        size = sizeof(name);
        memset(name, 0, sizeof(name));
        ret = pPowerReadFriendlyName(NULL, &scheme, NULL, NULL, (UCHAR *)name, &size);
        printf("scheme %lu %ls, size %lu: friendly name %lu, size %lu, first characters %04x %04x %04x\n", i, str,
               sizeof(scheme), ret, size, name[0], name[1], name[2]);
    }
    size = 0xdead;
    ret = pPowerEnumerate(NULL, NULL, NULL, 16, 0, NULL, &size);
    printf("PowerEnumerate(0), no buffer: %lu size %lu\n", ret, size);
    size = 4;
    ret = pPowerEnumerate(NULL, NULL, NULL, 16, 0, (UCHAR *)&scheme, &size);
    printf("PowerEnumerate(0), 4 bytes: %lu size %lu\n", ret, size);
    size = 0xdead;
    ret = pPowerReadFriendlyName(NULL, active_copy(), NULL, NULL, NULL, &size);
    printf("PowerReadFriendlyName, no buffer: %lu size %lu\n", ret, size);
    size = 2;
    ret = pPowerReadFriendlyName(NULL, active_copy(), NULL, NULL, (UCHAR *)name, &size);
    printf("PowerReadFriendlyName, 2 bytes: %lu size %lu\n", ret, size);
    size = sizeof(name);
    ret = pPowerReadFriendlyName(NULL, &GUID_NULL, NULL, NULL, (UCHAR *)name, &size);
    printf("PowerReadFriendlyName(GUID_NULL): %lu size %lu\n", ret, size);
    {
        static const GUID random = {0x12345678, 0x1234, 0x1234, {1, 2, 3, 4, 5, 6, 7, 8}};
        size = sizeof(name);
        ret = pPowerReadFriendlyName(NULL, &random, NULL, NULL, (UCHAR *)name, &size);
        printf("PowerReadFriendlyName(an unknown scheme): %lu size %lu\n", ret, size);
    }
    active = (GUID *)0xdeadbeef;
    ret = pPowerGetActiveScheme((HKEY)0x1234, &active);
    printf("PowerGetActiveScheme(a bogus key): %lu %s\n", ret, active == (GUID *)0xdeadbeef ? "untouched" : "set");
    if (!ret) LocalFree(active);
}

int main(void)
{
    DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS params = {setting_callback, (void *)0x1234};
    HPOWERNOTIFY handles[ARRAYSIZE(settings)], h;
    WNDCLASSW cls = {0};
    unsigned int i;
    DWORD error;
    HWND hwnd;
    BOOL ret;

    setvbuf(stdout, NULL, _IONBF, 0);
    main_thread = GetCurrentThreadId();
    cls.lpfnWndProc = wndproc;
    cls.lpszClassName = L"powersettingprobe";
    RegisterClassW(&cls);
    hwnd = CreateWindowW(L"powersettingprobe", NULL, WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, NULL, NULL);

    for (i = 0; i < ARRAYSIZE(settings); i++)
    {
        SetLastError(0xdeadbeef);
        handles[i] = RegisterPowerSettingNotification(hwnd, &settings[i].guid, DEVICE_NOTIFY_WINDOW_HANDLE);
        printf("register %s: %s, error %lu\n", settings[i].name, handles[i] ? "a handle" : "NULL",
               handles[i] ? 0 : GetLastError());
        pump(300);
    }
    /* each call a statement of its own: x64 evaluates arguments right to left, so a GetLastError() beside the call
     * in one argument list reads the error from before it */
    SetLastError(0xdeadbeef);
    h = RegisterPowerSettingNotification(NULL, &settings[0].guid, DEVICE_NOTIFY_WINDOW_HANDLE);
    error = GetLastError();
    printf("register NULL window: %s error %lu\n", h ? "a handle" : "NULL", error);
    /* a NULL guid brings Windows down: it is not checked */
    {
        static const GUID random = {0x12345678, 0x1234, 0x1234, {1, 2, 3, 4, 5, 6, 7, 8}};

        SetLastError(0xdeadbeef);
        h = RegisterPowerSettingNotification(hwnd, &random, DEVICE_NOTIFY_WINDOW_HANDLE);
        printf("register an unknown setting: %s error %lu\n", h ? "a handle" : "NULL", GetLastError());
        pump(300);
        if (h) printf("unregister it: %d\n", UnregisterPowerSettingNotification(h));
    }
    SetLastError(0xdeadbeef);
    h = RegisterPowerSettingNotification(hwnd, &settings[0].guid, 7);
    printf("register flags 7: %s error %lu\n", h ? "a handle" : "NULL", GetLastError());
    pump(300);
    if (h) printf("unregister it: %d\n", UnregisterPowerSettingNotification(h));
    SetLastError(0xdeadbeef);
    registering = TRUE;
    h = RegisterPowerSettingNotification(&params, &settings[3].guid, DEVICE_NOTIFY_CALLBACK);
    registering = FALSE;
    printf("register a callback: %s error %lu\n", h ? "a handle" : "NULL", GetLastError());
    pump(300);
    if (h) printf("unregister it: %d\n", UnregisterPowerSettingNotification(h));
    for (i = 0; i < ARRAYSIZE(settings); i++)
        if (handles[i]) printf("unregister %s: %d\n", settings[i].name, UnregisterPowerSettingNotification(handles[i]));
    SetLastError(0xdeadbeef);
    ret = UnregisterPowerSettingNotification(NULL);
    error = GetLastError();
    printf("unregister NULL: %d error %lu\n", ret, error);

    SetLastError(0xdeadbeef);
    h = RegisterSuspendResumeNotification(hwnd, DEVICE_NOTIFY_WINDOW_HANDLE);
    printf("RegisterSuspendResumeNotification window: %s error %lu\n", h ? "a handle" : "NULL", GetLastError());
    pump(300);
    if (h) printf("UnregisterSuspendResumeNotification %d\n", UnregisterSuspendResumeNotification(h));
    SetLastError(0xdeadbeef);
    registering = TRUE;
    h = RegisterSuspendResumeNotification(&params, DEVICE_NOTIFY_CALLBACK);
    registering = FALSE;
    printf("RegisterSuspendResumeNotification callback: %s error %lu\n", h ? "a handle" : "NULL", GetLastError());
    pump(300);
    if (h) printf("UnregisterSuspendResumeNotification %d\n", UnregisterSuspendResumeNotification(h));
    SetLastError(0xdeadbeef);
    h = RegisterSuspendResumeNotification(NULL, DEVICE_NOTIFY_WINDOW_HANDLE);
    printf("RegisterSuspendResumeNotification NULL window: %s error %lu\n", h ? "a handle" : "NULL", GetLastError());
    if (h) printf("UnregisterSuspendResumeNotification %d\n", UnregisterSuspendResumeNotification(h));
    SetLastError(0xdeadbeef);
    h = RegisterSuspendResumeNotification(hwnd, 7);
    printf("RegisterSuspendResumeNotification flags 7: %s error %lu\n", h ? "a handle" : "NULL", GetLastError());
    if (h) printf("UnregisterSuspendResumeNotification %d\n", UnregisterSuspendResumeNotification(h));
    SetLastError(0xdeadbeef);
    ret = UnregisterSuspendResumeNotification(NULL);
    error = GetLastError();
    printf("UnregisterSuspendResumeNotification(NULL) %d error %lu\n", ret, error);

    powrprof(hwnd);
    schemes();

    /* a service status handle that is none; a service would hear through its handler */
    SetLastError(0xdeadbeef);
    h = RegisterPowerSettingNotification((HANDLE)0x1234, &settings[3].guid, DEVICE_NOTIFY_SERVICE_HANDLE);
    printf("register a bogus service: %s error %lu\n", h ? "a handle" : "NULL", GetLastError());
    pump(300);
    if (h) printf("unregister it: %d\n", UnregisterPowerSettingNotification(h));
    SetLastError(0xdeadbeef);
    h = RegisterPowerSettingNotification(hwnd, &settings[3].guid, DEVICE_NOTIFY_ALL_INTERFACE_CLASSES);
    printf("register flags 4: %s error %lu\n", h ? "a handle" : "NULL", GetLastError());
    pump(300);
    if (h) printf("unregister it: %d\n", UnregisterPowerSettingNotification(h));
    SetLastError(0xdeadbeef);
    h = RegisterSuspendResumeNotification((HANDLE)0x1234, DEVICE_NOTIFY_SERVICE_HANDLE);
    printf("RegisterSuspendResumeNotification bogus service: %s error %lu\n", h ? "a handle" : "NULL", GetLastError());
    if (h) printf("UnregisterSuspendResumeNotification %d\n", UnregisterSuspendResumeNotification(h));
    DestroyWindow(hwnd);
    printf("done\n");
    return 0;
}
