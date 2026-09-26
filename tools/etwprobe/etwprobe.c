/* etwprobe: what the provider side of ETW answers with no session listening: registration
 * handles, EventSetInformation's checks, enablement, and classic RegisterTraceGuids.
 */
#include <windows.h>
#include <evntprov.h>
#include <wmistr.h>
#include <evntrace.h>
#include <stdio.h>

static const GUID provider = { 0x1d6b6bd2, 0x1e7c, 0x4e46, { 0x8a, 0x1d, 0x6f, 0x3e, 0x57, 0x81, 0x9c, 0x31 } };
static const GUID provider2 = { 0x1d6b6bd3, 0x1e7c, 0x4e46, { 0x8a, 0x1d, 0x6f, 0x3e, 0x57, 0x81, 0x9c, 0x31 } };
static const GUID control = { 0x2d6b6bd2, 0x1e7c, 0x4e46, { 0x8a, 0x1d, 0x6f, 0x3e, 0x57, 0x81, 0x9c, 0x31 } };
static const GUID class1 = { 0x3d6b6bd2, 0x1e7c, 0x4e46, { 0x8a, 0x1d, 0x6f, 0x3e, 0x57, 0x81, 0x9c, 0x31 } };
static const GUID class2 = { 0x4d6b6bd2, 0x1e7c, 0x4e46, { 0x8a, 0x1d, 0x6f, 0x3e, 0x57, 0x81, 0x9c, 0x31 } };

static int callbacks;

static void WINAPI enable_callback( const GUID *source, ULONG code, UCHAR level, ULONGLONG any, ULONGLONG all,
                                    EVENT_FILTER_DESCRIPTOR *filter, void *context )
{
    callbacks++;
    printf( "  enable callback code %lu level %u any %#I64x\n", code, level, any );
}

static ULONG WINAPI control_callback( WMIDPREQUESTCODE code, void *context, ULONG *size, void *buffer )
{
    callbacks++;
    printf( "  control callback code %d\n", code );
    return 0;
}

static void set_info( REGHANDLE handle, int cls, const void *info, ULONG size )
{
    ULONG ret = EventSetInformation( handle, cls, (void *)info, size );
    printf( "EventSetInformation(class %d, size %lu): %lu\n", cls, size, ret );
}

