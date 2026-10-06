/* wsample <window-class> <x> <y> <duration_ms> <interval_ms> [depth]
 *
 * 同一 Wine 会话里的调用栈采样器：找到 <window-class> 窗口所属线程，在 (x, y) 发一次左键点击，
 * 之后每 interval_ms 暂停该线程、取上下文并复制栈顶 64 KB，立即恢复线程，再用快照离线回溯。
 * 每个样本打印相对点击的时间和前 depth 帧（模块!符号+偏移，或模块+RVA）。
 * 只读目标进程内存，不写入；只发送一次左键点击。 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define STACK_COPY (64 * 1024)

static HANDLE process;
static BYTE stack_copy[STACK_COPY];
static DWORD64 stack_base;
static SIZE_T stack_len;

static BOOL CALLBACK read_mem( HANDLE proc, DWORD64 addr, void *buf, DWORD size, DWORD *read )
{
    SIZE_T done = 0;
    if (addr >= stack_base && addr + size <= stack_base + stack_len)
    {
        memcpy( buf, stack_copy + (addr - stack_base), size );
        if (read) *read = size;
        return TRUE;
    }
    if (!ReadProcessMemory( proc, (void *)(ULONG_PTR)addr, buf, size, &done )) return FALSE;
    if (read) *read = (DWORD)done;
    return TRUE;
}

static void describe( DWORD64 addr, char *out, size_t len )
{
    char buf[sizeof(SYMBOL_INFO) + 256];
    SYMBOL_INFO *sym = (SYMBOL_INFO *)buf;
    IMAGEHLP_MODULE64 mod = {sizeof(mod)};
    DWORD64 disp = 0;
    const char *modname = "?";

    if (SymGetModuleInfo64( process, addr, &mod )) modname = mod.ModuleName;
    memset( buf, 0, sizeof(buf) );
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    if (SymFromAddr( process, addr, &disp, sym ) && disp < 0x100000)
        snprintf( out, len, "%s!%s+%#llx", modname, sym->Name, (unsigned long long)disp );
    else if (mod.BaseOfImage)
        snprintf( out, len, "%s+%#llx", modname, (unsigned long long)(addr - mod.BaseOfImage) );
    else
        snprintf( out, len, "%#llx", (unsigned long long)addr );
}

int main( int argc, char **argv )
{
    typedef BOOL (WINAPI *dpi_fn)(DPI_AWARENESS_CONTEXT);
    dpi_fn set_dpi = (dpi_fn)(void *)GetProcAddress( GetModuleHandleW( L"user32.dll" ), "SetProcessDpiAwarenessContext" );
    WCHAR cls[128];
    HWND target;
    DWORD pid, tid, duration, interval, depth, n = 0;
    HANDLE thread;
    LARGE_INTEGER freq, t0, now;
    INPUT in[3] = {0};
    int x, y;

    if (argc < 6) { fprintf( stderr, "usage: wsample class x y duration_ms interval_ms [depth]\n" ); return 2; }
    SetErrorMode( SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX );
    if (set_dpi) set_dpi( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );
    setvbuf( stdout, NULL, _IOFBF, 4 << 20 );
    MultiByteToWideChar( CP_ACP, 0, argv[1], -1, cls, ARRAYSIZE(cls) );
    x = atoi( argv[2] ); y = atoi( argv[3] );
    duration = atoi( argv[4] ); interval = atoi( argv[5] ); depth = argc > 6 ? atoi( argv[6] ) : 14;
    if (!(target = FindWindowW( cls, NULL ))) { fprintf( stderr, "no window\n" ); return 3; }
    tid = GetWindowThreadProcessId( target, &pid );
    if (getenv( "WSAMPLE_TID" )) tid = strtoul( getenv( "WSAMPLE_TID" ), NULL, 16 );
    if (!(process = OpenProcess( PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid ))) { fprintf( stderr, "OpenProcess %lu\n", GetLastError() ); return 4; }
    if (!(thread = OpenThread( THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, tid ))) { fprintf( stderr, "OpenThread %lu\n", GetLastError() ); return 5; }
    SymSetOptions( SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS );
    if (!SymInitialize( process, NULL, FALSE )) { fprintf( stderr, "SymInitialize %lu\n", GetLastError() ); return 6; }
    {
        /* load every module explicitly: the automatic enumeration misses images Wine copied into anonymous memory */
        HMODULE mods[2048];
        DWORD needed = 0, i, loaded = 0;
        if (EnumProcessModulesEx( process, mods, sizeof(mods), &needed, LIST_MODULES_ALL ))
        {
            for (i = 0; i < needed / sizeof(HMODULE) && i < ARRAYSIZE(mods); i++)
            {
                WCHAR path[MAX_PATH];
                MODULEINFO mi;
                if (!GetModuleFileNameExW( process, mods[i], path, MAX_PATH )) continue;
                if (!GetModuleInformation( process, mods[i], &mi, sizeof(mi) )) continue;
                if (SymLoadModuleExW( process, NULL, path, NULL, (DWORD64)(ULONG_PTR)mi.lpBaseOfDll, mi.SizeOfImage, NULL, 0 )) loaded++;
                else if (!_wcsnicmp( path, L"C:\\Program Files\\Common Files\\", 30 ))
                {
                    /* Office Click-to-Run virtualizes this folder for its own processes only */
                    WCHAR vfs[MAX_PATH];
                    swprintf( vfs, MAX_PATH, L"C:\\Program Files\\Microsoft Office\\root\\vfs\\ProgramFilesCommonX64\\%ls", path + 30 );
                    if (SymLoadModuleExW( process, NULL, vfs, NULL, (DWORD64)(ULONG_PTR)mi.lpBaseOfDll, mi.SizeOfImage, NULL, 0 )) loaded++;
                    else if (getenv( "WSAMPLE_DEBUG" )) printf( "symload_failed err=%lu %ls\n", GetLastError(), vfs );
                }
                else if (GetLastError() != ERROR_SUCCESS && getenv( "WSAMPLE_DEBUG" ))
                    printf( "symload_failed base=%p size=%#lx err=%lu %ls\n", mi.lpBaseOfDll, mi.SizeOfImage, GetLastError(), path );
                if (getenv( "WSAMPLE_MODS" )) printf( "mod %p %#lx %ls\n", mi.lpBaseOfDll, mi.SizeOfImage, path );
            }
        }
        printf( "modules_loaded=%lu\n", loaded );
    }
    printf( "pid=%04lx tid=%04lx interval=%lu duration=%lu\n", pid, tid, interval, duration );

    in[0].type = INPUT_MOUSE;
    in[0].mi.dx = MulDiv( x - GetSystemMetrics(SM_XVIRTUALSCREEN), 65535, GetSystemMetrics(SM_CXVIRTUALSCREEN) - 1 );
    in[0].mi.dy = MulDiv( y - GetSystemMetrics(SM_YVIRTUALSCREEN), 65535, GetSystemMetrics(SM_CYVIRTUALSCREEN) - 1 );
    in[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    in[1].type = INPUT_MOUSE; in[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    in[2].type = INPUT_MOUSE; in[2].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput( 1, &in[0], sizeof(INPUT) );
    Sleep( 60 );
    QueryPerformanceFrequency( &freq );
    QueryPerformanceCounter( &t0 );
    if (getenv( "WSAMPLE_BUSY" ))
    {
        /* measure each thread's CPU time across the click instead of sampling */
        static DWORD tids[4096]; static ULONGLONG before[4096];
        DWORD count = 0, j;
        THREADENTRY32 te = {sizeof(te)};
        HANDLE snap = CreateToolhelp32Snapshot( TH32CS_SNAPTHREAD, 0 );
        if (Thread32First( snap, &te )) do
            if (te.th32OwnerProcessID == pid && count < 4096) tids[count++] = te.th32ThreadID;
        while (Thread32Next( snap, &te ));
        CloseHandle( snap );
        for (j = 0; j < count; j++)
        {
            FILETIME c, e, k, u; HANDLE h = OpenThread( THREAD_QUERY_INFORMATION, FALSE, tids[j] );
            before[j] = 0;
            if (h && GetThreadTimes( h, &c, &e, &k, &u ))
                before[j] = ((ULONGLONG)k.dwHighDateTime << 32 | k.dwLowDateTime) + ((ULONGLONG)u.dwHighDateTime << 32 | u.dwLowDateTime);
            if (h) CloseHandle( h );
        }
        SendInput( 2, &in[1], sizeof(INPUT) );
        Sleep( duration );
        for (j = 0; j < count; j++)
        {
            FILETIME c, e, k, u; HANDLE h = OpenThread( THREAD_QUERY_INFORMATION, FALSE, tids[j] );
            ULONGLONG after = 0;
            if (h && GetThreadTimes( h, &c, &e, &k, &u ))
                after = ((ULONGLONG)k.dwHighDateTime << 32 | k.dwLowDateTime) + ((ULONGLONG)u.dwHighDateTime << 32 | u.dwLowDateTime);
            if (h) CloseHandle( h );
            if (after > before[j] + 50000) printf( "busy tid=%04lx cpu_ms=%llu\n", tids[j], (after - before[j]) / 10000 );
        }
        return 0;
    }
    SendInput( 2, &in[1], sizeof(INPUT) );

    for (;;)
    {
        CONTEXT ctx;
        STACKFRAME64 frame;
        SIZE_T done = 0;
        double ms;
        DWORD i;
        BYTE op = 0;
        char text[512];

        QueryPerformanceCounter( &now );
        ms = (double)(now.QuadPart - t0.QuadPart) * 1000.0 / freq.QuadPart;
        if (ms > duration) break;

        if (SuspendThread( thread ) == (DWORD)-1) break;
        memset( &ctx, 0, sizeof(ctx) );
        ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        if (!GetThreadContext( thread, &ctx )) { ResumeThread( thread ); Sleep( interval ); continue; }
        stack_base = ctx.Rsp;
        ReadProcessMemory( process, (void *)(ULONG_PTR)stack_base, stack_copy, STACK_COPY, &done );
        if (!done)
        {
            /* the stack may end before 64 KB: copy page by page */
            stack_len = 0;
            while (stack_len < STACK_COPY)
            {
                SIZE_T chunk = 4096 - ((stack_base + stack_len) & 4095);
                if (chunk > STACK_COPY - stack_len) chunk = STACK_COPY - stack_len;
                if (!ReadProcessMemory( process, (void *)(ULONG_PTR)(stack_base + stack_len), stack_copy + stack_len,
                                        chunk, &done ) || !done) break;
                stack_len += done;
            }
        }
        else stack_len = done;
        ResumeThread( thread );

        printf( "S %8.1f", ms );
        if (getenv( "WSAMPLE_DEBUG" ))
        {
            DWORD64 top = 0;
            if (stack_len >= 8) memcpy( &top, stack_copy, 8 );
            describe( top, text, sizeof(text) );
            printf( " [rip=%#llx rsp=%#llx rbp=%#llx len=%#llx ft=%p top=%s]", (unsigned long long)ctx.Rip,
                    (unsigned long long)ctx.Rsp, (unsigned long long)ctx.Rbp, (unsigned long long)stack_len,
                    SymFunctionTableAccess64( process, ctx.Rip ), text );
        }
        /* a thread inside a syscall reports the thunk's ret instruction: unwind that leaf by hand */
        if (stack_len >= 8 && read_mem( process, ctx.Rip, &op, 1, NULL ) && op == 0xc3)
        {
            describe( ctx.Rip, text, sizeof(text) );
            printf( " | %s", text );
            memcpy( &ctx.Rip, stack_copy, sizeof(ctx.Rip) );
            ctx.Rsp += 8;
        }
        memset( &frame, 0, sizeof(frame) );
        frame.AddrPC.Offset = ctx.Rip; frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrStack.Offset = ctx.Rsp; frame.AddrStack.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = ctx.Rbp; frame.AddrFrame.Mode = AddrModeFlat;
        for (i = 0; i < depth; i++)
        {
            if (!StackWalk64( IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &ctx, read_mem,
                              SymFunctionTableAccess64, SymGetModuleBase64, NULL )) break;
            if (!frame.AddrPC.Offset) break;
            describe( frame.AddrPC.Offset, text, sizeof(text) );
            printf( " | %s", text );
        }
        printf( "\n" );
        n++;
        Sleep( interval );
    }
    printf( "samples=%lu\n", n );
    SymCleanup( process );
    return 0;
}
