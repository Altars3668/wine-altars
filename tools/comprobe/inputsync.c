/*
 * What COM does with an outgoing call from a thread that is handling a message another thread sent it, the case
 * input-synchronous calls are for.  The main thread, a single-threaded apartment, makes an object with the
 * interfaces of in-place activation -- IOleInPlaceFrame (with IOleWindow and IOleInPlaceUIWindow), IOleInPlaceSite,
 * IOleInPlaceActiveObject, IOleInPlaceObject, IOleClientSite and IOleCommandTarget -- and gives proxies for them to
 * a second single-threaded apartment, which has a window.  Then the main thread sends that window a message, and
 * while handling it the second thread calls every method of every proxy: those COM delivers are the ones marked
 * [input_sync], the others fail.  GetWindow and ContextSensitiveHelp are called again while handling a posted
 * message, after ReplyMessage in a sent one, and in a message the thread sent itself.  The sender gives up after
 * five seconds, so a call that waits for it shows as a long one rather than hanging.
 */
#define COBJMACROS
#include <windows.h>
#include <ole2.h>
#include <docobj.h>
#include <stdio.h>

static const IID iid_unknown = {0x00000000,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const IID iid_olewindow = {0x00000114,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const IID iid_uiwindow = {0x00000115,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const IID iid_frame = {0x00000116,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const IID iid_activeobject = {0x00000117,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const IID iid_clientsite = {0x00000118,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const IID iid_site = {0x00000119,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const IID iid_object = {0x00000113,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const IID iid_commandtarget = {0xb722bccb,0x4e68,0x101b,{0xa2,0xbc,0x00,0xaa,0x00,0x40,0x47,0x70}};

static HWND target;
static IStream *marshaled;
static IOleInPlaceFrame *frame;
static IOleInPlaceSite *site;
static IOleInPlaceActiveObject *active;
static IOleInPlaceObject *inplace;
static IOleClientSite *client;
static IOleCommandTarget *command;
static HANDLE ready, done;

/* the object, on the main thread: every method does nothing, and answers S_OK */
static struct object
{
    IOleInPlaceFrame IOleInPlaceFrame_iface;
    IOleInPlaceSite IOleInPlaceSite_iface;
    IOleInPlaceActiveObject IOleInPlaceActiveObject_iface;
    IOleInPlaceObject IOleInPlaceObject_iface;
    IOleClientSite IOleClientSite_iface;
    IOleCommandTarget IOleCommandTarget_iface;
} object;

static HRESULT query(REFIID riid, void **obj)
{
    if (IsEqualGUID(riid, &iid_unknown) || IsEqualGUID(riid, &iid_olewindow) || IsEqualGUID(riid, &iid_uiwindow)
            || IsEqualGUID(riid, &iid_frame)) *obj = &object.IOleInPlaceFrame_iface;
    else if (IsEqualGUID(riid, &iid_site)) *obj = &object.IOleInPlaceSite_iface;
    else if (IsEqualGUID(riid, &iid_activeobject)) *obj = &object.IOleInPlaceActiveObject_iface;
    else if (IsEqualGUID(riid, &iid_object)) *obj = &object.IOleInPlaceObject_iface;
    else if (IsEqualGUID(riid, &iid_clientsite)) *obj = &object.IOleClientSite_iface;
    else if (IsEqualGUID(riid, &iid_commandtarget)) *obj = &object.IOleCommandTarget_iface;
    else
    {
        *obj = NULL;
        return E_NOINTERFACE;
    }
    return S_OK;
}

/* how many times the object was called */
static LONG object_calls;

static HRESULT WINAPI object_QueryInterface(void *iface, REFIID riid, void **obj) { return query(riid, obj); }
static ULONG WINAPI object_AddRef(void *iface) { return 2; }
static ULONG WINAPI object_Release(void *iface) { return 1; }
/* for methods whose parameters are all [in] */
static HRESULT WINAPI object_method(void *iface)
{
    InterlockedIncrement(&object_calls);
    return S_OK;
}

static HRESULT WINAPI object_GetWindow(void *iface, HWND *hwnd)
{
    InterlockedIncrement(&object_calls);
    *hwnd = GetDesktopWindow();
    return S_OK;
}

static HRESULT WINAPI object_GetBorder(void *iface, RECT *rect)
{
    InterlockedIncrement(&object_calls);
    SetRectEmpty(rect);
    return S_OK;
}

static HRESULT WINAPI object_InsertMenus(void *iface, HMENU menu, OLEMENUGROUPWIDTHS *widths)
{
    InterlockedIncrement(&object_calls);
    return S_OK;
}

static HRESULT WINAPI object_GetWindowContext(void *iface, IOleInPlaceFrame **frame, IOleInPlaceUIWindow **doc,
                                              RECT *pos, RECT *clip, OLEINPLACEFRAMEINFO *info)
{
    InterlockedIncrement(&object_calls);
    *frame = NULL;
    *doc = NULL;
    SetRectEmpty(pos);
    SetRectEmpty(clip);
    return S_OK;
}

static HRESULT WINAPI object_GetMoniker(void *iface, DWORD assign, DWORD which, IMoniker **moniker)
{
    InterlockedIncrement(&object_calls);
    *moniker = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI object_GetContainer(void *iface, IOleContainer **container)
{
    InterlockedIncrement(&object_calls);
    *container = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI object_QueryStatus(void *iface, const GUID *group, ULONG count, OLECMD *cmds, OLECMDTEXT *text)
{
    InterlockedIncrement(&object_calls);
    return S_OK;
}

static HRESULT WINAPI object_Exec(void *iface, const GUID *group, DWORD id, DWORD opt, VARIANT *in, VARIANT *out)
{
    InterlockedIncrement(&object_calls);
    if (out) VariantInit(out);
    return S_OK;
}

#define M object_method
static void *frame_vtbl[] = { object_QueryInterface, object_AddRef, object_Release, object_GetWindow, M,
                              object_GetBorder, M, M, M, object_InsertMenus, M, M, M, M, M };
static void *site_vtbl[] = { object_QueryInterface, object_AddRef, object_Release, object_GetWindow, M,
                             M, M, M, object_GetWindowContext, M, M, M, M, M, M };
static void *active_vtbl[] = { object_QueryInterface, object_AddRef, object_Release, object_GetWindow, M,
                               M, M, M, M, M };
static void *inplace_vtbl[] = { object_QueryInterface, object_AddRef, object_Release, object_GetWindow, M,
                                M, M, M, M };
static void *client_vtbl[] = { object_QueryInterface, object_AddRef, object_Release, M, object_GetMoniker,
                               object_GetContainer, M, M, M };
static void *command_vtbl[] = { object_QueryInterface, object_AddRef, object_Release, object_QueryStatus,
                                object_Exec };
#undef M

static DWORD start;

static void report(const char *name, HRESULT hr, LONG calls)
{
    if (hr == RPC_E_CANTCALLOUT_ININPUTSYNCCALL) printf("  %-47s refused, RPC_E_CANTCALLOUT_ININPUTSYNCCALL\n", name);
    else printf("  %-47s hr %#lx, %s%s\n", name, hr, calls ? "the object called" : "the object not called",
                GetTickCount() - start > 2000 ? " once the sender gave up" : "");
}

#define CALL(name, call) do { HRESULT hr_; LONG calls_ = object_calls; start = GetTickCount(); hr_ = (call); \
                              report(name, hr_, object_calls - calls_); } while (0)

static void call_everything(void)
{
    static const BORDERWIDTHS widths = { 0 };
    OLEMENUGROUPWIDTHS menu_widths = { { 0 } };
    OLEINPLACEFRAMEINFO info = { sizeof(info) };
    IOleInPlaceUIWindow *doc;
    IOleInPlaceFrame *frame2;
    IOleContainer *container;
    IMoniker *moniker;
    OLECMD cmd = { 1, 0 };
    RECT rect = { 0, 0, 10, 10 }, clip;
    SIZE size = { 1, 1 };
    MSG msg = { 0 };
    HWND hwnd;

    printf(" while handling a message another thread sent (InSendMessageEx %#lx):\n", InSendMessageEx(NULL));
    CALL("IOleWindow::GetWindow", IOleInPlaceFrame_GetWindow(frame, &hwnd));
    CALL("IOleWindow::ContextSensitiveHelp", IOleInPlaceFrame_ContextSensitiveHelp(frame, FALSE));
    CALL("IOleInPlaceUIWindow::GetBorder", IOleInPlaceFrame_GetBorder(frame, &rect));
    CALL("IOleInPlaceUIWindow::RequestBorderSpace", IOleInPlaceFrame_RequestBorderSpace(frame, &widths));
    CALL("IOleInPlaceUIWindow::SetBorderSpace", IOleInPlaceFrame_SetBorderSpace(frame, &widths));
    CALL("IOleInPlaceUIWindow::SetActiveObject", IOleInPlaceFrame_SetActiveObject(frame, NULL, NULL));
    CALL("IOleInPlaceFrame::InsertMenus", IOleInPlaceFrame_InsertMenus(frame, NULL, &menu_widths));
    CALL("IOleInPlaceFrame::SetMenu", IOleInPlaceFrame_SetMenu(frame, NULL, NULL, NULL));
    CALL("IOleInPlaceFrame::RemoveMenus", IOleInPlaceFrame_RemoveMenus(frame, NULL));
    CALL("IOleInPlaceFrame::SetStatusText", IOleInPlaceFrame_SetStatusText(frame, L"status"));
    CALL("IOleInPlaceFrame::EnableModeless", IOleInPlaceFrame_EnableModeless(frame, TRUE));
    CALL("IOleInPlaceFrame::TranslateAccelerator", IOleInPlaceFrame_TranslateAccelerator(frame, &msg, 0));
    CALL("IOleInPlaceSite::GetWindow", IOleInPlaceSite_GetWindow(site, &hwnd));
    CALL("IOleInPlaceSite::CanInPlaceActivate", IOleInPlaceSite_CanInPlaceActivate(site));
    CALL("IOleInPlaceSite::OnInPlaceActivate", IOleInPlaceSite_OnInPlaceActivate(site));
    CALL("IOleInPlaceSite::OnUIActivate", IOleInPlaceSite_OnUIActivate(site));
    CALL("IOleInPlaceSite::GetWindowContext",
         IOleInPlaceSite_GetWindowContext(site, &frame2, &doc, &rect, &clip, &info));
    CALL("IOleInPlaceSite::Scroll", IOleInPlaceSite_Scroll(site, size));
    CALL("IOleInPlaceSite::OnUIDeactivate", IOleInPlaceSite_OnUIDeactivate(site, FALSE));
    CALL("IOleInPlaceSite::OnInPlaceDeactivate", IOleInPlaceSite_OnInPlaceDeactivate(site));
    CALL("IOleInPlaceSite::DiscardUndoState", IOleInPlaceSite_DiscardUndoState(site));
    CALL("IOleInPlaceSite::DeactivateAndUndo", IOleInPlaceSite_DeactivateAndUndo(site));
    CALL("IOleInPlaceSite::OnPosRectChange", IOleInPlaceSite_OnPosRectChange(site, &rect));
    CALL("IOleInPlaceActiveObject::TranslateAccelerator", IOleInPlaceActiveObject_TranslateAccelerator(active, &msg));
    CALL("IOleInPlaceActiveObject::OnFrameWindowActivate",
         IOleInPlaceActiveObject_OnFrameWindowActivate(active, TRUE));
    CALL("IOleInPlaceActiveObject::OnDocWindowActivate", IOleInPlaceActiveObject_OnDocWindowActivate(active, TRUE));
    CALL("IOleInPlaceActiveObject::ResizeBorder", IOleInPlaceActiveObject_ResizeBorder(active, &rect, NULL, TRUE));
    CALL("IOleInPlaceActiveObject::EnableModeless", IOleInPlaceActiveObject_EnableModeless(active, TRUE));
    CALL("IOleInPlaceObject::InPlaceDeactivate", IOleInPlaceObject_InPlaceDeactivate(inplace));
    CALL("IOleInPlaceObject::UIDeactivate", IOleInPlaceObject_UIDeactivate(inplace));
    CALL("IOleInPlaceObject::SetObjectRects", IOleInPlaceObject_SetObjectRects(inplace, &rect, &rect));
    CALL("IOleInPlaceObject::ReactivateAndUndo", IOleInPlaceObject_ReactivateAndUndo(inplace));
    CALL("IOleClientSite::SaveObject", IOleClientSite_SaveObject(client));
    CALL("IOleClientSite::GetMoniker", IOleClientSite_GetMoniker(client, OLEGETMONIKER_ONLYIFTHERE,
                                                                 OLEWHICHMK_CONTAINER, &moniker));
    CALL("IOleClientSite::GetContainer", IOleClientSite_GetContainer(client, &container));
    CALL("IOleClientSite::ShowObject", IOleClientSite_ShowObject(client));
    CALL("IOleClientSite::OnShowWindow", IOleClientSite_OnShowWindow(client, TRUE));
    CALL("IOleClientSite::RequestNewObjectLayout", IOleClientSite_RequestNewObjectLayout(client));
    CALL("IOleCommandTarget::QueryStatus", IOleCommandTarget_QueryStatus(command, NULL, 1, &cmd, NULL));
    CALL("IOleCommandTarget::Exec", IOleCommandTarget_Exec(command, NULL, 1, 0, NULL, NULL));
}

static void call_proxy(const char *where)
{
    DWORD flags = InSendMessageEx(NULL);
    HRESULT hr;
    HWND hwnd = NULL;

    start = GetTickCount();
    hr = IOleInPlaceFrame_GetWindow(frame, &hwnd);
    printf(" %s (InSendMessageEx %#lx): GetWindow hr %#lx%s, %s\n", where, flags, hr,
           hr == S_OK && hwnd == GetDesktopWindow() ? " desktop window" : "",
           GetTickCount() - start > 2000 ? "waited for the sender" : "at once");
    start = GetTickCount();
    hr = IOleInPlaceFrame_ContextSensitiveHelp(frame, FALSE);
    printf(" %s: ContextSensitiveHelp hr %#lx, %s\n", where, hr,
           GetTickCount() - start > 2000 ? "waited for the sender" : "at once");
    if (!InSendMessageEx(NULL))
    {
        MSG msg = { 0 };
        LONG calls = object_calls;

        hr = IOleInPlaceActiveObject_TranslateAccelerator(active, &msg);
        printf(" %s: IOleInPlaceActiveObject::TranslateAccelerator hr %#lx, %s\n", where, hr,
               object_calls - calls ? "the object called" : "the object not called");
    }
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg)
    {
    case WM_APP:
        call_everything();
        return 1;
    case WM_APP + 1:
        call_proxy("posted");
        SetEvent(done);
        return 0;
    case WM_APP + 2:
        ReplyMessage(1);
        call_proxy("sent by another thread, after ReplyMessage");
        SetEvent(done);
        return 1;
    case WM_APP + 3:
        SendMessageW(hwnd, WM_APP + 4, 0, 0);
        SetEvent(done);
        return 0;
    case WM_APP + 4:
        call_proxy("sent by the thread itself");
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static DWORD WINAPI thread_proc(void *arg)
{
    WNDCLASSW cls = { 0 };
    IUnknown *unk;
    HRESULT hr;
    MSG msg;

    CoInitialize(NULL);
    cls.lpfnWndProc = window_proc;
    cls.lpszClassName = L"inputsync probe";
    RegisterClassW(&cls);
    target = CreateWindowW(L"inputsync probe", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, NULL, NULL);
    hr = CoGetInterfaceAndReleaseStream(marshaled, &iid_unknown, (void **)&unk);
    printf("proxy: hr %#lx\n", hr);
    IUnknown_QueryInterface(unk, &iid_frame, (void **)&frame);
    IUnknown_QueryInterface(unk, &iid_site, (void **)&site);
    IUnknown_QueryInterface(unk, &iid_activeobject, (void **)&active);
    IUnknown_QueryInterface(unk, &iid_object, (void **)&inplace);
    IUnknown_QueryInterface(unk, &iid_clientsite, (void **)&client);
    IUnknown_QueryInterface(unk, &iid_commandtarget, (void **)&command);
    printf("interfaces: %s\n", frame && site && active && inplace && client && command ? "all" : "missing some");
    SetEvent(ready);
    while (GetMessageW(&msg, NULL, 0, 0)) DispatchMessageW(&msg);
    IUnknown_Release(unk);
    CoUninitialize();
    return 0;
}

/* waits, taking the calls the other thread makes meanwhile */
static void wait_for(HANDLE event)
{
    DWORD index;

    CoWaitForMultipleHandles(0, 20000, 1, &event, &index);
}

int main(void)
{
    DWORD_PTR result;
    HANDLE thread;
    DWORD begin;
    LRESULT ret;

    setvbuf(stdout, NULL, _IONBF, 0);
    CoInitialize(NULL);
    object.IOleInPlaceFrame_iface.lpVtbl = (void *)frame_vtbl;
    object.IOleInPlaceSite_iface.lpVtbl = (void *)site_vtbl;
    object.IOleInPlaceActiveObject_iface.lpVtbl = (void *)active_vtbl;
    object.IOleInPlaceObject_iface.lpVtbl = (void *)inplace_vtbl;
    object.IOleClientSite_iface.lpVtbl = (void *)client_vtbl;
    object.IOleCommandTarget_iface.lpVtbl = (void *)command_vtbl;
    ready = CreateEventW(NULL, FALSE, FALSE, NULL);
    done = CreateEventW(NULL, FALSE, FALSE, NULL);
    CoMarshalInterThreadInterfaceInStream(&iid_unknown, (IUnknown *)&object.IOleInPlaceFrame_iface, &marshaled);
    thread = CreateThread(NULL, 0, thread_proc, NULL, 0, NULL);
    wait_for(ready);

    begin = GetTickCount();
    ret = SendMessageTimeoutW(target, WM_APP, 0, 0, SMTO_NORMAL, 5000, &result);
    printf("sent: returned %Id, result %Id, after %s\n", ret, result, GetTickCount() - begin > 2000 ? "long" : "short");

    PostMessageW(target, WM_APP + 1, 0, 0);
    wait_for(done);

    ret = SendMessageTimeoutW(target, WM_APP + 2, 0, 0, SMTO_NORMAL, 5000, &result);
    printf("sent, replied early: returned %Id, result %Id\n", ret, result);
    wait_for(done);

    PostMessageW(target, WM_APP + 3, 0, 0);
    wait_for(done);

    PostMessageW(target, WM_QUIT, 0, 0);
    wait_for(thread);
    CoUninitialize();
    printf("done\n");
    return 0;
}
