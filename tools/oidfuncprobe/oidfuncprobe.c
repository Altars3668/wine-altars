/*
 * oidfuncprobe - what CryptDecodeObjectEx and CryptEncodeObjectEx do with a
 * structure only a DLL knows, and with one nothing knows.
 *
 * Word decodes SPC_INDIRECT_DATA_OBJID, which wintrust registers under the
 * CryptDllDecodeObject function set only; Wine found it there after printing
 * that it had no decoder.  This decodes and encodes it, and a made-up OID,
 * and prints the results and the last errors, one call at a time.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <windows.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <stdio.h>

int main(void)
{
    /* an SPC_INDIRECT_DATA_CONTENT: PE image data, sha256, a 32-byte digest */
    static const BYTE sha256[] = "2.16.840.1.101.3.4.2.1";
    SPC_INDIRECT_DATA_CONTENT content, *decoded;
    BYTE digest[32], *encoded = NULL, buf[512];
    DWORD size, error;
    BOOL ret;

    setvbuf(stdout, NULL, _IONBF, 0);
    LoadLibraryA("wintrust.dll");
    memset(&content, 0, sizeof(content));
    memset(digest, 0x5a, sizeof(digest));
    content.Data.pszObjId = (char *)SPC_PE_IMAGE_DATA_OBJID;
    content.DigestAlgorithm.pszObjId = (char *)sha256;
    content.Digest.cbData = sizeof(digest);
    content.Digest.pbData = digest;

    SetLastError(0xdeadbeef);
    ret = CryptEncodeObjectEx(X509_ASN_ENCODING, SPC_INDIRECT_DATA_OBJID, &content, CRYPT_ENCODE_ALLOC_FLAG, NULL,
                              &encoded, &size);
    error = GetLastError();
    printf("CryptEncodeObjectEx(SPC_INDIRECT_DATA) %d error %lu size %lu\n", ret, error, ret ? size : 0);
    if (!ret) return 1;

    SetLastError(0xdeadbeef);
    ret = CryptDecodeObjectEx(X509_ASN_ENCODING, SPC_INDIRECT_DATA_OBJID, encoded, size, CRYPT_DECODE_ALLOC_FLAG, NULL,
                              &decoded, &size);
    error = GetLastError();
    printf("CryptDecodeObjectEx(SPC_INDIRECT_DATA) %d error %lu", ret, error);
    if (ret) printf(": data %s, digest algorithm %s, digest %lu bytes", decoded->Data.pszObjId,
                    decoded->DigestAlgorithm.pszObjId, decoded->Digest.cbData);
    printf("\n");
    if (ret) LocalFree(decoded);

    SetLastError(0xdeadbeef);
    ret = CryptDecodeObjectEx(X509_ASN_ENCODING, "1.2.3.4.5.6.7.8.9", encoded, 30, 0, NULL, buf, &size);
    error = GetLastError();
    printf("CryptDecodeObjectEx(an OID nothing knows) %d error %#lx\n", ret, error);
    size = sizeof(buf);
    SetLastError(0xdeadbeef);
    ret = CryptEncodeObjectEx(X509_ASN_ENCODING, "1.2.3.4.5.6.7.8.9", &content, 0, NULL, buf, &size);
    error = GetLastError();
    printf("CryptEncodeObjectEx(an OID nothing knows) %d error %#lx\n", ret, error);
    size = sizeof(buf);
    SetLastError(0xdeadbeef);
    ret = CryptDecodeObject(X509_ASN_ENCODING, "1.2.3.4.5.6.7.8.9", encoded, 30, 0, buf, &size);
    error = GetLastError();
    printf("CryptDecodeObject(an OID nothing knows) %d error %#lx\n", ret, error);
    size = sizeof(buf);
    SetLastError(0xdeadbeef);
    ret = CryptEncodeObject(X509_ASN_ENCODING, "1.2.3.4.5.6.7.8.9", &content, buf, &size);
    error = GetLastError();
    printf("CryptEncodeObject(an OID nothing knows) %d error %#lx\n", ret, error);
    SetLastError(0xdeadbeef);
    ret = CryptDecodeObjectEx(X509_ASN_ENCODING, (LPCSTR)1999, encoded, 30, 0, NULL, buf, &size);
    error = GetLastError();
    printf("CryptDecodeObjectEx(numeric 1999) %d error %#lx\n", ret, error);
    LocalFree(encoded);
    printf("done\n");
    return 0;
}
