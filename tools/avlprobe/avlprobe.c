/* avlprobe: what ntdll's AVL generic table and RtlIsNameInExpression do, printed so that the output of a
 * run on Windows and one under Wine can be diffed.  Office's App-V subsystem (AppvIsvSubsystems64.dll)
 * imports both.
 *
 * Pointers are never printed: a node is named by the value it holds, the table by "table", and an
 * allocation by its size and the offset of the pointer the table hands out.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winternl.h"
#include "ddk/ntddk.h"

typedef TABLE_SEARCH_RESULT SEARCH_RESULT;
typedef NTSTATUS (WINAPI *MATCH_FUNCTION)(RTL_AVL_TABLE *, void *, void *);

static void (WINAPI *pRtlInitializeGenericTableAvl)(RTL_AVL_TABLE *, void *, void *, void *, void *);
static void *(WINAPI *pRtlInsertElementGenericTableAvl)(RTL_AVL_TABLE *, void *, ULONG, BOOLEAN *);
static void *(WINAPI *pRtlInsertElementGenericTableFullAvl)(RTL_AVL_TABLE *, void *, ULONG, BOOLEAN *, void *, SEARCH_RESULT);
static void *(WINAPI *pRtlLookupElementGenericTableAvl)(RTL_AVL_TABLE *, void *);
static void *(WINAPI *pRtlLookupElementGenericTableFullAvl)(RTL_AVL_TABLE *, void *, void **, SEARCH_RESULT *);
static void *(WINAPI *pRtlLookupFirstMatchingElementGenericTableAvl)(RTL_AVL_TABLE *, void *, void **);
static BOOLEAN (WINAPI *pRtlDeleteElementGenericTableAvl)(RTL_AVL_TABLE *, void *);
static void (WINAPI *pRtlDeleteElementGenericTableAvlEx)(RTL_AVL_TABLE *, void *);
static void *(WINAPI *pRtlEnumerateGenericTableAvl)(RTL_AVL_TABLE *, BOOLEAN);
static void *(WINAPI *pRtlEnumerateGenericTableWithoutSplayingAvl)(RTL_AVL_TABLE *, void **);
static void *(WINAPI *pRtlEnumerateGenericTableLikeADirectory)(RTL_AVL_TABLE *, MATCH_FUNCTION, void *, ULONG, void **, ULONG *, void *);
static void *(WINAPI *pRtlGetElementGenericTableAvl)(RTL_AVL_TABLE *, ULONG);
static ULONG (WINAPI *pRtlNumberGenericTableElementsAvl)(RTL_AVL_TABLE *);
static BOOLEAN (WINAPI *pRtlIsGenericTableEmptyAvl)(RTL_AVL_TABLE *);
static BOOLEAN (WINAPI *pRtlIsNameInExpression)(UNICODE_STRING *, UNICODE_STRING *, BOOLEAN, WCHAR *);

/* every allocation the table asked for, so that a node can be named by its value */
struct allocation { void *ptr; LONG size; int freed; };
static struct allocation allocations[256];
static int allocation_count, fail_allocation, compare_by_tens, compare_calls;
static RTL_AVL_TABLE *current_table;
static void *caller_buffer;
static int buffer_first, buffer_second, buffer_neither;

static RTL_GENERIC_COMPARE_RESULTS WINAPI compare( RTL_AVL_TABLE *table, void *a, void *b )
{
    int x = *(int *)a, y = *(int *)b;
    compare_calls++;
    if (a == caller_buffer) buffer_first++;
    else if (b == caller_buffer) buffer_second++;
    else buffer_neither++;
    if (compare_by_tens) { x /= 10; y /= 10; }
    if (x < y) return GenericLessThan;
    if (x > y) return GenericGreaterThan;
    return GenericEqual;
}

static void * WINAPI allocate( RTL_AVL_TABLE *table, LONG size )
{
    void *ptr;
    if (fail_allocation) { printf( "  allocate %ld refused\n", size ); return NULL; }
    ptr = calloc( 1, size + 64 );
    allocations[allocation_count].ptr = ptr;
    allocations[allocation_count].size = size;
    allocations[allocation_count].freed = 0;
    allocation_count++;
    return ptr;
}

static void WINAPI release( RTL_AVL_TABLE *table, void *ptr )
{
    int i;
    for (i = 0; i < allocation_count; i++)
    {
        if (allocations[i].ptr != ptr) continue;
        printf( "  free: allocation of %ld%s\n", allocations[i].size, allocations[i].freed ? " (again)" : "" );
        allocations[i].freed = 1;
        return;
    }
    printf( "  free: unknown pointer\n" );
}

/* the value in the node at links, or -1 when links is not the start of an allocation */
static int node_value( void *links )
{
    int i;
    for (i = 0; i < allocation_count; i++)
        if (allocations[i].ptr == links) return *(int *)((char *)links + sizeof(RTL_BALANCED_LINKS));
    return -1;
}

