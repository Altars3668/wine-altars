/*
 * What OLE's default handler answers QueryInterface with, for the interfaces it has of its own and for those only
 * the object has: before the object runs, while it runs (OleRun starts the local server of the class the ProgID
 * names, by default Excel.Sheet.12), and after IOleObject::Close.  Run it where a local server may start, the
 * desktop session.
 */
#define COBJMACROS
#include <windows.h>
#include <ole2.h>
#include <stdio.h>

static const struct { const char *name; IID iid; } interfaces[] =
{
    { "IUnknown", {0x00000000,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}} },
    { "IOleObject", {0x00000112,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}} },
    { "IDataObject", {0x0000010e,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}} },
    { "IPersistStorage", {0x0000010a,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}} },
    { "IRunnableObject", {0x00000126,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}} },
    { "IViewObject2", {0x00000127,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}} },
    { "IOleCache2", {0x00000128,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}} },
    { "IOleCacheControl", {0x00000129,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}} },
    { "IOleWindow", {0x00000114,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}} },
    { "IOleInPlaceObject", {0x00000113,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}} },
    { "IOleLink", {0x0000011d,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}} },
    { "IDispatch", {0x00020400,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}} },
    { "IPersistFile", {0x0000010b,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}} },
    { "an interface no one has", {0x3f2d8a1e,0x55c1,0x4a7e,{0x9b,0x0e,0x61,0x2c,0x7d,0x13,0xa4,0x5f}} },
};

static void query_all(IUnknown *handler, const char *when)
{
    IUnknown *unk, *identity;
    unsigned int i;
    HRESULT hr;

    printf("%s:\n", when);
    for (i = 0; i < ARRAY_SIZE(interfaces); i++)
    {
        unk = (IUnknown *)0xdeadbeef;
        hr = IUnknown_QueryInterface(handler, &interfaces[i].iid, (void **)&unk);
        if (SUCCEEDED(hr))
        {
            IUnknown_QueryInterface(unk, &interfaces[0].iid, (void **)&identity);
            printf("  %-24s hr %#lx, %s\n", interfaces[i].name, hr,
                   identity == handler ? "the handler's identity" : "another identity");
            IUnknown_Release(identity);
            IUnknown_Release(unk);
        }
        else printf("  %-24s hr %#lx, %s\n", interfaces[i].name, hr, unk ? "pointer left" : "pointer NULL");
    }
}

int main(int argc, char **argv)
{
    const char *progid = argc > 1 ? argv[1] : "Excel.Sheet.12";
    WCHAR progidW[64];
    IPersistStorage *persist;
    IOleObject *object;
    IStorage *storage;
    ILockBytes *bytes;
    IUnknown *handler, *outer;
    CLSID clsid;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    OleInitialize(NULL);
    MultiByteToWideChar(CP_ACP, 0, progid, -1, progidW, ARRAY_SIZE(progidW));
    hr = CLSIDFromProgID(progidW, &clsid);
    printf("CLSIDFromProgID(%s): hr %#lx\n", progid, hr);
    if (FAILED(hr)) return 1;

    hr = OleCreateDefaultHandler(&clsid, NULL, &interfaces[0].iid, (void **)&handler);
    printf("OleCreateDefaultHandler: hr %#lx\n", hr);
    if (FAILED(hr)) return 1;
    IUnknown_QueryInterface(handler, &interfaces[0].iid, (void **)&outer);
    IUnknown_Release(outer);
    query_all(handler, "not running");

    CreateILockBytesOnHGlobal(NULL, TRUE, &bytes);
    StgCreateDocfileOnILockBytes(bytes, STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, &storage);
    IUnknown_QueryInterface(handler, &interfaces[3].iid, (void **)&persist);
    hr = IPersistStorage_InitNew(persist, storage);
    printf("IPersistStorage::InitNew: hr %#lx\n", hr);
    IPersistStorage_Release(persist);

    hr = OleRun(handler);
    printf("OleRun: hr %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        query_all(handler, "running");
        IUnknown_QueryInterface(handler, &interfaces[1].iid, (void **)&object);
        hr = IOleObject_Close(object, OLECLOSE_NOSAVE);
        printf("IOleObject::Close: hr %#lx\n", hr);
        IOleObject_Release(object);
        query_all(handler, "closed");
    }

    IUnknown_Release(handler);
    IStorage_Release(storage);
    ILockBytes_Release(bytes);
    OleUninitialize();
    printf("done\n");
    return 0;
}
