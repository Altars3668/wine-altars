/* exportnameprobe: the name a DLL's export directory gives itself, read from the file.
 *
 * Wine's loader asks for the builtin behind a file in system32 by this name, so it has to be
 * the module's file name, extension included.  The file is read and parsed by hand, so the
 * answer does not depend on how anything gets loaded.
 */
#include <windows.h>
#include <stdio.h>

static const WCHAR *default_dlls[] =
{
    L"kernel32.dll", L"Windows.Globalization.dll", L"Windows.Media.Speech.dll",
    L"twinapi.appcore.dll", L"Windows.UI.dll", L"Windows.Networking.dll", L"Windows.Web.dll",
    L"Windows.Gaming.Input.dll", L"winspool.drv", L"ntoskrnl.exe",
};

static DWORD rva_to_offset( IMAGE_SECTION_HEADER *sec, unsigned int count, DWORD rva )
{
    unsigned int i;

    for (i = 0; i < count; i++)
        if (rva >= sec[i].VirtualAddress && rva < sec[i].VirtualAddress + max( sec[i].Misc.VirtualSize, sec[i].SizeOfRawData ))
            return rva - sec[i].VirtualAddress + sec[i].PointerToRawData;
    return 0;
}

static void probe( const WCHAR *name )
{
    WCHAR path[MAX_PATH];
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS64 *nt64;
    IMAGE_SECTION_HEADER *sec;
    IMAGE_DATA_DIRECTORY *dir;
    IMAGE_EXPORT_DIRECTORY *exp;
    DWORD size, read, off, err;
    unsigned char *data;
    HANDLE file;

    GetSystemDirectoryW( path, MAX_PATH );
    lstrcatW( path, L"\\" );
    lstrcatW( path, name );
    file = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
    err = GetLastError();
    if (file == INVALID_HANDLE_VALUE)
    {
        printf( "%ls: open error %lu\n", name, err );
        return;
    }
    size = GetFileSize( file, NULL );
    data = malloc( size );
    ReadFile( file, data, size, &read, NULL );
    CloseHandle( file );

    dos = (IMAGE_DOS_HEADER *)data;
    nt64 = (IMAGE_NT_HEADERS64 *)(data + dos->e_lfanew);
    if (nt64->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        dir = &nt64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    else
        dir = &((IMAGE_NT_HEADERS32 *)nt64)->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    sec = (IMAGE_SECTION_HEADER *)((char *)&nt64->OptionalHeader + nt64->FileHeader.SizeOfOptionalHeader);

    if (!dir->VirtualAddress || !(off = rva_to_offset( sec, nt64->FileHeader.NumberOfSections, dir->VirtualAddress )))
    {
        printf( "%ls: no export directory\n", name );
        free( data );
        return;
    }
    exp = (IMAGE_EXPORT_DIRECTORY *)(data + off);
    off = rva_to_offset( sec, nt64->FileHeader.NumberOfSections, exp->Name );
    printf( "%ls: export name \"%s\"\n", name, off ? (char *)data + off : "(none)" );
    free( data );
}

int main( int argc, char **argv )
{
    unsigned int i;

    if (argc > 1)
    {
        for (i = 1; i < argc; i++)
        {
            WCHAR name[MAX_PATH];
            MultiByteToWideChar( CP_ACP, 0, argv[i], -1, name, MAX_PATH );
            probe( name );
        }
    }
    else for (i = 0; i < ARRAY_SIZE(default_dlls); i++) probe( default_dlls[i] );
    return 0;
}