static const char *name_links( RTL_AVL_TABLE *table, void *links )
{
    static char buffers[8][32];
    static int n;
    char *buffer = buffers[n++ % 8];
    int value;

    if (!links) return "NULL";
    if (links == (void *)0xdeadbeef) return "untouched";
    if (links == &table->BalancedRoot) return "root";
    if (links == table) return "table";
    if ((value = node_value( links )) != -1) { sprintf( buffer, "node %d", value ); return buffer; }
    sprintf( buffer, "?%+Id", (char *)links - (char *)table );
    return buffer;
}

/* a data pointer the table gave out: the value, and where it is in its allocation */
static const char *name_data( void *data )
{
    static char buffers[8][64];
    static int n;
    char *buffer = buffers[n++ % 8];
    int i;

    if (!data) return "NULL";
    for (i = 0; i < allocation_count; i++)
    {
        char *start = allocations[i].ptr;
        if ((char *)data >= start && (char *)data < start + allocations[i].size + 64)
        {
            sprintf( buffer, "%d (at +%Id)", *(int *)data, (char *)data - start );
            return buffer;
        }
    }
    sprintf( buffer, "%d (not a node)", *(int *)data );
    return buffer;
}

static void dump_subtree( RTL_AVL_TABLE *table, RTL_BALANCED_LINKS *node, RTL_BALANCED_LINKS *parent, char *out )
{
    char tmp[64];
    if (!node) { strcat( out, "." ); return; }
    sprintf( tmp, "%d%s%+d", node_value( node ), node->Parent == parent ? "" : "!", node->Balance );
    strcat( out, tmp );
    if (!node->LeftChild && !node->RightChild) return;
    strcat( out, "(" );
    dump_subtree( table, node->LeftChild, node, out );
    strcat( out, " " );
    dump_subtree( table, node->RightChild, node, out );
    strcat( out, ")" );
}

static void dump_table( const char *what, RTL_AVL_TABLE *table )
{
    char tree[4096] = "";
    dump_subtree( table, table->BalancedRoot.RightChild, &table->BalancedRoot, tree );
    printf( "%s: tree %s\n", what, tree );
    printf( "  root: parent %s left %s balance %d reserved %02x%02x%02x; count %lu depth %lu ordered %s/%lu"
            " restart %s deletes %lu\n",
            name_links( table, table->BalancedRoot.Parent ), name_links( table, table->BalancedRoot.LeftChild ),
            table->BalancedRoot.Balance, table->BalancedRoot.Reserved[0], table->BalancedRoot.Reserved[1],
            table->BalancedRoot.Reserved[2], table->NumberGenericTableElements, table->DepthOfTree,
            name_links( table, table->OrderedPointer ), table->WhichOrderedElement,
            name_links( table, table->RestartKey ), table->DeleteCount );
}

static void init_table( RTL_AVL_TABLE *table )
{
    memset( table, 0xcc, sizeof(*table) );
    allocation_count = 0;
    current_table = table;
    pRtlInitializeGenericTableAvl( table, compare, allocate, release, (void *)0xdeadbeef );
}

static void insert( RTL_AVL_TABLE *table, int value, ULONG size )
{
    BOOLEAN new_element = 0xcc;
    int before = allocation_count;
    void *data;

    compare_calls = 0;
    data = pRtlInsertElementGenericTableAvl( table, &value, size, &new_element );
    printf( "insert %d size %lu: %s new %d, %d compares", value, size, name_data( data ), new_element, compare_calls );
    if (allocation_count > before) printf( ", allocated %ld", allocations[before].size );
    printf( "\n" );
}

