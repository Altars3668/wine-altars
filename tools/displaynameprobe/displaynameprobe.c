/* How "winmgmts:" display names reach their parser: which interfaces WMI's scripting classes give
 * out as class objects, what CoGetClassObject answers when a class's in-process server refuses the
 * interface asked for, and what MkParseDisplayName makes of a few WMI names.
 *
 * GetObject("winmgmts:") in a script is MkParseDisplayName, which first asks the ProgID's class
 * object for IParseDisplayName and only then creates an instance through IClassFactory.  Wine's
 * wbemdisp answers the first question with E_NOINTERFACE, so COM goes on to look for a local server
 * that does not exist and logs two ERRs before the fallback works.  This probe says whether
 * Windows' class object hands out IParseDisplayName itself, whether that is the same object as the
 * class factory, and what COM answers in the case Wine logs about.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror displaynameprobe.c -o displaynameprobe.exe -lole32 -loleaut32 -luuid
 */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <stdio.h>

static const CLSID CLSID_WinMGMTS = {0x172bddf8, 0xceea, 0x11d1, {0x8b, 0x05, 0x00, 0x60, 0x08, 0x06, 0xd9, 0xb6}};
static const CLSID CLSID_SWbemLocator = {0x76a64158, 0xcb41, 0x11d1, {0x8b, 0x02, 0x00, 0x60, 0x08, 0x06, 0xd9, 0xb6}};

static const struct { const char *name; const IID *iid; } interfaces[] =
{
    {"IUnknown", &IID_IUnknown},
    {"IClassFactory", &IID_IClassFactory},
    {"IParseDisplayName", &IID_IParseDisplayName},
    {"IPersistStream", &IID_IPersistStream},
};

static const struct { const char *name; const CLSID *clsid; } classes[] =
{
    {"WinMGMTS", &CLSID_WinMGMTS},
    {"SWbemLocator", &CLSID_SWbemLocator},
};

