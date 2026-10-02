/* pathprobe: what wmiutils' IWbemPath makes of object paths -- the info flags, the text in every form, the class,
 * and each key as IWbemPathKeyList gives it (name, value, apparent CIM type) through GetKey and GetKey2 -- and what
 * building a key list with SetKey, SetKey2, MakeSingleton and RemoveKey writes.  The paths are made up; the probe
 * prints results only. */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <wbemcli.h>
#include <wmiutils.h>
#include <stdio.h>

DEFINE_GUID(CLSID_WbemDefPath_, 0xcf4cc405, 0xe2c5, 0x4ddd, 0xb3, 0xce, 0x5e, 0x75, 0x82, 0xd8, 0xc9, 0xfa);
DEFINE_GUID(IID_IWbemPath_, 0x3bc15af2, 0x736c, 0x477e, 0x9e, 0x51, 0x23, 0x8a, 0xf8, 0x66, 0x7d, 0xcc);

static const WCHAR *paths[] =
{
    L"\\\\srv\\root\\cimv2:Win32_Process.Handle=\"4\"",
    L"Win32_LogicalDisk.DeviceID=\"C:\"",
    L"Win32_OperatingSystem=@",
    L"\\\\.\\root\\cimv2:Win32_OperatingSystem=@",
    L"Win32_LogicalDisk=\"C:\"",
    L"Class.A=5",
    L"Class.A=-5",
    L"Class.A=+5",
    L"Class.A=4294967296",
    L"Class.A=18446744073709551615",
    L"Class.A=-9223372036854775808",
    L"Class.A=0x10",
    L"Class.A=010",
    L"Class.A=1.5",
    L"Class.A=TRUE",
    L"Class.A=true",
    L"Class.A=\"x,y\",B=2",
    L"Class.A=\"q\\\"uote\"",
    L"Class.A=\"back\\\\slash\"",
    L"Class.A=\"tab\\tx\"",
    L"Class.A='single'",
    L"Class.A=\"\"",
    L"Class.A=\"unterminated",
    L"Class.A=5,B=\"b\",C=-1",
    L"Class.B=\"b\",A=5",
    L"Class.Ref=\"Other.K=\\\"v\\\"\"",
    L"Class.A = 5",
    L"Class.A=5 ",
    L"Class.=5",
    L"Class.A",
    L"Class.A=",
    L"Class=5",
    L"Class=@,A=5",
    L"Class.A=@",
    L"Class.A=\"v\",A=\"w\"",
    L"Class.A={1234}",
    L"root\\cimv2:Class",
    L"Class",
    L"\\\\srv\\root\\cimv2",
    L"\\\\srv\\root",
    L"\\\\srv",
    L"",
};

static void print_variant(const VARIANT *v)
{
    switch (V_VT(v))
    {
    case VT_EMPTY: printf("empty"); break;
    case VT_NULL: printf("null"); break;
    case VT_BSTR: printf("bstr [%ls]", V_BSTR(v)); break;
    case VT_I1: printf("i1 %d", V_I1(v)); break;
    case VT_UI1: printf("ui1 %u", V_UI1(v)); break;
    case VT_I2: printf("i2 %d", V_I2(v)); break;
    case VT_UI2: printf("ui2 %u", V_UI2(v)); break;
    case VT_I4: printf("i4 %ld", V_I4(v)); break;
    case VT_UI4: printf("ui4 %lu", V_UI4(v)); break;
    case VT_I8: printf("i8 %lld", V_I8(v)); break;
    case VT_UI8: printf("ui8 %llu", V_UI8(v)); break;
    case VT_R4: printf("r4 %g", V_R4(v)); break;
    case VT_R8: printf("r8 %g", V_R8(v)); break;
    case VT_BOOL: printf("bool %d", V_BOOL(v)); break;
    default: printf("vt %#x", V_VT(v)); break;
    }
}

static void print_bytes(const BYTE *p, ULONG len)
{
    ULONG i;
    for (i = 0; i < len && i < 48; i++) printf("%02x", p[i]);
    if (len > 48) printf("...");
}

static void show_text(IWbemPath *path)
{
    static const LONG flags[] = { 0, WBEMPATH_COMPRESSED, WBEMPATH_GET_RELATIVE_ONLY, WBEMPATH_GET_SERVER_TOO,
                                  WBEMPATH_GET_SERVER_AND_NAMESPACE_ONLY, WBEMPATH_GET_NAMESPACE_ONLY,
                                  WBEMPATH_GET_ORIGINAL };
    WCHAR buf[512];
    unsigned int i;
    ULONG len;
    HRESULT hr;

    for (i = 0; i < ARRAYSIZE(flags); i++)
    {
        len = ARRAYSIZE(buf);
        buf[0] = 0;
        hr = IWbemPath_GetText(path, flags[i], &len, buf);
        if (hr == S_OK) printf("  text %#lx: [%ls] len %lu\n", flags[i], buf, len);
        else printf("  text %#lx: %#lx\n", flags[i], hr);
    }
}

