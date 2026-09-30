/* What NtQueryInformationProcess(ProcessCommandLineInformation) answers.
 *
 * Wine did not have the class; WMI's Win32_Process.CommandLine was empty for every process but the
 * caller's.  This prints, for the probe itself: the status and length for buffers of 0 bytes, one byte
 * short of a UNICODE_STRING, a UNICODE_STRING, one byte short and exactly what the call asked for, then
 * Length, MaximumLength, where Buffer points and what follows the string; whether it reads the live PEB
 * (the probe changes a character of its own command line in place and asks again); and for a child
 * created suspended, through a handle with PROCESS_QUERY_LIMITED_INFORMATION and one with only
 * SYNCHRONIZE.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror cmdlineprobe.c -o cmdlineprobe.exe
 *   i686-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror cmdlineprobe.c -o cmdlineprobe32.exe
 */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <wchar.h>

#define ProcessCommandLineInformation 60

static NTSTATUS (WINAPI *query)(HANDLE, PROCESSINFOCLASS, void *, ULONG, ULONG *);

static void show( const char *what, HANDLE process, ULONG size )
{
    static union { UNICODE_STRING str; char bytes[4096]; } buf;
    ULONG len = 0xdeadbeef;
    NTSTATUS status;

    memset( &buf, 0xcc, sizeof(buf) );
    status = query( process, ProcessCommandLineInformation, size ? &buf : NULL, size, &len );
    printf( "%-40s size %4lu: status %#010lx, length %lu", what, size, (unsigned long)status, len );
    if (!status)
    {
        const WCHAR *end = (const WCHAR *)((const char *)buf.str.Buffer + buf.str.Length);
        printf( ", Length %u, MaximumLength %u, Buffer %s, then %#06x, \"%ls\"", buf.str.Length, buf.str.MaximumLength,
                buf.str.Buffer == (WCHAR *)(&buf.str + 1) ? "after the header" : "elsewhere", *end,
                buf.str.Length < 400 ? buf.str.Buffer : L"(long)" );
    }
    printf( "\n" );
}

int main(void)
{
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    WCHAR path[MAX_PATH], cmd[MAX_PATH + 64];
    ULONG len = 0;
    HANDLE limited, sync;
    WCHAR *cmdline;

    memset( &si, 0, sizeof(si) );
    si.cb = sizeof(si);
    query = (void *)GetProcAddress( GetModuleHandleA( "ntdll" ), "NtQueryInformationProcess" );
    printf( "sizeof(UNICODE_STRING) %u\n", (unsigned int)sizeof(UNICODE_STRING) );
    query( GetCurrentProcess(), ProcessCommandLineInformation, NULL, 0, &len );
    show( "self", GetCurrentProcess(), 0 );
    show( "self", GetCurrentProcess(), sizeof(UNICODE_STRING) - 1 );
    show( "self", GetCurrentProcess(), sizeof(UNICODE_STRING) );
    if (len > sizeof(UNICODE_STRING)) show( "self", GetCurrentProcess(), len - 1 );
    show( "self", GetCurrentProcess(), len );

    cmdline = GetCommandLineW();
    if (*cmdline)
    {
        WCHAR saved = cmdline[1];
        cmdline[1] = L'#';
        show( "self, a character changed in place", GetCurrentProcess(), 4096 );
        cmdline[1] = saved;
    }

    GetModuleFileNameW( NULL, path, MAX_PATH );
    swprintf( cmd, MAX_PATH + 64, L"\"%ls\" child  with   spaces", path );
    if (!CreateProcessW( NULL, cmd, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi ))
    {
        printf( "CreateProcess failed %lu\n", GetLastError() );
        return 1;
    }
    limited = OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pi.dwProcessId );
    sync = OpenProcess( SYNCHRONIZE, FALSE, pi.dwProcessId );
    show( "suspended child, limited query access", limited, 4096 );
    show( "suspended child, SYNCHRONIZE only", sync, 4096 );
    TerminateProcess( pi.hProcess, 0 );
    CloseHandle( limited );
    CloseHandle( sync );
    CloseHandle( pi.hProcess );
    CloseHandle( pi.hThread );
    return 0;
}
