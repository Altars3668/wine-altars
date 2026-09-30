/* Whether a thread holds DLL synchronization, as PrivIsDllSynchronizationHeld answers it, and in
 * which order the loader calls a program's TLS callbacks and a DLL's entry point.
 *
 * AuxUlib (aux_ulib.lib, linked into Word, PowerPoint and 14 other Office binaries) takes
 * GetModuleHandleW(L"api-ms-win-core-libraryloader-l1-1-0.dll"), looks the function up there and
 * calls it as BOOL (WINAPI *)(BOOL *held); where it is missing, AuxUlib compares the owner of
 * PEB->LoaderLock with the calling thread itself.  This prints which modules export the function
 * and, in each of these states, what it returns, what it stores, the last error, what AuxUlib's own
 * check says and the TEB's SameTebFlags (0x1000 LoadOwner, 0x2000 LoaderWorker):
 *
 *   - this program's TLS callback at process attach, thread attach, thread detach and process detach
 *   - the entry point of dllsyncprobe_dll.dll (next to this program; it only calls back in here)
 *     when LoadLibrary loads it, at thread attach and detach, and at process exit
 *   - a DLL notification callback while version.dll loads and unloads
 *   - the main thread with the loader lock not held, held once, held twice, and let go of again
 *   - another thread while the main thread holds the loader lock
 *
 * The states are listed in the order they were reached; the two lines from process exit are written
 * as they happen, after everything else.
 *
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror dllsyncprobe.c -o dllsyncprobe.exe
 *   x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror -shared dllsyncprobe_dll.c -o dllsyncprobe_dll.dll
 */
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <string.h>
#include <intrin.h>

enum state { TLS_PROCESS_ATTACH, TLS_THREAD_ATTACH, TLS_THREAD_DETACH, DLLMAIN_PROCESS_ATTACH,
             DLLMAIN_THREAD_ATTACH, DLLMAIN_THREAD_DETACH, NOTIFY_LOADED, NOTIFY_UNLOADED, MAIN_FREE,
             MAIN_LOCKED, MAIN_LOCKED_TWICE, MAIN_LOCKED_ONCE_AGAIN, MAIN_UNLOCKED, OTHER_THREAD,
             STATE_COUNT };

static const char *state_names[STATE_COUNT] =
{
    "TLS callback, process attach", "TLS callback, thread attach", "TLS callback, thread detach",
    "DllMain, process attach (LoadLibrary)", "DllMain, thread attach", "DllMain, thread detach",
    "DLL notification, loaded", "DLL notification, unloaded", "main thread, loader lock not held",
    "main thread, loader lock held", "main thread, loader lock held twice", "main thread, held once again",
    "main thread, loader lock let go of", "other thread, main thread holds the loader lock",
};

struct answer { BOOL ret, held; DWORD error; BOOL aux; USHORT flags; };

static struct answer answers[STATE_COUNT];
static enum state order[STATE_COUNT];
static unsigned int order_count;
static BOOL (WINAPI *is_held)(BOOL *);
static BOOL resolved;
static DWORD other_thread_id;

static NTSTATUS (WINAPI *pLdrLockLoaderLock)(ULONG, ULONG *, ULONG_PTR *);
static NTSTATUS (WINAPI *pLdrUnlockLoaderLock)(ULONG, ULONG_PTR);

/* what AuxUlib does without the function: PEB+0x110 is LoaderLock, +0x10 in it OwningThread */
static BOOL aux_check(void)
{
    BYTE *peb = (BYTE *)__readgsqword(0x60);
    RTL_CRITICAL_SECTION *lock = *(RTL_CRITICAL_SECTION **)(peb + 0x110);

    return (DWORD_PTR)lock->OwningThread == GetCurrentThreadId();
}

/* once, from the first callback: later from threads that must not touch the loader */
static void resolve(void)
{
    if (resolved) return;
    resolved = TRUE;
    is_held = (void *)GetProcAddress(GetModuleHandleW(L"api-ms-win-core-libraryloader-l1-1-0.dll"),
                                     "PrivIsDllSynchronizationHeld");
}

