/*
 * 直接调用 WAM 的 WinRT ABI，避免 PowerShell 的 System.__ComObject 投影歧义。
 * 仅输出 HRESULT、集合键名、字符串长度和身份摘要，不输出凭据值。
 * 可选导出目录必须由调用方预先建立并限制访问权限。
 *
 * ABI 对照：microsoft/windows-rs 的 Windows/Security/Authentication/Web/Core
 * 和 Windows/Security/Credentials；仅使用下列已核对签名的槽位。
 *
 * Copyright 2026 AltarsCN. LGPL 2.1 or later.
 */
#define COBJMACROS
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <inspectable.h>
#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define METHOD(o, n, type) ((type)(*(void ***)(o))[n])
#define RELEASE(o) do { if (o) IUnknown_Release((IUnknown *)(o)); } while (0)

typedef HRESULT (WINAPI *get_object_fn)(void *, void **);
typedef HRESULT (WINAPI *get_string_fn)(void *, HSTRING *);
typedef HRESULT (WINAPI *get_uint_fn)(void *, UINT32 *);
typedef HRESULT (WINAPI *get_bool_fn)(void *, BYTE *);
typedef HRESULT (WINAPI *find_provider_fn)(void *, HSTRING, HSTRING, void **);
typedef HRESULT (WINAPI *create_request_fn)(void *, void *, HSTRING, HSTRING, void **);
typedef HRESULT (WINAPI *token_request_fn)(void *, void *, void **);
typedef HRESULT (WINAPI *vector_get_fn)(void *, UINT32, void **);
typedef HRESULT (WINAPI *map_insert_fn)(void *, HSTRING, HSTRING, BYTE *);
typedef HRESULT (WINAPI *map_lookup_fn)(void *, HSTRING, HSTRING *);

static const WCHAR *output_dir;
static unsigned int failures;

static HRESULT check(const char *what, HRESULT hr)
{
    printf("%s hr=0x%08lx\n", what, (unsigned long)hr);
    if (FAILED(hr)) failures++;
    return hr;
}

static HRESULT query(void *obj, const WCHAR *text, void **out)
{
    GUID iid;
    *out = NULL;
    if (!obj) return E_POINTER;
    if (FAILED(CLSIDFromString(text, &iid))) return E_INVALIDARG;
    return IUnknown_QueryInterface((IUnknown *)obj, &iid, out);
}

static HSTRING string(const WCHAR *text)
{
    HSTRING result = NULL;
    if (FAILED(WindowsCreateString(text, wcslen(text), &result))) failures++;
    return result;
}

static HRESULT factory(const WCHAR *name, const WCHAR *iid_text, void **out)
{
    GUID iid;
    HSTRING class_name = string(name);
    HRESULT hr = CLSIDFromString(iid_text, &iid);
    if (SUCCEEDED(hr)) hr = RoGetActivationFactory(class_name, &iid, out);
    WindowsDeleteString(class_name);
    return hr;
}

static HRESULT await(void *operation, void **out)
{
    void *info = NULL;
    UINT32 status = 0;
    ULONGLONG deadline = GetTickCount64() + 120000;
    HRESULT hr, error = S_OK;

    *out = NULL;
    hr = query(operation, L"{00000036-0000-0000-c000-000000000046}", &info);
    if (FAILED(hr)) return hr;
    do
    {
        hr = METHOD(info, 7, get_uint_fn)(info, &status);
        if (FAILED(hr) || status) break;
        Sleep(20);
    } while (GetTickCount64() < deadline);
    if (SUCCEEDED(hr) && status == 3)
    {
        hr = METHOD(info, 8, get_uint_fn)(info, (UINT32 *)&error);
        if (SUCCEEDED(hr)) hr = FAILED(error) ? error : E_UNEXPECTED;
    }
    if (SUCCEEDED(hr) && status != 1)
        hr = HRESULT_FROM_WIN32(status == 2 ? ERROR_CANCELLED : WAIT_TIMEOUT);
    if (SUCCEEDED(hr)) hr = METHOD(operation, 8, get_object_fn)(operation, out);
    RELEASE(info);
    return hr;
}

static void identity_shape(const char *label, HSTRING text)
{
    const WCHAR *value;
    UINT32 len;
    BYTE hash[32];
    NTSTATUS status;
    unsigned int i;

    value = WindowsGetStringRawBuffer(text, &len);
    status = BCryptHash(BCRYPT_SHA256_ALG_HANDLE, NULL, 0, (BYTE *)value,
            len * sizeof(*value), hash, sizeof(hash));
    printf("%s chars=%u sha256-utf16-prefix=", label, len);
    if (status >= 0) for (i = 0; i < 8; i++) printf("%02x", hash[i]);
    else printf("unavailable");
    putchar('\n');
}

