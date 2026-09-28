/*
 * The TaskScheduler type library as a system registers it: every type in it with its kind, GUID and flags, and
 * for each interface every function with its DISPID, invoke kind, flags, return type and parameters -- which is
 * what a script or an early-bound client sees through IDispatch.  Run with a path to a .tlb or a module to read
 * that one instead.
 */
#define COBJMACROS
#include <windows.h>
#include <oleauto.h>
#include <stdio.h>

static const GUID libid = {0xe34cb9f1,0xc7f7,0x424c,{0xbe,0x29,0x02,0x7d,0xcc,0x09,0x36,0x3a}};

static const char *vt_name(ITypeInfo *info, const TYPEDESC *desc, char *buf, size_t size)
{
    char inner[128];

    switch (desc->vt)
    {
    case VT_PTR:
        snprintf(buf, size, "%s*", vt_name(info, desc->lptdesc, inner, sizeof(inner)));
        return buf;
    case VT_SAFEARRAY:
        snprintf(buf, size, "SAFEARRAY(%s)", vt_name(info, desc->lptdesc, inner, sizeof(inner)));
        return buf;
    case VT_USERDEFINED:
    {
        ITypeInfo *ref;
        BSTR name = NULL;

        if (SUCCEEDED(ITypeInfo_GetRefTypeInfo(info, desc->hreftype, &ref)))
        {
            ITypeInfo_GetDocumentation(ref, MEMBERID_NIL, &name, NULL, NULL, NULL);
            ITypeInfo_Release(ref);
        }
        snprintf(buf, size, "%ls", name ? name : L"?");
        SysFreeString(name);
        return buf;
    }
    default:
        snprintf(buf, size, "vt%d", desc->vt);
        return buf;
    }
}

static void dump_funcs(ITypeInfo *info, const TYPEATTR *attr)
{
    unsigned int i, j;

    for (i = 0; i < attr->cFuncs; ++i)
    {
        BSTR names[16];
        UINT count = 0;
        FUNCDESC *func;
        char buf[128];

        if (FAILED(ITypeInfo_GetFuncDesc(info, i, &func))) continue;
        ITypeInfo_GetNames(info, func->memid, names, ARRAY_SIZE(names), &count);
        printf("    %ls memid %#lx invkind %d funckind %d flags %#x oVft %d ret %s", count ? names[0] : L"?",
                func->memid, func->invkind, func->funckind, func->wFuncFlags, func->oVft,
                vt_name(info, &func->elemdescFunc.tdesc, buf, sizeof(buf)));
        printf(" (");
        for (j = 0; j < func->cParams; ++j)
        {
            printf("%s%s %ls flags %#x", j ? ", " : "", vt_name(info, &func->lprgelemdescParam[j].tdesc, buf, sizeof(buf)),
                    j + 1 < count ? names[j + 1] : L"", func->lprgelemdescParam[j].paramdesc.wParamFlags);
        }
        printf(") optional %d\n", func->cParamsOpt);
        for (j = 0; j < count; ++j) SysFreeString(names[j]);
        ITypeInfo_ReleaseFuncDesc(info, func);
    }
}

static void dump_vars(ITypeInfo *info, const TYPEATTR *attr)
{
    unsigned int i;

    for (i = 0; i < attr->cVars; ++i)
    {
        VARDESC *var;
        BSTR name = NULL;

        if (FAILED(ITypeInfo_GetVarDesc(info, i, &var))) continue;
        ITypeInfo_GetDocumentation(info, var->memid, &name, NULL, NULL, NULL);
        printf("    %ls", name);
        if (var->varkind == VAR_CONST && var->lpvarValue) printf(" = %ld", V_I4(var->lpvarValue));
        printf("\n");
        SysFreeString(name);
        ITypeInfo_ReleaseVarDesc(info, var);
    }
}

int main(int argc, char **argv)
{
    ITypeLib *typelib;
    TLIBATTR *libattr;
    WCHAR path[MAX_PATH];
    UINT count, i, j;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1)
    {
        MultiByteToWideChar(CP_ACP, 0, argv[1], -1, path, ARRAY_SIZE(path));
        hr = LoadTypeLibEx(path, REGKIND_NONE, &typelib);
    }
    else hr = LoadRegTypeLib(&libid, 1, 0, LOCALE_NEUTRAL, &typelib);
    if (FAILED(hr))
    {
        printf("load %#lx\n", hr);
        return 1;
    }
    ITypeLib_GetLibAttr(typelib, &libattr);
    printf("library version %u.%u lcid %#lx syskind %d flags %#x\n", libattr->wMajorVerNum, libattr->wMinorVerNum,
            libattr->lcid, libattr->syskind, libattr->wLibFlags);
    ITypeLib_ReleaseTLibAttr(typelib, libattr);

    count = ITypeLib_GetTypeInfoCount(typelib);
    printf("%u types\n", count);
    for (i = 0; i < count; ++i)
    {
        ITypeInfo *info;
        TYPEATTR *attr;
        BSTR name = NULL;
        WCHAR guid[40];

        if (FAILED(ITypeLib_GetTypeInfo(typelib, i, &info))) continue;
        ITypeInfo_GetTypeAttr(info, &attr);
        ITypeLib_GetDocumentation(typelib, i, &name, NULL, NULL, NULL);
        StringFromGUID2(&attr->guid, guid, ARRAY_SIZE(guid));
        printf("%ls kind %d %ls flags %#x funcs %u vars %u impl %u vft %u\n", name, attr->typekind, guid,
                attr->wTypeFlags, attr->cFuncs, attr->cVars, attr->cImplTypes, attr->cbSizeVft);
        for (j = 0; j < attr->cImplTypes; ++j)
        {
            HREFTYPE ref;
            ITypeInfo *base;
            BSTR base_name = NULL;
            INT flags = 0;

            ITypeInfo_GetRefTypeOfImplType(info, j, &ref);
            ITypeInfo_GetImplTypeFlags(info, j, &flags);
            if (SUCCEEDED(ITypeInfo_GetRefTypeInfo(info, ref, &base)))
            {
                ITypeInfo_GetDocumentation(base, MEMBERID_NIL, &base_name, NULL, NULL, NULL);
                ITypeInfo_Release(base);
            }
            printf("  implements %ls flags %#x\n", base_name, flags);
            SysFreeString(base_name);
        }
        if (attr->typekind == TKIND_DISPATCH && (attr->wTypeFlags & TYPEFLAG_FDUAL))
        {
            /* the functions of a dual interface are those of its vtable part */
            HREFTYPE ref;
            ITypeInfo *vtbl;
            TYPEATTR *vattr;

            if (SUCCEEDED(ITypeInfo_GetRefTypeOfImplType(info, -1, &ref)) &&
                SUCCEEDED(ITypeInfo_GetRefTypeInfo(info, ref, &vtbl)))
            {
                ITypeInfo_GetTypeAttr(vtbl, &vattr);
                printf("  interface part: kind %d funcs %u vft %u\n", vattr->typekind, vattr->cFuncs, vattr->cbSizeVft);
                dump_funcs(vtbl, vattr);
                ITypeInfo_ReleaseTypeAttr(vtbl, vattr);
                ITypeInfo_Release(vtbl);
            }
        }
        else if (attr->typekind == TKIND_ENUM) dump_vars(info, attr);
        else dump_funcs(info, attr);
        SysFreeString(name);
        ITypeInfo_ReleaseTypeAttr(info, attr);
        ITypeInfo_Release(info);
    }
    ITypeLib_Release(typelib);
    printf("done\n");
    return 0;
}
