/* Exercises the OnlineId device-ticket contract exactly as Office does.
 *
 * This probe asks for a ticket and reports what came back.  It never asserts a
 * ticket was granted: on a machine with no MSA device credential the expected
 * answer is a completed operation carrying OnlineIdSystemTicketStatus_Error,
 * and that is what this checks.  No credential, account name or token value is
 * read or printed. */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
#include <stdio.h>

#define WIDL_using_Windows_Foundation
#include "windows.foundation.h"
#define WIDL_using_Windows_Security_Authentication_OnlineId
#include "windows.security.authentication.onlineid.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static const WCHAR *authenticator_class =
    L"Windows.Security.Authentication.OnlineId.OnlineIdSystemAuthenticator";
static const WCHAR *request_class =
    L"Windows.Security.Authentication.OnlineId.OnlineIdServiceTicketRequest";

/* The licensing endpoint Office actually asks for, in the three-part form its
 * advanced request uses. */
static const WCHAR *licensing_service =
    L"service::https://licensing.m365.svc.cloud.microsoft/::MBI_SSL_SHORT";

static unsigned failures;

static void check(int ok, const char *what)
{
    printf("%-46s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) failures++;
}

static HSTRING make_string(const WCHAR *text)
{
    HSTRING str = NULL;
    WindowsCreateString(text, (UINT32)wcslen(text), &str);
    return str;
}

int main(void)
{
    IOnlineIdSystemAuthenticatorStatics *statics = NULL;
    IOnlineIdSystemAuthenticatorForUser *authenticator = NULL;
    IOnlineIdServiceTicketRequestFactory *request_factory = NULL;
    IOnlineIdServiceTicketRequest *request = NULL;
    IAsyncOperation_OnlineIdSystemTicketResult *operation = NULL;
    IOnlineIdSystemTicketResult *result = NULL;
    __x_ABI_CWindows_CSecurity_CAuthentication_COnlineId_CIOnlineIdSystemIdentity *identity = (void *)(UINT_PTR)0x1234;
    IAsyncInfo *info = NULL;
    OnlineIdSystemTicketStatus status = (OnlineIdSystemTicketStatus)0x5555;
    AsyncStatus async_status = (AsyncStatus)0x5555;
    HSTRING service = NULL, policy = NULL, echoed = NULL;
    GUID app_id = {0x2b379600, 0xb42b, 0x4fe9, {0xa5, 0x9c, 0xa3, 0x12, 0xfb, 0x93, 0x49, 0x35}};
    GUID read_back = {0};
    HRESULT hr, extended = S_OK;
    UINT32 length = 0;
    const WCHAR *raw;

    setvbuf(stdout, NULL, _IONBF, 0);
    hr = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(hr)) {printf("RoInitialize hr=%#lx\n", hr); return 1;}

    service = make_string(authenticator_class);
    hr = RoGetActivationFactory(service, &IID_IOnlineIdSystemAuthenticatorStatics, (void **)&statics);
    WindowsDeleteString(service);
    check(SUCCEEDED(hr) && statics, "RoGetActivationFactory(authenticator)");
    if (FAILED(hr)) return 1;

    hr = IOnlineIdSystemAuthenticatorStatics_get_Default(statics, &authenticator);
    check(SUCCEEDED(hr) && authenticator, "get_Default");
    if (FAILED(hr)) return 1;

    /* The application id must survive a round trip; Office sets it before
     * asking for a ticket. */
    hr = IOnlineIdSystemAuthenticatorForUser_put_ApplicationId(authenticator, app_id);
    check(hr == S_OK, "put_ApplicationId");
    hr = IOnlineIdSystemAuthenticatorForUser_get_ApplicationId(authenticator, &read_back);
    check(hr == S_OK && IsEqualGUID(&read_back, &app_id), "get_ApplicationId round-trips");

    service = make_string(request_class);
    hr = RoGetActivationFactory(service, &IID_IOnlineIdServiceTicketRequestFactory, (void **)&request_factory);
    WindowsDeleteString(service);
    check(SUCCEEDED(hr) && request_factory, "RoGetActivationFactory(ticket request)");
    if (FAILED(hr)) return 1;

    /* Two-part form: service and policy are kept apart. */
    service = make_string(L"https://events.data.microsoft.com/OneCollector/1.0/");
    policy = make_string(L"NFS_1wk_compact_SSL");
    hr = IOnlineIdServiceTicketRequestFactory_CreateOnlineIdServiceTicketRequest(request_factory, service, policy, &request);
    check(SUCCEEDED(hr) && request, "CreateOnlineIdServiceTicketRequest");
    if (SUCCEEDED(hr))
    {
        hr = IOnlineIdServiceTicketRequest_get_Service(request, &echoed);
        raw = WindowsGetStringRawBuffer(echoed, &length);
        check(hr == S_OK && raw && !wcscmp(raw, L"https://events.data.microsoft.com/OneCollector/1.0/"),
              "  Service round-trips");
        WindowsDeleteString(echoed); echoed = NULL;
        hr = IOnlineIdServiceTicketRequest_get_Policy(request, &echoed);
        raw = WindowsGetStringRawBuffer(echoed, &length);
        check(hr == S_OK && raw && !wcscmp(raw, L"NFS_1wk_compact_SSL"), "  Policy round-trips");
        WindowsDeleteString(echoed); echoed = NULL;
        IOnlineIdServiceTicketRequest_Release(request);
        request = NULL;
    }
    WindowsDeleteString(service);
    WindowsDeleteString(policy);

    /* Advanced form: the policy rides inside the service string, so Policy is
     * empty rather than invented. */
    service = make_string(licensing_service);
    hr = IOnlineIdServiceTicketRequestFactory_CreateOnlineIdServiceTicketRequestAdvanced(request_factory, service, &request);
    WindowsDeleteString(service);
    check(SUCCEEDED(hr) && request, "CreateOnlineIdServiceTicketRequestAdvanced");
    if (FAILED(hr)) return 1;

    hr = IOnlineIdServiceTicketRequest_get_Service(request, &echoed);
    raw = WindowsGetStringRawBuffer(echoed, &length);
    check(hr == S_OK && raw && !wcscmp(raw, licensing_service), "  advanced Service round-trips");
    WindowsDeleteString(echoed); echoed = NULL;
    hr = IOnlineIdServiceTicketRequest_get_Policy(request, &echoed);
    check(hr == S_OK && echoed == NULL, "  advanced Policy is empty");

    hr = IOnlineIdSystemAuthenticatorForUser_GetTicketAsync(authenticator, request, &operation);
    check(SUCCEEDED(hr) && operation, "GetTicketAsync returns an operation");
    if (FAILED(hr)) return 1;

    /* Office queries IAsyncInfo on the operation, so it has to be reachable. */
    hr = IAsyncOperation_OnlineIdSystemTicketResult_QueryInterface(operation, &IID_IAsyncInfo, (void **)&info);
    check(SUCCEEDED(hr) && info, "operation exposes IAsyncInfo");
    if (SUCCEEDED(hr))
    {
        hr = IAsyncInfo_get_Status(info, &async_status);
        check(hr == S_OK && async_status == Completed, "  IAsyncInfo status is Completed");
        hr = IAsyncInfo_get_ErrorCode(info, &extended);
        check(hr == S_OK && extended == S_OK, "  IAsyncInfo error code is S_OK");
        IAsyncInfo_Release(info);
    }

    hr = IAsyncOperation_OnlineIdSystemTicketResult_GetResults(operation, &result);
    check(SUCCEEDED(hr) && result, "GetResults");
    if (FAILED(hr)) return 1;

    /* No MSA device credential here, so the honest answer is Error with a
     * reason -- not Success, and not a fabricated ticket. */
    hr = IOnlineIdSystemTicketResult_get_Status(result, &status);
    check(hr == S_OK && status == OnlineIdSystemTicketStatus_Error, "result status is Error (no device credential)");
    hr = IOnlineIdSystemTicketResult_get_ExtendedError(result, &extended);
    check(hr == S_OK && extended == HRESULT_FROM_WIN32(ERROR_NOT_FOUND), "extended error is ERROR_NOT_FOUND");
    hr = IOnlineIdSystemTicketResult_get_Identity(result, &identity);
    check(hr == S_OK && identity == NULL, "no identity is reported");

    IOnlineIdSystemTicketResult_Release(result);
    IAsyncOperation_OnlineIdSystemTicketResult_Release(operation);
    IOnlineIdServiceTicketRequest_Release(request);
    IOnlineIdServiceTicketRequestFactory_Release(request_factory);
    IOnlineIdSystemAuthenticatorForUser_Release(authenticator);
    IOnlineIdSystemAuthenticatorStatics_Release(statics);
    RoUninitialize();

    printf("onlineid ticket probe: failures=%u\n", failures);
    return failures ? 1 : 0;
}