static void safe_key(HSTRING key)
{
    const WCHAR *s;
    UINT32 len, i;
    s = WindowsGetStringRawBuffer(key, &len);
    for (i = 0; i < len && i < 100; i++)
        putchar(s[i] >= 32 && s[i] < 127 ? s[i] : '?');
    if (len > 100) printf("...");
}

static FILE *export_file(const WCHAR *name)
{
    WCHAR path[MAX_PATH];
    FILE *file;
    if (!output_dir) return NULL;
    if (swprintf(path, ARRAY_SIZE(path), L"%ls\\%ls", output_dir, name) < 0)
    {
        failures++;
        return NULL;
    }
    file = _wfopen(path, L"wb");
    if (!file) { printf("export-open-failed errno=%d\n", errno); failures++; }
    return file;
}

static void export_string(const WCHAR *name, HSTRING text)
{
    FILE *f = export_file(name);
    const WCHAR *s;
    UINT32 len;
    int bytes;
    char *utf8;
    if (!f) return;
    s = WindowsGetStringRawBuffer(text, &len);
    bytes = WideCharToMultiByte(CP_UTF8, 0, s, len, NULL, 0, NULL, NULL);
    utf8 = malloc(bytes ? bytes : 1);
    if (!utf8) { fclose(f); failures++; return; }
    WideCharToMultiByte(CP_UTF8, 0, s, len, utf8, bytes, NULL, NULL);
    if (fwrite(utf8, 1, bytes, f) != (size_t)bytes) failures++;
    SecureZeroMemory(utf8, bytes);
    free(utf8);
    if (fclose(f)) failures++;
}

static void map_shape(const char *label, void *map, const WCHAR *file_name)
{
    void *iterable = NULL, *iterator = NULL, *pair = NULL;
    HSTRING key = NULL, value = NULL;
    BYTE has = 0;
    UINT32 count = 0, len;
    const WCHAR *raw;
    HRESULT hr;
    FILE *f = NULL;

    printf("%s null=%u\n", label, map == NULL);
    if (!map) return;
    if (FAILED(check("map.size", METHOD(map, 7, get_uint_fn)(map, &count)))) return;
    printf("%s count=%u\n", label, count);
    if (FAILED(check("map.iterable", query(map,
            L"{e9bdaaf0-cbf6-5c72-be90-29cbf3a1319b}", &iterable)))) return;
    if (FAILED(check("map.first", METHOD(iterable, 6, get_object_fn)(iterable, &iterator)))) goto done;
    if (file_name) f = export_file(file_name);
    if (f && fwrite(&count, sizeof(count), 1, f) != 1) failures++;
    hr = METHOD(iterator, 7, get_bool_fn)(iterator, &has);
    while (SUCCEEDED(hr) && has)
    {
        if (FAILED(hr = METHOD(iterator, 6, get_object_fn)(iterator, &pair))) break;
        if (FAILED(hr = METHOD(pair, 6, get_string_fn)(pair, &key))) break;
        if (FAILED(hr = METHOD(pair, 7, get_string_fn)(pair, &value))) break;
        printf("  key="); safe_key(key); printf(" chars=%u\n", WindowsGetStringLen(value));
        if (f)
        {
            raw = WindowsGetStringRawBuffer(key, &len);
            if (fwrite(&len, sizeof(len), 1, f) != 1 || fwrite(raw, sizeof(*raw), len, f) != len) failures++;
            raw = WindowsGetStringRawBuffer(value, &len);
            if (fwrite(&len, sizeof(len), 1, f) != 1 || fwrite(raw, sizeof(*raw), len, f) != len) failures++;
        }
        WindowsDeleteString(key); key = NULL;
        WindowsDeleteString(value); value = NULL;
        RELEASE(pair); pair = NULL;
        hr = METHOD(iterator, 8, get_bool_fn)(iterator, &has);
    }
    check("map.iteration", hr);
done:
    if (f && fclose(f)) failures++;
    WindowsDeleteString(key); WindowsDeleteString(value);
    RELEASE(pair); RELEASE(iterator); RELEASE(iterable);
}

