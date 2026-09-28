/*
 * bptrace - breakpoints that stay armed, for following one object through a
 * client's code.
 *
 *   bptrace <wine_pid> <module> <rva_hex[,rva_hex...]> <max_hits> [read[,read...]]
 *
 * Attaches with the standard debugging API (no hardware breakpoints, which
 * Wine does not honour), puts an int3 at each RVA and re-arms it after every
 * hit by single-stepping over the original byte.  Each hit prints the RVA,
 * the thread, rcx/rdx/r8, the return address at [rsp] as module+offset, and
 * the qword each read names -- enough to see which object a method ran on and
 * what it held at that moment.  A read is <hex>, the qword at [rcx+hex], or
 * <reg>+<hex>, at [reg+hex] (rax to r15, rsp too), followed by any number of
 * @<hex>, each taking the qword read so far as a pointer and reading the one
 * at that offset from it: rcx+18@158 is a field of the object whose pointer
 * the object in rcx holds at 0x18.
 *
 * If the module is not loaded yet, it arms when the module loads: the name is
 * read from the export directory of each image the debug events report.
 *
 * BPTRACE_ARG7=<hex> reports (and counts) only the hits whose seventh argument,
 * at [rsp+0x38] on entry, is that value -- SetWindowPos's flags, say; and
 * BPTRACE_R9=<module>+<hex> only those whose fourth argument, in r9, is that
 * address -- SetTimer's timer procedure; BPTRACE_RDX=<hex> only those whose
 * second, in rdx, is that value -- a timer's id.  Each hit carries the tick
 * count it happened at.
 * BPTRACE_STACK=<n> also prints, of the first n qwords of the stack, those that
 * point into a loaded module: a heuristic call chain, return addresses and some
 * stale values among them, which a PDB turns into function names.
 * Everything is restored before detaching at max_hits; killing it while armed
 * leaves int3s behind in the target, which then dies on the next hit.
 *
 * Found which call left Word holding a dead Csi stream: ole32's
 * StorageImpl_Construct asked it for Stat, the storage's Flush then committed
 * and invalidated it, and Word's FcMacFn asked it for Stat again.
 *
 *   x86_64-w64-mingw32-gcc -O2 -Wall -o bptrace.exe bptrace.c
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXBP 16
static DWORD64 bps[MAXBP]; static BYTE orig[MAXBP]; static int nbp;
static struct read { int reg; DWORD64 off; int nderef; DWORD64 deref[4]; char spec[48]; } reads[16];
static int nreads;
static const char *const regs[] = { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
                                    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15" };

static void parse_read(const char *spec, struct read *r)
{
    const char *p = spec, *at;
    int i;

    lstrcpynA(r->spec, spec, sizeof(r->spec));
    r->reg = 1;  /* rcx */
    for (i = 0; i < 16; i++)
    {
        size_t len = strlen(regs[i]);
        if (!_strnicmp(p, regs[i], len) && (p[len] == '+' || p[len] == '@' || !p[len]))
        {
            r->reg = i;
            p += len;
            if (*p == '+') p++;
            break;
        }
    }
    r->off = strtoull(p, NULL, 16);
    r->nderef = 0;
    for (at = strchr(p, '@'); at && r->nderef < 4; at = strchr(at + 1, '@'))
        r->deref[r->nderef++] = strtoull(at + 1, NULL, 16);
}

static DWORD64 reg_value(const CONTEXT *ctx, int reg)
{
    const DWORD64 values[16] = { ctx->Rax, ctx->Rcx, ctx->Rdx, ctx->Rbx, ctx->Rsp, ctx->Rbp, ctx->Rsi, ctx->Rdi,
                                 ctx->R8, ctx->R9, ctx->R10, ctx->R11, ctx->R12, ctx->R13, ctx->R14, ctx->R15 };
    return values[reg];
}
static struct { DWORD64 base, size; char name[64]; } mods[512]; static int nmods;

static void load_modules(DWORD pid)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    MODULEENTRY32 me; me.dwSize = sizeof(me);
    for (BOOL ok = Module32First(snap, &me); ok && nmods < 512; ok = Module32Next(snap, &me))
    {
        mods[nmods].base = (DWORD64)me.modBaseAddr; mods[nmods].size = me.modBaseSize;
        lstrcpynA(mods[nmods].name, me.szModule, 64); nmods++;
    }
    CloseHandle(snap);
}

static const char *modname(DWORD64 a, DWORD64 *off)
{
    for (int i = 0; i < nmods; i++)
        if (a >= mods[i].base && a < mods[i].base + mods[i].size) { *off = a - mods[i].base; return mods[i].name; }
    *off = a; return "?";
}

