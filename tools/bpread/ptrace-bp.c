/* ptrace-bp -- fork+exec a command under raw Linux ptrace (PTRACE_SEIZE),
 * bypassing Wine's own Windows-level debugging entirely, and set a one-shot
 * int3 at a known address once its module is mapped.
 *
 * THE SHORT VERSION, established the hard way across three implementations
 * of this idea: raw ptrace on a Wine process does not work, for a structural
 * reason no amount of getting this tool's own ptrace usage more correct can
 * fix. wine-src/server/ptrace.c's suspend_for_ptrace() has wineserver
 * PTRACE_ATTACH to a thread **on demand**, for GetThreadContext,
 * SetThreadContext, debug registers, and several other routine internal
 * operations (eight call sites in that one file) -- every one of them, for
 * the entire life of every Wine process, not only under any kind of
 * debugging. Its own comment: "this may fail if the client is already being
 * debugged" -- and on that failure, STATUS_ACCESS_DENIED, silently, to
 * whatever internal caller needed it. Only one tracer can hold a thread at
 * once; an external tracer occupies that slot for as long as it stays
 * attached, so *every* one of those operations starts failing the moment it
 * attaches -- independent of signals, group-stops, or anything this file
 * does. Confirmed by ruling out everything else first: PTRACE_TRACEME
 * forwarding real signals, PTRACE_TRACEME suppressing them, and finally a
 * full rewrite onto PTRACE_SEIZE with textbook PTRACE_LISTEN handling (the
 * authoritative fix for the classic API's actual, documented group-stop
 * limitation) all ended in the identical "every thread exits with code 2
 * within seconds" result. Three different, each-more-correct
 * implementations converging on one outcome is what makes this the answer
 * rather than another guess; see docs/office365-under-wine.md for the
 * full account, including why the crash tools/bpread's Win32-API version
 * hits (a real, separate STATUS_ACCESS_VIOLATION during early startup) is
 * NOT the same failure -- that one goes through wineserver's own
 * DebugActiveProcess, so there is no second tracer competing for the slot
 * at all.
 *
 * Kept in the tree working (self-tested, and correctly avoids the
 * TRACEME-era signal problems it was built to test) because the mechanics
 * -- PTRACE_SEIZE, a pipe instead of a self-raised SIGSTOP to synchronise
 * attach-before-exec race-free, PTRACE_O_TRACECLONE for every thread,
 * PTRACE_INTERRUPT to force an arming opportunity on an otherwise-quiet
 * target -- are correct and may be useful again elsewhere. The likely real
 * next step for *this* investigation is narrower than any external tool:
 * patch suspend_for_ptrace() itself to tolerate PTRACE_ATTACH failing with
 * EPERM (another tracer already present) instead of treating it as fatal to
 * the caller.
 *
 * This must be the tracee's *parent* (Yama's default ptrace_scope=1 refuses
 * PTRACE_ATTACH between unrelated processes). PTRACE_SEIZE still needs to
 * attach to an already-*existing* process, so the child blocks in read() on
 * a pipe (genuinely running, not stopped, as far as the kernel's job control
 * is concerned -- no stop-state for PTRACE_SEIZE to have to reconcile) until
 * the parent has seized it and closes the write end, at which point the
 * child's own execve proceeds, arriving as a PTRACE_EVENT_EXEC. (An earlier
 * version had the child raise(SIGSTOP) on itself instead, relying on
 * WUNTRACED + PTRACE_SEIZE-while-stopped; that hung indefinitely -- the
 * tracee never resumed past its own pre-existing stop under the newly
 * established tracer. Not fully explained; the pipe avoids the ambiguity
 * entirely rather than resolving it.)
 *
 * WINWORD.EXE is heavily multi-threaded; PTRACE_O_TRACECLONE makes every new
 * thread arrive as its own PTRACE_EVENT_CLONE stop on the parent thread, so
 * every thread this process ever creates ends up traced and reported here,
 * not only the first one -- necessary because the target address could
 * execute on any of them.
 *
 * The target address is expected to already be known (this project measured
 * Office's module base addresses as apparently deterministic across runs in
 * this environment) -- this tool does not discover it. Whether the target is
 * mapped yet is a plain read of /proc/<pid>/maps, which needs no ptrace
 * state at all, so it is polled continuously; only the moment it first comes
 * up mapped does anything get stopped, specifically to patch in the int3.
 *
 * Usage: ptrace-bp <hex_address> <hit_count> -- <cmd> [args...]
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>

#ifndef PTRACE_EVENT_STOP
#define PTRACE_EVENT_STOP 128
#endif
#ifndef PTRACE_LISTEN
#define PTRACE_LISTEN 0x4208
#endif
#ifndef PTRACE_SEIZE
#define PTRACE_SEIZE 0x4206
#endif
#ifndef PTRACE_O_EXITKILL
#define PTRACE_O_EXITKILL 0x00100000
#endif

static unsigned long g_addr;
static int g_hits_wanted, g_hits_seen;
static unsigned long g_orig_word;
static int g_armed;
static pid_t g_leader;
static int g_clone_events, g_sig_events, g_stop_events, g_live_threads;
static volatile int g_mapped_seen;
static int g_syncpipe[2];

/* Is addr currently mapped in pid's address space? Plain file read, no
 * ptrace state needed, so this can be polled freely without disturbing
 * anything. */
