/* wicwebpprobe: which WIC decoders a system has, and what the WebP one gives for a set of files -- container,
 * frames, sizes, pixel formats, metadata and a checksum of the pixels -- printed so that Windows and Wine diff.
 *
 *   wicwebpprobe.exe [file...]
 *
 * samples/make-samples.py makes the WebP files: lossy, lossless, each with and without alpha, and animations;
 * samples.h carries them, and without arguments the probe writes each to the temporary directory and decodes it.
 *
 * Word inserts a WebP picture through WIC; under Wine it failed with E_FAIL.
 */
#define COBJMACROS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <wincodec.h>
#include <wincodecsdk.h>
#include <shellapi.h>
#include <shlwapi.h>
#include "samples.h"

static IWICImagingFactory *factory;

static const char *guid_str( const GUID *guid )
{
    static char buffers[4][40];
    static int n;
    char *buffer = buffers[n++ % 4];
    sprintf( buffer, "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", guid->Data1, guid->Data2, guid->Data3,
             guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3], guid->Data4[4], guid->Data4[5],
             guid->Data4[6], guid->Data4[7] );
    return buffer;
}

static const char *format_name( const GUID *format )
{
    static const struct { const GUID *guid; const char *name; } names[] =
    {
        { &GUID_WICPixelFormat32bppBGRA, "32bppBGRA" },
        { &GUID_WICPixelFormat32bppPBGRA, "32bppPBGRA" },
        { &GUID_WICPixelFormat32bppBGR, "32bppBGR" },
        { &GUID_WICPixelFormat24bppBGR, "24bppBGR" },
        { &GUID_WICPixelFormat24bppRGB, "24bppRGB" },
        { &GUID_WICPixelFormat32bppRGBA, "32bppRGBA" },
        { &GUID_WICPixelFormat32bppPRGBA, "32bppPRGBA" },
        { &GUID_WICPixelFormat8bppIndexed, "8bppIndexed" },
        { &GUID_WICPixelFormat8bppGray, "8bppGray" },
        { &GUID_ContainerFormatWebp, "container WebP" },
        { &GUID_ContainerFormatPng, "container PNG" },
    };
    unsigned int i;
    for (i = 0; i < ARRAY_SIZE(names); i++) if (IsEqualGUID( format, names[i].guid )) return names[i].name;
    return guid_str( format );
}

/* the signing status and versions every component reports */
static void list_components( WICComponentType type, const char *what )
{
    IEnumUnknown *enumerator;
    IUnknown *unknown;

    if (FAILED(IWICImagingFactory_CreateComponentEnumerator( factory, type, WICComponentEnumerateDefault, &enumerator )))
        return;
    while (IEnumUnknown_Next( enumerator, 1, &unknown, NULL ) == S_OK)
    {
        IWICComponentInfo *info;
        WCHAR name[256], spec[64], version[64];
        DWORD status = 0xdeadbeef;
        HRESULT hr_status, hr_spec, hr_version;
        UINT len;

        if (SUCCEEDED(IUnknown_QueryInterface( unknown, &IID_IWICComponentInfo, (void **)&info )))
        {
            name[0] = spec[0] = version[0] = 0;
            IWICComponentInfo_GetFriendlyName( info, ARRAY_SIZE(name), name, &len );
            hr_status = IWICComponentInfo_GetSigningStatus( info, &status );
            hr_spec = IWICComponentInfo_GetSpecVersion( info, ARRAY_SIZE(spec), spec, &len );
            hr_version = IWICComponentInfo_GetVersion( info, ARRAY_SIZE(version), version, &len );
            printf( "component %s %ls: signing %#lx (%#lx) spec %ls (%#lx) version %ls (%#lx)\n", what, name, status,
                    hr_status, spec, hr_spec, version, hr_version );
            if (type == WICMetadataReader || type == WICMetadataWriter)
            {
                IWICMetadataHandlerInfo *handler;
                BOOL full = 2, padding = 2, fixed = 2;
                HRESULT hr[3];

                IWICComponentInfo_QueryInterface( info, &IID_IWICMetadataHandlerInfo, (void **)&handler );
                hr[0] = IWICMetadataHandlerInfo_DoesRequireFullStream( handler, &full );
                hr[1] = IWICMetadataHandlerInfo_DoesSupportPadding( handler, &padding );
                hr[2] = IWICMetadataHandlerInfo_DoesRequireFixedSize( handler, &fixed );
                printf( "handler %s %ls: full stream %d padding %d fixed size %d (%#lx %#lx %#lx)\n", what, name,
                        full, padding, fixed, hr[0], hr[1], hr[2] );
                IWICMetadataHandlerInfo_Release( handler );
            }
            IWICComponentInfo_Release( info );
        }
        IUnknown_Release( unknown );
    }
    IEnumUnknown_Release( enumerator );
}

