/* trustscan: WinVerifyTrust (generic verify v2, no UI, no revocation, cached URLs only) on every .exe and .dll
 * under the directories given, and what it said, one line a file; the end tallies the answers.
 * Usage: trustscan <directory>...   Prints results only. */
#include <windows.h>
#include <wintrust.h>
#include <softpub.h>
#include <stdio.h>

static unsigned int counts[16], total;
static LONG results[16];

static void verify( const WCHAR *path )
{
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_FILE_INFO file = { sizeof(file), path };
    WINTRUST_DATA data = { sizeof(data) };
    unsigned int i;
    LONG ret;

    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &file;
    data.dwStateAction = WTD_STATEACTION_IGNORE;
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    ret = WinVerifyTrust( INVALID_HANDLE_VALUE, &action, &data );
    printf( "%#lx %ls\n", ret, path );
    total++;
    for (i = 0; i < ARRAYSIZE(results); i++)
    {
        if (counts[i] && results[i] != ret) continue;
        results[i] = ret;
        counts[i]++;
        break;
    }
}

static void walk( const WCHAR *dir )
{
    WCHAR path[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE find;

    swprintf( path, MAX_PATH, L"%ls\\*", dir );
    if ((find = FindFirstFileW( path, &fd )) == INVALID_HANDLE_VALUE) return;
    do
    {
        size_t len = wcslen( fd.cFileName );

        if (!wcscmp( fd.cFileName, L"." ) || !wcscmp( fd.cFileName, L".." )) continue;
        swprintf( path, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName );
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) walk( path );
        }
        else if (len > 4 && (!_wcsicmp( fd.cFileName + len - 4, L".exe" ) || !_wcsicmp( fd.cFileName + len - 4, L".dll" )))
            verify( path );
    } while (FindNextFileW( find, &fd ));
    FindClose( find );
}

int wmain( int argc, WCHAR **argv )
{
    unsigned int i;

    for (i = 1; i < argc; i++) walk( argv[i] );
    printf( "%u files\n", total );
    for (i = 0; i < ARRAYSIZE(results) && counts[i]; i++) printf( "  %#lx: %u\n", results[i], counts[i] );
    return 0;
}
