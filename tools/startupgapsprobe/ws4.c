/* ws4: what the Windows Web Services API does with an SSL transport security binding -- on an HTTP and a TCP channel,
 * with certificate failures to ignore, with a custom client certificate (is the callback asked when the channel
 * opens?), and through WsCreateServiceProxyFromTemplate with the same property in the policy and the template.
 * Nothing is sent anywhere: the endpoint is a closed local port.  Prints results only. */
#include <windows.h>
#include <webservices.h>
#include <stdio.h>

/* not in mingw's header */
typedef struct
{
    WS_SECURITY_BINDING binding;
    WS_CERT_CREDENTIAL *localCertCredential;
} SSL_BINDING;

typedef struct
{
    WS_CERT_CREDENTIAL credential;
    HRESULT (CALLBACK *getCertCallback)(void *, const WS_ENDPOINT_ADDRESS *, const WS_STRING *, const void **,
                                        WS_ERROR *);
    void *getCertCallbackState;
    void *certIssuerListNotificationCallback;
    void *certIssuerListNotificationCallbackState;
} CUSTOM_CERT;

static int asked;

static HRESULT CALLBACK get_cert(void *state, const WS_ENDPOINT_ADDRESS *address, const WS_STRING *via,
                                 const void **cert, WS_ERROR *error)
{
    asked++;
    printf("    the certificate callback: address %.*ls, via %s\n", (int)address->url.length, address->url.chars,
           via ? "set" : "NULL");
    *cert = NULL;
    return E_FAIL;
}

static HRESULT channel_with(WS_CHANNEL_BINDING binding, WS_SECURITY_BINDING_PROPERTY *props, ULONG count,
                            WS_CERT_CREDENTIAL *cert, WS_CHANNEL **channel)
{
    /* the SDK numbers the binding types from 1; mingw from 0 */
    SSL_BINDING ssl = { { 1 /* WS_SSL_TRANSPORT_SECURITY_BINDING_TYPE */, props, count }, cert };
    WS_SECURITY_BINDING *bindings[] = { &ssl.binding };
    WS_SECURITY_DESCRIPTION desc = { bindings, 1, NULL, 0 };

    *channel = NULL;
    return WsCreateChannel(WS_CHANNEL_TYPE_REQUEST, binding, NULL, 0, &desc, channel, NULL);
}

