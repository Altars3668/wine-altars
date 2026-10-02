/* tstinfoprobe: what CryptDecodeObjectEx makes of RFC 3161 TSTInfo structures (TIMESTAMP_INFO), handcrafted to
 * have each optional part on its own -- accuracy in its forms, ordering, a nonce, a TSA name, extensions -- and
 * times with no fraction of a second and with one to six digits of it; and whether TIMESTAMP_INFO encodes, and how a
 * TIMESTAMP_REQUEST and a TIMESTAMP_RESPONSE do.  tstinfo-vectors.h is generated; see the README.
 * Usage: tstinfoprobe   Prints results only. */
#include <windows.h>
#include <wincrypt.h>
#include <stdio.h>
#include "tstinfo-vectors.h"

#define ENCODING (X509_ASN_ENCODING | PKCS_7_ASN_ENCODING)

static void print_hex( const char *what, const BYTE *data, DWORD size )
{
    DWORD i;

    printf( "    %s %lu bytes", what, size );
    for (i = 0; i < size && i < 16; i++) printf( "%s%02x", i ? "" : " ", data[i] );
    printf( "%s\n", size > 16 ? "..." : "" );
}

static void print_info( const CRYPT_TIMESTAMP_INFO *info )
{
    SYSTEMTIME st;
    ULARGE_INTEGER t;
    DWORD i;

    printf( "    version %lu, policy %s, hash %s, parameters %lu bytes\n", info->dwVersion,
            info->pszTSAPolicyId ? info->pszTSAPolicyId : "(null)",
            info->HashAlgorithm.pszObjId ? info->HashAlgorithm.pszObjId : "(null)",
            info->HashAlgorithm.Parameters.cbData );
    print_hex( "imprint", info->HashedMessage.pbData, info->HashedMessage.cbData );
    print_hex( "serial", info->SerialNumber.pbData, info->SerialNumber.cbData );
    FileTimeToSystemTime( &info->ftTime, &st );
    t.LowPart = info->ftTime.dwLowDateTime;
    t.HighPart = info->ftTime.dwHighDateTime;
    printf( "    time %04u-%02u-%02u %02u:%02u:%02u.%03u, %llu hundred nanoseconds past the second\n", st.wYear,
            st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, t.QuadPart % 10000000 );
    if (info->pvAccuracy)
        printf( "    accuracy %lu s %lu ms %lu us\n", info->pvAccuracy->dwSeconds, info->pvAccuracy->dwMillis,
                info->pvAccuracy->dwMicros );
    else printf( "    no accuracy\n" );
    printf( "    ordering %d\n", info->fOrdering );
    print_hex( "nonce", info->Nonce.pbData, info->Nonce.cbData );
    print_hex( "tsa", info->Tsa.pbData, info->Tsa.cbData );
    printf( "    %lu extensions\n", info->cExtension );
    for (i = 0; i < info->cExtension; i++)
    {
        printf( "      %s critical %d", info->rgExtension[i].pszObjId, info->rgExtension[i].fCritical );
        print_hex( "value", info->rgExtension[i].Value.pbData, info->rgExtension[i].Value.cbData );
    }
}

