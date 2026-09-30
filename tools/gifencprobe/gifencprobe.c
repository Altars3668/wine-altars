/* gifencprobe - what WIC's GIF encoder does with metadata, the way an animated GIF is written.
 *
 * PowerPoint's "animated GIF" export writes frames through WIC and sets the loop count and each
 * frame's delay through metadata query writers; under Wine the query writer's SetMetadataByName is a
 * stub and the GIF encoder has no metadata writers, and the export fails.  This encodes a two-frame
 * GIF: the encoder's query writer gets the NETSCAPE2.0 application extension (loop forever) and a
 * comment, each frame's gets a delay and a disposal method.  It prints every call's result, then
 * walks the bytes that came out block by block, and reads the file back through WIC's decoder and
 * query readers.  Before and after the metadata is set it lists the names each query writer enumerates and
 * the blocks each block writer holds, and on the first frame it tries a value of the wrong type, an item
 * the block does not have and a block GIF does not have.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <wincodec.h>
#include <wincodecsdk.h>
#include <stdio.h>

static IWICImagingFactory *factory;

static void set(IWICMetadataQueryWriter *writer, const WCHAR *name, PROPVARIANT *value)
{
    HRESULT hr = IWICMetadataQueryWriter_SetMetadataByName(writer, name, value);
    printf("  SetMetadataByName %ls: %#lx\n", name, hr);
}

static void set_bytes(IWICMetadataQueryWriter *writer, const WCHAR *name, const BYTE *data, ULONG size)
{
    PROPVARIANT value;

    PropVariantInit(&value);
    value.vt = VT_UI1 | VT_VECTOR;
    value.caub.cElems = size;
    value.caub.pElems = (BYTE *)data;
    set(writer, name, &value);
}

static void set_ui2(IWICMetadataQueryWriter *writer, const WCHAR *name, USHORT v)
{
    PROPVARIANT value;

    PropVariantInit(&value);
    value.vt = VT_UI2;
    value.uiVal = v;
    set(writer, name, &value);
}

static void set_ui1(IWICMetadataQueryWriter *writer, const WCHAR *name, BYTE v)
{
    PROPVARIANT value;

    PropVariantInit(&value);
    value.vt = VT_UI1;
    value.bVal = v;
    set(writer, name, &value);
}

static const char *format_name(const GUID *guid)
{
    static const struct { const GUID *guid; const char *name; } formats[] =
    {
        { &GUID_MetadataFormatLSD, "LSD" }, { &GUID_MetadataFormatIMD, "IMD" }, { &GUID_MetadataFormatGCE, "GCE" },
        { &GUID_MetadataFormatAPE, "APE" }, { &GUID_MetadataFormatGifComment, "GifComment" },
        { &GUID_MetadataFormatUnknown, "Unknown" }, { &GUID_MetadataFormatXMP, "XMP" },
    };
    unsigned int i;

    for (i = 0; i < ARRAYSIZE(formats); i++) if (IsEqualGUID(guid, formats[i].guid)) return formats[i].name;
    return "other";
}

static void list_names(IWICMetadataQueryWriter *writer, const char *what)
{
    IEnumString *names;
    LPOLESTR name;
    HRESULT hr = IWICMetadataQueryWriter_GetEnumerator(writer, &names);

    printf("  %s names (%#lx):", what, hr);
    if (SUCCEEDED(hr))
    {
        while (IEnumString_Next(names, 1, &name, NULL) == S_OK)
        {
            printf(" %ls", name);
            CoTaskMemFree(name);
        }
        IEnumString_Release(names);
    }
    printf("\n");
}

static void list_blocks(IUnknown *object, const char *what)
{
    IWICMetadataBlockWriter *blocks;
    IWICMetadataWriter *block;
    UINT count = 0, items, i;
    GUID format;
    HRESULT hr = IUnknown_QueryInterface(object, &IID_IWICMetadataBlockWriter, (void **)&blocks);

    printf("  %s block writer %#lx", what, hr);
    if (FAILED(hr))
    {
        printf("\n");
        return;
    }
    hr = IWICMetadataBlockWriter_GetCount(blocks, &count);
    printf(", GetCount %#lx %u:", hr, count);
    for (i = 0; SUCCEEDED(hr) && i < count; i++)
    {
        if (FAILED(IWICMetadataBlockWriter_GetWriterByIndex(blocks, i, &block)))
        {
            printf(" ?");
            continue;
        }
        items = 0;
        IWICMetadataWriter_GetMetadataFormat(block, &format);
        IWICMetadataWriter_GetCount(block, &items);
        printf(" %s(%u items)", format_name(&format), items);
        IWICMetadataWriter_Release(block);
    }
    printf("\n");
    IWICMetadataBlockWriter_Release(blocks);
}

static void get_ui2(IWICMetadataQueryWriter *writer, const WCHAR *name)
{
    PROPVARIANT value;
    HRESULT hr;

    PropVariantInit(&value);
    hr = IWICMetadataQueryWriter_GetMetadataByName(writer, name, &value);
    printf("  get %ls: %#lx vt %#x %u\n", name, hr, value.vt, value.vt == VT_UI2 ? value.uiVal : 0);
    PropVariantClear(&value);
}

static void try_bad_values(IWICMetadataQueryWriter *writer)
{
    PROPVARIANT value;

    PropVariantInit(&value);
    value.vt = VT_I4;
    value.lVal = 10;
    printf("  Delay as VT_I4: %#lx\n", IWICMetadataQueryWriter_SetMetadataByName(writer, L"/grctlext/Delay", &value));
    value.vt = VT_UI2;
    value.uiVal = 1;
    printf("  /grctlext/NoSuchItem: %#lx\n", IWICMetadataQueryWriter_SetMetadataByName(writer, L"/grctlext/NoSuchItem", &value));
    value.vt = VT_LPSTR;
    value.pszVal = (char *)"x";
    printf("  /xmp/dc:title on a GIF frame: %#lx\n", IWICMetadataQueryWriter_SetMetadataByName(writer, L"/xmp/dc:title", &value));
}

static void encode_frame(IWICBitmapEncoder *encoder, IWICPalette *palette, BYTE colour, USHORT delay)
{
    IWICMetadataQueryWriter *writer;
    IWICBitmapFrameEncode *frame;
    WICPixelFormatGUID format = GUID_WICPixelFormat8bppIndexed;
    BYTE pixels[8 * 8];
    HRESULT hr;

    memset(pixels, colour, sizeof(pixels));
    hr = IWICBitmapEncoder_CreateNewFrame(encoder, &frame, NULL);
    printf("frame: CreateNewFrame %#lx", hr);
    if (FAILED(hr)) { printf("\n"); return; }
    printf(", Initialize %#lx", IWICBitmapFrameEncode_Initialize(frame, NULL));
    printf(", SetSize %#lx", IWICBitmapFrameEncode_SetSize(frame, 8, 8));
    printf(", SetPixelFormat %#lx", IWICBitmapFrameEncode_SetPixelFormat(frame, &format));
    printf(", SetPalette %#lx\n", IWICBitmapFrameEncode_SetPalette(frame, palette));
    hr = IWICBitmapFrameEncode_GetMetadataQueryWriter(frame, &writer);
    printf("  frame GetMetadataQueryWriter: %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        list_names(writer, "frame");
        list_blocks((IUnknown *)frame, "frame");
        get_ui2(writer, L"/grctlext/Delay");
        if (colour == 1) try_bad_values(writer);
        set_ui2(writer, L"/grctlext/Delay", delay);
        set_ui1(writer, L"/grctlext/Disposal", 2);
        get_ui2(writer, L"/grctlext/Delay");
        list_names(writer, "frame after");
        list_blocks((IUnknown *)frame, "frame after");
        IWICMetadataQueryWriter_Release(writer);
    }
    printf("  WritePixels %#lx", IWICBitmapFrameEncode_WritePixels(frame, 8, 8, sizeof(pixels), pixels));
    printf(", Commit %#lx\n", IWICBitmapFrameEncode_Commit(frame));
    IWICBitmapFrameEncode_Release(frame);
}

/* the blocks of a GIF file, as they are in the bytes */
static void walk(const BYTE *data, SIZE_T size)
{
    SIZE_T i = 13;

    if (size < 13) { printf("file too short: %llu bytes\n", (unsigned long long)size); return; }
    printf("file %llu bytes, %.6s, screen %ux%u, flags %#x, background %u, aspect %u\n", (unsigned long long)size, data,
           data[6] | data[7] << 8, data[8] | data[9] << 8, data[10], data[11], data[12]);
    if (data[10] & 0x80) i += 3 << ((data[10] & 7) + 1);
    while (i < size)
    {
        if (data[i] == 0x3b) { printf("trailer at %llu\n", (unsigned long long)i); return; }
        if (data[i] == 0x21 && i + 1 < size)
        {
            BYTE label = data[i + 1];
            SIZE_T j = i + 2;

            printf("extension %#x:", label);
            while (j < size && data[j])
            {
                SIZE_T k, n = data[j];

                printf(" [");
                for (k = 1; k <= n && j + k < size; k++) printf("%02x", data[j + k]);
                printf("]");
                j += n + 1;
            }
            printf("\n");
            i = j + 1;
        }
        else if (data[i] == 0x2c && i + 10 <= size)
        {
            SIZE_T j = i + 10;

            printf("image at %u,%u size %ux%u flags %#x", data[i + 1] | data[i + 2] << 8, data[i + 3] | data[i + 4] << 8,
                   data[i + 5] | data[i + 6] << 8, data[i + 7] | data[i + 8] << 8, data[i + 9]);
            if (data[i + 9] & 0x80) j += 3 << ((data[i + 9] & 7) + 1);
            printf(", lzw minimum %u\n", data[j]);
            j++;
            while (j < size && data[j]) j += data[j] + 1;
            i = j + 1;
        }
        else
        {
            printf("unexpected byte %#x at %llu\n", data[i], (unsigned long long)i);
            return;
        }
    }
}

