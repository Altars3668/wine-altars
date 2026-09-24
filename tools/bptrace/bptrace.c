/*
 * bptrace - breakpoints that stay armed, for following one object through a
 * client's code.
 *
 *   bptrace <wine_pid> <module> <rva_hex[,rva_hex...]> <max_hits> [off_hex[,off_hex...]]
 *
 * Attaches with the standard debugging API (no hardware breakpoints, which
 * Wine does not honour), puts an int3 at each RVA and re-arms it after every
 * hit by single-stepping over the original byte.  Each hit prints the RVA,
 * the thread, rcx/rdx/r8, the return address at [rsp] as module+offset, and
 * the qwords at [rcx+off] for each offset given -- enough to see which object
 * a method ran on and what it held at that moment.
 *
 * If the module is not loaded yet, it arms when the module loads: the name is
 * read from the export directory of each image the debug events report.
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
static DWORD64 offs[16]; static int noff;
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

    if (argc < 5) { printf("usage: bptrace pid module rvas maxhits [offs]\n"); return 2; }
    setvbuf(stdout, NULL, _IONBF, 0);
    pid = strtoul(argv[1], NULL, 0); mod = argv[2]; maxhits = atoi(argv[4]);
    if (argc > 5) for (tok = strtok(_strdup(argv[5]), ","); tok && noff < 16; tok = strtok(NULL, ",")) offs[noff++] = strtoull(tok, NULL, 16);
    for (int tries = 0; tries < 20 && !base; tries++)
    {
        nmods = 0;
        load_modules(pid);
        for (int i = 0; i < nmods; i++) if (!_stricmp(mods[i].name, mod)) base = mods[i].base;
        if (!base) Sleep(250);
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
                DWORD64 ret = 0, off; ReadProcessMemory(proc, (void *)ctx.Rsp, &ret, 8, &n);
                const char *rm = modname(ret, &off);
                printf("hit %d rva %llx tid %lx rcx %llx rdx %llx r8 %llx ret %s+%llx", ++hits, bps[which] - base,
                       ev.dwThreadId, ctx.Rcx, ctx.Rdx, ctx.R8, rm, off);
                for (int k = 0; k < noff; k++)
                {
                    DWORD64 q = 0; ReadProcessMemory(proc, (void *)(ctx.Rcx + offs[k]), &q, 8, &n);
                    printf(" [rcx+%llx]=%llx", offs[k], q);
                }
                printf("\n");
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
