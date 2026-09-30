/* How CopyFileEx and CopyFile2 report progress, and what each answer from the callback does.
 *
 * Wine's test covers one case: the first call is CALLBACK_STREAM_SWITCH, and PROGRESS_CANCEL fails the
 * copy with ERROR_REQUEST_ABORTED and deletes the copy when it can.  This prints every callback -- reason,
 * sizes, how much was transferred -- for an empty file, a 100 000-byte one and a 3 000 000-byte one, and
 * what happens to the copy when the callback answers PROGRESS_STOP, PROGRESS_QUIET or PROGRESS_CANCEL on
 * the first chunk, or sets the cancel flag.  Then the same through CopyFile2's messages.  Files go in a
 * new directory under %TEMP%, removed at the end.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror copyprogressprobe.c -o copyprogressprobe.exe
 */
#define _WIN32_WINNT 0x0602
#include <windows.h>
#include <stdio.h>

static WCHAR dir[MAX_PATH], src[MAX_PATH], dst[MAX_PATH];
static DWORD answer_at_chunk, answer, calls;
static BOOL cancel_flag, set_cancel;

static DWORD CALLBACK progress(LARGE_INTEGER total, LARGE_INTEGER transferred, LARGE_INTEGER stream_size,
                               LARGE_INTEGER stream_transferred, DWORD stream, DWORD reason, HANDLE source,
                               HANDLE dest, void *param)
{
    (void)source; (void)dest; (void)param;
    calls++;
    printf("  call %lu: reason %lu stream %lu total %lld/%lld stream %lld/%lld\n", calls, reason, stream,
           (long long)transferred.QuadPart, (long long)total.QuadPart, (long long)stream_transferred.QuadPart, (long long)stream_size.QuadPart);
    if (reason == CALLBACK_CHUNK_FINISHED && calls - 1 == answer_at_chunk)
    {
        if (set_cancel) cancel_flag = TRUE;
        else return answer;
    }
    return PROGRESS_CONTINUE;
}

static COPYFILE2_MESSAGE_ACTION CALLBACK progress2(const COPYFILE2_MESSAGE *msg, void *param)
{
    (void)param;
    calls++;
    switch (msg->Type)
    {
    case COPYFILE2_CALLBACK_STREAM_STARTED:
        printf("  call %lu: stream %lu started, size %llu of %llu\n", calls, msg->Info.StreamStarted.dwStreamNumber,
               (unsigned long long)msg->Info.StreamStarted.uliStreamSize.QuadPart, (unsigned long long)msg->Info.StreamStarted.uliTotalFileSize.QuadPart);
        break;
    case COPYFILE2_CALLBACK_CHUNK_STARTED:
        printf("  call %lu: chunk %llu started, size %llu\n", calls, (unsigned long long)msg->Info.ChunkStarted.uliChunkNumber.QuadPart,
               (unsigned long long)msg->Info.ChunkStarted.uliChunkSize.QuadPart);
        break;
    case COPYFILE2_CALLBACK_CHUNK_FINISHED:
        printf("  call %lu: chunk %llu finished, size %llu, flags %#lx, transferred %llu of %llu\n", calls,
               (unsigned long long)msg->Info.ChunkFinished.uliChunkNumber.QuadPart, (unsigned long long)msg->Info.ChunkFinished.uliChunkSize.QuadPart,
               msg->Info.ChunkFinished.dwFlags, (unsigned long long)msg->Info.ChunkFinished.uliTotalBytesTransferred.QuadPart,
               (unsigned long long)msg->Info.ChunkFinished.uliTotalFileSize.QuadPart);
        break;
    case COPYFILE2_CALLBACK_STREAM_FINISHED:
        printf("  call %lu: stream finished, transferred %llu of %llu\n", calls,
               (unsigned long long)msg->Info.StreamFinished.uliTotalBytesTransferred.QuadPart, (unsigned long long)msg->Info.StreamFinished.uliTotalFileSize.QuadPart);
        break;
    default:
        printf("  call %lu: message %u\n", calls, msg->Type);
        break;
    }
    return COPYFILE2_PROGRESS_CONTINUE;
}

