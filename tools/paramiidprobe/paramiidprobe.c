/* paramiidprobe: what RoGetParameterizedTypeInstanceIID answers -- the IID, the type
 * signature behind it, which names it asks the locator about, and its errors.
 * Pure computation: nothing is registered or written. */
#include <windows.h>
#include <stdio.h>

typedef void *ROPARAMIIDHANDLE;
typedef struct builder builder;
typedef struct builder_vtbl
{
    HRESULT (WINAPI *SetWinRtInterface)(builder *, GUID);
    HRESULT (WINAPI *SetDelegate)(builder *, GUID);
    HRESULT (WINAPI *SetInterfaceGroupSimpleDefault)(builder *, const WCHAR *, const WCHAR *, const GUID *);
    HRESULT (WINAPI *SetInterfaceGroupParameterizedDefault)(builder *, const WCHAR *, UINT32, const WCHAR **);
    HRESULT (WINAPI *SetRuntimeClassSimpleDefault)(builder *, const WCHAR *, const WCHAR *, const GUID *);
    HRESULT (WINAPI *SetRuntimeClassParameterizedDefault)(builder *, const WCHAR *, UINT32, const WCHAR **);
    HRESULT (WINAPI *SetStruct)(builder *, const WCHAR *, UINT32, const WCHAR **);
    HRESULT (WINAPI *SetEnum)(builder *, const WCHAR *, const WCHAR *);
    HRESULT (WINAPI *SetParameterizedInterface)(builder *, GUID, UINT32);
    HRESULT (WINAPI *SetParameterizedDelegate)(builder *, GUID, UINT32);
} builder_vtbl;
struct builder { const builder_vtbl *vtbl; };

typedef struct locator locator;
typedef struct locator_vtbl { HRESULT (WINAPI *Locate)(const locator *, const WCHAR *, builder *); } locator_vtbl;
struct locator { const locator_vtbl *vtbl; };

static HRESULT (WINAPI *pRoGetParameterizedTypeInstanceIID)(UINT32, const WCHAR **, const locator *, GUID *, ROPARAMIIDHANDLE *);
static void (WINAPI *pRoFreeParameterizedTypeExtra)(ROPARAMIIDHANDLE);
static const char *(WINAPI *pRoParameterizedTypeExtraGetTypeSignature)(ROPARAMIIDHANDLE);

static GUID guid(const char *s)
{
    GUID g;
    unsigned int d[11];
    sscanf(s, "%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x", &d[0], &d[1], &d[2], &d[3], &d[4], &d[5], &d[6], &d[7],
           &d[8], &d[9], &d[10]);
    g.Data1 = d[0]; g.Data2 = d[1]; g.Data3 = d[2];
    for (int i = 0; i < 8; i++) g.Data4[i] = d[3 + i];
    return g;
}

/* what the locator says about each name; "mode" picks a misbehaviour */
static char calls[2048];
static int mode;
static GUID group_iid, rc_iid;

static void note(const WCHAR *name)
{
    char buf[200];
    snprintf(buf, sizeof(buf), "%s%ls", calls[0] ? ", " : "", name);
    strncat(calls, buf, sizeof(calls) - strlen(calls) - 1);
}