/* what every codec says it supports */
static void list_codec_capabilities( WICComponentType type )
{
    IEnumUnknown *enumerator;
    IUnknown *unknown;

    if (FAILED(IWICImagingFactory_CreateComponentEnumerator( factory, type, WICComponentEnumerateDefault, &enumerator )))
        return;
    while (IEnumUnknown_Next( enumerator, 1, &unknown, NULL ) == S_OK)
    {
        IWICBitmapCodecInfo *info;
        WCHAR name[256], mime[256], *comma;
        BOOL animation = 2, chromakey = 2, lossless = 2, multiframe = 2, matches = 2, other = 2, upper = 2;
        HRESULT hr[7];
        GUID formats[64];
        UINT len, count = 0;

        if (SUCCEEDED(IUnknown_QueryInterface( unknown, &IID_IWICBitmapCodecInfo, (void **)&info )))
        {
            name[0] = mime[0] = 0;
            IWICBitmapCodecInfo_GetFriendlyName( info, ARRAY_SIZE(name), name, &len );
            IWICBitmapCodecInfo_GetMimeTypes( info, ARRAY_SIZE(mime), mime, &len );
            if ((comma = wcschr( mime, ',' ))) *comma = 0;
            hr[0] = IWICBitmapCodecInfo_DoesSupportAnimation( info, &animation );
            hr[1] = IWICBitmapCodecInfo_DoesSupportChromaKey( info, &chromakey );
            hr[2] = IWICBitmapCodecInfo_DoesSupportLossless( info, &lossless );
            hr[3] = IWICBitmapCodecInfo_DoesSupportMultiframe( info, &multiframe );
            hr[4] = IWICBitmapCodecInfo_MatchesMimeType( info, mime, &matches );
            hr[5] = IWICBitmapCodecInfo_MatchesMimeType( info, L"image/nothing", &other );
            _wcsupr( mime );
            hr[6] = IWICBitmapCodecInfo_MatchesMimeType( info, mime, &upper );
            IWICBitmapCodecInfo_GetPixelFormats( info, ARRAY_SIZE(formats), formats, &count );
            printf( "%s %ls: animation %d chromakey %d lossless %d multiframe %d, mime %ls %d nothing %d upper %d,"
                    " %u formats (%#lx %#lx %#lx %#lx %#lx %#lx %#lx)\n", type == WICDecoder ? "decoder" : "encoder",
                    name, animation, chromakey, lossless, multiframe, mime, matches, other, upper, count,
                    hr[0], hr[1], hr[2], hr[3], hr[4], hr[5], hr[6] );
            IWICBitmapCodecInfo_Release( info );
        }
        IUnknown_Release( unknown );
    }
    IEnumUnknown_Release( enumerator );
}

