#include <windows.h>
#include <wmistr.h>
#include <evntrace.h>
#include <stdio.h>

static const GUID provider = {0x7d6b6bd2, 0x1e7c, 0x4e46, {0x8a, 0x1d, 0x6f, 0x3e, 0x57, 0x81, 0x9c, 0x31}};
static const GUID class1 = {0x8d6b6bd2, 0x1e7c, 0x4e46, {0x8a, 0x1d, 0x6f, 0x3e, 0x57, 0x81, 0x9c, 0x31}};
static const GUID class2 = {0x9d6b6bd2, 0x1e7c, 0x4e46, {0x8a, 0x1d, 0x6f, 0x3e, 0x57, 0x81, 0x9c, 0x31}};

static ULONG WINAPI control_callback(WMIDPREQUESTCODE code, void *context, ULONG *size, void *buffer)
{
    return ERROR_SUCCESS;
}

static void check(const char *name, WMIDPREQUEST callback, const GUID *guid, ULONG count,
                  TRACE_GUID_REGISTRATION *classes, const char *path, const char *resource, BOOL output, BOOL wide)
{
    const TRACEHANDLE sentinel = 0x12345678;
    TRACEHANDLE handle = sentinel;
    ULONG ret, error, unreg = ~0u;
    ULONG i;

    for (i = 0; classes && i < count; i++) classes[i].RegHandle = (HANDLE)(ULONG_PTR)sentinel;
    SetLastError(0xdeadbeef);
    if (wide)
        ret = RegisterTraceGuidsW(callback, NULL, guid, count, classes,
                                  path ? L"nonexistent-etw-mof.dll" : NULL, resource ? L"1" : NULL,
                                  output ? &handle : NULL);
    else
        ret = RegisterTraceGuidsA(callback, NULL, guid, count, classes, path, resource,
                                  output ? &handle : NULL);
    error = GetLastError();
    if (!ret && output && handle != sentinel) unreg = UnregisterTraceGuids(handle);
    printf("%s: result %lu handle %s class %s last %lu unregister %lu\n", name, ret,
           !output ? "absent" : handle == sentinel ? "unchanged" : handle ? "set" : "zero",
           !classes || !count ? "absent" : classes[0].RegHandle == (HANDLE)(ULONG_PTR)sentinel ? "unchanged" :
           classes[0].RegHandle ? "set" : "zero", error, unreg);
}

int main(void)
{
    TRACE_GUID_REGISTRATION classes[2] = {{&class1, NULL}, {&class2, NULL}};

    setvbuf(stdout, NULL, _IONBF, 0);
    check("two classes", control_callback, &provider, 2, classes, NULL, NULL, TRUE, FALSE);
    check("zero classes", control_callback, &provider, 0, NULL, NULL, NULL, TRUE, FALSE);
    check("no callback", NULL, &provider, 2, classes, NULL, NULL, TRUE, FALSE);
    check("no provider", control_callback, NULL, 2, classes, NULL, NULL, TRUE, FALSE);
    check("no output", control_callback, &provider, 2, classes, NULL, NULL, FALSE, FALSE);
    check("MOF file", control_callback, &provider, 0, NULL, "nonexistent-etw-mof.dll", NULL, TRUE, FALSE);
    check("MOF resource", control_callback, &provider, 0, NULL, "nonexistent-etw-mof.dll", "1", TRUE, FALSE);
    check("W two classes", control_callback, &provider, 2, classes, NULL, NULL, TRUE, TRUE);
    check("W no callback", NULL, &provider, 2, classes, NULL, NULL, TRUE, TRUE);
    check("W no provider", control_callback, NULL, 2, classes, NULL, NULL, TRUE, TRUE);
    check("W no output", control_callback, &provider, 2, classes, NULL, NULL, FALSE, TRUE);
    return 0;
}
