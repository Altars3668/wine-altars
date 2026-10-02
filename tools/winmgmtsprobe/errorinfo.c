/* errorinfo: what a failing WbemScripting call leaves for VBScript's Err -- ISupportErrorInfo, and the IErrorInfo's
 * description, source, GUID and help -- next to what wmiutils' IWbemStatusCodeText says of the same codes; and the
 * DISPIDs of ISWbemServicesEx.  Non-ASCII text is printed as \uXXXX.  Prints results only. */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <oaidl.h>
#include <wbemdisp.h>
#include <wbemcli.h>
#include <stdio.h>

DEFINE_GUID(CLSID_WbemStatusCodeText_, 0xeb87e1bd, 0x3233, 0x11d2, 0xae, 0xc9, 0x00, 0xc0, 0x4f, 0xb6, 0x88, 0x20);
DEFINE_GUID(IID_IWbemStatusCodeText_, 0xeb87e1bc, 0x3233, 0x11d2, 0xae, 0xc9, 0x00, 0xc0, 0x4f, 0xb6, 0x88, 0x20);
DEFINE_GUID(IID_ISWbemServicesEx_, 0xd2f68443, 0x85dc, 0x427e, 0x91, 0xd8, 0x36, 0x65, 0x54, 0xcc, 0x75, 0x4c);

static void print_w(const WCHAR *str)
{
    if (!str)
    {
        printf("(null)");
        return;
    }
    for (; *str; str++)
    {
        if (*str >= 0x20 && *str < 0x7f) printf("%c", (char)*str);
        else if (*str == '\r') printf("\\r");
        else if (*str == '\n') printf("\\n");
        else printf("\\u%04x", *str);
    }
}

static void show_error(const char *what, HRESULT hr, IUnknown *obj, REFIID iid)
{
    ISupportErrorInfo *support;
    IErrorInfo *info;
    BSTR str;
    GUID guid;
    DWORD context;
    HRESULT hr2;

    printf("%s: %#lx\n", what, hr);
    if (obj && SUCCEEDED(IUnknown_QueryInterface(obj, &IID_ISupportErrorInfo, (void **)&support)))
    {
        printf("  ISupportErrorInfo: %#lx\n", ISupportErrorInfo_InterfaceSupportsErrorInfo(support, iid));
        ISupportErrorInfo_Release(support);
    }
    else if (obj) printf("  no ISupportErrorInfo\n");
    hr2 = GetErrorInfo(0, &info);
    printf("  GetErrorInfo %#lx\n", hr2);
    if (hr2 != S_OK) return;
    if (SUCCEEDED(IErrorInfo_GetDescription(info, &str))) { printf("  description ["); print_w(str); printf("]\n"); SysFreeString(str); }
    if (SUCCEEDED(IErrorInfo_GetSource(info, &str))) { printf("  source ["); print_w(str); printf("]\n"); SysFreeString(str); }
    if (SUCCEEDED(IErrorInfo_GetGUID(info, &guid)))
        printf("  guid {%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}\n", guid.Data1, guid.Data2, guid.Data3,
               guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3], guid.Data4[4], guid.Data4[5],
               guid.Data4[6], guid.Data4[7]);
    if (SUCCEEDED(IErrorInfo_GetHelpFile(info, &str))) { printf("  help file ["); print_w(str); printf("]\n"); SysFreeString(str); }
    if (SUCCEEDED(IErrorInfo_GetHelpContext(info, &context))) printf("  help context %lu\n", context);
    IErrorInfo_Release(info);
}

