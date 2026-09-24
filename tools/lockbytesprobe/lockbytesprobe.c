/*
 * lockbytesprobe - which ILockBytes methods StgOpenStorageOnILockBytes calls
 * when it opens a compound file and when it refuses one that is not.
 *
 * Word probes every document it opens this way.  Office's cloud file cache
 * hands it a stream that commits itself on Flush, after which the stream is
 * gone; an unexpected Flush there leaves Word holding a dead stream.  Run on
 * Windows and under Wine and diff the output.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -o lockbytesprobe.exe \
 *       lockbytesprobe.c -lole32 -luuid
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#include <windows.h>
#include <objbase.h>
#include <stdio.h>

struct lockbytes
{
    ILockBytes ILockBytes_iface;
    LONG ref;
    BYTE *data;
    ULONG size, alloc;
    BOOL log;
};

static struct lockbytes *impl(ILockBytes *iface) { return CONTAINING_RECORD(iface, struct lockbytes, ILockBytes_iface); }

static HRESULT WINAPI lb_QueryInterface(ILockBytes *iface, REFIID riid, void **out)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_ILockBytes))
    {
        *out = iface; ILockBytes_AddRef(iface); return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI lb_AddRef(ILockBytes *iface) { return InterlockedIncrement(&impl(iface)->ref); }
static ULONG WINAPI lb_Release(ILockBytes *iface) { return InterlockedDecrement(&impl(iface)->ref); }

static HRESULT WINAPI lb_ReadAt(ILockBytes *iface, ULARGE_INTEGER off, void *buf, ULONG cb, ULONG *read)
{
    struct lockbytes *lb = impl(iface);
    ULONG n = off.QuadPart >= lb->size ? 0 : min(cb, lb->size - (ULONG)off.QuadPart);
    if (lb->log) printf("    ReadAt(%llu, %lu)\n", off.QuadPart, cb);
    memcpy(buf, lb->data + off.QuadPart, n);
    if (read) *read = n;
    return S_OK;
}

static HRESULT WINAPI lb_WriteAt(ILockBytes *iface, ULARGE_INTEGER off, const void *buf, ULONG cb, ULONG *written)
{
    struct lockbytes *lb = impl(iface);
    if (lb->log) printf("    WriteAt(%llu, %lu)\n", off.QuadPart, cb);
    if (off.QuadPart + cb > lb->alloc)
    {
        lb->alloc = (ULONG)(off.QuadPart + cb) * 2;
        lb->data = realloc(lb->data, lb->alloc);
    }
    if (off.QuadPart + cb > lb->size)
    {
        memset(lb->data + lb->size, 0, (ULONG)off.QuadPart + cb - lb->size);
        lb->size = (ULONG)off.QuadPart + cb;
    }
    memcpy(lb->data + off.QuadPart, buf, cb);
    if (written) *written = cb;
    return S_OK;
}

static HRESULT WINAPI lb_Flush(ILockBytes *iface)
{
    if (impl(iface)->log) printf("    Flush()\n");
    return S_OK;
}

static HRESULT WINAPI lb_SetSize(ILockBytes *iface, ULARGE_INTEGER size)
{
    struct lockbytes *lb = impl(iface);
    if (lb->log) printf("    SetSize(%llu)\n", size.QuadPart);
    if (size.QuadPart > lb->alloc) { lb->alloc = (ULONG)size.QuadPart; lb->data = realloc(lb->data, lb->alloc); }
    if (size.QuadPart > lb->size) memset(lb->data + lb->size, 0, (ULONG)size.QuadPart - lb->size);
    lb->size = (ULONG)size.QuadPart;
    return S_OK;
}

static HRESULT WINAPI lb_LockRegion(ILockBytes *iface, ULARGE_INTEGER off, ULARGE_INTEGER cb, DWORD type)
{
    if (impl(iface)->log) printf("    LockRegion(%llu, %llu, %lu)\n", off.QuadPart, cb.QuadPart, type);
    return STG_E_INVALIDFUNCTION;
}

static HRESULT WINAPI lb_UnlockRegion(ILockBytes *iface, ULARGE_INTEGER off, ULARGE_INTEGER cb, DWORD type)
{
    if (impl(iface)->log) printf("    UnlockRegion(%llu, %llu, %lu)\n", off.QuadPart, cb.QuadPart, type);
    return STG_E_INVALIDFUNCTION;
}

static HRESULT WINAPI lb_Stat(ILockBytes *iface, STATSTG *st, DWORD flag)
{
    struct lockbytes *lb = impl(iface);
    if (lb->log) printf("    Stat(%lu)\n", flag);
    memset(st, 0, sizeof(*st));
    st->type = STGTY_LOCKBYTES;
    st->cbSize.QuadPart = lb->size;
    return S_OK;
}

static const ILockBytesVtbl lb_vtbl =
{
    lb_QueryInterface, lb_AddRef, lb_Release, lb_ReadAt, lb_WriteAt, lb_Flush, lb_SetSize,
    lb_LockRegion, lb_UnlockRegion, lb_Stat,
};

static void init_lb(struct lockbytes *lb, const BYTE *data, ULONG size)
{
    lb->ILockBytes_iface.lpVtbl = (ILockBytesVtbl *)&lb_vtbl;
    lb->ref = 1;
    lb->alloc = max(size, 1);
    lb->data = malloc(lb->alloc);
    memcpy(lb->data, data, size);
    lb->size = size;
    lb->log = TRUE;
}

static const struct { DWORD mode; const char *name; } modes[] =
{
    { STGM_READ | STGM_SHARE_DENY_WRITE, "READ|SHARE_DENY_WRITE" },
    { STGM_READ | STGM_SHARE_EXCLUSIVE, "READ|SHARE_EXCLUSIVE" },
    { STGM_READ | STGM_SHARE_DENY_NONE | STGM_TRANSACTED, "READ|SHARE_DENY_NONE|TRANSACTED" },
    { STGM_READ | STGM_SHARE_DENY_WRITE | STGM_TRANSACTED, "READ|SHARE_DENY_WRITE|TRANSACTED" },
    { STGM_READWRITE | STGM_SHARE_EXCLUSIVE, "READWRITE|SHARE_EXCLUSIVE" },
    { STGM_READWRITE | STGM_SHARE_EXCLUSIVE | STGM_TRANSACTED, "READWRITE|SHARE_EXCLUSIVE|TRANSACTED" },
};

static void open_case(const char *what, const BYTE *data, ULONG size)
{
    unsigned int i;
    for (i = 0; i < ARRAYSIZE(modes); i++)
    {
        struct lockbytes lb;
        IStorage *stg = NULL;
        HRESULT hr;

        init_lb(&lb, data, size);
        printf("== %s, %s\n", what, modes[i].name);
        hr = StgOpenStorageOnILockBytes(&lb.ILockBytes_iface, NULL, modes[i].mode, NULL, 0, &stg);
        printf("  StgOpenStorageOnILockBytes -> %#lx\n", hr);
        if (stg)
        {
            printf("  Release:\n");
            IStorage_Release(stg);
        }
        printf("  lockbytes refcount left %ld\n", lb.ref);
        free(lb.data);
    }
}

int main(void)
{
    BYTE zip[4096] = { 'P', 'K', 3, 4 };
    struct lockbytes made;
    IStorage *stg;
    IStream *stm;
    HRESULT hr;

    setvbuf(stdout, NULL, _IONBF, 0);
    CoInitialize(NULL);

    printf("== StgIsStorageILockBytes on a zip\n");
    {
        struct lockbytes lb; init_lb(&lb, zip, sizeof(zip));
        printf("  -> %#lx\n", StgIsStorageILockBytes(&lb.ILockBytes_iface));
        free(lb.data);
    }

    open_case("zip", zip, sizeof(zip));

    /* A real compound file to open. */
    init_lb(&made, NULL, 0);
    made.log = FALSE;
    hr = StgCreateDocfileOnILockBytes(&made.ILockBytes_iface, STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, &stg);
    printf("== StgCreateDocfileOnILockBytes -> %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        IStorage_CreateStream(stg, L"s", STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE, 0, 0, &stm);
        IStream_Write(stm, "data", 4, NULL);
        IStream_Release(stm);
        IStorage_Commit(stg, STGC_DEFAULT);
        IStorage_Release(stg);
        printf("  compound file of %lu bytes\n", made.size);
        open_case("compound file", made.data, made.size);
    }

    CoUninitialize();
    return 0;
}
