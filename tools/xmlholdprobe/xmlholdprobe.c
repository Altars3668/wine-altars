/* xmlholdprobe -- 用 msxml 读一个文件之后，还能不能删掉它？
 *
 * Click-to-Run 的 integrator 在集成阶段解析 C2RManifest*.xml，随后删除它们。
 * 在 Wine 下这一步以 0x20 (ERROR_SHARING_VIOLATION) 失败，整个
 * INTEGRATE_INSTALL 任务随之失败，Office 的文件类型和 ProgID 就没人注册。
 * 这个探针把两件事分开：先不解析直接删（对照），再解析后删。
 *
 * Build:
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -municode \
 *       -o xmlholdprobe.exe xmlholdprobe.c -lole32 -loleaut32 -luuid
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <objbase.h>
#include <msxml6.h>
#include <stdio.h>

static void out( const char *s ) { DWORD w; WriteFile( GetStdHandle( STD_OUTPUT_HANDLE ), s, (DWORD)strlen(s), &w, NULL ); }
static void outf( const char *fmt, ... )
{
    char b[1024]; va_list ap; va_start(ap,fmt); vsnprintf(b,sizeof(b),fmt,ap); va_end(ap); out(b);
}

static BOOL copy_to_temp( const WCHAR *src, WCHAR *dst, DWORD max )
{
    WCHAR dir[MAX_PATH];
    if (!GetTempPathW( ARRAYSIZE(dir), dir )) return FALSE;
    if (!GetTempFileNameW( dir, L"xhp", 0, dst )) return FALSE;
    (void)max;
    return CopyFileW( src, dst, FALSE );
}

static void try_delete( const WCHAR *path, const char *label )
{
    if (DeleteFileW( path )) outf( "  %-26s 删除成功\n", label );
    else outf( "  %-26s 删除失败 %lu%s\n", label, GetLastError(),
               GetLastError() == ERROR_SHARING_VIOLATION ? "  (ERROR_SHARING_VIOLATION)" : "" );
}

int wmain( int argc, WCHAR **argv )
{
    WCHAR tmp[MAX_PATH];
    IXMLDOMDocument *doc;
    VARIANT_BOOL ok = 0;
    VARIANT src;
    HRESULT hr;
    BSTR path;

    if (argc < 2) { out( "usage: xmlholdprobe <file.xml>\n" ); return 2; }
    CoInitializeEx( NULL, COINIT_APARTMENTTHREADED );

    /* 对照：没人碰过的副本 */
    if (!copy_to_temp( argv[1], tmp, ARRAYSIZE(tmp) )) { out( "copy failed\n" ); return 1; }
    try_delete( tmp, "未解析直接删" );

    /* 解析之后再删 */
    if (!copy_to_temp( argv[1], tmp, ARRAYSIZE(tmp) )) { out( "copy failed\n" ); return 1; }
    hr = CoCreateInstance( &CLSID_DOMDocument60, NULL, CLSCTX_INPROC_SERVER,
                           &IID_IXMLDOMDocument, (void **)&doc );
    outf( "  CoCreateInstance DOMDocument60 : 0x%08lx\n", (unsigned long)hr );
    if (FAILED(hr)) return 1;

    path = SysAllocString( tmp );
    VariantInit( &src );
    V_VT( &src ) = VT_BSTR;
    V_BSTR( &src ) = path;
    hr = IXMLDOMDocument_load( doc, src, &ok );
    outf( "  load                           : 0x%08lx ok=%d\n", (unsigned long)hr, ok );
    SysFreeString( path );

    try_delete( tmp, "解析后（文档仍存活）" );

    IXMLDOMDocument_Release( doc );
    try_delete( tmp, "Release 之后" );

    /* integrator 走的是 file:/// URL，那条路要过 urlmon 的 moniker 绑定。 */
    if (copy_to_temp( argv[1], tmp, ARRAYSIZE(tmp) ))
    {
        WCHAR url[MAX_PATH + 16] = L"file:///";
        DWORD i;

        for (i = 0; tmp[i]; i++) url[8 + i] = tmp[i] == '\\' ? '/' : tmp[i];
        url[8 + i] = 0;

        hr = CoCreateInstance( &CLSID_DOMDocument60, NULL, CLSCTX_INPROC_SERVER,
                               &IID_IXMLDOMDocument, (void **)&doc );
        if (SUCCEEDED(hr))
        {
            path = SysAllocString( url );
            VariantInit( &src );
            V_VT( &src ) = VT_BSTR;
            V_BSTR( &src ) = path;
            hr = IXMLDOMDocument_load( doc, src, &ok );
            outf( "  load(file:/// URL)             : 0x%08lx ok=%d\n", (unsigned long)hr, ok );
            SysFreeString( path );
            try_delete( tmp, "URL 加载后（文档仍存活）" );
            IXMLDOMDocument_Release( doc );
            try_delete( tmp, "URL 加载 + Release 之后" );
        }
    }

    CoUninitialize();
    out( "done\n" );
    return 0;
}
