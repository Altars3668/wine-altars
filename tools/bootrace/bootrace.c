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
static struct bp *pending_rearm;   /* re-armed on the next single-step event */
static HANDLE hproc;
static DWORD_PTR modbase;
static int hits;
static BOOL show_regs;      /* -regs: integer args and the return address */
static int  stack_words;    /* -stack N: N qwords from rsp, to walk callers */
static BOOL rearm;          /* -rearm: keep the breakpoint after it fires */

/* -wstr <reg>[+-]<hex>: print the UTF-16 string at that address on every hit.
 * A breakpoint in the middle of a function is usually placed to read a buffer
 * the function just filled in, and the buffer is named by a register and an
 * offset, so that is the form this takes. */
#define MAX_WSTR 6
static struct { char reg[8]; long long off; int deref; } wstrs[MAX_WSTR];
static int n_wstr;

static DWORD64 reg_value(const CONTEXT *c, const char *name)
{
    if (!_stricmp(name, "rax")) return c->Rax;
    if (!_stricmp(name, "rbx")) return c->Rbx;
    if (!_stricmp(name, "rcx")) return c->Rcx;
    if (!_stricmp(name, "rdx")) return c->Rdx;
    if (!_stricmp(name, "rsi")) return c->Rsi;
    if (!_stricmp(name, "rdi")) return c->Rdi;
    if (!_stricmp(name, "rbp")) return c->Rbp;
    if (!_stricmp(name, "rsp")) return c->Rsp;
    if (!_stricmp(name, "r8"))  return c->R8;
    if (!_stricmp(name, "r9"))  return c->R9;
    if (!_stricmp(name, "r10")) return c->R10;
    if (!_stricmp(name, "r11")) return c->R11;
    if (!_stricmp(name, "r12")) return c->R12;
    if (!_stricmp(name, "r13")) return c->R13;
    if (!_stricmp(name, "r14")) return c->R14;
    if (!_stricmp(name, "r15")) return c->R15;
    return 0;
}