static void guid_text(const GUID *g, char *out)
{
    sprintf(out, "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", g->Data1, g->Data2, g->Data3,
            g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
}

/* which of the interfaces an object answers to, and whether each is the same object (same IUnknown) */
static void describe(IUnknown *obj)
{
    IUnknown *identity = NULL, *unk;
    unsigned int i;
    HRESULT hr;

    IUnknown_QueryInterface(obj, &IID_IUnknown, (void **)&identity);
    for (i = 0; i < ARRAYSIZE(interfaces); i++)
    {
        IUnknown *other = NULL, *other_identity = NULL;

        hr = IUnknown_QueryInterface(obj, interfaces[i].iid, (void **)&other);
        printf("    QI %s: %#lx", interfaces[i].name, hr);
        if (SUCCEEDED(hr) && other)
        {
            IUnknown_QueryInterface(other, &IID_IUnknown, (void **)&other_identity);
            printf(", %s object", other_identity == identity ? "same" : "another");
            if (other_identity) IUnknown_Release(other_identity);
            IUnknown_Release(other);
        }
        printf("\n");
    }
    if (SUCCEEDED(IUnknown_QueryInterface(obj, &IID_IParseDisplayName, (void **)&unk)))
    {
        IParseDisplayName *parse = (IParseDisplayName *)unk;
        IMoniker *moniker = NULL;
        IBindCtx *ctx;
        ULONG eaten = 0;

        CreateBindCtx(0, &ctx);
        hr = IParseDisplayName_ParseDisplayName(parse, ctx, (WCHAR *)L"winmgmts:", &eaten, &moniker);
        printf("    ParseDisplayName(\"winmgmts:\"): %#lx, eaten %lu\n", hr, eaten);
        if (moniker) IMoniker_Release(moniker);
        IBindCtx_Release(ctx);
        IParseDisplayName_Release(parse);
    }
    if (identity) IUnknown_Release(identity);
}

static void class_objects(void)
{
    HRESULT (WINAPI *get_class_object)(REFCLSID, REFIID, void **);
    static const DWORD contexts[] = {CLSCTX_INPROC_SERVER, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER | CLSCTX_REMOTE_SERVER, CLSCTX_ALL};
    HMODULE module = LoadLibraryW(L"wbemdisp.dll");
    unsigned int c, i, k;
    IUnknown *obj;
    HRESULT hr;

    get_class_object = module ? (void *)GetProcAddress(module, "DllGetClassObject") : NULL;
    printf("wbemdisp.dll %s\n", get_class_object ? "loaded" : "not loaded");

    for (c = 0; c < ARRAYSIZE(classes); c++)
    {
        for (i = 0; get_class_object && i < ARRAYSIZE(interfaces); i++)
        {
            obj = NULL;
            hr = get_class_object(classes[c].clsid, interfaces[i].iid, (void **)&obj);
            printf("DllGetClassObject(%s, %s): %#lx\n", classes[c].name, interfaces[i].name, hr);
            if (SUCCEEDED(hr) && obj)
            {
                describe(obj);
                IUnknown_Release(obj);
            }
        }
        for (k = 0; k < ARRAYSIZE(contexts); k++)
            for (i = 1; i < ARRAYSIZE(interfaces); i++)
            {
                obj = NULL;
                hr = CoGetClassObject(classes[c].clsid, contexts[k], NULL, interfaces[i].iid, (void **)&obj);
                printf("CoGetClassObject(%s, %#lx, %s): %#lx\n", classes[c].name, contexts[k], interfaces[i].name, hr);
                if (obj) IUnknown_Release(obj);
            }
        for (i = 0; i < ARRAYSIZE(interfaces); i++)
        {
            obj = NULL;
            hr = CoCreateInstance(classes[c].clsid, NULL, CLSCTX_INPROC_SERVER, interfaces[i].iid, (void **)&obj);
            printf("CoCreateInstance(%s, %s): %#lx\n", classes[c].name, interfaces[i].name, hr);
            if (obj) IUnknown_Release(obj);
        }
    }
}

static void type_name(IUnknown *obj, char *out, size_t size)
{
    IDispatch *disp;
    ITypeInfo *info;
    BSTR name;

    snprintf(out, size, "(no IDispatch)");
    if (FAILED(IUnknown_QueryInterface(obj, &IID_IDispatch, (void **)&disp))) return;
    snprintf(out, size, "(no type info)");
    if (SUCCEEDED(IDispatch_GetTypeInfo(disp, 0, 0, &info)))
    {
        if (SUCCEEDED(ITypeInfo_GetDocumentation(info, MEMBERID_NIL, &name, NULL, NULL, NULL)))
        {
            snprintf(out, size, "%ls", name);
            SysFreeString(name);
        }
        ITypeInfo_Release(info);
    }
    IDispatch_Release(disp);
}

static void parse(const WCHAR *name)
{
    char text[256], clsid[40];
    IMoniker *moniker = NULL;
    IUnknown *obj = NULL;
    WCHAR *display;
    IBindCtx *ctx;
    ULONG eaten = 0;
    DWORD sys = 0;
    CLSID class;
    HRESULT hr;

    CreateBindCtx(0, &ctx);
    hr = MkParseDisplayName(ctx, name, &eaten, &moniker);
    printf("MkParseDisplayName(\"%ls\"): %#lx, eaten %lu of %u\n", name, hr, eaten, (unsigned int)wcslen(name));
    if (SUCCEEDED(hr) && moniker)
    {
        IMoniker_IsSystemMoniker(moniker, &sys);
        hr = IMoniker_GetClassID(moniker, &class);
        if (SUCCEEDED(hr)) guid_text(&class, clsid); else sprintf(clsid, "%#lx", hr);
        printf("    moniker MKSYS %lu, class %s\n", sys, clsid);
        hr = IMoniker_GetDisplayName(moniker, ctx, NULL, &display);
        if (SUCCEEDED(hr))
        {
            printf("    display name: \"%ls\"\n", display);
            CoTaskMemFree(display);
        }
        else printf("    display name: %#lx\n", hr);
        hr = IMoniker_BindToObject(moniker, ctx, NULL, &IID_IUnknown, (void **)&obj);
        printf("    BindToObject: %#lx", hr);
        if (SUCCEEDED(hr) && obj)
        {
            type_name(obj, text, sizeof(text));
            printf(", type %s", text);
            IUnknown_Release(obj);
        }
        printf("\n");
        IMoniker_Release(moniker);
    }
    IBindCtx_Release(ctx);
}

int main(void)
{
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    class_objects();
    parse(L"winmgmts:");
    parse(L"WINMGMTS:");
    parse(L"winmgmts:\\\\.\\root\\cimv2");
    parse(L"winmgmts:{impersonationLevel=impersonate}!\\\\.\\root\\cimv2");
    parse(L"winmgmts:root\\cimv2:Win32_OperatingSystem=@");
    parse(L"winmgmts:\\\\.\\root\\nosuchnamespace");
    CoUninitialize();
    return 0;
}
