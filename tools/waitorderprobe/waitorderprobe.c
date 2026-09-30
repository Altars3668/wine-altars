/* In what order an alertable wait takes a queued user APC and what it waits for, and what a wait that ran
 * the APC leaves of the objects.
 *
 * Wine's kernel32 test fixes one case: an alertable WaitForSingleObjectEx on a signaled event with an APC
 * queued answers WAIT_IO_COMPLETION and the event stays signaled.  user32's fixes another: a message already
 * waiting wins over the APC in MsgWaitForMultipleObjectsEx with no handles and MWMO_ALERTABLE.  This asks
 * the same of a free mutex, one the thread owns, an abandoned one, a semaphore, several objects with and
 * without bWaitAll, SignalObjectAndWait, MsgWaitForMultipleObjectsEx with a handle as well as the queue, and
 * CoWaitForMultipleHandles, and prints what each wait leaves: the APCs it ran, those still queued, what is
 * still signaled.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror waitorderprobe.c -o waitorderprobe.exe -lole32
 */
#include <windows.h>
#include <objbase.h>
#include <stdio.h>

static unsigned int apcs;

static void CALLBACK apc( ULONG_PTR arg )
{
    (void)arg;
    apcs++;
}

static void queue_apc(void)
{
    QueueUserAPC( apc, GetCurrentThread(), 0 );
}

static const char *wait_name( DWORD ret )
{
    static char buffer[32];

    if (ret == WAIT_IO_COMPLETION) return "WAIT_IO_COMPLETION";
    if (ret == WAIT_TIMEOUT) return "WAIT_TIMEOUT";
    if (ret == WAIT_FAILED) sprintf( buffer, "WAIT_FAILED %lu", GetLastError() );
    else if (ret >= WAIT_ABANDONED_0 && ret < WAIT_ABANDONED_0 + MAXIMUM_WAIT_OBJECTS)
        sprintf( buffer, "WAIT_ABANDONED_0+%lu", ret - WAIT_ABANDONED_0 );
    else if (ret < MAXIMUM_WAIT_OBJECTS) sprintf( buffer, "WAIT_OBJECT_0+%lu", ret );
    else sprintf( buffer, "%#lx", ret );
    return buffer;
}

/* prints what the wait answered, the APCs it ran and those it left queued, which it then runs */
static void report( const char *what, const char *ret, const char *after )
{
    unsigned int ran = apcs;

    apcs = 0;
    while (SleepEx( 0, TRUE ) == WAIT_IO_COMPLETION);
    printf( "%-66s %s, APCs run %u, left %u%s%s\n", what, ret, ran, apcs, *after ? ", " : "", after );
    apcs = 0;
}

static const char *signaled( HANDLE handle )
{
    return WaitForSingleObject( handle, 0 ) == WAIT_OBJECT_0 ? "signaled" : "not signaled";
}

static unsigned int mutex_owned( HANDLE mutex )
{
    unsigned int count = 0;

    while (ReleaseMutex( mutex )) count++;
    return count;
}

static DWORD CALLBACK abandon( void *mutex )
{
    WaitForSingleObject( mutex, INFINITE );
    return 0;
}

static void single_objects(void)
{
    HANDLE event, mutex, sem, thread;
    char after[128];
    LONG count;
    DWORD ret;

    event = CreateEventW( NULL, FALSE, TRUE, NULL );
    queue_apc();
    ret = WaitForSingleObjectEx( event, 0, TRUE );
    sprintf( after, "event %s", signaled( event ) );
    report( "auto-reset event signaled, APC, timeout 0", wait_name( ret ), after );
    CloseHandle( event );

    event = CreateEventW( NULL, FALSE, TRUE, NULL );
    queue_apc();
    queue_apc();
    ret = WaitForSingleObjectEx( event, INFINITE, TRUE );
    sprintf( after, "event %s", signaled( event ) );
    report( "auto-reset event signaled, two APCs, INFINITE", wait_name( ret ), after );
    CloseHandle( event );

    event = CreateEventW( NULL, TRUE, TRUE, NULL );
    queue_apc();
    ret = WaitForSingleObjectEx( event, 0, TRUE );
    sprintf( after, "event %s", signaled( event ) );
    report( "manual-reset event signaled, APC", wait_name( ret ), after );
    CloseHandle( event );

    event = CreateEventW( NULL, FALSE, TRUE, NULL );
    queue_apc();
    ret = WaitForSingleObjectEx( event, 0, FALSE );
    sprintf( after, "event %s", signaled( event ) );
    report( "auto-reset event signaled, APC, not alertable", wait_name( ret ), after );
    CloseHandle( event );

    mutex = CreateMutexW( NULL, FALSE, NULL );
    queue_apc();
    ret = WaitForSingleObjectEx( mutex, 0, TRUE );
    sprintf( after, "mutex owned %u times", mutex_owned( mutex ) );
    report( "free mutex, APC", wait_name( ret ), after );
    CloseHandle( mutex );

    mutex = CreateMutexW( NULL, TRUE, NULL );
    queue_apc();
    ret = WaitForSingleObjectEx( mutex, 0, TRUE );
    sprintf( after, "mutex owned %u times", mutex_owned( mutex ) );
    report( "mutex this thread owns, APC", wait_name( ret ), after );
    CloseHandle( mutex );

    mutex = CreateMutexW( NULL, FALSE, NULL );
    thread = CreateThread( NULL, 0, abandon, mutex, 0, NULL );
    WaitForSingleObject( thread, INFINITE );
    CloseHandle( thread );
    queue_apc();
    ret = WaitForSingleObjectEx( mutex, 0, TRUE );
    sprintf( after, "a wait after it gives %s", wait_name( WaitForSingleObject( mutex, 0 ) ) );
    sprintf( after + strlen( after ), ", mutex owned %u times", mutex_owned( mutex ) );
    report( "abandoned mutex, APC", wait_name( ret ), after );
    CloseHandle( mutex );

    sem = CreateSemaphoreW( NULL, 1, 10, NULL );
    queue_apc();
    ret = WaitForSingleObjectEx( sem, 0, TRUE );
    ReleaseSemaphore( sem, 1, &count );
    sprintf( after, "semaphore count %ld", count );
    report( "semaphore with count 1, APC", wait_name( ret ), after );
    CloseHandle( sem );

    queue_apc();
    ret = SleepEx( 0, TRUE );
    report( "SleepEx(0, TRUE), APC", wait_name( ret ), "" );
}

