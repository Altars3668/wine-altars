/* contextprobe: what an IWbemContext (CLSID_WbemContext) does with names -- their case, their order in GetNames and
 * in an enumeration, what deleting a missing one says, Clone, DeleteAll, and the flags each method takes.  Prints
 * results only. */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <wbemcli.h>
#include <stdio.h>

DEFINE_GUID(CLSID_WbemContext_, 0x674b6698, 0xee92, 0x11d0, 0xad, 0x71, 0x00, 0xc0, 0x4f, 0xd8, 0xfd, 0xff);
DEFINE_GUID(IID_IWbemContext_, 0x44aca674, 0xe8fc, 0x11d0, 0xa0, 0x7c, 0x00, 0xc0, 0x4f, 0xb6, 0x88, 0x20);

static void print_variant(const VARIANT *v)
{
    switch (V_VT(v))
    {
    case VT_EMPTY: printf("empty"); break;
    case VT_BSTR: printf("bstr [%ls]", V_BSTR(v)); break;
    case VT_I2: printf("i2 %d", V_I2(v)); break;
    case VT_I4: printf("i4 %ld", V_I4(v)); break;
    case VT_BOOL: printf("bool %d", V_BOOL(v)); break;
    default: printf("vt %#x", V_VT(v)); break;
    }
}

static void names(IWbemContext *ctx, LONG flags)
{
    SAFEARRAY *array = (SAFEARRAY *)0xdeadbeef;
    LONG lower, upper, i;
    HRESULT hr;
    BSTR name;

    hr = IWbemContext_GetNames(ctx, flags, &array);
    printf("  GetNames(%#lx) %#lx", flags, hr);
    if (hr == S_OK)
    {
        if (!array) printf(" NULL");
        else
        {
            SafeArrayGetLBound(array, 1, &lower);
            SafeArrayGetUBound(array, 1, &upper);
            printf(" [%ld..%ld] vt", lower, upper);
            {
                VARTYPE vt = 0;
                SafeArrayGetVartype(array, &vt);
                printf(" %#x:", vt);
            }
            for (i = lower; i <= upper; i++)
            {
                if (SafeArrayGetElement(array, &i, &name) == S_OK)
                {
                    printf(" %ls", name);
                    SysFreeString(name);
                }
            }
            SafeArrayDestroy(array);
        }
    }
    printf("\n");
}

static void enumerate(IWbemContext *ctx)
{
    VARIANT value;
    HRESULT hr;
    BSTR name;
    int i;

    hr = IWbemContext_BeginEnumeration(ctx, 0);
    printf("  BeginEnumeration %#lx\n", hr);
    for (i = 0; i < 8; i++)
    {
        name = NULL;
        VariantInit(&value);
        hr = IWbemContext_Next(ctx, 0, &name, &value);
        printf("    Next %#lx", hr);
        if (hr == S_OK)
        {
            printf(" [%ls] ", name);
            print_variant(&value);
        }
        printf("\n");
        SysFreeString(name);
        VariantClear(&value);
        if (hr != S_OK) break;
    }
    hr = IWbemContext_EndEnumeration(ctx);
    printf("  EndEnumeration %#lx\n", hr);
    name = NULL;
    VariantInit(&value);
    hr = IWbemContext_Next(ctx, 0, &name, &value);
    printf("  Next after the end %#lx\n", hr);
    SysFreeString(name);
    VariantClear(&value);
}

static void set(IWbemContext *ctx, const WCHAR *name, VARIANT *value)
{
    HRESULT hr = IWbemContext_SetValue(ctx, name, 0, value);
    printf("  SetValue %ls %#lx\n", name, hr);
}

int main(void)
{
    IWbemContext *ctx, *clone;
    VARIANT value;
    HRESULT hr;
    BSTR name;

    CoInitialize(NULL);
    hr = CoCreateInstance(&CLSID_WbemContext_, NULL, CLSCTX_INPROC_SERVER, &IID_IWbemContext_, (void **)&ctx);
    if (FAILED(hr)) { printf("WbemContext %#lx\n", hr); return 1; }

    printf("empty\n");
    names(ctx, 0);
    enumerate(ctx);
    name = NULL;
    VariantInit(&value);
    hr = IWbemContext_Next(ctx, 0, &name, &value);
    printf("  Next with no enumeration %#lx\n", hr);

    printf("values\n");
    V_VT(&value) = VT_I4; V_I4(&value) = 1;
    set(ctx, L"B", &value);
    V_VT(&value) = VT_BSTR; V_BSTR(&value) = SysAllocString(L"x");
    set(ctx, L"a", &value);
    VariantClear(&value);
    V_VT(&value) = VT_BOOL; V_BOOL(&value) = VARIANT_TRUE;
    set(ctx, L"C", &value);
    V_VT(&value) = VT_I2; V_I2(&value) = 2;
    set(ctx, L"b", &value);
    hr = IWbemContext_SetValue(ctx, L"D", 1, &value);
    printf("  SetValue D flags 1 %#lx\n", hr);
    names(ctx, 0);
    names(ctx, 1);
    enumerate(ctx);
    hr = IWbemContext_BeginEnumeration(ctx, 1);
    printf("  BeginEnumeration(1) %#lx\n", hr);
    IWbemContext_EndEnumeration(ctx);

    VariantInit(&value);
    hr = IWbemContext_GetValue(ctx, L"A", 0, &value);
    printf("  GetValue A %#lx ", hr);
    print_variant(&value);
    printf("\n");
    VariantClear(&value);
    hr = IWbemContext_GetValue(ctx, L"nonesuch", 0, &value);
    printf("  GetValue nonesuch %#lx\n", hr);
    hr = IWbemContext_GetValue(ctx, L"a", 1, &value);
    printf("  GetValue a flags 1 %#lx\n", hr);
    VariantClear(&value);

    hr = IWbemContext_DeleteValue(ctx, L"nonesuch", 0);
    printf("  DeleteValue nonesuch %#lx\n", hr);
    hr = IWbemContext_DeleteValue(ctx, L"A", 0);
    printf("  DeleteValue A %#lx\n", hr);
    hr = IWbemContext_DeleteValue(ctx, L"C", 1);
    printf("  DeleteValue C flags 1 %#lx\n", hr);
    names(ctx, 0);

    hr = IWbemContext_Clone(ctx, &clone);
    printf("  Clone %#lx\n", hr);
    if (hr == S_OK)
    {
        names(clone, 0);
        IWbemContext_Release(clone);
    }

    hr = IWbemContext_DeleteAll(ctx);
    printf("  DeleteAll %#lx\n", hr);
    names(ctx, 0);
    enumerate(ctx);

    IWbemContext_Release(ctx);
    CoUninitialize();
    return 0;
}
