#include "probe.h"
#include <webservices.h>

int main(void)
{
    WS_ENDPOINT_ADDRESS address = {0};
    unsigned int mode, repeat;
    HRESULT hr;
    HRESULT (WINAPI *abort_channel_fn)(WS_CHANNEL *, WS_ERROR *);
    address.url.chars = (WCHAR *)L"http://127.0.0.1:9/";
    address.url.length = wcslen(address.url.chars);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (!probe_start()) return 1;
    abort_channel_fn = (void *)GetProcAddress(GetModuleHandleW(L"webservices.dll"), "WsAbortChannel");
    if (!abort_channel_fn) return probe_done(1);
    /* 只打开 HTTP channel 的配置，不发送消息或连接外部服务器。 */
    for (mode = 0; mode < 3; ++mode)
    {
        WS_SERVICE_PROXY *proxy = NULL;
        WS_SERVICE_PROXY_STATE state;
        hr = WsCreateServiceProxy(WS_CHANNEL_TYPE_REQUEST, WS_HTTP_CHANNEL_BINDING, NULL,
                                  NULL, 0, NULL, 0, &proxy, NULL);
        printf("proxy mode=%u create=%#lx\n", mode, hr);
        if (FAILED(hr)) return probe_done(1);
        if (mode)
        {
            hr = WsOpenServiceProxy(proxy, &address, NULL, NULL);
            printf("proxy mode=%u open=%#lx\n", mode, hr);
            if (FAILED(hr)) { WsFreeServiceProxy(proxy); return probe_done(1); }
        }
        if (mode == 2) printf("proxy mode=%u close_before=%#lx\n", mode, WsCloseServiceProxy(proxy, NULL, NULL));
        for (repeat = 0; repeat < 2; ++repeat)
        {
            hr = WsAbortServiceProxy(proxy, NULL);
            state = 0xdeadbeef;
            printf("proxy mode=%u abort=%u hr=%#lx ", mode, repeat, hr);
            hr = WsGetServiceProxyProperty(proxy, WS_PROXY_PROPERTY_STATE, &state, sizeof(state), NULL);
            printf("query=%#lx state=%u\n", hr, state);
        }
        hr = WsCloseServiceProxy(proxy, NULL, NULL);
        printf("proxy mode=%u close_after=%#lx ", mode, hr);
        hr = WsResetServiceProxy(proxy, NULL);
        printf("reset=%#lx\n", hr);
        WsFreeServiceProxy(proxy);
    }
    for (mode = 0; mode < 3; ++mode)
    {
        WS_CHANNEL *channel = NULL;
        WS_CHANNEL_STATE state;
        hr = WsCreateChannel(WS_CHANNEL_TYPE_REQUEST, WS_HTTP_CHANNEL_BINDING, NULL, 0, NULL, &channel, NULL);
        printf("channel mode=%u create=%#lx\n", mode, hr);
        if (FAILED(hr)) return probe_done(1);
        if (mode)
        {
            hr = WsOpenChannel(channel, &address, NULL, NULL);
            printf("channel mode=%u open=%#lx\n", mode, hr);
            if (FAILED(hr)) { WsFreeChannel(channel); return probe_done(1); }
        }
        if (mode == 2) printf("channel mode=%u close_before=%#lx\n", mode, WsCloseChannel(channel, NULL, NULL));
        for (repeat = 0; repeat < 2; ++repeat)
        {
            hr = abort_channel_fn(channel, NULL);
            state = 0xdeadbeef;
            printf("channel mode=%u abort=%u hr=%#lx ", mode, repeat, hr);
            hr = WsGetChannelProperty(channel, WS_CHANNEL_PROPERTY_STATE, &state, sizeof(state), NULL);
            printf("query=%#lx state=%u\n", hr, state);
        }
        hr = WsCloseChannel(channel, NULL, NULL);
        printf("channel mode=%u close_after=%#lx ", mode, hr);
        hr = WsResetChannel(channel, NULL);
        printf("reset=%#lx\n", hr);
        WsFreeChannel(channel);
    }
    return probe_done(0);
}
