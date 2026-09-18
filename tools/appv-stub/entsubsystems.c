/* A presence marker for AppVEntSubsystems64.dll.
 *
 * Office decides whether it is a Click-to-Run install by asking
 * GetModuleHandleW whether AppVEntSubsystems32/64.dll is loaded in its own
 * process -- nothing more. That dll belongs to the Windows App-V client, which
 * Wine does not have, so the answer is always no and every Office app reports
 * its release audience as "Not_C2R" while OLicenseHeartbeat.exe, which asks a
 * different way, reports the real one.
 *
 * The check reads a module handle and never calls into the module, so what it
 * wants is a module by that name, not an App-V implementation. This provides
 * exactly that and nothing else. It is pulled in as a static import of the
 * AppvIsvSubsystems64 stand-in so the loader maps it before any DllMain runs;
 * LoadLibrary from inside DllMain would be the deadlock-prone way to do it.
 */
#include <windows.h>

/* referenced once by the ISV stand-in purely to create the import */
BOOL WINAPI AppVEntSubsystemsPresent(void) { return TRUE; }

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r)
{
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(h);
    return TRUE;
}
