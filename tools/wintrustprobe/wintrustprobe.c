/* wintrustprobe: WinVerifyTrust on files, with what decides it for a signature whose certificate has expired:
 * the signing certificate's validity, and the time the signature was stamped, legacy or RFC 3161.
 * For each signer WinVerifyTrust keeps it prints the time it verified as of and its countersigners; for an RFC 3161
 * timestamp it prints what crypt32 makes of the token: CryptVerifyTimeStampSignature on the signer's encrypted hash
 * (and on wrong data, no data, and a cut token), and TIMESTAMP_INFO decoded on its own.
 * Usage: wintrustprobe <file>...   Prints results only. */
#include <windows.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <stdio.h>

#define ENCODING (X509_ASN_ENCODING | PKCS_7_ASN_ENCODING)

static void print_time( const char *what, const FILETIME *ft )
{
    SYSTEMTIME st;
    FileTimeToSystemTime( ft, &st );
    printf( "  %-26s %04u-%02u-%02u %02u:%02u:%02u.%03u\n", what, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
            st.wSecond, st.wMilliseconds );
}

static void print_name( const char *what, const CERT_CONTEXT *cert )
{
    WCHAR name[256];

    CertGetNameStringW( cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, NULL, name, ARRAYSIZE(name) );
    printf( "  %-26s %ls\n", what, name );
}

static void print_result( const char *what, BOOL ret )
{
    if (ret) printf( "  %-26s TRUE\n", what );
    else printf( "  %-26s FALSE, error %#lx\n", what, GetLastError() );
}

static void print_timestamp_info( const CRYPT_TIMESTAMP_INFO *info )
{
    printf( "    version %lu, policy %s, hash %s (parameters %lu bytes), imprint %lu bytes\n", info->dwVersion,
            info->pszTSAPolicyId ? info->pszTSAPolicyId : "(null)",
            info->HashAlgorithm.pszObjId ? info->HashAlgorithm.pszObjId : "(null)",
            info->HashAlgorithm.Parameters.cbData, info->HashedMessage.cbData );
    printf( "    serial %lu bytes, ordering %d, nonce %lu bytes, tsa %lu bytes, %lu extensions\n",
            info->SerialNumber.cbData, info->fOrdering, info->Nonce.cbData, info->Tsa.cbData, info->cExtension );
    if (info->pvAccuracy)
        printf( "    accuracy %lu s %lu ms %lu us\n", info->pvAccuracy->dwSeconds, info->pvAccuracy->dwMillis,
                info->pvAccuracy->dwMicros );
    else printf( "    no accuracy\n" );
    print_time( "  time", &info->ftTime );
}

/* the TSTInfo a token carries, as the content of its signed data */
static BYTE *token_content( const BYTE *token, DWORD size, DWORD *content_size )
{
    HCRYPTMSG msg;
    char type[64];
    DWORD type_size = sizeof(type);
    BYTE *content = NULL;

    if (!(msg = CryptMsgOpenToDecode( ENCODING, 0, 0, 0, NULL, NULL ))) return NULL;
    if (CryptMsgUpdate( msg, token, size, TRUE ))
    {
        if (CryptMsgGetParam( msg, CMSG_INNER_CONTENT_TYPE_PARAM, 0, type, &type_size ))
            printf( "  %-26s %s\n", "token content type", type );
        if (CryptMsgGetParam( msg, CMSG_CONTENT_PARAM, 0, NULL, content_size ) &&
            (content = malloc( *content_size )) &&
            !CryptMsgGetParam( msg, CMSG_CONTENT_PARAM, 0, content, content_size ))
        {
            free( content );
            content = NULL;
        }
    }
    CryptMsgClose( msg );
    return content;
}

static void show_decode( const BYTE *tst, DWORD tst_size )
{
    CRYPT_TIMESTAMP_INFO *info;
    DWORD size = 0;
    BOOL ret;

    printf( "  TIMESTAMP_INFO of the %lu-byte TSTInfo:\n", tst_size );
    ret = CryptDecodeObjectEx( ENCODING, TIMESTAMP_INFO, tst, tst_size, 0, NULL, NULL, &size );
    print_result( "size query", ret );
    if (ret) printf( "    %lu bytes\n", size );
    ret = CryptDecodeObjectEx( ENCODING, TIMESTAMP_INFO, tst, tst_size, CRYPT_DECODE_ALLOC_FLAG, NULL, &info, &size );
    print_result( "decode", ret );
    if (ret)
    {
        print_timestamp_info( info );
        LocalFree( info );
    }
    ret = CryptDecodeObjectEx( X509_ASN_ENCODING, TIMESTAMP_INFO, tst, tst_size, CRYPT_DECODE_ALLOC_FLAG, NULL,
                               &info, &size );
    print_result( "decode, X509 only", ret );
    if (ret) LocalFree( info );
    ret = CryptDecodeObjectEx( ENCODING, TIMESTAMP_INFO, tst, tst_size - 1, CRYPT_DECODE_ALLOC_FLAG, NULL, &info,
                               &size );
    print_result( "decode, one byte short", ret );
    if (ret) LocalFree( info );
}

