/* mftwebpprobe: the Media Foundation transforms that take WebP, the way Office looks for them -- a video decoder
 * with the input subtype that is the WIC WebP decoder's CLSID -- and what they say about themselves. */
#define COBJMACROS
#include <stdio.h>
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <mferror.h>

#include "../wicwebpprobe/samples.h"

static const GUID subtype_webp = {0x7693e886,0x51c9,0x4070,{0x84,0x19,0x9f,0x70,0x73,0x8e,0xc8,0xfa}};

static const char *guid_str( const GUID *guid )
{
    static char buffers[8][40];
    static int n;
    char *buffer = buffers[n++ % 8];
    sprintf( buffer, "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", guid->Data1, guid->Data2, guid->Data3,
             guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3], guid->Data4[4], guid->Data4[5],
             guid->Data4[6], guid->Data4[7] );
    return buffer;
}

static void print_types( IMFActivate *activate, const GUID *key, const char *what )
{
    MFT_REGISTER_TYPE_INFO *types;
    UINT32 size, i;

    if (FAILED(IMFActivate_GetAllocatedBlob( activate, key, (UINT8 **)&types, &size ))) return;
    printf( "  %s:", what );
    for (i = 0; i < size / sizeof(*types); i++)
        printf( " %s/%s", guid_str( &types[i].guidMajorType ), guid_str( &types[i].guidSubtype ) );
    printf( "\n" );
    CoTaskMemFree( types );
}

static void print_attributes( IMFAttributes *attributes, const char *indent )
{
    UINT32 count = 0, i;

    IMFAttributes_GetCount( attributes, &count );
    for (i = 0; i < count; i++)
    {
        PROPVARIANT value;
        GUID key;

        PropVariantInit( &value );
        if (FAILED(IMFAttributes_GetItemByIndex( attributes, i, &key, &value ))) continue;
        printf( "%s%s:", indent, guid_str( &key ) );
        switch (value.vt)
        {
        case VT_UI4: printf( " %u\n", value.ulVal ); break;
        case VT_UI8: printf( " %u,%u\n", (UINT32)(value.uhVal.QuadPart >> 32), (UINT32)value.uhVal.QuadPart ); break;
        case VT_CLSID: printf( " %s\n", guid_str( value.puuid ) ); break;
        case VT_LPWSTR: printf( " %ls\n", value.pwszVal ); break;
        case VT_R8: printf( " %f\n", value.dblVal ); break;
        case VT_VECTOR | VT_UI1: printf( " blob of %lu\n", value.caub.cElems ); break;
        default: printf( " vt %d\n", value.vt ); break;
        }
        PropVariantClear( &value );
    }
}

static DWORD checksum( const BYTE *data, UINT size )
{
    DWORD hash = 2166136261u;
    UINT i;
    for (i = 0; i < size; i++) hash = (hash ^ data[i]) * 16777619u;
    return hash;
}

