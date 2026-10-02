/* unsigned: a DLL with no signature, for misc.exe to load with LOAD_LIBRARY_REQUIRE_SIGNED_TARGET. */
#include <windows.h>

__declspec(dllexport) int unsigned_probe(void)
{
    return 42;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    return TRUE;
}