static void make_source(DWORD size)
{
    HANDLE file = CreateFileW(src, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    static char block[4096];
    DWORD written;

    while (size)
    {
        DWORD len = size < sizeof(block) ? size : sizeof(block);
        WriteFile(file, block, len, &written, NULL);
        size -= len;
    }
    CloseHandle(file);
}

static void report(const char *what, BOOL ret, DWORD error)
{
    WIN32_FILE_ATTRIBUTE_DATA data;

    if (GetFileAttributesExW(dst, GetFileExInfoStandard, &data))
        printf("%s: %s %lu, %lu calls, copy of %lu bytes left\n", what, ret ? "ok" : "failed", ret ? 0 : error, calls,
               data.nFileSizeLow);
    else
        printf("%s: %s %lu, %lu calls, no copy left\n", what, ret ? "ok" : "failed", ret ? 0 : error, calls);
    DeleteFileW(dst);
}

int main(void)
{
    static const DWORD sizes[] = {0, 100000, 3000000};
    static const struct { DWORD answer; BOOL cancel_flag; const char *name; } answers[] =
    {
        {PROGRESS_STOP, FALSE, "PROGRESS_STOP"},
        {PROGRESS_QUIET, FALSE, "PROGRESS_QUIET"},
        {PROGRESS_CANCEL, FALSE, "PROGRESS_CANCEL"},
        {PROGRESS_CONTINUE, TRUE, "cancel flag"},
    };
    COPYFILE2_EXTENDED_PARAMETERS params = {sizeof(params), 0, NULL, NULL, NULL};
    WCHAR temp[MAX_PATH];
    unsigned int i;
    char what[64];
    HRESULT hr;
    BOOL ret;

    GetTempPathW(MAX_PATH, temp);
    swprintf(dir, MAX_PATH, L"%lscopyprogress-%lu", temp, GetCurrentProcessId());
    CreateDirectoryW(dir, NULL);
    swprintf(src, MAX_PATH, L"%ls\\source.bin", dir);
    swprintf(dst, MAX_PATH, L"%ls\\copy.bin", dir);

    answer_at_chunk = ~0u;
    for (i = 0; i < ARRAYSIZE(sizes); i++)
    {
        make_source(sizes[i]);
        calls = 0;
        printf("CopyFileEx of %lu bytes\n", sizes[i]);
        SetLastError(0xdeadbeef);
        ret = CopyFileExW(src, dst, progress, NULL, NULL, 0);
        sprintf(what, "CopyFileEx of %lu bytes", sizes[i]);
        report(what, ret, GetLastError());
    }

    make_source(3000000);
    for (i = 0; i < ARRAYSIZE(answers); i++)
    {
        answer_at_chunk = 1;
        answer = answers[i].answer;
        set_cancel = answers[i].cancel_flag;
        cancel_flag = FALSE;
        calls = 0;
        printf("CopyFileEx answering %s at the first chunk\n", answers[i].name);
        SetLastError(0xdeadbeef);
        ret = CopyFileExW(src, dst, progress, NULL, &cancel_flag, 0);
        report(answers[i].name, ret, GetLastError());
    }

    params.pProgressRoutine = progress2;
    for (i = 0; i < ARRAYSIZE(sizes); i++)
    {
        make_source(sizes[i]);
        calls = 0;
        printf("CopyFile2 of %lu bytes\n", sizes[i]);
        hr = CopyFile2(src, dst, &params);
        sprintf(what, "CopyFile2 of %lu bytes", sizes[i]);
        report(what, SUCCEEDED(hr), (DWORD)hr);
    }

    DeleteFileW(src);
    RemoveDirectoryW(dir);
    return 0;
}
