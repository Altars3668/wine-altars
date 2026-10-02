/* classinfo: what each WbemScripting object says it is -- the name of the type IDispatch::GetTypeInfo gives, and the
 * class IProvideClassInfo gives, which is what VBScript's TypeName prints when there is one.  Prints results only. */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <ocidl.h>
#include <wbemdisp.h>
#include <stdio.h>

static void show(const char *what, IUnknown *unk)
{
    IProvideClassInfo *provide;
    IDispatch *disp;
    ITypeInfo *info;
    BSTR name;
    HRESULT hr;

    printf("%-28s", what);
    if (!unk)
    {
        printf(" (none)\n");
        return;
    }
    hr = IUnknown_QueryInterface(unk, &IID_IDispatch, (void **)&disp);
    if (hr == S_OK)
    {
        hr = IDispatch_GetTypeInfo(disp, 0, 0, &info);
        if (hr == S_OK)
        {
            hr = ITypeInfo_GetDocumentation(info, MEMBERID_NIL, &name, NULL, NULL, NULL);
            printf(" typeinfo %ls", hr == S_OK ? name : L"?");
            if (hr == S_OK) SysFreeString(name);
            ITypeInfo_Release(info);
        }
        else printf(" typeinfo %#lx", hr);
        IDispatch_Release(disp);
    }
    else printf(" IDispatch %#lx", hr);

    hr = IUnknown_QueryInterface(unk, &IID_IProvideClassInfo, (void **)&provide);
    if (hr == S_OK)
    {
        hr = IProvideClassInfo_GetClassInfo(provide, &info);
        if (hr == S_OK)
        {
            hr = ITypeInfo_GetDocumentation(info, MEMBERID_NIL, &name, NULL, NULL, NULL);
            printf(", class %ls", hr == S_OK ? name : L"?");
            if (hr == S_OK) SysFreeString(name);
            ITypeInfo_Release(info);
        }
        else printf(", GetClassInfo %#lx", hr);
        IProvideClassInfo_Release(provide);
    }
    else printf(", IProvideClassInfo %#lx", hr);
    printf("\n");
}

static IUnknown *first(IUnknown *collection)
{
    IEnumVARIANT *items;
    IDispatch *disp;
    IUnknown *unk = NULL;
    DISPPARAMS params = { 0 };
    VARIANT result, item;
    HRESULT hr;

    if (FAILED(IUnknown_QueryInterface(collection, &IID_IDispatch, (void **)&disp))) return NULL;
    VariantInit(&result);
    hr = IDispatch_Invoke(disp, DISPID_NEWENUM, &IID_NULL, 0, DISPATCH_PROPERTYGET | DISPATCH_METHOD, &params,
                          &result, NULL, NULL);
    IDispatch_Release(disp);
    if (hr != S_OK || V_VT(&result) != VT_UNKNOWN) return NULL;
    hr = IUnknown_QueryInterface(V_UNKNOWN(&result), &IID_IEnumVARIANT, (void **)&items);
    VariantClear(&result);
    if (hr != S_OK) return NULL;
    VariantInit(&item);
    if (IEnumVARIANT_Next(items, 1, &item, NULL) == S_OK && V_VT(&item) == VT_DISPATCH)
        IDispatch_QueryInterface(V_DISPATCH(&item), &IID_IUnknown, (void **)&unk);
    VariantClear(&item);
    IEnumVARIANT_Release(items);
    return unk;
}