static void list_decoders(void)
{
    IEnumUnknown *enumerator;
    IUnknown *unknown;
    HRESULT hr;

    hr = IWICImagingFactory_CreateComponentEnumerator( factory, WICDecoder, WICComponentEnumerateDefault, &enumerator );
    if (FAILED(hr))
    {
        printf( "CreateComponentEnumerator failed %#lx\n", hr );
        return;
    }
    while (IEnumUnknown_Next( enumerator, 1, &unknown, NULL ) == S_OK)
    {
        IWICBitmapDecoderInfo *info;
        WCHAR name[256], extensions[256], mime[256];
        UINT len;
        CLSID clsid;
        GUID container;

        if (SUCCEEDED(IUnknown_QueryInterface( unknown, &IID_IWICBitmapDecoderInfo, (void **)&info )))
        {
            name[0] = extensions[0] = mime[0] = 0;
            IWICBitmapDecoderInfo_GetFriendlyName( info, ARRAY_SIZE(name), name, &len );
            IWICBitmapDecoderInfo_GetFileExtensions( info, ARRAY_SIZE(extensions), extensions, &len );
            IWICBitmapDecoderInfo_GetMimeTypes( info, ARRAY_SIZE(mime), mime, &len );
            IWICBitmapDecoderInfo_GetCLSID( info, &clsid );
            IWICBitmapDecoderInfo_GetContainerFormat( info, &container );
            printf( "decoder %ls %s container %s extensions %ls mime %ls\n", name, guid_str( &clsid ),
                    format_name( &container ), extensions, mime );
            IWICBitmapDecoderInfo_Release( info );
        }
        IUnknown_Release( unknown );
    }
    IEnumUnknown_Release( enumerator );
}

/* what the WebP decoder's registration says about it */
static void webp_decoder_info(void)
{
    IWICComponentInfo *component;
    IWICBitmapDecoderInfo *info;
    WCHAR text[256];
    GUID formats[32], guid;
    WICBitmapPattern *patterns;
    UINT count, size, len, i, j;
    BOOL flag;
    DWORD status;
    HRESULT hr;

    hr = IWICImagingFactory_CreateComponentInfo( factory, &CLSID_WICWebpDecoder, &component );
    if (FAILED(hr))
    {
        printf( "no WebP decoder info %#lx\n", hr );
        return;
    }
    IWICComponentInfo_QueryInterface( component, &IID_IWICBitmapDecoderInfo, (void **)&info );
    IWICComponentInfo_Release( component );
    text[0] = 0;
    IWICBitmapDecoderInfo_GetAuthor( info, ARRAY_SIZE(text), text, &len );
    printf( "webp author %ls", text );
    text[0] = 0;
    IWICBitmapDecoderInfo_GetVersion( info, ARRAY_SIZE(text), text, &len );
    printf( " version %ls", text );
    text[0] = 0;
    IWICBitmapDecoderInfo_GetSpecVersion( info, ARRAY_SIZE(text), text, &len );
    printf( " spec %ls", text );
    IWICBitmapDecoderInfo_GetVendorGUID( info, &guid );
    printf( " vendor %s", guid_str( &guid ) );
    IWICBitmapDecoderInfo_GetSigningStatus( info, &status );
    printf( " signing %#lx\n", status );
    count = 0;
    hr = IWICBitmapDecoderInfo_GetPixelFormats( info, ARRAY_SIZE(formats), formats, &count );
    printf( "webp pixel formats %#lx:", hr );
    for (i = 0; i < count; i++) printf( " %s", format_name( &formats[i] ) );
    printf( "\n" );
    IWICBitmapDecoderInfo_DoesSupportAnimation( info, &flag ); printf( "webp animation %d", flag );
    IWICBitmapDecoderInfo_DoesSupportChromaKey( info, &flag ); printf( " chromakey %d", flag );
    IWICBitmapDecoderInfo_DoesSupportLossless( info, &flag ); printf( " lossless %d", flag );
    IWICBitmapDecoderInfo_DoesSupportMultiframe( info, &flag ); printf( " multiframe %d\n", flag );
    count = size = 0;
    hr = IWICBitmapDecoderInfo_GetPatterns( info, 0, NULL, &count, &size );
    printf( "webp patterns %#lx count %u size %u\n", hr, count, size );
    if (SUCCEEDED(hr) && size)
    {
        patterns = malloc( size );
        hr = IWICBitmapDecoderInfo_GetPatterns( info, size, patterns, &count, &size );
        for (i = 0; SUCCEEDED(hr) && i < count; i++)
        {
            printf( "  position %lu length %lu end %d:", patterns[i].Position.LowPart, patterns[i].Length,
                    patterns[i].EndOfStream );
            for (j = 0; j < patterns[i].Length; j++) printf( " %02x", patterns[i].Pattern[j] );
            printf( " mask" );
            for (j = 0; j < patterns[i].Length; j++) printf( " %02x", patterns[i].Mask[j] );
            printf( "\n" );
        }
        free( patterns );
    }
    IWICBitmapDecoderInfo_Release( info );
}

