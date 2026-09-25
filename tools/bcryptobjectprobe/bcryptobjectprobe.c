/*
 * bcryptobjectprobe - what bcrypt does with the object buffer a caller
 * hands BCryptCreateHash, BCryptDuplicateHash and BCryptGenerateSymmetricKey.
 *
 * Office hands one to every hash it makes, and Wine printed "ignoring object
 * buffer" 250 times as Word started.  This asks for each algorithm's
 * BCRYPT_OBJECT_LENGTH, then makes the object in a buffer of that size, one
 * byte short, and of none, and says where the handle points.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>

static const char *where(void *handle, BYTE *object, ULONG len)
{
    if (!handle) return "no handle";
    if ((BYTE *)handle >= object && (BYTE *)handle < object + len) return "the handle is in the buffer";
    return "the handle is elsewhere";
}

static void hash(const WCHAR *name, ULONG flags)
{
    BCRYPT_ALG_HANDLE alg;
    BCRYPT_HASH_HANDLE h, copy;
    ULONG length = 0, size;
    BYTE object[2048], out[64];
    NTSTATUS status;

    status = BCryptOpenAlgorithmProvider(&alg, name, NULL, flags);
    if (status) { printf("%ls: open %#lx\n", name, status); return; }
    status = BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (UCHAR *)&length, sizeof(length), &size, 0);
    printf("%ls%s: object length %#lx %lu\n", name, flags ? " (hmac)" : "", status, length);

    h = NULL;
    memset(object, 0xcc, sizeof(object));
    status = BCryptCreateHash(alg, &h, object, length, flags ? (UCHAR *)"key" : NULL, flags ? 3 : 0, 0);
    printf("  create in %lu bytes: %#lx, %s, buffer %s\n", length, status, where(h, object, length),
           object[0] == 0xcc && object[length - 1] == 0xcc ? "untouched" : "written");
    if (!status)
    {
        BCryptHashData(h, (UCHAR *)"abc", 3, 0);
        copy = NULL;
        status = BCryptDuplicateHash(h, &copy, object + length, length - 1, 0);
        printf("  duplicate into %lu bytes: %#lx\n", length - 1, status);
        if (!status) BCryptDestroyHash(copy);
        copy = NULL;
        status = BCryptDuplicateHash(h, &copy, object + length, length, 0);
        printf("  duplicate into %lu bytes: %#lx, %s\n", length, status, where(copy, object + length, length));
        if (!status) BCryptDestroyHash(copy);
        copy = NULL;
        status = BCryptDuplicateHash(h, &copy, object + length, 0, 0);
        printf("  duplicate into a buffer of no length: %#lx\n", status);
        if (!status) BCryptDestroyHash(copy);
        copy = NULL;
        status = BCryptDuplicateHash(h, &copy, NULL, length, 0);
        printf("  duplicate into no buffer but a length: %#lx\n", status);
        if (!status) BCryptDestroyHash(copy);
        status = BCryptFinishHash(h, out, 32, 0);
        printf("  finish %#lx\n", status);
        BCryptDestroyHash(h);
    }
    h = NULL;
    status = BCryptCreateHash(alg, &h, object, length - 1, flags ? (UCHAR *)"key" : NULL, flags ? 3 : 0, 0);
    printf("  create in %lu bytes: %#lx\n", length - 1, status);
    if (!status) BCryptDestroyHash(h);
    h = NULL;
    status = BCryptCreateHash(alg, &h, NULL, 0, flags ? (UCHAR *)"key" : NULL, flags ? 3 : 0, 0);
    printf("  create with no buffer: %#lx\n", status);
    if (!status) BCryptDestroyHash(h);
    h = NULL;
    status = BCryptCreateHash(alg, &h, NULL, length, flags ? (UCHAR *)"key" : NULL, flags ? 3 : 0, 0);
    printf("  create with no buffer but a length: %#lx\n", status);
    if (!status) BCryptDestroyHash(h);
    h = NULL;
    status = BCryptCreateHash(alg, &h, object, 0, flags ? (UCHAR *)"key" : NULL, flags ? 3 : 0, 0);
    printf("  create with a buffer of no length: %#lx\n", status);
    if (!status) BCryptDestroyHash(h);
    for (size = length; size; size--)
    {
        h = NULL;
        status = BCryptCreateHash(alg, &h, object, size - 1, flags ? (UCHAR *)"key" : NULL, flags ? 3 : 0, 0);
        if (status) break;
        BCryptDestroyHash(h);
    }
    printf("  the smallest buffer it takes: %lu bytes, then %#lx\n", size, status);
    if (!flags)
    {
        h = NULL;
        status = BCryptCreateHash(alg, &h, NULL, 0, (UCHAR *)"key", 3, 0);
        printf("  a secret for a hash that is no HMAC: %#lx\n", status);
        if (!status) BCryptDestroyHash(h);
        h = NULL;
        status = BCryptCreateHash(alg, &h, NULL, 0, (UCHAR *)"key", 0, 0);
        printf("  a secret of no length for a hash that is no HMAC: %#lx\n", status);
        if (!status) BCryptDestroyHash(h);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
}

static void key(void)
{
    static UCHAR secret[16] = {1, 2, 3};
    BCRYPT_ALG_HANDLE alg;
    BCRYPT_KEY_HANDLE k;
    ULONG length = 0, size;
    BYTE object[2048];
    NTSTATUS status;

    BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, NULL, 0);
    status = BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, (UCHAR *)&length, sizeof(length), &size, 0);
    printf("AES: object length %#lx %lu\n", status, length);
    k = NULL;
    status = BCryptGenerateSymmetricKey(alg, &k, object, length, secret, sizeof(secret), 0);
    printf("  key in %lu bytes: %#lx, %s\n", length, status, where(k, object, length));
    if (!status) BCryptDestroyKey(k);
    k = NULL;
    status = BCryptGenerateSymmetricKey(alg, &k, object, length - 1, secret, sizeof(secret), 0);
    printf("  key in %lu bytes: %#lx\n", length - 1, status);
    if (!status) BCryptDestroyKey(k);
    k = NULL;
    status = BCryptGenerateSymmetricKey(alg, &k, NULL, 0, secret, sizeof(secret), 0);
    printf("  key with no buffer: %#lx\n", status);
    if (!status) BCryptDestroyKey(k);
    k = NULL;
    status = BCryptGenerateSymmetricKey(alg, &k, NULL, length, secret, sizeof(secret), 0);
    printf("  key with no buffer but a length: %#lx\n", status);
    if (!status) BCryptDestroyKey(k);
    k = NULL;
    status = BCryptGenerateSymmetricKey(alg, &k, object, 0, secret, sizeof(secret), 0);
    printf("  key with a buffer of no length: %#lx\n", status);
    if (!status) BCryptDestroyKey(k);
    for (size = length; size; size--)
    {
        k = NULL;
        status = BCryptGenerateSymmetricKey(alg, &k, object, size - 1, secret, sizeof(secret), 0);
        if (status) break;
        BCryptDestroyKey(k);
    }
    printf("  the smallest buffer it takes: %lu bytes, then %#lx\n", size, status);

    k = NULL;
    status = BCryptGenerateSymmetricKey(alg, &k, NULL, 0, secret, sizeof(secret), 0);
    if (!status)
    {
        BCRYPT_KEY_HANDLE copy;
        struct { BCRYPT_KEY_DATA_BLOB_HEADER hdr; UCHAR key[16]; } blob;

        copy = NULL;
        status = BCryptDuplicateKey(k, &copy, object, 0, 0);
        printf("  duplicate a key into a buffer of no length: %#lx\n", status);
        if (!status) BCryptDestroyKey(copy);
        copy = NULL;
        status = BCryptDuplicateKey(k, &copy, NULL, length, 0);
        printf("  duplicate a key into no buffer but a length: %#lx\n", status);
        if (!status) BCryptDestroyKey(copy);
        copy = NULL;
        status = BCryptDuplicateKey(k, &copy, object, length, 0);
        printf("  duplicate a key into %lu bytes: %#lx, %s\n", length, status, where(copy, object, length));
        if (!status) BCryptDestroyKey(copy);

        blob.hdr.dwMagic = BCRYPT_KEY_DATA_BLOB_MAGIC;
        blob.hdr.dwVersion = BCRYPT_KEY_DATA_BLOB_VERSION1;
        blob.hdr.cbKeyData = sizeof(blob.key);
        memcpy(blob.key, secret, sizeof(blob.key));
        copy = NULL;
        status = BCryptImportKey(alg, NULL, BCRYPT_KEY_DATA_BLOB, &copy, object, 0, (UCHAR *)&blob, sizeof(blob), 0);
        printf("  import a key into a buffer of no length: %#lx\n", status);
        if (!status) BCryptDestroyKey(copy);
        copy = NULL;
        status = BCryptImportKey(alg, NULL, BCRYPT_KEY_DATA_BLOB, &copy, NULL, length, (UCHAR *)&blob, sizeof(blob), 0);
        printf("  import a key into no buffer but a length: %#lx\n", status);
        if (!status) BCryptDestroyKey(copy);
        copy = NULL;
        status = BCryptImportKey(alg, NULL, BCRYPT_KEY_DATA_BLOB, &copy, object, length, (UCHAR *)&blob, sizeof(blob), 0);
        printf("  import a key into %lu bytes: %#lx, %s\n", length, status, where(copy, object, length));
        if (!status) BCryptDestroyKey(copy);
        BCryptDestroyKey(k);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    hash(BCRYPT_SHA1_ALGORITHM, 0);
    hash(BCRYPT_SHA256_ALGORITHM, 0);
    hash(BCRYPT_SHA256_ALGORITHM, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    hash(BCRYPT_SHA512_ALGORITHM, 0);
    hash(BCRYPT_MD5_ALGORITHM, 0);
    key();
    printf("done\n");
    return 0;
}