static void measure(struct answer *a)
{
    resolve();
    a->held = 0xcc;
    SetLastError(0xdeadbeef);
    a->ret = is_held ? is_held(&a->held) : -1;
    a->error = GetLastError();
    a->aux = aux_check();
    a->flags = *(USHORT *)(__readgsqword(0x30) + 0x17ee);
}

static void ask(enum state state)
{
    measure(&answers[state]);
    order[order_count++] = state;
}

static void print_answer(const char *name, const struct answer *a)
{
    if (a->ret == -1) printf("%-48s no function; AuxUlib's check %d, SameTebFlags %#x\n", name, a->aux, a->flags);
    else printf("%-48s returns %d, held %#x, error %#lx; AuxUlib's check %d, SameTebFlags %#x\n",
                name, a->ret, a->held, a->error, a->aux, a->flags);
}

/* at process exit the C runtime may already be gone: format by hand and write directly */
static void put(char **p, const char *s) { while (*s) *(*p)++ = *s++; }
static void put_hex(char **p, unsigned long v)
{
    char digits[16];
    int n = 0;

    put(p, "0x");
    do digits[n++] = "0123456789abcdef"[v & 15]; while (v >>= 4);
    while (n) *(*p)++ = digits[--n];
}

static void print_at_exit(const char *name)
{
    struct answer a;
    char buf[256], *p = buf;
    DWORD written;

    measure(&a);
    put(&p, name);
    while (p - buf < 49) *p++ = ' ';
    if (a.ret == -1) put(&p, "no function");
    else
    {
        put(&p, "returns "); put_hex(&p, a.ret);
        put(&p, ", held "); put_hex(&p, a.held);
        put(&p, ", error "); put_hex(&p, a.error);
    }
    put(&p, "; AuxUlib's check "); put_hex(&p, a.aux);
    put(&p, ", SameTebFlags "); put_hex(&p, a.flags);
    put(&p, "\r\n");
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buf, p - buf, &written, NULL);
}

static void NTAPI tls_callback(void *module, DWORD reason, void *reserved)
{
    (void)module; (void)reserved;
    switch (reason)
    {
    case DLL_PROCESS_ATTACH: ask(TLS_PROCESS_ATTACH); break;
    case DLL_THREAD_ATTACH: if (GetCurrentThreadId() == other_thread_id) ask(TLS_THREAD_ATTACH); break;
    case DLL_THREAD_DETACH: if (GetCurrentThreadId() == other_thread_id) ask(TLS_THREAD_DETACH); break;
    case DLL_PROCESS_DETACH: print_at_exit("TLS callback, process detach"); break;
    }
}

__attribute__((section(".CRT$XLF"), used)) static const PIMAGE_TLS_CALLBACK tls_callback_entry = tls_callback;

/* dllsyncprobe_dll.dll's entry point calls this */
__declspec(dllexport) void dll_main_reached(DWORD reason, void *reserved)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH: ask(DLLMAIN_PROCESS_ATTACH); break;
    case DLL_THREAD_ATTACH: if (GetCurrentThreadId() == other_thread_id) ask(DLLMAIN_THREAD_ATTACH); break;
    case DLL_THREAD_DETACH: if (GetCurrentThreadId() == other_thread_id) ask(DLLMAIN_THREAD_DETACH); break;
    case DLL_PROCESS_DETACH: print_at_exit(reserved ? "DllMain, process detach at exit" : "DllMain, process detach"); break;
    }
}

struct notification_data { ULONG flags; const UNICODE_STRING *full_name, *base_name; void *base; ULONG size; };

static void CALLBACK notification(ULONG reason, const struct notification_data *data, void *context)
{
    (void)context;
    if (data->base_name->Length != 22 || _wcsnicmp(data->base_name->Buffer, L"version.dll", 11)) return;
    ask(reason == 1 ? NOTIFY_LOADED : NOTIFY_UNLOADED);
}

static HANDLE go, done;
static ULONG try_disposition = 0xcc;

