/*
 * bootrace - report which functions of a module actually run, in order.
 *
 * Word's startup is a chain of Boot::Ifr* methods, each returning an
 * InitFailureReason; the startup dialog prints the number of whichever one
 * fails. Names and RVAs come from the public PDB, but nothing says which ran.
 * This breaks on each entry point and prints them as they are reached, so the
 * last one printed is where startup stopped.
 *
 * A debugger rather than an injected dll: no import to hook, nothing to load
 * into the target, and it works the same whether the process is Word or not.
 *
 *   bootrace <exe> <rva-list> <module.dll> [args...]
 *
 * rva-list is "  <hex rva> <name>" per line, as produced by pdb-rvas.py.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BP 4096

struct bp {
    DWORD_PTR rva;
    char name[256];
    BYTE orig;
    BOOL armed;
    DWORD_PTR addr;
};

static struct bp bps[MAX_BP];
static int n_bp;
static HANDLE hproc;
static DWORD_PTR modbase;
static int hits;

static int load_list(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[512];
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 0; }
    while (n_bp < MAX_BP && fgets(line, sizeof(line), f)) {
        unsigned long long rva;
        char name[256];
        if (sscanf(line, "%llx %255s", &rva, name) != 2) continue;
        bps[n_bp].rva = (DWORD_PTR)rva;
        strncpy(bps[n_bp].name, name, sizeof(bps[n_bp].name)-1);
        n_bp++;
    }
    fclose(f);
    return n_bp;
}

static void arm_all(DWORD_PTR base)
{
    BYTE cc = 0xCC;
    int i, ok = 0;
    modbase = base;
    for (i = 0; i < n_bp; i++) {
        SIZE_T got = 0;
        bps[i].addr = base + bps[i].rva;
        if (!ReadProcessMemory(hproc, (void*)bps[i].addr, &bps[i].orig, 1, &got) || got != 1)
            continue;
        if (!WriteProcessMemory(hproc, (void*)bps[i].addr, &cc, 1, &got) || got != 1)
            continue;
        bps[i].armed = TRUE;
        ok++;
    }
    FlushInstructionCache(hproc, NULL, 0);
    fprintf(stderr, "[bootrace] module at %p, armed %d/%d breakpoints\n",
            (void*)base, ok, n_bp);
    fflush(stderr);
}

static struct bp *find_bp(DWORD_PTR addr)
{
    int i;
    for (i = 0; i < n_bp; i++)
        if (bps[i].armed && bps[i].addr == addr) return &bps[i];
    return NULL;
}

/* the module a LOAD_DLL event refers to, read out of the target */
static BOOL name_matches(HANDLE h, LPVOID name_ptr, BOOL unicode, const char *want)
{
    void *remote = NULL;
    char buf[MAX_PATH];
    WCHAR wbuf[MAX_PATH];
    SIZE_T got = 0;

    if (!name_ptr) return FALSE;
    if (!ReadProcessMemory(h, name_ptr, &remote, sizeof(remote), &got) || !remote)
        return FALSE;
    if (unicode) {
        if (!ReadProcessMemory(h, remote, wbuf, sizeof(wbuf)-2, &got)) return FALSE;
        wbuf[got/2] = 0;
        WideCharToMultiByte(CP_ACP, 0, wbuf, -1, buf, sizeof(buf), NULL, NULL);
    } else {
        if (!ReadProcessMemory(h, remote, buf, sizeof(buf)-1, &got)) return FALSE;
        buf[got] = 0;
    }
    { const char *p = strrchr(buf, '\\');
      if (p) p++; else p = buf;
      return !_stricmp(p, want); }
}

int main(int argc, char **argv)
{
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    DEBUG_EVENT ev;
    char cmd[2048];
    const char *want_mod;
    int i;

    if (argc < 4) {
        fprintf(stderr, "usage: bootrace <exe> <rva-list> <module.dll> [args...]\n");
        return 2;
    }
    if (!load_list(argv[2])) return 2;
    want_mod = argv[3];

    snprintf(cmd, sizeof(cmd), "\"%s\"", argv[1]);
    for (i = 4; i < argc; i++) {
        strncat(cmd, " ", sizeof(cmd)-strlen(cmd)-1);
        strncat(cmd, argv[i], sizeof(cmd)-strlen(cmd)-1);
    }
    fprintf(stderr, "[bootrace] %d symbols, waiting for %s\n", n_bp, want_mod);

    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE,
                        DEBUG_ONLY_THIS_PROCESS, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "CreateProcess failed: %lu\n", GetLastError());
        return 1;
    }
    hproc = pi.hProcess;

    while (WaitForDebugEvent(&ev, INFINITE)) {
        DWORD cont = DBG_EXCEPTION_NOT_HANDLED;

        switch (ev.dwDebugEventCode) {
        case CREATE_PROCESS_DEBUG_EVENT:
            if (ev.u.CreateProcessInfo.hFile) CloseHandle(ev.u.CreateProcessInfo.hFile);
            cont = DBG_CONTINUE;
            break;

        case LOAD_DLL_DEBUG_EVENT:
            if (!modbase && name_matches(hproc, ev.u.LoadDll.lpImageName,
                                         ev.u.LoadDll.fUnicode, want_mod))
                arm_all((DWORD_PTR)ev.u.LoadDll.lpBaseOfDll);
            if (ev.u.LoadDll.hFile) CloseHandle(ev.u.LoadDll.hFile);
            cont = DBG_CONTINUE;
            break;

        case EXCEPTION_DEBUG_EVENT: {
            EXCEPTION_RECORD *r = &ev.u.Exception.ExceptionRecord;
            if (r->ExceptionCode == EXCEPTION_BREAKPOINT) {
                struct bp *b = find_bp((DWORD_PTR)r->ExceptionAddress);
                if (b) {
                    HANDLE ht = OpenThread(THREAD_ALL_ACCESS, FALSE, ev.dwThreadId);
                    CONTEXT ctx = { 0 };
                    SIZE_T got = 0;
                    /* one-shot: restore the byte and step rip back over it */
                    WriteProcessMemory(hproc, (void*)b->addr, &b->orig, 1, &got);
                    FlushInstructionCache(hproc, (void*)b->addr, 1);
                    b->armed = FALSE;
                    if (ht) {
                        ctx.ContextFlags = CONTEXT_CONTROL;
                        if (GetThreadContext(ht, &ctx)) {
                            ctx.Rip = (DWORD64)b->addr;
                            SetThreadContext(ht, &ctx);
                        }
                        CloseHandle(ht);
                    }
                    printf("%3d %s\n", ++hits, b->name);
                    fflush(stdout);
                    cont = DBG_CONTINUE;
                } else {
                    /* not ours - the loader's initial breakpoint, or the app's */
                    cont = DBG_CONTINUE;
                }
            }
            break;
        }

        case EXIT_PROCESS_DEBUG_EVENT:
            fprintf(stderr, "[bootrace] process exited (%lu), %d entry points reached\n",
                    ev.u.ExitProcess.dwExitCode, hits);
            ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, DBG_CONTINUE);
            return 0;

        default:
            cont = DBG_CONTINUE;
            break;
        }
        ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, cont);
    }
    return 0;
}
