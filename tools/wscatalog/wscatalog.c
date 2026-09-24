/*
 * wscatalog - read Office's services catalog the way Office does, and print
 * what came out.
 *
 *   wscatalog catalog.xml
 *
 * Office fetches its catalog of connected services (OneDrive, SharePoint,
 * ...) from odc.officeapps.live.com/odc/servicemanager/catalog and reads it
 * with WWSAPI: WsReadToStartElement, WsReadStartElement, WsReadToStartElement,
 * then one WsReadType over the content of <ServicesCatalogResults> with the
 * type description below -- copied field by field out of
 * Mso98win32client.dll 16.0.20326.  Run the same file on Windows and under
 * Wine and diff the output: what Office would have in hand is exactly this.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -municode \
 *       -o wscatalog.exe wscatalog.c -lwebservices
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <windows.h>
#include <webservices.h>
#include <stdio.h>
#include <stddef.h>

#define XS(s) { sizeof(s) - 1, (BYTE *)(s), NULL, 0 }
static WS_XML_STRING ns = XS("urn:schemas-microsoft-com:office:office");
static WS_XML_STRING s_results = XS("ServicesCatalogResults"), s_webconnectinfo = XS("WebConnectInfo"),
    s_webconnect = XS("WebConnect"), s_catalog = XS("ServicesCatalog"), s_service = XS("Service"),
    s_connectmechanism = XS("ConnectMechanism"), s_height = XS("Height"), s_width = XS("Width"),
    s_id = XS("Id"), s_sortorder = XS("SortOrder"), s_capabilities = XS("Capabilities"),
    s_serviceowner = XS("ServiceOwner"), s_supportsmultiple = XS("SupportsMultiple"),
    s_ismanaged = XS("IsManaged"), s_isremovable = XS("IsRemovable"),
    s_supportsnonroaming = XS("SupportsNonRoaming"), s_authmechanism = XS("AuthMechanism"),
    s_allowsrefresh = XS("AllowsRefreshTokenAccess"), s_name = XS("Name"), s_longname = XS("LongName"),
    s_description = XS("Description"), s_serviceurl = XS("ServiceUrl"),
    s_capmeta = XS("CapabilitiesMetadata"), s_thumbnails = XS("Thumbnails"),
    s_thumbnailurl = XS("ThumbnailUrl"), s_size = XS("Size"), s_auth = XS("Auth"),
    s_bootstrapper = XS("BootStrapperUri"), s_authorization = XS("AuthorizationUri"),
    s_tokenissuance = XS("TokenIssuanceUri"), s_clientid = XS("ClientId"), s_scope = XS("Scope"),
    s_redirect = XS("RedirectUri"), s_policy = XS("Policy"), s_target = XS("Target"),
    s_trusteddomains = XS("TrustedDomains"), s_trusteddomain = XS("TrustedDomain"),
    s_accountlimit = XS("AccountLimit"), s_provider = XS("Provider");

struct text { WCHAR *value; };
struct thumb { LONGLONG size; WCHAR *url; };
struct auth { WCHAR *bootstrapper, *authorization, *tokenissuance, *clientid, *scope, *redirect, *policy, *target;
              ULONG domain_count; struct text *domains; };
struct limit { LONGLONG value; };
struct service
{
    WCHAR *id; LONGLONG sortorder, capabilities, connectmechanism, serviceowner;
    BOOL supportsmultiple, ismanaged, isremovable; LONGLONG height, width; BOOL supportsnonroaming;
    LONGLONG authmechanism; BOOL allowsrefresh;
    struct text *name, *longname, *description, *serviceurl, *capmeta;
    ULONG thumb_count; struct thumb *thumbs; struct auth *auth; struct limit *accountlimit; WCHAR *provider;
};
struct webconnect { LONGLONG connectmechanism, height, width; };
struct results { ULONG webconnect_count; struct webconnect *webconnects; ULONG service_count; struct service *services; };

#define TEXT_STRUCT(var, xname, type, tstruct) \
    static WS_FIELD_DESCRIPTION var##_text = { WS_TEXT_FIELD_MAPPING, NULL, NULL, type, NULL, 0 }; \
    static WS_FIELD_DESCRIPTION *var##_fields[] = { &var##_text }; \
    static WS_STRUCT_DESCRIPTION var = { sizeof(tstruct), 8, var##_fields, 1, &xname, &ns };
TEXT_STRUCT(name_desc, s_name, WS_WSZ_TYPE, struct text)
TEXT_STRUCT(longname_desc, s_longname, WS_WSZ_TYPE, struct text)
TEXT_STRUCT(description_desc, s_description, WS_WSZ_TYPE, struct text)
TEXT_STRUCT(serviceurl_desc, s_serviceurl, WS_WSZ_TYPE, struct text)
TEXT_STRUCT(capmeta_desc, s_capmeta, WS_WSZ_TYPE, struct text)
TEXT_STRUCT(trusteddomain_desc, s_trusteddomain, WS_WSZ_TYPE, struct text)
TEXT_STRUCT(accountlimit_desc, s_accountlimit, WS_INT64_TYPE, struct limit)

#define ATTR(var, xname, type, s, field, opts) \
    static WS_FIELD_DESCRIPTION var = { WS_ATTRIBUTE_FIELD_MAPPING, &xname, &ns, type, NULL, offsetof(s, field), opts };
#define ELEM(var, xname, type, desc, s, field, opts) \
    static WS_FIELD_DESCRIPTION var = { WS_ELEMENT_FIELD_MAPPING, &xname, &ns, type, desc, offsetof(s, field), opts };

ATTR(thumb_size, s_size, WS_INT64_TYPE, struct thumb, size, 0)
static WS_FIELD_DESCRIPTION thumb_text = { WS_TEXT_FIELD_MAPPING, NULL, NULL, WS_WSZ_TYPE, NULL, offsetof(struct thumb, url) };
static WS_FIELD_DESCRIPTION *thumb_fields[] = { &thumb_size, &thumb_text };
static WS_STRUCT_DESCRIPTION thumb_desc = { sizeof(struct thumb), 8, thumb_fields, 2, &s_thumbnailurl, &ns };

ELEM(auth_boot, s_bootstrapper, WS_WSZ_TYPE, NULL, struct auth, bootstrapper, WS_FIELD_OPTIONAL)
ELEM(auth_authz, s_authorization, WS_WSZ_TYPE, NULL, struct auth, authorization, WS_FIELD_OPTIONAL)
ELEM(auth_token, s_tokenissuance, WS_WSZ_TYPE, NULL, struct auth, tokenissuance, WS_FIELD_OPTIONAL)
ELEM(auth_client, s_clientid, WS_WSZ_TYPE, NULL, struct auth, clientid, WS_FIELD_OPTIONAL)
ELEM(auth_scope, s_scope, WS_WSZ_TYPE, NULL, struct auth, scope, WS_FIELD_OPTIONAL)
ELEM(auth_redirect, s_redirect, WS_WSZ_TYPE, NULL, struct auth, redirect, WS_FIELD_OPTIONAL)
ELEM(auth_policy, s_policy, WS_WSZ_TYPE, NULL, struct auth, policy, WS_FIELD_OPTIONAL)
ELEM(auth_target, s_target, WS_WSZ_TYPE, NULL, struct auth, target, WS_FIELD_OPTIONAL)
static WS_FIELD_DESCRIPTION auth_domains = { WS_REPEATING_ELEMENT_FIELD_MAPPING, &s_trusteddomains, &ns, WS_STRUCT_TYPE,
    &trusteddomain_desc, offsetof(struct auth, domains), WS_FIELD_OPTIONAL, NULL, offsetof(struct auth, domain_count),
    &s_trusteddomain, &ns };
static WS_FIELD_DESCRIPTION *auth_fields[] = { &auth_boot, &auth_authz, &auth_token, &auth_client, &auth_scope,
    &auth_redirect, &auth_policy, &auth_target, &auth_domains };
static WS_STRUCT_DESCRIPTION auth_desc = { sizeof(struct auth), 8, auth_fields, 9, &s_auth, &ns };

ATTR(svc_id, s_id, WS_WSZ_TYPE, struct service, id, 0)
ATTR(svc_sort, s_sortorder, WS_INT64_TYPE, struct service, sortorder, 0)
ATTR(svc_caps, s_capabilities, WS_INT64_TYPE, struct service, capabilities, 0)
ATTR(svc_connect, s_connectmechanism, WS_INT64_TYPE, struct service, connectmechanism, 0)
ATTR(svc_owner, s_serviceowner, WS_INT64_TYPE, struct service, serviceowner, 0)
ATTR(svc_multi, s_supportsmultiple, WS_BOOL_TYPE, struct service, supportsmultiple, WS_FIELD_OPTIONAL)
ATTR(svc_managed, s_ismanaged, WS_BOOL_TYPE, struct service, ismanaged, WS_FIELD_OPTIONAL)
ATTR(svc_removable, s_isremovable, WS_BOOL_TYPE, struct service, isremovable, WS_FIELD_OPTIONAL)
ATTR(svc_height, s_height, WS_INT64_TYPE, struct service, height, WS_FIELD_OPTIONAL)
ATTR(svc_width, s_width, WS_INT64_TYPE, struct service, width, WS_FIELD_OPTIONAL)
ATTR(svc_nonroaming, s_supportsnonroaming, WS_BOOL_TYPE, struct service, supportsnonroaming, WS_FIELD_OPTIONAL)
ATTR(svc_authmech, s_authmechanism, WS_INT64_TYPE, struct service, authmechanism, WS_FIELD_OPTIONAL)
ATTR(svc_refresh, s_allowsrefresh, WS_BOOL_TYPE, struct service, allowsrefresh, WS_FIELD_OPTIONAL)
ELEM(svc_name, s_name, WS_STRUCT_TYPE, &name_desc, struct service, name, WS_FIELD_POINTER)
ELEM(svc_longname, s_longname, WS_STRUCT_TYPE, &longname_desc, struct service, longname, WS_FIELD_POINTER | WS_FIELD_OPTIONAL)
ELEM(svc_description, s_description, WS_STRUCT_TYPE, &description_desc, struct service, description, WS_FIELD_POINTER)
ELEM(svc_url, s_serviceurl, WS_STRUCT_TYPE, &serviceurl_desc, struct service, serviceurl, WS_FIELD_POINTER | WS_FIELD_OPTIONAL)
ELEM(svc_capmeta, s_capmeta, WS_STRUCT_TYPE, &capmeta_desc, struct service, capmeta, WS_FIELD_POINTER | WS_FIELD_OPTIONAL)
static WS_ITEM_RANGE thumb_range = { 1, 0xffffffff };
static WS_FIELD_DESCRIPTION svc_thumbs = { WS_REPEATING_ELEMENT_FIELD_MAPPING, &s_thumbnails, &ns, WS_STRUCT_TYPE,
    &thumb_desc, offsetof(struct service, thumbs), WS_FIELD_OPTIONAL, NULL, offsetof(struct service, thumb_count),
    &s_thumbnailurl, &ns, &thumb_range };
ELEM(svc_auth, s_auth, WS_STRUCT_TYPE, &auth_desc, struct service, auth, WS_FIELD_POINTER | WS_FIELD_OPTIONAL)
ELEM(svc_limit, s_accountlimit, WS_STRUCT_TYPE, &accountlimit_desc, struct service, accountlimit, WS_FIELD_POINTER | WS_FIELD_OPTIONAL)
ELEM(svc_provider, s_provider, WS_WSZ_TYPE, NULL, struct service, provider, WS_FIELD_OPTIONAL)
static WS_FIELD_DESCRIPTION *service_fields[] = { &svc_id, &svc_sort, &svc_caps, &svc_connect, &svc_owner,
    &svc_multi, &svc_managed, &svc_removable, &svc_height, &svc_width, &svc_nonroaming, &svc_authmech,
    &svc_refresh, &svc_name, &svc_longname, &svc_description, &svc_url, &svc_capmeta, &svc_thumbs,
    &svc_auth, &svc_limit, &svc_provider };
static WS_STRUCT_DESCRIPTION service_desc = { sizeof(struct service), 8, service_fields, 22, &s_service, &ns };

ATTR(wc_connect, s_connectmechanism, WS_INT64_TYPE, struct webconnect, connectmechanism, 0)
ATTR(wc_height, s_height, WS_INT64_TYPE, struct webconnect, height, 0)
ATTR(wc_width, s_width, WS_INT64_TYPE, struct webconnect, width, 0)
static WS_FIELD_DESCRIPTION *webconnect_fields[] = { &wc_connect, &wc_height, &wc_width };
static WS_STRUCT_DESCRIPTION webconnect_desc = { sizeof(struct webconnect), 8, webconnect_fields, 3, &s_webconnect, &ns };

static WS_FIELD_DESCRIPTION res_webconnect = { WS_REPEATING_ELEMENT_FIELD_MAPPING, &s_webconnectinfo, &ns, WS_STRUCT_TYPE,
    &webconnect_desc, offsetof(struct results, webconnects), 0, NULL, offsetof(struct results, webconnect_count),
    &s_webconnect, &ns };
static WS_FIELD_DESCRIPTION res_services = { WS_REPEATING_ELEMENT_FIELD_MAPPING, &s_catalog, &ns, WS_STRUCT_TYPE,
    &service_desc, offsetof(struct results, services), 0, NULL, offsetof(struct results, service_count),
    &s_service, &ns };
static WS_FIELD_DESCRIPTION *results_fields[] = { &res_webconnect, &res_services };
static WS_STRUCT_DESCRIPTION results_desc = { sizeof(struct results), 8, results_fields, 2, &s_results, &ns };

static void u8(const WCHAR *s, char *out, int n)
{
    if (!s) { strcpy(out, "(null)"); return; }
    WideCharToMultiByte(CP_UTF8, 0, s, -1, out, n, NULL, NULL);
}

/* A missing value and an empty one are different answers: print them differently. */
static void quoted(const char *label, const WCHAR *s)
{
    char a[4096];
    if (!s) { printf("    %s (null)\n", label); return; }
    WideCharToMultiByte(CP_UTF8, 0, s, -1, a, sizeof(a), NULL, NULL);
    printf("    %s \"%s\"\n", label, a);
}