static void show_keylist_text(IWbemPathKeyList *keys)
{
    static const LONG flags[] = { 0, WBEMPATH_TEXT, WBEMPATH_QUOTEDTEXT };
    WCHAR buf[512];
    unsigned int i;
    ULONG len;
    HRESULT hr;

    for (i = 0; i < ARRAYSIZE(flags); i++)
    {
        len = ARRAYSIZE(buf);
        buf[0] = 0;
        hr = IWbemPathKeyList_GetText(keys, flags[i], &len, buf);
        if (hr == S_OK) printf("  keylist text %#lx: [%ls] len %lu\n", flags[i], buf, len);
        else printf("  keylist text %#lx: %#lx\n", flags[i], hr);
    }
}

static void show_key(IWbemPathKeyList *keys, ULONG index)
{
    static const ULONG flags[] = { 0, WBEMPATH_TEXT, WBEMPATH_QUOTEDTEXT };
    WCHAR name[128];
    BYTE value[512];
    ULONG name_len, value_len, type;
    unsigned int i;
    VARIANT var;
    HRESULT hr;

    /* sizes first */
    name_len = 0; type = 0xdead;
    VariantInit(&var);
    hr = IWbemPathKeyList_GetKey2(keys, index, 0, &name_len, NULL, &var, &type);
    printf("    key %lu GetKey2 sizes: %#lx name_len %lu type %#lx vt %#x\n", index, hr, name_len, type, V_VT(&var));
    VariantClear(&var);

    for (i = 0; i < ARRAYSIZE(flags); i++)
    {
        name_len = ARRAYSIZE(name); name[0] = 0; type = 0xdead;
        VariantInit(&var);
        hr = IWbemPathKeyList_GetKey2(keys, index, flags[i], &name_len, name, &var, &type);
        printf("    key %lu GetKey2 %#lx: %#lx [%ls] name_len %lu type %lu value ", index, flags[i], hr, name,
               name_len, type);
        print_variant(&var);
        printf("\n");
        VariantClear(&var);
    }

    name_len = ARRAYSIZE(name); value_len = 0; type = 0xdead;
    hr = IWbemPathKeyList_GetKey(keys, index, 0, &name_len, name, &value_len, NULL, &type);
    printf("    key %lu GetKey size: %#lx value_len %lu type %lu\n", index, hr, value_len, type);
    for (i = 0; i < ARRAYSIZE(flags); i++)
    {
        name_len = ARRAYSIZE(name); name[0] = 0; value_len = sizeof(value); type = 0xdead;
        memset(value, 0xcc, sizeof(value));
        hr = IWbemPathKeyList_GetKey(keys, index, flags[i], &name_len, name, &value_len, value, &type);
        printf("    key %lu GetKey %#lx: %#lx [%ls] name_len %lu type %lu value_len %lu ", index, flags[i], hr, name,
               name_len, type, value_len);
        if (hr == S_OK && flags[i]) printf("[%ls] ", (WCHAR *)value);
        if (hr == S_OK) print_bytes(value, value_len);
        printf("\n");
    }
}

static void show_path(IWbemPath *path, const WCHAR *what)
{
    IWbemPathKeyList *keys;
    ULONGLONG info;
    WCHAR buf[256];
    ULONG len, count, i;
    HRESULT hr;

    printf("%ls\n", what);
    info = 0;
    hr = IWbemPath_GetInfo(path, 0, &info);
    printf("  info %#lx %#llx\n", hr, info);
    show_text(path);
    len = ARRAYSIZE(buf); buf[0] = 0;
    hr = IWbemPath_GetServer(path, &len, buf);
    printf("  server %#lx [%ls]\n", hr, hr == S_OK ? buf : L"");
    count = 0xdead;
    hr = IWbemPath_GetNamespaceCount(path, &count);
    printf("  namespaces %#lx %lu\n", hr, count);
    len = ARRAYSIZE(buf); buf[0] = 0;
    hr = IWbemPath_GetClassName(path, &len, buf);
    printf("  class %#lx [%ls]\n", hr, hr == S_OK ? buf : L"");

    hr = IWbemPath_GetKeyList(path, &keys);
    printf("  GetKeyList %#lx\n", hr);
    if (FAILED(hr)) return;
    count = 0xdead;
    hr = IWbemPathKeyList_GetCount(keys, &count);
    printf("  keys %#lx %lu\n", hr, count);
    info = 0;
    hr = IWbemPathKeyList_GetInfo(keys, 0, &info);
    printf("  keylist info %#lx %#llx\n", hr, info);
    show_keylist_text(keys);
    if (count > 16) count = 0;
    for (i = 0; i <= count; i++) show_key(keys, i);
    IWbemPathKeyList_Release(keys);
}

