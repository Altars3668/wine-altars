/*
 * ownerprobe -- what happens to a window owned by a window of another thread or process when
 * that owner is destroyed, and whether the owner of another process's window can be set.
 *
 * Wine clears the owner of windows of other threads of the destroying process, and for windows of
 * other processes only prints "cannot set owner (nil) on other process window"; the server keeps
 * the dead handle.  This asks Windows, with hidden windows only: no input, nothing shown.
 *
 *   ownerprobe            the whole run (starts itself again as the other process)
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

static const WCHAR class_name[] = L"OwnerProbeClass";

static LRESULT CALLBACK wndproc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    return DefWindowProcW( hwnd, msg, wp, lp );
}

static HWND create( const WCHAR *title, HWND owner )
{
    return CreateWindowExW( 0, class_name, title, WS_POPUP, 10, 10, 50, 50, owner, 0, NULL, NULL );
}

static void show_owner( const char *what, HWND hwnd, HWND expect )
{
    HWND owner = GetWindow( hwnd, GW_OWNER );
    LONG_PTR parent = GetWindowLongPtrW( hwnd, GWLP_HWNDPARENT );
    printf( "  %-40s IsWindow %d GW_OWNER %s GWLP_HWNDPARENT %s\n", what, IsWindow( hwnd ),
            !owner ? "NULL" : owner == expect ? "old owner" : "other",
            !parent ? "NULL" : (HWND)parent == expect ? "old owner" : "other" );
    fflush( stdout );
}

struct thread_args { HWND owner; HWND owned; HANDLE ready, done; };

static DWORD WINAPI thread_proc( void *arg )
{
    struct thread_args *args = arg;
    MSG msg;

    args->owned = create( L"owned by another thread", args->owner );
    SetEvent( args->ready );
    while (MsgWaitForMultipleObjects( 1, &args->done, FALSE, INFINITE, QS_ALLINPUT ) != WAIT_OBJECT_0)
        while (PeekMessageW( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessageW( &msg );
    DestroyWindow( args->owned );
    return 0;
}

static void pump( DWORD ms )
{
    DWORD end = GetTickCount() + ms;
    MSG msg;
    while ((LONG)(end - GetTickCount()) > 0)
    {
        while (PeekMessageW( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessageW( &msg );
        Sleep( 10 );
    }
}

/* the other process: a window, its owner as this process sees it on request, until told to stop */
static void print_placement( const char *who, const WINDOWPLACEMENT *wp )
{
    printf( "  %-12s flags %#x showCmd %u min (%ld,%ld) max (%ld,%ld) normal (%ld,%ld)-(%ld,%ld)\n", who,
            wp->flags, wp->showCmd, wp->ptMinPosition.x, wp->ptMinPosition.y, wp->ptMaxPosition.x,
            wp->ptMaxPosition.y, wp->rcNormalPosition.left, wp->rcNormalPosition.top,
            wp->rcNormalPosition.right, wp->rcNormalPosition.bottom );
    fflush( stdout );
}

/* the steps the other process takes with its window, one per request */
static void child_step( HWND hwnd, int step )
{
    static const int commands[] = { SW_SHOWNOACTIVATE, SW_MAXIMIZE, SW_MINIMIZE, SW_RESTORE, SW_RESTORE, -1 };
    WINDOWPLACEMENT wp = { sizeof(wp) };

    if (step < ARRAYSIZE(commands) && commands[step] >= 0)
    {
        if (!step) SetWindowPos( hwnd, 0, 100, 120, 300, 200, SWP_NOZORDER | SWP_NOACTIVATE );
        ShowWindow( hwnd, commands[step] );
    }
    GetWindowPlacement( hwnd, &wp );
    print_placement( "(its own)", &wp );
}

