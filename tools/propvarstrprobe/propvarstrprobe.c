/* What the propsys conversions to a string give for the values Wine's tests do not cover.
 *
 * Wine's tests fix a few: TRUE as "1", 0.125f and 0.456 as written, a FILETIME and a DATE as
 * "yyyy/mm/dd:hh:mm:ss.mmm", the elements of a vector between "; ".  This prints what VariantToString,
 * PropVariantToString and PropVariantToBSTR answer for VARIANT_TRUE and FALSE, large, small and odd
 * doubles, a float that is not exact, a DATE with milliseconds, CY, DECIMAL, ERROR, a SAFEARRAY of bytes
 * (which Excel hands VariantToString at startup) and of strings, and vectors of several types.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror propvarstrprobe.c -o propvarstrprobe.exe \
 *       -lpropsys -loleaut32 -lole32
 */
#define COBJMACROS
#include <windows.h>
#include <propvarutil.h>
#include <stdio.h>
#include <math.h>

static void show(const char *what, HRESULT hr, const WCHAR *str)
{
    printf("%-32s %#010lx", what, (unsigned long)hr);
    if (SUCCEEDED(hr) && str)
    {
        printf(" \"");
        for (; *str; str++)
            if (*str >= 32 && *str < 127) printf("%c", (char)*str);
            else printf("\\x%04x", *str);
        printf("\"");
    }
    printf("\n");
}

static void variant(const char *what, VARIANT *var)
{
    WCHAR buffer[256];
    char name[64];
    HRESULT hr;

    buffer[0] = 0x2222;
    hr = VariantToString(var, buffer, ARRAYSIZE(buffer));
    sprintf(name, "VariantToString %s", what);
    show(name, hr, buffer);
}

static void propvariant(const char *what, PROPVARIANT *propvar)
{
    WCHAR buffer[256];
    char name[64];
    HRESULT hr;
    BSTR bstr = NULL;

    buffer[0] = 0x2222;
    hr = PropVariantToString(propvar, buffer, ARRAYSIZE(buffer));
    sprintf(name, "PropVariantToString %s", what);
    show(name, hr, buffer);
    hr = PropVariantToBSTR(propvar, &bstr);
    sprintf(name, "PropVariantToBSTR %s", what);
    show(name, hr, bstr);
    SysFreeString(bstr);
}

