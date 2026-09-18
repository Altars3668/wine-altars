/* SPPC/SLC 四 API 的时序对照。不安装、激活、重置或卸载许可，不输出秘密。
 * consume/isolation 模式必须显式传入 --evaluate；这些调用不是纯只读操作。 */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "../../wine-src/include/slpublic.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define COUNT_SENTINEL 0xfeedbeefu

struct api
{
    HMODULE module;
    HRESULT (WINAPI *open)(HSLC *);
    HRESULT (WINAPI *close)(HSLC);
    HRESULT (WINAPI *query)(HSLC, const SLID *, const SLID *, LPCWSTR, UINT *, SL_LICENSING_STATUS **);
    HRESULT (WINAPI *consume)(HSLC, const SLID *, const SLID *, LPCWSTR, void *);
};

static unsigned int failures;

static void module_version(const WCHAR *path)
{
    DWORD size = GetFileVersionInfoSizeW(path, NULL);
    BYTE *data = size ? malloc(size) : NULL;
    VS_FIXEDFILEINFO *info = NULL;
    UINT bytes = 0;

    if (data && GetFileVersionInfoW(path, 0, size, data) &&
            VerQueryValueW(data, L"\\", (void **)&info, &bytes) && bytes >= sizeof(*info))
        printf("module-version=%u.%u.%u.%u\n", HIWORD(info->dwFileVersionMS),
                LOWORD(info->dwFileVersionMS), HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
    else printf("module-version=unavailable\n");
    free(data);
}

static BOOL load_api(const WCHAR *name, struct api *api)
{
    WCHAR path[MAX_PATH], resolved[MAX_PATH];
    HMODULE target;
    const WCHAR *base;
    UINT length = GetSystemDirectoryW(path, ARRAY_SIZE(path));

    memset(api, 0, sizeof(*api));
    if (!length || length + wcslen(name) + 6 >= ARRAY_SIZE(path)) return FALSE;
    wcscat(path, L"\\"); wcscat(path, name); wcscat(path, L".dll");
    api->module = LoadLibraryW(path);
    if (!api->module) { printf("load-error=%lu\n", GetLastError()); return FALSE; }
    api->open = (void *)GetProcAddress(api->module, "SLOpen");
    api->close = (void *)GetProcAddress(api->module, "SLClose");
    api->query = (void *)GetProcAddress(api->module, "SLGetLicensingStatusInformation");
    api->consume = (void *)GetProcAddress(api->module, "SLConsumeRight");
    printf("requested-module=%ls.dll pointer-bytes=%u\n", name, (unsigned int)sizeof(void *));
    module_version(path);
    if (!api->open || !api->close || !api->query || !api->consume) return FALSE;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCWSTR)(void *)api->query, &target) && GetModuleFileNameW(target, resolved, ARRAY_SIZE(resolved)))
    {
        base = wcsrchr(resolved, L'\\');
        printf("resolved-query-module=%ls\n", base ? base + 1 : resolved);
    }
    return TRUE;
}

static HSLC open_context(struct api *api, const char *label)
{
    HSLC handle = NULL;
    HRESULT hr = api->open(&handle);
    printf("%s.open hr=%#lx handle-null=%u\n", label, hr, handle == NULL);
    if (FAILED(hr) || !handle) failures++;
    return FAILED(hr) ? NULL : handle;
}

static void close_context(struct api *api, HSLC handle, const char *label)
{
    HRESULT hr = api->close(handle);
    printf("%s.close hr=%#lx\n", label, hr);
}

static void query_status(struct api *api, HSLC handle, const SLID *app, const SLID *sku,
        const WCHAR *right, BOOL null_count, BOOL null_array, const char *label)
{
    UINT count = COUNT_SENTINEL, index, zero = 0, unlicensed = 0, licensed = 0, grace = 0, notification = 0;
    SL_LICENSING_STATUS *status = NULL;
    GUID empty = {0};
    HRESULT hr = api->query(handle, app, sku, right, null_count ? NULL : &count, null_array ? NULL : &status);

    printf("%s.query hr=%#lx count=", label, hr);
    if (count == COUNT_SENTINEL) printf("unchanged"); else printf("%u", count);
    printf(" array-null=%u\n", status == NULL);
    if (SUCCEEDED(hr) && !null_count && !null_array)
    {
        if (count == COUNT_SENTINEL || count > 512 || (count && !status))
        {
            printf("%s.malformed-success=1\n", label);
            failures++;
            return;
        }
        for (index = 0; index < count; index++)
        {
            if (IsEqualGUID(&status[index].SkuId, &empty)) zero++;
            switch (status[index].eStatus)
            {
            case SL_LICENSING_STATUS_UNLICENSED: unlicensed++; break;
            case SL_LICENSING_STATUS_LICENSED: licensed++; break;
            case SL_LICENSING_STATUS_IN_GRACE_PERIOD: grace++; break;
            case SL_LICENSING_STATUS_NOTIFICATION: notification++; break;
            default: failures++; break;
            }
        }
        /* 只汇总类别，不输出或导出密钥、设备绑定、有效期、宽限数值。 */
        printf("%s.shape zero-skus=%u unlicensed=%u licensed=%u grace=%u notification=%u\n",
                label, zero, unlicensed, licensed, grace, notification);
        LocalFree(status);
    }
}

