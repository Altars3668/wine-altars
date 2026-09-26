/* xmlmlangprobe: which encodings xmllite's reader reads, with and without an IMultiLanguage2 set
 * as XmlReaderProperty_MultiLanguage, which Word sets on its readers.
 */
#define COBJMACROS
#include <windows.h>
#include <ole2.h>
#include <shlwapi.h>
#include <mlang.h>
#include <initguid.h>
#include <xmllite.h>
#include <stdio.h>

struct doc
{
    const char *encoding;
    const char *text;     /* the element's content, in that encoding */
};

static const struct doc docs[] =
{
    { "UTF-8",        "\xc3\xa9" },
    { "utf-8",        "\xc3\xa9" },
    { "US-ASCII",     "e" },
    { "US-ASCII",     "\xe9" },
    { "windows-1252", "\xe9\x80" },
    { "ISO-8859-1",   "\xe9" },
    { "iso-8859-2",   "\xe9" },
    { "latin1",       "\xe9" },
    { "GB2312",       "\xd6\xd0" },
    { "gbk",          "\xd6\xd0" },
    { "Shift_JIS",    "\x93\xfa" },
    { "big5",         "\xa4\xa4" },
    { "koi8-r",       "\xc1" },
    { "x-no-such",    "e" },
    { "UTF-7",        "e" },
    { "UCS-2",        "e" },
    { "ascii",        "e" },
    { "windows-1250", "\xe9" },
    { "windows-1251", "\xe9" },
    { "windows-1253", "\xe9" },
    { "windows-1254", "\xe9" },
    { "windows-1255", "\xe9" },
    { "windows-1256", "\xe9" },
    { "windows-1257", "\xe9" },
    { "windows-1258", "\xe9" },
    { "iso-8859-3",   "\xe9" },
    { "iso-8859-4",   "\xe9" },
    { "iso-8859-5",   "\xe9" },
    { "iso-8859-7",   "\xe9" },
    { "iso-8859-9",   "\xe9" },
    { "iso-8859-15",  "\xa4" },
    { "UTF-16LE",     "e" },
    { "unicode",      "e" },
    { "cp1252",       "\xe9" },
    { "us-ascii",     "e" },
    { "ISO_8859-1",   "\xe9" },
    { "euc-jp",       "\xc6\xfc" },
    { "iso-2022-jp",  "e" },
    { "utf-32",       "e" },
    { "Windows-1252", "\xe9" },
    { "WINDOWS-1252", "\xe9" },
    { "iso-8859-1 ",  "\xe9" },
};

static void read_doc( const struct doc *doc, IUnknown *mlang, const WCHAR *hint )
{
    char buffer[256];
    IXmlReader *reader;
    IUnknown *input = NULL;
    const WCHAR *value;
    XmlNodeType type;
    IStream *stream;
    UINT len, i;
    HRESULT hr;

    sprintf( buffer, "<?xml version=\"1.0\" encoding=\"%s\"?><a>%s</a>", doc->encoding, doc->text );
    stream = SHCreateMemStream( (BYTE *)buffer, strlen( buffer ) );
    CreateXmlReader( &IID_IXmlReader, (void **)&reader, NULL );
    if (mlang)
    {
        hr = IXmlReader_SetProperty( reader, XmlReaderProperty_MultiLanguage, (LONG_PTR)mlang );
        if (hr) printf( "  SetProperty(MultiLanguage) %#lx\n", hr );
    }
    if (hint)
    {
        hr = CreateXmlReaderInputWithEncodingName( (IUnknown *)stream, NULL, hint, FALSE, NULL, &input );
        if (hr) printf( "  CreateXmlReaderInputWithEncodingName(%ls) %#lx\n", hint, hr );
        hr = IXmlReader_SetInput( reader, input );
    }
    else hr = IXmlReader_SetInput( reader, (IUnknown *)stream );
    printf( "%-13s%s%ls%s: SetInput %#lx", doc->encoding, hint ? " hint " : "", hint ? hint : L"", mlang ? " mlang" : "", hr );
    for (;;)
    {
        hr = IXmlReader_Read( reader, &type );
        if (hr != S_OK) break;
        if (type == XmlNodeType_Text)
        {
            IXmlReader_GetValue( reader, &value, &len );
            printf( " text" );
            for (i = 0; i < len; i++) printf( " %04x", value[i] );
        }
    }
    printf( " -> %#lx\n", hr );
    IXmlReader_Release( reader );
    if (input) IUnknown_Release( input );
    IStream_Release( stream );
}