static void show_verify( const char *what, const BYTE *token, DWORD token_size, const BYTE *data, DWORD data_size,
                         const BYTE *tst, DWORD tst_size, BOOL details )
{
    CRYPT_TIMESTAMP_CONTEXT *context = NULL;
    const CERT_CONTEXT *signer = NULL, *cert = NULL;
    HCERTSTORE store = NULL;
    unsigned int count = 0;
    BOOL ret;

    ret = CryptVerifyTimeStampSignature( token, token_size, data, data_size, NULL, &context, &signer, &store );
    print_result( what, ret );
    if (!ret || !details) goto done;
    printf( "    context %lu bytes, %s\n", context->cbEncoded,
            context->cbEncoded == token_size && !memcmp( context->pbEncoded, token, token_size ) ? "the token" :
            context->cbEncoded == tst_size && !memcmp( context->pbEncoded, tst, tst_size ) ? "the TSTInfo" :
            "something else" );
    print_timestamp_info( context->pTimeStamp );
    if (signer)
    {
        print_name( "  signer", signer );
        print_time( "  signer valid from", &signer->pCertInfo->NotBefore );
        print_time( "  signer valid to", &signer->pCertInfo->NotAfter );
    }
    else printf( "    no signer\n" );
    while ((cert = CertEnumCertificatesInStore( store, cert ))) count++;
    printf( "    store %s, %u certificates\n", store ? "returned" : "not returned", count );
done:
    if (context) CryptMemFree( context );
    if (signer) CertFreeCertificateContext( signer );
    if (store) CertCloseStore( store, 0 );
}

static void show_rfc3161( const CMSG_SIGNER_INFO *signer, const CRYPT_ATTRIBUTE *attr )
{
    const BYTE *token = attr->rgValue[0].pbData;
    DWORD token_size = attr->rgValue[0].cbData, tst_size = 0;
    BYTE *tst, *wrong;
    BOOL ret;

    printf( "  RFC 3161 token, %lu values, the first %lu bytes\n", attr->cValue, token_size );
    if (!(tst = token_content( token, token_size, &tst_size ))) printf( "  no content (%#lx)\n", GetLastError() );
    else show_decode( tst, tst_size );

    show_verify( "verify the encrypted hash", token, token_size, signer->EncryptedHash.pbData,
                 signer->EncryptedHash.cbData, tst, tst_size, TRUE );
    wrong = malloc( signer->EncryptedHash.cbData );
    memcpy( wrong, signer->EncryptedHash.pbData, signer->EncryptedHash.cbData );
    wrong[0] ^= 1;
    show_verify( "verify wrong data", token, token_size, wrong, signer->EncryptedHash.cbData, tst, tst_size, FALSE );
    free( wrong );
    show_verify( "verify no data", token, token_size, NULL, 0, tst, tst_size, FALSE );
    show_verify( "verify a cut token", token, token_size - 1, signer->EncryptedHash.pbData,
                 signer->EncryptedHash.cbData, tst, tst_size, FALSE );
    if (tst) show_verify( "verify the TSTInfo itself", tst, tst_size, signer->EncryptedHash.pbData,
                          signer->EncryptedHash.cbData, tst, tst_size, FALSE );
    {
        CRYPT_TIMESTAMP_CONTEXT *context = NULL;
        ret = CryptVerifyTimeStampSignature( token, token_size, signer->EncryptedHash.pbData,
                                             signer->EncryptedHash.cbData, NULL, &context, NULL, NULL );
        print_result( "verify, no signer or store", ret );
        if (context) CryptMemFree( context );
    }
    free( tst );
}

static void show_legacy( const CRYPT_ATTRIBUTE *attr )
{
    CMSG_CMS_SIGNER_INFO *counter;
    DWORD size, i, j;

    printf( "  legacy countersignature, %lu values\n", attr->cValue );
    if (!CryptDecodeObjectEx( ENCODING, CMS_SIGNER_INFO, attr->rgValue[0].pbData, attr->rgValue[0].cbData,
                              CRYPT_DECODE_ALLOC_FLAG, NULL, &counter, &size ))
    {
        printf( "    no signer info (%#lx)\n", GetLastError() );
        return;
    }
    for (i = 0; i < counter->AuthAttrs.cAttr; i++)
    {
        if (strcmp( counter->AuthAttrs.rgAttr[i].pszObjId, szOID_RSA_signingTime )) continue;
        for (j = 0; j < counter->AuthAttrs.rgAttr[i].cValue; j++)
        {
            FILETIME time;
            size = sizeof(time);
            if (CryptDecodeObjectEx( ENCODING, X509_CHOICE_OF_TIME, counter->AuthAttrs.rgAttr[i].rgValue[j].pbData,
                                     counter->AuthAttrs.rgAttr[i].rgValue[j].cbData, 0, NULL, &time, &size ))
                print_time( "  signing time", &time );
        }
    }
    LocalFree( counter );
}

