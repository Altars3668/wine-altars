/*
 * bpdump - copy out the buffer a function is handed, each time it is called.
 *
 *   bpdump <wine_pid> <module> <rva_hex> <hits> <outfile>
 *
 * Breaks at module+rva (re-armed by single-stepping) and appends the buffer at
 * rdx, r8 bytes long (capped at 1 MiB), to outfile, each preceded by a
 * one-line header.  At winhttp!WinHttpWriteData that is exactly a request
 * body as the client streams it; this is how Office's FSSHTTP envelope with
 * its doubled xmlns:s declaration was seen.
 *
 * The module has to be loaded already (it is found through a Toolhelp
 * snapshot).  Self-test it against a program that calls the function with
 * known data before trusting what it captures.
 *
 *   x86_64-w64-mingw32-gcc -O2 -Wall -o bpdump.exe bpdump.c
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tlhelp32.h>

int main(int argc, char **argv)
{
    DWORD pid, rva; int hits_wanted, hits = 0; const char *mod, *out;
    DWORD64 base = 0, bp = 0; BYTE orig = 0, cc = 0xcc; BOOL armed = FALSE, first = TRUE;
    DEBUG_EVENT ev; FILE *f; SIZE_T n;
    DWORD stepping_tid = 0;

    if (argc < 6) { fprintf(stderr, "usage: bpdump pid module rva hits outfile\n"); return 2; }
    pid = strtoul(argv[1], NULL, 0); mod = argv[2]; rva = strtoul(argv[3], NULL, 16);
    hits_wanted = atoi(argv[4]); out = argv[5];
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!(f = fopen(out, "wb"))) { perror("fopen"); return 1; }
    if (!DebugActiveProcess(pid)) { fprintf(stderr, "DebugActiveProcess %lu\n", GetLastError()); return 1; }
    DebugSetProcessKillOnExit(FALSE);

    {
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
        MODULEENTRY32 me; me.dwSize = sizeof(me);
        for (BOOL ok = Module32First(snap, &me); ok; ok = Module32Next(snap, &me))
            if (!_stricmp(me.szModule, mod)) { base = (DWORD64)me.modBaseAddr; break; }
        CloseHandle(snap);
        if (!base) { fprintf(stderr, "module %s not loaded\n", mod); DebugActiveProcessStop(pid); return 1; }
    }

    while (WaitForDebugEvent(&ev, 600000))
    {
        if (base && !bp)
        {
            HANDLE p2 = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
            bp = base + rva;
            ReadProcessMemory(p2, (void *)bp, &orig, 1, &n);
            WriteProcessMemory(p2, (void *)bp, &cc, 1, &n); FlushInstructionCache(p2, (void *)bp, 1);
            armed = TRUE;
            printf("armed %s base %llx bp %llx orig %02x\n", mod, base, bp, orig);
            CloseHandle(p2);
        }
        DWORD cont = DBG_CONTINUE;
        HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        if (ev.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT && 0)
        {
            WCHAR name[MAX_PATH]; DWORD64 p = 0; char an[MAX_PATH];
            if (ev.u.LoadDll.lpImageName && ReadProcessMemory(proc, ev.u.LoadDll.lpImageName, &p, sizeof(p), &n) && p &&
                ReadProcessMemory(proc, (void *)p, name, sizeof(name) - 2, &n))
            {
                name[MAX_PATH - 1] = 0;
                WideCharToMultiByte(CP_ACP, 0, name, -1, an, sizeof(an), NULL, NULL);
                const char *b = strrchr(an, '\\'); b = b ? b + 1 : an;
                if (!_stricmp(b, mod))
                {
                    base = (DWORD64)ev.u.LoadDll.lpBaseOfDll; bp = base + rva;
                    ReadProcessMemory(proc, (void *)bp, &orig, 1, &n);
                    WriteProcessMemory(proc, (void *)bp, &cc, 1, &n); FlushInstructionCache(proc, (void *)bp, 1);
                    armed = TRUE;
                    printf("armed %s base %llx bp %llx orig %02x\n", mod, base, bp, orig);
                }
            }
            if (ev.u.LoadDll.hFile) CloseHandle(ev.u.LoadDll.hFile);
        }
        else if (ev.dwDebugEventCode == EXCEPTION_DEBUG_EVENT)
        {
            EXCEPTION_RECORD *er = &ev.u.Exception.ExceptionRecord;
            if (er->ExceptionCode == EXCEPTION_BREAKPOINT && (DWORD64)er->ExceptionAddress == bp && armed)
            {
                HANDLE th = OpenThread(THREAD_ALL_ACCESS, FALSE, ev.dwThreadId);
                CONTEXT ctx; ctx.ContextFlags = CONTEXT_FULL;
                GetThreadContext(th, &ctx);
                SIZE_T len = (SIZE_T)(ctx.R8 & 0xffffffff); if (len > (1 << 20)) len = 1 << 20;
                BYTE *buf = malloc(len ? len : 1);
                SIZE_T got = 0;
                ReadProcessMemory(proc, (void *)ctx.Rdx, buf, len, &got);
                fprintf(f, "\n==== hit %d tid %lu handle %llx buffer %llx len %llu read %llu ====\n", hits + 1,
                        ev.dwThreadId, ctx.Rcx, ctx.Rdx, (unsigned long long)len, (unsigned long long)got);
                fwrite(buf, 1, got, f); fflush(f); free(buf);
                printf("hit %d: handle %llx len %llu\n", hits + 1, ctx.Rcx, (unsigned long long)len);
                hits++;
                WriteProcessMemory(proc, (void *)bp, &orig, 1, &n); FlushInstructionCache(proc, (void *)bp, 1);
                armed = FALSE;
                ctx.Rip = bp;
                if (hits < hits_wanted) { ctx.EFlags |= 0x100; stepping_tid = ev.dwThreadId; }
                SetThreadContext(th, &ctx); CloseHandle(th);
            }
            else if (er->ExceptionCode == EXCEPTION_SINGLE_STEP && ev.dwThreadId == stepping_tid)
            {
                WriteProcessMemory(proc, (void *)bp, &cc, 1, &n); FlushInstructionCache(proc, (void *)bp, 1);
                armed = TRUE; stepping_tid = 0;
            }
            else if (er->ExceptionCode == EXCEPTION_BREAKPOINT && first) { /* attach breakpoint */ }
            else cont = DBG_EXCEPTION_NOT_HANDLED;
            if (er->ExceptionCode == EXCEPTION_BREAKPOINT) first = FALSE;
        }
        else if (ev.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT && ev.u.CreateProcessInfo.hFile)
            CloseHandle(ev.u.CreateProcessInfo.hFile);
        else if (ev.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT) { CloseHandle(proc); break; }
        if (proc) CloseHandle(proc);
        ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, cont);
        if (hits >= hits_wanted && !armed && !stepping_tid) break;
    }
    fclose(f);
    DebugActiveProcessStop(pid);
    printf("done, %d hits\n", hits);
    return 0;
}