int main(void)
{
    static const double doubles[] = {1e20, 1e-7, 123456789012345678.0, -0.0, 0.1 + 0.2, 1.0 / 3.0, 12345.678};
    static const char *double_names[] = {"1e20", "1e-7", "123456789012345678", "-0", "0.1+0.2", "1/3", "12345.678"};
    static BYTE bytes[] = {1, 20, 30, 4};
    static VARIANT_BOOL bools[] = {VARIANT_TRUE, VARIANT_FALSE, 1};
    static double dbls[] = {0.5, 2.25};
    static WCHAR a[] = L"a", b[] = L"b c";
    static WCHAR *strs[] = {a, b};
    SAFEARRAYBOUND bound = {4, 0};
    PROPVARIANT propvar;
    VARIANT var;
    unsigned int i;
    void *data;

    VariantInit(&var);
    V_VT(&var) = VT_BOOL; V_BOOL(&var) = VARIANT_TRUE; variant("VT_BOOL VARIANT_TRUE", &var);
    V_BOOL(&var) = VARIANT_FALSE; variant("VT_BOOL VARIANT_FALSE", &var);
    V_VT(&var) = VT_R4; V_R4(&var) = 0.1f; variant("VT_R4 0.1f", &var);
    for (i = 0; i < ARRAYSIZE(doubles); i++)
    {
        char name[48];
        V_VT(&var) = VT_R8; V_R8(&var) = doubles[i];
        sprintf(name, "VT_R8 %s", double_names[i]);
        variant(name, &var);
    }
    V_VT(&var) = VT_R8; V_R8(&var) = NAN; variant("VT_R8 NaN", &var);
    V_VT(&var) = VT_DATE; V_DATE(&var) = 45000.5 + 1.5 / 86400.0; variant("VT_DATE with .5s", &var);
    V_VT(&var) = VT_DATE; V_DATE(&var) = -1.25; variant("VT_DATE -1.25", &var);
    V_VT(&var) = VT_CY; V_CY(&var).int64 = 123456; variant("VT_CY 12.3456", &var);
    V_VT(&var) = VT_DECIMAL; VarDecFromR8(1.5, &V_DECIMAL(&var)); V_VT(&var) = VT_DECIMAL; variant("VT_DECIMAL 1.5", &var);
    V_VT(&var) = VT_ERROR; V_ERROR(&var) = E_FAIL; variant("VT_ERROR E_FAIL", &var);
    V_VT(&var) = VT_NULL; variant("VT_NULL", &var);

    V_VT(&var) = VT_ARRAY | VT_UI1;
    V_ARRAY(&var) = SafeArrayCreate(VT_UI1, 1, &bound);
    SafeArrayAccessData(V_ARRAY(&var), &data);
    memcpy(data, bytes, sizeof(bytes));
    SafeArrayUnaccessData(V_ARRAY(&var));
    variant("VT_ARRAY|VT_UI1", &var);
    PropVariantInit(&propvar);
    VariantToPropVariant(&var, &propvar);
    printf("VariantToPropVariant VT_ARRAY|VT_UI1 gives vt %#x\n", propvar.vt);
    propvariant("from that", &propvar);
    PropVariantClear(&propvar);
    VariantClear(&var);

    V_VT(&var) = VT_ARRAY | VT_BSTR;
    bound.cElements = 2;
    V_ARRAY(&var) = SafeArrayCreate(VT_BSTR, 1, &bound);
    for (i = 0; i < 2; i++)
    {
        LONG index = i;
        BSTR str = SysAllocString(strs[i]);
        SafeArrayPutElement(V_ARRAY(&var), &index, str);
        SysFreeString(str);
    }
    variant("VT_ARRAY|VT_BSTR", &var);
    VariantClear(&var);

    PropVariantInit(&propvar);
    propvar.vt = VT_BOOL; propvar.boolVal = VARIANT_TRUE; propvariant("VT_BOOL VARIANT_TRUE", &propvar);
    propvar.vt = VT_R8; propvar.dblVal = 1e20; propvariant("VT_R8 1e20", &propvar);
    propvar.vt = VT_R8; propvar.dblVal = 1e-7; propvariant("VT_R8 1e-7", &propvar);
    propvar.vt = VT_R4; propvar.fltVal = 0.1f; propvariant("VT_R4 0.1f", &propvar);
    propvar.vt = VT_DATE; propvar.date = 45000.5 + 1.5 / 86400.0; propvariant("VT_DATE with .5s", &propvar);
    propvar.vt = VT_CY; propvar.cyVal.int64 = 123456; propvariant("VT_CY 12.3456", &propvar);
    propvar.vt = VT_ERROR; propvar.scode = E_FAIL; propvariant("VT_ERROR E_FAIL", &propvar);
    propvar.vt = VT_VECTOR | VT_UI1; propvar.caub.cElems = 0; propvar.caub.pElems = bytes;
    propvariant("empty VT_VECTOR|VT_UI1", &propvar);
    propvar.caub.cElems = ARRAYSIZE(bytes); propvariant("VT_VECTOR|VT_UI1", &propvar);
    propvar.vt = VT_VECTOR | VT_BOOL; propvar.cabool.cElems = ARRAYSIZE(bools); propvar.cabool.pElems = bools;
    propvariant("VT_VECTOR|VT_BOOL", &propvar);
    propvar.vt = VT_VECTOR | VT_R8; propvar.cadbl.cElems = ARRAYSIZE(dbls); propvar.cadbl.pElems = dbls;
    propvariant("VT_VECTOR|VT_R8", &propvar);
    propvar.vt = VT_VECTOR | VT_LPWSTR; propvar.calpwstr.cElems = ARRAYSIZE(strs); propvar.calpwstr.pElems = strs;
    propvariant("VT_VECTOR|VT_LPWSTR", &propvar);
    return 0;
}
