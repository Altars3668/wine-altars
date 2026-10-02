/* dispids: the DISPIDs SWbemObject's members have, whether it is an ISWbemObjectEx, and what its Path_ and
 * SystemProperties_ answer through the vtable, for a class and for an instance.  Prints results only. */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <wbemdisp.h>
#include <stdio.h>

static void show(const char *what, ISWbemObject *obj)
{
    static const WCHAR *names[] = { L"Put_", L"Path_", L"Security_", L"Refresh_", L"SystemProperties_",
                                    L"GetText_", L"SetFromText_", L"Properties_", L"Methods_" };
    ISWbemObjectEx *ex = NULL;
    ISWbemPropertySet *set;
    unsigned int i;
    HRESULT hr;

    printf("%s\n", what);
    for (i = 0; i < ARRAYSIZE(names); i++)
    {
        DISPID id = -1;
        LPOLESTR name = (LPOLESTR)names[i];
        hr = ISWbemObject_GetIDsOfNames(obj, &IID_NULL, &name, 1, 0, &id);
        printf("  %-18ls %#lx, dispid %ld\n", names[i], hr, id);
    }
    hr = ISWbemObject_QueryInterface(obj, &IID_ISWbemObjectEx, (void **)&ex);
    printf("  ISWbemObjectEx %#lx\n", hr);
    if (ex)
    {
        LONG count = -1;
        hr = ISWbemObjectEx_get_SystemProperties_(ex, &set);
        printf("  SystemProperties_ %#lx\n", hr);
        if (SUCCEEDED(hr))
        {
            hr = ISWbemPropertySet_get_Count(set, &count);
            printf("  count %#lx, %ld\n", hr, count);
            ISWbemPropertySet_Release(set);
        }
        ISWbemObjectEx_Release(ex);
    }
}

int main(void)
{
    ISWbemLocator *locator;
    ISWbemServices *services;
    ISWbemObject *obj;
    BSTR str, ns;
    HRESULT hr;

    CoInitialize(NULL);
    hr = CoCreateInstance(&CLSID_SWbemLocator, NULL, CLSCTX_INPROC_SERVER, &IID_ISWbemLocator, (void **)&locator);
    if (FAILED(hr)) { printf("locator %#lx\n", hr); return 1; }
    ns = SysAllocString(L"root\\cimv2");
    hr = ISWbemLocator_ConnectServer(locator, NULL, ns, NULL, NULL, NULL, NULL, 0, NULL, &services);
    if (FAILED(hr)) { printf("ConnectServer %#lx\n", hr); return 1; }
    str = SysAllocString(L"Win32_OperatingSystem");
    hr = ISWbemServices_Get(services, str, 0, NULL, &obj);
    printf("Get(Win32_OperatingSystem) %#lx\n", hr);
    if (SUCCEEDED(hr)) { show("the class", obj); ISWbemObject_Release(obj); }
    SysFreeString(str);
    str = SysAllocString(L"Win32_OperatingSystem=@");
    hr = ISWbemServices_Get(services, str, 0, NULL, &obj);
    printf("Get(Win32_OperatingSystem=@) %#lx\n", hr);
    if (SUCCEEDED(hr)) { show("the instance", obj); ISWbemObject_Release(obj); }
    SysFreeString(str);
    return 0;
}