static void show_signature( const WCHAR *file )
{
    HCERTSTORE store = NULL;
    HCRYPTMSG msg = NULL;
    DWORD size = 0, i;
    CMSG_SIGNER_INFO *signer;

    if (!CryptQueryObject( CERT_QUERY_OBJECT_FILE, file, CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
                           CERT_QUERY_FORMAT_FLAG_BINARY, 0, NULL, NULL, NULL, &store, &msg, NULL ))
    {
        printf( "  no embedded signature (%lu)\n", GetLastError() );
        return;
    }
    CryptMsgGetParam( msg, CMSG_SIGNER_INFO_PARAM, 0, NULL, &size );
    if ((signer = malloc( size )) && CryptMsgGetParam( msg, CMSG_SIGNER_INFO_PARAM, 0, signer, &size ))
    {
        CERT_INFO info = { 0 };
        const CERT_CONTEXT *cert;

        info.Issuer = signer->Issuer;
        info.SerialNumber = signer->SerialNumber;
        if ((cert = CertGetSubjectCertificateFromStore( store, ENCODING, &info )))
        {
            print_name( "signer", cert );
            print_time( "signer valid from", &cert->pCertInfo->NotBefore );
            print_time( "signer valid to", &cert->pCertInfo->NotAfter );
            CertFreeCertificateContext( cert );
        }
        for (i = 0; i < signer->UnauthAttrs.cAttr; i++)
        {
            const CRYPT_ATTRIBUTE *attr = &signer->UnauthAttrs.rgAttr[i];

            printf( "  unauthenticated attr       %s\n", attr->pszObjId );
            if (!strcmp( attr->pszObjId, szOID_RFC3161_counterSign ) && attr->cValue) show_rfc3161( signer, attr );
            else if (!strcmp( attr->pszObjId, szOID_RSA_counterSign ) && attr->cValue) show_legacy( attr );
        }
    }
    free( signer );
    CryptMsgClose( msg );
    CertCloseStore( store, 0 );
}

static void show_signer( const char *what, const CRYPT_PROVIDER_SGNR *sgnr )
{
    printf( "  %s: type %#lx, error %#lx, %lu certificates, chain status %#lx, signer info %s\n", what,
            sgnr->dwSignerType, sgnr->dwError, sgnr->csCertChain,
            sgnr->pChainContext ? sgnr->pChainContext->TrustStatus.dwErrorStatus : ~0u,
            sgnr->psSigner ? "present" : "absent" );
    print_time( "  verified as of", &sgnr->sftVerifyAsOf );
    if (sgnr->csCertChain && sgnr->pasCertChain[0].pCert) print_name( "  certificate", sgnr->pasCertChain[0].pCert );
}

int wmain( int argc, WCHAR **argv )
{
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;

    for (int i = 1; i < argc; i++)
    {
        WINTRUST_FILE_INFO file = { sizeof(file), argv[i] };
        WINTRUST_DATA data = { sizeof(data) };
        CRYPT_PROVIDER_DATA *prov;
        LONG ret;

        data.dwUIChoice = WTD_UI_NONE;
        data.fdwRevocationChecks = WTD_REVOKE_NONE;
        data.dwUnionChoice = WTD_CHOICE_FILE;
        data.pFile = &file;
        data.dwStateAction = WTD_STATEACTION_VERIFY;
        data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
        ret = WinVerifyTrust( INVALID_HANDLE_VALUE, &action, &data );
        printf( "%ls\n  WinVerifyTrust             %#lx\n", argv[i], ret );
        if ((prov = WTHelperProvDataFromStateData( data.hWVTStateData )))
        {
            DWORD s, c;

            print_time( "system time", &prov->sftSystemTime );
            printf( "  %lu signers\n", prov->csSigners );
            for (s = 0; s < prov->csSigners; s++)
            {
                show_signer( "signer", &prov->pasSigners[s] );
                for (c = 0; c < prov->pasSigners[s].csCounterSigners; c++)
                    show_signer( "countersigner", &prov->pasSigners[s].pasCounterSigners[c] );
            }
        }
        data.dwStateAction = WTD_STATEACTION_CLOSE;
        WinVerifyTrust( INVALID_HANDLE_VALUE, &action, &data );
        show_signature( argv[i] );
    }
    return 0;
}