int main(void)
{
    ULONG failures = 0x1f, disabled = TRUE;
    WS_SECURITY_BINDING_PROPERTY props[] =
    {
        { WS_SECURITY_BINDING_PROPERTY_CERT_FAILURES_TO_IGNORE, &failures, sizeof(failures) },
        { WS_SECURITY_BINDING_PROPERTY_DISABLE_CERT_REVOCATION_CHECK, &disabled, sizeof(disabled) },
    };
    WS_ENDPOINT_ADDRESS address = { { 0 } };
    CUSTOM_CERT custom = { { WS_CUSTOM_CERT_CREDENTIAL_TYPE }, get_cert };
    WS_CHANNEL *channel;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    address.url.chars = (WCHAR *)L"https://127.0.0.1:1/probe";
    address.url.length = wcslen(address.url.chars);

    hr = channel_with(WS_HTTP_CHANNEL_BINDING, props, 2, NULL, &channel);
    printf("an HTTP channel with failures to ignore and no revocation check: %#lx\n", hr);
    if (channel) WsFreeChannel(channel);
    hr = channel_with(WS_TCP_CHANNEL_BINDING, NULL, 0, NULL, &channel);
    printf("a TCP channel with an SSL binding: %#lx\n", hr);
    if (channel) WsFreeChannel(channel);
    hr = channel_with(WS_HTTP_CHANNEL_BINDING, NULL, 0, &custom.credential, &channel);
    printf("an HTTP channel with a custom certificate: %#lx\n", hr);
    if (channel)
    {
        hr = WsOpenChannel(channel, &address, NULL, NULL);
        printf("  WsOpenChannel: %#lx, the callback asked %d times\n", hr, asked);
        WsCloseChannel(channel, NULL, NULL);
        WsFreeChannel(channel);
    }

    {
        struct { WS_CHANNEL_PROPERTIES channel; WS_SECURITY_PROPERTIES security; WS_SECURITY_BINDING_PROPERTIES ssl; }
            policy = { { 0 } };
        WS_HTTP_SSL_BINDING_TEMPLATE templ = { { 0 } };
        WS_SERVICE_PROXY *proxy = NULL;

        policy.ssl.properties = props;
        policy.ssl.propertyCount = 1;
        hr = WsCreateServiceProxyFromTemplate(WS_CHANNEL_TYPE_REQUEST, NULL, 0, WS_HTTP_SSL_BINDING_TEMPLATE_TYPE,
                                              &templ, sizeof(templ), &policy, sizeof(policy), &proxy, NULL);
        printf("a template proxy with a binding property in the policy: %#lx\n", hr);
        if (proxy) WsFreeServiceProxy(proxy);
        templ.sslTransportSecurityBinding.securityBindingProperties.properties = props;
        templ.sslTransportSecurityBinding.securityBindingProperties.propertyCount = 1;
        proxy = NULL;
        hr = WsCreateServiceProxyFromTemplate(WS_CHANNEL_TYPE_REQUEST, NULL, 0, WS_HTTP_SSL_BINDING_TEMPLATE_TYPE,
                                              &templ, sizeof(templ), &policy, sizeof(policy), &proxy, NULL);
        printf("  and the same one in the template: %#lx\n", hr);
        if (proxy) WsFreeServiceProxy(proxy);
        policy.ssl.propertyCount = 0;
        templ.sslTransportSecurityBinding.localCertCredential = &custom.credential;
        asked = 0;
        proxy = NULL;
        hr = WsCreateServiceProxyFromTemplate(WS_CHANNEL_TYPE_REQUEST, NULL, 0, WS_HTTP_SSL_BINDING_TEMPLATE_TYPE,
                                              &templ, sizeof(templ), &policy, sizeof(policy), &proxy, NULL);
        printf("a template proxy with a custom certificate: %#lx\n", hr);
        if (proxy)
        {
            hr = WsOpenServiceProxy(proxy, &address, NULL, NULL);
            printf("  WsOpenServiceProxy: %#lx, the callback asked %d times\n", hr, asked);
            WsCloseServiceProxy(proxy, NULL, NULL);
            WsFreeServiceProxy(proxy);
        }
        {
            BOOL require = TRUE;
            WS_SECURITY_BINDING_PROPERTY req = { WS_SECURITY_BINDING_PROPERTY_REQUIRE_SSL_CLIENT_CERT, &require,
                                                 sizeof(require) };

            policy.ssl.properties = &req;
            policy.ssl.propertyCount = 1;
            templ.sslTransportSecurityBinding.securityBindingProperties.propertyCount = 0;
            asked = 0;
            proxy = NULL;
            hr = WsCreateServiceProxyFromTemplate(WS_CHANNEL_TYPE_REQUEST, NULL, 0, WS_HTTP_SSL_BINDING_TEMPLATE_TYPE,
                                                  &templ, sizeof(templ), &policy, sizeof(policy), &proxy, NULL);
            printf("  the policy requiring a client certificate: %#lx\n", hr);
            if (proxy)
            {
                hr = WsOpenServiceProxy(proxy, &address, NULL, NULL);
                printf("  WsOpenServiceProxy: %#lx, the callback asked %d times\n", hr, asked);
                WsCloseServiceProxy(proxy, NULL, NULL);
                WsFreeServiceProxy(proxy);
            }
        }
    }
    {
        WS_CHANNEL *ch2;
        WS_SERVICE_PROXY *proxy2 = NULL;
        SSL_BINDING ssl = { { 1, NULL, 0 }, &custom.credential };
        WS_SECURITY_BINDING *bindings[] = { &ssl.binding };
        WS_SECURITY_DESCRIPTION desc = { bindings, 1, NULL, 0 };

        asked = 0;
        hr = WsCreateServiceProxy(WS_CHANNEL_TYPE_REQUEST, WS_HTTP_CHANNEL_BINDING, &desc, NULL, 0, NULL, 0, &proxy2,
                                  NULL);
        printf("WsCreateServiceProxy with a custom certificate: %#lx\n", hr);
        if (proxy2)
        {
            hr = WsOpenServiceProxy(proxy2, &address, NULL, NULL);
            printf("  WsOpenServiceProxy: %#lx, the callback asked %d times\n", hr, asked);
            WsCloseServiceProxy(proxy2, NULL, NULL);
            WsFreeServiceProxy(proxy2);
        }
        (void)ch2;
    }
    return 0;
}
