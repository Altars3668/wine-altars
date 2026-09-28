/*
 * embedevents <word-embed.vbs> <output.docx> <progress log> <seconds to show> [script options] [window.bmp]
 *
 * Runs word-embed.vbs with a show period and the script's options ("new" when none are given, so that a Word
 * already running is left alone), and logs from window events what happens meanwhile to the windows of the Word
 * and Excel processes that were not running when it started: each one's creation, showing, hiding, moving,
 * reparenting and destruction, and the process whose thread did it, interleaved with the script's progress.
 * Shortly before the show period ends it lists those processes' windows and, given a file, saves what Word's
 * window draws (with PrintWindow, so nothing else on the screen is in it).  What activating an Excel worksheet in
 * a Word document does to the windows of the two, and which of them does it, on the machine it runs on.
 */
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x2
#endif

enum kind { KIND_OTHER, KIND_WORD, KIND_EXCEL };
static const char *kind_names[] = { "other", "word", "excel" };

static DWORD old_pids[64];
static unsigned int old_count;
static struct { DWORD pid; enum kind kind; } processes[256];
static unsigned int process_count;
static struct { DWORD tid; enum kind kind; } threads[1024];
static unsigned int thread_count;
static struct { HWND hwnd; char class[48]; enum kind kind; } windows[4096];
static unsigned int window_count;
static DWORD start;

static const char *base_name(const char *path)
{
    const char *name = strrchr(path, '\\');
    return name ? name + 1 : path;
}

static enum kind process_kind(DWORD pid)
{
    char path[MAX_PATH];
    DWORD size = sizeof(path);
    enum kind kind = KIND_OTHER;
    HANDLE process;
    unsigned int i;

    for (i = 0; i < process_count; i++) if (processes[i].pid == pid) return processes[i].kind;
    for (i = 0; i < old_count; i++) if (old_pids[i] == pid) break;
    /* a process that was running before is someone else's */
    if (i == old_count && (process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)))
    {
        if (QueryFullProcessImageNameA(process, 0, path, &size))
        {
            if (!_stricmp(base_name(path), "WINWORD.EXE")) kind = KIND_WORD;
            else if (!_stricmp(base_name(path), "EXCEL.EXE")) kind = KIND_EXCEL;
        }
        CloseHandle(process);
    }
    if (process_count < ARRAYSIZE(processes))
    {
        processes[process_count].pid = pid;
        processes[process_count++].kind = kind;
    }
    return kind;
}

static enum kind thread_kind(DWORD tid)
{
    enum kind kind = KIND_OTHER;
    HANDLE thread;
    unsigned int i;

    for (i = 0; i < thread_count; i++) if (threads[i].tid == tid) return threads[i].kind;
    if ((thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, tid)))
    {
        DWORD pid = GetProcessIdOfThread(thread);

        if (pid) kind = process_kind(pid);
        CloseHandle(thread);
    }
    if (thread_count < ARRAYSIZE(threads))
    {
        threads[thread_count].tid = tid;
        threads[thread_count++].kind = kind;
    }
    return kind;
}

/* where a window is, in its parent's client area, and what it is */
static void describe(HWND hwnd, char *buffer, size_t size)
{
    HWND parent = GetAncestor(hwnd, GA_PARENT);
    char parent_class[48] = "";
    RECT rect;

    GetWindowRect(hwnd, &rect);
    if (parent && parent != GetDesktopWindow())
    {
        MapWindowPoints(NULL, parent, (POINT *)&rect, 2);
        GetClassNameA(parent, parent_class, sizeof(parent_class));
    }
    snprintf(buffer, size, "vis %d (%ld,%ld)-(%ld,%ld) in %s", IsWindowVisible(hwnd), rect.left, rect.top,
             rect.right, rect.bottom, parent && parent != GetDesktopWindow() ? parent_class : "desktop");
}

static const char *event_name(DWORD event)
{
    switch (event)
    {
    case EVENT_SYSTEM_FOREGROUND: return "foregrnd";
    case EVENT_OBJECT_CREATE: return "create";
    case EVENT_OBJECT_DESTROY: return "destroy";
    case EVENT_OBJECT_SHOW: return "show";
    case EVENT_OBJECT_HIDE: return "hide";
    case EVENT_OBJECT_FOCUS: return "focus";
    case EVENT_OBJECT_LOCATIONCHANGE: return "move";
    case EVENT_OBJECT_PARENTCHANGE: return "parent";
    }
    return NULL;
}