/* every method, with plausible arguments */
static void methods( IMFTransform *transform )
{
    DWORD a = 0xdead, b = 0xdead, c = 0xdead, d = 0xdead, ids_in[4], ids_out[4];
    MFT_INPUT_STREAM_INFO input_info;
    MFT_OUTPUT_STREAM_INFO output_info;
    MFT_OUTPUT_DATA_BUFFER output = { 0 };
    IMFAttributes *attributes;
    IMFMediaType *type;
    IMFSample *sample;
    IUnknown *unknown;
    HRESULT hr;

    hr = IMFTransform_GetStreamLimits( transform, &a, &b, &c, &d );
    printf( "  GetStreamLimits %#lx %lu %lu %lu %lu\n", hr, a, b, c, d );
    hr = IMFTransform_GetStreamCount( transform, &a, &b );
    printf( "  GetStreamCount %#lx %lu %lu\n", hr, a, b );
    hr = IMFTransform_GetStreamIDs( transform, 4, ids_in, 4, ids_out );
    printf( "  GetStreamIDs %#lx\n", hr );
    hr = IMFTransform_GetInputStreamInfo( transform, 0, &input_info );
    printf( "  GetInputStreamInfo %#lx\n", hr );
    hr = IMFTransform_GetOutputStreamInfo( transform, 0, &output_info );
    printf( "  GetOutputStreamInfo %#lx\n", hr );
    attributes = NULL;
    hr = IMFTransform_GetAttributes( transform, &attributes );
    printf( "  GetAttributes %#lx\n", hr );
    if (attributes) { print_attributes( attributes, "    " ); IMFAttributes_Release( attributes ); }
    attributes = NULL;
    hr = IMFTransform_GetInputStreamAttributes( transform, 0, &attributes );
    printf( "  GetInputStreamAttributes %#lx\n", hr );
    if (attributes) IMFAttributes_Release( attributes );
    attributes = NULL;
    hr = IMFTransform_GetOutputStreamAttributes( transform, 0, &attributes );
    printf( "  GetOutputStreamAttributes %#lx\n", hr );
    if (attributes) IMFAttributes_Release( attributes );
    hr = IMFTransform_DeleteInputStream( transform, 0 );
    printf( "  DeleteInputStream %#lx\n", hr );
    hr = IMFTransform_AddInputStreams( transform, 1, ids_in );
    printf( "  AddInputStreams %#lx\n", hr );
    type = NULL;
    hr = IMFTransform_GetInputAvailableType( transform, 0, 0, &type );
    printf( "  GetInputAvailableType %#lx\n", hr );
    if (type) IMFMediaType_Release( type );
    type = NULL;
    hr = IMFTransform_GetOutputAvailableType( transform, 0, 0, &type );
    printf( "  GetOutputAvailableType %#lx\n", hr );
    if (type) IMFMediaType_Release( type );
    MFCreateMediaType( &type );
    IMFMediaType_SetGUID( type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video );
    IMFMediaType_SetGUID( type, &MF_MT_SUBTYPE, &subtype_webp );
    hr = IMFTransform_SetInputType( transform, 0, type, MFT_SET_TYPE_TEST_ONLY );
    printf( "  SetInputType test %#lx\n", hr );
    hr = IMFTransform_SetOutputType( transform, 0, type, 0 );
    printf( "  SetOutputType %#lx\n", hr );
    hr = IMFTransform_SetInputType( transform, 0, NULL, 0 );
    printf( "  SetInputType NULL %#lx\n", hr );
    IMFMediaType_Release( type );
    type = NULL;
    hr = IMFTransform_GetInputCurrentType( transform, 0, &type );
    printf( "  GetInputCurrentType %#lx\n", hr );
    if (type) IMFMediaType_Release( type );
    type = NULL;
    hr = IMFTransform_GetOutputCurrentType( transform, 0, &type );
    printf( "  GetOutputCurrentType %#lx\n", hr );
    if (type) IMFMediaType_Release( type );
    hr = IMFTransform_GetInputStatus( transform, 0, &a );
    printf( "  GetInputStatus %#lx\n", hr );
    hr = IMFTransform_GetOutputStatus( transform, &a );
    printf( "  GetOutputStatus %#lx\n", hr );
    hr = IMFTransform_SetOutputBounds( transform, 0, 0 );
    printf( "  SetOutputBounds %#lx\n", hr );
    hr = IMFTransform_ProcessEvent( transform, 0, NULL );
    printf( "  ProcessEvent %#lx\n", hr );
    hr = IMFTransform_ProcessMessage( transform, MFT_MESSAGE_COMMAND_FLUSH, 0 );
    printf( "  ProcessMessage flush %#lx\n", hr );
    hr = IMFTransform_ProcessMessage( transform, MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0 );
    printf( "  ProcessMessage begin streaming %#lx\n", hr );
    MFCreateSample( &sample );
    hr = IMFTransform_ProcessInput( transform, 0, sample, 0 );
    printf( "  ProcessInput %#lx\n", hr );
    IMFSample_Release( sample );
    hr = IMFTransform_ProcessOutput( transform, 0, 1, &output, &a );
    printf( "  ProcessOutput %#lx\n", hr );
    unknown = NULL;
    hr = IMFTransform_QueryInterface( transform, &IID_IMFGetService, (void **)&unknown );
    printf( "  IMFGetService %#lx\n", hr );
    if (unknown) IUnknown_Release( unknown );
    unknown = NULL;
    hr = IMFTransform_QueryInterface( transform, &IID_IMFShutdown, (void **)&unknown );
    printf( "  IMFShutdown %#lx\n", hr );
    if (unknown) IUnknown_Release( unknown );
}

