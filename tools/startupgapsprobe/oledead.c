/* oledead: RevokeDragDrop on a window that is gone, and what happens to the drop target's reference; what
 * MXXMLWriter60 answers for an interface Office asks for.  Own window and objects only. */
#define COBJMACROS
#include <windows.h>
#include <ole2.h>
#include <initguid.h>
#include <msxml6.h>
#include <stdio.h>

static LONG refs = 1;
static HRESULT WINAPI dt_qi(IDropTarget *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IDropTarget)) { *out = iface; InterlockedIncrement(&refs); return S_OK; }
    *out = NULL; return E_NOINTERFACE;
}
static ULONG WINAPI dt_addref(IDropTarget *iface) { return InterlockedIncrement(&refs); }
static ULONG WINAPI dt_release(IDropTarget *iface) { return InterlockedDecrement(&refs); }
static HRESULT WINAPI dt_enter(IDropTarget *iface, IDataObject *o, DWORD k, POINTL p, DWORD *e) { return S_OK; }
static HRESULT WINAPI dt_over(IDropTarget *iface, DWORD k, POINTL p, DWORD *e) { return S_OK; }
static HRESULT WINAPI dt_leave(IDropTarget *iface) { return S_OK; }
static HRESULT WINAPI dt_drop(IDropTarget *iface, IDataObject *o, DWORD k, POINTL p, DWORD *e) { return S_OK; }
static IDropTargetVtbl dt_vtbl = { dt_qi, dt_addref, dt_release, dt_enter, dt_over, dt_leave, dt_drop };
static IDropTarget target = { &dt_vtbl };

DEFINE_GUID(IID_unknown_office, 0xe19c7100, 0x9709, 0x4db7, 0x93, 0x73, 0xe7, 0xb5, 0x18, 0xb4, 0x70, 0x86);

int main(void)
{
    HWND hwnd, other;
    HRESULT hr;
    IUnknown *writer, *reader, *doc;
    void *out;

    setvbuf(stdout, NULL, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    printf("OleInitialize %#lx\n", OleInitialize(NULL));
    hwnd = CreateWindowA("static", "oledead", WS_POPUP, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
    other = CreateWindowA("static", "oledead2", WS_POPUP, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
    hr = RegisterDragDrop(hwnd, &target);
    printf("register %#lx, target refs %ld\n", hr, refs);
    DestroyWindow(hwnd);
    printf("window destroyed, target refs %ld\n", refs);
    hr = RevokeDragDrop(hwnd);
    printf("revoke on the destroyed window %#lx, target refs %ld\n", hr, refs);
    hr = RevokeDragDrop(hwnd);
    printf("again %#lx, target refs %ld\n", hr, refs);
    hr = RegisterDragDrop(hwnd, &target);
    printf("register on the destroyed window %#lx, target refs %ld\n", hr, refs);
    printf("revoke NULL %#lx\n", RevokeDragDrop(NULL));
    printf("revoke a window never registered %#lx\n", RevokeDragDrop(other));
    hr = RegisterDragDrop(other, &target);
    printf("register other %#lx refs %ld\n", hr, refs);
    hr = RevokeDragDrop(other);
    printf("revoke other %#lx refs %ld\n", hr, refs);
    DestroyWindow(other);

    hr = CoCreateInstance(&CLSID_MXXMLWriter60, NULL, CLSCTX_INPROC_SERVER, &IID_IUnknown, (void **)&writer);
    printf("MXXMLWriter60 %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IUnknown_QueryInterface(writer, &IID_unknown_office, &out);
        printf("  {e19c7100-9709-4db7-9373-e7b518b47086}: %#lx\n", hr);
        if (SUCCEEDED(hr)) IUnknown_Release((IUnknown *)out);
        IUnknown_Release(writer);
    }
    hr = CoCreateInstance(&CLSID_SAXXMLReader60, NULL, CLSCTX_INPROC_SERVER, &IID_IUnknown, (void **)&reader);
    if (SUCCEEDED(hr))
    {
        printf("SAXXMLReader60 {e19c7100...}: %#lx\n", IUnknown_QueryInterface(reader, &IID_unknown_office, &out));
        IUnknown_Release(reader);
    }
    hr = CoCreateInstance(&CLSID_DOMDocument60, NULL, CLSCTX_INPROC_SERVER, &IID_IUnknown, (void **)&doc);
    if (SUCCEEDED(hr))
    {
        printf("DOMDocument60 {e19c7100...}: %#lx\n", IUnknown_QueryInterface(doc, &IID_unknown_office, &out));
        IUnknown_Release(doc);
    }
    OleUninitialize();
    printf("after OleUninitialize, target refs %ld\n", refs);
    return 0;
}
