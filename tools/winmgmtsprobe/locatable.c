/* locatable: what IWbemServices::ExecQuery gives for a query that leaves the keys out, with and without
 * WBEM_FLAG_ENSURE_LOCATABLE: the paths, the property count and names, and whether the key is there; and the
 * namespace an instance's path has when the connection named it in capitals.  The queries are on the probe's own
 * process; the host name is printed as <host>, the process id as <pid>.  Prints results only. */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <wbemcli.h>
#include <stdio.h>
#include <wchar.h>

static WCHAR host[MAX_COMPUTERNAME_LENGTH + 1], pid[16];

static void print_scrubbed(const WCHAR *str)
{
    size_t len = wcslen(host), len_pid = wcslen(pid);
    const WCHAR *start = str;
    while (*str)
    {
        if (len && !_wcsnicmp(str, host, len))
        {
            printf("<host>");
            str += len;
        }
        /* the process id as a number of its own, not the digits of a name */
        else if (!wcsncmp(str, pid, len_pid) && !iswalnum(str[len_pid]) && (str == start || !iswalnum(str[-1])))
        {
            printf("<pid>");
            str += len_pid;
        }
        else printf("%lc", *str++);
    }
}

static void print_value(IWbemClassObject *obj, const WCHAR *name)
{
    VARIANT v;
    HRESULT hr;
    CIMTYPE type;

    VariantInit(&v);
    hr = IWbemClassObject_Get(obj, name, 0, &v, &type, NULL);
    printf("    %ls: ", name);
    if (hr != S_OK) printf("%#lx", hr);
    else if (V_VT(&v) == VT_NULL) printf("null");
    else if (V_VT(&v) == VT_BSTR) print_scrubbed(V_BSTR(&v));
    else if (V_VT(&v) == VT_I4) printf("%ld", V_I4(&v));
    else if (V_VT(&v) == VT_UI4) printf("%lu", V_UI4(&v));
    else printf("vt %#x", V_VT(&v));
    printf("\n");
    VariantClear(&v);
}

static void query(IWbemServices *services, const WCHAR *format, LONG flags, const WCHAR *key)
{
    WCHAR wql[256];
    BSTR language, text;
    IEnumWbemClassObject *result;
    IWbemClassObject *obj;
    SAFEARRAY *names;
    LONG i, lower, upper;
    ULONG count;
    HRESULT hr;

    swprintf(wql, ARRAYSIZE(wql), format, pid);
    language = SysAllocString(L"WQL");
    text = SysAllocString(wql);
    printf("%ls, flags %#lx\n", format, flags);
    hr = IWbemServices_ExecQuery(services, language, text, flags, NULL, &result);
    if (hr != S_OK)
    {
        printf("  ExecQuery %#lx\n", hr);
        return;
    }
    hr = IEnumWbemClassObject_Next(result, 10000, 1, &obj, &count);
    if (hr == S_OK)
    {
        print_value(obj, L"__PATH");
        print_value(obj, L"__RELPATH");
        print_value(obj, L"__SERVER");
        print_value(obj, L"__NAMESPACE");
        print_value(obj, L"__PROPERTY_COUNT");
        print_value(obj, key);
        if (IWbemClassObject_GetNames(obj, NULL, WBEM_FLAG_NONSYSTEM_ONLY, NULL, &names) == S_OK)
        {
            SafeArrayGetLBound(names, 1, &lower);
            SafeArrayGetUBound(names, 1, &upper);
            printf("    names:");
            for (i = lower; i <= upper; i++)
            {
                BSTR name;
                if (SafeArrayGetElement(names, &i, &name) == S_OK)
                {
                    printf(" %ls", name);
                    SysFreeString(name);
                }
            }
            printf("\n");
            SafeArrayDestroy(names);
        }
        IWbemClassObject_Release(obj);
    }
    else printf("  Next %#lx\n", hr);
    IEnumWbemClassObject_Release(result);
    SysFreeString(language);
    SysFreeString(text);
}

int main(void)
{
    IWbemLocator *locator;
    IWbemServices *services;
    DWORD size = ARRAYSIZE(host);
    BSTR ns;
    HRESULT hr;

    GetComputerNameW(host, &size);
    swprintf(pid, ARRAYSIZE(pid), L"%lu", GetCurrentProcessId());
    CoInitialize(NULL);
    CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, NULL,
                         EOAC_NONE, NULL);
    hr = CoCreateInstance(&CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER, &IID_IWbemLocator, (void **)&locator);
    if (FAILED(hr)) { printf("locator %#lx\n", hr); return 1; }
    ns = SysAllocString(L"root\\cimv2");
    hr = IWbemLocator_ConnectServer(locator, ns, NULL, NULL, NULL, 0, NULL, NULL, &services);
    if (FAILED(hr)) { printf("ConnectServer %#lx\n", hr); return 1; }

    query(services, L"SELECT Name FROM Win32_Process WHERE Handle = %ls", 0, L"Handle");
    query(services, L"SELECT Name FROM Win32_Process WHERE Handle = %ls", WBEM_FLAG_ENSURE_LOCATABLE, L"Handle");
    query(services, L"SELECT Name FROM Win32_Process WHERE Handle = %ls",
          WBEM_FLAG_RETURN_IMMEDIATELY | WBEM_FLAG_FORWARD_ONLY, L"Handle");
    query(services, L"SELECT Size FROM Win32_LogicalDisk WHERE DeviceID = 'C:'", 0, L"DeviceID");
    query(services, L"SELECT Size FROM Win32_LogicalDisk WHERE DeviceID = 'C:'", WBEM_FLAG_ENSURE_LOCATABLE,
          L"DeviceID");
    query(services, L"SELECT Caption FROM Win32_OperatingSystem", 0, L"Name");
    query(services, L"SELECT Caption FROM Win32_OperatingSystem", WBEM_FLAG_ENSURE_LOCATABLE, L"Name");
    query(services, L"SELECT Handle, Name FROM Win32_Process WHERE Handle = %ls", 0, L"Handle");
    query(services, L"SELECT * FROM Win32_Process WHERE Handle = %ls", 0, L"Handle");
    IWbemServices_Release(services);

    SysFreeString(ns);
    ns = SysAllocString(L"ROOT\\CIMV2");
    hr = IWbemLocator_ConnectServer(locator, ns, NULL, NULL, NULL, 0, NULL, NULL, &services);
    if (FAILED(hr)) { printf("ConnectServer ROOT\\CIMV2 %#lx\n", hr); return 1; }
    printf("connected to ROOT\\CIMV2\n");
    query(services, L"SELECT * FROM Win32_Process WHERE Handle = %ls", 0, L"Handle");
    query(services, L"SELECT * FROM Win32_LogicalDisk WHERE DeviceID = 'C:'", 0, L"DeviceID");
    IWbemServices_Release(services);
    IWbemLocator_Release(locator);
    CoUninitialize();
    return 0;
}
