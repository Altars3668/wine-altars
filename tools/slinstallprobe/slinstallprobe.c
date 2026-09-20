/* slinstallprobe -- 安装一份产品定义，再看目录是否认得这个产品。
 *
 * Click-to-Run 在安装时把 root\Licenses16 下的 XrML 文件交给 SLInstallLicense。
 * 这个探针做同一件事：装一份 PPD，然后按 Office 的方式评估整个应用。
 * 它只写产品定义，不安装密钥、不激活、不接触账户；评估结果仍应是未授权。
 *
 * Build:
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -municode \
 *       -o slinstallprobe.exe slinstallprobe.c -lole32
 *
 * Usage:  slinstallprobe.exe <licence.xrm-ms> [more.xrm-ms ...]
 */

#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include "../../wine-src/include/slpublic.h"

/* Office 的应用 SLID，与 Word 启动时 SLLoadApplicationPolicies 传的一致。 */
static const SLID office_app =
    {0x0ff1ce15, 0xa989, 0x479d, {0xaf, 0x46, 0xf2, 0x75, 0xc6, 0x37, 0x06, 0x63}};

typedef HRESULT (WINAPI *open_fn)( HSLC * );
typedef HRESULT (WINAPI *close_fn)( HSLC );
typedef HRESULT (WINAPI *install_fn)( HSLC, UINT, const BYTE *, SLID * );
typedef HRESULT (WINAPI *consume_fn)( HSLC, const SLID *, const SLID *, LPCWSTR, void * );
typedef HRESULT (WINAPI *query_fn)( HSLC, const SLID *, const SLID *, LPCWSTR, UINT *, SL_LICENSING_STATUS ** );

static void out( const char *s )
{
    DWORD written;
    WriteFile( GetStdHandle( STD_OUTPUT_HANDLE ), s, (DWORD)strlen( s ), &written, NULL );
}

static void outf( const char *fmt, ... )
{
    char buf[1024];
    va_list ap;
    va_start( ap, fmt );
    vsnprintf( buf, sizeof(buf), fmt, ap );
    va_end( ap );
    out( buf );
}

static const char *status_name( SLLICENSINGSTATUS code )
{
    switch (code)
    {
    case SL_LICENSING_STATUS_UNLICENSED:   return "UNLICENSED";
    case SL_LICENSING_STATUS_LICENSED:     return "LICENSED";
    case SL_LICENSING_STATUS_IN_GRACE_PERIOD: return "IN_GRACE_PERIOD";
    case SL_LICENSING_STATUS_NOTIFICATION: return "NOTIFICATION";
    default: return "?";
    }
}

static BYTE *read_file( const WCHAR *path, DWORD *size )
{
    HANDLE file = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
    BYTE *data;
    DWORD read;

    if (file == INVALID_HANDLE_VALUE) return NULL;
    *size = GetFileSize( file, NULL );
    if (*size == INVALID_FILE_SIZE || !(data = malloc( *size )))
    {
        CloseHandle( file );
        return NULL;
    }
    if (!ReadFile( file, data, *size, &read, NULL ) || read != *size)
    {
        free( data );
        CloseHandle( file );
        return NULL;
    }
    CloseHandle( file );
    return data;
}

int wmain( int argc, WCHAR **argv )
{
    SL_LICENSING_STATUS *status = NULL;
    install_fn pSLInstallLicense;
    consume_fn pSLConsumeRight;
    close_fn pSLClose;
    open_fn pSLOpen;
    query_fn pSLQuery;
    UINT count = 0, i;
    HSLC handle = NULL;
    HMODULE mod;
    HRESULT hr;
    int arg;

    if (argc < 2) { out( "usage: slinstallprobe <licence.xrm-ms> [...]\n" ); return 2; }

    if (!(mod = LoadLibraryW( L"sppc.dll" ))) { outf( "LoadLibrary sppc.dll: %lu\n", GetLastError() ); return 1; }
#define GET(x) do { p##x = (void *)GetProcAddress( mod, #x ); \
    if (!p##x) { outf( "missing %s\n", #x ); return 1; } } while (0)
    GET(SLOpen); GET(SLClose); GET(SLInstallLicense); GET(SLConsumeRight);
#undef GET
    pSLQuery = (void *)GetProcAddress( mod, "SLGetLicensingStatusInformation" );
    if (!pSLQuery) { out( "missing SLGetLicensingStatusInformation\n" ); return 1; }

    hr = pSLOpen( &handle );
    outf( "SLOpen                       : 0x%08lx\n", (unsigned long)hr );
    if (FAILED(hr)) return 1;

    hr = pSLConsumeRight( handle, &office_app, NULL, NULL, NULL );
    outf( "before install: ConsumeRight  : 0x%08lx%s\n", (unsigned long)hr,
          hr == (HRESULT)0xC004F015 ? "  (SL_E_PRODUCT_SKU_NOT_INSTALLED)" : "" );

    for (arg = 1; arg < argc; arg++)
    {
        SLID file_id = {0};
        DWORD size = 0;
        BYTE *data = read_file( argv[arg], &size );

        if (!data) { outf( "  read failed: %ls\n", argv[arg] ); continue; }
        hr = pSLInstallLicense( handle, size, data, &file_id );
        outf( "SLInstallLicense %7lu B   : 0x%08lx  fileId={%08lx-...}\n",
              (unsigned long)size, (unsigned long)hr, file_id.Data1 );
        free( data );
    }

    hr = pSLConsumeRight( handle, &office_app, NULL, NULL, NULL );
    outf( "after install:  ConsumeRight  : 0x%08lx\n", (unsigned long)hr );

    hr = pSLQuery( handle, NULL, NULL, NULL, &count, &status );
    outf( "GetLicensingStatusInformation: 0x%08lx  count=%u\n", (unsigned long)hr, count );
    for (i = 0; i < count && i < 8; i++)
        outf( "   [%u] sku={%08lx-%04x-%04x-...} status=%s reason=0x%08lx\n", i,
              status[i].SkuId.Data1, status[i].SkuId.Data2, status[i].SkuId.Data3,
              status_name( status[i].eStatus ), (unsigned long)status[i].hrReason );
    if (status) LocalFree( status );

    pSLClose( handle );
    out( "done\n" );
    return 0;
}
