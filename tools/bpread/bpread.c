/*
 * bpread -- patch a one-shot int3 into a named module at a given RVA the
 * moment that module loads (or is found already loaded), and print registers
 * when it hits.
 *
 * Standard Win32 debugging API (CreateProcess with DEBUG_PROCESS or
 * DebugActiveProcess, WaitForDebugEvent, Read/WriteProcessMemory,
 * Get/SetThreadContext) -- exactly what winedbg itself is built on, so it
 * needs no injected code and no hardware breakpoints (Dr0-7 are known not to
 * work under Wine; this avoids them entirely).
 *
 * Three ways to get a target:
 *
 *   bpread <module.dll> <rva_hex[,rva_hex...]> <hit_count> -- <cmd> [args...]
 *   bpread <module.dll> <rvas> <hit_count> -- --attach <wine_pid>
 *   bpread <module.dll> <rvas> <hit_count> -- --waitfor <exe_name>
 *
 * The launch form (plain <cmd>) is the one to reach for first -- but
 * launching WINWORD.EXE this way crashes it with STATUS_ACCESS_VIOLATION,
 * reproducibly, the first time it is tried against Office: this is not a bug
 * in this tool (confirmed by attaching with zero breakpoints armed at all --
 * still crashes) but something about WINWORD.EXE's own early startup that a
 * debugger's mere presence breaks. --waitfor polls tools/winenum-style
 * process enumeration for the name and attaches the instant it appears --
 * still early enough to race a one-shot check, but exactly as fatal if it
 * catches the process too early. --attach against an *already stable*
 * process (main window up) is reliably safe, but by then a one-shot startup
 * check has typically already run and will not be seen. Which of these two
 * failure modes you get is a real, open question for whatever check you are
 * chasing -- this tool does not solve it, only makes it fast to try.
 *
 * Prints, per hit: rip, rax, rcx, rdx, r8, r9, r10, r11, and the first eight
 * qwords at [rcx] (a vtable, if rcx is an object) plus [rcx+2b8]-style reads
 * are left to the caller to add -- this tool is deliberately generic.
 *
 * DebugSetProcessKillOnExit(FALSE) is called immediately so a crash in this
 * tool does not take the debuggee down with it.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BP 16

struct bp
{
    DWORD64 va;
    BYTE orig;
    BOOL armed;
};

static struct bp bps[MAX_BP];
static int n_bp;
static DWORD64 module_base;
static char module_name[MAX_PATH];
static int hits_wanted, hits_seen;
static BOOL first_exception_seen;
static LONG64 rbp_dump_offset;
static SIZE_T rbp_dump_bytes;
static DWORD64 r14_offsets[MAX_BP];
static unsigned int r14_offset_count;
static BOOL fields_only;
static BOOL code_target;
static BOOL hresult_only;

static void set_bp(HANDLE proc, struct bp *b)
{
    SIZE_T n;
    BYTE cc = 0xCC;
    if (!ReadProcessMemory(proc, (LPCVOID)b->va, &b->orig, 1, &n) || n != 1)
    {
        fprintf(stderr, "ReadProcessMemory at %llx failed, gle=%lu\n", b->va, GetLastError());
        return;
    }
    if (!WriteProcessMemory(proc, (LPVOID)b->va, &cc, 1, &n) || n != 1)
    {
        fprintf(stderr, "WriteProcessMemory at %llx failed, gle=%lu\n", b->va, GetLastError());
        return;
    }
    FlushInstructionCache(proc, (LPCVOID)b->va, 1);
    b->armed = TRUE;
    printf("armed breakpoint at %llx (orig byte %02x)\n", b->va, b->orig);
}

static void clear_bp(HANDLE proc, struct bp *b)
{
    SIZE_T n;
    if (!b->armed) return;
    WriteProcessMemory(proc, (LPVOID)b->va, &b->orig, 1, &n);
    FlushInstructionCache(proc, (LPCVOID)b->va, 1);
    b->armed = FALSE;
}

static struct bp *find_bp(DWORD64 va)
{
    int i;
    for (i = 0; i < n_bp; i++) if (bps[i].va == va) return &bps[i];
    return NULL;
}

/* Case-insensitive substring: module paths came back with different case
 * than the on-disk name once already, not worth trusting case to match. */