static void print_error(WS_ERROR *error)
{
    ULONG count = 0, i;
    WsGetErrorProperty(error, WS_ERROR_PROPERTY_STRING_COUNT, &count, sizeof(count));
    for (i = 0; i < count; i++)
    {
        WS_STRING str; char buf[1024];
        if (FAILED(WsGetErrorString(error, i, &str))) continue;
        WideCharToMultiByte(CP_UTF8, 0, str.chars, str.length, buf, sizeof(buf) - 1, NULL, NULL);
        buf[min(str.length * 3, sizeof(buf) - 1)] = 0;
        printf("  error[%lu]: %s\n", i, buf);
    }
}

int wmain(int argc, WCHAR **argv)
{
    WS_XML_READER_TEXT_ENCODING enc = {{ WS_XML_READER_ENCODING_TYPE_TEXT }, WS_CHARSET_AUTO};
    WS_XML_READER_BUFFER_INPUT input = {{ WS_XML_READER_INPUT_TYPE_BUFFER }};
    WS_XML_READER *reader; WS_HEAP *heap; WS_ERROR *error;
    struct results *res = NULL;
    char *data; DWORD size, got; HANDLE file; HRESULT hr; ULONG i, j;
    char a[4096];

    setvbuf(stdout, NULL, _IONBF, 0);
    file = CreateFileW(argc > 1 ? argv[1] : L"catalog.xml", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) { printf("cannot open input\n"); return 1; }
    size = GetFileSize(file, NULL); data = malloc(size);
    ReadFile(file, data, size, &got, NULL); CloseHandle(file);

    WsCreateError(NULL, 0, &error);
    WsCreateHeap(16 << 20, 0, NULL, 0, &heap, NULL);
    WsCreateReader(NULL, 0, &reader, NULL);
    input.encodedData = data; input.encodedDataSize = size;
    printf("WsSetInput %#lx\n", WsSetInput(reader, &enc.encoding, &input.input, NULL, 0, error));
    printf("WsFillReader %#lx\n", WsFillReader(reader, size, NULL, error));
    printf("WsReadToStartElement %#lx\n", WsReadToStartElement(reader, NULL, NULL, NULL, error));
    printf("WsReadStartElement %#lx\n", WsReadStartElement(reader, error));
    printf("WsReadToStartElement %#lx\n", WsReadToStartElement(reader, NULL, NULL, NULL, error));
    hr = WsReadType(reader, WS_ELEMENT_CONTENT_TYPE_MAPPING, WS_STRUCT_TYPE, &results_desc,
                    WS_READ_REQUIRED_POINTER, heap, &res, sizeof(res), error);
    printf("WsReadType %#lx\n", hr);
    if (FAILED(hr)) { print_error(error); return 2; }

    printf("webconnect %lu\n", res->webconnect_count);
    for (i = 0; i < res->webconnect_count; i++)
        printf("  mechanism %lld height %lld width %lld\n", res->webconnects[i].connectmechanism,
               res->webconnects[i].height, res->webconnects[i].width);
    printf("services %lu\n", res->service_count);
    for (i = 0; i < res->service_count; i++)
    {
        struct service *s = &res->services[i];
        u8(s->id, a, sizeof(a)); printf("service %s\n", a);
        printf("  sort %lld caps %lld connect %lld owner %lld multi %d managed %d removable %d height %lld width %lld nonroaming %d authmech %lld refresh %d\n",
               s->sortorder, s->capabilities, s->connectmechanism, s->serviceowner, s->supportsmultiple, s->ismanaged,
               s->isremovable, s->height, s->width, s->supportsnonroaming, s->authmechanism, s->allowsrefresh);
        u8(s->name ? s->name->value : NULL, a, sizeof(a)); printf("  name %s\n", a);
        u8(s->longname ? s->longname->value : NULL, a, sizeof(a)); printf("  longname %s\n", a);
        u8(s->description ? s->description->value : NULL, a, sizeof(a)); printf("  description %s\n", a);
        u8(s->serviceurl ? s->serviceurl->value : NULL, a, sizeof(a)); printf("  url %s\n", a);
        u8(s->capmeta ? s->capmeta->value : NULL, a, sizeof(a)); printf("  capmeta %.200s\n", a);
        printf("  thumbnails %lu\n", s->thumb_count);
        for (j = 0; j < s->thumb_count; j++) { u8(s->thumbs[j].url, a, sizeof(a)); printf("    %lld %s\n", s->thumbs[j].size, a); }
        if (s->auth)
        {
            u8(s->auth->clientid, a, sizeof(a)); printf("  auth client %s domains %lu\n", a, s->auth->domain_count);
            quoted("bootstrapper", s->auth->bootstrapper); quoted("authorization", s->auth->authorization);
            quoted("tokenissuance", s->auth->tokenissuance); quoted("scope", s->auth->scope);
            quoted("redirect", s->auth->redirect); quoted("policy", s->auth->policy);
            quoted("target", s->auth->target);
            for (j = 0; j < s->auth->domain_count; j++) quoted("domain", s->auth->domains[j].value);
        }
        if (s->accountlimit) printf("  accountlimit %lld\n", s->accountlimit->value);
        u8(s->provider, a, sizeof(a)); printf("  provider %s\n", a);
    }
    return 0;
}
