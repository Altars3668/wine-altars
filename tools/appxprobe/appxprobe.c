/* appxprobe -- what Windows.Management.Deployment answers in this prefix.
 *
 * Office's Click-to-Run LASTRUN step asks the PackageManager whether its MSIX
 * add-ons are installed and then tries to provision them.  Wine answered
 * E_NOTIMPL and left the [out] parameters untouched, and C2R -- which follows
 * the documented contract and reads the operation it was handed -- dereferenced
 * a pointer that was never written.  This probe walks the same path so the
 * answers can be read off directly.
 *
 * Build:
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -municode \
 *       -o appxprobe.exe appxprobe.c -lole32 -loleaut32 -lruntimeobject
 */

#define COBJMACROS
#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#define WIDL_using_Windows_Management_Deployment
#define WIDL_using_Windows_ApplicationModel

#include <windows.h>
#include <initguid.h>
#include <stdio.h>
#include <roapi.h>
#include <winstring.h>
#include <objbase.h>
#include <asyncinfo.h>
#include <windows.foundation.h>
#include <windows.management.deployment.h>

static void out( const char *s )
{
    DWORD written;
    WriteFile( GetStdHandle( STD_OUTPUT_HANDLE ), s, (DWORD)strlen( s ), &written, NULL );
}

static void outf( const char *fmt, ... )
{
    char buf[2048];
    va_list ap;
    va_start( ap, fmt );
    vsnprintf( buf, sizeof(buf), fmt, ap );
    va_end( ap );
    out( buf );
}

static const char *hs( HSTRING s )
{
    static char buf[512];
    UINT32 len = 0;
    const WCHAR *w = WindowsGetStringRawBuffer( s, &len );
    if (!w) return "(null)";
    WideCharToMultiByte( CP_UTF8, 0, w, len, buf, sizeof(buf) - 1, NULL, NULL );
    buf[len < sizeof(buf) - 1 ? len : sizeof(buf) - 1] = 0;
    return buf;
}

static HSTRING mk( const WCHAR *s )
{
    HSTRING h = NULL;
    WindowsCreateString( s, (UINT32)wcslen( s ), &h );
    return h;
}

/* A completed-handler, to check the operation really does deliver one. */
struct handler
{
    IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress iface;
    LONG ref;
    LONG calls;
    AsyncStatus seen;
};

static struct handler handler;