static void several_objects(void)
{
    HANDLE events[2], event;
    char after[128];
    DWORD ret;

    events[0] = CreateEventW( NULL, FALSE, TRUE, NULL );
    events[1] = CreateEventW( NULL, FALSE, TRUE, NULL );
    queue_apc();
    ret = WaitForMultipleObjectsEx( 2, events, FALSE, 0, TRUE );
    sprintf( after, "events %s, %s", signaled( events[0] ), signaled( events[1] ) );
    report( "two auto-reset events signaled, wait any, APC", wait_name( ret ), after );

    SetEvent( events[0] );
    SetEvent( events[1] );
    queue_apc();
    ret = WaitForMultipleObjectsEx( 2, events, TRUE, 0, TRUE );
    sprintf( after, "events %s, %s", signaled( events[0] ), signaled( events[1] ) );
    report( "two auto-reset events signaled, wait all, APC", wait_name( ret ), after );

    SetEvent( events[0] );
    queue_apc();
    ret = WaitForMultipleObjectsEx( 2, events, TRUE, 0, TRUE );
    sprintf( after, "events %s, %s", signaled( events[0] ), signaled( events[1] ) );
    report( "one of two signaled, wait all, APC", wait_name( ret ), after );

    ResetEvent( events[0] );
    SetEvent( events[1] );
    queue_apc();
    ret = WaitForMultipleObjectsEx( 2, events, FALSE, 0, TRUE );
    sprintf( after, "events %s, %s", signaled( events[0] ), signaled( events[1] ) );
    report( "second of two signaled, wait any, APC", wait_name( ret ), after );

    ResetEvent( events[0] );
    ResetEvent( events[1] );
    event = CreateEventW( NULL, TRUE, FALSE, NULL );
    SetEvent( events[1] );
    queue_apc();
    ret = SignalObjectAndWait( event, events[1], 0, TRUE );
    sprintf( after, "the event to signal %s, the one waited for %s", signaled( event ), signaled( events[1] ) );
    report( "SignalObjectAndWait, the one waited for signaled, APC", wait_name( ret ), after );
    CloseHandle( event );
    CloseHandle( events[0] );
    CloseHandle( events[1] );
}

static void post(void)
{
    PostThreadMessageW( GetCurrentThreadId(), WM_USER, 0, 0 );
}

