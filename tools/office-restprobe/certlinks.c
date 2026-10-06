#include "probe.h"
#include <wincrypt.h>

/* Wine 的公开证书测试样本；不读取任何系统或个人证书存储。 */
static const BYTE fixture[] = {
0x30,0x7a,0x02,0x01,0x01,0x30,0x02,0x06,0x00,0x30,0x15,0x31,0x13,0x30,0x11,0x06,
0x03,0x55,0x04,0x03,0x13,0x0a,0x4a,0x75,0x61,0x6e,0x20,0x4c,0x61,0x6e,0x67,0x00,
0x30,0x22,0x18,0x0f,0x31,0x36,0x30,0x31,0x30,0x31,0x30,0x31,0x30,0x30,0x30,0x30,
0x30,0x30,0x5a,0x18,0x0f,0x31,0x36,0x30,0x31,0x30,0x31,0x30,0x31,0x30,0x30,0x30,
0x30,0x30,0x30,0x5a,0x30,0x15,0x31,0x13,0x30,0x11,0x06,0x03,0x55,0x04,0x03,0x13,
0x0a,0x4a,0x75,0x61,0x6e,0x20,0x4c,0x61,0x6e,0x67,0x00,0x30,0x07,0x30,0x02,0x06,
0x00,0x03,0x01,0x00,0xa3,0x16,0x30,0x14,0x30,0x12,0x06,0x03,0x55,0x1d,0x13,0x01,
0x01,0xff,0x04,0x08,0x30,0x06,0x01,0x01,0xff,0x02,0x01,0x01 };

static void property(PCCERT_CONTEXT cert, const char *label, DWORD id)
{
    WCHAR buffer[32] = {0};
    DWORD size = sizeof(buffer);
    BOOL ret = CertGetCertificateContextProperty(cert, id, buffer, &size);
    printf("%s prop=%lu ok=%u size=%lu alpha=%u beta=%u gamma=%u\n", label, id, ret, size,
           ret && !wcscmp(buffer, L"alpha"), ret && !wcscmp(buffer, L"beta"), ret && !wcscmp(buffer, L"gamma"));
}

int main(void)
{
    HCERTSTORE store;
    PCCERT_CONTEXT original, incoming, old, linked;
    CRYPT_DATA_BLOB data;
    DWORD disposition, error;
    BOOL ret;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (!probe_start()) return 1;
    for (disposition = 1; disposition <= 7; ++disposition)
    {
        store = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, CERT_STORE_CREATE_NEW_FLAG, NULL);
        original = CertCreateCertificateContext(X509_ASN_ENCODING, fixture, sizeof(fixture));
        incoming = CertCreateCertificateContext(X509_ASN_ENCODING, fixture, sizeof(fixture));
        if (!store || !original || !incoming) return probe_done(1);
        data.pbData = (BYTE *)L"alpha";
        data.cbData = sizeof(L"alpha");
        if (!CertSetCertificateContextProperty(original, CERT_FRIENDLY_NAME_PROP_ID, 0, &data) ||
            !CertAddCertificateLinkToStore(store, original, CERT_STORE_ADD_ALWAYS, &old)) return probe_done(1);
        data.pbData = (BYTE *)L"beta";
        data.cbData = sizeof(L"beta");
        CertSetCertificateContextProperty(incoming, CERT_DESCRIPTION_PROP_ID, 0, &data);
        linked = NULL;
        SetLastError(PROBE_SENTINEL);
        ret = CertAddCertificateLinkToStore(store, incoming, disposition, &linked);
        error = GetLastError();
        printf("case=%lu add=%u error=%#lx linked=%u data_shared=%u old_context=%u\n", disposition, ret, error,
               !!linked, linked && linked->pCertInfo == incoming->pCertInfo, linked == old);
        if (ret)
        {
            property(linked, "linked", CERT_FRIENDLY_NAME_PROP_ID);
            property(incoming, "incoming", CERT_FRIENDLY_NAME_PROP_ID);
            property(linked, "linked", CERT_DESCRIPTION_PROP_ID);
            data.pbData = (BYTE *)L"gamma";
            data.cbData = sizeof(L"gamma");
            CertSetCertificateContextProperty(linked, CERT_FRIENDLY_NAME_PROP_ID, 0, &data);
            property(incoming, "incoming_after_link_write", CERT_FRIENDLY_NAME_PROP_ID);
            CertFreeCertificateContext(linked);
        }
        CertFreeCertificateContext(old);
        CertCloseStore(store, 0);
        CertFreeCertificateContext(original);
        CertFreeCertificateContext(incoming);
    }
    return probe_done(0);
}
