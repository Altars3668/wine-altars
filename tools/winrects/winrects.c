/*
 * The window and client rectangles of every visible top-level window of a class, in screen coordinates, with its
 * styles: what the frame an application kept for itself is.
 *
 *   winrects [class]
 */
#include <windows.h>
#include <stdio.h>

static const WCHAR *class_filter;

static BOOL CALLBACK print_window(HWND hwnd, LPARAM param)
{
    WCHAR class[256];
    RECT window, client;
    POINT origin = {0, 0};

    if (!IsWindowVisible(hwnd)) return TRUE;
    GetClassNameW(hwnd, class, ARRAY_SIZE(class));
    if (class_filter && wcscmp(class, class_filter)) return TRUE;
    GetWindowRect(hwnd, &window);
    GetClientRect(hwnd, &client);
    ClientToScreen(hwnd, &origin);
    OffsetRect(&client, origin.x, origin.y);
    printf("%p %ls style %08lx ex %08lx window (%ld,%ld)-(%ld,%ld) client (%ld,%ld)-(%ld,%ld) frame l %ld t %ld r %ld b %ld\n",
           hwnd, class, GetWindowLongW(hwnd, GWL_STYLE), GetWindowLongW(hwnd, GWL_EXSTYLE),
           window.left, window.top, window.right, window.bottom, client.left, client.top, client.right, client.bottom,
           client.left - window.left, client.top - window.top, window.right - client.right, window.bottom - client.bottom);
    return TRUE;
}

int main(int argc, char **argv)
{
    static WCHAR filter[256];

    if (argc > 1)
    {
        MultiByteToWideChar(CP_ACP, 0, argv[1], -1, filter, ARRAY_SIZE(filter));
        class_filter = filter;
    }
    EnumWindows(print_window, 0);
    return 0;
}
