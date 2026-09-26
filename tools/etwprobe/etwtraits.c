/* etwtraits: EventSetInformation's check of provider traits, each case on a new registration,
 * and what setting them twice answers.  A companion of etwprobe.
 */
#include <windows.h>
#include <evntprov.h>
#include <stdio.h>

static const GUID provider = { 0x1d6b6bd2, 0x1e7c, 0x4e46, { 0x8a, 0x1d, 0x6f, 0x3e, 0x57, 0x81, 0x9c, 0x31 } };

static unsigned int make( unsigned char *buf, unsigned int declared, const char *name, int namelen,
                          int trait_size, int trait_type, int trait_len )
{
    unsigned int pos;

    memset( buf, 0, 128 );
    memcpy( buf + 2, name, namelen );
    pos = 2 + namelen;
    if (trait_size >= 0)
    {
        *(USHORT *)(buf + pos) = trait_size;
        buf[pos + 2] = trait_type;
        pos += trait_len;
    }
    *(USHORT *)buf = declared ? declared : pos;
    return pos;
}

static void one( const char *what, const unsigned char *buf, unsigned int size )
{
    REGHANDLE handle;
    ULONG ret;

    EventRegister( &provider, NULL, NULL, &handle );
    ret = EventSetInformation( handle, EventProviderSetTraits, (void *)buf, size );
    printf( "%-40s size %2u: %lu\n", what, size, ret );
    EventUnregister( handle );
}

int main( void )
{
    unsigned char buf[128], good[128];
    unsigned int size, good_size;
    REGHANDLE handle;

    setvbuf( stdout, NULL, _IONBF, 0 );
    size = make( buf, 0, "Provider", 9, 19, 1, 19 );  one( "name, group trait", buf, size );
    size = make( buf, 0, "Provider", 9, -1, 0, 0 );   one( "name only", buf, size );
    size = make( buf, 0, "", 1, 19, 1, 19 );           one( "empty name, group trait", buf, size );
    size = make( buf, 0, "Provider", 8, -1, 0, 0 );   one( "name without its nul", buf, size );
    size = make( buf, 0, "Provider", 9, 0, 0, 3 );    one( "trait of size 0", buf, size );
    size = make( buf, 0, "Provider", 9, 2, 1, 2 );    one( "trait of size 2", buf, size );
    size = make( buf, 0, "Provider", 9, 3, 1, 3 );    one( "group trait of size 3", buf, size );
    size = make( buf, 0, "Provider", 9, 20, 1, 19 );  one( "trait larger than the rest", buf, size );
    size = make( buf, 0, "Provider", 9, 5, 7, 5 );    one( "trait of type 7, size 5", buf, size );
    size = make( buf, 0, "Provider", 9, 3, 0, 3 );    one( "trait of type 0, size 3", buf, size );
    size = make( buf, 0, "Provider", 9, 19, 1, 19 );
    one( "size one more than declared", buf, size + 1 );
    one( "size one less than declared", buf, size - 1 );
    *(USHORT *)buf = size + 1;                          one( "declared one more", buf, size );

    good_size = make( good, 0, "Provider", 9, 19, 1, 19 );
    EventRegister( &provider, NULL, NULL, &handle );
    printf( "twice, same: %lu", EventSetInformation( handle, EventProviderSetTraits, good, good_size ) );
    printf( " %lu\n", EventSetInformation( handle, EventProviderSetTraits, good, good_size ) );
    EventUnregister( handle );
    size = make( buf, 0, "Other", 6, 19, 1, 19 );
    EventRegister( &provider, NULL, NULL, &handle );
    printf( "twice, other: %lu", EventSetInformation( handle, EventProviderSetTraits, good, good_size ) );
    printf( " %lu\n", EventSetInformation( handle, EventProviderSetTraits, buf, size ) );
    EventUnregister( handle );
    size = make( buf, 0, "Provider", 9, 0, 0, 3 );
    EventRegister( &provider, NULL, NULL, &handle );
    printf( "bad, then good: %lu", EventSetInformation( handle, EventProviderSetTraits, buf, size ) );
    printf( " %lu\n", EventSetInformation( handle, EventProviderSetTraits, good, good_size ) );
    EventUnregister( handle );
    printf( "done\n" );
    return 0;
}