static void print_propvariant( const PROPVARIANT *value );

/* the WebP metadata readers: their registration, and what a stream given to one has to hold */
static void webp_metadata_readers(void)
{
    static const struct { const CLSID *clsid; const char *name; BYTE payload[16]; UINT size; } readers[] =
    {
        { &CLSID_WICWebpAnimMetadataReader, "ANIM", { 1, 2, 3, 4, 5, 0 }, 6 },
        { &CLSID_WICWebpAnmfMetadataReader, "ANMF", { 1, 0, 0, 2, 0, 0, 63, 0, 0, 47, 0, 0, 0x2c, 0x01, 0, 2 }, 16 },
    };
    unsigned int i, j, l;

    for (i = 0; i < ARRAY_SIZE(readers); i++)
    {
        IWICComponentInfo *component;
        IWICMetadataReaderInfo *info;
        GUID format, containers[8];
        UINT count = 0, len;
        BOOL flag;
        WCHAR name[128];
        HRESULT hr;

        hr = IWICImagingFactory_CreateComponentInfo( factory, readers[i].clsid, &component );
        printf( "%s reader info %#lx\n", readers[i].name, hr );
        if (FAILED(hr)) continue;
        IWICComponentInfo_QueryInterface( component, &IID_IWICMetadataReaderInfo, (void **)&info );
        IWICComponentInfo_Release( component );
        name[0] = 0;
        IWICMetadataReaderInfo_GetFriendlyName( info, ARRAY_SIZE(name), name, &len );
        IWICMetadataReaderInfo_GetMetadataFormat( info, &format );
        hr = IWICMetadataReaderInfo_GetContainerFormats( info, ARRAY_SIZE(containers), containers, &count );
        printf( "  %ls format %s containers %#lx:", name, guid_str( &format ), hr );
        for (j = 0; j < count; j++) printf( " %s", format_name( &containers[j] ) );
        printf( "\n" );
        for (j = 0; j < count; j++)
        {
            UINT patterns = 0, size = 0, k, l;
            WICMetadataPattern *list;
            hr = IWICMetadataReaderInfo_GetPatterns( info, &containers[j], 0, NULL, &patterns, &size );
            printf( "  patterns for %s %#lx: %u (%u bytes)\n", format_name( &containers[j] ), hr, patterns, size );
            if (SUCCEEDED(hr) && size)
            {
                list = malloc( size );
                if (SUCCEEDED(IWICMetadataReaderInfo_GetPatterns( info, &containers[j], size, list, &patterns, &size )))
                    for (k = 0; k < patterns; k++)
                    {
                        printf( "    position %lu length %lu data offset %lu:", list[k].Position.LowPart,
                                list[k].Length, list[k].DataOffset.LowPart );
                        for (l = 0; l < list[k].Length; l++) printf( " %02x", list[k].Pattern[l] );
                        printf( " mask" );
                        for (l = 0; l < list[k].Length; l++) printf( " %02x", list[k].Mask[l] );
                        printf( "\n" );
                    }
                free( list );
            }
        }
        IWICMetadataReaderInfo_DoesRequireFullStream( info, &flag ); printf( "  full stream %d", flag );
        IWICMetadataReaderInfo_DoesSupportPadding( info, &flag ); printf( " padding %d", flag );
        IWICMetadataReaderInfo_DoesRequireFixedSize( info, &flag ); printf( " fixed size %d\n", flag );
        IWICMetadataReaderInfo_Release( info );

        /* load from the payload alone, then from the chunk with its header */
        for (j = 0; j < 2; j++)
        {
            IWICMetadataReader *reader;
            IWICPersistStream *persist;
            BYTE data[32];
            UINT size = 0, items = 0;
            IStream *stream;

            if (j)
            {
                memcpy( data, readers[i].name, 4 );
                data[4] = readers[i].size; data[5] = data[6] = data[7] = 0;
                size = 8;
            }
            memcpy( data + size, readers[i].payload, readers[i].size );
            size += readers[i].size;
            hr = CoCreateInstance( readers[i].clsid, NULL, CLSCTX_INPROC_SERVER, &IID_IWICMetadataReader,
                                   (void **)&reader );
            if (FAILED(hr))
            {
                printf( "  create %#lx\n", hr );
                break;
            }
            stream = SHCreateMemStream( data, size );
            IWICMetadataReader_QueryInterface( reader, &IID_IWICPersistStream, (void **)&persist );
            hr = IWICPersistStream_LoadEx( persist, stream, NULL, WICPersistOptionDefault );
            IWICMetadataReader_GetCount( reader, &items );
            printf( "  load %s: %#lx, %u items\n", j ? "chunk" : "payload", hr, items );
            if (SUCCEEDED(hr))
            {
                for (l = 0; l < items; l++)
                {
                    PROPVARIANT schema, id, value;
                    PropVariantInit( &schema ); PropVariantInit( &id ); PropVariantInit( &value );
                    IWICMetadataReader_GetValueByIndex( reader, l, &schema, &id, &value );
                    printf( "    schema vt %d id ", schema.vt );
                    print_propvariant( &id );
                    printf( " value " );
                    print_propvariant( &value );
                    printf( "\n" );
                    PropVariantClear( &schema ); PropVariantClear( &id ); PropVariantClear( &value );
                }
            }
            IWICPersistStream_Release( persist );
            IStream_Release( stream );
            IWICMetadataReader_Release( reader );
        }
    }
}