/* push one WebP file through the transform */
static void decode( IMFTransform *transform, const unsigned char *data, unsigned int size, int with_size )
{
    MFT_INPUT_STREAM_INFO input_info;
    MFT_OUTPUT_STREAM_INFO output_info;
    MFT_OUTPUT_DATA_BUFFER output;
    IMFMediaType *type;
    IMFMediaBuffer *buffer;
    IMFSample *sample;
    IMFAttributes *attributes;
    DWORD status, index, stream_inputs, stream_outputs, min_in, max_in, min_out, max_out;
    BYTE *bytes;
    HRESULT hr;

    MFCreateMediaType( &type );
    IMFMediaType_SetGUID( type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video );
    IMFMediaType_SetGUID( type, &MF_MT_SUBTYPE, &subtype_webp );
    if (with_size) IMFMediaType_SetUINT64( type, &MF_MT_FRAME_SIZE, (UINT64)96 << 32 | 64 );
    hr = IMFTransform_SetInputType( transform, 0, type, 0 );
    printf( "  set input type (%s frame size): %#lx\n", with_size ? "with" : "without", hr );
    IMFMediaType_Release( type );
    if (FAILED(hr)) return;

    hr = IMFTransform_GetStreamCount( transform, &stream_inputs, &stream_outputs );
    printf( "  streams %#lx %lu %lu", hr, stream_inputs, stream_outputs );
    hr = IMFTransform_GetStreamLimits( transform, &min_in, &max_in, &min_out, &max_out );
    printf( " limits %#lx %lu-%lu %lu-%lu\n", hr, min_in, max_in, min_out, max_out );
    if (SUCCEEDED(IMFTransform_GetAttributes( transform, &attributes )))
    {
        printf( "  transform attributes:\n" );
        print_attributes( attributes, "    " );
        IMFAttributes_Release( attributes );
    }
    if (SUCCEEDED(IMFTransform_GetInputCurrentType( transform, 0, &type )))
    {
        printf( "  input type:\n" );
        print_attributes( (IMFAttributes *)type, "    " );
        IMFMediaType_Release( type );
    }
    for (index = 0; IMFTransform_GetOutputAvailableType( transform, 0, index, &type ) == S_OK; index++)
    {
        printf( "  output type %lu:\n", index );
        print_attributes( (IMFAttributes *)type, "    " );
        if (!index)
        {
            hr = IMFTransform_SetOutputType( transform, 0, type, 0 );
            printf( "  set output type 0: %#lx\n", hr );
        }
        IMFMediaType_Release( type );
    }
    hr = IMFTransform_GetInputStreamInfo( transform, 0, &input_info );
    printf( "  input info %#lx: flags %#lx size %lu lookahead %lu alignment %lu\n", hr, input_info.dwFlags,
            input_info.cbSize, input_info.cbMaxLookahead, input_info.cbAlignment );
    hr = IMFTransform_GetOutputStreamInfo( transform, 0, &output_info );
    printf( "  output info %#lx: flags %#lx size %lu alignment %lu\n", hr, output_info.dwFlags, output_info.cbSize,
            output_info.cbAlignment );

    hr = IMFTransform_ProcessMessage( transform, MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0 );
    printf( "  begin streaming %#lx\n", hr );
    MFCreateMemoryBuffer( size, &buffer );
    IMFMediaBuffer_Lock( buffer, &bytes, NULL, NULL );
    memcpy( bytes, data, size );
    IMFMediaBuffer_Unlock( buffer );
    IMFMediaBuffer_SetCurrentLength( buffer, size );
    MFCreateSample( &sample );
    IMFSample_AddBuffer( sample, buffer );
    IMFMediaBuffer_Release( buffer );
    hr = IMFTransform_ProcessInput( transform, 0, sample, 0 );
    printf( "  process input %#lx\n", hr );
    IMFSample_Release( sample );
    hr = IMFTransform_ProcessInput( transform, 0, NULL, 0 );

    for (index = 0; index < 3; index++)
    {
        memset( &output, 0, sizeof(output) );
        if (!(output_info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)))
        {
            MFCreateMemoryBuffer( output_info.cbSize ? output_info.cbSize : 96 * 64 * 8, &buffer );
            MFCreateSample( &output.pSample );
            IMFSample_AddBuffer( output.pSample, buffer );
            IMFMediaBuffer_Release( buffer );
        }
        status = 0;
        hr = IMFTransform_ProcessOutput( transform, 0, 1, &output, &status );
        printf( "  process output %lu: %#lx status %#lx flags %#lx\n", index, hr, status, output.dwStatus );
        if (output.pSample)
        {
            if (SUCCEEDED(hr) && SUCCEEDED(IMFSample_ConvertToContiguousBuffer( output.pSample, &buffer )))
            {
                DWORD length = 0;
                LONGLONG time = -1, duration = -1;
                IMFMediaBuffer_Lock( buffer, &bytes, NULL, &length );
                IMFSample_GetSampleTime( output.pSample, &time );
                IMFSample_GetSampleDuration( output.pSample, &duration );
                printf( "    %lu bytes, checksum %08lx, first %02x%02x%02x%02x%02x%02x%02x%02x, time %lld duration %lld\n",
                        length, checksum( bytes, length ), bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5],
                        bytes[6], bytes[7], time, duration );
                IMFMediaBuffer_Unlock( buffer );
                IMFMediaBuffer_Release( buffer );
                print_attributes( (IMFAttributes *)output.pSample, "    sample " );
            }
            IMFSample_Release( output.pSample );
        }
        if (output.pEvents) IMFCollection_Release( output.pEvents );
        if (hr == MF_E_TRANSFORM_STREAM_CHANGE)
        {
            if (IMFTransform_GetOutputAvailableType( transform, 0, 0, &type ) == S_OK)
            {
                printf( "  new output type:\n" );
                print_attributes( (IMFAttributes *)type, "    " );
                hr = IMFTransform_SetOutputType( transform, 0, type, 0 );
                printf( "  set it: %#lx\n", hr );
                IMFMediaType_Release( type );
            }
            hr = IMFTransform_GetOutputStreamInfo( transform, 0, &output_info );
            printf( "  output info %#lx: flags %#lx size %lu alignment %lu\n", hr, output_info.dwFlags,
                    output_info.cbSize, output_info.cbAlignment );
        }
    }
}