static DWORD WINAPI other_thread(void *arg)
{
    ULONG_PTR cookie;

    (void)arg;
    WaitForSingleObject(go, INFINITE);
    ask(OTHER_THREAD);
    /* the lock really is held: trying it from here does not get it */
    if (!pLdrLockLoaderLock(2, &try_disposition, &cookie) && try_disposition == 1) pLdrUnlockLoaderLock(0, cookie);
    SetEvent(done);
    return 0;
}

static void exports(const char *dll)
{
    HMODULE module = GetModuleHandleA(dll), kernelbase = GetModuleHandleA("kernelbase.dll");
    void *proc;

    if (!module) module = LoadLibraryExA(dll, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    proc = module ? (void *)GetProcAddress(module, "PrivIsDllSynchronizationHeld") : NULL;
    printf("%s: %s%s, %s%s\n", dll, module ? "loaded" : "missing",
           module && module == kernelbase && strcmp(dll, "kernelbase.dll") ? " (is kernelbase)" : "",
           proc ? "exports it" : "does not export it",
           proc && proc == (void *)is_held ? " (the same address)" : proc ? " (another address)" : "");
}

int main(void)
{
    NTSTATUS (WINAPI *register_notification)(ULONG, void *, void *, void **);
    NTSTATUS (WINAPI *unregister_notification)(void *);
    HMODULE ntdll = GetModuleHandleA("ntdll.dll"), version, dll;
    ULONG_PTR cookie1, cookie2;
    void *registration = NULL;
    HANDLE thread;
    unsigned int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    pLdrLockLoaderLock = (void *)GetProcAddress(ntdll, "LdrLockLoaderLock");
    pLdrUnlockLoaderLock = (void *)GetProcAddress(ntdll, "LdrUnlockLoaderLock");
    register_notification = (void *)GetProcAddress(ntdll, "LdrRegisterDllNotification");
    unregister_notification = (void *)GetProcAddress(ntdll, "LdrUnregisterDllNotification");
    resolve();

    exports("api-ms-win-core-libraryloader-l1-1-0.dll");
    exports("api-ms-win-core-libraryloader-private-l1-1-0.dll");
    exports("kernelbase.dll");
    exports("kernel32.dll");
    exports("ntdll.dll");

    if (register_notification) register_notification(0, notification, NULL, &registration);
    version = LoadLibraryA("version.dll");
    if (version) FreeLibrary(version);
    if (registration) unregister_notification(registration);

    /* stays loaded until the process exits */
    if (!(dll = LoadLibraryA("dllsyncprobe_dll.dll"))) printf("dllsyncprobe_dll.dll: %lu\n", GetLastError());

    ask(MAIN_FREE);
    pLdrLockLoaderLock(0, NULL, &cookie1);
    ask(MAIN_LOCKED);
    pLdrLockLoaderLock(0, NULL, &cookie2);
    ask(MAIN_LOCKED_TWICE);
    pLdrUnlockLoaderLock(0, cookie2);
    ask(MAIN_LOCKED_ONCE_AGAIN);
    pLdrUnlockLoaderLock(0, cookie1);
    ask(MAIN_UNLOCKED);

    /* the thread must be running before the lock is taken: starting a thread needs the lock */
    go = CreateEventA(NULL, FALSE, FALSE, NULL);
    done = CreateEventA(NULL, FALSE, FALSE, NULL);
    thread = CreateThread(NULL, 0, other_thread, NULL, CREATE_SUSPENDED, &other_thread_id);
    ResumeThread(thread);
    Sleep(200);
    pLdrLockLoaderLock(0, NULL, &cookie1);
    SetEvent(go);
    if (WaitForSingleObject(done, 5000)) printf("other thread did not answer while the lock was held\n");
    pLdrUnlockLoaderLock(0, cookie1);
    WaitForSingleObject(thread, 5000);
    CloseHandle(thread);
    printf("other thread, LdrLockLoaderLock(try only) while the main thread holds it: disposition %lu\n", try_disposition);

    for (i = 0; i < order_count; i++) print_answer(state_names[order[i]], &answers[order[i]]);
    for (i = 0; i < STATE_COUNT; i++)
    {
        unsigned int j;
        for (j = 0; j < order_count; j++) if (order[j] == i) break;
        if (j == order_count) printf("%-48s not reached\n", state_names[i]);
    }
    return 0;
}
