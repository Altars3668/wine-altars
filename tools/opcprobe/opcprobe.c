/* What the Windows OPC API (opcservices.dll, IOpcFactory) writes and reads.
 *
 * Builds a small package with parts of every compression option and relationships with internal,
 * external absolute and external relative targets, writes it (the bytes are printed as base64, to be
 * unzipped elsewhere), prints the relationships parts as GetRelationshipsContentStream gives them,
 * reads the package back and lists it, feeds ReadPackageFromStream hand-made ZIP files (no content
 * types, an undeclared extension, an absolute target without TargetMode, TargetMode spellings), and
 * prints what the remaining methods answer: DeletePart, DeleteRelationship, GetEnumeratorForType,
 * ComparePartUri, GetRelativeUri and the IStream methods of a part's content stream and of
 * CreateStreamOnFile's stream.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror opcprobe.c -o opcprobe.exe -lole32 -luuid -loleaut32 -lurlmon
 */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <msopc.h>
#include <urlmon.h>
#include <stdio.h>
#include <string.h>

DEFINE_GUID(IID_unknown_factory_iface, 0xe19c7100, 0x9709, 0x4db7, 0x93, 0x73, 0xe7, 0xb5, 0x18, 0xb4, 0x70, 0x86);

static IOpcFactory *factory;

static void print_b64(const char *tag, const BYTE *data, ULONG size)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char line[80];
    ULONG i, n = 0;

    printf("%s-begin %lu\n", tag, size);
    for (i = 0; i < size; i += 3)
    {
        DWORD v = data[i] << 16;
        int k = size - i;

        if (k > 1) v |= data[i + 1] << 8;
        if (k > 2) v |= data[i + 2];
        line[n++] = alphabet[(v >> 18) & 63];
        line[n++] = alphabet[(v >> 12) & 63];
        line[n++] = k > 1 ? alphabet[(v >> 6) & 63] : '=';
        line[n++] = k > 2 ? alphabet[v & 63] : '=';
        if (n == 76)
        {
            printf("%s %.*s\n", tag, (int)n, line);
            n = 0;
        }
    }
    if (n) printf("%s %.*s\n", tag, (int)n, line);
    printf("%s-end\n", tag);
}

static BYTE *stream_bytes(IStream *stream, ULONG *size)
{
    LARGE_INTEGER zero = {{0}};
    STATSTG stat;
    ULONG read = 0;
    BYTE *data;

    *size = 0;
    if (FAILED(IStream_Stat(stream, &stat, STATFLAG_NONAME))) return NULL;
    if (FAILED(IStream_Seek(stream, zero, STREAM_SEEK_SET, NULL))) return NULL;
    data = malloc(stat.cbSize.QuadPart + 1);
    IStream_Read(stream, data, stat.cbSize.QuadPart, &read);
    data[read] = 0;
    *size = read;
    return data;
}

static void print_text(const char *tag, IStream *stream)
{
    ULONG size;
    BYTE *data = stream_bytes(stream, &size);

    if (!data) { printf("%s: (unreadable)\n", tag); return; }
    printf("%s (%lu bytes): %s\n", tag, size, data);
    free(data);
}

static IOpcPartUri *part_uri(const WCHAR *name)
{
    IOpcPartUri *uri = NULL;
    HRESULT hr = IOpcFactory_CreatePartUri(factory, name, &uri);

    if (FAILED(hr)) printf("CreatePartUri %ls: %#lx\n", name, hr);
    return uri;
}

static IOpcPart *add_part(IOpcPartSet *set, const WCHAR *name, const WCHAR *type,
        OPC_COMPRESSION_OPTIONS options, const char *content)
{
    IOpcPartUri *uri = part_uri(name);
    IOpcPart *part = NULL;
    IStream *stream;
    HRESULT hr;

    hr = IOpcPartSet_CreatePart(set, uri, type, options, &part);
    IOpcPartUri_Release(uri);
    if (FAILED(hr)) { printf("CreatePart %ls: %#lx\n", name, hr); return NULL; }
    if (SUCCEEDED(IOpcPart_GetContentStream(part, &stream)))
    {
        IStream_Write(stream, content, strlen(content), NULL);
        IStream_Release(stream);
    }
    return part;
}

