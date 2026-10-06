/* excstack <process.exe> [exception-code-hex] [count]
 *
 * 用标准调试 API 附加到同一 Wine 会话里名为 <process.exe> 的进程（取第一个），等待指定代码
 * （默认 c0000005）的第一次机会异常；命中时打印异常记录、寄存器和用 dbghelp StackWalk64 回溯的
 * 调用栈（模块!符号+偏移，或模块+RVA），然后把异常原样交还给目标（DBG_EXCEPTION_NOT_HANDLED），
 * 命中 count 次（默认 1）后分离。不改目标内存，不设断点；附加时的初始断点照惯例 DBG_CONTINUE。
 * 必须在目标启动完成后再附加：启动早期附加调试器本身可能让目标崩溃。
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

static HANDLE process;

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

static void load_modules(void)
{
    HMODULE mods[2048];
    DWORD needed = 0, i, loaded = 0;

    SymSetOptions( SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS );
    SymInitialize( process, NULL, FALSE );
    if (!EnumProcessModulesEx( process, mods, sizeof(mods), &needed, LIST_MODULES_ALL )) return;
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
        }
    }
    printf( "modules_loaded=%lu\n", loaded );
}

static void dump( const DEBUG_EVENT *ev )
{
    const EXCEPTION_RECORD *rec = &ev->u.Exception.ExceptionRecord;
    HANDLE thread = OpenThread( THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, ev->dwThreadId );
    CONTEXT ctx;
    STACKFRAME64 frame;
    char text[512];
    DWORD i;

    load_modules();
    describe( (DWORD64)(ULONG_PTR)rec->ExceptionAddress, text, sizeof(text) );
    printf( "exception %08lx first_chance=%lu tid=%04lx at %s\n", rec->ExceptionCode,
            ev->u.Exception.dwFirstChance, ev->dwThreadId, text );
    for (i = 0; i < rec->NumberParameters && i < 4; i++)
        printf( "  info[%lu]=%#llx\n", i, (unsigned long long)rec->ExceptionInformation[i] );
    if (!thread) { printf( "OpenThread failed %lu\n", GetLastError() ); return; }
    memset( &ctx, 0, sizeof(ctx) );
    ctx.ContextFlags = CONTEXT_FULL;
    if (!GetThreadContext( thread, &ctx )) { printf( "GetThreadContext failed %lu\n", GetLastError() ); CloseHandle( thread ); return; }
    printf( "  rax=%016llx rbx=%016llx rcx=%016llx rdx=%016llx\n", ctx.Rax, ctx.Rbx, ctx.Rcx, ctx.Rdx );
    printf( "  rsi=%016llx rdi=%016llx rbp=%016llx rsp=%016llx\n", ctx.Rsi, ctx.Rdi, ctx.Rbp, ctx.Rsp );
    printf( "   r8=%016llx  r9=%016llx r10=%016llx r11=%016llx\n", ctx.R8, ctx.R9, ctx.R10, ctx.R11 );
    printf( "  r12=%016llx r13=%016llx r14=%016llx r15=%016llx\n", ctx.R12, ctx.R13, ctx.R14, ctx.R15 );
    memset( &frame, 0, sizeof(frame) );
    frame.AddrPC.Offset = ctx.Rip; frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Rsp; frame.AddrStack.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Rbp; frame.AddrFrame.Mode = AddrModeFlat;
    for (i = 0; i < 48; i++)
    {
        if (!StackWalk64( IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &ctx, NULL,
                          SymFunctionTableAccess64, SymGetModuleBase64, NULL )) break;
        if (!frame.AddrPC.Offset) break;
        describe( frame.AddrPC.Offset, text, sizeof(text) );
        printf( "  #%02lu %s\n", i, text );
    }
    fflush( stdout );
    SymCleanup( process );
    CloseHandle( thread );
}

int main( int argc, char **argv )
{
    WCHAR name[MAX_PATH];
    PROCESSENTRY32W pe = {sizeof(pe)};
    HANDLE snap;
    DWORD pid = 0, code = 0xc0000005, want = 1, hits = 0, events = 0;
    BOOL attach_bp_seen = FALSE;
    DEBUG_EVENT ev;

    if (argc < 2) { fprintf( stderr, "usage: excstack process.exe [code-hex] [count]\n" ); return 2; }
    SetErrorMode( SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX );
    setvbuf( stdout, NULL, _IONBF, 0 );
    MultiByteToWideChar( CP_ACP, 0, argv[1], -1, name, ARRAYSIZE(name) );
    if (argc > 2) code = strtoul( argv[2], NULL, 16 );
    if (argc > 3) want = atoi( argv[3] );

    snap = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
    if (Process32FirstW( snap, &pe )) do
        if (!_wcsicmp( pe.szExeFile, name )) { pid = pe.th32ProcessID; break; }
    while (Process32NextW( snap, &pe ));
    CloseHandle( snap );
    if (!pid) { fprintf( stderr, "no process %ls\n", name ); return 3; }
    if (!DebugActiveProcess( pid )) { fprintf( stderr, "DebugActiveProcess %lu\n", GetLastError() ); return 4; }
    DebugSetProcessKillOnExit( FALSE );
    printf( "attached to %ls pid=%04lx, waiting for %08lx\n", name, pid, code );

    while (WaitForDebugEvent( &ev, INFINITE ))
    {
        DWORD status = DBG_CONTINUE;
        events++;
        switch (ev.dwDebugEventCode)
        {
        case CREATE_PROCESS_DEBUG_EVENT:
            process = ev.u.CreateProcessInfo.hProcess;
            if (ev.u.CreateProcessInfo.hFile) CloseHandle( ev.u.CreateProcessInfo.hFile );
            break;
        case LOAD_DLL_DEBUG_EVENT:
            if (ev.u.LoadDll.hFile) CloseHandle( ev.u.LoadDll.hFile );
            break;
        case EXCEPTION_DEBUG_EVENT:
            if (!attach_bp_seen && ev.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT)
            {
                attach_bp_seen = TRUE;      /* the breakpoint every attach raises */
                break;
            }
            status = DBG_EXCEPTION_NOT_HANDLED;
            if (ev.u.Exception.ExceptionRecord.ExceptionCode == code && ev.u.Exception.dwFirstChance)
            {
                dump( &ev );
                hits++;
            }
            break;
        case EXIT_PROCESS_DEBUG_EVENT:
            printf( "process exited, code %lu, hits %lu\n", ev.u.ExitProcess.dwExitCode, hits );
            ContinueDebugEvent( ev.dwProcessId, ev.dwThreadId, DBG_CONTINUE );
            return hits ? 0 : 1;
        }
        ContinueDebugEvent( ev.dwProcessId, ev.dwThreadId, status );
        if (hits >= want)
        {
            DebugActiveProcessStop( pid );
            printf( "detached after %lu hit(s), %lu events\n", hits, events );
            return 0;
        }
    }
    printf( "WaitForDebugEvent failed %lu\n", GetLastError() );
    return 5;
}