static void test_basics(void)
{
    static const int values[] = { 50, 30, 70, 20, 40, 60, 80, 10, 25, 35, 45, 5, 1, 90, 95, 99, 42, 43, 44 };
    RTL_AVL_TABLE table;
    void *data, *restart;
    unsigned int i;
    int value;

    printf( "sizeof(RTL_AVL_TABLE) %u, sizeof(RTL_BALANCED_LINKS) %u\n",
            (unsigned int)sizeof(RTL_AVL_TABLE), (unsigned int)sizeof(RTL_BALANCED_LINKS) );
    init_table( &table );
    printf( "after init: compare %d allocate %d free %d context %p empty %d count %lu\n",
            table.CompareRoutine == (void *)compare, table.AllocateRoutine == (void *)allocate,
            table.FreeRoutine == (void *)release, table.TableContext, pRtlIsGenericTableEmptyAvl( &table ),
            pRtlNumberGenericTableElementsAvl( &table ) );
    dump_table( "after init", &table );
    printf( "root right %s\n", name_links( &table, table.BalancedRoot.RightChild ) );
    value = 1;
    printf( "lookup in empty: %s\n", name_data( pRtlLookupElementGenericTableAvl( &table, &value ) ) );
    printf( "enumerate empty: %s\n", name_data( pRtlEnumerateGenericTableAvl( &table, TRUE ) ) );
    restart = NULL;
    printf( "enumerate empty without splaying: %s\n",
            name_data( pRtlEnumerateGenericTableWithoutSplayingAvl( &table, &restart ) ) );
    printf( "element 0 of empty: %s\n", name_data( pRtlGetElementGenericTableAvl( &table, 0 ) ) );
    printf( "delete from empty: %d\n", pRtlDeleteElementGenericTableAvl( &table, &value ) );

    for (i = 0; i < ARRAY_SIZE(values); i++)
    {
        insert( &table, values[i], sizeof(int) );
        dump_table( "  now", &table );
    }
    insert( &table, 40, sizeof(int) );
    insert( &table, 41, 100 );
    dump_table( "after the size 100 insert", &table );
    printf( "empty %d count %lu\n", pRtlIsGenericTableEmptyAvl( &table ), pRtlNumberGenericTableElementsAvl( &table ) );

    for (value = 0; value <= 100; value += 5)
    {
        compare_calls = 0;
        data = pRtlLookupElementGenericTableAvl( &table, &value );
        printf( "lookup %d: %s, %d compares\n", value, name_data( data ), compare_calls );
    }
    dump_table( "after lookups", &table );

    printf( "enumerate:" );
    for (data = pRtlEnumerateGenericTableAvl( &table, TRUE ); data; data = pRtlEnumerateGenericTableAvl( &table, FALSE ))
        printf( " %d[%s]", *(int *)data, name_links( &table, table.RestartKey ) );
    printf( "\n" );
    dump_table( "after enumerate", &table );
    printf( "enumerate again without restart: %s\n", name_data( pRtlEnumerateGenericTableAvl( &table, FALSE ) ) );
    dump_table( "after that", &table );

    printf( "without splaying:" );
    restart = NULL;
    while ((data = pRtlEnumerateGenericTableWithoutSplayingAvl( &table, &restart )))
        printf( " %d[%s]", *(int *)data, name_links( &table, restart ) );
    printf( " end[%s]\n", name_links( &table, restart ) );
    dump_table( "after that", &table );

    printf( "elements:" );
    for (i = 0; i < 22; i++)
    {
        data = pRtlGetElementGenericTableAvl( &table, i );
        printf( " %s[%s/%lu]", data ? name_data( data ) : "NULL", name_links( &table, table.OrderedPointer ),
                table.WhichOrderedElement );
    }
    printf( "\n" );
    {
        static const ULONG order[] = { 5, 3, 17, 16, 0, 20, 21, 10, 11, 9, 9, ~0u };
        printf( "elements in another order:" );
        for (i = 0; i < ARRAY_SIZE(order); i++)
        {
            data = pRtlGetElementGenericTableAvl( &table, order[i] );
            printf( " %lu:%s[%s/%lu]", order[i], data ? name_data( data ) : "NULL",
                    name_links( &table, table.OrderedPointer ), table.WhichOrderedElement );
        }
        printf( "\n" );
    }

    /* deleting: a leaf, one child, two children, the root, and absent values */
    {
        static const int deletions[] = { 99, 95, 30, 50, 12, 1, 44, 45, 5, 10, 40 };
        for (i = 0; i < ARRAY_SIZE(deletions); i++)
        {
            BOOLEAN ret;
            value = deletions[i];
            compare_calls = 0;
            ret = pRtlDeleteElementGenericTableAvl( &table, &value );
            printf( "delete %d: %d, %d compares\n", value, ret, compare_calls );
            dump_table( "  now", &table );
        }
    }
    printf( "elements after deletions:" );
    for (i = 0; i < 16; i++)
    {
        data = pRtlGetElementGenericTableAvl( &table, i );
        printf( " %s", data ? name_data( data ) : "NULL" );
    }
    printf( "\n" );
    dump_table( "after that", &table );

    /* deleting everything that is left, in order */
    while ((data = pRtlGetElementGenericTableAvl( &table, 0 )))
    {
        value = *(int *)data;
        pRtlDeleteElementGenericTableAvl( &table, &value );
    }
    dump_table( "emptied", &table );
    printf( "empty %d count %lu\n", pRtlIsGenericTableEmptyAvl( &table ), pRtlNumberGenericTableElementsAvl( &table ) );
}

/* where a delete leaves a running enumeration */
static void test_enumeration_and_deletes(void)
{
    RTL_AVL_TABLE table;
    void *data, *restart;
    int value, i;

    init_table( &table );
    for (i = 1; i <= 9; i++) insert( &table, i * 10, sizeof(int) );
    data = pRtlEnumerateGenericTableAvl( &table, TRUE );
    data = pRtlEnumerateGenericTableAvl( &table, FALSE );
    data = pRtlEnumerateGenericTableAvl( &table, FALSE );
    printf( "enumerated to %s, restart %s\n", name_data( data ), name_links( &table, table.RestartKey ) );
    value = 30;
    pRtlDeleteElementGenericTableAvl( &table, &value );
    dump_table( "deleted the restart key", &table );
    data = pRtlEnumerateGenericTableAvl( &table, FALSE );
    printf( "next: %s, restart %s\n", name_data( data ), name_links( &table, table.RestartKey ) );
    value = 60;
    pRtlDeleteElementGenericTableAvl( &table, &value );
    data = pRtlEnumerateGenericTableAvl( &table, FALSE );
    printf( "deleted 60, next: %s, restart %s\n", name_data( data ), name_links( &table, table.RestartKey ) );

    pRtlGetElementGenericTableAvl( &table, 3 );
    printf( "element 3: ordered %s/%lu\n", name_links( &table, table.OrderedPointer ), table.WhichOrderedElement );
    value = 90;
    pRtlDeleteElementGenericTableAvl( &table, &value );
    printf( "deleted 90: ordered %s/%lu\n", name_links( &table, table.OrderedPointer ), table.WhichOrderedElement );
    pRtlGetElementGenericTableAvl( &table, 3 );
    value = 15;
    insert( &table, 15, sizeof(int) );
    printf( "inserted 15: ordered %s/%lu\n", name_links( &table, table.OrderedPointer ), table.WhichOrderedElement );
    data = pRtlGetElementGenericTableAvl( &table, 3 );
    printf( "element 3 now: %s\n", name_data( data ) );

    restart = NULL;
    data = pRtlEnumerateGenericTableWithoutSplayingAvl( &table, &restart );
    data = pRtlEnumerateGenericTableWithoutSplayingAvl( &table, &restart );
    printf( "without splaying at %s\n", name_data( data ) );
    value = 20;
    pRtlDeleteElementGenericTableAvl( &table, &value );
    dump_table( "deleted 20", &table );
}