static HRESULT WINAPI handler_QueryInterface(
        IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress *iface,
        REFIID iid, void **obj )
{
    if (IsEqualGUID( iid, &IID_IUnknown ) ||
        IsEqualGUID( iid, &IID_IAgileObject ) ||
        IsEqualGUID( iid, &IID_IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress ))
    {
        *obj = iface;
        return S_OK;
    }
    *obj = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI handler_AddRef( IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress *iface )
{
    return InterlockedIncrement( &((struct handler *)iface)->ref );
}
static ULONG WINAPI handler_Release( IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress *iface )
{
    return InterlockedDecrement( &((struct handler *)iface)->ref );
}
static HRESULT WINAPI handler_Invoke( IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgress *iface,
                                      IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *info, AsyncStatus status )
{
    struct handler *impl = (struct handler *)iface;
    InterlockedIncrement( &impl->calls );
    impl->seen = status;
    (void)info;
    return S_OK;
}
static const struct IAsyncOperationWithProgressCompletedHandler_DeploymentResult_DeploymentProgressVtbl handler_vtbl =
{
    handler_QueryInterface, handler_AddRef, handler_Release, handler_Invoke,
};

static void report_operation( IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *op )
{
    IDeploymentResult *result = NULL;
    IAsyncInfo *info = NULL;
    AsyncStatus status = 99;
    HRESULT hr, code = 0;

    hr = IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress_QueryInterface( op, &IID_IAsyncInfo, (void **)&info );
    outf( "    QI IAsyncInfo            : 0x%08lx\n", (unsigned long)hr );
    if (SUCCEEDED(hr))
    {
        hr = IAsyncInfo_get_Status( info, &status );
        outf( "    IAsyncInfo::Status       : 0x%08lx status=%d (%s)\n", (unsigned long)hr, status,
              status == Started ? "Started" : status == Completed ? "Completed" :
              status == Canceled ? "Canceled" : status == Error ? "Error" : "?" );
        hr = IAsyncInfo_get_ErrorCode( info, &code );
        outf( "    IAsyncInfo::ErrorCode    : 0x%08lx code=0x%08lx\n", (unsigned long)hr, (unsigned long)code );
        IAsyncInfo_Release( info );
    }

    handler.iface.lpVtbl = (void *)&handler_vtbl;
    handler.calls = 0;
    hr = IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress_put_Completed( op, &handler.iface );
    outf( "    put_Completed            : 0x%08lx invoked=%ld status=%d\n", (unsigned long)hr,
          (long)handler.calls, handler.seen );

    hr = IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress_GetResults( op, &result );
    outf( "    GetResults               : 0x%08lx result=%p\n", (unsigned long)hr, result );
    if (result)
    {
        HSTRING text = NULL;
        HRESULT ext = 0;
        IDeploymentResult_get_ExtendedErrorCode( result, &ext );
        IDeploymentResult_get_ErrorText( result, &text );
        outf( "      ExtendedErrorCode      : 0x%08lx\n", (unsigned long)ext );
        outf( "      ErrorText              : %s\n", hs( text ) );
        WindowsDeleteString( text );
        IDeploymentResult_Release( result );
    }
}

int wmain( void )
{
    IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress *op = NULL;
    IActivationFactory *factory = NULL;
    IPackageManager *manager = NULL;
    IPackageManager6 *manager6 = NULL;
    IInspectable *inspectable = NULL;
    IIterable_Package *packages = NULL;
    IIterator_Package *iter = NULL;
    HSTRING cls, family, name, publisher;
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    outf( "RoInitialize                 : 0x%08lx\n", (unsigned long)hr );

    cls = mk( L"Windows.Management.Deployment.PackageManager" );
    hr = RoGetActivationFactory( cls, &IID_IActivationFactory, (void **)&factory );
    outf( "RoGetActivationFactory       : 0x%08lx factory=%p\n", (unsigned long)hr, factory );
    WindowsDeleteString( cls );
    if (FAILED(hr)) return 1;

    /* C2R asks the factory for IAgileObject before it uses it. */
    {
        IUnknown *agile = NULL;
        hr = IActivationFactory_QueryInterface( factory, &IID_IAgileObject, (void **)&agile );
        outf( "factory QI IAgileObject      : 0x%08lx\n", (unsigned long)hr );
        if (agile) IUnknown_Release( agile );
    }

    hr = IActivationFactory_ActivateInstance( factory, &inspectable );
    outf( "ActivateInstance             : 0x%08lx instance=%p\n", (unsigned long)hr, inspectable );
    if (FAILED(hr)) return 1;

    hr = IInspectable_QueryInterface( inspectable, &IID_IPackageManager, (void **)&manager );
    outf( "QI IPackageManager           : 0x%08lx\n", (unsigned long)hr );

    hr = IInspectable_QueryInterface( inspectable, &IID_IPackageManager6, (void **)&manager6 );
    outf( "QI IPackageManager6          : 0x%08lx manager6=%p\n", (unsigned long)hr, manager6 );
    if (FAILED(hr)) { out( "  -- this is the QI that used to fail\n" ); return 1; }

    out( "\n[1] FindPackagesByPackageFamilyName(Microsoft.Office.Desktop_8wekyb3d8bbwe)\n" );
    family = mk( L"Microsoft.Office.Desktop_8wekyb3d8bbwe" );
    hr = IPackageManager_FindPackagesByPackageFamilyName( manager, family, &packages );
    outf( "    hr=0x%08lx packages=%p\n", (unsigned long)hr, packages );
    WindowsDeleteString( family );
    if (packages)
    {
        boolean has = 1;
        UINT32 n = 0;
        hr = IIterable_Package_First( packages, &iter );
        outf( "    First                    : 0x%08lx iter=%p\n", (unsigned long)hr, iter );
        if (iter)
        {
            IIterator_Package_get_HasCurrent( iter, &has );
            while (has) { n++; IIterator_Package_MoveNext( iter, &has ); if (n > 4) break; }
            outf( "    packages found           : %u\n", n );
            IIterator_Package_Release( iter );
        }
        IIterable_Package_Release( packages );
    }

    out( "\n[2] FindPackagesByNamePublisher(Microsoft.Office.Desktop, _8wekyb3d8bbwe)\n" );
    name = mk( L"Microsoft.Office.Desktop" );
    publisher = mk( L"_8wekyb3d8bbwe" );
    packages = NULL;
    hr = IPackageManager_FindPackagesByNamePublisher( manager, name, publisher, &packages );
    outf( "    hr=0x%08lx packages=%p\n", (unsigned long)hr, packages );
    if (packages) IIterable_Package_Release( packages );
    WindowsDeleteString( name );
    WindowsDeleteString( publisher );

    out( "\n[3] IPackageManager6::ProvisionPackageForAllUsersAsync -- the call that crashed\n" );
    family = mk( L"Microsoft.WritingAssistant_8wekyb3d8bbwe" );
    hr = IPackageManager6_ProvisionPackageForAllUsersAsync( manager6, family, &op );
    outf( "    hr=0x%08lx operation=%p\n", (unsigned long)hr, op );
    WindowsDeleteString( family );
    if (op)
    {
        report_operation( op );
        IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress_Release( op );
    }

    out( "\n[4] IPackageManager::StagePackageAsync\n" );
    op = NULL;
    hr = IPackageManager_StagePackageAsync( manager, NULL, NULL, &op );
    outf( "    hr=0x%08lx operation=%p\n", (unsigned long)hr, op );
    if (op)
    {
        report_operation( op );
        IAsyncOperationWithProgress_DeploymentResult_DeploymentProgress_Release( op );
    }

    IPackageManager6_Release( manager6 );
    IPackageManager_Release( manager );
    IInspectable_Release( inspectable );
    IActivationFactory_Release( factory );
    out( "\ndone\n" );
    return 0;
}