int wmain(int argc, WCHAR **argv)
{
    struct api api, other;
    HSLC first, second = NULL;
    GUID app, sku, unknown;
    const WCHAR *mode;
    BOOL evaluate, consume_case;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (argc < 3 || (wcscmp(argv[1], L"sppc") && wcscmp(argv[1], L"slc")))
    {
        printf("usage: sppcprobe sppc|slc <case> [--evaluate]\n"
                "cases: fresh explicit consume consume-sku isolation failure-history invalid-consume\n"
                "       closed invalid outputs failure-outputs cross\n");
        return 1;
    }
    mode = argv[2];
    evaluate = argc == 4 && !wcscmp(argv[3], L"--evaluate");
    consume_case = !wcscmp(mode, L"consume") || !wcscmp(mode, L"consume-sku") ||
            !wcscmp(mode, L"failure-history") || !wcscmp(mode, L"invalid-consume") || !wcscmp(mode, L"isolation");
    if (consume_case && !evaluate)
    {
        printf("rights-evaluation-not-enabled\n");
        return 1;
    }
    CLSIDFromString(L"{0ff1ce15-a989-479d-af46-f275c6370663}", &app);
    /* 已注册的 Office16 O365HomePremR Subscription1，只用于指定 SKU 查询。 */
    CLSIDFromString(L"{537ea5b5-7d50-4876-bd38-a53a77caca32}", &sku);
    CLSIDFromString(L"{11111111-2222-3333-4444-555555555555}", &unknown);
    if (!load_api(argv[1], &api)) return 1;
    first = open_context(&api, "first");
    if (!first) return 1;
    printf("case=%ls\n", mode);
    if (!wcscmp(mode, L"fresh"))
    {
        query_status(&api, first, NULL, NULL, NULL, FALSE, FALSE, "fresh");
        second = open_context(&api, "second");
        if (second)
        {
            printf("handles-distinct=%u\n", first != second);
            query_status(&api, second, NULL, NULL, NULL, FALSE, FALSE, "second-fresh");
        }
    }
    else if (!wcscmp(mode, L"explicit"))
    {
        query_status(&api, first, &app, NULL, NULL, FALSE, FALSE, "application");
        query_status(&api, first, NULL, &sku, NULL, FALSE, FALSE, "sku");
        query_status(&api, first, &app, &sku, NULL, FALSE, FALSE, "application-sku");
        query_status(&api, first, &unknown, NULL, NULL, FALSE, FALSE, "unknown-application");
        query_status(&api, first, &app, &unknown, NULL, FALSE, FALSE, "unknown-sku");
        query_status(&api, first, NULL, NULL, NULL, FALSE, FALSE, "after-explicit-query");
    }
    else if (consume_case)
    {
        if (!wcscmp(mode, L"isolation")) second = open_context(&api, "second");
        query_status(&api, first, NULL, NULL, NULL, FALSE, FALSE, "before-consume");
        hr = api.consume(first, &app, !wcscmp(mode, L"consume-sku") ? &sku : NULL, NULL, NULL);
        printf("consume-office-application hr=%#lx\n", hr);
        query_status(&api, first, NULL, NULL, NULL, FALSE, FALSE, "after-consume");
        if (!wcscmp(mode, L"consume-sku"))
            query_status(&api, first, NULL, &sku, NULL, FALSE, FALSE, "sku-only-after-consume");
        if (second) query_status(&api, second, NULL, NULL, NULL, FALSE, FALSE, "other-handle");
        if (!wcscmp(mode, L"failure-history") || !wcscmp(mode, L"invalid-consume"))
        {
            hr = !wcscmp(mode, L"failure-history") ? api.consume(first, &app, &unknown, NULL, NULL) :
                    api.consume(first, NULL, NULL, NULL, NULL);
            printf("consume-rejected-input hr=%#lx\n", hr);
            query_status(&api, first, NULL, NULL, NULL, FALSE, FALSE, "after-rejected-consume");
        }
    }
    else if (!wcscmp(mode, L"failure-outputs"))
    {
        UINT count = COUNT_SENTINEL;
        SL_LICENSING_STATUS *status = (void *)(UINT_PTR)0x1234;
        hr = api.query(first, NULL, NULL, NULL, &count, &status);
        printf("failure-outputs hr=%#lx count-unchanged=%u array-unchanged=%u array-null=%u\n", hr,
                count == COUNT_SENTINEL, status == (void *)(UINT_PTR)0x1234, status == NULL);
        if (SUCCEEDED(hr) && status != (void *)(UINT_PTR)0x1234) LocalFree(status);
    }
    else if (!wcscmp(mode, L"outputs"))
    {
        query_status(&api, first, &app, NULL, NULL, TRUE, FALSE, "null-count");
        query_status(&api, first, &app, NULL, NULL, FALSE, TRUE, "null-array");
        query_status(&api, first, &app, NULL, L"probe-invalid-right", FALSE, FALSE, "non-null-right");
    }
    else if (!wcscmp(mode, L"invalid"))
    {
        query_status(&api, NULL, &app, NULL, NULL, FALSE, FALSE, "null-handle");
        query_status(&api, (HSLC)(UINT_PTR)0x1234, &app, NULL, NULL, FALSE, FALSE, "invalid-handle");
    }
    else if (!wcscmp(mode, L"closed"))
    {
        close_context(&api, first, "first");
        query_status(&api, first, &app, NULL, NULL, FALSE, FALSE, "closed-handle");
        first = NULL;
    }
    else if (!wcscmp(mode, L"cross"))
    {
        if (load_api(!wcscmp(argv[1], L"sppc") ? L"slc" : L"sppc", &other))
        {
            query_status(&other, first, &app, NULL, NULL, FALSE, FALSE, "cross-query");
            FreeLibrary(other.module);
        }
        else failures++;
    }
    else { printf("unknown-mode\n"); failures++; }
    if (second) close_context(&api, second, "second");
    if (first) close_context(&api, first, "first");
    FreeLibrary(api.module);
    printf("probe-failures=%u\n", failures);
    return failures ? 1 : 0;
}
