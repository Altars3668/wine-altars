/* scriptsiteprobe - whether a script host has to be a service provider for CreateObject to work.
 *
 * Wine's vbscript and jscript hand an object that has IObjectWithSite a site of their own that
 * forwards to the host's IServiceProvider, and when the host's IActiveScriptSite has none they fail
 * the whole CreateObject() / new ActiveXObject() -- so under Wine's cscript, which has none, even
 * Msxml2.DOMDocument.6.0 cannot be created.  This hosts both engines with a site that is not a
 * service provider, and with one that is but offers no service, and prints what creating an object
 * with a site (Msxml2.DOMDocument.6.0) and one without (Scripting.Dictionary) gives.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#define CONST_VTABLE
#include <windows.h>
#include <initguid.h>
#include <activscp.h>
#include <stdio.h>

DEFINE_GUID(CLSID_VBScript_, 0xb54f3741, 0x5b07, 0x11cf, 0xa4, 0xb0, 0x00, 0xaa, 0x00, 0x4a, 0x55, 0xe8);
DEFINE_GUID(CLSID_JScript_, 0xf414c260, 0x6ac0, 0x11cf, 0xb6, 0xd1, 0x00, 0xaa, 0x00, 0xbb, 0xbb, 0x58);

struct site
{
    IActiveScriptSite IActiveScriptSite_iface;
    IServiceProvider IServiceProvider_iface;
    BOOL service_provider;
    unsigned int services_asked;
    WCHAR error[256];
};

static struct site *impl_from_site(IActiveScriptSite *iface)
{
    return CONTAINING_RECORD(iface, struct site, IActiveScriptSite_iface);
}

static struct site *impl_from_sp(IServiceProvider *iface)
{
    return CONTAINING_RECORD(iface, struct site, IServiceProvider_iface);
}

static HRESULT WINAPI site_QueryInterface(IActiveScriptSite *iface, REFIID riid, void **out)
{
    struct site *site = impl_from_site(iface);

    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IActiveScriptSite))
        *out = &site->IActiveScriptSite_iface;
    else if (site->service_provider && IsEqualGUID(riid, &IID_IServiceProvider))
        *out = &site->IServiceProvider_iface;
    else
    {
        *out = NULL;
        return E_NOINTERFACE;
    }
    return S_OK;
}

static ULONG WINAPI site_AddRef(IActiveScriptSite *iface) { return 2; }
static ULONG WINAPI site_Release(IActiveScriptSite *iface) { return 1; }

static HRESULT WINAPI site_GetLCID(IActiveScriptSite *iface, LCID *lcid)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI site_GetItemInfo(IActiveScriptSite *iface, LPCOLESTR name, DWORD mask, IUnknown **unk,
        ITypeInfo **ti)
{
    return TYPE_E_ELEMENTNOTFOUND;
}

static HRESULT WINAPI site_GetDocVersionString(IActiveScriptSite *iface, BSTR *version)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI site_OnScriptTerminate(IActiveScriptSite *iface, const VARIANT *result, const EXCEPINFO *ei)
{
    return S_OK;
}

static HRESULT WINAPI site_OnStateChange(IActiveScriptSite *iface, SCRIPTSTATE state)
{
    return S_OK;
}

static HRESULT WINAPI site_OnScriptError(IActiveScriptSite *iface, IActiveScriptError *error)
{
    struct site *site = impl_from_site(iface);
    EXCEPINFO ei = { 0 };

    IActiveScriptError_GetExceptionInfo(error, &ei);
    swprintf(site->error, ARRAYSIZE(site->error), L"error %#lx", ei.scode);
    SysFreeString(ei.bstrSource);
    SysFreeString(ei.bstrDescription);
    SysFreeString(ei.bstrHelpFile);
    return S_OK;
}

static HRESULT WINAPI site_OnEnterScript(IActiveScriptSite *iface) { return S_OK; }
static HRESULT WINAPI site_OnLeaveScript(IActiveScriptSite *iface) { return S_OK; }

static const IActiveScriptSiteVtbl site_vtbl =
{
    site_QueryInterface, site_AddRef, site_Release, site_GetLCID, site_GetItemInfo, site_GetDocVersionString,
    site_OnScriptTerminate, site_OnStateChange, site_OnScriptError, site_OnEnterScript, site_OnLeaveScript,
};

static HRESULT WINAPI sp_QueryInterface(IServiceProvider *iface, REFIID riid, void **out)
{
    return site_QueryInterface(&impl_from_sp(iface)->IActiveScriptSite_iface, riid, out);
}

static ULONG WINAPI sp_AddRef(IServiceProvider *iface) { return 2; }
static ULONG WINAPI sp_Release(IServiceProvider *iface) { return 1; }

static HRESULT WINAPI sp_QueryService(IServiceProvider *iface, REFGUID service, REFIID riid, void **out)
{
    ++impl_from_sp(iface)->services_asked;
    *out = NULL;
    return E_NOINTERFACE;
}

static const IServiceProviderVtbl sp_vtbl = { sp_QueryInterface, sp_AddRef, sp_Release, sp_QueryService };

static void run(const char *engine, const CLSID *clsid, BOOL service_provider, const WCHAR *code)
{
    struct site site = { { &site_vtbl }, { &sp_vtbl }, service_provider, 0, L"" };
    IActiveScriptParse *parse;
    IDispatch *disp = NULL;
    IActiveScript *script;
    DISPPARAMS params = { 0 };
    VARIANT result;
    LPOLESTR name = (LPOLESTR)L"result";
    DISPID id;
    HRESULT hr;

    hr = CoCreateInstance(clsid, NULL, CLSCTX_INPROC_SERVER, &IID_IActiveScript, (void **)&script);
    if (FAILED(hr))
    {
        printf("%s: CoCreateInstance %#lx\n", engine, hr);
        return;
    }
    IActiveScript_QueryInterface(script, &IID_IActiveScriptParse, (void **)&parse);
    IActiveScriptParse64_InitNew(parse);
    IActiveScript_SetScriptSite(script, &site.IActiveScriptSite_iface);
    IActiveScript_SetScriptState(script, SCRIPTSTATE_STARTED);
    hr = IActiveScriptParse64_ParseScriptText(parse, code, NULL, NULL, NULL, 0, 0, 0, NULL, NULL);

    VariantInit(&result);
    if (SUCCEEDED(IActiveScript_GetScriptDispatch(script, NULL, &disp))
            && SUCCEEDED(IDispatch_GetIDsOfNames(disp, &IID_NULL, &name, 1, 0, &id)))
        IDispatch_Invoke(disp, id, &IID_NULL, 0, DISPATCH_PROPERTYGET, &params, &result, NULL, NULL);
    printf("%-8s site %-24s parse %#-10lx result %-12ls %ls  services asked %u\n", engine,
           service_provider ? "is a service provider" : "is not a service provider", hr,
           V_VT(&result) == VT_BSTR ? V_BSTR(&result) : L"-", site.error, site.services_asked);
    VariantClear(&result);
    if (disp) IDispatch_Release(disp);

    IActiveScript_Close(script);
    IActiveScriptParse64_Release(parse);
    IActiveScript_Release(script);
}

int main(void)
{
    static const WCHAR vbs_dom[] = L"Set o = CreateObject(\"Msxml2.DOMDocument.6.0\")\nresult = TypeName(o)\n";
    static const WCHAR vbs_dict[] = L"Set o = CreateObject(\"Scripting.Dictionary\")\nresult = TypeName(o)\n";
    static const WCHAR js_dom[] = L"var o = new ActiveXObject(\"Msxml2.DOMDocument.6.0\"); var result = typeof o;";
    static const WCHAR js_dict[] = L"var o = new ActiveXObject(\"Scripting.Dictionary\"); var result = typeof o;";
    int sp;

    CoInitialize(NULL);
    for (sp = 0; sp < 2; ++sp)
    {
        printf("Msxml2.DOMDocument.6.0 (has IObjectWithSite):\n");
        run("vbscript", &CLSID_VBScript_, sp, vbs_dom);
        run("jscript", &CLSID_JScript_, sp, js_dom);
        printf("Scripting.Dictionary:\n");
        run("vbscript", &CLSID_VBScript_, sp, vbs_dict);
        run("jscript", &CLSID_JScript_, sp, js_dict);
    }
    CoUninitialize();
    return 0;
}
