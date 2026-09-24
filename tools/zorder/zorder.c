/* zorder : the visible top-level windows from the top of the Win32 Z order down, with class,
 * rect and extended style, and the foreground window.  Run in the Wine session being looked at. */
#include <windows.h>
#include <stdio.h>
int main(void)
{
    HWND h = GetTopWindow(NULL);
    printf("foreground %p active-in-this-thread n/a\n", GetForegroundWindow());
    for (; h; h = GetWindow(h, GW_HWNDNEXT))
    {
        char cls[64]; RECT r;
        if (!IsWindowVisible(h)) continue;
        GetClassNameA(h, cls, sizeof(cls)); GetWindowRect(h, &r);
        printf("%p %-30s (%ld,%ld)-(%ld,%ld) ex=%08lx\n", h, cls, r.left, r.top, r.right, r.bottom, GetWindowLongA(h, GWL_EXSTYLE));
    }
    return 0;
}
