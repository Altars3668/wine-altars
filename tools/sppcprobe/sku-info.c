/* SLGetProductSkuInformation 的原生对照，只查产品定义元数据。
 *
 * 属性名是白名单：ApplicationBitmap 描述某个 SKU 覆盖哪些应用，属于安装自带的
 * 产品定义，不是密钥、认证材料、设备绑定或许可状态。另一个名字用于确认未知属性的
 * 失败码。不调用 SLConsumeRight，不做权利评估，不安装或激活任何东西。
 */
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include "../../wine-src/include/slpublic.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

typedef HRESULT (WINAPI *open_fn)(HSLC *);
typedef HRESULT (WINAPI *close_fn)(HSLC);
typedef HRESULT (WINAPI *sku_fn)(HSLC, const SLID *, LPCWSTR, SLDATATYPE *, UINT *, BYTE **);

static const WCHAR *const names[] = {L"ApplicationBitmap", L"WineAltarsSkuProbeMissingProperty"};

int wmain(int argc, WCHAR **argv)
{
    WCHAR path[MAX_PATH];
    HMODULE module;
    open_fn open_context;
    close_fn close_context;
    sku_fn query;
    HSLC handle = NULL, query_handle;
    const WCHAR *name, *mode;
    SLID sku;
    SLDATATYPE type = (SLDATATYPE)0xdead;
    UINT size = 0xdeadbeef, i;
    BYTE *value = (BYTE *)(UINT_PTR)0xdeadbeef;
    HRESULT hr;
    DWORD length;
    int failures = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (argc != 5 || (wcscmp(argv[1], L"sppc") && wcscmp(argv[1], L"slc")))
    {
        printf("usage: sku-info sppc|slc {sku-guid} bitmap|missing|null-handle|null-sku|null-size|null-data --query\n");
        return 1;
    }
    mode = argv[3];
    if (CLSIDFromString(argv[2], &sku) != NOERROR) {printf("bad-guid\n"); return 1;}
    name = wcscmp(mode, L"missing") ? names[0] : names[1];

    length = GetSystemDirectoryW(path, ARRAY_SIZE(path));
    if (!length || length + wcslen(argv[1]) + 6 >= ARRAY_SIZE(path)) return 1;
    wcscat(path, L"\\"); wcscat(path, argv[1]); wcscat(path, L".dll");
    if (!(module = LoadLibraryW(path))) {printf("load-error=%lu\n", GetLastError()); return 1;}
    open_context = (open_fn)(void *)GetProcAddress(module, "SLOpen");
    close_context = (close_fn)(void *)GetProcAddress(module, "SLClose");
    query = (sku_fn)(void *)GetProcAddress(module, "SLGetProductSkuInformation");
    if (!open_context || !close_context || !query) {printf("missing-export\n"); FreeLibrary(module); return 1;}

    printf("requested-module=%ls.dll case=%ls\n", argv[1], mode);
    hr = open_context(&handle);
    printf("open hr=%#lx handle-null=%u\n", (DWORD)hr, handle == NULL);
    if (FAILED(hr) || !handle) {FreeLibrary(module); return 1;}
    query_handle = !wcscmp(mode, L"null-handle") ? NULL : handle;

    hr = query(query_handle, !wcscmp(mode, L"null-sku") ? NULL : &sku, name,
               &type, !wcscmp(mode, L"null-size") ? NULL : &size,
               !wcscmp(mode, L"null-data") ? NULL : &value);
    printf("query hr=%#lx", (DWORD)hr);
    printf(" type=%s", type == (SLDATATYPE)0xdead ? "unchanged" : "set");
    if (type != (SLDATATYPE)0xdead) printf("(%u)", (UINT)type);
    printf(" size=%s", size == 0xdeadbeef ? "unchanged" : "set");
    if (size != 0xdeadbeef) printf("(%u)", size);
    printf(" data-unchanged=%u\n", value == (BYTE *)(UINT_PTR)0xdeadbeef);

    /* 只在成功且长度合理时打印内容：这是产品定义位图，不是凭据。 */
    if (SUCCEEDED(hr) && size != 0xdeadbeef && size && size <= 64 &&
        value != (BYTE *)(UINT_PTR)0xdeadbeef && value)
    {
        printf("value-bytes=");
        for (i = 0; i < size; i++) printf("%02x", value[i]);
        printf("\n");
        if (size == sizeof(DWORD)) printf("value-dword=%#lx\n", *(DWORD *)value);
        LocalFree(value);
    }
    else if (SUCCEEDED(hr)) {printf("unexpected-success-shape\n"); failures++;}

    hr = close_context(handle);
    printf("close hr=%#lx\nprobe-failures=%d\n", (DWORD)hr, failures);
    FreeLibrary(module);
    return failures ? 2 : 0;
}