static void print_wstrs(const CONTEXT *c)
{
    int i;
    for (i = 0; i < n_wstr; i++) {
        WCHAR w[260];
        char a[520];
        SIZE_T got = 0;
        DWORD64 addr = reg_value(c, wstrs[i].reg) + wstrs[i].off;

        if (wstrs[i].deref) {
            /* the slot holds a pointer, not the characters: print the pointer
             * too, because a poison value like -1 is the answer by itself */
            DWORD64 p = 0;
            if (!ReadProcessMemory(hproc, (void*)(DWORD_PTR)addr, &p, 8, &got) || !got) {
                printf("      *%s%+lld @%016llx = <unreadable slot>\n", wstrs[i].reg,
                       wstrs[i].off, (unsigned long long)addr);
                continue;
            }
            printf("      *%s%+lld -> %016llx", wstrs[i].reg, wstrs[i].off,
                   (unsigned long long)p);
            memset(w, 0, sizeof(w));
            if (p && ReadProcessMemory(hproc, (void*)(DWORD_PTR)p, w, sizeof(w)-2, &got) && got) {
                w[(sizeof(w)/2)-1] = 0;
                WideCharToMultiByte(CP_UTF8, 0, w, -1, a, sizeof(a), NULL, NULL);
                printf(" \"%s\"\n", a);
            } else {
                printf(" <unreadable>\n");
            }
            continue;
        }

        memset(w, 0, sizeof(w));
        if (!ReadProcessMemory(hproc, (void*)(DWORD_PTR)addr, w, sizeof(w)-2, &got) || !got) {
            printf("      %s%+lld @%016llx = <unreadable>\n", wstrs[i].reg,
                   wstrs[i].off, (unsigned long long)addr);
            continue;
        }
        w[(sizeof(w)/2)-1] = 0;
        WideCharToMultiByte(CP_UTF8, 0, w, -1, a, sizeof(a), NULL, NULL);
        printf("      %s%+lld @%016llx = \"%s\"\n", wstrs[i].reg, wstrs[i].off,
               (unsigned long long)addr, a);
    }
}

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

    while (argc > 1 && argv[1][0] == '-') {
        if (!strcmp(argv[1], "-regs"))       show_regs = TRUE;
        else if (!strcmp(argv[1], "-rearm")) rearm = TRUE;
        else if (!strncmp(argv[1], "-stack", 6) && argc > 2) {
            stack_words = atoi(argv[2]);
            memmove(&argv[1], &argv[2], (argc-2)*sizeof(char*)); argc--;
        }
        else if ((!strcmp(argv[1], "-wstr") || !strcmp(argv[1], "-wptr")) && argc > 2) {
            if (n_wstr < MAX_WSTR) {
                wstrs[n_wstr].deref = !strcmp(argv[1], "-wptr");
                char *p = argv[2], *sign = strpbrk(p, "+-");
                wstrs[n_wstr].off = 0;
                if (sign) {
                    long long v = strtoll(sign + 1, NULL, 0);
                    wstrs[n_wstr].off = (*sign == '-') ? -v : v;
                    *sign = 0;
                }
                strncpy(wstrs[n_wstr].reg, p, sizeof(wstrs[n_wstr].reg)-1);
                n_wstr++;
            }
            memmove(&argv[1], &argv[2], (argc-2)*sizeof(char*)); argc--;
        }
        memmove(&argv[1], &argv[2], (argc-2)*sizeof(char*)); argc--;
    }
    if (argc < 4) {
        fprintf(stderr, "usage: bootrace [-regs] [-stack N] [-rearm] [-wstr reg+off] "
                        "<exe> <rva-list> <module.dll> [args...]\n");
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
                    CONTEXT full;
                    BOOL ht_ok = FALSE;
                    SIZE_T got = 0;

                    memset(&full, 0, sizeof(full));
                    if (ht) {
                        full.ContextFlags = CONTEXT_FULL;
                        ht_ok = GetThreadContext(ht, &full);
                    }
                    /* restore the byte and step rip back over it */
                    WriteProcessMemory(hproc, (void*)b->addr, &b->orig, 1, &got);
                    FlushInstructionCache(hproc, (void*)b->addr, 1);
                    if (!rearm) b->armed = FALSE;
                    if (ht_ok) {
                        full.Rip = (DWORD64)b->addr;
                        if (rearm) {
                            full.EFlags |= 0x100;   /* TF: stop after this instruction */
                            pending_rearm = b;
                        }
                        SetThreadContext(ht, &full);
                    }
                    if (ht) CloseHandle(ht);
                    printf("%3d %s\n", ++hits, b->name);
                    if ((show_regs || stack_words || n_wstr) && ht_ok) {
                        SIZE_T got = 0;
                        if (show_regs) {
                            DWORD64 ret_addr = 0;
                            /* at a function's first byte, [rsp] is the return address
                             * and rcx/rdx/r8/r9 are the first four integer args */
                            ReadProcessMemory(hproc, (void*)full.Rsp, &ret_addr, 8, &got);
                            printf("      rax=%016llx rcx=%016llx rdx=%016llx r8=%016llx r9=%016llx\n",
                                   (unsigned long long)full.Rax,
                                   (unsigned long long)full.Rcx, (unsigned long long)full.Rdx,
                                   (unsigned long long)full.R8,  (unsigned long long)full.R9);
                            printf("      called from %016llx  (rsp=%016llx)\n",
                                   (unsigned long long)ret_addr, (unsigned long long)full.Rsp);
                        }
                        if (n_wstr) print_wstrs(&full);
                        if (stack_words) {
                            int k;
                            printf("      stack:");
                            for (k = 0; k < stack_words; k++) {
                                DWORD64 v = 0;
                                if (!ReadProcessMemory(hproc, (void*)(full.Rsp + 8*k), &v, 8, &got))
                                    break;
                                printf(" %llx", (unsigned long long)v);
                            }
                            printf("\n");
                        }
                    }
                    fflush(stdout);
                    cont = DBG_CONTINUE;
                } else {
                    /* not ours - the loader's initial breakpoint, or the app's */
                    cont = DBG_CONTINUE;
                }
            }
            else if (r->ExceptionCode == EXCEPTION_SINGLE_STEP && pending_rearm) {
                BYTE cc = 0xCC;
                SIZE_T got = 0;
                WriteProcessMemory(hproc, (void*)pending_rearm->addr, &cc, 1, &got);
                FlushInstructionCache(hproc, (void*)pending_rearm->addr, 1);
                pending_rearm = NULL;
                cont = DBG_CONTINUE;
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
