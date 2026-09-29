/* oleclipprobe: what OleGetClipboard() offers for data a program put on the clipboard without OLE.
 *
 * Puts every standard clipboard format, and a few registered ones, on the clipboard with SetClipboardData(),
 * then prints what the IDataObject from OleGetClipboard() enumerates for each format (the TYMED it offers)
 * and what GetData() answers for each medium.  It replaces the clipboard of the window station it runs in:
 * run it where that does not matter (an SSH session has its own).
 */
#define COBJMACROS
#include <windows.h>
#include <ole2.h>
#include <shlobj.h>
#include <stdio.h>

static const char *format_name(UINT cf, char *buf, size_t len)
{
    static const char *names[] =
    {
        NULL, "CF_TEXT", "CF_BITMAP", "CF_METAFILEPICT", "CF_SYLK", "CF_DIF", "CF_TIFF", "CF_OEMTEXT", "CF_DIB",
        "CF_PALETTE", "CF_PENDATA", "CF_RIFF", "CF_WAVE", "CF_UNICODETEXT", "CF_ENHMETAFILE", "CF_HDROP",
        "CF_LOCALE", "CF_DIBV5",
    };
    char name[100];

    if (cf < ARRAYSIZE(names) && names[cf]) return names[cf];
    switch (cf)
    {
    case CF_OWNERDISPLAY: return "CF_OWNERDISPLAY";
    case CF_DSPTEXT: return "CF_DSPTEXT";
    case CF_DSPBITMAP: return "CF_DSPBITMAP";
    case CF_DSPMETAFILEPICT: return "CF_DSPMETAFILEPICT";
    case CF_DSPENHMETAFILE: return "CF_DSPENHMETAFILE";
    }
    if (GetClipboardFormatNameA(cf, name, sizeof(name))) snprintf(buf, len, "\"%s\"", name);
    else snprintf(buf, len, "%04x", cf);
    return buf;
}

static void tymed_str(DWORD tymed, char *buf, size_t len)
{
    static const struct { DWORD flag; const char *name; } flags[] =
    {
        { TYMED_HGLOBAL, "HGLOBAL" }, { TYMED_FILE, "FILE" }, { TYMED_ISTREAM, "ISTREAM" },
        { TYMED_ISTORAGE, "ISTORAGE" }, { TYMED_GDI, "GDI" }, { TYMED_MFPICT, "MFPICT" }, { TYMED_ENHMF, "ENHMF" },
    };
    size_t i, pos = 0;

    buf[0] = 0;
    if (!tymed) { snprintf(buf, len, "NULL"); return; }
    for (i = 0; i < ARRAYSIZE(flags); i++)
        if (tymed & flags[i].flag)
            pos += snprintf(buf + pos, len - pos, "%s%s", pos ? "|" : "", flags[i].name);
    if (tymed & ~0x7f) snprintf(buf + pos, len - pos, "%s%#lx", pos ? "|" : "", tymed & ~0x7fUL);
}

static HGLOBAL global_bytes(const void *data, SIZE_T size)
{
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, size);
    memcpy(GlobalLock(h), data, size);
    GlobalUnlock(h);
    return h;
}

static HGLOBAL global_dib(void)
{
    struct { BITMAPINFOHEADER header; DWORD pixel; } dib;

    memset(&dib, 0, sizeof(dib));
    dib.header.biSize = sizeof(dib.header);
    dib.header.biWidth = dib.header.biHeight = dib.header.biPlanes = 1;
    dib.header.biBitCount = 32;
    dib.header.biCompression = BI_RGB;
    dib.pixel = 0x00ff8000;
    return global_bytes(&dib, sizeof(dib));
}

static HENHMETAFILE make_emf(void)
{
    HDC dc = CreateEnhMetaFileA(NULL, NULL, NULL, NULL);
    Rectangle(dc, 0, 0, 10, 10);
    return CloseEnhMetaFile(dc);
}

static HGLOBAL global_metafilepict(void)
{
    METAFILEPICT mfp = { MM_ANISOTROPIC, 100, 100, NULL };
    HDC dc = CreateMetaFileA(NULL);
    Rectangle(dc, 0, 0, 10, 10);
    mfp.hMF = CloseMetaFile(dc);
    return global_bytes(&mfp, sizeof(mfp));
}

static HGLOBAL global_hdrop(void)
{
    static const WCHAR file[] = L"C:\\Windows\\win.ini\0";
    char buf[sizeof(DROPFILES) + sizeof(file)];
    DROPFILES *drop = (DROPFILES *)buf;

    memset(buf, 0, sizeof(buf));
    drop->pFiles = sizeof(DROPFILES);
    drop->fWide = TRUE;
    memcpy(buf + sizeof(DROPFILES), file, sizeof(file));
    return global_bytes(buf, sizeof(buf));
}

static HPALETTE make_palette(void)
{
    struct { WORD version, count; PALETTEENTRY entries[2]; } pal = { 0x300, 2, {{ 0, 0, 0, 0 }, { 255, 255, 255, 0 }} };
    return CreatePalette((LOGPALETTE *)&pal);
}

