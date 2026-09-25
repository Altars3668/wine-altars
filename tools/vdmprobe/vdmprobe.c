/*
 * vdmprobe - what IVirtualDesktopManager answers.
 *
 * Word creates the VirtualDesktopManager of twinapi.dll to ask whether its
 * windows are on the desktop the user is looking at, and Wine had no such
 * class.  This asks each of its three questions about a visible window of its
 * own, a hidden one, a child, the desktop window, a window of another process
 * and handles that are no windows.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <initguid.h>
#include <stdio.h>

DEFINE_GUID(CLSID_VirtualDesktopManager, 0xaa509086, 0x5ca9, 0x4c25, 0x8f, 0x95, 0x58, 0x9d, 0x3c, 0x07, 0xb4, 0x8a);
DEFINE_GUID(IID_IVirtualDesktopManager, 0xa5cd92ff, 0x29be, 0x454c, 0x8d, 0x04, 0xd8, 0x28, 0x79, 0xfb, 0x3f, 0x1b);

typedef struct IVirtualDesktopManager IVirtualDesktopManager;
typedef struct
{
    HRESULT (WINAPI *QueryInterface)(IVirtualDesktopManager *, REFIID, void **);
    ULONG (WINAPI *AddRef)(IVirtualDesktopManager *);
    ULONG (WINAPI *Release)(IVirtualDesktopManager *);
    HRESULT (WINAPI *IsWindowOnCurrentVirtualDesktop)(IVirtualDesktopManager *, HWND, BOOL *);
    HRESULT (WINAPI *GetWindowDesktopId)(IVirtualDesktopManager *, HWND, GUID *);
    HRESULT (WINAPI *MoveWindowToDesktop)(IVirtualDesktopManager *, HWND, REFGUID);
} IVirtualDesktopManagerVtbl;
struct IVirtualDesktopManager { const IVirtualDesktopManagerVtbl *lpVtbl; };

static IVirtualDesktopManager *vdm;

static void ask(const char *what, HWND hwnd, GUID *id)
{
    GUID got;
    BOOL on = 2;
    HRESULT hr;
    WCHAR buf[64];

    hr = vdm->lpVtbl->IsWindowOnCurrentVirtualDesktop(vdm, hwnd, &on);
    printf("%-26s IsWindowOnCurrentVirtualDesktop %#lx %d\n", what, hr, on);
    memset(&got, 0xcc, sizeof(got));
    hr = vdm->lpVtbl->GetWindowDesktopId(vdm, hwnd, &got);
    StringFromGUID2(&got, buf, 64);
    printf("%-26s GetWindowDesktopId %#lx %ls\n", what, hr, buf);
    if (id && SUCCEEDED(hr)) *id = got;
}

int main(void)
{
    static const GUID random = {0x12345678, 0x1234, 0x1234, {1, 2, 3, 4, 5, 6, 7, 8}};
    HWND visible, hidden, child, other;
    GUID current = {0};
    IUnknown *unk;
    HRESULT hr;
    BOOL on;

    setvbuf(stdout, NULL, _IONBF, 0);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    hr = CoCreateInstance(&CLSID_VirtualDesktopManager, NULL, CLSCTX_INPROC_SERVER, &IID_IVirtualDesktopManager,
                          (void **)&vdm);
    printf("CoCreateInstance %#lx\n", hr);
    if (FAILED(hr)) return 1;
    hr = vdm->lpVtbl->QueryInterface(vdm, &IID_IUnknown, (void **)&unk);
    printf("QI IUnknown %#lx\n", hr);
    if (SUCCEEDED(hr)) unk->lpVtbl->Release(unk);
    {
        static const GUID IID_IAgileObject_ = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};
        static const GUID IID_IMarshal_ = {0x00000003, 0, 0, {0xc0, 0, 0, 0, 0, 0, 0, 0x46}};
        hr = vdm->lpVtbl->QueryInterface(vdm, &IID_IAgileObject_, (void **)&unk);
        printf("QI IAgileObject %#lx\n", hr);
        if (SUCCEEDED(hr)) unk->lpVtbl->Release(unk);
        hr = vdm->lpVtbl->QueryInterface(vdm, &IID_IMarshal_, (void **)&unk);
        printf("QI IMarshal %#lx\n", hr);
        if (SUCCEEDED(hr)) unk->lpVtbl->Release(unk);
    }

    visible = CreateWindowA("static", "visible", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
    hidden = CreateWindowA("static", "hidden", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
    child = CreateWindowA("static", "child", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, visible, NULL, NULL, NULL);
    other = FindWindowA("Shell_TrayWnd", NULL);
    Sleep(200);

    ask("visible window", visible, &current);
    ask("hidden window", hidden, NULL);
    ask("child", child, NULL);
    ask("desktop window", GetDesktopWindow(), NULL);
    ask("another process's window", other, NULL);
    ask("NULL", NULL, NULL);
    ask("not a window", (HWND)0xdeadbeef, NULL);
    /* a NULL result pointer brings Windows down: it is not checked */

    hr = vdm->lpVtbl->MoveWindowToDesktop(vdm, visible, &current);
    printf("MoveWindowToDesktop(visible, current) %#lx\n", hr);
    hr = vdm->lpVtbl->MoveWindowToDesktop(vdm, visible, &GUID_NULL);
    printf("MoveWindowToDesktop(visible, GUID_NULL) %#lx\n", hr);
    hr = vdm->lpVtbl->MoveWindowToDesktop(vdm, visible, &random);
    printf("MoveWindowToDesktop(visible, random) %#lx\n", hr);
    hr = vdm->lpVtbl->MoveWindowToDesktop(vdm, hidden, &current);
    printf("MoveWindowToDesktop(hidden, current) %#lx\n", hr);
    hr = vdm->lpVtbl->MoveWindowToDesktop(vdm, child, &current);
    printf("MoveWindowToDesktop(child, current) %#lx\n", hr);
    hr = vdm->lpVtbl->MoveWindowToDesktop(vdm, other, &current);
    printf("MoveWindowToDesktop(another process's, current) %#lx\n", hr);
    hr = vdm->lpVtbl->MoveWindowToDesktop(vdm, NULL, &current);
    printf("MoveWindowToDesktop(NULL, current) %#lx\n", hr);

    ShowWindow(hidden, SW_SHOW);
    Sleep(200);
    ask("the hidden one, shown", hidden, NULL);
    DestroyWindow(child);
    DestroyWindow(visible);
    DestroyWindow(hidden);
    on = 2;
    hr = vdm->lpVtbl->IsWindowOnCurrentVirtualDesktop(vdm, visible, &on);
    printf("a destroyed window: IsWindowOnCurrentVirtualDesktop %#lx %d\n", hr, on);
    vdm->lpVtbl->Release(vdm);
    printf("done\n");
    return 0;
}