static void arm(DWORD pid, DWORD64 b)
{
    HANDLE p = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    SIZE_T n; BYTE cc = 0xcc;
    for (int i = 0; i < nbp; i++)
    {
        bps[i] += b;
        ReadProcessMemory(p, (void *)bps[i], &orig[i], 1, &n);
        WriteProcessMemory(p, (void *)bps[i], &cc, 1, &n); FlushInstructionCache(p, (void *)bps[i], 1);
    }
    CloseHandle(p);
    printf("armed %d breakpoints at base %llx\n", nbp, b);
}

/* The name in a loaded image's export directory, read out of the target's memory. */
static BOOL export_name_is(HANDLE p, DWORD64 b, const char *want)
{
    IMAGE_DOS_HEADER dos; IMAGE_NT_HEADERS64 nt; IMAGE_EXPORT_DIRECTORY ed; char name[64] = {0}; SIZE_T n;
    DWORD rva;
    if (!ReadProcessMemory(p, (void *)b, &dos, sizeof(dos), &n) || dos.e_magic != IMAGE_DOS_SIGNATURE) return FALSE;
    if (!ReadProcessMemory(p, (void *)(b + dos.e_lfanew), &nt, sizeof(nt), &n)) return FALSE;
    rva = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    if (!rva || !ReadProcessMemory(p, (void *)(b + rva), &ed, sizeof(ed), &n)) return FALSE;
    if (!ReadProcessMemory(p, (void *)(b + ed.Name), name, sizeof(name) - 1, &n)) return FALSE;
    return !_stricmp(name, want);
}