static void test_full(void)
{
    static const int values[] = { 40, 20, 60, 10, 30, 50, 70 };
    RTL_AVL_TABLE table;
    SEARCH_RESULT result;
    BOOLEAN new_element;
    void *node, *data;
    int value, i;

    if (!pRtlLookupElementGenericTableFullAvl || !pRtlInsertElementGenericTableFullAvl)
    {
        printf( "no Full functions\n" );
        return;
    }
    init_table( &table );
    value = 5;
    node = (void *)0xdeadbeef;
    result = 0xcc;
    data = pRtlLookupElementGenericTableFullAvl( &table, &value, &node, &result );
    printf( "full lookup in empty: %s node %s result %d\n", name_data( data ), name_links( &table, node ), result );
    new_element = 0xcc;
    data = pRtlInsertElementGenericTableFullAvl( &table, &value, sizeof(int), &new_element, node, result );
    printf( "full insert 5: %s new %d\n", name_data( data ), new_element );
    dump_table( "  now", &table );
    value = 5;
    data = pRtlLookupElementGenericTableFullAvl( &table, &value, &node, &result );
    printf( "full lookup 5: %s node %s result %d\n", name_data( data ), name_links( &table, node ), result );
    value = 5;
    pRtlDeleteElementGenericTableAvl( &table, &value );

    for (i = 0; i < ARRAY_SIZE(values); i++) insert( &table, values[i], sizeof(int) );
    for (value = 5; value <= 75; value += 5)
    {
        node = (void *)0xdeadbeef;
        result = 0xcc;
        compare_calls = 0;
        data = pRtlLookupElementGenericTableFullAvl( &table, &value, &node, &result );
        printf( "full lookup %d: %s node %s result %d, %d compares\n", value, name_data( data ),
                name_links( &table, node ), result, compare_calls );
    }
    dump_table( "after full lookups", &table );
    value = 45;
    pRtlLookupElementGenericTableFullAvl( &table, &value, &node, &result );
    compare_calls = 0;
    data = pRtlInsertElementGenericTableFullAvl( &table, &value, sizeof(int), &new_element, node, result );
    printf( "full insert 45: %s new %d, %d compares\n", name_data( data ), new_element, compare_calls );
    dump_table( "  now", &table );
    value = 20;
    pRtlLookupElementGenericTableFullAvl( &table, &value, &node, &result );
    compare_calls = 0;
    data = pRtlInsertElementGenericTableFullAvl( &table, &value, sizeof(int), &new_element, node, result );
    printf( "full insert 20 found: %s new %d, %d compares, count %lu\n", name_data( data ), new_element,
            compare_calls, table.NumberGenericTableElements );

    if (pRtlDeleteElementGenericTableAvlEx)
    {
        value = 40;
        data = pRtlLookupElementGenericTableFullAvl( &table, &value, &node, &result );
        printf( "delete ex of %s\n", name_links( &table, node ) );
        compare_calls = 0;
        pRtlDeleteElementGenericTableAvlEx( &table, node );
        printf( "  %d compares\n", compare_calls );
        dump_table( "  now", &table );
    }
    else printf( "no RtlDeleteElementGenericTableAvlEx\n" );
}

static void test_first_matching(void)
{
    static const int values[] = { 11, 25, 21, 29, 23, 31, 5, 27, 22, 40 };
    RTL_AVL_TABLE table;
    void *data, *restart;
    int value, i;

    if (!pRtlLookupFirstMatchingElementGenericTableAvl)
    {
        printf( "no RtlLookupFirstMatchingElementGenericTableAvl\n" );
        return;
    }
    init_table( &table );
    for (i = 0; i < ARRAY_SIZE(values); i++) insert( &table, values[i], sizeof(int) );
    dump_table( "first matching table", &table );
    compare_by_tens = 1;
    for (value = 0; value <= 50; value += 10)
    {
        restart = (void *)0xdeadbeef;
        compare_calls = 0;
        data = pRtlLookupFirstMatchingElementGenericTableAvl( &table, &value, &restart );
        printf( "first matching %d: %s restart %s, %d compares", value, name_data( data ),
                name_links( &table, restart ), compare_calls );
        if (data)
        {
            printf( ", then" );
            while ((data = pRtlEnumerateGenericTableWithoutSplayingAvl( &table, &restart )))
                printf( " %d", *(int *)data );
        }
        printf( "\n" );
    }
    value = 20;
    data = pRtlLookupElementGenericTableAvl( &table, &value );
    printf( "lookup by tens 20: %s\n", name_data( data ) );
    compare_by_tens = 0;
    dump_table( "after that", &table );
}