static void put_formats(void)
{
    static const char bytes[] = "oleclipprobe bytes";
    static const char text[] = "oleclipprobe text";
    OBJECTDESCRIPTOR od;
    HDC dc;

    memset(&od, 0, sizeof(od));
    od.cbSize = sizeof(od);
    if (!OpenClipboard(NULL)) { printf("OpenClipboard failed %lu\n", GetLastError()); return; }
    EmptyClipboard();
    SetClipboardData(CF_TEXT, global_bytes(text, sizeof(text)));
    SetClipboardData(CF_DIB, global_dib());
    SetClipboardData(CF_SYLK, global_bytes(bytes, sizeof(bytes)));
    SetClipboardData(CF_DIF, global_bytes(bytes, sizeof(bytes)));
    SetClipboardData(CF_TIFF, global_bytes(bytes, sizeof(bytes)));
    SetClipboardData(CF_PENDATA, global_bytes(bytes, sizeof(bytes)));
    SetClipboardData(CF_RIFF, global_bytes(bytes, sizeof(bytes)));
    SetClipboardData(CF_WAVE, global_bytes(bytes, sizeof(bytes)));
    SetClipboardData(CF_HDROP, global_hdrop());
    SetClipboardData(CF_ENHMETAFILE, make_emf());
    SetClipboardData(CF_PALETTE, make_palette());
    SetClipboardData(CF_DSPTEXT, global_bytes(text, sizeof(text)));
    dc = GetDC(NULL);
    SetClipboardData(CF_DSPBITMAP, CreateCompatibleBitmap(dc, 2, 2));
    ReleaseDC(NULL, dc);
    SetClipboardData(CF_DSPMETAFILEPICT, global_metafilepict());
    SetClipboardData(CF_DSPENHMETAFILE, make_emf());
    SetClipboardData(RegisterClipboardFormatA("Embed Source"), global_bytes(bytes, sizeof(bytes)));
    SetClipboardData(RegisterClipboardFormatA("Object Descriptor"), global_bytes(&od, sizeof(od)));
    SetClipboardData(RegisterClipboardFormatA("Link Source"), global_bytes(bytes, sizeof(bytes)));
    SetClipboardData(RegisterClipboardFormatA("Rich Text Format"), global_bytes(text, sizeof(text)));
    SetClipboardData(RegisterClipboardFormatA("oleclipprobe private"), global_bytes(bytes, sizeof(bytes)));
    CloseClipboard();
}

int main(void)
{
    static const DWORD media[] = { TYMED_HGLOBAL, TYMED_ISTREAM, TYMED_ISTORAGE, TYMED_GDI, TYMED_MFPICT, TYMED_ENHMF };
    IEnumFORMATETC *enum_fmt;
    IDataObject *data;
    FORMATETC fmt;
    HRESULT hr;
    UINT cf;
    char buf[128], tymed[64];
    unsigned int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    hr = OleInitialize(NULL);
    printf("OleInitialize %#lx\n", hr);
    put_formats();

    printf("clipboard formats:");
    if (OpenClipboard(NULL))
    {
        for (cf = EnumClipboardFormats(0); cf; cf = EnumClipboardFormats(cf)) printf(" %s", format_name(cf, buf, sizeof(buf)));
        CloseClipboard();
    }
    printf("\n");

    hr = OleGetClipboard(&data);
    printf("OleGetClipboard %#lx\n", hr);
    if (FAILED(hr)) return 1;
    hr = IDataObject_EnumFormatEtc(data, DATADIR_GET, &enum_fmt);
    printf("EnumFormatEtc %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        while (IEnumFORMATETC_Next(enum_fmt, 1, &fmt, NULL) == S_OK)
        {
            tymed_str(fmt.tymed, tymed, sizeof(tymed));
            printf("%s: tymed %s aspect %lu lindex %ld ptd %s\n", format_name(fmt.cfFormat, buf, sizeof(buf)), tymed,
                   fmt.dwAspect, fmt.lindex, fmt.ptd ? "set" : "NULL");
            if (fmt.ptd) CoTaskMemFree(fmt.ptd);
            for (i = 0; i < ARRAYSIZE(media); i++)
            {
                FORMATETC want = { fmt.cfFormat, NULL, DVASPECT_CONTENT, -1, media[i] };
                STGMEDIUM med = { 0 };

                hr = IDataObject_GetData(data, &want, &med);
                tymed_str(media[i], tymed, sizeof(tymed));
                printf("    GetData %s: %#lx", tymed, hr);
                if (SUCCEEDED(hr))
                {
                    tymed_str(med.tymed, tymed, sizeof(tymed));
                    printf(" got %s", tymed);
                    ReleaseStgMedium(&med);
                }
                printf("\n");
            }
        }
        IEnumFORMATETC_Release(enum_fmt);
    }
    IDataObject_Release(data);
    OleUninitialize();
    return 0;
}
