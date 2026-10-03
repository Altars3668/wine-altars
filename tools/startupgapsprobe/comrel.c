/* comrel: what CoReleaseMarshalData answers for marshal data of an object CoDisconnectObject has disconnected -- what
 * Word's exit does when it disconnects its objects and then revokes their running object table entries, its drop
 * targets and its global interface table entries.  For each kind of marshal (normal, table strong, table weak, of
 * IUnknown and of another interface), for data whose apartment has gone, and for those three revocations, it prints
 * the HRESULTs and the object's reference count after each step; and what the global interface table gives the
 * apartment that registered an interface and another one, before and after a disconnect.  Only objects of its own,
 * a running object table entry under an item moniker of its own, revoked again.  Prints results only. */
#define COBJMACROS
#include <windows.h>
#include <ole2.h>
#include <stdio.h>

struct object
{
    IDropTarget IDropTarget_iface;
    LONG refs;
};

static inline struct object *impl(IDropTarget *iface)
{
    return CONTAINING_RECORD(iface, struct object, IDropTarget_iface);
}

static HRESULT WINAPI object_QueryInterface(IDropTarget *iface, REFIID iid, void **out)
{
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IDropTarget))
    {
        *out = iface;
        IDropTarget_AddRef(iface);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI object_AddRef(IDropTarget *iface)
{
    return InterlockedIncrement(&impl(iface)->refs);
}

static ULONG WINAPI object_Release(IDropTarget *iface)
{
    return InterlockedDecrement(&impl(iface)->refs);
}

static HRESULT WINAPI object_DragEnter(IDropTarget *iface, IDataObject *data, DWORD keys, POINTL pt, DWORD *effect)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI object_DragOver(IDropTarget *iface, DWORD keys, POINTL pt, DWORD *effect)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI object_DragLeave(IDropTarget *iface)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI object_Drop(IDropTarget *iface, IDataObject *data, DWORD keys, POINTL pt, DWORD *effect)
{
    return E_NOTIMPL;
}

static const IDropTargetVtbl object_vtbl =
{
    object_QueryInterface, object_AddRef, object_Release,
    object_DragEnter, object_DragOver, object_DragLeave, object_Drop
};

static void init(struct object *obj)
{
    obj->IDropTarget_iface.lpVtbl = (IDropTargetVtbl *)&object_vtbl;
    obj->refs = 1;
}

static void marshal_kind(const char *what, REFIID iid, MSHLFLAGS flags, BOOL disconnect, int releases)
{
    struct object obj;
    LARGE_INTEGER zero = {{0}};
    IStream *stream;
    HRESULT hr;
    int i;

    init(&obj);
    CreateStreamOnHGlobal(NULL, TRUE, &stream);
    hr = CoMarshalInterface(stream, iid, (IUnknown *)&obj.IDropTarget_iface, MSHCTX_INPROC, NULL, flags);
    printf("%s: marshal %#lx refs %ld", what, hr, obj.refs);
    if (disconnect)
    {
        hr = CoDisconnectObject((IUnknown *)&obj.IDropTarget_iface, 0);
        printf(", disconnect %#lx refs %ld", hr, obj.refs);
    }
    for (i = 0; i < releases; i++)
    {
        IStream_Seek(stream, zero, STREAM_SEEK_SET, NULL);
        hr = CoReleaseMarshalData(stream);
        printf(", release %#lx refs %ld", hr, obj.refs);
    }
    if (!disconnect)
    {
        hr = CoDisconnectObject((IUnknown *)&obj.IDropTarget_iface, 0);
        printf(", then disconnect %#lx refs %ld", hr, obj.refs);
    }
    printf("\n");
    IStream_Release(stream);
}

/* table strong data unmarshaled after the object was disconnected */
static void unmarshal_disconnected(void)
{
    struct object obj;
    LARGE_INTEGER zero = {{0}};
    IStream *stream;
    IUnknown *unk = NULL;
    HRESULT hr;

    init(&obj);
    CreateStreamOnHGlobal(NULL, TRUE, &stream);
    CoMarshalInterface(stream, &IID_IDropTarget, (IUnknown *)&obj.IDropTarget_iface, MSHCTX_INPROC, NULL,
                       MSHLFLAGS_TABLESTRONG);
    CoDisconnectObject((IUnknown *)&obj.IDropTarget_iface, 0);
    IStream_Seek(stream, zero, STREAM_SEEK_SET, NULL);
    hr = CoUnmarshalInterface(stream, &IID_IUnknown, (void **)&unk);
    printf("table strong, disconnected, unmarshaled: %#lx %s refs %ld\n", hr,
           unk == (IUnknown *)&obj.IDropTarget_iface ? "the object" : unk ? "another pointer" : "NULL", obj.refs);
    if (unk) IUnknown_Release(unk);
    IStream_Release(stream);
}

static IStream *stale_stream;
static struct object stale_obj;

static DWORD WINAPI stale_thread(void *arg)
{
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    init(&stale_obj);
    CreateStreamOnHGlobal(NULL, TRUE, &stale_stream);
    CoMarshalInterface(stale_stream, &IID_IDropTarget, (IUnknown *)&stale_obj.IDropTarget_iface, MSHCTX_INPROC, NULL,
                       MSHLFLAGS_TABLESTRONG);
    CoUninitialize();
    return 0;
}

/* table strong data whose apartment was uninitialized, released in another one */
static void stale_apartment(void)
{
    LARGE_INTEGER zero = {{0}};
    HANDLE thread = CreateThread(NULL, 0, stale_thread, NULL, 0, NULL);
    HRESULT hr;

    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    printf("table strong, apartment gone: refs %ld", stale_obj.refs);
    IStream_Seek(stale_stream, zero, STREAM_SEEK_SET, NULL);
    hr = CoReleaseMarshalData(stale_stream);
    printf(", release %#lx refs %ld\n", hr, stale_obj.refs);
    IStream_Release(stale_stream);
}

/* waits for a thread, dispatching the calls it makes into this apartment meanwhile */
static void wait_pumping(HANDLE thread)
{
    MSG msg;

    while (MsgWaitForMultipleObjects(1, &thread, FALSE, 10000, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
}

static IGlobalInterfaceTable *git_table;
static DWORD git_cookie;
static struct object git_obj;

static DWORD WINAPI git_thread(void *arg)
{
    IDropTarget *target = NULL;
    HRESULT hr;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    hr = IGlobalInterfaceTable_GetInterfaceFromGlobal(git_table, git_cookie, &IID_IDropTarget, (void **)&target);
    printf(", from another apartment %#lx %s refs %ld", hr, target == &git_obj.IDropTarget_iface ? "the object" :
           target ? "a proxy" : "NULL", git_obj.refs);
    if (target) IDropTarget_Release(target);
    CoUninitialize();
    return 0;
}

static void git_from_apartments(const char *when)
{
    IDropTarget *target = NULL;
    HANDLE thread;
    HRESULT hr;

    hr = IGlobalInterfaceTable_GetInterfaceFromGlobal(git_table, git_cookie, &IID_IDropTarget, (void **)&target);
    printf("  %s: from its apartment %#lx %s refs %ld", when, hr, target == &git_obj.IDropTarget_iface ? "the object" :
           target ? "another pointer" : "NULL", git_obj.refs);
    if (target) IDropTarget_Release(target);
    thread = CreateThread(NULL, 0, git_thread, NULL, 0, NULL);
    wait_pumping(thread);
    CloseHandle(thread);
    printf(", after %ld\n", git_obj.refs);
}

/* what the global interface table gives back, here and in another apartment, before and after a disconnect */
static void git_details(void)
{
    HRESULT hr;

    init(&git_obj);
    CoCreateInstance(&CLSID_StdGlobalInterfaceTable, NULL, CLSCTX_INPROC_SERVER, &IID_IGlobalInterfaceTable,
                     (void **)&git_table);
    hr = IGlobalInterfaceTable_RegisterInterfaceInGlobal(git_table, (IUnknown *)&git_obj.IDropTarget_iface,
                                                         &IID_IDropTarget, &git_cookie);
    printf("global interface table, from apartments: register %#lx refs %ld\n", hr, git_obj.refs);
    git_from_apartments("registered");
    hr = CoDisconnectObject((IUnknown *)&git_obj.IDropTarget_iface, 0);
    printf("  disconnect %#lx refs %ld\n", hr, git_obj.refs);
    git_from_apartments("disconnected");
    hr = IGlobalInterfaceTable_RevokeInterfaceFromGlobal(git_table, git_cookie);
    printf("  revoke %#lx refs %ld\n", hr, git_obj.refs);
    IGlobalInterfaceTable_Release(git_table);
}

static void rot(BOOL disconnect)
{
    IRunningObjectTable *table;
    struct object obj;
    IMoniker *moniker;
    DWORD cookie = 0;
    HRESULT hr;

    init(&obj);
    GetRunningObjectTable(0, &table);
    CreateItemMoniker(L"!", L"wine-altars-comrel-probe", &moniker);
    hr = IRunningObjectTable_Register(table, ROTFLAGS_REGISTRATIONKEEPSALIVE, (IUnknown *)&obj.IDropTarget_iface,
                                      moniker, &cookie);
    printf("running object table%s: register %#lx refs %ld", disconnect ? ", disconnected" : "", hr, obj.refs);
    if (disconnect)
    {
        hr = CoDisconnectObject((IUnknown *)&obj.IDropTarget_iface, 0);
        printf(", disconnect %#lx refs %ld", hr, obj.refs);
    }
    hr = IRunningObjectTable_IsRunning(table, moniker);
    printf(", running %#lx", hr);
    {
        IUnknown *unk = NULL;
        IEnumMoniker *enum_moniker;
        IMoniker *found;
        FILETIME time;
        int listed = 0;

        hr = IRunningObjectTable_GetObject(table, moniker, &unk);
        printf(", object %#lx %s", hr, unk == (IUnknown *)&obj.IDropTarget_iface ? "the object" :
               unk ? "another pointer" : "NULL");
        if (unk) IUnknown_Release(unk);
        hr = IRunningObjectTable_GetTimeOfLastChange(table, moniker, &time);
        printf(", time %#lx", hr);
        if (SUCCEEDED(IRunningObjectTable_EnumRunning(table, &enum_moniker)))
        {
            while (IEnumMoniker_Next(enum_moniker, 1, &found, NULL) == S_OK)
            {
                if (IMoniker_IsEqual(found, moniker) == S_OK) listed++;
                IMoniker_Release(found);
            }
            IEnumMoniker_Release(enum_moniker);
        }
        printf(", listed %d refs %ld", listed, obj.refs);
    }
    hr = IRunningObjectTable_Revoke(table, cookie);
    printf(", revoke %#lx refs %ld", hr, obj.refs);
    hr = IRunningObjectTable_Revoke(table, cookie);
    printf(", again %#lx\n", hr);
    IMoniker_Release(moniker);
    IRunningObjectTable_Release(table);
}

static void drop_target(BOOL disconnect)
{
    HWND hwnd = CreateWindowExW(0, L"static", L"comrel", WS_POPUP, 0, 0, 10, 10, NULL, NULL, NULL, NULL);
    struct object obj;
    HRESULT hr;

    init(&obj);
    hr = RegisterDragDrop(hwnd, &obj.IDropTarget_iface);
    printf("drop target%s: register %#lx refs %ld", disconnect ? ", disconnected" : "", hr, obj.refs);
    if (disconnect)
    {
        hr = CoDisconnectObject((IUnknown *)&obj.IDropTarget_iface, 0);
        printf(", disconnect %#lx refs %ld", hr, obj.refs);
    }
    hr = RevokeDragDrop(hwnd);
    printf(", revoke %#lx refs %ld", hr, obj.refs);
    hr = RevokeDragDrop(hwnd);
    printf(", again %#lx\n", hr);
    DestroyWindow(hwnd);
}

static void git(BOOL disconnect)
{
    IGlobalInterfaceTable *table;
    struct object obj;
    DWORD cookie = 0;
    HRESULT hr;

    init(&obj);
    CoCreateInstance(&CLSID_StdGlobalInterfaceTable, NULL, CLSCTX_INPROC_SERVER, &IID_IGlobalInterfaceTable,
                     (void **)&table);
    hr = IGlobalInterfaceTable_RegisterInterfaceInGlobal(table, (IUnknown *)&obj.IDropTarget_iface, &IID_IDropTarget,
                                                         &cookie);
    printf("global interface table%s: register %#lx refs %ld", disconnect ? ", disconnected" : "", hr, obj.refs);
    if (disconnect)
    {
        hr = CoDisconnectObject((IUnknown *)&obj.IDropTarget_iface, 0);
        printf(", disconnect %#lx refs %ld", hr, obj.refs);
    }
    hr = IGlobalInterfaceTable_RevokeInterfaceFromGlobal(table, cookie);
    printf(", revoke %#lx refs %ld", hr, obj.refs);
    hr = IGlobalInterfaceTable_RevokeInterfaceFromGlobal(table, cookie);
    printf(", again %#lx\n", hr);
    IGlobalInterfaceTable_Release(table);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    OleInitialize(NULL);
    marshal_kind("normal", &IID_IUnknown, MSHLFLAGS_NORMAL, FALSE, 1);
    marshal_kind("normal, disconnected", &IID_IUnknown, MSHLFLAGS_NORMAL, TRUE, 1);
    marshal_kind("table strong", &IID_IUnknown, MSHLFLAGS_TABLESTRONG, FALSE, 2);
    marshal_kind("table strong, disconnected", &IID_IUnknown, MSHLFLAGS_TABLESTRONG, TRUE, 2);
    marshal_kind("table weak", &IID_IUnknown, MSHLFLAGS_TABLEWEAK, FALSE, 2);
    marshal_kind("table weak, disconnected", &IID_IUnknown, MSHLFLAGS_TABLEWEAK, TRUE, 2);
    marshal_kind("IDropTarget normal", &IID_IDropTarget, MSHLFLAGS_NORMAL, FALSE, 1);
    marshal_kind("IDropTarget table strong", &IID_IDropTarget, MSHLFLAGS_TABLESTRONG, FALSE, 2);
    marshal_kind("IDropTarget table strong, disconnected", &IID_IDropTarget, MSHLFLAGS_TABLESTRONG, TRUE, 2);
    unmarshal_disconnected();
    stale_apartment();
    rot(FALSE);
    rot(TRUE);
    drop_target(FALSE);
    drop_target(TRUE);
    git(FALSE);
    git(TRUE);
    git_details();
    OleUninitialize();
    return 0;
}