static NTSTATUS match_result;
static int match_calls;

static NTSTATUS WINAPI match( RTL_AVL_TABLE *table, void *data, void *match_data )
{
    int value = *(int *)data;
    match_calls++;
    printf( " m%d", value );
    if (match_data == (void *)1) return value % 20 ? STATUS_NO_MATCH : STATUS_SUCCESS;
    if (match_data == (void *)2) return value == 50 ? STATUS_NO_MORE_MATCHES : STATUS_NO_MATCH;
    if (match_data == (void *)3) return value == 30 ? STATUS_INVALID_PARAMETER : STATUS_SUCCESS;
    if (match_data == (void *)4) return value == 30 ? STATUS_MORE_ENTRIES : STATUS_NO_MATCH;
    if (match_data == (void *)5) return value == 30 ? STATUS_BUFFER_OVERFLOW : STATUS_NO_MATCH;
    if (match_data == (void *)6) return value == 30 ? STATUS_NO_MATCH + 1 : STATUS_NO_MATCH;
    return match_result;
}

static void directory( RTL_AVL_TABLE *table, void *match_data, ULONG next, void **restart, ULONG *deletes, int value )
{
    void *data;
    printf( "like a directory from %d next %lu restart %s deletes %lu:", value, next, name_links( table, *restart ),
            *deletes );
    compare_calls = match_calls = 0;
    data = pRtlEnumerateGenericTableLikeADirectory( table, match_data ? match : NULL, match_data, next, restart,
                                                    deletes, &value );
    printf( " -> %s restart %s deletes %lu, %d compares\n", name_data( data ), name_links( table, *restart ),
            *deletes, compare_calls );
}

static void test_like_a_directory(void)
{
    RTL_AVL_TABLE table;
    void *restart;
    ULONG deletes;
    int value, i;

    if (!pRtlEnumerateGenericTableLikeADirectory)
    {
        printf( "no RtlEnumerateGenericTableLikeADirectory\n" );
        return;
    }
    init_table( &table );
    restart = NULL; deletes = 0;
    directory( &table, NULL, FALSE, &restart, &deletes, 0 );
    for (i = 1; i <= 9; i++) insert( &table, i * 10, sizeof(int) );

    for (value = 0; value <= 100; value += 25)
    {
        restart = NULL; deletes = 0;
        directory( &table, NULL, FALSE, &restart, &deletes, value );
        restart = NULL; deletes = 0;
        directory( &table, NULL, TRUE, &restart, &deletes, value );
    }
    restart = NULL; deletes = 0;
    directory( &table, NULL, FALSE, &restart, &deletes, 30 );
    directory( &table, NULL, TRUE, &restart, &deletes, 30 );
    directory( &table, NULL, TRUE, &restart, &deletes, 30 );
    directory( &table, NULL, FALSE, &restart, &deletes, 30 );
    directory( &table, NULL, TRUE, &restart, &deletes, 0 );
    directory( &table, NULL, TRUE, &restart, &deletes, 99 );

    /* a delete in between invalidates the restart key */
    restart = NULL; deletes = 0;
    directory( &table, NULL, FALSE, &restart, &deletes, 40 );
    value = 70;
    pRtlDeleteElementGenericTableAvl( &table, &value );
    directory( &table, NULL, TRUE, &restart, &deletes, 40 );
    directory( &table, NULL, TRUE, &restart, &deletes, 40 );
    deletes = 12345;
    directory( &table, NULL, TRUE, &restart, &deletes, 20 );

    /* match functions */
    restart = NULL; deletes = 0;
    directory( &table, (void *)1, FALSE, &restart, &deletes, 10 );
    directory( &table, (void *)1, TRUE, &restart, &deletes, 10 );
    directory( &table, (void *)1, TRUE, &restart, &deletes, 10 );
    restart = NULL; deletes = 0;
    directory( &table, (void *)2, FALSE, &restart, &deletes, 10 );
    restart = NULL; deletes = 0;
    directory( &table, (void *)3, FALSE, &restart, &deletes, 10 );
    directory( &table, (void *)3, TRUE, &restart, &deletes, 10 );
    directory( &table, (void *)3, TRUE, &restart, &deletes, 10 );
    dump_table( "after that", &table );
}