static void CALLBACK event_proc(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG object, LONG child, DWORD tid,
                                DWORD time)
{
    char where[160] = "";
    const char *name;
    unsigned int i;
    DWORD pid;

    if (object != OBJID_WINDOW || child != CHILDID_SELF || !hwnd || !(name = event_name(event))) return;
    for (i = 0; i < window_count; i++) if (windows[i].hwnd == hwnd) break;
    if (i == window_count)
    {
        enum kind kind;

        if (!GetWindowThreadProcessId(hwnd, &pid) || (kind = process_kind(pid)) == KIND_OTHER) return;
        if (window_count == ARRAYSIZE(windows)) return;
        windows[i].hwnd = hwnd;
        windows[i].kind = kind;
        GetClassNameA(hwnd, windows[i].class, sizeof(windows[i].class));
        window_count++;
    }
    /* the windows in-place activation moves, and not every tooltip and ribbon part */
    if (event == EVENT_OBJECT_LOCATIONCHANGE && strncmp(windows[i].class, "EXCEL", 5) &&
        strcmp(windows[i].class, "_WwG"))
        return;
    if (event != EVENT_OBJECT_DESTROY) describe(hwnd, where, sizeof(where));
    printf("%7lu %-8s %p %-5s %-16s by %-5s %04lx  %s\n", time - start, name, hwnd, kind_names[windows[i].kind],
           windows[i].class, kind_names[thread_kind(tid)], tid, where);
}