static DWORD checksum( const BYTE *data, UINT size )
{
    DWORD hash = 2166136261u;
    UINT i;
    for (i = 0; i < size; i++) hash = (hash ^ data[i]) * 16777619u;
    return hash;
}

static void dump_metadata( IWICMetadataQueryReader *reader, const char *indent, int depth )
{
    IEnumString *names;
    LPOLESTR name;

    if (depth > 3 || FAILED(IWICMetadataQueryReader_GetEnumerator( reader, &names ))) return;
    while (IEnumString_Next( names, 1, &name, NULL ) == S_OK)
    {
        PROPVARIANT value;

        HRESULT hr_value;

        PropVariantInit( &value );
        hr_value = IWICMetadataQueryReader_GetMetadataByName( reader, name, &value );
        if (FAILED(hr_value)) printf( "%s%ls: %#lx\n", indent, name, hr_value );
        else
        {
            printf( "%s%ls: vt %d", indent, name, value.vt );
            switch (value.vt)
            {
            case VT_UI1: printf( " %u", value.bVal ); break;
            case VT_UI2: printf( " %u", value.uiVal ); break;
            case VT_UI4: printf( " %lu", value.ulVal ); break;
            case VT_I4: printf( " %ld", value.lVal ); break;
            case VT_BOOL: printf( " %d", value.boolVal ); break;
            case VT_LPSTR: printf( " %s", value.pszVal ); break;
            case VT_LPWSTR: printf( " %ls", value.pwszVal ); break;
            case VT_BLOB: printf( " blob of %lu", value.blob.cbSize ); break;
            case VT_UNKNOWN:
            {
                IWICMetadataQueryReader *sub;
                char deeper[64];
                printf( "\n" );
                sprintf( deeper, "%s  ", indent );
                if (SUCCEEDED(IUnknown_QueryInterface( value.punkVal, &IID_IWICMetadataQueryReader, (void **)&sub )))
                {
                    dump_metadata( sub, deeper, depth + 1 );
                    IWICMetadataQueryReader_Release( sub );
                }
                break;
            }
            }
            if (value.vt != VT_UNKNOWN) printf( "\n" );
            PropVariantClear( &value );
        }
        CoTaskMemFree( name );
    }
    IEnumString_Release( names );
}