static int child( void )
{
    HANDLE query = OpenEventW( EVENT_ALL_ACCESS, FALSE, L"OwnerProbeQuery" );
    HANDLE answered = OpenEventW( EVENT_ALL_ACCESS, FALSE, L"OwnerProbeAnswered" );
    HANDLE quit = OpenEventW( EVENT_ALL_ACCESS, FALSE, L"OwnerProbeQuit" );
    HANDLE step_event = OpenEventW( EVENT_ALL_ACCESS, FALSE, L"OwnerProbeStep" );
    HANDLE events[3] = { query, quit, step_event };
    int step = 0;
    HWND hwnd = create( L"OwnerProbeChildWindow", 0 );
    MSG msg;

    if (!query || !answered || !quit || !step_event || !hwnd) return 1;
    SetEvent( answered );
    for (;;)
    {
        DWORD ret = MsgWaitForMultipleObjects( 3, events, FALSE, INFINITE, QS_ALLINPUT );
        if (ret == WAIT_OBJECT_0)
        {
            HWND owner = GetWindow( hwnd, GW_OWNER );
            printf( "  %-40s IsWindow %d GW_OWNER %s, IsWindow(owner) %d\n", "(the owned window's own process)",
                    IsWindow( hwnd ), owner ? "set" : "NULL", owner ? IsWindow( owner ) : -1 );
            fflush( stdout );
            SetEvent( answered );
        }
        else if (ret == WAIT_OBJECT_0 + 1) break;
        else if (ret == WAIT_OBJECT_0 + 2)
        {
            child_step( hwnd, step++ );
            SetEvent( answered );
        }
        else while (PeekMessageW( &msg, 0, 0, 0, PM_REMOVE )) DispatchMessageW( &msg );
    }
    DestroyWindow( hwnd );
    return 0;
}

static void ask_child( HANDLE query, HANDLE answered )
{
    fflush( stdout );
    SetEvent( query );
    WaitForSingleObject( answered, 5000 );
}