static HRESULT WINAPI locate(const locator *iface, const WCHAR *name, builder *b)
{
    static const WCHAR *point_fields[] = { L"Single", L"Single" };
    static const WCHAR *nested_fields[] = { L"Windows.Foundation.Point", L"Windows.Foundation.AsyncStatus", L"String" };
    static const WCHAR *bad_fields[] = { L"Windows.Foundation.Collections.IVector`1", L"String" };
    static const WCHAR *map_default[] = { L"Windows.Foundation.Collections.IMap`2", L"String", L"Object" };
    HRESULT hr;

    note(name);
    if (mode == 1) return E_FAIL;
    if (mode == 2) return S_OK;
    if (mode == 3 && !wcscmp(name, L"Windows.Foundation.Collections.IVector`1"))
    {
        b->vtbl->SetParameterizedInterface(b, guid("913337e9-11a1-4345-a3a2-4e7f956e222d"), 1);
        hr = b->vtbl->SetWinRtInterface(b, guid("5a648006-843a-4da9-865b-9d26e5dfad7b"));
        printf("  second setter: %#lx\n", hr);
        return S_OK;
    }
    if (mode == 4) return 0x80000013;  /* RO_E_CLOSED, to see if it is passed on */
    if (mode == 5 && !wcscmp(name, L"Windows.Foundation.Collections.IVector`1"))
        return b->vtbl->SetParameterizedInterface(b, guid("913337e9-11a1-4345-a3a2-4e7f956e222d"), 0);
    if (mode == 6 && !wcscmp(name, L"Windows.Foundation.Collections.IVector`1"))
        return b->vtbl->SetParameterizedInterface(b, guid("913337e9-11a1-4345-a3a2-4e7f956e222d"), 2);

    if (!wcscmp(name, L"Windows.Foundation.Collections.IVector`1"))
        return b->vtbl->SetParameterizedInterface(b, guid("913337e9-11a1-4345-a3a2-4e7f956e222d"), 1);
    if (!wcscmp(name, L"Windows.Foundation.Collections.IIterable`1"))
        return b->vtbl->SetParameterizedInterface(b, guid("faa585ea-6214-4217-afda-7f46de5869b3"), 1);
    if (!wcscmp(name, L"Windows.Foundation.Collections.IMap`2"))
        return b->vtbl->SetParameterizedInterface(b, guid("3c2925fe-8519-45c1-aa79-197b6718c1c1"), 2);
    if (!wcscmp(name, L"Windows.Foundation.Collections.IKeyValuePair`2"))
        return b->vtbl->SetParameterizedInterface(b, guid("02b51929-c1c4-4a7e-8940-0312b5c18500"), 2);
    if (!wcscmp(name, L"Windows.Foundation.IAsyncOperation`1"))
        return b->vtbl->SetParameterizedInterface(b, guid("9fc2b0bb-e446-44e2-aa61-9cab8f636af2"), 1);
    if (!wcscmp(name, L"Windows.Foundation.IReference`1"))
        return b->vtbl->SetParameterizedInterface(b, guid("61c17706-2d65-11e0-9ae8-d48564015472"), 1);
    if (!wcscmp(name, L"Windows.Foundation.AsyncOperationCompletedHandler`1"))
        return b->vtbl->SetParameterizedDelegate(b, guid("fcdcf02c-e5d8-4478-915a-4d90b74b83a5"), 1);
    if (!wcscmp(name, L"Windows.Foundation.IAsyncAction"))
        return b->vtbl->SetWinRtInterface(b, guid("5a648006-843a-4da9-865b-9d26e5dfad7b"));
    if (!wcscmp(name, L"Windows.Foundation.AsyncActionCompletedHandler"))
        return b->vtbl->SetDelegate(b, guid("a4ed5c81-76c9-40bd-8be6-b1d90fb20ae7"));
    if (!wcscmp(name, L"Windows.Foundation.Uri"))
        return b->vtbl->SetRuntimeClassSimpleDefault(b, name, L"Windows.Foundation.IUriRuntimeClass", &rc_iid);
    if (!wcscmp(name, L"Test.ClassNoIid"))
        return b->vtbl->SetRuntimeClassSimpleDefault(b, name, L"Windows.Foundation.IAsyncAction", NULL);
    if (!wcscmp(name, L"Windows.Foundation.Collections.PropertySet"))
        return b->vtbl->SetRuntimeClassParameterizedDefault(b, name, 3, map_default);
    if (!wcscmp(name, L"Test.Group"))
        return b->vtbl->SetInterfaceGroupSimpleDefault(b, name, L"Windows.Foundation.IAsyncAction", &group_iid);
    if (!wcscmp(name, L"Test.GroupNoIid"))
        return b->vtbl->SetInterfaceGroupSimpleDefault(b, name, L"Windows.Foundation.IAsyncAction", NULL);
    if (!wcscmp(name, L"Test.PGroup"))
        return b->vtbl->SetInterfaceGroupParameterizedDefault(b, name, 3, map_default);
    if (!wcscmp(name, L"Windows.Foundation.Point"))
        return b->vtbl->SetStruct(b, name, 2, point_fields);
    if (!wcscmp(name, L"Test.Nested"))
        return b->vtbl->SetStruct(b, name, 3, nested_fields);
    if (!wcscmp(name, L"Test.BadStruct"))
        return b->vtbl->SetStruct(b, name, 2, bad_fields);
    if (!wcscmp(name, L"Test.EmptyStruct"))
        return b->vtbl->SetStruct(b, name, 0, NULL);
    if (!wcscmp(name, L"Windows.Foundation.AsyncStatus"))
        return b->vtbl->SetEnum(b, name, L"Int32");
    if (!wcscmp(name, L"Test.Flags"))
        return b->vtbl->SetEnum(b, name, L"UInt32");
    if (!wcscmp(name, L"Test.StringEnum"))
        return b->vtbl->SetEnum(b, name, L"String");
    if (!wcscmp(name, L"Test.ByteEnum"))
        return b->vtbl->SetEnum(b, name, L"UInt8");
    return 0x80073d54;  /* not found; any failure */
}