int main(void)
{
    MFT_REGISTER_TYPE_INFO input = { MFMediaType_Video, subtype_webp };
    IMFActivate **activates;
    UINT32 count, i;
    HRESULT hr;

    setvbuf( stdout, NULL, _IONBF, 0 );
    CoInitializeEx( NULL, COINIT_MULTITHREADED );
    MFStartup( MF_VERSION, 0 );
    hr = MFTEnumEx( MFT_CATEGORY_VIDEO_DECODER, MFT_ENUM_FLAG_ALL, &input, NULL, &activates, &count );
    printf( "MFTEnumEx %#lx, %u\n", hr, count );
    for (i = 0; SUCCEEDED(hr) && i < count; i++)
    {
        WCHAR *name = NULL, path[MAX_PATH];
        GUID clsid;
        UINT32 flags = 0, length;
        HKEY key;
        char keyname[128];
        DWORD size = sizeof(path);

        IMFActivate_GetAllocatedString( activates[i], &MFT_FRIENDLY_NAME_Attribute, &name, &length );
        printf( "activate attributes:\n" );
        print_attributes( (IMFAttributes *)activates[i], "  " );
        IMFActivate_GetGUID( activates[i], &MFT_TRANSFORM_CLSID_Attribute, &clsid );
        IMFActivate_GetUINT32( activates[i], &MF_TRANSFORM_FLAGS_Attribute, &flags );
        printf( "mft %u: %ls clsid %s flags %#x\n", i, name, guid_str( &clsid ), flags );
        CoTaskMemFree( name );
        print_types( activates[i], &MFT_INPUT_TYPES_Attributes, "input" );
        print_types( activates[i], &MFT_OUTPUT_TYPES_Attributes, "output" );
        sprintf( keyname, "CLSID\\%s\\InprocServer32", guid_str( &clsid ) );
        path[0] = 0;
        if (!RegOpenKeyExA( HKEY_CLASSES_ROOT, keyname, 0, KEY_READ, &key ))
        {
            RegQueryValueExW( key, NULL, NULL, NULL, (BYTE *)path, &size );
            RegCloseKey( key );
        }
        printf( "  server %ls\n", path );

        /* what the transform itself offers */
        {
            IMFTransform *transform;
            IMFMediaType *type;
            DWORD index;

            hr = IMFActivate_ActivateObject( activates[i], &IID_IMFTransform, (void **)&transform );
            printf( "  activate %#lx\n", hr );
            if (SUCCEEDED(hr))
            {
                for (index = 0; IMFTransform_GetInputAvailableType( transform, 0, index, &type ) == S_OK; index++)
                {
                    GUID major, sub;
                    IMFMediaType_GetMajorType( type, &major );
                    IMFMediaType_GetGUID( type, &MF_MT_SUBTYPE, &sub );
                    printf( "  available input %lu: %s %s\n", index, guid_str( &major ), guid_str( &sub ) );
                    IMFMediaType_Release( type );
                }
                hr = IMFTransform_GetOutputAvailableType( transform, 0, 0, &type );
                printf( "  output types before input: %#lx\n", hr );
                if (SUCCEEDED(hr)) IMFMediaType_Release( type );
                methods( transform );
                decode( transform, samples[0].data, samples[0].size, 0 );
                IMFActivate_ShutdownObject( activates[i] );
                IMFTransform_Release( transform );
            }
            hr = IMFActivate_ActivateObject( activates[i], &IID_IMFTransform, (void **)&transform );
            if (SUCCEEDED(hr))
            {
                printf( "  again, with a frame size, lossless with alpha:\n" );
                decode( transform, samples[3].data, samples[3].size, 1 );
                IMFActivate_ShutdownObject( activates[i] );
                IMFTransform_Release( transform );
            }
        }
        IMFActivate_Release( activates[i] );
    }
    CoTaskMemFree( activates );

    /* a registered decoder, for comparison: H.264 */
    {
        MFT_REGISTER_TYPE_INFO h264 = { MFMediaType_Video, MFVideoFormat_H264 };
        hr = MFTEnumEx( MFT_CATEGORY_VIDEO_DECODER, MFT_ENUM_FLAG_SYNCMFT, &h264, NULL, &activates, &count );
        printf( "H.264 decoders %#lx, %u\n", hr, count );
        for (i = 0; SUCCEEDED(hr) && i < count && i < 1; i++)
        {
            printf( "activate attributes:\n" );
            print_attributes( (IMFAttributes *)activates[i], "  " );
        }
        for (i = 0; SUCCEEDED(hr) && i < count; i++) IMFActivate_Release( activates[i] );
        if (SUCCEEDED(hr)) CoTaskMemFree( activates );
    }
    MFShutdown();
    printf( "done\n" );
    return 0;
}
