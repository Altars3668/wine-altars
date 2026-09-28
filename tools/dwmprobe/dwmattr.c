/*
 * What DwmGetWindowAttribute and DwmSetWindowAttribute answer: for each attribute up to 40, on a top-level window,
 * a get with a 4-byte and a 16-byte buffer, then sets of a few values and the get after each; and the edge cases
 * of a child window, a destroyed one, no buffer and short buffers.  Run it in a desktop session: the service
 * session has no composition.
 */
#include <windows.h>
#include <dwmapi.h>
#include <stdio.h>

static void get(HWND hwnd, DWORD attr, DWORD size)
{
    BYTE buffer[32];
    HRESULT hr;
    DWORD i;

    memset(buffer, 0xcc, sizeof(buffer));
    hr = DwmGetWindowAttribute(hwnd, attr, buffer, size);
    printf("  get %2lu size %2lu: hr %#lx", attr, size, hr);
    if (SUCCEEDED(hr))
    {
        printf(" =");
        for (i = 0; i < size && i < 16; i += 4) printf(" %08lx", *(DWORD *)(buffer + i));
    }
    printf("\n");
}

static void set(HWND hwnd, DWORD attr, DWORD value, DWORD size)
{
    DWORD buffer[4] = { value, value, value, value };
    HRESULT hr = DwmSetWindowAttribute(hwnd, attr, buffer, size);
    printf("  set %2lu = %#lx size %lu: hr %#lx\n", attr, value, size, hr);
}

int main(void)
{
    static const DWORD values[] = { 0, 1, 2, 3, 4, 0xffffffff, 0xfffffffe, 0x00ff0000 };
    HWND hwnd, child;
    BOOL enabled = FALSE;
    DWORD attr, i;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    hr = DwmIsCompositionEnabled(&enabled);
    printf("DwmIsCompositionEnabled: hr %#lx enabled %d\n", hr, enabled);
    hwnd = CreateWindowExW(0, L"static", L"dwm", WS_OVERLAPPEDWINDOW, 100, 100, 300, 200, NULL, NULL, NULL, NULL);
    child = CreateWindowExW(0, L"static", L"child", WS_CHILD | WS_VISIBLE, 10, 10, 50, 50, hwnd, NULL, NULL, NULL);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(hwnd);

    printf("== gets, before anything is set\n");
    for (attr = 0; attr <= 40; ++attr)
    {
        get(hwnd, attr, 4);
        get(hwnd, attr, 16);
    }

    printf("== sets\n");
    for (attr = 0; attr <= 40; ++attr)
    {
        for (i = 0; i < ARRAY_SIZE(values); ++i)
        {
            set(hwnd, attr, values[i], 4);
            get(hwnd, attr, 4);
        }
        set(hwnd, attr, 1, 2);
        set(hwnd, attr, 1, 8);
    }

    printf("== edges\n");
    hr = DwmGetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, NULL, 4);
    printf(" get 33 NULL: hr %#lx\n", hr);
    hr = DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, NULL, 4);
    printf(" set 33 NULL: hr %#lx\n", hr);
    for (attr = 1; attr <= 38; ++attr)
    {
        DWORD value = 1;
        HRESULT hr2;

        hr = DwmSetWindowAttribute(child, attr, &value, 4);
        hr2 = DwmGetWindowAttribute(child, attr, &value, 4);
        printf(" child %2lu: set hr %#lx, get hr %#lx\n", attr, hr, hr2);
    }
    hr = DwmSetWindowAttribute(NULL, DWMWA_WINDOW_CORNER_PREFERENCE, &i, 4);
    printf(" set NULL hwnd: hr %#lx\n", hr);
    hr = DwmGetWindowAttribute(NULL, DWMWA_WINDOW_CORNER_PREFERENCE, &i, 4);
    printf(" get NULL hwnd: hr %#lx\n", hr);
    DestroyWindow(hwnd);
    hr = DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &i, 4);
    printf(" set destroyed: hr %#lx\n", hr);
    hr = DwmGetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &i, 4);
    printf(" get destroyed: hr %#lx\n", hr);
    printf("done\n");
    return 0;
}