static BOOL ci_contains(const char *haystack, const char *needle)
{
    size_t hlen = strlen(haystack), nlen = strlen(needle), i;
    if (nlen > hlen) return FALSE;
    for (i = 0; i + nlen <= hlen; i++)
        if (!_strnicmp(haystack + i, needle, nlen)) return TRUE;
    return FALSE;
}

int main(int argc, char **argv)
{
    char cmdline[4096] = {0};
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    DEBUG_EVENT ev;
    HANDLE proc = NULL;
    int i, rva_argi;
    char *rvas, *tok;

    const char *dump_offset_env, *dump_bytes_env;

    setvbuf(stdout, NULL, _IONBF, 0); /* redirected to a file gets fully
        buffered by default; unbuffered so a reader watching the file live
        (or a run that gets killed) actually sees output as it happens */

    dump_offset_env = getenv("BPREAD_RBP_OFFSET");
    dump_bytes_env = getenv("BPREAD_DUMP_BYTES");
    if (dump_offset_env) rbp_dump_offset = strtoll(dump_offset_env, NULL, 0);
    if (dump_bytes_env) rbp_dump_bytes = strtoul(dump_bytes_env, NULL, 0);
    if (rbp_dump_bytes > 256) rbp_dump_bytes = 256;
    fields_only = getenv("BPREAD_FIELDS_ONLY") && !strcmp(getenv("BPREAD_FIELDS_ONLY"), "1");
    code_target = getenv("BPREAD_CODE_TARGET") && !strcmp(getenv("BPREAD_CODE_TARGET"), "1");
    hresult_only = getenv("BPREAD_HRESULT_ONLY") && !strcmp(getenv("BPREAD_HRESULT_ONLY"), "1");
    if (hresult_only) code_target = FALSE;
    if (code_target || hresult_only) fields_only = TRUE;
    if (!code_target && !hresult_only && getenv("BPREAD_R14_QWORDS"))
    {
        char *list = _strdup(getenv("BPREAD_R14_QWORDS")), *end;
        if (!list) return 1;
        for (tok = strtok(list, ","); tok; tok = strtok(NULL, ","))
        {
            DWORD64 offset = strtoull(tok, &end, 0);
            if (*end || offset > 4096 || offset % 8 || r14_offset_count == MAX_BP)
            {
                fprintf(stderr, "invalid BPREAD_R14_QWORDS offset\n");
                free(list);
                return 1;
            }
            r14_offsets[r14_offset_count++] = offset;
        }
        free(list);
    }

    /* argv: [0]=self [1]=module [2]=rvas(csv) [3]=hitcount [4]="--" [5..]=cmd */
    if (argc < 6 || strcmp(argv[4], "--") != 0)
    {
        fprintf(stderr, "usage: %s <module.dll> <rva_hex[,rva_hex...]> <hit_count> -- <cmd> [args...]\n", argv[0]);
        return 1;
    }
    strncpy(module_name, argv[1], sizeof(module_name)-1);
    rvas = _strdup(argv[2]);
    hits_wanted = atoi(argv[3]);
    rva_argi = 5;

    tok = strtok(rvas, ",");
    while (tok && n_bp < MAX_BP)
    {
        bps[n_bp].va = strtoull(tok, NULL, 16); /* RVA for now; add base once known */
        n_bp++;
        tok = strtok(NULL, ",");
    }

    if (!strcmp(argv[rva_argi], "--attach") || !strcmp(argv[rva_argi], "--waitfor"))
    {
        /* --attach takes wineserver's own small-integer process id -- the one
         * a Win32 tool like tools/winenum prints -- not the Linux pid `pgrep`
         * or `ps` report. OpenProcess against the Linux pid fails with
         * ERROR_INVALID_PARAMETER for *every* access mask, including
         * PROCESS_QUERY_LIMITED_INFORMATION alone, which is what makes the
         * two easy to conflate: it looks like a rights problem and is
         * actually a wrong-namespace problem. --waitfor <exe.exe> takes the
         * process name instead and polls toolhelp for it, for racing a
         * breakpoint against a licensing check that may only ever run once,
         * early -- launching WINWORD.EXE straight under DEBUG_PROCESS crashed
         * it reproducibly (see below), so the only way to be attached before
         * such a one-shot check runs is to attach within moments of the
         * ordinary, working launch path creating it.
         *
         * Attach to an already-running process instead of launching one --
         * whatever made WINWORD.EXE crash early under DEBUG_PROCESS (twice,
         * at the same address, only when launched under a debugger from the
         * start) is sidestepped by attaching once it is already stably
         * running. Unlike real Windows, Wine's DebugActiveProcess does NOT
         * synthesize a LOAD_DLL_DEBUG_EVENT for modules already loaded before
         * the attach (checked directly: the target module was mapped per
         * /proc/<pid>/maps, but never arrived as an event here) -- so those
         * have to be found through toolhelp instead, right after attaching. */
        HANDLE snap;
        MODULEENTRY32 me = { sizeof(me) };
        DWORD pid;

        if (!strcmp(argv[rva_argi], "--waitfor"))
        {
            const char *exe = argv[rva_argi + 1];
            PROCESSENTRY32 pe;
            int loops = 0;
            pid = 0;
            printf("waiting for %s ...\n", exe);
            while (!pid)
            {
                HANDLE psnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
                pe.dwSize = sizeof(pe); /* reset every call: some Process32First
                    implementations are picky about a stale dwSize from a
                    previous loop iteration even though the struct is nominally
                    just an input/output buffer */
                if (psnap == INVALID_HANDLE_VALUE)
                    printf("  CreateToolhelp32Snapshot failed, gle=%lu\n", GetLastError());
                else if (!Process32First(psnap, &pe))
                    printf("  Process32First failed, gle=%lu\n", GetLastError());
                else
                {
                    do { if (ci_contains(pe.szExeFile, exe)) { pid = pe.th32ProcessID; break; } }
                    while (Process32Next(psnap, &pe));
                }
                if (psnap != INVALID_HANDLE_VALUE) CloseHandle(psnap);
                if (!pid)
                {
                    if (++loops % 50 == 0) printf("  still waiting (%d polls)\n", loops);
                    Sleep(20);
                }
            }
            printf("found %s as pid %lu\n", exe, pid);
        }
        else pid = strtoul(argv[rva_argi + 1], NULL, 10);

        printf("attaching to pid %lu\n", pid);
        if (!DebugActiveProcess(pid))
        {
            fprintf(stderr, "DebugActiveProcess failed, gle=%lu\n", GetLastError());
            return 1;
        }
        proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        pi.dwProcessId = pid;

        snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
        if (snap != INVALID_HANDLE_VALUE && Module32First(snap, &me))
        {
            do
            {
                printf("  module: %p-%p %s\n", me.modBaseAddr,
                        me.modBaseAddr + me.modBaseSize, me.szExePath);
                if (ci_contains(me.szExePath, module_name) && !module_base)
                {
                    module_base = (DWORD64)(UINT_PTR)me.modBaseAddr;
                    printf("%s already loaded at %llx (%s)\n", module_name, module_base, me.szExePath);
                    for (i = 0; i < n_bp; i++)
                    {
                        bps[i].va += module_base;
                        set_bp(proc, &bps[i]);
                    }
                }
            } while (Module32Next(snap, &me));
        }
        else fprintf(stderr, "CreateToolhelp32Snapshot/Module32First failed, gle=%lu\n", GetLastError());
        if (snap != INVALID_HANDLE_VALUE) CloseHandle(snap);
    }
    else
    {
        for (i = rva_argi; i < argc; i++)
        {
            strcat(cmdline, argv[i]);
            if (i + 1 < argc) strcat(cmdline, " ");
        }
        printf("launching: %s\n", cmdline);

        if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE,
                DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS, NULL, NULL, &si, &pi))
        {
            fprintf(stderr, "CreateProcess failed, gle=%lu\n", GetLastError());
            return 1;
        }
        proc = pi.hProcess;
    }
    DebugSetProcessKillOnExit(FALSE);

    for (;;)
    {
        DWORD cont = DBG_CONTINUE;
        if (!WaitForDebugEvent(&ev, 5 * 60 * 1000))
        {
            fprintf(stderr, "WaitForDebugEvent timed out or failed, gle=%lu\n", GetLastError());
            break;
        }

        switch (ev.dwDebugEventCode)
        {
        case CREATE_PROCESS_DEBUG_EVENT:
        {
            WCHAR path[MAX_PATH] = {0};
            char pathA[MAX_PATH] = {0};
            HANDLE hf = ev.u.CreateProcessInfo.hFile;

            if (hf && GetFinalPathNameByHandleW(hf, path, MAX_PATH, 0))
                WideCharToMultiByte(CP_UTF8, 0, path, -1, pathA, sizeof(pathA), NULL, NULL);
            printf("  process: base=%p %s\n", ev.u.CreateProcessInfo.lpBaseOfImage, pathA);
            if (pathA[0] && ci_contains(pathA, module_name) && !module_base)
            {
                module_base = (DWORD64)(UINT_PTR)ev.u.CreateProcessInfo.lpBaseOfImage;
                printf("%s main image at %llx (%s)\n", module_name, module_base, pathA);
                for (i = 0; i < n_bp; i++)
                {
                    bps[i].va += module_base;
                    set_bp(proc, &bps[i]);
                }
            }
            if (hf) CloseHandle(hf);
            break;
        }

        case LOAD_DLL_DEBUG_EVENT:
        {
            WCHAR path[MAX_PATH] = {0};
            char pathA[MAX_PATH] = {0};
            HANDLE hf = ev.u.LoadDll.hFile;
            if (hf && GetFinalPathNameByHandleW(hf, path, MAX_PATH, 0))
                WideCharToMultiByte(CP_UTF8, 0, path, -1, pathA, sizeof(pathA), NULL, NULL);
            printf("  loads: base=%p %s\n", ev.u.LoadDll.lpBaseOfDll, pathA);
            if (pathA[0] && ci_contains(pathA, module_name) && !module_base)
            {
                module_base = (DWORD64)(UINT_PTR)ev.u.LoadDll.lpBaseOfDll;
                printf("%s loaded at %llx (%s)\n", module_name, module_base, pathA);
                /* WINWORD.EXE crashed with STATUS_ACCESS_VIOLATION, reproducibly,
                 * arming a breakpoint immediately on this event during early
                 * startup (attach or launch, same result either way) -- give
                 * whatever else touches this module's freshly-mapped .text
                 * (relocations, import fixups) time to finish first. */
                Sleep(500);
                for (i = 0; i < n_bp; i++)
                {
                    bps[i].va += module_base; /* RVA -> VA, once */
                    set_bp(proc, &bps[i]);
                }
            }
            if (hf) CloseHandle(hf);
            break;
        }

        case EXCEPTION_DEBUG_EVENT:
        {
            EXCEPTION_RECORD *er = &ev.u.Exception.ExceptionRecord;
            BOOL is_first = !first_exception_seen;
            first_exception_seen = TRUE;

            /* The loader raises one EXCEPTION_BREAKPOINT itself, right after
             * process init, before any of our own breakpoints can be armed --
             * every debugger is expected to just DBG_CONTINUE past it. Getting
             * this wrong does not look like a breakpoint problem: replying
             * DBG_EXCEPTION_NOT_HANDLED to it here reproducibly crashed
             * WINWORD.EXE with STATUS_ACCESS_VIOLATION later on, at an
             * unrelated address, twice in a row. */
            if (is_first && er->ExceptionCode == EXCEPTION_BREAKPOINT && !find_bp((DWORD64)(UINT_PTR)er->ExceptionAddress))
            {
                printf("initial loader breakpoint at %p, continuing\n", er->ExceptionAddress);
                break; /* cont stays DBG_CONTINUE */
            }

            /* Log every exception this process raises, not just our own
             * breakpoints -- the fatal early-attach access violation needs to
             * be seen to be diagnosed. */
            printf("EXCEPTION code=%#lx addr=%p flags=%#lx firstchance=%lu tid=%lu",
                    er->ExceptionCode, er->ExceptionAddress, er->ExceptionFlags,
                    ev.u.Exception.dwFirstChance, ev.dwThreadId);
            if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2)
            {
                const char *kind = er->ExceptionInformation[0] == 0 ? "read"
                        : er->ExceptionInformation[0] == 1 ? "write" : "execute";
                printf(" %s at %p", kind, (void *)er->ExceptionInformation[1]);
            }
            printf("\n");

            if (er->ExceptionCode == EXCEPTION_BREAKPOINT)
            {
                DWORD64 addr = (DWORD64)(UINT_PTR)er->ExceptionAddress;
                struct bp *b = find_bp(addr);
                if (b)
                {
                    HANDLE th = OpenThread(THREAD_ALL_ACCESS, FALSE, ev.dwThreadId);
                    CONTEXT ctx = {0};
                    BYTE stack[64];
                    SIZE_T n;

                    if (!th)
                    {
                        fprintf(stderr, "OpenThread(%lu) failed, gle=%lu\n", ev.dwThreadId, GetLastError());
                        cont = DBG_EXCEPTION_NOT_HANDLED;
                        break;
                    }
                    ctx.ContextFlags = CONTEXT_ALL;
                    if (!GetThreadContext(th, &ctx))
                    {
                        fprintf(stderr, "GetThreadContext(%lu) failed, gle=%lu\n", ev.dwThreadId, GetLastError());
                        CloseHandle(th);
                        cont = DBG_EXCEPTION_NOT_HANDLED;
                        break;
                    }

                    hits_seen++;
                    if (fields_only) printf("HIT #%d at %llx\n", hits_seen, addr);
                    else printf("HIT #%d at %llx: rax=%llx rbx=%llx rcx=%llx rdx=%llx "
                            "rsi=%llx rdi=%llx rbp=%llx rsp=%llx\n"
                            "  r8=%llx r9=%llx r10=%llx r11=%llx r12=%llx r13=%llx "
                            "r14=%llx r15=%llx\n", hits_seen, addr,
                            (unsigned long long)ctx.Rax, (unsigned long long)ctx.Rbx,
                            (unsigned long long)ctx.Rcx, (unsigned long long)ctx.Rdx,
                            (unsigned long long)ctx.Rsi, (unsigned long long)ctx.Rdi,
                            (unsigned long long)ctx.Rbp, (unsigned long long)ctx.Rsp,
                            (unsigned long long)ctx.R8, (unsigned long long)ctx.R9,
                            (unsigned long long)ctx.R10, (unsigned long long)ctx.R11,
                            (unsigned long long)ctx.R12, (unsigned long long)ctx.R13,
                            (unsigned long long)ctx.R14, (unsigned long long)ctx.R15);

                    /* 仅用于已确认返回 HRESULT 的指令位置，不读取对象或其他寄存器。 */
                    if (hresult_only) printf("  hresult=%#lx\n", (DWORD)ctx.Rax);

                    /* CFG 间接调用点的 RAX 是代码目标；只输出可执行映像的 RVA。
                     * 不读取对象内容，也不输出其余寄存器或非代码指针。 */
                    if (code_target)
                    {
                        MEMORY_BASIC_INFORMATION info;
                        if (VirtualQueryEx(proc, (LPCVOID)(UINT_PTR)ctx.Rax, &info, sizeof(info)) == sizeof(info)
                                && info.Type == MEM_IMAGE && (info.Protect &
                                (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)))
                            printf("  code-target image=%p rva=%llx\n", info.AllocationBase,
                                    (unsigned long long)(ctx.Rax - (DWORD64)(UINT_PTR)info.AllocationBase));
                        else printf("  code-target unavailable (not an executable image)\n");
                    }

                    /* 凭据路径只读取已知的长度字段，不打印整个对象或寄存器内容。 */
                    for (i = 0; i < (int)r14_offset_count; i++)
                    {
                        DWORD64 value;
                        if (ReadProcessMemory(proc, (LPCVOID)(ctx.R14 + r14_offsets[i]),
                                &value, sizeof(value), &n) && n == sizeof(value))
                            printf("  r14-field[%#llx]=%llu\n", r14_offsets[i], value);
                        else
                            fprintf(stderr, "r14-field read failed, gle=%lu\n", GetLastError());
                    }
                    if (!fields_only && ctx.Rcx && ReadProcessMemory(proc, (LPCVOID)ctx.Rcx, stack, sizeof(stack), &n) && n == sizeof(stack))
                    {
                        DWORD64 *qwords = (DWORD64 *)stack;
                        SIZE_T j;

                        for (j = 0; j < sizeof(stack) / sizeof(*qwords); j++)
                            printf("  [rcx+%02llx] = %016llx\n",
                                    (unsigned long long)(j * sizeof(*qwords)),
                                    (unsigned long long)qwords[j]);
                    }
                    if (!fields_only && rbp_dump_bytes && ctx.Rbp)
                    {
                        BYTE dump[256];
                        DWORD64 dump_addr = ctx.Rbp + rbp_dump_offset;
                        SIZE_T j;

                        if (ReadProcessMemory(proc, (LPCVOID)dump_addr, dump, rbp_dump_bytes, &n))
                        {
                            printf("  memory[rbp%+lld] at %llx (%llu bytes):\n",
                                    (long long)rbp_dump_offset, (unsigned long long)dump_addr,
                                    (unsigned long long)n);
                            for (j = 0; j < n; j++)
                            {
                                if (!(j % 16)) printf("    %llx:", (unsigned long long)(dump_addr + j));
                                printf(" %02x", dump[j]);
                                if (j % 16 == 15 || j + 1 == n) putchar('\n');
                            }
                        }
                        else fprintf(stderr, "ReadProcessMemory at rbp%+lld failed, gle=%lu\n",
                                     (long long)rbp_dump_offset, GetLastError());
                    }

                    clear_bp(proc, b); /* one-shot: do not re-arm */
                    ctx.Rip = addr; /* step back over the int3 byte */
                    if (!SetThreadContext(th, &ctx))
                    {
                        fprintf(stderr, "SetThreadContext(%lu) failed, gle=%lu\n", ev.dwThreadId, GetLastError());
                        CloseHandle(th);
                        cont = DBG_EXCEPTION_NOT_HANDLED;
                        break;
                    }
                    CloseHandle(th);

                    if (hits_seen >= hits_wanted)
                    {
                        printf("reached requested hit count, detaching\n");
                        for (i = 0; i < n_bp; i++) clear_bp(proc, &bps[i]);
                        ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, DBG_CONTINUE);
                        DebugActiveProcessStop(ev.dwProcessId);
                        return 0;
                    }
                }
                else cont = DBG_EXCEPTION_NOT_HANDLED;
            }
            else cont = DBG_EXCEPTION_NOT_HANDLED;
            break;
        }

        case EXIT_PROCESS_DEBUG_EVENT:
            printf("target process exited, code %lu\n", ev.u.ExitProcess.dwExitCode);
            return 0;

        default:
            break;
        }

        ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, cont);
    }

    return 1;
}
