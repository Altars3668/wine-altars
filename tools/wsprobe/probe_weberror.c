/* probe_weberror: Windows.Web.WebError.GetStatus over the error codes a web request can end with. */
#include <winsock2.h>
#define WIDL_using_Windows_Networking_Sockets
#define WIDL_using_Windows_Web
#define WIDL_using_Windows_Security_Cryptography_Certificates
#include "wr.h"
#include "windows.networking.sockets.h"
#include "windows.web.h"

int main(void)
{
    static const INT32 others[] = { 0, 1, E_FAIL, E_ABORT, E_INVALIDARG, E_OUTOFMEMORY, E_ACCESSDENIED, 0x800704c7, 0x800704cd,
                                    0x80070002, 0x80000013, 0x8007274c, 0x8007274d, 0x80072746, 0x80090325, 0x80090327,
                                    0x80092010, 0x800b0101, 0x800b010f, 0x800b0109, 0x80190000, 0x80190063, 0x801902bd,
                                    0x00072efd, 0xc0072efd, 0x80082efd, 0x80272efd };
    IWebErrorStatics *web_error;
    IWebSocketErrorStatics *ws_error;
    WebErrorStatus a, b;
    HRESULT hr1, hr2;
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    RoInitialize(RO_INIT_MULTITHREADED);
    web_error = get_factory(L"Windows.Web.WebError", &IID_IWebErrorStatics);
    ws_error = get_factory(L"Windows.Networking.Sockets.WebSocketError", &IID_IWebSocketErrorStatics);
    for (i = 12000; i < 12200; i++)
    {
        INT32 hr = 0x80070000 | i;
        a = b = 999;
        hr1 = IWebErrorStatics_GetStatus(web_error, hr, &a);
        hr2 = IWebSocketErrorStatics_GetStatus(ws_error, hr, &b);
        if (a || b || hr1 || hr2) printf("%d %#lx: %#lx %d %#lx %d\n", i, (ULONG)hr, hr1, a, hr2, b);
    }
    for (i = 0; i < 1000; i++)
    {
        INT32 hr = 0x80190000 | i;
        a = b = 999;
        hr1 = IWebErrorStatics_GetStatus(web_error, hr, &a);
        hr2 = IWebSocketErrorStatics_GetStatus(ws_error, hr, &b);
        if (a || b || hr1 || hr2) printf("http %d %#lx: %#lx %d %#lx %d\n", i, (ULONG)hr, hr1, a, hr2, b);
    }
    for (i = 0; i < ARRAY_SIZE(others); i++)
    {
        a = b = 999;
        hr1 = IWebErrorStatics_GetStatus(web_error, others[i], &a);
        hr2 = IWebSocketErrorStatics_GetStatus(ws_error, others[i], &b);
        printf("other %#lx: %#lx %d %#lx %d\n", (ULONG)others[i], hr1, a, hr2, b);
    }
    print_class("web error factory", web_error);
    print_class("websocket error factory", ws_error);
    RoUninitialize();
    printf("done\n");
    return 0;
}