static void build(IWbemPath *path)
{
    IWbemPathKeyList *keys;
    VARIANT var;
    LONG value = 5;
    HRESULT hr;

    printf("building on Class\n");
    hr = IWbemPath_SetText(path, WBEMPATH_CREATE_ACCEPT_ALL, L"Class");
    printf("  SetText %#lx\n", hr);
    if (FAILED(IWbemPath_GetKeyList(path, &keys))) return;

    V_VT(&var) = VT_I4; V_I4(&var) = 5;
    hr = IWbemPathKeyList_SetKey2(keys, L"A", 0, CIM_SINT32, &var);
    printf("  SetKey2 A i4 5 as sint32: %#lx\n", hr);
    show_path(path, L"after SetKey2 A");

    V_VT(&var) = VT_BSTR; V_BSTR(&var) = SysAllocString(L"x,y \"q\" \\");
    hr = IWbemPathKeyList_SetKey2(keys, L"B", 0, CIM_STRING, &var);
    printf("  SetKey2 B bstr as string: %#lx\n", hr);
    VariantClear(&var);
    V_VT(&var) = VT_BSTR; V_BSTR(&var) = SysAllocString(L"7");
    hr = IWbemPathKeyList_SetKey2(keys, L"C", 0, CIM_UINT32, &var);
    printf("  SetKey2 C bstr 7 as uint32: %#lx\n", hr);
    VariantClear(&var);
    V_VT(&var) = VT_BOOL; V_BOOL(&var) = VARIANT_TRUE;
    hr = IWbemPathKeyList_SetKey2(keys, L"D", 0, CIM_BOOLEAN, &var);
    printf("  SetKey2 D bool as boolean: %#lx\n", hr);
    hr = IWbemPathKeyList_SetKey(keys, L"E", 0, CIM_SINT32, &value);
    printf("  SetKey E sint32 5: %#lx\n", hr);
    hr = IWbemPathKeyList_SetKey(keys, L"F", 0, CIM_STRING, (void *)L"text");
    printf("  SetKey F string: %#lx\n", hr);
    V_VT(&var) = VT_I4; V_I4(&var) = 9;
    hr = IWbemPathKeyList_SetKey2(keys, L"A", 0, CIM_SINT32, &var);
    printf("  SetKey2 A again, 9: %#lx\n", hr);
    show_path(path, L"after the keys");

    hr = IWbemPathKeyList_RemoveKey(keys, L"B", 0);
    printf("  RemoveKey B: %#lx\n", hr);
    hr = IWbemPathKeyList_RemoveKey(keys, L"nonesuch", 0);
    printf("  RemoveKey nonesuch: %#lx\n", hr);
    show_path(path, L"after RemoveKey");

    hr = IWbemPathKeyList_MakeSingleton(keys, TRUE);
    printf("  MakeSingleton TRUE: %#lx\n", hr);
    show_path(path, L"after MakeSingleton TRUE");
    hr = IWbemPathKeyList_MakeSingleton(keys, FALSE);
    printf("  MakeSingleton FALSE: %#lx\n", hr);
    show_path(path, L"after MakeSingleton FALSE");

    V_VT(&var) = VT_BSTR; V_BSTR(&var) = SysAllocString(L"C:");
    hr = IWbemPathKeyList_SetKey2(keys, L"", 0, CIM_STRING, &var);
    printf("  SetKey2 unnamed C: as string: %#lx\n", hr);
    VariantClear(&var);
    show_path(path, L"after an unnamed key");

    hr = IWbemPathKeyList_RemoveAllKeys(keys, 0);
    printf("  RemoveAllKeys: %#lx\n", hr);
    show_path(path, L"after RemoveAllKeys");
    IWbemPathKeyList_Release(keys);
}

int main(void)
{
    IWbemPath *path;
    unsigned int i;
    HRESULT hr;

    CoInitialize(NULL);
    hr = CoCreateInstance(&CLSID_WbemDefPath_, NULL, CLSCTX_INPROC_SERVER, &IID_IWbemPath_, (void **)&path);
    if (FAILED(hr)) { printf("WbemDefPath %#lx\n", hr); return 1; }
    for (i = 0; i < ARRAYSIZE(paths); i++)
    {
        hr = IWbemPath_SetText(path, WBEMPATH_CREATE_ACCEPT_ALL, paths[i]);
        if (FAILED(hr))
        {
            printf("[%ls] SetText %#lx\n", paths[i], hr);
            continue;
        }
        show_path(path, paths[i]);
    }
    build(path);
    IWbemPath_Release(path);
    CoUninitialize();
    return 0;
}
