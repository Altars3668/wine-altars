/* zwatch [seconds] : print the Win32 Z order of the visible OpusApp windows, and the foreground
 * window, each time it changes (sampled every 20 ms) with GetTickCount, which is the clock
 * WINEDEBUG=+timestamp prints: a change can be matched to the trace line that made it. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 30;
    char last[1024] = "";
    DWORD start = GetTickCount();
    setvbuf(stdout, NULL, _IONBF, 0);
    while (GetTickCount() - start < (DWORD)secs * 1000)
    {
        char cur[1024] = "", cls[64];
        HWND h;
        for (h = GetTopWindow(NULL); h; h = GetWindow(h, GW_HWNDNEXT))
        {
            if (!IsWindowVisible(h)) continue;
            GetClassNameA(h, cls, sizeof(cls));
            if (strcmp(cls, "OpusApp")) continue;
            sprintf(cur + strlen(cur), " %lx", (unsigned long)(ULONG_PTR)h);
        }
        sprintf(cur + strlen(cur), " fg=%lx", (unsigned long)(ULONG_PTR)GetForegroundWindow());
        if (strcmp(cur, last)) { printf("%lu:%s\n", GetTickCount(), cur); strcpy(last, cur); }
        Sleep(20);
    }
    return 0;
}