int main(int argc, char **argv)
{
    DWORD pid; const char *mod; char *list, *tok; int maxhits, hits = 0;
    DWORD64 base = 0; DEBUG_EVENT ev; SIZE_T n; BYTE cc = 0xcc;
    DWORD stepping_tid = 0; int stepping_bp = -1; BOOL first = TRUE;
    const char *arg7_env = getenv("BPTRACE_ARG7"), *stack_env = getenv("BPTRACE_STACK");
    const char *r9_env = getenv("BPTRACE_R9"), *rdx_env = getenv("BPTRACE_RDX");
    DWORD64 arg7_want = arg7_env ? strtoull(arg7_env, NULL, 16) : 0, r9_want = 0;
    DWORD64 rdx_want = rdx_env ? strtoull(rdx_env, NULL, 16) : 0;
    int stack_qwords = stack_env ? atoi(stack_env) : 0;

    if (argc < 5) { printf("usage: bptrace pid module rvas maxhits [offs]\n"); return 2; }
    setvbuf(stdout, NULL, _IONBF, 0);
    pid = strtoul(argv[1], NULL, 0); mod = argv[2]; maxhits = atoi(argv[4]);
    if (argc > 5) for (tok = strtok(_strdup(argv[5]), ","); tok && nreads < 16; tok = strtok(NULL, ","))
        parse_read(tok, &reads[nreads++]);
    for (int tries = 0; tries < 20 && !base; tries++)
    {
        nmods = 0;
        load_modules(pid);
        for (int i = 0; i < nmods; i++) if (!_stricmp(mods[i].name, mod)) base = mods[i].base;
        if (!base) Sleep(250);
    }
    if (r9_env)
    {
        char r9_mod[64]; const char *plus = strchr(r9_env, '+');
        lstrcpynA(r9_mod, r9_env, plus ? min(64, plus - r9_env + 1) : 64);
        for (int i = 0; i < nmods; i++) if (!_stricmp(mods[i].name, r9_mod)) r9_want = mods[i].base;
        if (!r9_want) { printf("%s is not loaded\n", r9_mod); return 1; }
        r9_want += plus ? strtoull(plus + 1, NULL, 16) : 0;
    }
    list = _strdup(argv[3]);
    for (tok = strtok(list, ","); tok && nbp < MAXBP; tok = strtok(NULL, ",")) bps[nbp++] = strtoull(tok, NULL, 16);
    if (!DebugActiveProcess(pid)) { printf("DebugActiveProcess %lu\n", GetLastError()); return 1; }
    DebugSetProcessKillOnExit(FALSE);
    if (!base) printf("%s not loaded yet, waiting for it\n", mod);
    else arm(pid, base);
    while (hits < maxhits && WaitForDebugEvent(&ev, 900000))
    {
        DWORD cont = DBG_CONTINUE;
        HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        if (ev.dwDebugEventCode == EXCEPTION_DEBUG_EVENT)
        {
            EXCEPTION_RECORD *er = &ev.u.Exception.ExceptionRecord;
            int which = -1;
            for (int i = 0; i < nbp; i++) if ((DWORD64)er->ExceptionAddress == bps[i]) which = i;
            if (er->ExceptionCode == EXCEPTION_BREAKPOINT && which >= 0)
            {
                HANDLE th = OpenThread(THREAD_ALL_ACCESS, FALSE, ev.dwThreadId);
                CONTEXT ctx; ctx.ContextFlags = CONTEXT_FULL; GetThreadContext(th, &ctx);
                DWORD64 ret = 0, off, arg7 = 0; ReadProcessMemory(proc, (void *)ctx.Rsp, &ret, 8, &n);
                ReadProcessMemory(proc, (void *)(ctx.Rsp + 0x38), &arg7, 8, &n);
                if ((!arg7_env || (DWORD)arg7 == arg7_want) && (!r9_env || ctx.R9 == r9_want) &&
                    (!rdx_env || ctx.Rdx == rdx_want))
                {
                    const char *rm = modname(ret, &off);
                    printf("hit %d tick %lu rva %llx tid %lx rcx %llx rdx %llx r8 %llx r9 %llx arg7 %llx ret %s+%llx",
                           ++hits, GetTickCount(), bps[which] - base, ev.dwThreadId, ctx.Rcx, ctx.Rdx, ctx.R8, ctx.R9,
                           arg7 & 0xffffffff, rm, off);
                    for (int k = 0; k < nreads; k++)
                    {
                        DWORD64 q = 0;
                        BOOL ok = ReadProcessMemory(proc, (void *)(reg_value(&ctx, reads[k].reg) + reads[k].off), &q, 8, &n);
                        for (int d = 0; ok && d < reads[k].nderef; d++)
                            ok = ReadProcessMemory(proc, (void *)(q + reads[k].deref[d]), &q, 8, &n);
                        if (ok) printf(" [%s]=%llx", reads[k].spec, q);
                        else printf(" [%s]=?", reads[k].spec);
                    }
                    printf("\n");
                    for (int k = 1; k < stack_qwords; k++)
                    {
                        DWORD64 q = 0; const char *qm;
                        if (!ReadProcessMemory(proc, (void *)(ctx.Rsp + 8 * k), &q, 8, &n)) break;
                        if ((qm = modname(q, &off))[0] != '?') printf("    [rsp+%x] %s+%llx\n", 8 * k, qm, off);
                    }
                }
                WriteProcessMemory(proc, (void *)bps[which], &orig[which], 1, &n); FlushInstructionCache(proc, (void *)bps[which], 1);
                ctx.Rip = bps[which]; ctx.EFlags |= 0x100; stepping_tid = ev.dwThreadId; stepping_bp = which;
                SetThreadContext(th, &ctx); CloseHandle(th);
            }
            else if (er->ExceptionCode == EXCEPTION_SINGLE_STEP && ev.dwThreadId == stepping_tid && stepping_bp >= 0)
            {
                WriteProcessMemory(proc, (void *)bps[stepping_bp], &cc, 1, &n); FlushInstructionCache(proc, (void *)bps[stepping_bp], 1);
                stepping_tid = 0; stepping_bp = -1;
            }
            else if (er->ExceptionCode == EXCEPTION_BREAKPOINT && first) { }
            else cont = DBG_EXCEPTION_NOT_HANDLED;
            if (er->ExceptionCode == EXCEPTION_BREAKPOINT) first = FALSE;
        }
        else if (ev.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT && ev.u.CreateProcessInfo.hFile) CloseHandle(ev.u.CreateProcessInfo.hFile);
        else if (ev.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT)
        {
            if (!base && export_name_is(proc, (DWORD64)ev.u.LoadDll.lpBaseOfDll, mod))
            {
                base = (DWORD64)ev.u.LoadDll.lpBaseOfDll;
                arm(pid, base);
                load_modules(pid);
            }
            if (ev.u.LoadDll.hFile) CloseHandle(ev.u.LoadDll.hFile);
        }
        else if (ev.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) { CloseHandle(proc); printf("process exited\n"); return 0; }
        if (proc) CloseHandle(proc);
        ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, cont);
    }
    /* restore everything before detaching */
    if (base)
    {
        HANDLE p = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        for (int i = 0; i < nbp; i++) { WriteProcessMemory(p, (void *)bps[i], &orig[i], 1, &n); FlushInstructionCache(p, (void *)bps[i], 1); }
        CloseHandle(p);
    }
    DebugActiveProcessStop(pid);
    printf("done, %d hits\n", hits);
    return 0;
}
