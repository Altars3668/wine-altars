/* Which properties a shell item's property store has on Windows, for a file and a folder.
 *
 * Wine's IShellItem2 property methods are all stubs; Office asks every shell item it shows for a store
 * with GPS_FASTPROPERTIESONLY.  This lists every key (by canonical name) and value of the stores
 * SHGetPropertyStoreFromParsingName gives with GPS_FASTPROPERTIESONLY and GPS_DEFAULT for a new text file
 * and a new folder under %TEMP%, and what IShellItem2::GetString, GetUInt64 and GetFileTime answer for a
 * few keys.  Both are removed at the end.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror propstoreprobe.c -o propstoreprobe.exe \
 *       -lshell32 -lpropsys -lole32 -luuid
 */
#define COBJMACROS
#include <windows.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>
#include <stdio.h>

static void print_w( const WCHAR *str )
{
    for (; *str; str++)
        if (*str >= 32 && *str < 127) printf( "%c", (char)*str );
        else printf( "\\x%04x", *str );
}

static void list_store( const WCHAR *path, GETPROPERTYSTOREFLAGS flags, const char *name )
{
    IPropertyStore *store;
    DWORD count = 0, i;
    HRESULT hr;

    hr = SHGetPropertyStoreFromParsingName( path, NULL, flags, &IID_IPropertyStore, (void **)&store );
    printf( "%s: hr %#010lx", name, (unsigned long)hr );
    if (FAILED(hr)) { printf( "\n" ); return; }
    IPropertyStore_GetCount( store, &count );
    printf( ", %lu properties\n", count );
    for (i = 0; i < count; i++)
    {
        PROPERTYKEY key;
        PROPVARIANT value;
        WCHAR *text = NULL, *canonical = NULL;

        if (FAILED(IPropertyStore_GetAt( store, i, &key ))) continue;
        PropVariantInit( &value );
        IPropertyStore_GetValue( store, &key, &value );
        printf( "  " );
        if (SUCCEEDED(PSGetNameFromPropertyKey( &key, &canonical )) && canonical) print_w( canonical );
        else printf( "{%08lx-...} %lu", key.fmtid.Data1, key.pid );
        printf( ": vt %#x", value.vt );
        if (value.vt == VT_FILETIME) printf( " (a time)" );
        else if (SUCCEEDED(PropVariantToStringAlloc( &value, &text )) && text)
        {
            /* the path and name of the temporary file are the probe's own */
            printf( " \"" );
            if (wcslen( text ) > 60) { text[60] = 0; print_w( text ); printf( "..." ); }
            else print_w( text );
            printf( "\"" );
        }
        printf( "\n" );
        CoTaskMemFree( text );
        CoTaskMemFree( canonical );
        PropVariantClear( &value );
    }
    IPropertyStore_Release( store );
}

static void typed( const WCHAR *path, const char *name )
{
    IShellItem2 *item;
    ULONGLONG size;
    FILETIME time;
    WCHAR *text;
    HRESULT hr;

    if (FAILED(hr = SHCreateItemFromParsingName( path, NULL, &IID_IShellItem2, (void **)&item )))
    {
        printf( "%s: SHCreateItemFromParsingName %#lx\n", name, (unsigned long)hr );
        return;
    }
    hr = IShellItem2_GetString( item, &PKEY_ItemNameDisplay, &text );
    printf( "%s GetString(ItemNameDisplay): %#010lx", name, (unsigned long)hr );
    if (SUCCEEDED(hr)) { printf( " \"" ); print_w( text ); printf( "\"" ); CoTaskMemFree( text ); }
    hr = IShellItem2_GetString( item, &PKEY_ItemType, &text );
    printf( "\n%s GetString(ItemType): %#010lx", name, (unsigned long)hr );
    if (SUCCEEDED(hr)) { printf( " \"" ); print_w( text ); printf( "\"" ); CoTaskMemFree( text ); }
    hr = IShellItem2_GetUInt64( item, &PKEY_Size, &size );
    printf( "\n%s GetUInt64(Size): %#010lx", name, (unsigned long)hr );
    if (SUCCEEDED(hr)) printf( " %llu", size );
    hr = IShellItem2_GetFileTime( item, &PKEY_DateModified, &time );
    printf( "\n%s GetFileTime(DateModified): %#010lx\n", name, (unsigned long)hr );
    IShellItem2_Release( item );
}

int main(void)
{
    WCHAR temp[MAX_PATH], file[MAX_PATH], dir[MAX_PATH];
    HANDLE handle;
    DWORD written;

    CoInitializeEx( NULL, COINIT_APARTMENTTHREADED );
    GetTempPathW( MAX_PATH, temp );
    swprintf( file, MAX_PATH, L"%lspropstore-%lu.txt", temp, GetCurrentProcessId() );
    swprintf( dir, MAX_PATH, L"%lspropstore-%lu", temp, GetCurrentProcessId() );
    handle = CreateFileW( file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL );
    WriteFile( handle, "twelve bytes", 12, &written, NULL );
    CloseHandle( handle );
    CreateDirectoryW( dir, NULL );

    list_store( file, GPS_FASTPROPERTIESONLY, "file, fast" );
    list_store( file, GPS_DEFAULT, "file, default" );
    list_store( dir, GPS_FASTPROPERTIESONLY, "folder, fast" );
    typed( file, "file" );
    typed( dir, "folder" );

    DeleteFileW( file );
    RemoveDirectoryW( dir );
    CoUninitialize();
    return 0;
}
