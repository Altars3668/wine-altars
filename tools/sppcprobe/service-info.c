/* 仅对照服务信息契约，不消费权利、不加载列表中的插件，也不查询身份或许可材料。
 * --selftest 不加载 SLC/SPPC；其他模式可能启动原生许可服务或产生缓存/事件。 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../../wine-src/include/slpublic.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define SENTINEL 0xfeedbeefu
#define MAX_PLUGIN_BYTES 65536u
#define MAX_PLUGINS 128u

typedef HRESULT (WINAPI *service_info_fn)(HSLC, LPCWSTR, SLDATATYPE *, UINT *, BYTE **);

/* 先完整验证字节边界与双 NUL，再允许读取任何名称；末尾只容许零填充。 */
static BOOL plugin_list_shape(const WCHAR *data, UINT bytes, UINT *count)
{
    UINT offset = 0, end, total = 0, units;

    if (!data || bytes < 2 * sizeof(WCHAR) || bytes > MAX_PLUGIN_BYTES || bytes % sizeof(WCHAR))
        return FALSE;
    units = bytes / sizeof(WCHAR);
    while (offset < units && data[offset])
    {
        for (end = offset; end < units && data[end]; end++);
        if (end + 1 >= units || ++total > MAX_PLUGINS) return FALSE;
        offset = end + 1;
    }
    for (; offset < units; offset++) if (data[offset]) return FALSE;
    *count = total;
    return TRUE;
}

/* 路径只用于提取文件名，绝不据此访问磁盘、网络共享或加载 DLL。
 * 只输出 ASCII 可见字符，避免控制字符或用户目录进入诊断日志。 */
static void print_plugin_names(const WCHAR *data, UINT count)
{
    UINT index;
    const WCHAR *end, *base, *cursor;

    for (index = 0; index < count; index++)
    {
        base = data;
        for (end = data; *end; end++) if (*end == L'\\' || *end == L'/') base = end + 1;
        printf("plugin[%u].basename=", index);
        for (cursor = base; cursor < end; cursor++)
            putchar(*cursor >= 32 && *cursor < 127 ? (char)*cursor : '?');
        putchar('\n');
        data = end + 1;
    }
}

static int selftest(void)
{
    static const WCHAR empty[] = {0, 0};
    static const WCHAR one[] = L"C:\\Windows\\System32\\one.dll\0";
    static const WCHAR two[] = L"C:\\Windows\\System32\\one.dll\0C:\\Program Files\\Office\\two.dll\0";
    static const WCHAR padded[] = L"C:\\Windows\\one.dll\0\0\0";
    static const WCHAR single_nul[] = L"C:\\Windows\\one.dll";
    static const WCHAR no_nul[] = {L'a', L'b'};
    static const WCHAR trailing[] = {L'a', 0, 0, L'b', 0, 0};
    static const WCHAR leading[] = {0, L'a', 0, 0};
    WCHAR many[2 * (MAX_PLUGINS + 1) + 1];
    struct fixture {const WCHAR *data; UINT bytes; BOOL valid; UINT count;} fixtures[] =
    {
        {empty, sizeof(empty), TRUE, 0},
        {one, sizeof(one), TRUE, 1},
        {two, sizeof(two), TRUE, 2},
        {padded, sizeof(padded), TRUE, 1},
        {single_nul, sizeof(single_nul), FALSE, 0},
        {no_nul, sizeof(no_nul), FALSE, 0},
        {trailing, sizeof(trailing), FALSE, 0},
        {leading, sizeof(leading), FALSE, 0},
        {one, sizeof(one) - 1, FALSE, 0},
        {NULL, sizeof(empty), FALSE, 0},
        {empty, 0, FALSE, 0},
        {empty, sizeof(WCHAR), FALSE, 0},
        {empty, MAX_PLUGIN_BYTES + sizeof(WCHAR), FALSE, 0},
        {many, sizeof(many), FALSE, 0},
    };
    UINT index, count, failures = 0;
    BOOL actual;

    for (index = 0; index < ARRAY_SIZE(many) - 1; index++) many[index] = index % 2 ? 0 : L'a';
    many[ARRAY_SIZE(many) - 1] = 0;
    for (index = 0; index < ARRAY_SIZE(fixtures); index++)
    {
        count = SENTINEL;
        actual = plugin_list_shape(fixtures[index].data, fixtures[index].bytes, &count);
        if (actual != fixtures[index].valid || (actual && count != fixtures[index].count) ||
                (!actual && count != SENTINEL))
        {
            printf("selftest-failure=%u\n", index);
            failures++;
        }
    }
    printf("selftests=%u failures=%u\n", (UINT)ARRAY_SIZE(fixtures), failures);
    return failures ? 1 : 0;
}