/* what the first round did not settle */
static void test_details(void)
{
    RTL_AVL_TABLE table;
    BOOLEAN new_element;
    void *data, *restart;
    ULONG deletes;
    int value, i;

    init_table( &table );
    for (i = 1; i <= 9; i++) insert( &table, i * 10, sizeof(int) );

    /* which argument of the compare routine is the caller's buffer */
    value = 55;
    caller_buffer = &value;
    buffer_first = buffer_second = buffer_neither = 0;
    pRtlLookupElementGenericTableAvl( &table, &value );
    printf( "lookup: buffer first %d second %d neither %d\n", buffer_first, buffer_second, buffer_neither );
    buffer_first = buffer_second = buffer_neither = 0;
    pRtlInsertElementGenericTableAvl( &table, &value, sizeof(int), &new_element );
    printf( "insert: buffer first %d second %d neither %d\n", buffer_first, buffer_second, buffer_neither );
    buffer_first = buffer_second = buffer_neither = 0;
    pRtlDeleteElementGenericTableAvl( &table, &value );
    printf( "delete: buffer first %d second %d neither %d\n", buffer_first, buffer_second, buffer_neither );
    if (pRtlLookupFirstMatchingElementGenericTableAvl)
    {
        compare_by_tens = 1;
        value = 20;
        buffer_first = buffer_second = buffer_neither = 0;
        pRtlLookupFirstMatchingElementGenericTableAvl( &table, &value, &restart );
        printf( "first matching: buffer first %d second %d neither %d\n", buffer_first, buffer_second, buffer_neither );
        compare_by_tens = 0;
    }
    caller_buffer = NULL;

    /* does an insert or a lookup move the element cursor */
    data = pRtlGetElementGenericTableAvl( &table, 3 );
    printf( "element 3: %s ordered %s/%lu\n", name_data( data ), name_links( &table, table.OrderedPointer ),
            table.WhichOrderedElement );
    insert( &table, 5, sizeof(int) );
    printf( "after inserting 5: ordered %s/%lu\n", name_links( &table, table.OrderedPointer ), table.WhichOrderedElement );
    data = pRtlGetElementGenericTableAvl( &table, 3 );
    printf( "element 3: %s\n", name_data( data ) );
    insert( &table, 30, sizeof(int) );
    printf( "after inserting 30 again: ordered %s/%lu\n", name_links( &table, table.OrderedPointer ),
            table.WhichOrderedElement );
    value = 99;
    pRtlDeleteElementGenericTableAvl( &table, &value );
    printf( "after deleting absent 99: ordered %s/%lu deletes %lu\n", name_links( &table, table.OrderedPointer ),
            table.WhichOrderedElement, table.DeleteCount );
    data = pRtlInsertElementGenericTableAvl( &table, &value, sizeof(int), NULL );
    printf( "insert without a flag: %s\n", name_data( data ) );
    printf( "count %lu\n", pRtlNumberGenericTableElementsAvl( &table ) );

    /* enumerating without restarting on a fresh key */
    table.RestartKey = NULL;
    data = pRtlEnumerateGenericTableAvl( &table, FALSE );
    printf( "enumerate without restart from no key: %s restart %s\n", name_data( data ),
            name_links( &table, table.RestartKey ) );

    /* like a directory, off the end */
    if (pRtlEnumerateGenericTableLikeADirectory)
    {
        restart = NULL; deletes = table.DeleteCount;
        directory( &table, NULL, FALSE, &restart, &deletes, 80 );
        directory( &table, (void *)1, TRUE, &restart, &deletes, 80 );
        restart = NULL; deletes = table.DeleteCount;
        directory( &table, NULL, FALSE, &restart, &deletes, 99 );
        directory( &table, NULL, TRUE, &restart, &deletes, 99 );
        directory( &table, NULL, TRUE, &restart, &deletes, 99 );
        restart = NULL; deletes = table.DeleteCount;
        directory( &table, NULL, FALSE, &restart, &deletes, 90 );
        deletes = 777;
        directory( &table, NULL, FALSE, &restart, &deletes, 1000 );
        restart = NULL; deletes = table.DeleteCount;
        directory( &table, (void *)4, FALSE, &restart, &deletes, 10 );
        directory( &table, (void *)4, TRUE, &restart, &deletes, 10 );
        restart = NULL; deletes = table.DeleteCount;
        directory( &table, (void *)5, FALSE, &restart, &deletes, 10 );
        directory( &table, (void *)5, TRUE, &restart, &deletes, 10 );
        restart = NULL; deletes = table.DeleteCount;
        directory( &table, (void *)6, FALSE, &restart, &deletes, 10 );
        directory( &table, (void *)6, TRUE, &restart, &deletes, 10 );
        restart = NULL; deletes = table.DeleteCount;
        directory( &table, NULL, TRUE, &restart, &deletes, 35 );

        /* an emptied table with deletes behind it */
        init_table( &table );
        insert( &table, 1, sizeof(int) );
        value = 1;
        pRtlDeleteElementGenericTableAvl( &table, &value );
        restart = (void *)&table; deletes = 0;
        directory( &table, NULL, FALSE, &restart, &deletes, 0 );
    }
}