static const locator_vtbl locator_vtbl_ = { locate };
static const locator loc = { &locator_vtbl_ };

static void run(const char *label, UINT32 count, const WCHAR **names, BOOL extra)
{
    ROPARAMIIDHANDLE handle = (void *)0xdeadbeef;
    GUID iid;
    HRESULT hr;

    calls[0] = 0;
    memset(&iid, 0xcc, sizeof(iid));
    hr = pRoGetParameterizedTypeInstanceIID(count, names, &loc, &iid, extra ? &handle : NULL);
    printf("%s: %#lx {%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", label, hr, iid.Data1, iid.Data2, iid.Data3,
           iid.Data4[0], iid.Data4[1], iid.Data4[2], iid.Data4[3], iid.Data4[4], iid.Data4[5], iid.Data4[6], iid.Data4[7]);
    if (extra)
    {
        if (handle == (void *)0xdeadbeef) printf(" handle untouched");
        else if (!handle) printf(" handle NULL");
        else if (handle == INVALID_HANDLE_VALUE) printf(" handle INVALID");
        else
        {
            const char *sig = pRoParameterizedTypeExtraGetTypeSignature(handle);
            printf(" sig %s", sig ? sig : "(null)");
            pRoFreeParameterizedTypeExtra(handle);
        }
    }
    printf("; asked [%s]\n", calls);
}

#define RUN(label, ...) do { static const WCHAR *n[] = { __VA_ARGS__ }; run(label, ARRAYSIZE(n), n, TRUE); } while (0)