int main( int argc, char **argv )
{
    WNDCLASSW cls = { 0 };
    struct thread_args args;
    HANDLE query, answered, quit, thread, step_event;
    PROCESS_INFORMATION pi;
    STARTUPINFOW si = { sizeof(si) };
    WCHAR cmd[MAX_PATH + 16];
    HWND owner, owned, other;
    LONG_PTR prev;

    cls.lpfnWndProc = wndproc;
    cls.lpszClassName = class_name;
    RegisterClassW( &cls );
    if (argc > 1 && !strcmp( argv[1], "child" )) return child();

    printf( "same thread\n" );
    owner = create( L"owner", 0 );
    owned = create( L"owned", owner );
    show_owner( "before", owned, owner );
    DestroyWindow( owner );
    show_owner( "after DestroyWindow(owner)", owned, owner );
    if (IsWindow( owned )) DestroyWindow( owned );

    printf( "another thread of the process\n" );
    owner = create( L"owner", 0 );
    args.owner = owner;
    args.ready = CreateEventW( NULL, FALSE, FALSE, NULL );
    args.done = CreateEventW( NULL, FALSE, FALSE, NULL );
    thread = CreateThread( NULL, 0, thread_proc, &args, 0, NULL );
    WaitForSingleObject( args.ready, INFINITE );
    show_owner( "before", args.owned, owner );
    DestroyWindow( owner );
    show_owner( "after DestroyWindow(owner)", args.owned, owner );
    pump( 200 );
    show_owner( "200 ms later", args.owned, owner );
    SetEvent( args.done );
    WaitForSingleObject( thread, INFINITE );

    printf( "another process\n" );
    query = CreateEventW( NULL, FALSE, FALSE, L"OwnerProbeQuery" );
    answered = CreateEventW( NULL, FALSE, FALSE, L"OwnerProbeAnswered" );
    quit = CreateEventW( NULL, TRUE, FALSE, L"OwnerProbeQuit" );
    step_event = CreateEventW( NULL, FALSE, FALSE, L"OwnerProbeStep" );
    GetModuleFileNameW( NULL, cmd, MAX_PATH );
    wcscat( cmd, L" child" );
    {
        WCHAR line[MAX_PATH + 32];
        swprintf( line, ARRAYSIZE(line), L"\"%s", cmd );
        wcscpy( wcsrchr( line, ' ' ), L"\" child" );
        if (!CreateProcessW( NULL, line, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi ))
        {
            printf( "  CreateProcess failed %lu\n", GetLastError() );
            return 1;
        }
    }
    if (WaitForSingleObject( answered, 10000 )) { printf( "  child did not start\n" ); return 1; }
    other = FindWindowW( class_name, L"OwnerProbeChildWindow" );
    printf( "  found the other process's window: %d\n", other != NULL );
    fflush( stdout );
    owner = create( L"owner", 0 );
    SetLastError( 0xdeadbeef );
    prev = SetWindowLongPtrW( other, GWLP_HWNDPARENT, (LONG_PTR)owner );
    printf( "  SetWindowLongPtr(GWLP_HWNDPARENT) of the other process's window: returned %s, error %lu\n",
            prev ? "non-NULL" : "NULL", GetLastError() );
    show_owner( "after setting the owner", other, owner );
    ask_child( query, answered );
    DestroyWindow( owner );
    show_owner( "after DestroyWindow(owner)", other, owner );
    ask_child( query, answered );
    pump( 200 );
    show_owner( "200 ms later", other, owner );
    ask_child( query, answered );

    printf( "another process sets its own window's owner to a window of this one\n" );
    owner = create( L"owner", 0 );
    /* the child cannot be told which window to use without more plumbing: set it from here,
     * then check that its own SetWindowLongPtr(0) clears it */
    SetWindowLongPtrW( other, GWLP_HWNDPARENT, (LONG_PTR)owner );
    show_owner( "set again", other, owner );
    SetWindowLongPtrW( other, GWLP_HWNDPARENT, 0 );
    show_owner( "cleared by SetWindowLongPtr(0)", other, owner );
    DestroyWindow( owner );

    printf( "placement of the other process's window\n" );
    {
        static const char *steps[] = { "shown at (100,120) 300x200", "maximized", "minimized from maximized",
                                       "restored", "restored again" };
        WINDOWPLACEMENT wp;
        unsigned int k;
        BOOL ret;

        for (k = 0; k < ARRAYSIZE(steps); k++)
        {
            fflush( stdout );
            SetEvent( step_event );
            WaitForSingleObject( answered, 5000 );
            memset( &wp, 0xcc, sizeof(wp) );
            wp.length = sizeof(wp);
            SetLastError( 0xdeadbeef );
            ret = GetWindowPlacement( other, &wp );
            printf( "  %s: GetWindowPlacement %d error %lu\n", steps[k], ret, ret ? 0 : GetLastError() );
            if (ret) print_placement( "(this one)", &wp );
        }
        /* move its normal position from here, while it is shown normally */
        wp.length = sizeof(wp);
        GetWindowPlacement( other, &wp );
        OffsetRect( &wp.rcNormalPosition, 50, 30 );
        wp.showCmd = SW_SHOWNOACTIVATE;
        SetLastError( 0xdeadbeef );
        ret = SetWindowPlacement( other, &wp );
        printf( "  SetWindowPlacement of the other process's window: %d error %lu\n", ret, ret ? 0 : GetLastError() );
        Sleep( 100 );
        {
            RECT rect;
            GetWindowRect( other, &rect );
            printf( "  its window rect now (%ld,%ld)-(%ld,%ld)\n", rect.left, rect.top, rect.right, rect.bottom );
        }
        SetEvent( step_event );
        WaitForSingleObject( answered, 5000 );
    }

    SetEvent( quit );
    WaitForSingleObject( pi.hProcess, 5000 );
    CloseHandle( pi.hProcess );
    CloseHandle( pi.hThread );
    return 0;
}