/* which calls leave the element cursor where GetElement put it */
static void test_cursor(void)
{
    RTL_AVL_TABLE table;
    SEARCH_RESULT result;
    BOOLEAN new_element;
    void *node, *restart;
    ULONG deletes;
    int value, i;

    init_table( &table );
    for (i = 1; i <= 9; i++) insert( &table, i * 10, sizeof(int) );
#define CURSOR(what) printf( "%s: ordered %s/%lu\n", what, name_links( &table, table.OrderedPointer ), \
                             table.WhichOrderedElement ); pRtlGetElementGenericTableAvl( &table, 3 )
    pRtlGetElementGenericTableAvl( &table, 3 );
    CURSOR( "element 3" );
    value = 50; pRtlLookupElementGenericTableAvl( &table, &value );
    CURSOR( "lookup present" );
    value = 55; pRtlLookupElementGenericTableAvl( &table, &value );
    CURSOR( "lookup absent" );
    value = 55;
    if (pRtlLookupElementGenericTableFullAvl)
    {
        pRtlLookupElementGenericTableFullAvl( &table, &value, &node, &result );
        CURSOR( "full lookup absent" );
        value = 50;
        pRtlLookupElementGenericTableFullAvl( &table, &value, &node, &result );
        pRtlInsertElementGenericTableFullAvl( &table, &value, sizeof(int), &new_element, node, result );
        CURSOR( "full insert found" );
    }
    value = 55; pRtlDeleteElementGenericTableAvl( &table, &value );
    CURSOR( "delete absent" );
    pRtlEnumerateGenericTableAvl( &table, TRUE );
    CURSOR( "enumerate" );
    restart = NULL; pRtlEnumerateGenericTableWithoutSplayingAvl( &table, &restart );
    CURSOR( "enumerate without splaying" );
    if (pRtlLookupFirstMatchingElementGenericTableAvl)
    {
        value = 50; pRtlLookupFirstMatchingElementGenericTableAvl( &table, &value, &restart );
        CURSOR( "first matching" );
    }
    if (pRtlEnumerateGenericTableLikeADirectory)
    {
        restart = NULL; deletes = 0; value = 50;
        pRtlEnumerateGenericTableLikeADirectory( &table, NULL, NULL, FALSE, &restart, &deletes, &value );
        CURSOR( "like a directory" );
    }
    pRtlIsGenericTableEmptyAvl( &table );
    pRtlNumberGenericTableElementsAvl( &table );
    CURSOR( "empty and number" );
    value = 50; pRtlInsertElementGenericTableAvl( &table, &value, sizeof(int), &new_element );
    CURSOR( "insert present" );
    fail_allocation = 1;
    value = 51; pRtlInsertElementGenericTableAvl( &table, &value, sizeof(int), &new_element );
    fail_allocation = 0;
    CURSOR( "insert refused" );
    value = 50; pRtlDeleteElementGenericTableAvl( &table, &value );
    CURSOR( "delete present" );
#undef CURSOR

    if (pRtlEnumerateGenericTableLikeADirectory)
    {
        restart = NULL; deletes = 555;
        directory( &table, (void *)2, FALSE, &restart, &deletes, 10 );
        restart = NULL; deletes = 555;
        directory( &table, (void *)1, FALSE, &restart, &deletes, 85 );
        restart = &table; deletes = 555;
        directory( &table, (void *)1, FALSE, &restart, &deletes, 85 );
    }
    printf( "table deletes %lu\n", table.DeleteCount );
}

static void test_failed_allocation(void)
{
    RTL_AVL_TABLE table;
    BOOLEAN new_element;
    void *data;
    int value = 7;

    init_table( &table );
    insert( &table, 3, sizeof(int) );
    fail_allocation = 1;
    new_element = 0xcc;
    data = pRtlInsertElementGenericTableAvl( &table, &value, sizeof(int), &new_element );
    printf( "insert with a refused allocation: %s new %d\n", name_data( data ), new_element );
    fail_allocation = 0;
    dump_table( "  now", &table );
}