static void list_tree(HWND hwnd, int depth)
{
    char class[48], text[48], where[160];
    HWND child;
    DWORD pid;

    GetWindowThreadProcessId(hwnd, &pid);
    GetClassNameA(hwnd, class, sizeof(class));
    GetWindowTextA(hwnd, text, sizeof(text));
    describe(hwnd, where, sizeof(where));
    printf("  %*s%p %-5s %-24s style %08lx  %s \"%s\"\n", depth * 2, "", hwnd, kind_names[process_kind(pid)],
           class, GetWindowLongA(hwnd, GWL_STYLE), where, text);
    if (depth >= 10) return;
    for (child = GetWindow(hwnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) list_tree(child, depth + 1);
}

static BOOL CALLBACK list_top(HWND hwnd, LPARAM param)
{
    DWORD pid;

    GetWindowThreadProcessId(hwnd, &pid);
    if (process_kind(pid) != KIND_OTHER) list_tree(hwnd, 0);
    return TRUE;
}

static BOOL CALLBACK find_word(HWND hwnd, LPARAM param)
{
    char class[16];
    DWORD pid;

    GetWindowThreadProcessId(hwnd, &pid);
    GetClassNameA(hwnd, class, sizeof(class));
    if (process_kind(pid) != KIND_WORD || strcmp(class, "OpusApp") || !IsWindowVisible(hwnd)) return TRUE;
    *(HWND *)param = hwnd;
    return FALSE;
}

static BOOL save_window(HWND hwnd, const char *path)
{
    BITMAPFILEHEADER file = { 0x4d42 };
    BITMAPINFOHEADER info = { sizeof(info) };
    HBITMAP bitmap;
    BOOL printed;
    void *bits;
    RECT rect;
    FILE *out;
    HDC dc;

    GetWindowRect(hwnd, &rect);
    info.biWidth = rect.right - rect.left;
    info.biHeight = -(rect.bottom - rect.top);
    info.biPlanes = 1;
    info.biBitCount = 32;
    dc = CreateCompatibleDC(NULL);
    bitmap = CreateDIBSection(dc, (BITMAPINFO *)&info, DIB_RGB_COLORS, &bits, NULL, 0);
    SelectObject(dc, bitmap);
    printed = PrintWindow(hwnd, dc, PW_RENDERFULLCONTENT);
    GdiFlush();
    if (printed && (out = fopen(path, "wb")))
    {
        file.bfOffBits = sizeof(file) + sizeof(info);
        file.bfSize = file.bfOffBits + info.biWidth * -info.biHeight * 4;
        fwrite(&file, sizeof(file), 1, out);
        fwrite(&info, sizeof(info), 1, out);
        fwrite(bits, 4, info.biWidth * -info.biHeight, out);
        fclose(out);
    }
    else printed = FALSE;
    DeleteDC(dc);
    DeleteObject(bitmap);
    return printed;
}

static void note_running(void)
{
    PROCESSENTRY32 entry = { sizeof(entry) };
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    unsigned int word = 0, excel = 0, powerpoint = 0;

    if (Process32First(snapshot, &entry))
        do
        {
            BOOL office = TRUE;

            if (!_stricmp(entry.szExeFile, "WINWORD.EXE")) word++;
            else if (!_stricmp(entry.szExeFile, "EXCEL.EXE")) excel++;
            else if (!_stricmp(entry.szExeFile, "POWERPNT.EXE")) powerpoint++;
            else office = FALSE;
            if (office && old_count < ARRAYSIZE(old_pids)) old_pids[old_count++] = entry.th32ProcessID;
        }
        while (Process32Next(snapshot, &entry));
    CloseHandle(snapshot);
    printf("running before: %u Word, %u Excel, %u PowerPoint (left alone)\n", word, excel, powerpoint);
}

/* prints what the script wrote to its progress log since the last call, and says whether it has activated */
static BOOL read_progress(const char *path, long *offset)
{
    BOOL activated = FALSE;
    char line[256];
    FILE *progress;

    if (!(progress = fopen(path, "r"))) return FALSE;
    if (!fseek(progress, *offset, SEEK_SET))
        while (fgets(line, sizeof(line), progress) && strchr(line, '\n'))
        {
            *offset = ftell(progress);
            line[strcspn(line, "\r\n")] = 0;
            printf("%7lu script   %s\n", GetTickCount() - start, line);
            if (!strcmp(line, "worksheet activated")) activated = TRUE;
        }
    fclose(progress);
    return activated;
}

int main(int argc, char **argv)
{
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    HWINEVENTHOOK hooks[2];
    DWORD dump_at = 0, limit;
    char cmdline[2048];
    int seconds;
    long offset = 0;
    BOOL running = TRUE;
    MSG msg;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 5)
    {
        printf("usage: embedevents <word-embed.vbs> <output.docx> <progress log> <seconds> [options] [window.bmp]\n");
        return 2;
    }
    seconds = atoi(argv[4]);
    note_running();
    start = GetTickCount();
    printf("start tick %lu\n", start);
    hooks[0] = SetWinEventHook(EVENT_OBJECT_CREATE, EVENT_OBJECT_PARENTCHANGE, NULL, event_proc, 0, 0,
                               WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    hooks[1] = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, NULL, event_proc, 0, 0,
                               WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if (!hooks[0] || !hooks[1])
    {
        printf("SetWinEventHook failed %lu\n", GetLastError());
        return 1;
    }
    snprintf(cmdline, sizeof(cmdline), "cscript.exe //nologo \"%s\" \"%s\" \"%s\" \"\" %d %s", argv[1], argv[2],
             argv[3], seconds, argc > 5 ? argv[5] : "new");
    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        printf("CreateProcess failed %lu\n", GetLastError());
        return 1;
    }
    limit = (seconds + 300) * 1000;
    while (running)
    {
        if (MsgWaitForMultipleObjects(1, &pi.hProcess, FALSE, 100, QS_ALLINPUT) == WAIT_OBJECT_0)
        {
            /* the events the script's last steps raised are still on their way */
            DWORD end = GetTickCount() + 1000;

            while ((LONG)(end - GetTickCount()) > 0)
            {
                MsgWaitForMultipleObjects(0, NULL, FALSE, 100, QS_ALLINPUT);
                while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
            }
            running = FALSE;
        }
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
        if (read_progress(argv[3], &offset) && seconds > 0)
            dump_at = GetTickCount() + (seconds > 6 ? seconds - 3 : seconds / 2) * 1000;
        if (dump_at && (LONG)(GetTickCount() - dump_at) >= 0)
        {
            HWND word = NULL;

            dump_at = 0;
            printf("%7lu windows:\n", GetTickCount() - start);
            EnumWindows(list_top, 0);
            EnumWindows(find_word, (LPARAM)&word);
            if (argc > 6 && word) printf("saved Word's window: %d\n", save_window(word, argv[6]));
        }
        if (running && GetTickCount() - start > limit)
        {
            printf("script still running after %lu s; it and the Office processes it started are left running\n",
                   limit / 1000);
            break;
        }
    }
    UnhookWinEvent(hooks[0]);
    UnhookWinEvent(hooks[1]);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    printf("done\n");
    return 0;
}
