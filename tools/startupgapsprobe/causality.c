/* causality: Windows.Foundation.Diagnostics.AsyncCausalityTracer's factory -- the interfaces it has, its class
 * name, what its methods return.  Traces one made-up operation, which only a tracing session would see. */
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <roapi.h>
#include <winstring.h>
#include <inspectable.h>
#include <activation.h>
#include <stdio.h>

DEFINE_GUID(IID_IAsyncCausalityTracerStatics_guess, 0x50850b26, 0x267e, 0x451b, 0xa8, 0x90, 0xab, 0x6a, 0x37, 0x02, 0x45, 0xee);
DEFINE_GUID(IID_IAgileObject_, 0x94ea2b94, 0xe9cc, 0x49e0, 0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90);

typedef struct statics statics;
struct statics_vtbl
{
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(statics *, REFIID, void **);
    ULONG (STDMETHODCALLTYPE *AddRef)(statics *);
    ULONG (STDMETHODCALLTYPE *Release)(statics *);
    HRESULT (STDMETHODCALLTYPE *GetIids)(statics *, ULONG *, IID **);
    HRESULT (STDMETHODCALLTYPE *GetRuntimeClassName)(statics *, HSTRING *);
    HRESULT (STDMETHODCALLTYPE *GetTrustLevel)(statics *, TrustLevel *);
    HRESULT (STDMETHODCALLTYPE *TraceOperationCreation)(statics *, int, int, GUID, UINT64, HSTRING, UINT64);
    HRESULT (STDMETHODCALLTYPE *TraceOperationCompletion)(statics *, int, int, GUID, UINT64, int);
    HRESULT (STDMETHODCALLTYPE *TraceOperationRelation)(statics *, int, int, GUID, UINT64, int);
    HRESULT (STDMETHODCALLTYPE *TraceSynchronousWorkStart)(statics *, int, int, GUID, UINT64, int);
    HRESULT (STDMETHODCALLTYPE *TraceSynchronousWorkCompletion)(statics *, int, int, int);
    HRESULT (STDMETHODCALLTYPE *add_TracingStatusChanged)(statics *, IUnknown *, INT64 *);
    HRESULT (STDMETHODCALLTYPE *remove_TracingStatusChanged)(statics *, INT64);
};
struct statics { const struct statics_vtbl *lpVtbl; };

/* a handler that only counts */
static HRESULT STDMETHODCALLTYPE h_qi(IUnknown *iface, REFIID iid, void **out) { *out = iface; return S_OK; }
static ULONG STDMETHODCALLTYPE h_addref(IUnknown *iface) { return 2; }
static ULONG STDMETHODCALLTYPE h_release(IUnknown *iface) { return 1; }
static HRESULT STDMETHODCALLTYPE h_invoke(IUnknown *iface, IInspectable *sender, IInspectable *args) { return S_OK; }
static const void *handler_vtbl[] = { h_qi, h_addref, h_release, h_invoke };
static struct { const void **vtbl; } handler = { handler_vtbl };

int main(void)
{
    static const WCHAR name[] = L"Windows.Foundation.Diagnostics.AsyncCausalityTracer";
    GUID platform = { 0x12345678, 0x1234, 0x1234, { 1, 2, 3, 4, 5, 6, 7, 8 } };
    IActivationFactory *factory;
    IInspectable *inspectable, *instance;
    statics *s;
    HSTRING hs, cls, op;
    ULONG count, i;
    IID *iids;
    INT64 token = -1;
    HRESULT hr;
    void *agile;
    TrustLevel trust;

    setvbuf(stdout, NULL, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    hr = RoInitialize(RO_INIT_MULTITHREADED);
    printf("RoInitialize %#lx\n", hr);
    WindowsCreateString(name, wcslen(name), &hs);
    hr = RoGetActivationFactory(hs, &IID_IActivationFactory, (void **)&factory);
    printf("factory %#lx\n", hr);
    if (FAILED(hr)) return 1;
    IActivationFactory_QueryInterface(factory, &IID_IInspectable, (void **)&inspectable);
    hr = IInspectable_GetIids(inspectable, &count, &iids);
    printf("GetIids %#lx count %lu:", hr, count);
    for (i = 0; SUCCEEDED(hr) && i < count; i++)
        printf(" {%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", iids[i].Data1, iids[i].Data2, iids[i].Data3,
               iids[i].Data4[0], iids[i].Data4[1], iids[i].Data4[2], iids[i].Data4[3], iids[i].Data4[4],
               iids[i].Data4[5], iids[i].Data4[6], iids[i].Data4[7]);
    printf("\n");
    hr = IInspectable_GetRuntimeClassName(inspectable, &cls);
    printf("GetRuntimeClassName %#lx %ls\n", hr, SUCCEEDED(hr) ? WindowsGetStringRawBuffer(cls, NULL) : L"");
    hr = IInspectable_GetTrustLevel(inspectable, &trust);
    printf("GetTrustLevel %#lx %d\n", hr, trust);
    hr = IActivationFactory_QueryInterface(factory, &IID_IAgileObject_, &agile);
    printf("IAgileObject %#lx\n", hr);
    instance = (IInspectable *)0xdeadbeef;
    hr = IActivationFactory_ActivateInstance(factory, &instance);
    printf("ActivateInstance %#lx %p\n", hr, instance);
    hr = IActivationFactory_QueryInterface(factory, &IID_IAsyncCausalityTracerStatics_guess, (void **)&s);
    printf("statics %#lx\n", hr);
    if (FAILED(hr)) return 1;
    WindowsCreateString(L"probe", 5, &op);
    printf("TraceOperationCreation %#lx\n", s->lpVtbl->TraceOperationCreation(s, 0, 0, platform, 1, op, 0));
    printf("TraceOperationCreation, bad level %#lx\n", s->lpVtbl->TraceOperationCreation(s, 7, 0, platform, 1, op, 0));
    printf("TraceOperationCreation, bad source %#lx\n", s->lpVtbl->TraceOperationCreation(s, 0, 7, platform, 1, op, 0));
    printf("TraceOperationCreation, no name %#lx\n", s->lpVtbl->TraceOperationCreation(s, 0, 0, platform, 1, NULL, 0));
    printf("TraceOperationRelation %#lx\n", s->lpVtbl->TraceOperationRelation(s, 0, 0, platform, 1, 0));
    printf("TraceSynchronousWorkStart %#lx\n", s->lpVtbl->TraceSynchronousWorkStart(s, 0, 0, platform, 1, 2));
    printf("TraceSynchronousWorkCompletion %#lx\n", s->lpVtbl->TraceSynchronousWorkCompletion(s, 0, 0, 2));
    printf("TraceOperationCompletion %#lx\n", s->lpVtbl->TraceOperationCompletion(s, 0, 0, platform, 1, 1));
    hr = s->lpVtbl->add_TracingStatusChanged(s, (IUnknown *)&handler, &token);
    printf("add_TracingStatusChanged %#lx token %s\n", hr, token == -1 ? "untouched" : token ? "nonzero" : "0");
    printf("add_TracingStatusChanged, no handler %#lx\n", s->lpVtbl->add_TracingStatusChanged(s, NULL, &token));
    printf("remove_TracingStatusChanged %#lx\n", s->lpVtbl->remove_TracingStatusChanged(s, token));
    printf("remove_TracingStatusChanged again %#lx\n", s->lpVtbl->remove_TracingStatusChanged(s, token));
    printf("remove_TracingStatusChanged 12345 %#lx\n", s->lpVtbl->remove_TracingStatusChanged(s, 12345));
    return 0;
}