int main( void )
{
    TRACE_GUID_REGISTRATION regs[2] = { { &class1, NULL }, { &class2, NULL } };
    unsigned char traits[64];
    EVENT_DESCRIPTOR desc = { 0 };
    TRACEHANDLE trace_handle;
    REGHANDLE handle, handle2, handle3;
    ULONG ret;

    setvbuf( stdout, NULL, _IONBF, 0 );

    handle = 0xdead;
    ret = EventRegister( &provider, enable_callback, NULL, &handle );
    printf( "EventRegister: %lu handle %s callbacks %d\n", ret, handle ? (handle == 0xdead ? "untouched" : "set") : "0", callbacks );
    ret = EventRegister( &provider2, NULL, NULL, &handle2 );
    printf( "EventRegister again: %lu, different %d\n", ret, handle2 != handle );
    ret = EventRegister( &provider, NULL, NULL, &handle3 );
    printf( "EventRegister same provider: %lu, different %d\n", ret, handle3 != handle );
    handle3 = 0xdead;
    ret = EventRegister( NULL, NULL, NULL, &handle3 );
    printf( "EventRegister(NULL): %lu handle %s\n", ret, handle3 == 0xdead ? "untouched" : handle3 ? "set" : "0" );
    ret = EventRegister( &provider, NULL, NULL, NULL );
    printf( "EventRegister(NULL handle): %lu\n", ret );

    printf( "EventEnabled %d EventProviderEnabled(0, 0) %d (5, ~0) %d\n", EventEnabled( handle, &desc ),
            EventProviderEnabled( handle, 0, 0 ), EventProviderEnabled( handle, 5, ~0ull ) );

    set_info( handle, EventProviderBinaryTrackInfo, NULL, 0 );
    set_info( handle, EventProviderBinaryTrackInfo, traits, 4 );
    set_info( handle, EventProviderSetReserved1, NULL, 0 );
    memset( traits, 0, sizeof(traits) );
    *(USHORT *)traits = 10;
    memcpy( traits + 2, "Provider", 9 );
    set_info( handle, EventProviderSetTraits, traits, 10 );
    set_info( handle, EventProviderSetTraits, traits, 11 );
    set_info( handle, EventProviderSetTraits, traits, 9 );
    set_info( handle, EventProviderSetTraits, traits, 0 );
    set_info( handle, EventProviderSetTraits, NULL, 10 );
    *(USHORT *)traits = 46;
    set_info( handle, EventProviderSetTraits, traits, 46 );
    set_info( handle, EventProviderSetTraits, traits, 1 );
    memset( traits, 0, sizeof(traits) );
    *(USHORT *)traits = 2 + 9 + 19;
    memcpy( traits + 2, "Provider", 9 );
    *(USHORT *)(traits + 11) = 19;
    traits[13] = 1;
    set_info( handle, EventProviderSetTraits, traits, 30 );
    *(USHORT *)(traits + 11) = 2;
    set_info( handle, EventProviderSetTraits, traits, 30 );
    *(USHORT *)(traits + 11) = 20;
    set_info( handle, EventProviderSetTraits, traits, 30 );
    *(USHORT *)(traits + 11) = 3;
    *(USHORT *)traits = 2 + 9 + 3;
    set_info( handle, EventProviderSetTraits, traits, 14 );
    *(USHORT *)traits = 2 + 9;
    set_info( handle, EventProviderSetTraits, traits, 11 );
    *(USHORT *)traits = 2 + 3;
    memcpy( traits + 2, "Pro", 3 );
    set_info( handle, EventProviderSetTraits, traits, 5 );
    *(USHORT *)traits = 2;
    set_info( handle, EventProviderSetTraits, traits, 2 );
    traits[0] = 1;
    set_info( handle, EventProviderUseDescriptorType, traits, 1 );
    set_info( handle, EventProviderUseDescriptorType, traits, 4 );
    set_info( handle, EventProviderUseDescriptorType, traits, 0 );
    set_info( handle, 4, traits, 4 );
    set_info( handle, 5, traits, 4 );
    set_info( handle, 99, traits, 4 );
    set_info( 0, EventProviderSetTraits, traits, 46 );
    set_info( 0x12345678, EventProviderSetTraits, traits, 46 );

    ret = EventWriteString( handle, 0, 0, L"hello" );
    printf( "EventWriteString: %lu\n", ret );
    ret = EventWrite( handle, &desc, 0, NULL );
    printf( "EventWrite: %lu\n", ret );

    ret = EventUnregister( handle );
    printf( "EventUnregister: %lu\n", ret );
    ret = EventUnregister( handle );
    printf( "EventUnregister again: %lu\n", ret );
    ret = EventUnregister( 0 );
    printf( "EventUnregister(0): %lu\n", ret );
    ret = EventUnregister( 0x12345678 );
    printf( "EventUnregister(bogus): %lu\n", ret );
    ret = EventWrite( handle, &desc, 0, NULL );
    printf( "EventWrite after unregister: %lu\n", ret );
    set_info( handle, EventProviderSetTraits, traits, 46 );
    EventUnregister( handle2 );

    trace_handle = 0xdead;
    ret = RegisterTraceGuidsW( control_callback, NULL, &control, 2, regs, NULL, NULL, &trace_handle );
    printf( "RegisterTraceGuidsW: %lu handle %s reg handles %s %s callbacks %d\n", ret,
            trace_handle == 0xdead ? "untouched" : trace_handle ? "set" : "0",
            regs[0].RegHandle ? "set" : "NULL", regs[1].RegHandle ? "set" : "NULL", callbacks );
    ret = GetTraceEnableFlags( trace_handle );
    printf( "GetTraceEnableFlags(registration): %lu error %lu\n", ret, GetLastError() );
    ret = UnregisterTraceGuids( trace_handle );
    printf( "UnregisterTraceGuids: %lu\n", ret );
    ret = UnregisterTraceGuids( trace_handle );
    printf( "UnregisterTraceGuids again: %lu\n", ret );
    ret = UnregisterTraceGuids( 0 );
    printf( "UnregisterTraceGuids(0): %lu\n", ret );
    trace_handle = 0xdead;
    ret = RegisterTraceGuidsW( NULL, NULL, &control, 2, regs, NULL, NULL, &trace_handle );
    printf( "RegisterTraceGuidsW(no callback): %lu\n", ret );
    ret = RegisterTraceGuidsW( control_callback, NULL, NULL, 2, regs, NULL, NULL, &trace_handle );
    printf( "RegisterTraceGuidsW(no control guid): %lu\n", ret );
    ret = RegisterTraceGuidsW( control_callback, NULL, &control, 0, NULL, NULL, NULL, &trace_handle );
    printf( "RegisterTraceGuidsW(no classes): %lu\n", ret );
    if (!ret) UnregisterTraceGuids( trace_handle );
    ret = RegisterTraceGuidsW( control_callback, NULL, &control, 2, regs, NULL, NULL, NULL );
    printf( "RegisterTraceGuidsW(no handle): %lu\n", ret );
    printf( "done\n" );
    return 0;
}
