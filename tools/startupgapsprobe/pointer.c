/* pointer: the pointer device functions -- what GetPointerDevices answers with and without an array, the devices'
 * properties, rectangles and cursors, RegisterPointerDeviceNotifications for windows of this and other threads,
 * the errors for pointer ids that do not exist, and mouse-in-pointer before and after enabling it.  Prints results
 * only. */
#include <windows.h>
#include <stdio.h>

static HWND window;

static DWORD WINAPI other_thread(void *arg)
{
    HWND *hwnd = arg;
    MSG msg;

    *hwnd = CreateWindowExW(0, L"static", L"other", WS_POPUP, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
    while (GetMessageW(&msg, NULL, 0, 0)) DispatchMessageW(&msg);
    return 0;
}

static void show_bool(const char *what, BOOL ret)
{
    printf("%s: %d error %lu\n", what, ret, ret ? 0 : GetLastError());
}

int main(void)
{
    POINTER_DEVICE_INFO devices[16];
    POINTER_INPUT_TYPE type;
    POINTER_INFO info;
    UINT32 count, i, j;
    HANDLE thread;
    HWND other = NULL;
    BOOL ret;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("SM_DIGITIZER %#x, SM_MAXIMUMTOUCHES %d, IsMouseInPointerEnabled %d\n", GetSystemMetrics(SM_DIGITIZER),
           GetSystemMetrics(SM_MAXIMUMTOUCHES), IsMouseInPointerEnabled());

    SetLastError(0xdeadbeef);
    ret = GetPointerDevices(NULL, NULL);
    show_bool("GetPointerDevices NULL count", ret);
    count = 0xdeadbeef;
    SetLastError(0xdeadbeef);
    ret = GetPointerDevices(&count, NULL);
    printf("GetPointerDevices no array: %d count %u error %lu\n", ret, count, GetLastError());
    count = 0;
    SetLastError(0xdeadbeef);
    ret = GetPointerDevices(&count, devices);
    printf("GetPointerDevices an array of 0: %d count %u error %lu\n", ret, count, GetLastError());
    count = ARRAYSIZE(devices);
    memset(devices, 0, sizeof(devices));
    SetLastError(0xdeadbeef);
    ret = GetPointerDevices(&count, devices);
    printf("GetPointerDevices an array of 16: %d count %u error %lu\n", ret, count, GetLastError());
    for (i = 0; ret && i < count; i++)
    {
        POINTER_DEVICE_PROPERTY props[32];
        POINTER_DEVICE_CURSOR_INFO cursors[8];
        POINTER_DEVICE_INFO one;
        RECT device_rect, display_rect;
        UINT32 n;

        printf("  device: type %d, start cursor %lu, contacts %u, product of %u characters, display orientation %lu,"
               " monitor %s\n", devices[i].pointerDeviceType, devices[i].startingCursorId, devices[i].maxActiveContacts,
               (unsigned int)wcslen(devices[i].productString), devices[i].displayOrientation,
               devices[i].monitor ? "set" : "NULL");
        ret = GetPointerDevice(devices[i].device, &one);
        printf("    GetPointerDevice: %d same %d\n", ret, ret && !memcmp(&one, &devices[i], sizeof(one)));
        n = 0;
        ret = GetPointerDeviceProperties(devices[i].device, &n, NULL);
        printf("    GetPointerDeviceProperties: %d count %u\n", ret, n);
        if (ret && n <= ARRAYSIZE(props) && GetPointerDeviceProperties(devices[i].device, &n, props))
            for (j = 0; j < n; j++)
                printf("      usage page %#x usage %#x, logical %d..%d, physical %d..%d, unit %u exponent %u\n",
                       props[j].usagePageId, props[j].usageId, props[j].logicalMin, props[j].logicalMax,
                       props[j].physicalMin, props[j].physicalMax, props[j].unit, props[j].unitExponent);
        ret = GetPointerDeviceRects(devices[i].device, &device_rect, &display_rect);
        printf("    GetPointerDeviceRects: %d, device %ldx%ld, display %ldx%ld\n", ret,
               device_rect.right - device_rect.left, device_rect.bottom - device_rect.top,
               display_rect.right - display_rect.left, display_rect.bottom - display_rect.top);
        n = ARRAYSIZE(cursors);
        ret = GetPointerDeviceCursors(devices[i].device, &n, cursors);
        printf("    GetPointerDeviceCursors: %d count %u\n", ret, n);
        for (j = 0; ret && j < n; j++) printf("      cursor %u id %d\n", cursors[j].cursorId, cursors[j].cursor);
    }
    SetLastError(0xdeadbeef);
    ret = GetPointerDevice(NULL, devices);
    show_bool("GetPointerDevice NULL", ret);
    SetLastError(0xdeadbeef);
    ret = GetPointerDevice((HANDLE)0xdead, devices);
    show_bool("GetPointerDevice bad", ret);
    count = 0;
    SetLastError(0xdeadbeef);
    ret = GetPointerDeviceProperties((HANDLE)0xdead, &count, NULL);
    show_bool("GetPointerDeviceProperties bad", ret);
    {
        RECT r1, r2;
        SetLastError(0xdeadbeef);
        ret = GetPointerDeviceRects((HANDLE)0xdead, &r1, &r2);
        show_bool("GetPointerDeviceRects bad", ret);
    }

    window = CreateWindowExW(0, L"static", L"pointer", WS_POPUP, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
    thread = CreateThread(NULL, 0, other_thread, &other, 0, NULL);
    while (!other) Sleep(10);
    SetLastError(0xdeadbeef);
    show_bool("RegisterPointerDeviceNotifications own window", RegisterPointerDeviceNotifications(window, FALSE));
    SetLastError(0xdeadbeef);
    show_bool("  again, range", RegisterPointerDeviceNotifications(window, TRUE));
    SetLastError(0xdeadbeef);
    show_bool("RegisterPointerDeviceNotifications another thread's window", RegisterPointerDeviceNotifications(other, FALSE));
    SetLastError(0xdeadbeef);
    show_bool("RegisterPointerDeviceNotifications the desktop", RegisterPointerDeviceNotifications(GetDesktopWindow(), FALSE));
    SetLastError(0xdeadbeef);
    show_bool("RegisterPointerDeviceNotifications NULL", RegisterPointerDeviceNotifications(NULL, FALSE));
    SetLastError(0xdeadbeef);
    show_bool("RegisterPointerDeviceNotifications bad", RegisterPointerDeviceNotifications((HWND)0xdead, FALSE));
    PostThreadMessageW(GetThreadId(thread), WM_QUIT, 0, 0);
    WaitForSingleObject(thread, 5000);

    for (i = 0; i < 3; i++)
    {
        static const UINT32 ids[] = { 0, 1, 100 };
        type = 0xdead;
        SetLastError(0xdeadbeef);
        ret = GetPointerType(ids[i], &type);
        printf("GetPointerType %u: %d type %d error %lu\n", ids[i], ret, type, ret ? 0 : GetLastError());
        SetLastError(0xdeadbeef);
        ret = GetPointerInfo(ids[i], &info);
        printf("GetPointerInfo %u: %d error %lu\n", ids[i], ret, ret ? 0 : GetLastError());
    }
    SetLastError(0xdeadbeef);
    show_bool("EnableMouseInPointer FALSE", EnableMouseInPointer(FALSE));
    SetLastError(0xdeadbeef);
    show_bool("EnableMouseInPointer TRUE", EnableMouseInPointer(TRUE));
    printf("IsMouseInPointerEnabled %d\n", IsMouseInPointerEnabled());
    SetLastError(0xdeadbeef);
    show_bool("EnableMouseInPointer FALSE after", EnableMouseInPointer(FALSE));
    SetLastError(0xdeadbeef);
    show_bool("EnableMouseInPointer TRUE again", EnableMouseInPointer(TRUE));
    type = 0xdead;
    SetLastError(0xdeadbeef);
    ret = GetPointerType(1, &type);
    printf("GetPointerType 1 with mouse in pointer: %d type %d error %lu\n", ret, type, ret ? 0 : GetLastError());
    SetLastError(0xdeadbeef);
    ret = GetPointerInfo(1, &info);
    printf("GetPointerInfo 1 with mouse in pointer: %d error %lu\n", ret, ret ? 0 : GetLastError());
    count = ARRAYSIZE(devices);
    ret = GetPointerDevices(&count, devices);
    printf("GetPointerDevices with mouse in pointer: %d count %u\n", ret, count);
    return 0;
}