int main( void )
{
    unsigned int i;

    setvbuf( stdout, NULL, _IONBF, 0 );
    for (i = 0; i < ARRAYSIZE(tst_vectors); i++)
    {
        CRYPT_TIMESTAMP_INFO *info;
        DWORD size = 0, decoded;
        BYTE *encoded;
        BOOL ret;

        printf( "%s, %lu bytes\n", tst_vectors[i].name, tst_vectors[i].size );
        SetLastError( 0xdeadbeef );
        ret = CryptDecodeObjectEx( ENCODING, TIMESTAMP_INFO, tst_vectors[i].data, tst_vectors[i].size, 0, NULL,
                                   NULL, &size );
        if (!ret)
        {
            printf( "  size query FALSE, error %#lx\n", GetLastError() );
            continue;
        }
        printf( "  size %lu\n", size );
        ret = CryptDecodeObjectEx( ENCODING, TIMESTAMP_INFO, tst_vectors[i].data, tst_vectors[i].size,
                                   CRYPT_DECODE_ALLOC_FLAG, NULL, &info, &size );
        if (!ret)
        {
            printf( "  decode FALSE, error %#lx\n", GetLastError() );
            continue;
        }
        print_info( info );
        decoded = size;
        if (!i)
        {
            SetLastError( 0xdeadbeef );
            ret = CryptEncodeObjectEx( ENCODING, TIMESTAMP_INFO, info, CRYPT_ENCODE_ALLOC_FLAG, NULL, &encoded, &size );
            if (ret)
            {
                printf( "  encodes, %lu bytes, %s\n", size, size == tst_vectors[i].size &&
                        !memcmp( encoded, tst_vectors[i].data, size ) ? "the same" : "different" );
                LocalFree( encoded );
            }
            else printf( "  encode FALSE, error %#lx\n", GetLastError() );
            size = decoded;
            SetLastError( 0xdeadbeef );
            ret = CryptDecodeObjectEx( ENCODING, TIMESTAMP_INFO, tst_vectors[i].data, tst_vectors[i].size, 0, NULL,
                                       info, &size );
            printf( "  decode into the buffer, %lu bytes: %d, error %#lx, size %lu\n", decoded, ret,
                    ret ? 0 : GetLastError(), size );
            size = 100;
            SetLastError( 0xdeadbeef );
            ret = CryptDecodeObjectEx( ENCODING, TIMESTAMP_INFO, tst_vectors[i].data, tst_vectors[i].size, 0, NULL,
                                       info, &size );
            printf( "  decode into 100 bytes: %d, error %#lx, size %lu\n", ret, ret ? 0 : GetLastError(), size );
        }
        LocalFree( info );
    }

    {
        CRYPT_TIMESTAMP_REQUEST request = { 0 };
        static BYTE hash[32] = { 0x11 }, nonce[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        BYTE *encoded;
        DWORD size, j;

        request.dwVersion = TIMESTAMP_VERSION;
        request.HashAlgorithm.pszObjId = (char *)szOID_NIST_sha256;
        request.HashedMessage.cbData = sizeof(hash);
        request.HashedMessage.pbData = hash;
        request.Nonce.cbData = sizeof(nonce);
        request.Nonce.pbData = nonce;
        request.fCertReq = TRUE;
        SetLastError( 0xdeadbeef );
        if (CryptEncodeObjectEx( ENCODING, TIMESTAMP_REQUEST, &request, CRYPT_ENCODE_ALLOC_FLAG, NULL, &encoded, &size ))
        {
            printf( "TIMESTAMP_REQUEST encodes, %lu bytes:", size );
            for (j = 0; j < size; j++) printf( " %02x", encoded[j] );
            printf( "\n" );
            LocalFree( encoded );
        }
        else printf( "TIMESTAMP_REQUEST encode FALSE, error %#lx\n", GetLastError() );
        request.pszTSAPolicyId = (char *)"1.2.3.4";
        request.fCertReq = FALSE;
        if (CryptEncodeObjectEx( ENCODING, TIMESTAMP_REQUEST, &request, CRYPT_ENCODE_ALLOC_FLAG, NULL, &encoded, &size ))
        {
            printf( "TIMESTAMP_REQUEST with a policy, no certificates, encodes, %lu bytes:", size );
            for (j = 0; j < size; j++) printf( " %02x", encoded[j] );
            printf( "\n" );
            LocalFree( encoded );
        }
        else printf( "TIMESTAMP_REQUEST with a policy encode FALSE, error %#lx\n", GetLastError() );
    }
    {
        /* PKIStatusInfo rejected (2) with the text "no" and failure info badAlg; no token */
        static const BYTE rejected[] = { 0x30, 0x10, 0x30, 0x0e, 0x02, 0x01, 0x02, 0x30, 0x04, 0x0c, 0x02, 0x6e,
                                         0x6f, 0x03, 0x03, 0x07, 0x80, 0x00 };
        CRYPT_TIMESTAMP_RESPONSE *response;
        DWORD size;

        SetLastError( 0xdeadbeef );
        if (CryptDecodeObjectEx( ENCODING, TIMESTAMP_RESPONSE, rejected, sizeof(rejected), CRYPT_DECODE_ALLOC_FLAG,
                                 NULL, &response, &size ))
        {
            printf( "TIMESTAMP_RESPONSE decodes, %lu bytes: status %lu, %lu texts, failure info %lu bytes, "
                    "content %lu bytes\n", size, response->dwStatus, response->cFreeText,
                    response->FailureInfo.cbData, response->ContentInfo.cbData );
            if (response->cFreeText) printf( "  text %ls\n", response->rgFreeText[0] );
            if (response->FailureInfo.cbData)
                printf( "  failure info %02x, %lu unused bits\n", response->FailureInfo.pbData[0],
                        response->FailureInfo.cUnusedBits );
            LocalFree( response );
        }
        else printf( "TIMESTAMP_RESPONSE decode FALSE, error %#lx\n", GetLastError() );
    }
    return 0;
}