static HRESULT set_request_properties(void *request, BOOL silent_profile)
{
    static const WCHAR *pairs[][2] = {
        {L"Client_uiflow", L"new_account"}, {L"api-version", L"2.0"},
        {L"oauth2_batch", L"1"}, {L"x-client-info", L"1"},
        {L"telemetry", L"MATS"}, {L"x-client-SKU", L"MSAL.xplat.Win32"},
        {L"x-client-src-SKU", L"MSAL.xplat.Win32"}, {L"x-client-OS", L"10.0.19045.5796"},
        {L"x-client-Ver", L"10.0.0"}, {L"x-client-xtra-sku", L"MSAL.xplat.Win32|10.0.0,|,|,|,|"},
        {L"mkt", L"zh-CN"}, {L"fl", L"easi2"}, {L"lw", L"1"},
        {L"noauthcancel", L"1"}, {L"signup", L"1"}
    };
    void *map = NULL;
    HSTRING key, value, back;
    HRESULT hr;
    BYTE replaced;
    INT32 order;
    unsigned int i, checked = 0;

    if (FAILED(hr = METHOD(request, 10, get_object_fn)(request, &map))) return hr;
    if (!map) return E_UNEXPECTED;
    for (i = 0; i < ARRAY_SIZE(pairs); i++)
    {
        /* 后续静默请求没有创建账户的五项 UI 属性。 */
        if (silent_profile && (i == 0 || i >= 11)) continue;
        key = string(pairs[i][0]); value = string(pairs[i][1]); back = NULL;
        hr = METHOD(map, 10, map_insert_fn)(map, key, value, &replaced);
        if (SUCCEEDED(hr)) hr = METHOD(map, 6, map_lookup_fn)(map, key, &back);
        if (SUCCEEDED(hr)) hr = WindowsCompareStringOrdinal(value, back, &order);
        if (SUCCEEDED(hr) && order) hr = E_UNEXPECTED;
        WindowsDeleteString(key); WindowsDeleteString(value); WindowsDeleteString(back);
        if (FAILED(hr)) break;
        checked++;
    }
    printf("request-property-roundtrips=%u\n", checked);
    RELEASE(map);
    return hr;
}

