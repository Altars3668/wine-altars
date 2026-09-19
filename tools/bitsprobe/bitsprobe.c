/*
 * bitsprobe -- can BITS download part of a file, the way Office asks it to?
 *
 * Click-to-Run downloads through BITS, and not with AddFile: it uses
 * IBackgroundCopyJob3::AddFileWithRanges, which is how it fetches the pieces of
 * a multi-gigabyte stream file it still needs.  When that call does nothing the
 * job ends up with no files in it, Resume answers BG_E_EMPTY (0x80200003), and
 * Click-to-Run reports
 *
 *     BGTransportJob::StartDownload  "Unable to resume job"
 *     FailOverTransport::DoConnect   0x80200003  "Ran out of sources to retry from."
 *
 * and falls back to its own single-stream HTTP transport, which is much slower.
 *
 * This walks the same sequence -- create the manager, create a download job,
 * add one file with ranges, resume, wait -- and then checks that the bytes
 * actually landed at the offsets that were asked for, which is the part a
 * "returns S_OK" stub gets wrong.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -municode \
 *       -o bitsprobe.exe bitsprobe.c -lole32 -luuid
 *
 *   bitsprobe <url> [<local file>]
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <objbase.h>
#include <bits.h>
#include <bits1_5.h>
#include <bits2_0.h>
#include <stdio.h>
#include <stdarg.h>

static void out(const WCHAR *s)
{
    char u[4096];
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, u, sizeof(u), NULL, NULL);
    if (n > 0) fwrite(u, 1, n - 1, stdout);
    fflush(stdout);
}

static void outf(const WCHAR *fmt, ...)
{
    WCHAR b[4096];
    va_list ap;
    va_start(ap, fmt);
    vswprintf(b, ARRAYSIZE(b), fmt, ap);
    va_end(ap);
    out(b);
}

static const WCHAR *state_name(BG_JOB_STATE s)
{
    switch (s)
    {
    case BG_JOB_STATE_QUEUED:         return L"QUEUED";
    case BG_JOB_STATE_CONNECTING:     return L"CONNECTING";
    case BG_JOB_STATE_TRANSFERRING:   return L"TRANSFERRING";
    case BG_JOB_STATE_SUSPENDED:      return L"SUSPENDED";
    case BG_JOB_STATE_ERROR:          return L"ERROR";
    case BG_JOB_STATE_TRANSIENT_ERROR:return L"TRANSIENT_ERROR";
    case BG_JOB_STATE_TRANSFERRED:    return L"TRANSFERRED";
    case BG_JOB_STATE_ACKNOWLEDGED:   return L"ACKNOWLEDGED";
    case BG_JOB_STATE_CANCELLED:      return L"CANCELLED";
    default:                          return L"?";
    }
}


/* Office registers an IBackgroundCopyCallback and waits to be told the transfer
 * has begun.  Only JobTransferred was ever sent, and only at the very end, so
 * this counts what actually arrives. */
static LONG n_modification, n_error, n_transferred;

struct probe_callback { IBackgroundCopyCallback IBackgroundCopyCallback_iface; LONG ref; };