static void add_rel(IOpcRelationshipSet *rels, const WCHAR *id, const WCHAR *type, const WCHAR *target,
        OPC_URI_TARGET_MODE mode)
{
    IOpcRelationship *rel;
    IUri *uri;
    HRESULT hr;

    hr = CreateUri(target, Uri_CREATE_ALLOW_RELATIVE, 0, &uri);
    if (FAILED(hr)) { printf("CreateUri %ls: %#lx\n", target, hr); return; }
    hr = IOpcRelationshipSet_CreateRelationship(rels, id, type, uri, mode, &rel);
    printf("CreateRelationship %ls -> %ls mode %d: %#lx\n", id ? id : L"(null)", target, mode, hr);
    if (SUCCEEDED(hr)) IOpcRelationship_Release(rel);
    IUri_Release(uri);
}

static void list_rels(const char *indent, IOpcRelationshipSet *rels)
{
    IOpcRelationshipEnumerator *e;
    BOOL next = FALSE;

    if (FAILED(IOpcRelationshipSet_GetEnumerator(rels, &e))) return;
    while (SUCCEEDED(IOpcRelationshipEnumerator_MoveNext(e, &next)) && next)
    {
        OPC_URI_TARGET_MODE mode = -1;
        WCHAR *id = NULL, *type = NULL;
        IOpcRelationship *rel;
        BSTR target = NULL, source = NULL;
        IOpcUri *src = NULL;
        IUri *uri = NULL;

        if (FAILED(IOpcRelationshipEnumerator_GetCurrent(e, &rel))) break;
        IOpcRelationship_GetId(rel, &id);
        IOpcRelationship_GetRelationshipType(rel, &type);
        IOpcRelationship_GetTargetMode(rel, &mode);
        if (SUCCEEDED(IOpcRelationship_GetTargetUri(rel, &uri)))
        {
            IUri_GetRawUri(uri, &target);
            IUri_Release(uri);
        }
        if (SUCCEEDED(IOpcRelationship_GetSourceUri(rel, &src)))
        {
            IOpcUri_GetRawUri(src, &source);
            IOpcUri_Release(src);
        }
        printf("%srel id %ls (len %u) type %ls target %ls mode %d source %ls\n", indent,
                id && id[0] == 'R' && wcslen(id) == 9 ? L"R<generated>" : id, id ? (unsigned)wcslen(id) : 0,
                type, target, mode, source);
        CoTaskMemFree(id);
        CoTaskMemFree(type);
        SysFreeString(target);
        SysFreeString(source);
        IOpcRelationship_Release(rel);
    }
    IOpcRelationshipEnumerator_Release(e);
}

static void list_package(IOpcPackage *package)
{
    IOpcRelationshipSet *rels;
    IOpcPartEnumerator *e;
    IOpcPartSet *parts;
    BOOL next = FALSE;

    if (SUCCEEDED(IOpcPackage_GetRelationshipSet(package, &rels)))
    {
        printf("package relationships:\n");
        list_rels("  ", rels);
        IOpcRelationshipSet_Release(rels);
    }
    if (FAILED(IOpcPackage_GetPartSet(package, &parts))) return;
    if (FAILED(IOpcPartSet_GetEnumerator(parts, &e))) { IOpcPartSet_Release(parts); return; }
    while (SUCCEEDED(IOpcPartEnumerator_MoveNext(e, &next)) && next)
    {
        OPC_COMPRESSION_OPTIONS options = -1;
        IOpcPartUri *name = NULL;
        WCHAR *type = NULL;
        BSTR raw = NULL;
        IStream *stream;
        IOpcPart *part;
        ULONG size = 0;

        if (FAILED(IOpcPartEnumerator_GetCurrent(e, &part))) break;
        IOpcPart_GetName(part, &name);
        if (name) IOpcPartUri_GetRawUri(name, &raw);
        IOpcPart_GetContentType(part, &type);
        IOpcPart_GetCompressionOptions(part, &options);
        if (SUCCEEDED(IOpcPart_GetContentStream(part, &stream)))
        {
            free(stream_bytes(stream, &size));
            IStream_Release(stream);
        }
        printf("part %ls type %ls compression %d size %lu\n", raw, type, options, size);
        if (SUCCEEDED(IOpcPart_GetRelationshipSet(part, &rels)))
        {
            list_rels("  ", rels);
            IOpcRelationshipSet_Release(rels);
        }
        SysFreeString(raw);
        CoTaskMemFree(type);
        if (name) IOpcPartUri_Release(name);
        IOpcPart_Release(part);
    }
    IOpcPartEnumerator_Release(e);
    IOpcPartSet_Release(parts);
}