static void print_propvariant( const PROPVARIANT *value )
{
    printf( "vt %d", value->vt );
    switch (value->vt)
    {
    case VT_UI1: printf( " %u", value->bVal ); break;
    case VT_UI2: printf( " %u", value->uiVal ); break;
    case VT_UI4: printf( " %lu", value->ulVal ); break;
    case VT_UI8: printf( " %llu", (unsigned long long)value->uhVal.QuadPart ); break;
    case VT_I4: printf( " %ld", value->lVal ); break;
    case VT_BOOL: printf( " %d", value->boolVal ); break;
    case VT_LPSTR: printf( " %s", value->pszVal ); break;
    case VT_LPWSTR: printf( " %ls", value->pwszVal ); break;
    case VT_BLOB: printf( " blob of %lu", value->blob.cbSize ); break;
    }
}

/* every metadata block and its items, through IWICMetadataBlockReader */
static void dump_blocks( IUnknown *object, const char *indent )
{
    IWICMetadataBlockReader *blocks;
    UINT count, i;
    HRESULT hr;

    hr = IUnknown_QueryInterface( object, &IID_IWICMetadataBlockReader, (void **)&blocks );
    if (FAILED(hr))
    {
        printf( "%sno block reader %#lx\n", indent, hr );
        return;
    }
    hr = IWICMetadataBlockReader_GetCount( blocks, &count );
    printf( "%s%u blocks (%#lx)\n", indent, count, hr );
    for (i = 0; SUCCEEDED(hr) && i < count; i++)
    {
        IWICMetadataReader *reader;
        GUID format;
        UINT items, j;

        if (FAILED(IWICMetadataBlockReader_GetReaderByIndex( blocks, i, &reader ))) continue;
        IWICMetadataReader_GetMetadataFormat( reader, &format );
        IWICMetadataReader_GetCount( reader, &items );
        printf( "%s  block %u format %s, %u items\n", indent, i, guid_str( &format ), items );
        for (j = 0; j < items; j++)
        {
            PROPVARIANT schema, id, value;
            PropVariantInit( &schema ); PropVariantInit( &id ); PropVariantInit( &value );
            if (SUCCEEDED(IWICMetadataReader_GetValueByIndex( reader, j, &schema, &id, &value )))
            {
                printf( "%s    id ", indent );
                print_propvariant( &id );
                printf( " value " );
                print_propvariant( &value );
                printf( "\n" );
            }
            PropVariantClear( &schema ); PropVariantClear( &id ); PropVariantClear( &value );
        }
        IWICMetadataReader_Release( reader );
    }
    IWICMetadataBlockReader_Release( blocks );
}

static void dump_source( IWICBitmapSource *source, const char *what )
{
    WICPixelFormatGUID format;
    UINT width, height, stride, size;
    double dpi_x, dpi_y;
    BYTE *pixels;
    HRESULT hr;

    IWICBitmapSource_GetSize( source, &width, &height );
    IWICBitmapSource_GetPixelFormat( source, &format );
    IWICBitmapSource_GetResolution( source, &dpi_x, &dpi_y );
    printf( "  %s: %ux%u %s resolution %.2f %.2f", what, width, height, format_name( &format ), dpi_x, dpi_y );
    stride = width * 4;
    size = stride * height;
    pixels = malloc( size );
    hr = IWICBitmapSource_CopyPixels( source, NULL, stride, size, pixels );
    if (SUCCEEDED(hr))
    {
        printf( " pixels %08lx", checksum( pixels, size ) );
        printf( " first %02x%02x%02x%02x", pixels[0], pixels[1], pixels[2], pixels[3] );
        printf( " centre %02x%02x%02x%02x", pixels[(height / 2 * width + width / 2) * 4],
                pixels[(height / 2 * width + width / 2) * 4 + 1], pixels[(height / 2 * width + width / 2) * 4 + 2],
                pixels[(height / 2 * width + width / 2) * 4 + 3] );
    }
    else printf( " CopyPixels %#lx", hr );
    printf( "\n" );
    free( pixels );
}

