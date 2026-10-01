/* winmgmtsprobe: how "winmgmts:" becomes a moniker.  MkParseDisplayName looks the prefix up as a ProgID and binds
 * a class moniker to the class asking for IParseDisplayName; this prints what each step gives: the class object
 * asked for IParseDisplayName and for IClassFactory, in process and with CLSCTX_ALL, an instance asked for
 * IParseDisplayName, and the whole parse.  SWbemLocator, a class whose class object has no reason to parse names,
 * is the control: whether a class object that lacks the interface fails as such or as a class not registered.
 * Usage: winmgmtsprobe   Prints results only. */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <stdio.h>

static const CLSID clsid_winmgmts = { 0x172bddf8, 0xceea, 0x11d1, { 0x8b, 0x05, 0x00, 0x60, 0x08, 0x06, 0xd9, 0xb6 } };
static const CLSID clsid_swbemlocator = { 0x76a64158, 0xcb41, 0x11d1, { 0x8b, 0x02, 0x00, 0x60, 0x08, 0x06, 0xd9, 0xb6 } };

static void class_object( const char *name, const CLSID *clsid, DWORD context, const char *iid_name, const IID *iid )
{
    IUnknown *unk = NULL;
    HRESULT hr = CoGetClassObject( clsid, context, NULL, iid, (void **)&unk );
    printf( "CoGetClassObject(%s, %s, %s): %#lx\n", name, context == CLSCTX_ALL ? "CLSCTX_ALL" : "CLSCTX_INPROC_SERVER",
            iid_name, hr );
    if (unk) IUnknown_Release( unk );
}

static void instance( const char *name, const CLSID *clsid, const char *iid_name, const IID *iid )
{
    IUnknown *unk = NULL;
    HRESULT hr = CoCreateInstance( clsid, NULL, CLSCTX_ALL, iid, (void **)&unk );
    printf( "CoCreateInstance(%s, CLSCTX_ALL, %s): %#lx\n", name, iid_name, hr );
    if (unk) IUnknown_Release( unk );
}

static void parse( const WCHAR *name )
{
    IMoniker *moniker = NULL;
    IBindCtx *ctx;
    ULONG eaten = 0;
    HRESULT hr;

    CreateBindCtx( 0, &ctx );
    hr = MkParseDisplayName( ctx, name, &eaten, &moniker );
    printf( "MkParseDisplayName(%ls): %#lx, eaten %lu of %u\n", name, hr, eaten, (unsigned int)wcslen( name ) );
    if (moniker)
    {
        DWORD sys = 0;
        IMoniker_IsSystemMoniker( moniker, &sys );
        printf( "  system moniker kind %lu\n", sys );
        IMoniker_Release( moniker );
    }
    IBindCtx_Release( ctx );
}

int wmain(void)
{
    static const DWORD contexts[] = { CLSCTX_INPROC_SERVER, CLSCTX_ALL };
    unsigned int i;

    CoInitialize( NULL );
    for (i = 0; i < ARRAYSIZE(contexts); i++)
    {
        class_object( "WinMGMTS", &clsid_winmgmts, contexts[i], "IParseDisplayName", &IID_IParseDisplayName );
        class_object( "WinMGMTS", &clsid_winmgmts, contexts[i], "IClassFactory", &IID_IClassFactory );
        class_object( "SWbemLocator", &clsid_swbemlocator, contexts[i], "IParseDisplayName", &IID_IParseDisplayName );
    }
    instance( "WinMGMTS", &clsid_winmgmts, "IParseDisplayName", &IID_IParseDisplayName );
    instance( "WinMGMTS", &clsid_winmgmts, "IUnknown", &IID_IUnknown );
    parse( L"winmgmts:\\\\.\\root\\cimv2" );
    parse( L"winmgmts:{impersonationLevel=impersonate}!\\\\.\\root\\SecurityCenter2" );
    CoUninitialize();
    return 0;
}