/* a stored-only ZIP writer for hand-made packages */
struct zip
{
    BYTE data[16384];
    ULONG size;
    BYTE central[4096];
    ULONG central_size;
    int count;
};

static DWORD crc32(const BYTE *p, ULONG n)
{
    DWORD crc = ~0u;
    ULONG i;
    int k;

    for (i = 0; i < n; i++)
    {
        crc ^= p[i];
        for (k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xedb88320 & -(crc & 1));
    }
    return ~crc;
}

static void put16(BYTE *p, WORD v) { p[0] = v; p[1] = v >> 8; }
static void put32(BYTE *p, DWORD v) { put16(p, v); put16(p + 2, v >> 16); }

static void zip_add(struct zip *z, const char *name, const char *content)
{
    ULONG nlen = strlen(name), clen = strlen(content), offset = z->size;
    DWORD crc = crc32((const BYTE *)content, clen);
    BYTE *p = z->data + z->size, *c = z->central + z->central_size;

    memset(p, 0, 30);
    put32(p, 0x04034b50); put16(p + 4, 20); put32(p + 14, crc); put32(p + 18, clen); put32(p + 22, clen);
    put16(p + 26, nlen);
    memcpy(p + 30, name, nlen);
    memcpy(p + 30 + nlen, content, clen);
    z->size += 30 + nlen + clen;

    memset(c, 0, 46);
    put32(c, 0x02014b50); put16(c + 4, 20); put16(c + 6, 20); put32(c + 16, crc); put32(c + 20, clen);
    put32(c + 24, clen); put16(c + 28, nlen); put32(c + 42, offset);
    memcpy(c + 46, name, nlen);
    z->central_size += 46 + nlen;
    z->count++;
}

static IStream *zip_finish(struct zip *z)
{
    BYTE *p = z->data + z->size;
    IStream *stream;

    memcpy(p, z->central, z->central_size);
    p += z->central_size;
    memset(p, 0, 22);
    put32(p, 0x06054b50); put16(p + 8, z->count); put16(p + 10, z->count); put32(p + 12, z->central_size);
    put32(p + 16, z->size);
    CreateStreamOnHGlobal(NULL, TRUE, &stream);
    IStream_Write(stream, z->data, z->size + z->central_size + 22, NULL);
    return stream;
}