static void probe_file( const WCHAR *path )
{
    IWICBitmapDecoder *decoder;
    IWICBitmapFrameDecode *frame;
    IWICMetadataQueryReader *reader;
    IWICBitmapSource *converted;
    GUID container;
    CLSID clsid;
    UINT count, i;
    HRESULT hr;

    printf( "file %ls\n", wcsrchr( path, '\\' ) ? wcsrchr( path, '\\' ) + 1 : path );
    hr = IWICImagingFactory_CreateDecoderFromFilename( factory, path, NULL, GENERIC_READ,
                                                       WICDecodeMetadataCacheOnDemand, &decoder );
    if (FAILED(hr))
    {
        printf( "  CreateDecoderFromFilename %#lx\n", hr );
        return;
    }
    IWICBitmapDecoder_GetContainerFormat( decoder, &container );
    {
        IWICBitmapDecoderInfo *info;
        if (SUCCEEDED(IWICBitmapDecoder_GetDecoderInfo( decoder, &info )))
        {
            IWICBitmapDecoderInfo_GetCLSID( info, &clsid );
            printf( "  decoder %s", guid_str( &clsid ) );
            IWICBitmapDecoderInfo_Release( info );
        }
    }
    hr = IWICBitmapDecoder_GetFrameCount( decoder, &count );
    printf( " container %s frames %u (%#lx)\n", format_name( &container ), count, hr );
    dump_blocks( (IUnknown *)decoder, "  decoder " );
    hr = IWICBitmapDecoder_GetMetadataQueryReader( decoder, &reader );
    printf( "  container metadata %#lx\n", hr );
    if (SUCCEEDED(hr))
    {
        dump_metadata( reader, "    ", 0 );
        IWICMetadataQueryReader_Release( reader );
    }
    {
        IWICBitmapSource *thumbnail, *preview;
        IWICPalette *palette;
        IWICColorContext *context;
        UINT contexts = 0;
        hr = IWICBitmapDecoder_GetThumbnail( decoder, &thumbnail );
        printf( "  thumbnail %#lx", hr );
        if (SUCCEEDED(hr)) IWICBitmapSource_Release( thumbnail );
        hr = IWICBitmapDecoder_GetPreview( decoder, &preview );
        printf( " preview %#lx", hr );
        if (SUCCEEDED(hr)) IWICBitmapSource_Release( preview );
        IWICImagingFactory_CreatePalette( factory, &palette );
        hr = IWICBitmapDecoder_CopyPalette( decoder, palette );
        printf( " palette %#lx", hr );
        IWICPalette_Release( palette );
        IWICImagingFactory_CreateColorContext( factory, &context );
        hr = IWICBitmapDecoder_GetColorContexts( decoder, 1, &context, &contexts );
        printf( " color contexts %#lx %u\n", hr, contexts );
        IWICColorContext_Release( context );
    }

    for (i = 0; i < count && i < 6; i++)
    {
        char what[32];
        hr = IWICBitmapDecoder_GetFrame( decoder, i, &frame );
        if (FAILED(hr))
        {
            printf( "  frame %u: %#lx\n", i, hr );
            continue;
        }
        sprintf( what, "frame %u", i );
        dump_source( (IWICBitmapSource *)frame, what );
        dump_blocks( (IUnknown *)frame, "  frame " );
        hr = IWICBitmapFrameDecode_GetMetadataQueryReader( frame, &reader );
        printf( "  frame %u metadata %#lx\n", i, hr );
        if (SUCCEEDED(hr))
        {
            dump_metadata( reader, "    ", 0 );
            IWICMetadataQueryReader_Release( reader );
        }
        if (SUCCEEDED(WICConvertBitmapSource( &GUID_WICPixelFormat32bppBGRA, (IWICBitmapSource *)frame, &converted )))
        {
            sprintf( what, "frame %u as BGRA", i );
            dump_source( converted, what );
            IWICBitmapSource_Release( converted );
        }
        IWICBitmapFrameDecode_Release( frame );
    }
    for (i = count; i < count + 7; i += 6)
    {
        hr = IWICBitmapDecoder_GetFrame( decoder, i, &frame );
        printf( "  frame %u (past the end): %#lx\n", i, hr );
        if (SUCCEEDED(hr))
        {
            dump_source( (IWICBitmapSource *)frame, "that frame" );
            IWICBitmapFrameDecode_Release( frame );
        }
    }
    {
        IStream *stream;
        DWORD capability = 0;
        IWICBitmapDecoder *webp;

        if (SUCCEEDED(SHCreateStreamOnFileEx( path, STGM_READ, 0, FALSE, NULL, &stream )))
        {
            if (SUCCEEDED(CoCreateInstance( &CLSID_WICWebpDecoder, NULL, CLSCTX_INPROC_SERVER, &IID_IWICBitmapDecoder,
                                            (void **)&webp )))
            {
                hr = IWICBitmapDecoder_QueryCapability( webp, stream, &capability );
                printf( "  query capability %#lx: %#lx\n", hr, capability );
                IWICBitmapDecoder_Release( webp );
            }
            IStream_Release( stream );
        }
    }
    IWICBitmapDecoder_Release( decoder );
}