static void msg_waits(void)
{
    static const struct
    {
        BOOL handle, event_set, message, seen, apc;
        DWORD flags;
        const char *name;
    }
    tests[] =
    {
        {TRUE,  TRUE,  TRUE,  FALSE, FALSE, 0, "event signaled, message"},
        {TRUE,  TRUE,  TRUE,  FALSE, FALSE, MWMO_ALERTABLE, "event signaled, message, ALERTABLE"},
        {TRUE,  TRUE,  TRUE,  FALSE, TRUE,  MWMO_ALERTABLE, "event signaled, message, ALERTABLE, APC"},
        {TRUE,  FALSE, TRUE,  FALSE, TRUE,  MWMO_ALERTABLE, "event not signaled, message, ALERTABLE, APC"},
        {TRUE,  TRUE,  FALSE, FALSE, TRUE,  MWMO_ALERTABLE, "event signaled, no message, ALERTABLE, APC"},
        {TRUE,  TRUE,  TRUE,  FALSE, FALSE, MWMO_WAITALL, "event signaled, message, WAITALL"},
        {TRUE,  FALSE, TRUE,  FALSE, FALSE, MWMO_WAITALL, "event not signaled, message, WAITALL"},
        {TRUE,  TRUE,  FALSE, FALSE, FALSE, MWMO_WAITALL, "event signaled, no message, WAITALL"},
        {TRUE,  TRUE,  TRUE,  FALSE, TRUE,  MWMO_WAITALL | MWMO_ALERTABLE, "event signaled, message, WAITALL|ALERTABLE, APC"},
        {TRUE,  TRUE,  TRUE,  TRUE,  FALSE, 0, "event signaled, message seen"},
        {TRUE,  FALSE, TRUE,  TRUE,  FALSE, 0, "event not signaled, message seen"},
        {TRUE,  TRUE,  TRUE,  TRUE,  FALSE, MWMO_INPUTAVAILABLE, "event signaled, message seen, INPUTAVAILABLE"},
        {TRUE,  FALSE, TRUE,  TRUE,  TRUE,  MWMO_INPUTAVAILABLE | MWMO_ALERTABLE, "event not signaled, message seen, INPUTAVAILABLE|ALERTABLE, APC"},
        {TRUE,  FALSE, TRUE,  TRUE,  TRUE,  MWMO_ALERTABLE, "event not signaled, message seen, ALERTABLE, APC"},
        {FALSE, FALSE, TRUE,  FALSE, FALSE, 0, "no handle, message"},
        {FALSE, FALSE, TRUE,  FALSE, TRUE,  MWMO_WAITALL | MWMO_ALERTABLE, "no handle, message, WAITALL|ALERTABLE, APC"},
        {FALSE, FALSE, TRUE,  FALSE, TRUE,  MWMO_ALERTABLE, "no handle, message, ALERTABLE, APC"},
    };
    char what[128], after[128];
    unsigned int i;
    HANDLE event;
    DWORD ret;
    MSG msg;

    for (i = 0; i < ARRAYSIZE(tests); i++)
    {
        event = CreateEventW( NULL, FALSE, tests[i].event_set, NULL );
        if (tests[i].message) post();
        if (tests[i].seen) PeekMessageW( &msg, NULL, 0, 0, PM_NOREMOVE );
        if (tests[i].apc) queue_apc();
        ret = MsgWaitForMultipleObjectsEx( tests[i].handle ? 1 : 0, &event, 0, QS_POSTMESSAGE, tests[i].flags );
        sprintf( after, "%s%s, message %s", tests[i].handle ? "event " : "", tests[i].handle ? signaled( event ) : "",
                 PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE ) ? "was there" : "was not" );
        sprintf( what, "MsgWait %s", tests[i].name );
        report( what, wait_name( ret ), after );
        while (PeekMessageW( &msg, NULL, 0, 0, PM_REMOVE ));
        CloseHandle( event );
    }
}

static void co_waits( DWORD model, const char *apartment )
{
    static const struct { BOOL event_set, apc; DWORD flags; const char *name; } tests[] =
    {
        {TRUE,  TRUE,  COWAIT_ALERTABLE, "event signaled, ALERTABLE, APC"},
        {TRUE,  TRUE,  0, "event signaled, APC"},
        {FALSE, TRUE,  COWAIT_ALERTABLE, "event not signaled, ALERTABLE, APC"},
        {FALSE, TRUE,  0, "event not signaled, APC"},
    };
    char what[128], ret[64], after[64];
    unsigned int i;
    HANDLE event;
    DWORD index;
    HRESULT hr;

    CoInitializeEx( NULL, model );
    for (i = 0; i < ARRAYSIZE(tests); i++)
    {
        event = CreateEventW( NULL, FALSE, tests[i].event_set, NULL );
        if (tests[i].apc) queue_apc();
        index = 0xdeadbeef;
        hr = CoWaitForMultipleHandles( tests[i].flags, 0, 1, &event, &index );
        sprintf( ret, "hr %#lx index %s", (unsigned long)hr, index == 0xdeadbeef ? "untouched" : wait_name( index ) );
        sprintf( after, "event %s", signaled( event ) );
        sprintf( what, "CoWait %s %s", apartment, tests[i].name );
        report( what, ret, after );
        CloseHandle( event );
    }
    CoUninitialize();
}

int main(void)
{
    MSG msg;

    PeekMessageW( &msg, NULL, 0, 0, PM_NOREMOVE );
    single_objects();
    several_objects();
    msg_waits();
    co_waits( COINIT_APARTMENTTHREADED, "STA" );
    co_waits( COINIT_MULTITHREADED, "MTA" );
    return 0;
}