int main(void)
{
    static const WCHAR *names[] = { L"Get", L"GetAsync", L"Delete", L"DeleteAsync", L"InstancesOf",
        L"InstancesOfAsync", L"SubclassesOf", L"SubclassesOfAsync", L"ExecQuery", L"ExecQueryAsync",
        L"AssociatorsOf", L"AssociatorsOfAsync", L"ReferencesTo", L"ReferencesToAsync", L"ExecNotificationQuery",
        L"ExecNotificationQueryAsync", L"ExecMethod", L"ExecMethodAsync", L"Security_", L"Put", L"PutAsync" };
    static const HRESULT codes[] = { WBEM_E_FAILED, WBEM_E_NOT_FOUND, WBEM_E_INVALID_PARAMETER, WBEM_E_INVALID_CLASS,
        WBEM_E_INVALID_QUERY, WBEM_E_READ_ONLY, WBEM_E_INVALID_NAMESPACE, WBEM_E_ACCESS_DENIED, WBEM_E_NOT_SUPPORTED,
        WBEM_E_INVALID_OBJECT_PATH, WBEM_E_TYPE_MISMATCH, E_INVALIDARG, E_OUTOFMEMORY, 0x80070005 };
    ISWbemLocator *locator;
    ISWbemServices *services;
    IUnknown *ex;
    ISWbemObject *object;
    ISWbemObjectPath *path;
    ISWbemObjectSet *objects;
    IWbemStatusCodeText *text;
    unsigned int i;
    DISPID id;
    BSTR str, lang;
    HRESULT hr;

    CoInitialize(NULL);
    hr = CoCreateInstance(&CLSID_SWbemLocator, NULL, CLSCTX_INPROC_SERVER, &IID_ISWbemLocator, (void **)&locator);
    if (FAILED(hr)) { printf("locator %#lx\n", hr); return 1; }
    str = SysAllocString(L"root\\cimv2");
    hr = ISWbemLocator_ConnectServer(locator, NULL, str, NULL, NULL, NULL, NULL, 0, NULL, &services);
    SysFreeString(str);
    if (FAILED(hr)) { printf("ConnectServer %#lx\n", hr); return 1; }

    hr = ISWbemServices_QueryInterface(services, &IID_ISWbemServicesEx_, (void **)&ex);
    printf("ISWbemServicesEx %#lx\n", hr);
    if (hr == S_OK) IUnknown_Release(ex);
    for (i = 0; i < ARRAYSIZE(names); i++)
    {
        LPOLESTR name = (LPOLESTR)names[i];
        id = -1;
        hr = ISWbemServices_GetIDsOfNames(services, &IID_NULL, &name, 1, 0, &id);
        printf("  %-28ls %#lx, dispid %ld\n", names[i], hr, id);
    }

    str = SysAllocString(L"Win32_NoSuchClass");
    hr = ISWbemServices_Get(services, str, 0, NULL, &object);
    show_error("Get(Win32_NoSuchClass)", hr, (IUnknown *)services, &IID_ISWbemServices);
    SysFreeString(str);
    str = SysAllocString(L"Win32_LogicalDisk.DeviceID=\"nonesuch\"");
    hr = ISWbemServices_Get(services, str, 0, NULL, &object);
    show_error("Get(Win32_LogicalDisk.DeviceID=\"nonesuch\")", hr, (IUnknown *)services, &IID_ISWbemServices);
    SysFreeString(str);
    str = SysAllocString(L"SELEC * FROM Win32_LogicalDisk");
    lang = SysAllocString(L"WQL");
    hr = ISWbemServices_ExecQuery(services, str, lang, 0, NULL, &objects);
    show_error("ExecQuery(SELEC, flags 0)", hr, (IUnknown *)services, &IID_ISWbemServices);
    if (hr == S_OK) ISWbemObjectSet_Release(objects);
    SysFreeString(str);
    SysFreeString(lang);

    hr = CoCreateInstance(&CLSID_SWbemObjectPath, NULL, CLSCTX_INPROC_SERVER, &IID_ISWbemObjectPath, (void **)&path);
    if (hr == S_OK)
    {
        str = SysAllocString(L"Class.A = 5");
        hr = ISWbemObjectPath_put_Path(path, str);
        show_error("SWbemObjectPath.Path = \"Class.A = 5\"", hr, (IUnknown *)path, &IID_ISWbemObjectPath);
        SysFreeString(str);
        ISWbemObjectPath_Release(path);
    }

    hr = CoCreateInstance(&CLSID_WbemStatusCodeText_, NULL, CLSCTX_INPROC_SERVER, &IID_IWbemStatusCodeText_,
                          (void **)&text);
    printf("WbemStatusCodeText %#lx\n", hr);
    if (hr == S_OK)
    {
        for (i = 0; i < ARRAYSIZE(codes); i++)
        {
            str = NULL;
            hr = IWbemStatusCodeText_GetErrorCodeText(text, codes[i], 0, 0, &str);
            printf("  %#lx: %#lx [", codes[i], hr);
            print_w(str);
            SysFreeString(str);
            str = NULL;
            hr = IWbemStatusCodeText_GetFacilityCodeText(text, codes[i], 0, 0, &str);
            printf("] facility %#lx [", hr);
            print_w(str);
            printf("]\n");
            SysFreeString(str);
        }
        IWbemStatusCodeText_Release(text);
    }

    ISWbemServices_Release(services);
    ISWbemLocator_Release(locator);
    CoUninitialize();
    return 0;
}
