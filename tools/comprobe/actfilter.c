/*
 * When COM consults the activation filter a process registers: a filter that logs every call is registered, then
 * objects are activated each way COM offers -- CoCreateInstance and CoGetClassObject of an in-process class, of one
 * COM keeps for itself, of one registered with CoRegisterClassObject and of one not registered at all; from a
 * storage, a stream, a file, a class and a file moniker, a display name, OleCreate, OleCreateFromFile and
 * OleCreateFromData (whose data object prints what it is asked); the marshaler a custom OBJREF names; the proxies and
 * stubs standard marshaling makes for interfaces combase, oleaut32 and actxprxy marshal; and from another thread -- and then the filter refuses a class, puts another in
 * the place of one, and refuses what it put there.  The interfaces COM asks the filter for are printed too.
 */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <ole2.h>
#include <urlmon.h>
#include <shlobj.h>
#include <stdio.h>

static const CLSID clsid_security = {0x7b8a2d94,0x0ac9,0x11d1,{0x89,0x6c,0x00,0xc0,0x4f,0xb6,0xbf,0xc4}};
static const CLSID clsid_zones = {0x7b8a2d95,0x0ac9,0x11d1,{0x89,0x6c,0x00,0xc0,0x4f,0xb6,0xbf,0xc4}};
static const CLSID clsid_git = {0x00000323,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const CLSID clsid_local = {0x1c9d5b3a,0x2e4f,0x4d6a,{0x8b,0x7c,0x9e,0x0f,0x1a,0x2b,0x3c,0x4d}};
static const CLSID clsid_unregistered = {0x3f2d8a1e,0x55c1,0x4a7e,{0x9b,0x0e,0x61,0x2c,0x7d,0x13,0xa4,0x5f}};
static const IID iid_security = {0x79eac9ee,0xbaf9,0x11ce,{0x8c,0x82,0x00,0xaa,0x00,0x4b,0xa9,0x0b}};
static const IID iid_unknown = {0x00000000,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const IID iid_persist = {0x0000010c,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const IID iid_classfactory = {0x00000001,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const IID iid_dataobject = {0x0000010e,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};

typedef HRESULT (WINAPI *register_filter_func)(IUnknown *);

static enum { PASS, REFUSE, REPLACE, REPLACE_REFUSE, SUCCESS_CODE } mode;
static unsigned int calls;

/* one printf names two classes, so the names come from two buffers in turn */
static const char *name(REFCLSID clsid)
{
    static char buffers[2][64];
    static unsigned int next;
    char *buffer = buffers[next++ % 2];
    WCHAR str[40];

    if (IsEqualGUID(clsid, &clsid_security)) return "InternetSecurityManager";
    if (IsEqualGUID(clsid, &clsid_zones)) return "InternetZoneManager";
    if (IsEqualGUID(clsid, &clsid_git)) return "StdGlobalInterfaceTable";
    if (IsEqualGUID(clsid, &clsid_local)) return "RegisteredClassObject";
    if (IsEqualGUID(clsid, &clsid_unregistered)) return "Unregistered";
    StringFromGUID2(clsid, str, ARRAY_SIZE(str));
    snprintf(buffer, sizeof(buffers[0]), "%ls", str);
    return buffer;
}

static HRESULT WINAPI filter_QueryInterface(IUnknown *iface, REFIID riid, void **obj)
{
    WCHAR str[40];

    StringFromGUID2(riid, str, ARRAY_SIZE(str));
    printf("  filter QueryInterface %ls\n", str);
    *obj = iface;
    return S_OK;
}

struct filter
{
    const void *vtbl;
    unsigned int id;
    LONG refs;
};

static ULONG WINAPI filter_AddRef(IUnknown *iface)
{
    return InterlockedIncrement(&((struct filter *)iface)->refs);
}

static ULONG WINAPI filter_Release(IUnknown *iface)
{
    return InterlockedDecrement(&((struct filter *)iface)->refs);
}

/* IActivationFilter::HandleActivation, the one method after IUnknown's */
static HRESULT WINAPI filter_HandleActivation(IUnknown *iface, DWORD type, REFCLSID clsid, CLSID *replacement)
{
    struct filter *filter = (struct filter *)iface;

    calls++;
    printf("  %sHandleActivation type %#lx %s, replacement in %s\n", filter->id == 1 ? "" : "second filter's ",
           type, name(clsid), replacement ? name(replacement) : "NULL");
    if (replacement) *replacement = *clsid;
    if (mode == REFUSE && IsEqualGUID(clsid, &clsid_zones)) return E_ACCESSDENIED;
    if ((mode == REPLACE || mode == REPLACE_REFUSE) && IsEqualGUID(clsid, &clsid_zones) && replacement)
        *replacement = clsid_security;
    if (mode == REPLACE_REFUSE && IsEqualGUID(clsid, &clsid_security)) return E_ACCESSDENIED;
    if (mode == SUCCESS_CODE) return S_FALSE;
    return S_OK;
}

static const struct
{
    void *QueryInterface, *AddRef, *Release, *HandleActivation;
}
filter_vtbl = { filter_QueryInterface, filter_AddRef, filter_Release, filter_HandleActivation };
static struct filter filter = { &filter_vtbl, 1 }, filter2 = { &filter_vtbl, 2 };

static void report(const char *what, HRESULT hr, IUnknown *unk)
{
    printf(" %s: hr %#lx, %u calls\n", what, hr, calls);
    if (unk) IUnknown_Release(unk);
    calls = 0;
}

/* an object with IPersist, IDispatch and IServiceProvider, whose proxies and stubs come from combase, oleaut32 and
 * actxprxy; the class object that makes it; and the class COM is told about */
static const IID iid_dispatch = {0x00020400,0x0000,0x0000,{0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}};
static const IID iid_service = {0x6d5140c1,0x7436,0x11ce,{0x80,0x34,0x00,0xaa,0x00,0x60,0x09,0xfa}};

static struct object { const void *persist, *dispatch, *service; } object;

static HRESULT WINAPI object_QueryInterface(void *iface, REFIID riid, void **obj)
{
    if (IsEqualGUID(riid, &iid_unknown) || IsEqualGUID(riid, &iid_persist)) *obj = &object.persist;
    else if (IsEqualGUID(riid, &iid_dispatch)) *obj = &object.dispatch;
    else if (IsEqualGUID(riid, &iid_service)) *obj = &object.service;
    else
    {
        *obj = NULL;
        return E_NOINTERFACE;
    }
    return S_OK;
}

static ULONG WINAPI object_AddRef(void *iface) { return 2; }
static ULONG WINAPI object_Release(void *iface) { return 1; }

static HRESULT WINAPI object_GetClassID(void *iface, CLSID *clsid)
{
    *clsid = clsid_local;
    return S_OK;
}

static HRESULT WINAPI object_GetTypeInfoCount(void *iface, UINT *count)
{
    *count = 0;
    return S_OK;
}

static HRESULT WINAPI object_GetTypeInfo(void *iface, UINT index, LCID lcid, ITypeInfo **info)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI object_GetIDsOfNames(void *iface, REFIID riid, LPOLESTR *names, UINT count, LCID lcid,
                                           DISPID *ids)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI object_Invoke(void *iface, DISPID id, REFIID riid, LCID lcid, WORD flags, DISPPARAMS *params,
                                    VARIANT *result, EXCEPINFO *excep, UINT *arg)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI object_QueryService(void *iface, REFGUID service, REFIID riid, void **obj)
{
    *obj = NULL;
    return E_NOINTERFACE;
}

static const struct
{
    void *QueryInterface, *AddRef, *Release, *GetClassID;
}
persist_vtbl = { object_QueryInterface, object_AddRef, object_Release, object_GetClassID };
static const struct
{
    void *QueryInterface, *AddRef, *Release, *GetTypeInfoCount, *GetTypeInfo, *GetIDsOfNames, *Invoke;
}
dispatch_vtbl = { object_QueryInterface, object_AddRef, object_Release, object_GetTypeInfoCount, object_GetTypeInfo,
                  object_GetIDsOfNames, object_Invoke };
static const struct
{
    void *QueryInterface, *AddRef, *Release, *QueryService;
}
service_vtbl = { object_QueryInterface, object_AddRef, object_Release, object_QueryService };
static struct object object = { &persist_vtbl, &dispatch_vtbl, &service_vtbl };

static HRESULT WINAPI factory_QueryInterface(IClassFactory *iface, REFIID riid, void **obj)
{
    if (IsEqualGUID(riid, &iid_unknown) || IsEqualGUID(riid, &iid_classfactory))
    {
        *obj = iface;
        return S_OK;
    }
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI factory_AddRef(IClassFactory *iface) { return 2; }
static ULONG WINAPI factory_Release(IClassFactory *iface) { return 1; }

static HRESULT WINAPI factory_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID riid, void **obj)
{
    return object_QueryInterface(&object, riid, obj);
}

static HRESULT WINAPI factory_LockServer(IClassFactory *iface, BOOL lock) { return S_OK; }

static const struct
{
    void *QueryInterface, *AddRef, *Release, *CreateInstance, *LockServer;
}
factory_vtbl = { factory_QueryInterface, factory_AddRef, factory_Release, factory_CreateInstance, factory_LockServer };
static struct { const void *vtbl; } class_object = { &factory_vtbl };

/* a data object offering an embedded object of the security manager's class, as the clipboard would */
static CLIPFORMAT cf_embed_source, cf_object_descriptor;

static const char *format_name(const FORMATETC *fmt)
{
    static char buffer[96];
    char str[64];

    if (!GetClipboardFormatNameA(fmt->cfFormat, str, sizeof(str))) snprintf(str, sizeof(str), "%u", fmt->cfFormat);
    snprintf(buffer, sizeof(buffer), "%s, aspect %lu, index %ld, tymed %#lx%s", str, fmt->dwAspect, fmt->lindex,
             fmt->tymed, fmt->ptd ? ", device" : "");
    return buffer;
}

static IStorage *create_storage(void)
{
    IStorage *storage = NULL;
    ILockBytes *bytes;

    CreateILockBytesOnHGlobal(NULL, TRUE, &bytes);
    StgCreateDocfileOnILockBytes(bytes, STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, &storage);
    ILockBytes_Release(bytes);
    return storage;
}

static HRESULT WINAPI data_QueryInterface(IDataObject *iface, REFIID riid, void **obj)
{
    WCHAR str[40];

    if (IsEqualGUID(riid, &iid_unknown) || IsEqualGUID(riid, &iid_dataobject))
    {
        *obj = iface;
        return S_OK;
    }
    StringFromGUID2(riid, str, ARRAY_SIZE(str));
    printf("  data QueryInterface %ls\n", str);
    *obj = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI data_AddRef(IDataObject *iface) { return 2; }
static ULONG WINAPI data_Release(IDataObject *iface) { return 1; }

static BOOL offered(const FORMATETC *fmt)
{
    return (fmt->cfFormat == cf_embed_source && (fmt->tymed & TYMED_ISTORAGE))
        || (fmt->cfFormat == cf_object_descriptor && (fmt->tymed & TYMED_HGLOBAL));
}

static HRESULT WINAPI data_GetData(IDataObject *iface, FORMATETC *fmt, STGMEDIUM *medium)
{
    printf("  data GetData %s\n", format_name(fmt));
    if (!offered(fmt)) return DV_E_FORMATETC;
    medium->pUnkForRelease = NULL;
    if (fmt->cfFormat == cf_embed_source)
    {
        medium->tymed = TYMED_ISTORAGE;
        medium->pstg = create_storage();
        WriteClassStg(medium->pstg, &clsid_security);
    }
    else
    {
        OBJECTDESCRIPTOR *descriptor;

        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(*descriptor));
        descriptor = GlobalLock(medium->hGlobal);
        descriptor->cbSize = sizeof(*descriptor);
        descriptor->clsid = clsid_security;
        descriptor->dwDrawAspect = DVASPECT_CONTENT;
        GlobalUnlock(medium->hGlobal);
    }
    return S_OK;
}

static HRESULT WINAPI data_GetDataHere(IDataObject *iface, FORMATETC *fmt, STGMEDIUM *medium)
{
    printf("  data GetDataHere %s, medium tymed %#lx\n", format_name(fmt), medium->tymed);
    if (fmt->cfFormat != cf_embed_source || medium->tymed != TYMED_ISTORAGE) return DV_E_FORMATETC;
    WriteClassStg(medium->pstg, &clsid_security);
    return S_OK;
}

static HRESULT WINAPI data_QueryGetData(IDataObject *iface, FORMATETC *fmt)
{
    printf("  data QueryGetData %s\n", format_name(fmt));
    return offered(fmt) ? S_OK : DV_E_FORMATETC;
}

static HRESULT WINAPI data_GetCanonicalFormatEtc(IDataObject *iface, FORMATETC *in, FORMATETC *out)
{
    printf("  data GetCanonicalFormatEtc\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI data_SetData(IDataObject *iface, FORMATETC *fmt, STGMEDIUM *medium, BOOL release)
{
    printf("  data SetData %s\n", format_name(fmt));
    return E_NOTIMPL;
}

static HRESULT WINAPI data_EnumFormatEtc(IDataObject *iface, DWORD direction, IEnumFORMATETC **formats)
{
    FORMATETC list[2] =
    {
        { cf_embed_source, NULL, DVASPECT_CONTENT, -1, TYMED_ISTORAGE },
        { cf_object_descriptor, NULL, DVASPECT_CONTENT, -1, TYMED_HGLOBAL },
    };

    printf("  data EnumFormatEtc direction %lu\n", direction);
    if (direction != DATADIR_GET) return E_NOTIMPL;
    return SHCreateStdEnumFmtEtc(ARRAY_SIZE(list), list, formats);
}

static HRESULT WINAPI data_DAdvise(IDataObject *iface, FORMATETC *fmt, DWORD flags, IAdviseSink *sink, DWORD *conn)
{
    printf("  data DAdvise %s, flags %#lx\n", format_name(fmt), flags);
    return OLE_E_ADVISENOTSUPPORTED;
}

static HRESULT WINAPI data_DUnadvise(IDataObject *iface, DWORD conn)
{
    printf("  data DUnadvise\n");
    return OLE_E_ADVISENOTSUPPORTED;
}

static HRESULT WINAPI data_EnumDAdvise(IDataObject *iface, IEnumSTATDATA **advise)
{
    printf("  data EnumDAdvise\n");
    return OLE_E_ADVISENOTSUPPORTED;
}

static const IDataObjectVtbl data_vtbl =
{
    data_QueryInterface, data_AddRef, data_Release, data_GetData, data_GetDataHere, data_QueryGetData,
    data_GetCanonicalFormatEtc, data_SetData, data_EnumFormatEtc, data_DAdvise, data_DUnadvise, data_EnumDAdvise,
};
static IDataObject data_object = { &data_vtbl };

/* what another thread's activations and unmarshaling ask */
static IStream *marshaled[3];
static const IID *marshaled_iids[3] = { &iid_persist, &iid_dispatch, &iid_service };
static const char *marshaled_names[3] = { "IPersist", "IDispatch", "IServiceProvider" };

static DWORD WINAPI thread_proc(void *arg)
{
    IUnknown *unk;
    unsigned int i;
    HRESULT hr;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    unk = NULL;
    hr = CoCreateInstance(&clsid_security, NULL, CLSCTX_INPROC_SERVER, &iid_unknown, (void **)&unk);
    report("CoCreateInstance on another thread", hr, unk);
    for (i = 0; i < ARRAY_SIZE(marshaled); i++)
    {
        char what[64];

        unk = NULL;
        hr = CoGetInterfaceAndReleaseStream(marshaled[i], marshaled_iids[i], (void **)&unk);
        snprintf(what, sizeof(what), "unmarshaling a proxy for %s on another thread", marshaled_names[i]);
        report(what, hr, unk);
    }
    CoUninitialize();
    return 0;
}

int main(void)
{
    register_filter_func pCoRegisterActivationFilter;
    IClassFactory *factory;
    IStorage *storage;
    IStream *stream;
    IMoniker *moniker;
    IBindCtx *bind;
    IUnknown *unk;
    WCHAR path[MAX_PATH];
    HANDLE thread;
    DWORD cookie, index;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    OleInitialize(NULL);
    cf_embed_source = RegisterClipboardFormatA("Embed Source");
    cf_object_descriptor = RegisterClipboardFormatA("Object Descriptor");
    pCoRegisterActivationFilter = (void *)GetProcAddress(GetModuleHandleA("ole32.dll"), "CoRegisterActivationFilter");
    if (!pCoRegisterActivationFilter)
        pCoRegisterActivationFilter = (void *)GetProcAddress(LoadLibraryA("combase.dll"), "CoRegisterActivationFilter");
    printf("CoRegisterActivationFilter %p\n", pCoRegisterActivationFilter);
    if (!pCoRegisterActivationFilter) return 1;

    unk = NULL;
    hr = CoCreateInstance(&clsid_security, NULL, CLSCTX_INPROC_SERVER, &iid_unknown, (void **)&unk);
    report("before registering, CoCreateInstance", hr, unk);

    hr = pCoRegisterActivationFilter((IUnknown *)&filter);
    printf("register: hr %#lx, references %ld\n", hr, filter.refs);
    hr = pCoRegisterActivationFilter((IUnknown *)&filter);
    printf("register again: hr %#lx, references %ld\n", hr, filter.refs);

    unk = NULL;
    hr = CoCreateInstance(&clsid_security, NULL, CLSCTX_INPROC_SERVER, &iid_unknown, (void **)&unk);
    report("CoCreateInstance in-process", hr, unk);
    unk = NULL;
    hr = CoCreateInstance(&clsid_security, NULL, CLSCTX_ALL, &iid_unknown, (void **)&unk);
    report("CoCreateInstance CLSCTX_ALL", hr, unk);
    factory = NULL;
    hr = CoGetClassObject(&clsid_security, CLSCTX_INPROC_SERVER, NULL, &IID_IClassFactory, (void **)&factory);
    report("CoGetClassObject", hr, (IUnknown *)factory);
    unk = NULL;
    hr = CoCreateInstance(&clsid_git, NULL, CLSCTX_INPROC_SERVER, &iid_unknown, (void **)&unk);
    report("CoCreateInstance of COM's own global interface table", hr, unk);

    hr = CoRegisterClassObject(&clsid_local, (IUnknown *)&class_object, CLSCTX_INPROC_SERVER, REGCLS_MULTIPLEUSE,
                               &cookie);
    printf("CoRegisterClassObject: hr %#lx\n", hr);
    unk = NULL;
    hr = CoCreateInstance(&clsid_local, NULL, CLSCTX_INPROC_SERVER, &iid_unknown, (void **)&unk);
    report("CoCreateInstance of a class registered with CoRegisterClassObject", hr, unk);
    CoRevokeClassObject(cookie);
    unk = NULL;
    hr = CoCreateInstance(&clsid_unregistered, NULL, CLSCTX_INPROC_SERVER, &iid_unknown, (void **)&unk);
    report("CoCreateInstance of a class not registered", hr, unk);

    storage = create_storage();
    WriteClassStg(storage, &clsid_security);
    unk = NULL;
    hr = OleLoad(storage, &iid_unknown, NULL, (void **)&unk);
    report("OleLoad from a storage", hr, unk);
    IStorage_Release(storage);

    CreateStreamOnHGlobal(NULL, TRUE, &stream);
    WriteClassStm(stream, &clsid_security);
    IStream_Seek(stream, (LARGE_INTEGER){{0}}, STREAM_SEEK_SET, NULL);
    unk = NULL;
    hr = OleLoadFromStream(stream, &iid_unknown, (void **)&unk);
    report("OleLoadFromStream", hr, unk);
    IStream_Release(stream);

    GetTempPathW(ARRAY_SIZE(path), path);
    wcscat(path, L"actfilter.stg");
    if (SUCCEEDED(StgCreateDocfile(path, STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, &storage)))
    {
        WriteClassStg(storage, &clsid_security);
        IStorage_Release(storage);
        hr = CoGetInstanceFromFile(NULL, NULL, NULL, CLSCTX_INPROC_SERVER, STGM_READ, path, 1,
                                   &(MULTI_QI){ &iid_unknown, NULL, S_OK });
        report("CoGetInstanceFromFile", hr, NULL);

        CreateFileMoniker(path, &moniker);
        CreateBindCtx(0, &bind);
        unk = NULL;
        hr = IMoniker_BindToObject(moniker, bind, NULL, &iid_unknown, (void **)&unk);
        report("file moniker BindToObject", hr, unk);
        IBindCtx_Release(bind);
        IMoniker_Release(moniker);

        storage = create_storage();
        unk = NULL;
        hr = OleCreateFromFile(&CLSID_NULL, path, &iid_unknown, OLERENDER_NONE, NULL, NULL, storage, (void **)&unk);
        report("OleCreateFromFile", hr, unk);
        IStorage_Release(storage);
        DeleteFileW(path);
    }

    CreateClassMoniker(&clsid_security, &moniker);
    CreateBindCtx(0, &bind);
    unk = NULL;
    hr = IMoniker_BindToObject(moniker, bind, NULL, &iid_unknown, (void **)&unk);
    report("class moniker BindToObject", hr, unk);
    IBindCtx_Release(bind);
    IMoniker_Release(moniker);

    unk = NULL;
    hr = CoGetObject(L"clsid:7B8A2D94-0AC9-11D1-896C-00C04FB6BFC4:", NULL, &iid_unknown, (void **)&unk);
    report("CoGetObject of a clsid: display name", hr, unk);

    storage = create_storage();
    unk = NULL;
    hr = OleCreate(&clsid_security, &iid_unknown, OLERENDER_NONE, NULL, NULL, storage, (void **)&unk);
    report("OleCreate", hr, unk);
    IStorage_Release(storage);

    hr = OleQueryCreateFromData(&data_object);
    report("OleQueryCreateFromData", hr, NULL);
    storage = create_storage();
    unk = NULL;
    hr = OleCreateFromData(&data_object, &iid_unknown, OLERENDER_NONE, NULL, NULL, storage, (void **)&unk);
    report("OleCreateFromData", hr, unk);
    IStorage_Release(storage);

    {
        /* an OBJREF whose custom marshaler is the security manager */
        struct
        {
            ULONG signature, flags;
            IID iid;
            CLSID clsid;
            ULONG extension, size;
        }
        objref = { 0x574f454d, 4, iid_unknown, clsid_security, 0, 0 };

        CreateStreamOnHGlobal(NULL, TRUE, &stream);
        IStream_Write(stream, &objref, sizeof(objref), NULL);
        IStream_Seek(stream, (LARGE_INTEGER){{0}}, STREAM_SEEK_SET, NULL);
        unk = NULL;
        hr = CoUnmarshalInterface(stream, &iid_unknown, (void **)&unk);
        report("CoUnmarshalInterface of a custom OBJREF", hr, unk);
        IStream_Release(stream);
    }

    for (index = 0; index < ARRAY_SIZE(marshaled); index++)
    {
        char what[64];

        hr = CoMarshalInterThreadInterfaceInStream(marshaled_iids[index], (IUnknown *)&object, &marshaled[index]);
        snprintf(what, sizeof(what), "marshaling a stub for %s", marshaled_names[index]);
        report(what, hr, NULL);
    }
    thread = CreateThread(NULL, 0, thread_proc, NULL, 0, NULL);
    CoWaitForMultipleHandles(0, INFINITE, 1, &thread, &index);
    CloseHandle(thread);

    mode = REFUSE;
    unk = NULL;
    hr = CoCreateInstance(&clsid_zones, NULL, CLSCTX_INPROC_SERVER, &iid_unknown, (void **)&unk);
    report("refused", hr, unk);
    factory = NULL;
    hr = CoGetClassObject(&clsid_zones, CLSCTX_INPROC_SERVER, NULL, &IID_IClassFactory, (void **)&factory);
    report("refused, CoGetClassObject", hr, (IUnknown *)factory);

    mode = REPLACE;
    unk = NULL;
    hr = CoCreateInstance(&clsid_zones, NULL, CLSCTX_INPROC_SERVER, &iid_security, (void **)&unk);
    report("replaced, asking for IInternetSecurityManager", hr, unk);
    CreateClassMoniker(&clsid_zones, &moniker);
    CreateBindCtx(0, &bind);
    unk = NULL;
    hr = IMoniker_BindToObject(moniker, bind, NULL, &iid_unknown, (void **)&unk);
    report("replaced, through a class moniker", hr, unk);
    IBindCtx_Release(bind);
    IMoniker_Release(moniker);

    mode = REPLACE_REFUSE;
    unk = NULL;
    hr = CoCreateInstance(&clsid_zones, NULL, CLSCTX_INPROC_SERVER, &iid_unknown, (void **)&unk);
    report("replaced by a class the filter refuses", hr, unk);

    mode = SUCCESS_CODE;
    unk = NULL;
    hr = CoCreateInstance(&clsid_security, NULL, CLSCTX_INPROC_SERVER, &iid_unknown, (void **)&unk);
    report("filter answers S_FALSE", hr, unk);

    hr = pCoRegisterActivationFilter(NULL);
    printf("register NULL: hr %#lx, references %ld\n", hr, filter.refs);
    mode = PASS;
    unk = NULL;
    hr = CoCreateInstance(&clsid_security, NULL, CLSCTX_INPROC_SERVER, &iid_unknown, (void **)&unk);
    report("after registering NULL", hr, unk);

    hr = pCoRegisterActivationFilter((IUnknown *)&filter2);
    printf("register another filter: hr %#lx, references %ld and %ld\n", hr, filter.refs, filter2.refs);
    unk = NULL;
    hr = CoCreateInstance(&clsid_security, NULL, CLSCTX_INPROC_SERVER, &iid_unknown, (void **)&unk);
    report("after registering another filter", hr, unk);

    OleUninitialize();
    printf("after OleUninitialize: references %ld and %ld\n", filter.refs, filter2.refs);
    CoInitialize(NULL);
    unk = NULL;
    hr = CoCreateInstance(&clsid_security, NULL, CLSCTX_INPROC_SERVER, &iid_unknown, (void **)&unk);
    report("initialized again", hr, unk);
    CoUninitialize();
    printf("done\n");
    return 0;
}