static void test_name_in_expression(void)
{
    static const WCHAR *expressions[] =
    {
        L"", L"*", L"?", L"??", L"*.*", L"*.", L".*", L"a*", L"*a", L"a?c", L"A*B*", L"*B", L"<", L"<.txt", L"<.TXT",
        L"*.txt", L"*.TXT", L">", L">>>", L"a>", L"a>>", L"a>.txt", L"a\"", L"a\"*", L"\"", L"<\"*", L"*\"*", L"a.\"",
        L"ab.<", L"*<", L"<*", L"**", L"?*?", L"*?", L"a*.b", L"a.b*", L"A", L"ABC", L"abc", L"<a", L"*>", L">*",
        L"file.tar.gz", L"*.gz", L"<.gz", L"<.tar.gz", L"f<", L"f<.gz", L"?\"", L"*.?", L"a.>", L"a.>>", L">.>",
    };
    static const WCHAR *names[] =
    {
        L"", L"a", L"ab", L"abc", L"ABC", L"aBc", L"a.b", L"a.b.c", L"abc.txt", L"abc.TXT", L".txt", L"a.", L"a..",
        L"..", L".", L"file.tar.gz", L"aXc", L"a.bc", L"ab.c", L"b",
    };
    UNICODE_STRING expression, name;
    WCHAR table[65536];
    unsigned int i, j, k;

    for (i = 0; i < ARRAY_SIZE(expressions); i++)
    {
        RtlInitUnicodeString( &expression, expressions[i] );
        printf( "expression \"%ls\":", expressions[i] );
        for (k = 0; k < 2; k++)
        {
            printf( k ? " | ignore case" : "" );
            for (j = 0; j < ARRAY_SIZE(names); j++)
            {
                RtlInitUnicodeString( &name, names[j] );
                printf( " %d", pRtlIsNameInExpression( &expression, &name, k, NULL ) );
            }
        }
        printf( "\n" );
    }

    /* with a table of its own: only the name goes through it */
    for (i = 0; i < 65536; i++) table[i] = i;
    table['x'] = 'Y';
    table['a'] = 'A';
    RtlInitUnicodeString( &expression, L"AY" );
    RtlInitUnicodeString( &name, L"ax" );
    printf( "table: AY/ax %d", pRtlIsNameInExpression( &expression, &name, TRUE, table ) );
    printf( " %d", pRtlIsNameInExpression( &expression, &name, FALSE, table ) );
    RtlInitUnicodeString( &expression, L"ay" );
    printf( " ay/ax %d", pRtlIsNameInExpression( &expression, &name, TRUE, table ) );
    RtlInitUnicodeString( &expression, L"AX" );
    printf( " AX/ax %d", pRtlIsNameInExpression( &expression, &name, TRUE, table ) );
    RtlInitUnicodeString( &expression, L"A*" );
    RtlInitUnicodeString( &name, L"a\x00e9" );
    printf( " A*/a-acute %d", pRtlIsNameInExpression( &expression, &name, TRUE, table ) );
    RtlInitUnicodeString( &expression, L"A\x00c9" );
    printf( " A-ACUTE/a-acute %d %d", pRtlIsNameInExpression( &expression, &name, TRUE, table ),
            pRtlIsNameInExpression( &expression, &name, TRUE, NULL ) );
    RtlInitUnicodeString( &expression, L"\x03a3" );
    RtlInitUnicodeString( &name, L"\x03c3" );
    printf( " sigma %d", pRtlIsNameInExpression( &expression, &name, TRUE, NULL ) );
    RtlInitUnicodeString( &name, L"\x03c2" );
    printf( " final sigma %d", pRtlIsNameInExpression( &expression, &name, TRUE, NULL ) );
    RtlInitUnicodeString( &expression, L"\x00df" );
    RtlInitUnicodeString( &name, L"\x00df" );
    printf( " sharp s %d", pRtlIsNameInExpression( &expression, &name, TRUE, NULL ) );
    printf( "\n" );

    /* lengths are what count, not terminators */
    {
        WCHAR expr_buffer[] = L"a*bXYZ", name_buffer[] = L"axxbQQQ";
        expression.Buffer = expr_buffer; expression.Length = 3 * sizeof(WCHAR); expression.MaximumLength = 3 * sizeof(WCHAR);
        name.Buffer = name_buffer; name.Length = 4 * sizeof(WCHAR); name.MaximumLength = 4 * sizeof(WCHAR);
        printf( "counted: %d", pRtlIsNameInExpression( &expression, &name, FALSE, NULL ) );
        expression.Length = 5;
        printf( " odd expression length %d", pRtlIsNameInExpression( &expression, &name, FALSE, NULL ) );
        expression.Length = 3 * sizeof(WCHAR);
        name.Length = 7;
        printf( " odd name length %d\n", pRtlIsNameInExpression( &expression, &name, FALSE, NULL ) );
    }
}

int main( int argc, char **argv )
{
    HMODULE ntdll = GetModuleHandleA( "ntdll.dll" );

#define GET(f) p##f = (void *)GetProcAddress( ntdll, #f ); if (!p##f) printf( "no %s\n", #f )
    GET(RtlInitializeGenericTableAvl);
    GET(RtlInsertElementGenericTableAvl);
    GET(RtlInsertElementGenericTableFullAvl);
    GET(RtlLookupElementGenericTableAvl);
    GET(RtlLookupElementGenericTableFullAvl);
    GET(RtlLookupFirstMatchingElementGenericTableAvl);
    GET(RtlDeleteElementGenericTableAvl);
    GET(RtlDeleteElementGenericTableAvlEx);
    GET(RtlEnumerateGenericTableAvl);
    GET(RtlEnumerateGenericTableWithoutSplayingAvl);
    GET(RtlEnumerateGenericTableLikeADirectory);
    GET(RtlGetElementGenericTableAvl);
    GET(RtlNumberGenericTableElementsAvl);
    GET(RtlIsGenericTableEmptyAvl);
    GET(RtlIsNameInExpression);
#undef GET
    setvbuf( stdout, NULL, _IONBF, 0 );

    if (argc < 2 || !strcmp( argv[1], "names" )) if (pRtlIsNameInExpression) test_name_in_expression();
    if (argc < 2 || !strcmp( argv[1], "avl" ))
    {
        test_basics();
        test_enumeration_and_deletes();
        test_full();
        test_first_matching();
        test_like_a_directory();
        test_details();
        test_cursor();
        test_failed_allocation();
    }
    printf( "done\n" );
    return 0;
}