int main(void)
{
    WCHAR **argv;
    HRESULT hr;
    int argc, i;

    argv = CommandLineToArgvW( GetCommandLineW(), &argc );

    setvbuf( stdout, NULL, _IONBF, 0 );
    CoInitializeEx( NULL, COINIT_MULTITHREADED );
    hr = CoCreateInstance( &CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory,
                           (void **)&factory );
    if (FAILED(hr))
    {
        printf( "no imaging factory %#lx\n", hr );
        return 1;
    }
    list_decoders();
    list_codec_capabilities( WICDecoder );
    list_codec_capabilities( WICEncoder );
    list_components( WICDecoder, "decoder" );
    list_components( WICEncoder, "encoder" );
    list_components( WICPixelFormatConverter, "converter" );
    list_components( WICMetadataReader, "reader" );
    list_components( WICMetadataWriter, "writer" );
    list_components( WICPixelFormat, "format" );
    webp_decoder_info();
    webp_metadata_readers();
    {
        IUnknown *unknown;
        hr = CoCreateInstance( &CLSID_WICWebpDecoder, NULL, CLSCTX_INPROC_SERVER, &IID_IUnknown, (void **)&unknown );
        printf( "CLSID_WICWebpDecoder %#lx\n", hr );
        if (SUCCEEDED(hr)) IUnknown_Release( unknown );
    }
    if (argc > 1) for (i = 1; i < argc; i++) probe_file( argv[i] );
    else
    {
        /* the samples, written next to the program and removed after */
        WCHAR dir[MAX_PATH], path[MAX_PATH];
        unsigned int j;

        GetTempPathW( ARRAY_SIZE(dir), dir );
        for (j = 0; j < ARRAY_SIZE(samples); j++)
        {
            HANDLE file;
            DWORD written;

            swprintf( path, ARRAY_SIZE(path), L"%swicwebpprobe-%hs", dir, samples[j].name );
            file = CreateFileW( path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL );
            if (file == INVALID_HANDLE_VALUE) continue;
            WriteFile( file, samples[j].data, samples[j].size, &written, NULL );
            CloseHandle( file );
            probe_file( path );
            DeleteFileW( path );
        }
    }
    IWICImagingFactory_Release( factory );
    CoUninitialize();
    printf( "done\n" );
    return 0;
}
