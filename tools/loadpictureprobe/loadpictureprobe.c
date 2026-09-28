/*
 * loadpictureprobe: the errors behind VBScript's LoadPicture -- what OleLoadPicture answers for data that is no
 * picture, OleLoadPictureFile for a text file, a missing file and an empty name, and CreateFileW for the names
 * LoadPicture is given.
 */
#define COBJMACROS
#include <windows.h>
#include <ole2.h>
#include <olectl.h>
#include <stdio.h>

static void try_file(const WCHAR *name)
{
    IDispatch *disp = NULL;
    VARIANT v;
    HANDLE file;
    HRESULT hr;

    file = CreateFileW(name, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
    printf("CreateFileW(%ls): %s, error %lu\n", name, file == INVALID_HANDLE_VALUE ? "fails" : "opens",
           file == INVALID_HANDLE_VALUE ? GetLastError() : 0);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);

    V_VT(&v) = VT_BSTR;
    V_BSTR(&v) = SysAllocString(name);
    hr = OleLoadPictureFile(v, &disp);
    printf("OleLoadPictureFile(%ls): %#lx\n", name, hr);
    if (disp) IDispatch_Release(disp);
    VariantClear(&v);
}

static void try_create(const WCHAR *name)
{
    HANDLE file = CreateFileW(name, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);

    printf("CreateFileW(%ls): %s, error %lu\n", name, file == INVALID_HANDLE_VALUE ? "fails" : "opens",
           file == INVALID_HANDLE_VALUE ? GetLastError() : 0);
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
}

int main(void)
{
    static const char garbage[] = "this is no picture at all, just some text in a stream\r\n";
    WCHAR text[MAX_PATH], dir[MAX_PATH];
    IPicture *pic = NULL;
    IStream *stream;
    HGLOBAL mem;
    HANDLE file;
    DWORD written;
    HRESULT hr;

    OleInitialize(NULL);

    mem = GlobalAlloc(GMEM_MOVEABLE, sizeof(garbage));
    memcpy(GlobalLock(mem), garbage, sizeof(garbage));
    GlobalUnlock(mem);
    CreateStreamOnHGlobal(mem, TRUE, &stream);
    hr = OleLoadPicture(stream, 0, FALSE, &IID_IPicture, (void **)&pic);
    printf("OleLoadPicture(text): %#lx\n", hr);
    if (pic) IPicture_Release(pic);
    IStream_Release(stream);

    GetTempPathW(ARRAY_SIZE(dir), dir);
    swprintf(text, ARRAY_SIZE(text), L"%swa-loadpicture.txt", dir);
    file = CreateFileW(text, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    WriteFile(file, garbage, sizeof(garbage) - 1, &written, NULL);
    CloseHandle(file);

    try_file(text);
    try_file(L"x");
    try_file(L"");
    try_file(L"C:\\nodir\\x.bmp");
    DeleteFileW(text);

    /* names with a colon where a stream would be */
    try_create(L"file:C:\\x.bmp");
    try_create(L"file:x.bmp");
    try_create(L"C:\\nodir:x\\y.bmp");
    try_create(L"C:\\Windows:x\\y.bmp");
    try_create(L"C:\\Windows\\win.ini:nostream");
    try_create(L"C:\\Windows\\nofile.ini:nostream");
    try_create(L"C:\\Windows\\win.ini:bad\\name");
    try_create(L"C:\\Windows\\win.ini::$DATA");

    OleUninitialize();
    return 0;
}