static void read_raw( const char *what, const void *data, unsigned int size, IUnknown *mlang )
{
    IXmlReader *reader;
    const WCHAR *value;
    XmlNodeType type;
    IStream *stream;
    UINT len, i;
    HRESULT hr;

    stream = SHCreateMemStream( data, size );
    CreateXmlReader( &IID_IXmlReader, (void **)&reader, NULL );
    if (mlang) IXmlReader_SetProperty( reader, XmlReaderProperty_MultiLanguage, (LONG_PTR)mlang );
    hr = IXmlReader_SetInput( reader, (IUnknown *)stream );
    printf( "%s%s: SetInput %#lx", what, mlang ? " mlang" : "", hr );
    for (;;)
    {
        hr = IXmlReader_Read( reader, &type );
        if (hr != S_OK) break;
        if (type == XmlNodeType_Text)
        {
            IXmlReader_GetValue( reader, &value, &len );
            printf( " text" );
            for (i = 0; i < len; i++) printf( " %04x", value[i] );
        }
    }
    printf( " -> %#lx\n", hr );
    IXmlReader_Release( reader );
    IStream_Release( stream );
}

int main( void )
{
    IMultiLanguage2 *mlang2;
    IXmlReader *reader;
    LONG_PTR value;
    unsigned int i;
    HRESULT hr;

    CoInitialize( NULL );
    hr = CoCreateInstance( &CLSID_CMultiLanguage, NULL, CLSCTX_INPROC_SERVER, &IID_IMultiLanguage2, (void **)&mlang2 );
    printf( "CMultiLanguage %#lx\n", hr );
    if (hr) return 1;

    CreateXmlReader( &IID_IXmlReader, (void **)&reader, NULL );
    value = 0xdead;
    hr = IXmlReader_GetProperty( reader, XmlReaderProperty_MultiLanguage, &value );
    printf( "GetProperty(MultiLanguage) before %#lx %Ix\n", hr, value );
    hr = IXmlReader_SetProperty( reader, XmlReaderProperty_MultiLanguage, (LONG_PTR)mlang2 );
    value = 0;
    printf( "SetProperty %#lx", hr );
    hr = IXmlReader_GetProperty( reader, XmlReaderProperty_MultiLanguage, &value );
    printf( ", GetProperty %#lx same %d\n", hr, value == (LONG_PTR)mlang2 );
    if (value) IUnknown_Release( (IUnknown *)value );
    hr = IXmlReader_SetProperty( reader, XmlReaderProperty_MultiLanguage, 0 );
    value = 0xdead;
    printf( "SetProperty(0) %#lx", hr );
    hr = IXmlReader_GetProperty( reader, XmlReaderProperty_MultiLanguage, &value );
    printf( ", GetProperty %#lx %Ix\n", hr, value );
    IXmlReader_Release( reader );

    for (i = 0; i < ARRAY_SIZE(docs); i++)
    {
        read_doc( &docs[i], NULL, NULL );
        read_doc( &docs[i], (IUnknown *)mlang2, NULL );
    }
    {
        WCHAR out[4];
        int n;
        n = MultiByteToWideChar( 20127, 0, "\xe9", 1, out, 4 );
        printf( "MultiByteToWideChar(20127, e9): %d %04x\n", n, out[0] );
        n = MultiByteToWideChar( 28591, 0, "\xe9", 1, out, 4 );
        printf( "MultiByteToWideChar(28591, e9): %d %04x\n", n, out[0] );
    }
    {
        static const char bom1252[] = "\xef\xbb\xbf<?xml version=\"1.0\" encoding=\"windows-1252\"?><a>\xe9</a>";
        static const WCHAR wide1252[] = L"<?xml version=\"1.0\" encoding=\"windows-1252\"?><a>\x00e9</a>";
        static const WCHAR widebom1252[] = L"\xfeff<?xml version=\"1.0\" encoding=\"windows-1252\"?><a>\x00e9</a>";
        static const char utf16[] = "<?xml version=\"1.0\" encoding=\"UTF-16\"?><a>e</a>";
        static const char utf[] = "<?xml version=\"1.0\" encoding=\"utf\"?><a>e</a>";
        static const char utf16be[] = "<?xml version=\"1.0\" encoding=\"UTF-16BE\"?><a>e</a>";
        static const char ucs4[] = "<?xml version=\"1.0\" encoding=\"UCS-4\"?><a>e</a>";
        static const char cr1252[] = "<?xml version=\"1.0\"\r\n encoding=\"windows-1252\"?>\r\n<a>\xe9</a>";
        static const char sq1252[] = "<?xml version='1.0' encoding='windows-1252' standalone='yes'?><a>\xe9</a>";
        static const char nodecl[] = "<a>\xe9</a>";
        static const char big[] = "<?xml version=\"1.0\" encoding=\"GB2312\"?><a>";
        char *buffer = malloc( 20000 );
        unsigned int k;
        read_raw( "UTF-8 BOM, windows-1252", bom1252, sizeof(bom1252) - 1, NULL );
        read_raw( "UTF-16, windows-1252", wide1252, sizeof(wide1252) - 2, NULL );
        read_raw( "UTF-16 BOM, windows-1252", widebom1252, sizeof(widebom1252) - 2, NULL );
        read_raw( "UTF-16", utf16, sizeof(utf16) - 1, NULL );
        read_raw( "UTF-16", utf16, sizeof(utf16) - 1, (IUnknown *)mlang2 );
        read_raw( "utf", utf, sizeof(utf) - 1, NULL );
        read_raw( "utf", utf, sizeof(utf) - 1, (IUnknown *)mlang2 );
        read_raw( "UTF-16BE", utf16be, sizeof(utf16be) - 1, NULL );
        read_raw( "UCS-4", ucs4, sizeof(ucs4) - 1, NULL );
        read_raw( "CR LF, windows-1252", cr1252, sizeof(cr1252) - 1, NULL );
        read_raw( "single quotes, windows-1252", sq1252, sizeof(sq1252) - 1, NULL );
        read_raw( "no declaration", nodecl, sizeof(nodecl) - 1, NULL );
        /* a GB2312 character across the reader's chunks */
        strcpy( buffer, big );
        for (k = strlen( buffer ); k < 16000; k += 2) { buffer[k] = '\xd6'; buffer[k + 1] = '\xd0'; }
        strcpy( buffer + k, "</a>" );
        {
            IXmlReader *reader;
            IStream *stream = SHCreateMemStream( (BYTE *)buffer, strlen( buffer ) );
            XmlNodeType type;
            const WCHAR *value;
            UINT len, n, bad = 0, total = 0;
            HRESULT hr;
            CreateXmlReader( &IID_IXmlReader, (void **)&reader, NULL );
            IXmlReader_SetProperty( reader, XmlReaderProperty_MultiLanguage, (LONG_PTR)mlang2 );
            IXmlReader_SetInput( reader, (IUnknown *)stream );
            while ((hr = IXmlReader_Read( reader, &type )) == S_OK)
            {
                if (type != XmlNodeType_Text) continue;
                IXmlReader_GetValue( reader, &value, &len );
                for (n = 0; n < len; n++) if (value[n] != 0x4e2d) bad++;
                total += len;
            }
            printf( "long GB2312 mlang: %u characters, %u not U+4E2D, -> %#lx\n", total, bad, hr );
            IXmlReader_Release( reader );
            IStream_Release( stream );
        }
        free( buffer );
    }
    read_doc( &docs[4], NULL, L"windows-1252" );
    read_doc( &docs[4], (IUnknown *)mlang2, L"windows-1252" );
    read_doc( &docs[8], (IUnknown *)mlang2, L"GB2312" );
    IMultiLanguage2_Release( mlang2 );
    CoUninitialize();
    printf( "done\n" );
    return 0;
}
