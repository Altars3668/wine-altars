/* LD_PRELOAD for debugging: let any process of the user ptrace a Wine process.  Yama's
 * ptrace_scope 1 only lets ancestors attach, and ntdll names the wineserver as the one
 * exception (prctl(PR_SET_PTRACER, server_pid)); that call is turned into "anyone", which
 * still includes the wineserver. */
#define _GNU_SOURCE
#include <stdarg.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#ifndef PR_SET_PTRACER
#define PR_SET_PTRACER 0x59616d61
#endif

int prctl(int option, ...)
{
    unsigned long a2, a3, a4, a5;
    va_list ap;

    va_start(ap, option);
    a2 = va_arg(ap, unsigned long);
    a3 = va_arg(ap, unsigned long);
    a4 = va_arg(ap, unsigned long);
    a5 = va_arg(ap, unsigned long);
    va_end(ap);
    if (option == PR_SET_PTRACER) a2 = (unsigned long)-1;
    return syscall(SYS_prctl, option, a2, a3, a4, a5);
}

__attribute__((constructor)) static void allow_ptrace(void)
{
    syscall(SYS_prctl, PR_SET_PTRACER, (unsigned long)-1, 0UL, 0UL, 0UL);
}
