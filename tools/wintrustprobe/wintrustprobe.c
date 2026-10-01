/* wintrustprobe: WinVerifyTrust on files, with what decides it for a signature whose certificate has expired:
 * the signing certificate's validity, and the time the signature was stamped, legacy or RFC 3161.
 * Usage: wintrustprobe <file>...   Prints results only. */
#include <windows.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#include <stdio.h>

static void print_time( const char *what, const FILETIME *ft )
{
    SYSTEMTIME st;
    FileTimeToSystemTime( ft, &st );
    printf( "  %-22s %04u-%02u-%02u %02u:%02u:%02u\n", what, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond );
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
        if ((cert = CertGetSubjectCertificateFromStore( store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, &info )))
        {
            print_time( "signer valid from", &cert->pCertInfo->NotBefore );
            print_time( "signer valid to", &cert->pCertInfo->NotAfter );
            CertFreeCertificateContext( cert );
        }
        for (i = 0; i < signer->UnauthAttrs.cAttr; i++)
            printf( "  unauthenticated attr   %s\n", signer->UnauthAttrs.rgAttr[i].pszObjId );
    }
    free( signer );
    CryptMsgClose( msg );
    CertCloseStore( store, 0 );
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
        printf( "%ls\n  WinVerifyTrust         %#lx\n", argv[i], ret );
        if ((prov = WTHelperProvDataFromStateData( data.hWVTStateData )) && prov->csSigners)
            print_time( "verified as of", &prov->pasSigners[0].sftVerifyAsOf );
        data.dwStateAction = WTD_STATEACTION_CLOSE;
        WinVerifyTrust( INVALID_HANDLE_VALUE, &action, &data );
        show_signature( argv[i] );
    }
    return 0;
}