int wmain(int argc, WCHAR **argv)
{
    void *manager = NULL, *request_factory = NULL, *provider = NULL;
    void *op = NULL, *request = NULL, *result = NULL, *responses = NULL;
    void *response = NULL, *account = NULL, *account2 = NULL, *map = NULL, *error = NULL;
    HSTRING provider_id = NULL, authority = NULL, scope = NULL, client_id = NULL, text = NULL;
    UINT32 status = 0, count = 0, state = 0, i;
    DWORD attr;
    HRESULT hr;
    const WCHAR *raw;
    WCHAR name[80];
    BOOL office;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 2 || (wcscmp(argv[1], L"plain") && wcscmp(argv[1], L"office") &&
                     wcscmp(argv[1], L"office-silent") && wcscmp(argv[1], L"control")))
    {
        printf("usage: wamprobe plain|office|office-silent|control [private-output-directory [scope]]\n");
        return 1;
    }
    office = !wcscmp(argv[1], L"office") || !wcscmp(argv[1], L"office-silent");
    if (argc > 2 && wcscmp(argv[2], L"-"))
    {
        output_dir = argv[2];
        attr = GetFileAttributesW(output_dir);
        if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) return 1;
    }
    if (FAILED(check("RoInitialize", hr = RoInitialize(RO_INIT_MULTITHREADED)))) return 1;
    if (!wcscmp(argv[1], L"control"))
    {
        typedef HRESULT (WINAPI *create_response_fn)(void *, HSTRING, void **);
        /* 只构造受控对象，不查账户、不调用 broker、不联网。 */
        if (FAILED(check("control.factory", factory(L"Windows.Security.Authentication.Web.Core.WebTokenResponse",
                L"{ab6bf7f8-5450-4ef6-97f7-052b0431c0f0}", &request_factory)))) goto done;
        text = string(L"probe-control");
        if (FAILED(check("control.create", METHOD(request_factory, 6, create_response_fn)(request_factory,
                text, &response)))) goto done;
        if (FAILED(check("control.properties", METHOD(response, 9, get_object_fn)(response, &map)))) goto done;
        map_shape("control-properties", map, NULL);
        if (!map) failures++;
        goto done;
    }
    if (FAILED(check("manager.factory", factory(L"Windows.Security.Authentication.Web.Core.WebAuthenticationCoreManager",
            L"{6aca7c92-a581-4479-9c10-752eff44fd34}", &manager)))) goto done;
    provider_id = string(L"https://login.microsoft.com"); authority = string(L"consumers");
    scope = string(argc > 3 ? argv[3] : L"service::ssl.live.com::MBI_SSL_SHORT openid profile");
    client_id = string(L"00000000480728C5");
    export_string(L"request-scope", scope);
    export_string(L"request-client-id", client_id);
    text = string(argv[1]); export_string(L"request-profile", text);
    WindowsDeleteString(text); text = NULL;
    if (FAILED(check("find.provider", METHOD(manager, 12, find_provider_fn)(manager, provider_id, authority, &op)))) goto done;
    if (FAILED(check("find.await", await(op, &provider)))) goto done;
    if (FAILED(check("find.provider.present", provider ? S_OK : E_UNEXPECTED))) goto done;
    RELEASE(op); op = NULL;
    if (FAILED(check("request.factory", factory(L"Windows.Security.Authentication.Web.Core.WebTokenRequest",
            L"{6cf2141c-0ff0-4c67-b84f-99ddbe4a72c9}", &request_factory)))) goto done;
    if (FAILED(check("request.create", METHOD(request_factory, 6, create_request_fn)(request_factory,
            provider, scope, client_id, &request)))) goto done;
    if (office && FAILED(check("request.properties", set_request_properties(request, !wcscmp(argv[1], L"office-silent"))))) goto done;
    if (FAILED(check("request.silent", METHOD(manager, 6, token_request_fn)(manager, request, &op)))) goto done;
    if (FAILED(check("request.await", await(op, &result)))) goto done;
    if (FAILED(check("result.present", result ? S_OK : E_UNEXPECTED))) goto done;
    if (FAILED(check("result.status", METHOD(result, 7, get_uint_fn)(result, &status)))) goto done;
    printf("response-status=%u mode=%ls\n", status, argv[1]);
    if (FAILED(check("result.error", METHOD(result, 8, get_object_fn)(result, &error)))) goto done;
    printf("response-error-null=%u\n", error == NULL);
    if (error)
    {
        check("error.code", METHOD(error, 6, get_uint_fn)(error, &state));
        printf("provider-error-code=0x%08x\n", state);
    }
    if (status) { failures++; goto done; }
    if (FAILED(check("result.data", METHOD(result, 6, get_object_fn)(result, &responses)))) goto done;
    if (FAILED(check("data.present", responses ? S_OK : E_UNEXPECTED))) goto done;
    if (FAILED(check("data.size", METHOD(responses, 7, get_uint_fn)(responses, &count)))) goto done;
    printf("response-count=%u\n", count);
    if (!count || count > 16) { failures++; goto done; }
    for (i = 0; i < count; i++)
    {
        if (FAILED(check("data.get", METHOD(responses, 6, vector_get_fn)(responses, i, &response)))) goto done;
        if (FAILED(check("response.token", METHOD(response, 6, get_string_fn)(response, &text)))) goto done;
        raw = WindowsGetStringRawBuffer(text, NULL);
        printf("token chars=%u kind=%s\n", WindowsGetStringLen(text),
                raw[0] == '{' || raw[0] == '[' ? "json" : raw[0] == '<' ? "xml" : "opaque");
        swprintf(name, ARRAY_SIZE(name), L"response-%u-token", i); export_string(name, text);
        WindowsDeleteString(text); text = NULL;
        if (FAILED(check("response.properties", METHOD(response, 9, get_object_fn)(response, &map)))) goto done;
        swprintf(name, ARRAY_SIZE(name), L"response-%u-properties", i); map_shape("response-properties", map, name);
        RELEASE(map); map = NULL;
        if (FAILED(check("response.account", METHOD(response, 8, get_object_fn)(response, &account)))) goto done;
        if (FAILED(check("account.present", account ? S_OK : E_UNEXPECTED))) goto done;
        if (FAILED(check("account.qi2", query(account, L"{7b56d6f8-990b-4eb5-94a7-5621f3a8b824}", &account2)))) goto done;
        if (FAILED(check("account.id", METHOD(account2, 6, get_string_fn)(account2, &text)))) goto done;
        identity_shape("account-id", text);
        swprintf(name, ARRAY_SIZE(name), L"response-%u-account-id", i); export_string(name, text);
        WindowsDeleteString(text); text = NULL;
        if (FAILED(check("account.name", METHOD(account, 7, get_string_fn)(account, &text)))) goto done;
        identity_shape("account-name", text);
        swprintf(name, ARRAY_SIZE(name), L"response-%u-account-name", i); export_string(name, text);
        WindowsDeleteString(text); text = NULL;
        if (FAILED(check("account.state", METHOD(account, 8, get_uint_fn)(account, &state)))) goto done;
        printf("account-state=%u\n", state);
        if (FAILED(check("account.properties", METHOD(account2, 7, get_object_fn)(account2, &map)))) goto done;
        swprintf(name, ARRAY_SIZE(name), L"response-%u-account-properties", i); map_shape("account-properties", map, name);
        RELEASE(map); map = NULL;
        RELEASE(account2); account2 = NULL;
        RELEASE(account); account = NULL;
        RELEASE(response); response = NULL;
    }
done:
    RELEASE(map); RELEASE(account2); RELEASE(account); RELEASE(response);
    RELEASE(responses); RELEASE(result); RELEASE(error); RELEASE(request);
    RELEASE(op); RELEASE(provider); RELEASE(request_factory); RELEASE(manager);
    WindowsDeleteString(text); WindowsDeleteString(provider_id); WindowsDeleteString(authority);
    WindowsDeleteString(scope); WindowsDeleteString(client_id);
    RoUninitialize();
    printf("failures=%u\n", failures);
    return failures ? 1 : 0;
}
