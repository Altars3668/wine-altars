/* 只检查产品标识契约；不安装密钥、不激活、不修改许可证或 Office 状态。 */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
/* MinGW 的头尚缺此结构；使用本项目实际构建 SPPC 时的公开 ABI。 */
#include "../../wine-src/include/slpublic.h"
#include "../../wine-src/include/slerror.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    typedef HRESULT (WINAPI *open_fn)(HSLC *);
    typedef HRESULT (WINAPI *close_fn)(HSLC);
    typedef HRESULT (WINAPI *status_fn)(HSLC, const SLID *, const SLID *, LPCWSTR,
            UINT *, SL_LICENSING_STATUS **);
    typedef HRESULT (WINAPI *classify_fn)(LPCWSTR, UINT *);
    typedef HRESULT (WINAPI *consume_fn)(HSLC, const SLID *, const SLID *, LPCWSTR, void *);
    HMODULE sppc, mso;
    HSLC handle = NULL;
    SL_LICENSING_STATUS *statuses = NULL;
    GUID empty = {0}, app;
    BOOL catalog_mode = argc == 2 && !strcmp(argv[1], "--catalog");
    BOOL evaluate_mode = argc == 2 && !strcmp(argv[1], "--evaluate");
    WCHAR text[40];
    UINT count = 0, index, category, unknown = 0;
    HRESULT hr;
    open_fn open;
    close_fn close;
    status_fn status;
    classify_fn classify;
    consume_fn consume;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1 && !catalog_mode && !evaluate_mode)
    {
        printf("usage: sku-contract [--catalog|--evaluate]\n");
        return 1;
    }
    CLSIDFromString(L"{0ff1ce15-a989-479d-af46-f275c6370663}", &app);
    SetDllDirectoryW(L"C:\\Program Files\\Common Files\\Microsoft Shared\\OFFICE16");
    sppc = LoadLibraryW(L"sppc.dll");
    mso = LoadLibraryW(L"C:\\Program Files\\Common Files\\Microsoft Shared\\OFFICE16\\Mso20win32client.dll");
    if (!sppc || !mso) { printf("load-failed error=%lu\n", GetLastError()); return 1; }
    open = (open_fn)(void *)GetProcAddress(sppc, "SLOpen");
    close = (close_fn)(void *)GetProcAddress(sppc, "SLClose");
    status = (status_fn)(void *)GetProcAddress(sppc, "SLGetLicensingStatusInformation");
    classify = (classify_fn)(void *)GetProcAddress(mso, MAKEINTRESOURCEA(38131));
    if (!open || !close || !status || !classify) return 1;

    /* 该常量来自此构建的只读 SKU 表，只作分类器正控制，不注入许可接口。 */
    hr = classify(L"{195CBC52-71CC-4909-A7A3-90D745D26465}", &category);
    printf("catalog-control hr=%#lx category=%u\n", hr, category);
    if (hr != S_OK) return 1;
    if (FAILED(hr = open(&handle))) { printf("open-failed hr=%#lx\n", hr); return 1; }
    if (evaluate_mode)
    {
        consume = (consume_fn)(void *)GetProcAddress(sppc, "SLConsumeRight");
        if (!consume) { close(handle); return 1; }
        hr = consume(handle, &app, NULL, NULL, NULL);
        printf("rights-evaluation hr=%#lx\n", hr);
    }
    hr = status(handle, catalog_mode ? &app : NULL, NULL, NULL, &count, &statuses);
    printf("actual-status-query hr=%#lx count=%u\n", hr, count);
    if (!catalog_mode && !evaluate_mode && hr == (HRESULT)SL_E_RIGHT_NOT_CONSUMED && !count && !statuses)
    {
        close(handle);
        printf("fresh-context-has-no-invented-history=1\n");
        return 0;
    }
    if (hr != S_OK || count > 256 || (count && !statuses)) { close(handle); return 1; }
    for (index = 0; index < count; index++)
    {
        if (!StringFromGUID2(&statuses[index].SkuId, text, 40)) { LocalFree(statuses); close(handle); return 1; }
        hr = classify(text, &category);
        printf("record=%u zero-sku=%d state=%u reason=%#lx classification=%#lx\n",
                index, IsEqualGUID(&statuses[index].SkuId, &empty), statuses[index].eStatus,
                statuses[index].hrReason, hr);
        if (FAILED(hr)) unknown++;
    }
    LocalFree(statuses);
    close(handle);
    printf("unrecognized-records=%u\n", unknown);
    return unknown ? 2 : 0;
}
