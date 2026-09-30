/* The DLL dllsyncprobe.exe loads: its entry point only tells the program which call it got, so that
 * the program can ask PrivIsDllSynchronizationHeld from there and see the order of the calls.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror -shared dllsyncprobe_dll.c -o dllsyncprobe_dll.dll
 */
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    void (*reached)(DWORD, void *) = (void *)GetProcAddress(GetModuleHandleW(NULL), "dll_main_reached");

    (void)instance;
    if (reached) reached(reason, reserved);
    return TRUE;
}
