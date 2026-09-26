/* exportlist: the names a system DLL exports that contain a given string, read from the file.
 *   exportlist.exe ntdll.dll Lang
 */
#include <windows.h>
#include <stdio.h>

static DWORD rva_to_offset( IMAGE_SECTION_HEADER *sec, unsigned int count, DWORD rva )
{
    unsigned int i;

    for (i = 0; i < count; i++)
        if (rva >= sec[i].VirtualAddress && rva < sec[i].VirtualAddress + max( sec[i].Misc.VirtualSize, sec[i].SizeOfRawData ))
            return rva - sec[i].VirtualAddress + sec[i].PointerToRawData;
    return 0;
}

int main( int argc, char **argv )
{
    char path[MAX_PATH];
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS64 *nt;
    IMAGE_SECTION_HEADER *sec;
    IMAGE_EXPORT_DIRECTORY *exp;
    DWORD size, read, *names, i;
    unsigned char *data;
    HANDLE file;

    if (argc < 3) return 1;
    GetSystemDirectoryA( path, MAX_PATH );
    strcat( path, "\\" );
    strcat( path, argv[1] );
    file = CreateFileA( path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
    if (file == INVALID_HANDLE_VALUE) return 2;
    size = GetFileSize( file, NULL );
    data = malloc( size );
    ReadFile( file, data, size, &read, NULL );
    CloseHandle( file );
    dos = (IMAGE_DOS_HEADER *)data;
    nt = (IMAGE_NT_HEADERS64 *)(data + dos->e_lfanew);
    sec = (IMAGE_SECTION_HEADER *)((char *)&nt->OptionalHeader + nt->FileHeader.SizeOfOptionalHeader);
    exp = (IMAGE_EXPORT_DIRECTORY *)(data + rva_to_offset( sec, nt->FileHeader.NumberOfSections,
                                     nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress ));
    names = (DWORD *)(data + rva_to_offset( sec, nt->FileHeader.NumberOfSections, exp->AddressOfNames ));
    for (i = 0; i < exp->NumberOfNames; i++)
    {
        const char *name = (char *)data + rva_to_offset( sec, nt->FileHeader.NumberOfSections, names[i] );
        if (strstr( name, argv[2] )) printf( "%s\n", name );
    }
    return 0;
}
