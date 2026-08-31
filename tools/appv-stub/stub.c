/* A do-nothing stand-in for Office's AppvIsvSubsystems64.dll.
 *
 * That dll is Click-to-Run's runtime virtualisation layer: it Detours a large
 * set of Win32 APIs so that a process sees the package's root\vfs projected
 * over the real filesystem and root\vreg over the real registry.
 *
 * This prefix has both of those unfolded for real -- vfs copied to where it
 * would have been projected, vreg imported into HKLM -- so the virtualisation
 * has nothing left to do, and its DllMain fails here anyway. Reporting "this
 * process is not virtualised" is both true and what the callers can handle.
 */
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r)
{
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(h);
    return TRUE;
}

/* the real one is literally "mov eax,1; ret" -- read from the shipped dll */
int WINAPI APIExportForDetours(void) { return 1; }
HRESULT WINAPI RequestUnhookedFunctionList(void *a, void *b) { return S_OK; }
BOOL WINAPI VirtualizeCurrentThread(BOOL v) { return FALSE; }
BOOL WINAPI CurrentThreadIsVirtualized(void) { return FALSE; }
BOOL WINAPI VirtualizeCurrentProcess(void *a) { return FALSE; }
HRESULT WINAPI GetPhysicalPath(const WCHAR *in, WCHAR *out, DWORD *len)
{
    /* nothing is virtualised, so the physical path is the path */
    DWORD n;
    if (!in || !len) return E_INVALIDARG;
    n = lstrlenW(in) + 1;
    if (!out || *len < n) { *len = n; return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER); }
    lstrcpyW(out, in); *len = n - 1;
    return S_OK;
}
BOOL WINAPI IsProcessHooked(void) { return FALSE; }
