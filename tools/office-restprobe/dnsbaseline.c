#define WIN32_LEAN_AND_MEAN
#include "probe.h"
#include <winsock2.h>
#include <ws2tcpip.h>

int main(void)
{
    WSADATA data;
    ADDRINFOA hints = {0}, *result;
    int ret;
    unsigned int mode;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (!probe_start()) return 1;
    if (WSAStartup(MAKEWORD(2,2), &data)) return probe_done(1);
    for (mode = 0; mode < 2; ++mode)
    {
        result = NULL;
        ret = getaddrinfo("www.kernel.org", NULL, mode ? &hints : NULL, &result);
        printf("public_dns mode=%u status=%d result_present=%u\n", mode, ret, !!result);
        if (result) freeaddrinfo(result);
    }
    WSACleanup();
    return probe_done(0);
}
