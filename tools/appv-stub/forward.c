/* A logging pass-through in front of Office's real AppvIsvSubsystems64.dll.
 *
 * Pure instrumentation: every call is forwarded to the shipped dll, renamed
 * alongside as AppvIsvSubsystems64.dll.orig, and the answer it gave is written
 * to Z:\tmp\appv-calls.log. Nothing is changed, so a run with this in place is
 * still a measurement of Office's own virtualisation layer -- which is the
 * point, because the question is what that layer *answers*, not what a stub
 * would answer in its place.
 */
#include <windows.h>
#include <stdio.h>

static HMODULE real;
static CRITICAL_SECTION lock;

static void applog(const char *fmt, ...)
{
    char buf[512]; va_list ap; HANDLE f; DWORD n;
    va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    OutputDebugStringA(buf);
    EnterCriticalSection(&lock);
    f = CreateFileA("Z:\\tmp\\appv-calls.log", FILE_APPEND_DATA, FILE_SHARE_READ|FILE_SHARE_WRITE,
                    NULL, OPEN_ALWAYS, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) { WriteFile(f, buf, (DWORD)lstrlenA(buf), &n, NULL); CloseHandle(f); }
    LeaveCriticalSection(&lock);
}

static FARPROC pick(const char *name)
{
    if (!real)
    {
        WCHAR path[MAX_PATH]; DWORD n;
        n = GetModuleFileNameW(GetModuleHandleW(L"AppvIsvSubsystems64.dll"), path, MAX_PATH);
        if (n && n < MAX_PATH - 8) { lstrcatW(path, L".orig"); real = LoadLibraryW(path); }
        applog("appv: loaded real dll %p\n", real);
    }
    return real ? GetProcAddress(real, name) : NULL;
}

int WINAPI APIExportForDetours(void)
{
    int (WINAPI *fn)(void) = (void *)pick("APIExportForDetours");
    int r = fn ? fn() : 1;
    applog("appv: APIExportForDetours -> %d\n", r);
    return r;
}
HRESULT WINAPI RequestUnhookedFunctionList(void *a, void *b)
{
    HRESULT (WINAPI *fn)(void *, void *) = (void *)pick("RequestUnhookedFunctionList");
    HRESULT r = fn ? fn(a, b) : S_OK;
    applog("appv: RequestUnhookedFunctionList -> %#lx\n", (unsigned long)r);
    return r;
}
BOOL WINAPI VirtualizeCurrentThread(BOOL v)
{
    BOOL (WINAPI *fn)(BOOL) = (void *)pick("VirtualizeCurrentThread");
    BOOL r = fn ? fn(v) : FALSE;
    applog("appv: VirtualizeCurrentThread(%d) -> %d\n", v, r);
    return r;
}
BOOL WINAPI CurrentThreadIsVirtualized(void)
{
    BOOL (WINAPI *fn)(void) = (void *)pick("CurrentThreadIsVirtualized");
    BOOL r = fn ? fn() : FALSE;
    applog("appv: CurrentThreadIsVirtualized -> %d\n", r);
    return r;
}
BOOL WINAPI VirtualizeCurrentProcess(void *a)
{
    BOOL (WINAPI *fn)(void *) = (void *)pick("VirtualizeCurrentProcess");
    BOOL r = fn ? fn(a) : FALSE;
    applog("appv: VirtualizeCurrentProcess -> %d\n", r);
    return r;
}
HRESULT WINAPI GetPhysicalPath(const WCHAR *in, WCHAR *out, DWORD *len)
{
    HRESULT (WINAPI *fn)(const WCHAR *, WCHAR *, DWORD *) = (void *)pick("GetPhysicalPath");
    HRESULT r = fn ? fn(in, out, len) : E_NOTIMPL;
    applog("appv: GetPhysicalPath -> %#lx\n", (unsigned long)r);
    return r;
}
BOOL WINAPI IsProcessHooked(void)
{
    BOOL (WINAPI *fn)(void) = (void *)pick("IsProcessHooked");
    BOOL r = fn ? fn() : FALSE;
    applog("appv: IsProcessHooked -> %d\n", r);
    return r;
}

/* Office asks GetModuleHandleW whether AppVEntSubsystems64.dll is loaded to
 * decide whether it is a Click-to-Run install. Referencing it here makes it a
 * static import of this dll, so the loader maps it before any DllMain runs --
 * which is what the check needs and is safe, unlike LoadLibrary from DllMain. */
BOOL WINAPI AppVEntSubsystemsPresent(void);
BOOL (WINAPI *keep_ent_import_alive)(void) = AppVEntSubsystemsPresent;

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        InitializeCriticalSection(&lock);
        DisableThreadLibraryCalls(h);
        applog("appv: attached to pid %lu\n", (unsigned long)GetCurrentProcessId());
    }
    return TRUE;
}