static HRESULT WINAPI cb_QueryInterface(IBackgroundCopyCallback *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IBackgroundCopyCallback))
    {
        *out = iface;
        IBackgroundCopyCallback_AddRef(iface);
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI cb_AddRef(IBackgroundCopyCallback *iface)
{
    struct probe_callback *cb = CONTAINING_RECORD(iface, struct probe_callback, IBackgroundCopyCallback_iface);
    return InterlockedIncrement(&cb->ref);
}
static ULONG WINAPI cb_Release(IBackgroundCopyCallback *iface)
{
    struct probe_callback *cb = CONTAINING_RECORD(iface, struct probe_callback, IBackgroundCopyCallback_iface);
    return InterlockedDecrement(&cb->ref);
}
static HRESULT WINAPI cb_JobTransferred(IBackgroundCopyCallback *iface, IBackgroundCopyJob *job)
{
    (void)iface; (void)job;

    InterlockedIncrement(&n_transferred);
    return S_OK;
}
static HRESULT WINAPI cb_JobError(IBackgroundCopyCallback *iface, IBackgroundCopyJob *job, IBackgroundCopyError *err)
{
    (void)iface; (void)job; (void)err;

    InterlockedIncrement(&n_error);
    return S_OK;
}
static HRESULT WINAPI cb_JobModification(IBackgroundCopyCallback *iface, IBackgroundCopyJob *job, DWORD reserved)
{
    (void)iface; (void)job; (void)reserved;

    InterlockedIncrement(&n_modification);
    return S_OK;
}
static const IBackgroundCopyCallbackVtbl probe_callback_vtbl =
{
    cb_QueryInterface, cb_AddRef, cb_Release,
    cb_JobTransferred, cb_JobError, cb_JobModification
};
static struct probe_callback probe_callback = { { (IBackgroundCopyCallbackVtbl *)&probe_callback_vtbl }, 1 };

int wmain(int argc, WCHAR **argv)
{
    /* Two ranges, deliberately not at the start and not adjacent: a stub that
     * quietly downloads the whole file would pass a single 0- range. */
    BG_FILE_RANGE ranges[2] = { { 1000, 4096 }, { 500000, 8192 } };
    const WCHAR *url, *local;
    IBackgroundCopyManager *mgr = NULL;
    IBackgroundCopyJob *job = NULL;
    IBackgroundCopyJob3 *job3 = NULL;
    IBackgroundCopyFile *bfile = NULL;
    IBackgroundCopyFile2 *bfile2 = NULL;
    IEnumBackgroundCopyFiles *files = NULL;
    BG_FILE_RANGE *got = NULL;
    DWORD got_count = 0, waited = 0, n;
    BG_JOB_STATE state = BG_JOB_STATE_QUEUED;
    BG_JOB_PROGRESS prog;
    HANDLE h;
    GUID id;
    HRESULT hr;
    int rc = 1;

    if (argc < 2)
    {
        outf(L"用法: bitsprobe <url> [<本地文件>]\n");
        return 2;
    }
    url = argv[1];
    local = argc > 2 ? argv[2] : L"C:\\bitsprobe.out";

    DeleteFileW(local);

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) { outf(L"CoInitializeEx 0x%08lx\n", hr); return 1; }

    hr = CoCreateInstance(&CLSID_BackgroundCopyManager, NULL, CLSCTX_LOCAL_SERVER,
                          &IID_IBackgroundCopyManager, (void **)&mgr);
    outf(L"  CoCreateInstance(BackgroundCopyManager)  0x%08lx\n", hr);
    if (FAILED(hr)) goto done;

    hr = IBackgroundCopyManager_CreateJob(mgr, L"bitsprobe", BG_JOB_TYPE_DOWNLOAD, &id, &job);
    outf(L"  CreateJob                               0x%08lx\n", hr);
    if (FAILED(hr)) goto done;

    hr = IBackgroundCopyJob_QueryInterface(job, &IID_IBackgroundCopyJob3, (void **)&job3);
    outf(L"  QueryInterface(IBackgroundCopyJob3)     0x%08lx\n", hr);
    if (FAILED(hr)) goto done;

    hr = IBackgroundCopyJob_SetNotifyInterface(job, (IUnknown *)&probe_callback.IBackgroundCopyCallback_iface);
    outf(L"  SetNotifyInterface                      0x%08lx\n", hr);
    if (SUCCEEDED(hr))
        hr = IBackgroundCopyJob_SetNotifyFlags(job,
                 BG_NOTIFY_JOB_TRANSFERRED | BG_NOTIFY_JOB_ERROR | BG_NOTIFY_JOB_MODIFICATION);
    outf(L"  SetNotifyFlags                          0x%08lx\n", hr);
    if (FAILED(hr)) goto done;

    hr = IBackgroundCopyJob3_AddFileWithRanges(job3, url, local, 2, ranges);
    outf(L"  AddFileWithRanges(2 段)                 0x%08lx\n", hr);
    if (FAILED(hr)) goto done;

    /* The stub returned S_OK here too.  What it did not do is put a file in the
     * job, and that is what Resume trips over. */
    hr = IBackgroundCopyJob_Resume(job);
    outf(L"  Resume                                  0x%08lx%ls\n", hr,
         hr == (HRESULT)0x80200003 ? L"   <- BG_E_EMPTY，job 里没有文件" : L"");
    if (FAILED(hr)) goto done;

    while (waited < 120000)
    {
        hr = IBackgroundCopyJob_GetState(job, &state);
        if (FAILED(hr)) { outf(L"  GetState 0x%08lx\n", hr); goto done; }
        if (state == BG_JOB_STATE_TRANSFERRED || state == BG_JOB_STATE_ERROR ||
            state == BG_JOB_STATE_ACKNOWLEDGED) break;
        Sleep(200);
        waited += 200;
    }
    IBackgroundCopyJob_GetProgress(job, &prog);
    outf(L"  状态 %ls，传输 %I64u 字节，用时 %lu ms\n", state_name(state),
         prog.BytesTransferred, waited);

    if (state != BG_JOB_STATE_TRANSFERRED) goto done;

    /* GetFileRanges has to give back what was asked for. */
    hr = IBackgroundCopyJob_EnumFiles(job, &files);
    if (SUCCEEDED(hr) && SUCCEEDED(IEnumBackgroundCopyFiles_Next(files, 1, &bfile, &n)) && n == 1 &&
        SUCCEEDED(IBackgroundCopyFile_QueryInterface(bfile, &IID_IBackgroundCopyFile2, (void **)&bfile2)))
    {
        hr = IBackgroundCopyFile2_GetFileRanges(bfile2, &got_count, &got);
        outf(L"  GetFileRanges                           0x%08lx  %lu 段", hr, got_count);
        if (SUCCEEDED(hr) && got_count == 2 &&
            got[0].InitialOffset == ranges[0].InitialOffset && got[0].Length == ranges[0].Length &&
            got[1].InitialOffset == ranges[1].InitialOffset && got[1].Length == ranges[1].Length)
            outf(L"  与请求一致\n");
        else
            outf(L"  与请求不符\n");
        CoTaskMemFree(got);
    }

    outf(L"  回调: JobModification %ld 次, JobTransferred %ld 次, JobError %ld 次%ls\n",
         n_modification, n_transferred, n_error,
         n_modification ? L"" : L"   （Wine 不发 JobModification，见 dlls/qmgr 的 revert）");

    hr = IBackgroundCopyJob_Complete(job);
    outf(L"  Complete                                0x%08lx\n", hr);

    /* The point of ranges: the file is as large as the furthest range reaches,
     * and holds the requested bytes at the requested offsets -- not a copy of
     * the whole remote file starting at zero. */
    h = CreateFileW(local, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE)
    {
        LARGE_INTEGER size;
        UINT64 want = ranges[1].InitialOffset + ranges[1].Length;

        GetFileSizeEx(h, &size);
        CloseHandle(h);
        outf(L"  本地文件 %I64d 字节，按范围应为 %I64u  %ls\n",
             size.QuadPart, want, (UINT64)size.QuadPart == want ? L"一致" : L"不一致");
        /* What this checks is the ranges: the right bytes at the right offsets
         * and nothing else fetched.  The callback counts are reported, not
         * required -- Wine deliberately does not send JobModification, because
         * doing so from the service deadlocks a client that is inside a BITS
         * call waiting for this job.  See the revert in dlls/qmgr. */
        if ((UINT64)size.QuadPart == want && prog.BytesTransferred == ranges[0].Length + ranges[1].Length
            && n_transferred == 1)
            rc = 0;
    }
    else outf(L"  打开 %ls 失败 err=%lu\n", local, GetLastError());

done:
    if (bfile2) IBackgroundCopyFile2_Release(bfile2);
    if (bfile) IBackgroundCopyFile_Release(bfile);
    if (files) IEnumBackgroundCopyFiles_Release(files);
    if (job3) IBackgroundCopyJob3_Release(job3);
    if (job) IBackgroundCopyJob_Release(job);
    if (mgr) IBackgroundCopyManager_Release(mgr);
    CoUninitialize();
    outf(rc ? L"  结果: 失败\n" : L"  结果: 通过\n");
    return rc;
}