static void print_module_version(const WCHAR *path)
{
    DWORD bytes = GetFileVersionInfoSizeW(path, NULL);
    BYTE *data = bytes ? malloc(bytes) : NULL;
    VS_FIXEDFILEINFO *info;
    UINT size;

    if (data && GetFileVersionInfoW(path, 0, bytes, data) &&
            VerQueryValueW(data, L"\\", (void **)&info, &size) && size >= sizeof(*info))
        printf("module-version=%u.%u.%u.%u\n", HIWORD(info->dwFileVersionMS),
                LOWORD(info->dwFileVersionMS), HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
    else printf("module-version=unavailable\n");
    free(data);
}

int wmain(int argc, WCHAR **argv)
{
    static const WCHAR *cases[] = {L"active", L"active-no-type", L"missing", L"null-handle",
        L"invalid-handle", L"closed", L"null-name", L"null-size", L"null-data"};
    HRESULT (WINAPI *open_context)(HSLC *);
    HRESULT (WINAPI *close_context)(HSLC);
    service_info_fn query;
    WCHAR path[MAX_PATH], resolved[MAX_PATH];
    HMODULE module, target;
    HSLC handle = NULL, query_handle;
    SLDATATYPE type = (SLDATATYPE)SENTINEL;
    BYTE *data = (BYTE *)(UINT_PTR)0x1234;
    const WCHAR *mode, *name, *base;
    UINT index, bytes = SENTINEL, count, failures = 0, length;
    BOOL omit_type, active;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (argc == 2 && !wcscmp(argv[1], L"--selftest")) return selftest();
    if (argc != 4 || (wcscmp(argv[1], L"sppc") && wcscmp(argv[1], L"slc")) ||
            wcscmp(argv[3], L"--query"))
    {
        printf("usage: service-info --selftest\n"
                "       service-info sppc|slc <case> --query\n"
                "cases: active active-no-type missing null-handle invalid-handle closed\n"
                "       null-name null-size null-data\n");
        return 1;
    }
    mode = argv[2];
    for (index = 0; index < ARRAY_SIZE(cases); index++) if (!wcscmp(mode, cases[index])) break;
    if (index == ARRAY_SIZE(cases)) {printf("unknown-case\n"); return 1;}
    length = GetSystemDirectoryW(path, ARRAY_SIZE(path));
    if (!length || length + wcslen(argv[1]) + 6 >= ARRAY_SIZE(path)) return 1;
    wcscat(path, L"\\"); wcscat(path, argv[1]); wcscat(path, L".dll");
    module = LoadLibraryW(path);
    if (!module) {printf("load-error=%lu\n", GetLastError()); return 1;}
    open_context = (void *)GetProcAddress(module, "SLOpen");
    close_context = (void *)GetProcAddress(module, "SLClose");
    query = (void *)GetProcAddress(module, "SLGetServiceInformation");
    if (!open_context || !close_context || !query) {FreeLibrary(module); return 1;}
    printf("requested-module=%ls.dll pointer-bytes=%u case=%ls\n", argv[1], (UINT)sizeof(void *), mode);
    print_module_version(path);
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCWSTR)(void *)query, &target) && GetModuleFileNameW(target, resolved, ARRAY_SIZE(resolved)))
    {
        base = wcsrchr(resolved, L'\\');
        printf("resolved-query-module=%ls\n", base ? base + 1 : resolved);
    }
    hr = open_context(&handle);
    printf("open hr=%#lx handle-null=%u\n", hr, handle == NULL);
    if (FAILED(hr) || !handle) {FreeLibrary(module); return 1;}
    query_handle = handle;
    if (!wcscmp(mode, L"null-handle")) query_handle = NULL;
    if (!wcscmp(mode, L"invalid-handle")) query_handle = (HSLC)(UINT_PTR)0x1234;
    if (!wcscmp(mode, L"closed"))
    {
        hr = close_context(handle);
        printf("close-before-query hr=%#lx\n", hr);
        if (FAILED(hr)) {FreeLibrary(module); return 1;}
        handle = NULL;
    }
    name = !wcscmp(mode, L"missing") ? L"WineAltarsServiceInfoMissingProperty" : L"ActivePlugins";
    if (!wcscmp(mode, L"null-name")) name = NULL;
    omit_type = !wcscmp(mode, L"active-no-type");
    active = !wcscmp(mode, L"active") || omit_type;
    hr = query(query_handle, name, omit_type ? NULL : &type,
            !wcscmp(mode, L"null-size") ? NULL : &bytes, !wcscmp(mode, L"null-data") ? NULL : &data);
    printf("query hr=%#lx type=", hr);
    if ((UINT)type == SENTINEL) printf("unchanged"); else printf("%u", (UINT)type);
    printf(" bytes=");
    if (bytes == SENTINEL) printf("unchanged"); else printf("%u", bytes);
    printf(" data-unchanged=%u data-null=%u\n", data == (BYTE *)(UINT_PTR)0x1234, data == NULL);
    if (SUCCEEDED(hr) && active)
    {
        if (data == (BYTE *)(UINT_PTR)0x1234 || (!omit_type && type != SL_DATA_MULTI_SZ) ||
                !plugin_list_shape((const WCHAR *)data, bytes, &count))
        {
            printf("malformed-active-plugins=1\n");
            failures++;
        }
        else
        {
            printf("plugin-count=%u\n", count);
            print_plugin_names((const WCHAR *)data, count);
        }
    }
    if (SUCCEEDED(hr) && data && data != (BYTE *)(UINT_PTR)0x1234 && LocalFree(data)) failures++;
    if (handle)
    {
        hr = close_context(handle);
        printf("close hr=%#lx\n", hr);
        if (FAILED(hr)) failures++;
    }
    FreeLibrary(module);
    printf("probe-failures=%u\n", failures);
    return failures ? 1 : 0;
}