int main(void)
{
    static const WCHAR *fundamentals[] =
    {
        L"Boolean", L"UInt8", L"Int8", L"Int16", L"UInt16", L"Int32", L"UInt32", L"Int64", L"UInt64", L"Single",
        L"Double", L"Char16", L"String", L"Guid", L"Object", L"string", L"Byte", L"Char", L"Void", L"TimeSpan",
        L"Windows.Foundation.TimeSpan", L"",
    };
    HMODULE combase = LoadLibraryW(L"combase.dll");
    const WCHAR *names[3];
    char label[64];
    unsigned int i;

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    pRoGetParameterizedTypeInstanceIID = (void *)GetProcAddress(combase, "RoGetParameterizedTypeInstanceIID");
    pRoFreeParameterizedTypeExtra = (void *)GetProcAddress(combase, "RoFreeParameterizedTypeExtra");
    pRoParameterizedTypeExtraGetTypeSignature = (void *)GetProcAddress(combase, "RoParameterizedTypeExtraGetTypeSignature");
    printf("exports %d %d %d\n", !!pRoGetParameterizedTypeInstanceIID, !!pRoFreeParameterizedTypeExtra,
           !!pRoParameterizedTypeExtraGetTypeSignature);
    if (!pRoGetParameterizedTypeInstanceIID || !pRoFreeParameterizedTypeExtra || !pRoParameterizedTypeExtraGetTypeSignature)
        return 1;
    group_iid = guid("00000000-1111-2222-3333-444455556666");
    rc_iid = guid("9e365e57-48b2-4160-956f-c7385120bbfc");

    RUN("IVector<String>", L"Windows.Foundation.Collections.IVector`1", L"String");
    RUN("IIterable<String>", L"Windows.Foundation.Collections.IIterable`1", L"String");
    RUN("IMap<String,Object>", L"Windows.Foundation.Collections.IMap`2", L"String", L"Object");
    RUN("IAsyncOperation<Boolean>", L"Windows.Foundation.IAsyncOperation`1", L"Boolean");
    RUN("IIterable<IKeyValuePair<String,String>>", L"Windows.Foundation.Collections.IIterable`1",
        L"Windows.Foundation.Collections.IKeyValuePair`2", L"String", L"String");
    RUN("IMap<String,IVector<String>>", L"Windows.Foundation.Collections.IMap`2", L"String",
        L"Windows.Foundation.Collections.IVector`1", L"String");
    RUN("IVector<IAsyncAction>", L"Windows.Foundation.Collections.IVector`1", L"Windows.Foundation.IAsyncAction");
    RUN("IVector<Uri>", L"Windows.Foundation.Collections.IVector`1", L"Windows.Foundation.Uri");
    RUN("IVector<ClassNoIid>", L"Windows.Foundation.Collections.IVector`1", L"Test.ClassNoIid");
    RUN("IVector<PropertySet>", L"Windows.Foundation.Collections.IVector`1", L"Windows.Foundation.Collections.PropertySet");
    RUN("IVector<Group>", L"Windows.Foundation.Collections.IVector`1", L"Test.Group");
    RUN("IVector<GroupNoIid>", L"Windows.Foundation.Collections.IVector`1", L"Test.GroupNoIid");
    RUN("IVector<PGroup>", L"Windows.Foundation.Collections.IVector`1", L"Test.PGroup");
    RUN("IReference<Point>", L"Windows.Foundation.IReference`1", L"Windows.Foundation.Point");
    RUN("IReference<Nested>", L"Windows.Foundation.IReference`1", L"Test.Nested");
    RUN("IReference<BadStruct>", L"Windows.Foundation.IReference`1", L"Test.BadStruct");
    RUN("IReference<EmptyStruct>", L"Windows.Foundation.IReference`1", L"Test.EmptyStruct");
    RUN("IReference<AsyncStatus>", L"Windows.Foundation.IReference`1", L"Windows.Foundation.AsyncStatus");
    RUN("IReference<Flags>", L"Windows.Foundation.IReference`1", L"Test.Flags");
    RUN("IReference<StringEnum>", L"Windows.Foundation.IReference`1", L"Test.StringEnum");
    RUN("IReference<ByteEnum>", L"Windows.Foundation.IReference`1", L"Test.ByteEnum");
    RUN("IVector<AsyncActionCompletedHandler>", L"Windows.Foundation.Collections.IVector`1",
        L"Windows.Foundation.AsyncActionCompletedHandler");
    RUN("AsyncOperationCompletedHandler<Boolean>", L"Windows.Foundation.AsyncOperationCompletedHandler`1", L"Boolean");
    RUN("IVector<AsyncOperationCompletedHandler<Boolean>>", L"Windows.Foundation.Collections.IVector`1",
        L"Windows.Foundation.AsyncOperationCompletedHandler`1", L"Boolean");

    for (i = 0; i < ARRAYSIZE(fundamentals); i++)
    {
        names[0] = L"Windows.Foundation.IReference`1";
        names[1] = fundamentals[i];
        snprintf(label, sizeof(label), "IReference<%ls>", fundamentals[i]);
        run(label, 2, names, TRUE);
    }

    /* errors */
    RUN("not parameterized: String", L"String");
    RUN("not parameterized: IAsyncAction", L"Windows.Foundation.IAsyncAction");
    RUN("not parameterized: Point", L"Windows.Foundation.Point");
    RUN("too few", L"Windows.Foundation.Collections.IMap`2", L"String");
    RUN("too many", L"Windows.Foundation.Collections.IVector`1", L"String", L"String");
    RUN("unknown argument", L"Windows.Foundation.Collections.IVector`1", L"Test.Unknown");
    RUN("unknown first", L"Test.Unknown", L"String");
    RUN("parameterized without arguments", L"Windows.Foundation.Collections.IVector`1");
    names[0] = L"Windows.Foundation.Collections.IVector`1"; names[1] = L"String";
    run("count 0", 0, names, TRUE);
    run("no extra", 2, names, FALSE);
    /* NULL pointers are left out: they may crash the probe on Windows */
    mode = 1; run("locator fails", 2, names, TRUE);
    mode = 2; run("locator sets nothing", 2, names, TRUE);
    mode = 3; run("locator sets twice", 2, names, TRUE);
    mode = 4; run("locator RO_E_CLOSED", 2, names, TRUE);
    mode = 5; run("zero arguments", 2, names, TRUE);
    mode = 6; run("arg count 2 for 1 element", 2, names, TRUE);
    mode = 0;
    return 0;
}