static void read_back(IStream *stream)
{
    IWICMetadataQueryReader *reader;
    IWICBitmapFrameDecode *frame;
    IWICBitmapDecoder *decoder;
    LARGE_INTEGER zero = {{0}};
    const WCHAR *names[] = { L"/appext/Application", L"/appext/Data", L"/commentext/TextEntry",
                             L"/logscrdesc/Width" };
    PROPVARIANT value;
    UINT count = 0, i, n;
    HRESULT hr;

    IStream_Seek(stream, zero, STREAM_SEEK_SET, NULL);
    hr = IWICImagingFactory_CreateDecoderFromStream(factory, stream, NULL, WICDecodeMetadataCacheOnLoad, &decoder);
    printf("decoder: %#lx", hr);
    if (FAILED(hr)) { printf("\n"); return; }
    IWICBitmapDecoder_GetFrameCount(decoder, &count);
    printf(", %u frames\n", count);
    if (SUCCEEDED(IWICBitmapDecoder_GetMetadataQueryReader(decoder, &reader)))
    {
        for (n = 0; n < ARRAYSIZE(names); n++)
        {
            PropVariantInit(&value);
            hr = IWICMetadataQueryReader_GetMetadataByName(reader, names[n], &value);
            printf("  %ls: %#lx vt %#x", names[n], hr, value.vt);
            if (value.vt == (VT_UI1 | VT_VECTOR))
                for (i = 0; i < value.caub.cElems; i++) printf(" %02x", value.caub.pElems[i]);
            else if (value.vt == VT_LPSTR) printf(" \"%s\"", value.pszVal);
            else if (value.vt == VT_UI2) printf(" %u", value.uiVal);
            printf("\n");
            PropVariantClear(&value);
        }
        IWICMetadataQueryReader_Release(reader);
    }
    for (i = 0; i < count; i++)
    {
        if (FAILED(IWICBitmapDecoder_GetFrame(decoder, i, &frame))) continue;
        if (SUCCEEDED(IWICBitmapFrameDecode_GetMetadataQueryReader(frame, &reader)))
        {
            PropVariantInit(&value);
            hr = IWICMetadataQueryReader_GetMetadataByName(reader, L"/grctlext/Delay", &value);
            printf("  frame %u delay: %#lx %u", i, hr, value.uiVal);
            PropVariantClear(&value);
            PropVariantInit(&value);
            hr = IWICMetadataQueryReader_GetMetadataByName(reader, L"/grctlext/Disposal", &value);
            printf(", disposal: %#lx %u\n", hr, value.bVal);
            PropVariantClear(&value);
            IWICMetadataQueryReader_Release(reader);
        }
        IWICBitmapFrameDecode_Release(frame);
    }
    IWICBitmapDecoder_Release(decoder);
}