static const char content_types[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
    "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
    "<Default Extension=\"xml\" ContentType=\"application/xml\"/></Types>";
static const char package_rels[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
    "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"doc.xml\"/>"
    "</Relationships>";

static void read_case(const char *what, struct zip *z)
{
    static const OPC_READ_FLAGS flags[] = { OPC_READ_DEFAULT, OPC_VALIDATE_ON_LOAD, OPC_CACHE_ON_ACCESS };
    LARGE_INTEGER zero = {{0}};
    IOpcPackage *package;
    IStream *stream = zip_finish(z);
    unsigned int i;
    HRESULT hr;

    for (i = 0; i < ARRAYSIZE(flags); i++)
    {
        IStream_Seek(stream, zero, STREAM_SEEK_SET, NULL);
        package = NULL;
        hr = IOpcFactory_ReadPackageFromStream(factory, stream, flags[i], &package);
        printf("read %s flags %d: %#lx\n", what, flags[i], hr);
        if (SUCCEEDED(hr))
        {
            if (!i) list_package(package);
            IOpcPackage_Release(package);
        }
    }
    IStream_Release(stream);
}

static void rels_case(const char *what, const char *relationship)
{
    char rels[1024];
    struct zip z = {0};

    snprintf(rels, sizeof(rels), "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">%s"
            "</Relationships>", relationship);
    zip_add(&z, "[Content_Types].xml", content_types);
    zip_add(&z, "_rels/.rels", package_rels);
    zip_add(&z, "doc.xml", "<doc/>");
    zip_add(&z, "_rels/doc.xml.rels", rels);
    read_case(what, &z);
}

static void test_read_invalid(void)
{
    LARGE_INTEGER zero = {{0}};
    IOpcPackage *package;
    IStream *stream;
    struct zip z;
    BYTE junk[100];
    HRESULT hr;
    int i;

    CreateStreamOnHGlobal(NULL, TRUE, &stream);
    hr = IOpcFactory_ReadPackageFromStream(factory, stream, OPC_READ_DEFAULT, &package);
    printf("read empty stream: %#lx\n", hr);
    if (SUCCEEDED(hr)) IOpcPackage_Release(package);
    for (i = 0; i < 100; i++) junk[i] = i * 37 + 11;
    IStream_Write(stream, junk, sizeof(junk), NULL);
    IStream_Seek(stream, zero, STREAM_SEEK_SET, NULL);
    hr = IOpcFactory_ReadPackageFromStream(factory, stream, OPC_READ_DEFAULT, &package);
    printf("read junk: %#lx\n", hr);
    if (SUCCEEDED(hr)) IOpcPackage_Release(package);
    IStream_Release(stream);
    hr = IOpcFactory_ReadPackageFromStream(factory, NULL, OPC_READ_DEFAULT, &package);
    printf("read NULL stream: %#lx\n", hr);

    memset(&z, 0, sizeof(z));
    zip_add(&z, "a.txt", "text");
    read_case("no content types", &z);

    memset(&z, 0, sizeof(z));
    zip_add(&z, "[Content_Types].xml", content_types);
    zip_add(&z, "a.txt", "text");
    read_case("undeclared extension", &z);

    memset(&z, 0, sizeof(z));
    zip_add(&z, "[Content_Types].xml", content_types);
    zip_add(&z, "doc.xml", "<doc/>");
    read_case("no package rels", &z);

    rels_case("absolute target without TargetMode",
            "<Relationship Id=\"rId5\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink\" Target=\"https://example.com/\"/>");
    rels_case("TargetMode External",
            "<Relationship Id=\"rId5\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink\" Target=\"https://example.com/\" TargetMode=\"External\"/>");
    rels_case("TargetMode Internal",
            "<Relationship Id=\"rId5\" Type=\"t/t\" Target=\"doc.xml\" TargetMode=\"Internal\"/>");
    rels_case("TargetMode external (lower case)",
            "<Relationship Id=\"rId5\" Type=\"t/t\" Target=\"https://example.com/\" TargetMode=\"external\"/>");
    rels_case("relative external",
            "<Relationship Id=\"rId5\" Type=\"t/t\" Target=\"../other.docx\" TargetMode=\"External\"/>");
    rels_case("missing target",
            "<Relationship Id=\"rId5\" Type=\"t/t\" Target=\"missing.xml\"/>");
    rels_case("no Id",
            "<Relationship Type=\"t/t\" Target=\"doc.xml\"/>");
    rels_case("duplicate Id",
            "<Relationship Id=\"rId5\" Type=\"t/t\" Target=\"doc.xml\"/><Relationship Id=\"rId5\" Type=\"t/t\" Target=\"doc.xml\"/>");
    rels_case("unknown attribute",
            "<Relationship Id=\"rId5\" Type=\"t/t\" Target=\"doc.xml\" Extra=\"1\"/>");
}

static void test_stream_methods(const char *what, IStream *stream)
{
    ULARGE_INTEGER size, read, written, off, len;
    LARGE_INTEGER zero = {{0}};
    IStream *clone = NULL, *dest;
    STATSTG stat;
    char buf[16];
    ULONG n = 0;
    HRESULT hr;

    IStream_Write(stream, "0123456789", 10, NULL);
    memset(&stat, 0xcc, sizeof(stat));
    hr = IStream_Stat(stream, &stat, STATFLAG_NONAME);
    printf("%s Stat: %#lx type %lu size %llu mode %#lx clsid-null %d state %#lx\n", what, hr, stat.type,
            (unsigned long long)stat.cbSize.QuadPart, stat.grfMode, IsEqualGUID(&stat.clsid, &GUID_NULL), stat.grfStateBits);
    memset(&stat, 0, sizeof(stat));
    hr = IStream_Stat(stream, &stat, STATFLAG_DEFAULT);
    printf("%s Stat default: %#lx name %ls\n", what, hr, stat.pwcsName ? stat.pwcsName : L"(null)");
    CoTaskMemFree(stat.pwcsName);
    size.QuadPart = 4;
    hr = IStream_SetSize(stream, size);
    IStream_Stat(stream, &stat, STATFLAG_NONAME);
    printf("%s SetSize 4: %#lx now %llu\n", what, hr, (unsigned long long)stat.cbSize.QuadPart);
    size.QuadPart = 12;
    hr = IStream_SetSize(stream, size);
    IStream_Stat(stream, &stat, STATFLAG_NONAME);
    printf("%s SetSize 12: %#lx now %llu\n", what, hr, (unsigned long long)stat.cbSize.QuadPart);
    CreateStreamOnHGlobal(NULL, TRUE, &dest);
    IStream_Seek(stream, zero, STREAM_SEEK_SET, NULL);
    size.QuadPart = 6;
    read.QuadPart = written.QuadPart = 99;
    hr = IStream_CopyTo(stream, dest, size, &read, &written);
    printf("%s CopyTo 6: %#lx read %llu written %llu\n", what, hr, (unsigned long long)read.QuadPart, (unsigned long long)written.QuadPart);
    IStream_Release(dest);
    printf("%s Commit: %#lx\n", what, IStream_Commit(stream, STGC_DEFAULT));
    printf("%s Revert: %#lx\n", what, IStream_Revert(stream));
    off.QuadPart = 0; len.QuadPart = 2;
    printf("%s LockRegion: %#lx\n", what, IStream_LockRegion(stream, off, len, LOCK_WRITE));
    printf("%s UnlockRegion: %#lx\n", what, IStream_UnlockRegion(stream, off, len, LOCK_WRITE));
    hr = IStream_Clone(stream, &clone);
    printf("%s Clone: %#lx\n", what, hr);
    if (SUCCEEDED(hr) && clone)
    {
        IStream_Seek(stream, zero, STREAM_SEEK_SET, NULL);
        IStream_Seek(clone, zero, STREAM_SEEK_SET, NULL);
        IStream_Write(stream, "AB", 2, NULL);
        memset(buf, 0, sizeof(buf));
        IStream_Read(clone, buf, 4, &n);
        printf("%s Clone shares data: read %lu \"%.4s\"\n", what, n, buf);
        IStream_Release(clone);
    }
}

int main(void)
{
    static const WCHAR hyperlink[] = L"http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink";
    IOpcRelationshipSet *rels, *doc_rels, *empty_rels;
    IOpcPart *doc, *styles, *part;
    LARGE_INTEGER zero = {{0}};
    IOpcRelationshipEnumerator *e;
    IOpcPartUri *a, *b;
    IOpcPackage *package, *read_back;
    IOpcPartSet *parts;
    IStream *stream;
    IUnknown *unk;
    BYTE *data;
    ULONG size;
    INT32 cmp;
    BOOL next;
    HRESULT hr;
    int count;
    IUri *rel;

    CoInitialize(NULL);
    hr = CoCreateInstance(&CLSID_OpcFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IOpcFactory, (void **)&factory);
    printf("CoCreateInstance OpcFactory: %#lx\n", hr);
    if (FAILED(hr)) return 1;
    hr = IOpcFactory_QueryInterface(factory, &IID_unknown_factory_iface, (void **)&unk);
    printf("QueryInterface {e19c7100-9709-4db7-9373-e7b518b47086}: %#lx\n", hr);
    if (SUCCEEDED(hr)) IUnknown_Release(unk);

    IOpcFactory_CreatePackage(factory, &package);
    IOpcPackage_GetPartSet(package, &parts);
    doc = add_part(parts, L"/word/document.xml",
            L"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml", OPC_COMPRESSION_NORMAL,
            "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"/>");
    styles = add_part(parts, L"/word/styles.xml", L"application/xml", OPC_COMPRESSION_MAXIMUM, "<styles/>");
    part = add_part(parts, L"/word/media/image1.png", L"image/png", OPC_COMPRESSION_NONE, "not a png");
    IOpcPart_Release(part);
    part = add_part(parts, L"/docProps/core.xml", L"application/vnd.openxmlformats-package.core-properties+xml",
            OPC_COMPRESSION_FAST, "<coreProperties/>");
    IOpcPart_Release(part);
    part = add_part(parts, L"/data/a.bin", L"application/octet-stream", OPC_COMPRESSION_SUPERFAST, "aaaa");
    IOpcPart_Release(part);
    part = add_part(parts, L"/data/b.BIN", L"application/octet-stream", OPC_COMPRESSION_NORMAL, "bbbb");
    IOpcPart_Release(part);
    part = add_part(parts, L"/data/c.bin", L"application/x-other", OPC_COMPRESSION_NORMAL, "cccc");
    IOpcPart_Release(part);
    part = add_part(parts, L"/noextension", L"text/plain", OPC_COMPRESSION_NORMAL, "none");
    IOpcPart_Release(part);

    IOpcPackage_GetRelationshipSet(package, &rels);
    add_rel(rels, NULL, L"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument",
            L"/word/document.xml", OPC_URI_TARGET_MODE_INTERNAL);
    add_rel(rels, L"rIdCore", L"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties",
            L"docProps/core.xml", OPC_URI_TARGET_MODE_INTERNAL);
    IOpcPart_GetRelationshipSet(doc, &doc_rels);
    add_rel(doc_rels, L"rId1", L"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image",
            L"media/image1.png", OPC_URI_TARGET_MODE_INTERNAL);
    add_rel(doc_rels, L"rId2", hyperlink, L"https://example.com/", OPC_URI_TARGET_MODE_EXTERNAL);
    add_rel(doc_rels, L"rId3", hyperlink, L"../other.docx", OPC_URI_TARGET_MODE_EXTERNAL);
    add_rel(doc_rels, L"rId4", L"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles",
            L"styles.xml", OPC_URI_TARGET_MODE_INTERNAL);
    add_rel(doc_rels, L"rId5", hyperlink, L"https://example.com/a b?q=1#frag", OPC_URI_TARGET_MODE_EXTERNAL);
    add_rel(doc_rels, L"rId6", L"t/t", L"https://example.com/", OPC_URI_TARGET_MODE_INTERNAL);

    hr = IOpcRelationshipSet_GetRelationshipsContentStream(rels, &stream);
    printf("GetRelationshipsContentStream package: %#lx\n", hr);
    if (SUCCEEDED(hr)) { print_text("package rels", stream); IStream_Release(stream); }
    hr = IOpcRelationshipSet_GetRelationshipsContentStream(doc_rels, &stream);
    printf("GetRelationshipsContentStream document: %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        print_text("document rels", stream);
        printf("document rels stream Write: %#lx\n", IStream_Write(stream, "x", 1, NULL));
        IStream_Release(stream);
    }
    IOpcPart_GetRelationshipSet(styles, &empty_rels);
    hr = IOpcRelationshipSet_GetRelationshipsContentStream(empty_rels, &stream);
    printf("GetRelationshipsContentStream empty: %#lx\n", hr);
    if (SUCCEEDED(hr)) { print_text("empty rels", stream); IStream_Release(stream); }
    printf("GetRelationshipsContentStream NULL: %#lx\n", IOpcRelationshipSet_GetRelationshipsContentStream(doc_rels, NULL));

    CreateStreamOnHGlobal(NULL, TRUE, &stream);
    hr = IOpcFactory_WritePackageToStream(factory, package, OPC_WRITE_FORCE_ZIP32, stream);
    printf("WritePackageToStream ZIP32: %#lx\n", hr);
    data = stream_bytes(stream, &size);
    if (data) print_b64("zip32", data, size);
    free(data);
    IStream_Seek(stream, zero, STREAM_SEEK_SET, NULL);
    hr = IOpcFactory_ReadPackageFromStream(factory, stream, OPC_READ_DEFAULT, &read_back);
    printf("ReadPackageFromStream written package: %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        IOpcPartSet *read_parts;
        IOpcPartUri *uri = part_uri(L"/word/document.xml");
        IOpcRelationshipSet *read_rels;

        list_package(read_back);
        IOpcPackage_GetPartSet(read_back, &read_parts);
        if (SUCCEEDED(IOpcPartSet_GetPart(read_parts, uri, &part)))
        {
            IOpcPart_GetRelationshipSet(part, &read_rels);
            hr = IOpcRelationshipSet_GetRelationshipsContentStream(read_rels, &stream);
            printf("GetRelationshipsContentStream read document: %#lx\n", hr);
            if (SUCCEEDED(hr)) { print_text("read document rels", stream); IStream_Release(stream); }
            IOpcRelationshipSet_Release(read_rels);
            IOpcPart_Release(part);
        }
        IOpcPartUri_Release(uri);
        IOpcPartSet_Release(read_parts);
        IOpcPackage_Release(read_back);
    }
    IStream_Release(stream);
    CreateStreamOnHGlobal(NULL, TRUE, &stream);
    hr = IOpcFactory_WritePackageToStream(factory, package, OPC_WRITE_DEFAULT, stream);
    printf("WritePackageToStream default: %#lx\n", hr);
    data = stream_bytes(stream, &size);
    if (data) print_b64("zipdefault", data, size);
    free(data);
    IStream_Release(stream);

    /* relationship set methods */
    count = 0;
    hr = IOpcRelationshipSet_GetEnumeratorForType(doc_rels, hyperlink, &e);
    if (SUCCEEDED(hr))
    {
        while (SUCCEEDED(IOpcRelationshipEnumerator_MoveNext(e, &next)) && next) count++;
        IOpcRelationshipEnumerator_Release(e);
    }
    printf("GetEnumeratorForType hyperlink: %#lx count %d\n", hr, count);
    count = 0;
    hr = IOpcRelationshipSet_GetEnumeratorForType(doc_rels, L"HTTP://SCHEMAS.OPENXMLFORMATS.ORG/officeDocument/2006/relationships/hyperlink", &e);
    if (SUCCEEDED(hr))
    {
        while (SUCCEEDED(IOpcRelationshipEnumerator_MoveNext(e, &next)) && next) count++;
        IOpcRelationshipEnumerator_Release(e);
    }
    printf("GetEnumeratorForType HYPERLINK (case): %#lx count %d\n", hr, count);
    printf("GetEnumeratorForType NULL type: %#lx\n", IOpcRelationshipSet_GetEnumeratorForType(doc_rels, NULL, &e));
    printf("GetEnumeratorForType NULL out: %#lx\n", IOpcRelationshipSet_GetEnumeratorForType(doc_rels, hyperlink, NULL));
    hr = IOpcRelationshipSet_GetEnumeratorForType(doc_rels, hyperlink, &e);
    if (SUCCEEDED(hr))
    {
        printf("DeleteRelationship rId3 during enumeration: %#lx\n", IOpcRelationshipSet_DeleteRelationship(doc_rels, L"rId3"));
        hr = IOpcRelationshipEnumerator_MoveNext(e, &next);
        printf("  MoveNext after the delete: %#lx\n", hr);
        IOpcRelationshipEnumerator_Release(e);
    }
    printf("DeleteRelationship rId3 again: %#lx\n", IOpcRelationshipSet_DeleteRelationship(doc_rels, L"rId3"));
    printf("DeleteRelationship RID2 (case): %#lx\n", IOpcRelationshipSet_DeleteRelationship(doc_rels, L"RID2"));
    printf("DeleteRelationship NULL: %#lx\n", IOpcRelationshipSet_DeleteRelationship(doc_rels, NULL));
    list_rels("after deletes: ", doc_rels);

    /* part set */
    a = part_uri(L"/data/a.bin");
    printf("DeletePart /data/a.bin: %#lx\n", IOpcPartSet_DeletePart(parts, a));
    printf("DeletePart /data/a.bin again: %#lx\n", IOpcPartSet_DeletePart(parts, a));
    printf("DeletePart NULL: %#lx\n", IOpcPartSet_DeletePart(parts, NULL));
    IOpcPartUri_Release(a);
    b = part_uri(L"/DATA/B.bin");
    printf("DeletePart /DATA/B.bin (case): %#lx\n", IOpcPartSet_DeletePart(parts, b));
    IOpcPartUri_Release(b);

    /* part uris */
    a = part_uri(L"/a/b.xml");
    b = part_uri(L"/A/B.XML");
    cmp = 77;
    printf("ComparePartUri /a/b.xml /A/B.XML: %#lx %d\n", IOpcPartUri_ComparePartUri(a, b, &cmp), cmp);
    IOpcPartUri_Release(b);
    b = part_uri(L"/a/c.xml");
    cmp = 77;
    printf("ComparePartUri /a/b.xml /a/c.xml: %#lx %d\n", IOpcPartUri_ComparePartUri(a, b, &cmp), cmp);
    cmp = 77;
    printf("ComparePartUri /a/c.xml /a/b.xml: %#lx %d\n", IOpcPartUri_ComparePartUri(b, a, &cmp), cmp);
    printf("ComparePartUri NULL: %#lx\n", IOpcPartUri_ComparePartUri(a, NULL, &cmp));
    printf("ComparePartUri NULL result: %#lx\n", IOpcPartUri_ComparePartUri(a, b, NULL));
    IOpcPartUri_Release(b);
    {
        static const WCHAR *targets[] = { L"/a/c/d.xml", L"/e.xml", L"/a/b.xml", L"/a/c.xml", L"/x/y/z.xml", L"/a/_rels/b.xml.rels" };
        unsigned int i;

        for (i = 0; i < ARRAYSIZE(targets); i++)
        {
            BSTR str = NULL;

            b = part_uri(targets[i]);
            rel = NULL;
            hr = IOpcPartUri_GetRelativeUri(a, b, &rel);
            if (rel) { IUri_GetRawUri(rel, &str); IUri_Release(rel); }
            printf("GetRelativeUri /a/b.xml -> %ls: %#lx %ls\n", targets[i], hr, str ? str : L"(null)");
            SysFreeString(str);
            IOpcPartUri_Release(b);
        }
    }
    IOpcPartUri_Release(a);
    {
        IOpcUri *root;
        BSTR str = NULL;

        IOpcFactory_CreatePackageRootUri(factory, &root);
        b = part_uri(L"/a/b.xml");
        rel = NULL;
        hr = IOpcUri_GetRelativeUri(root, b, &rel);
        if (rel) { IUri_GetRawUri(rel, &str); IUri_Release(rel); }
        printf("GetRelativeUri / -> /a/b.xml: %#lx %ls\n", hr, str ? str : L"(null)");
        SysFreeString(str);
        IOpcPartUri_Release(b);
        IOpcUri_Release(root);
    }

    /* streams */
    part = add_part(parts, L"/streams/test.bin", L"application/octet-stream", OPC_COMPRESSION_NORMAL, "");
    if (part && SUCCEEDED(IOpcPart_GetContentStream(part, &stream)))
    {
        test_stream_methods("part stream", stream);
        IStream_Release(stream);
    }
    if (part) IOpcPart_Release(part);
    {
        WCHAR path[MAX_PATH];

        GetTempPathW(MAX_PATH, path);
        wcscat(path, L"opcprobe.tmp");
        hr = IOpcFactory_CreateStreamOnFile(factory, path, OPC_STREAM_IO_WRITE, NULL, 0, &stream);
        printf("CreateStreamOnFile write: %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            test_stream_methods("file stream", stream);
            IStream_Release(stream);
        }
        DeleteFileW(path);
    }

    test_read_invalid();

    IOpcRelationshipSet_Release(empty_rels);
    IOpcRelationshipSet_Release(doc_rels);
    IOpcRelationshipSet_Release(rels);
    IOpcPart_Release(styles);
    IOpcPart_Release(doc);
    IOpcPartSet_Release(parts);
    IOpcPackage_Release(package);
    IOpcFactory_Release(factory);
    CoUninitialize();
    return 0;
}