static int addr_is_mapped(pid_t pid, unsigned long addr)
{
    char path[64], line[512];
    FILE *f;
    int found = 0;
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    f = fopen(path, "r");
    if (!f) return 0;
    while (fgets(line, sizeof(line), f))
    {
        unsigned long lo, hi;
        if (sscanf(line, "%lx-%lx", &lo, &hi) == 2 && addr >= lo && addr < hi)
        {
            found = 1;
            break;
        }
    }
    fclose(f);
    return found;
}

/* Called with tid already ptrace-stopped for some reason (any reason).
 * Arms the breakpoint if the target is mapped and it has not been armed
 * yet; otherwise does nothing. */
static void try_arm(pid_t tid)
{
    unsigned long patched;
    if (g_armed) return;
    if (!g_mapped_seen) return;

    errno = 0;
    g_orig_word = ptrace(PTRACE_PEEKTEXT, tid, (void *)g_addr, NULL);
    if (g_orig_word == (unsigned long)-1 && errno)
    {
        fprintf(stderr, "PEEKTEXT still failed after maps said mapped: %s\n", strerror(errno));
        return;
    }
    patched = (g_orig_word & ~0xffUL) | 0xCC;
    if (ptrace(PTRACE_POKETEXT, tid, (void *)g_addr, (void *)patched) != 0)
    {
        perror("POKETEXT");
        return;
    }
    g_armed = 1;
    printf("armed int3 at %#lx (orig word %#lx), patched via tid %d\n", g_addr, g_orig_word, tid);
}