int main(void)
{
    ISWbemLocator *locator;
    ISWbemServices *services;
    ISWbemSecurity *security;
    ISWbemPrivilegeSet *privileges;
    ISWbemPrivilege *privilege;
    ISWbemObject *object, *class_object;
    ISWbemObjectSet *objects;
    ISWbemObjectPath *path;
    ISWbemPropertySet *props;
    ISWbemMethodSet *methods;
    ISWbemQualifierSet *qualifiers;
    ISWbemNamedValueSet *values;
    ISWbemNamedValue *value;
    IUnknown *unk;
    VARIANT var;
    BSTR str, lang;
    HRESULT hr;

    CoInitialize(NULL);
    hr = CoCreateInstance(&CLSID_SWbemLocator, NULL, CLSCTX_INPROC_SERVER, &IID_ISWbemLocator, (void **)&locator);
    if (FAILED(hr)) { printf("locator %#lx\n", hr); return 1; }
    show("SWbemLocator", (IUnknown *)locator);
    if (ISWbemLocator_get_Security_(locator, &security) == S_OK)
    {
        show("locator Security_", (IUnknown *)security);
        ISWbemSecurity_Release(security);
    }
    str = SysAllocString(L"root\\cimv2");
    hr = ISWbemLocator_ConnectServer(locator, NULL, str, NULL, NULL, NULL, NULL, 0, NULL, &services);
    SysFreeString(str);
    if (FAILED(hr)) { printf("ConnectServer %#lx\n", hr); return 1; }
    show("services", (IUnknown *)services);
    if (ISWbemServices_get_Security_(services, &security) == S_OK)
    {
        show("services Security_", (IUnknown *)security);
        if (ISWbemSecurity_get_Privileges(security, &privileges) == S_OK)
        {
            show("Privileges", (IUnknown *)privileges);
            if (ISWbemPrivilegeSet_Add(privileges, 19, VARIANT_TRUE, &privilege) == S_OK)
            {
                show("a privilege", (IUnknown *)privilege);
                ISWbemPrivilege_Release(privilege);
            }
            ISWbemPrivilegeSet_Release(privileges);
        }
        ISWbemSecurity_Release(security);
    }

    str = SysAllocString(L"Win32_LogicalDisk.DeviceID=\"C:\"");
    hr = ISWbemServices_Get(services, str, 0, NULL, &object);
    SysFreeString(str);
    if (hr == S_OK)
    {
        show("an instance", (IUnknown *)object);
        if (ISWbemObject_get_Path_(object, &path) == S_OK)
        {
            show("Path_", (IUnknown *)path);
            if (ISWbemObjectPath_get_Keys(path, &values) == S_OK)
            {
                show("Path_.Keys", (IUnknown *)values);
                unk = first((IUnknown *)values);
                show("a key", unk);
                if (unk) IUnknown_Release(unk);
                ISWbemNamedValueSet_Release(values);
            }
            ISWbemObjectPath_Release(path);
        }
        if (ISWbemObject_get_Properties_(object, &props) == S_OK)
        {
            show("Properties_", (IUnknown *)props);
            unk = first((IUnknown *)props);
            show("a property", unk);
            if (unk) IUnknown_Release(unk);
            ISWbemPropertySet_Release(props);
        }
        if (ISWbemObject_get_Methods_(object, &methods) == S_OK)
        {
            show("Methods_", (IUnknown *)methods);
            unk = first((IUnknown *)methods);
            show("a method", unk);
            if (unk) IUnknown_Release(unk);
            ISWbemMethodSet_Release(methods);
        }
        if (ISWbemObject_get_Qualifiers_(object, &qualifiers) == S_OK)
        {
            show("Qualifiers_", (IUnknown *)qualifiers);
            unk = first((IUnknown *)qualifiers);
            show("a qualifier", unk);
            if (unk) IUnknown_Release(unk);
            ISWbemQualifierSet_Release(qualifiers);
        }
        if (ISWbemObject_get_Security_(object, &security) == S_OK)
        {
            show("object Security_", (IUnknown *)security);
            ISWbemSecurity_Release(security);
        }
        ISWbemObject_Release(object);
    }
    str = SysAllocString(L"Win32_LogicalDisk");
    if (ISWbemServices_Get(services, str, 0, NULL, &class_object) == S_OK)
    {
        show("a class", (IUnknown *)class_object);
        ISWbemObject_Release(class_object);
    }
    SysFreeString(str);

    str = SysAllocString(L"SELECT * FROM Win32_LogicalDisk");
    lang = SysAllocString(L"WQL");
    if (ISWbemServices_ExecQuery(services, str, lang, 0, NULL, &objects) == S_OK)
    {
        show("ExecQuery's set", (IUnknown *)objects);
        unk = first((IUnknown *)objects);
        show("its first object", unk);
        if (unk) IUnknown_Release(unk);
        ISWbemObjectSet_Release(objects);
    }
    SysFreeString(str);
    SysFreeString(lang);

    if (CoCreateInstance(&CLSID_SWbemNamedValueSet, NULL, CLSCTX_INPROC_SERVER, &IID_ISWbemNamedValueSet,
                         (void **)&values) == S_OK)
    {
        show("SWbemNamedValueSet", (IUnknown *)values);
        str = SysAllocString(L"x");
        V_VT(&var) = VT_I4;
        V_I4(&var) = 1;
        if (ISWbemNamedValueSet_Add(values, str, &var, 0, &value) == S_OK)
        {
            show("a named value", (IUnknown *)value);
            ISWbemNamedValue_Release(value);
        }
        SysFreeString(str);
        ISWbemNamedValueSet_Release(values);
    }
    if (CoCreateInstance(&CLSID_SWbemObjectPath, NULL, CLSCTX_INPROC_SERVER, &IID_ISWbemObjectPath,
                         (void **)&path) == S_OK)
    {
        show("SWbemObjectPath", (IUnknown *)path);
        ISWbemObjectPath_Release(path);
    }

    ISWbemServices_Release(services);
    ISWbemLocator_Release(locator);
    CoUninitialize();
    return 0;
}