int main(void)
{
    static const BYTE netscape[] = "NETSCAPE2.0", loop[] = { 3, 1, 0, 0 };
    WICColor colours[4] = { 0xff000000, 0xffff0000, 0xff00ff00, 0xff0000ff };
    IWICMetadataQueryWriter *writer;
    IWICBitmapEncoder *encoder;
    IWICPalette *palette;
    STATSTG stat;
    IStream *stream;
    HGLOBAL global;
    PROPVARIANT value;
    HRESULT hr;

    CoInitialize(NULL);
    CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void **)&factory);
    IWICImagingFactory_CreatePalette(factory, &palette);
    IWICPalette_InitializeCustom(palette, colours, 4);
    CreateStreamOnHGlobal(NULL, TRUE, &stream);

    hr = IWICImagingFactory_CreateEncoder(factory, &GUID_ContainerFormatGif, NULL, &encoder);
    printf("CreateEncoder %#lx, Initialize %#lx\n", hr, IWICBitmapEncoder_Initialize(encoder, stream, WICBitmapEncoderNoCache));
    hr = IWICBitmapEncoder_GetMetadataQueryWriter(encoder, &writer);
    printf("encoder GetMetadataQueryWriter: %#lx\n", hr);
    list_blocks((IUnknown *)encoder, "encoder");
    if (SUCCEEDED(hr))
    {
        list_names(writer, "encoder");
        set_bytes(writer, L"/appext/Application", netscape, 11);
        set_bytes(writer, L"/appext/Data", loop, sizeof(loop));
        PropVariantInit(&value);
        value.vt = VT_LPSTR;
        value.pszVal = (char *)"gifencprobe";
        set(writer, L"/commentext/TextEntry", &value);
        list_names(writer, "encoder after");
        list_blocks((IUnknown *)encoder, "encoder after");
        IWICMetadataQueryWriter_Release(writer);
    }
    encode_frame(encoder, palette, 1, 50);
    encode_frame(encoder, palette, 2, 75);
    printf("encoder Commit %#lx\n", IWICBitmapEncoder_Commit(encoder));
    IWICBitmapEncoder_Release(encoder);

    IStream_Stat(stream, &stat, STATFLAG_NONAME);
    GetHGlobalFromStream(stream, &global);
    walk(GlobalLock(global), stat.cbSize.QuadPart);
    GlobalUnlock(global);
    read_back(stream);

    IStream_Release(stream);
    IWICPalette_Release(palette);
    IWICImagingFactory_Release(factory);
    CoUninitialize();
    return 0;
}