int main(int argc, char **argv)
{
    pid_t pid;
    int status, i;
    char *rest_argv[64];
    int rest_argc = 0;
    int poll_ticks = 0;

    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc < 5 || strcmp(argv[3], "--") != 0)
    {
        fprintf(stderr, "usage: %s <hex_address> <hit_count> -- <cmd> [args...]\n", argv[0]);
        return 1;
    }
    g_addr = strtoul(argv[1], NULL, 16);
    g_hits_wanted = atoi(argv[2]);
    for (i = 4; i < argc && rest_argc < 63; i++) rest_argv[rest_argc++] = argv[i];
    rest_argv[rest_argc] = NULL;

    printf("target address %#lx, %d hit(s) wanted\n", g_addr, g_hits_wanted);

    /* Synchronise with a pipe rather than a self-raised SIGSTOP: a child
     * blocked in read() is genuinely *running* as far as the kernel's job
     * control is concerned, not stopped, so PTRACE_SEIZE attaches to it
     * with no stop-state to reconcile at all -- no group-stop notification
     * to answer, no SIGCONT of this tool's own to race against one the
     * kernel might still be processing from a pre-existing real stop (which
     * is what a self-raise(SIGSTOP) version of this same idea hung on:
     * seized while already stopped, the tracee never resumed past its own
     * first group-stop notification, execve() never reached). */
    if (pipe(g_syncpipe) != 0) { perror("pipe"); return 1; }

    pid = fork();
    if (pid < 0) { perror("fork"); return 1; }
    if (pid == 0)
    {
        char buf;
        close(g_syncpipe[1]);
        if (read(g_syncpipe[0], &buf, 1)) {} /* blocks until the parent closes its end */
        close(g_syncpipe[0]);
        execvp(rest_argv[0], rest_argv);
        perror("execvp");
        _exit(1);
    }
    close(g_syncpipe[0]);
    g_leader = pid;
    g_live_threads = 1;

    if (ptrace(PTRACE_SEIZE, pid, NULL,
            (void *)(long)(PTRACE_O_TRACECLONE | PTRACE_O_TRACEEXEC | PTRACE_O_EXITKILL)) != 0)
    {
        perror("PTRACE_SEIZE");
        kill(pid, SIGKILL);
        return 1;
    }
    printf("seized pid %d while it waited on the sync pipe; releasing it to exec\n", pid);
    close(g_syncpipe[1]); /* child's read() returns 0, unblocking it into execvp */

    for (;;)
    {
        pid_t w = waitpid(-1, &status, WNOHANG | __WALL);

        if (w == 0)
        {
            /* Nothing changed right now. Check for the mapping with a plain
             * file read -- no ptrace state needed. A quiet target (nothing
             * like WINWORD.EXE's own constant flow of clone/signal events to
             * piggyback arming on) would otherwise never get armed at all --
             * PTRACE_INTERRUPT is the PTRACE_SEIZE-native, documented way to
             * force a stop on demand; it arrives as PTRACE_EVENT_STOP with
             * SIGTRAP (not a real stop-class signal), so the handler below
             * tells it apart from a genuine group-stop and just PTRACE_CONTs
             * afterward instead of PTRACE_LISTENing. */
            if (!g_armed)
            {
                g_mapped_seen = addr_is_mapped(g_leader, g_addr);
                if (g_mapped_seen) ptrace(PTRACE_INTERRUPT, g_leader, NULL, NULL);
            }
            if (++poll_ticks % 1000 == 0)
                printf("... still polling (%d) clones=%d stops=%d sigs=%d live_threads=%d mapped=%d\n",
                        poll_ticks, g_clone_events, g_stop_events, g_sig_events, g_live_threads, g_mapped_seen);
            usleep(2000);
            continue;
        }
        if (w == -1)
        {
            if (errno == EINTR) continue;
            if (errno == ECHILD) { printf("no more tracees\n"); return 0; }
            perror("waitpid");
            break;
        }

        if (WIFEXITED(status))
        {
            g_live_threads--;
            printf("tid %d exited, code %d (live=%d)\n", w, WEXITSTATUS(status), g_live_threads);
            if (w == g_leader) return 0;
            continue;
        }
        if (WIFSIGNALED(status))
        {
            g_live_threads--;
            printf("tid %d killed by signal %d (live=%d)\n", w, WTERMSIG(status), g_live_threads);
            if (w == g_leader) return 0;
            continue;
        }
        if (!WIFSTOPPED(status)) continue;

        int sig = WSTOPSIG(status);
        int event = status >> 16;

        if (sig == SIGTRAP && event == PTRACE_EVENT_CLONE)
        {
            unsigned long new_tid = 0;
            g_clone_events++;
            g_live_threads++;
            ptrace(PTRACE_GETEVENTMSG, w, NULL, &new_tid);
            if (g_clone_events <= 20 || g_clone_events % 50 == 0)
                printf("thread %d cloned -> new thread %lu (clone #%d, live=%d)\n",
                        w, new_tid, g_clone_events, g_live_threads);
            try_arm(w);
            ptrace(PTRACE_CONT, w, NULL, NULL);
            continue;
        }

        if (sig == SIGTRAP && event == PTRACE_EVENT_EXEC)
        {
            printf("tid %d exec'd\n", w);
            try_arm(w);
            ptrace(PTRACE_CONT, w, NULL, NULL);
            continue;
        }

        if (event == PTRACE_EVENT_STOP)
        {
            g_stop_events++;
            try_arm(w);
            if (sig == SIGTRAP)
            {
                /* Not a real stop-class signal -- this is this tool's own
                 * PTRACE_INTERRUPT (or the very first seize notification),
                 * a tracer-requested inspection point with nothing on the
                 * tracee's side expecting to still be stopped afterward.
                 * Plain PTRACE_CONT is correct here. */
                if (g_stop_events <= 20 || g_stop_events % 500 == 0)
                    printf("tid %d tracer-requested stop, stop #%d, PTRACE_CONT\n", w, g_stop_events);
                ptrace(PTRACE_CONT, w, NULL, NULL);
            }
            else
            {
                /* A real group-stop: this process sent itself an actual
                 * stop-class signal (SIGSTOP et al) for its own reasons. The
                 * documented correct response under PTRACE_SEIZE is
                 * PTRACE_LISTEN, not PTRACE_CONT: it leaves the tracee
                 * genuinely stopped -- exactly as it would be with no tracer
                 * at all -- while still letting this tool observe it, so
                 * whatever eventually SIGCONTs it (presumably another of
                 * Wine's own threads) wakes it normally. */
                if (g_stop_events <= 20 || g_stop_events % 500 == 0)
                    printf("tid %d group-stop (sig %d), stop #%d, PTRACE_LISTEN\n", w, sig, g_stop_events);
                if (ptrace(PTRACE_LISTEN, w, NULL, NULL) != 0)
                {
                    /* PTRACE_LISTEN can fail (ESRCH) if the tracee resumed
                     * on its own between the event and this call; fall back
                     * to PTRACE_CONT rather than leave it stuck. */
                    ptrace(PTRACE_CONT, w, NULL, NULL);
                }
            }
            continue;
        }

        if (sig == SIGTRAP)
        {
            struct user_regs_struct regs;
            if (ptrace(PTRACE_GETREGS, w, NULL, &regs) == 0 && g_armed && regs.rip - 1 == g_addr)
            {
                g_hits_seen++;
                printf("HIT #%d (tid %d) at %#lx: rax=%#llx rcx=%#llx rdx=%#llx r8=%#llx "
                        "r9=%#llx r10=%#llx r11=%#llx rsp=%#llx\n", g_hits_seen, w, g_addr,
                        (unsigned long long)regs.rax, (unsigned long long)regs.rcx,
                        (unsigned long long)regs.rdx, (unsigned long long)regs.r8,
                        (unsigned long long)regs.r9, (unsigned long long)regs.r10,
                        (unsigned long long)regs.r11, (unsigned long long)regs.rsp);
                if (regs.rcx)
                {
                    errno = 0;
                    unsigned long deref = ptrace(PTRACE_PEEKDATA, w, (void *)regs.rcx, NULL);
                    if (!(deref == (unsigned long)-1 && errno)) printf("  [rcx] = %#lx\n", deref);
                }
                ptrace(PTRACE_POKETEXT, w, (void *)g_addr, (void *)g_orig_word);
                regs.rip = g_addr;
                ptrace(PTRACE_SETREGS, w, NULL, &regs);
                if (g_hits_seen >= g_hits_wanted)
                {
                    printf("reached requested hit count, detaching\n");
                    ptrace(PTRACE_DETACH, w, NULL, NULL);
                    return 0;
                }
                ptrace(PTRACE_SINGLESTEP, w, NULL, NULL);
                waitpid(w, &status, 0);
                ptrace(PTRACE_POKETEXT, w, (void *)g_addr,
                        (void *)((g_orig_word & ~0xffUL) | 0xCC));
                ptrace(PTRACE_CONT, w, NULL, NULL);
                continue;
            }
            /* Some other plain SIGTRAP -- try arming, then continue
             * regardless. */
            try_arm(w);
            ptrace(PTRACE_CONT, w, NULL, NULL);
            continue;
        }

        /* Any other signal (not a stop-class one -- those arrive as
         * PTRACE_EVENT_STOP under SEIZE and are handled above): forward it
         * untouched, so whatever this thread's own signal expectations are
         * stay exactly as they would be with no tracer at all. */
        g_sig_events++;
        if (g_sig_events <= 20 || g_sig_events % 500 == 0)
            printf("forwarding signal %d to tid %d (sig #%d)\n", sig, w, g_sig_events);
        try_arm(w);
        ptrace(PTRACE_CONT, w, NULL, (void *)(long)sig);
    }

    return 1;
}
