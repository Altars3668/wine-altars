/*
 * sendkeys - type into a Wine window from inside the Wine session.
 *
 * Synthetic input from outside does not reach Wine on a GNOME Wayland
 * session: mutter keeps XTEST events away from Xwayland clients (or asks the
 * person at the desk for remote-control consent first), so xdotool can move
 * the pointer and nothing happens.  SendInput from a process in the same
 * session never touches the X server -- the wineserver queues the input to
 * the foreground thread directly -- so it works on :0 as well as on a bare
 * Xvfb, without anyone having to approve it.
 *
 *   sendkeys [-w class-substring] [-d ms] [-close] key...
 *
 * A key is a chord such as ctrl+p, alt+f, shift+tab, esc, enter, f10, or
 * text:some words to type literally.  move:x,y puts the pointer at a screen
 * position, click presses the left mouse button there, press and release
 * hold it down and let it go (so that press move:... release drags), and
 * wheel:n turns the wheel n notches (negative scrolls down), where the
 * pointer is.
 * down:key and up:key press and let go of one key on
 * its own, so that down:ctrl wheel:1 up:ctrl zooms.  With -w, the first visible top-level
 * window whose class contains the substring is brought to the foreground
 * first.  -d sets the pause between keys (default 150 ms).  -close posts
 * WM_CLOSE to that window instead, which closes an Office application the
 * way its close button does.
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *want_class;
static HWND found;

static BOOL CALLBACK find_window(HWND hwnd, LPARAM lparam)
{
    char cls[256];

    (void)lparam;
    if (!IsWindowVisible(hwnd)) return TRUE;
    GetClassNameA(hwnd, cls, sizeof(cls));
    if (strstr(cls, want_class)) { found = hwnd; return FALSE; }
    return TRUE;
}

static WORD key_code(const char *name)
{
    static const struct { const char *name; WORD vk; } names[] =
    {
        {"ctrl", VK_CONTROL}, {"alt", VK_MENU}, {"shift", VK_SHIFT}, {"win", VK_LWIN},
        {"esc", VK_ESCAPE}, {"enter", VK_RETURN}, {"tab", VK_TAB}, {"space", VK_SPACE},
        {"up", VK_UP}, {"down", VK_DOWN}, {"left", VK_LEFT}, {"right", VK_RIGHT},
        {"home", VK_HOME}, {"end", VK_END}, {"pgup", VK_PRIOR}, {"pgdn", VK_NEXT},
        {"bksp", VK_BACK}, {"del", VK_DELETE},
    };
    unsigned int i;

    for (i = 0; i < ARRAYSIZE(names); i++) if (!_stricmp(name, names[i].name)) return names[i].vk;
    if ((name[0] == 'f' || name[0] == 'F') && name[1] >= '1' && name[1] <= '9') return VK_F1 + atoi(name + 1) - 1;
    if (!name[1]) return (WORD)(VkKeyScanA(name[0]) & 0xff);
    fprintf(stderr, "unknown key %s\n", name);
    return 0;
}

static void press_chord(char *chord)
{
    INPUT in[16];
    WORD vks[8];
    int n = 0, count = 0, i;
    char *part = strtok(chord, "+");

    while (part && n < (int)ARRAYSIZE(vks)) { if ((vks[n] = key_code(part))) n++; part = strtok(NULL, "+"); }
    memset(in, 0, sizeof(in));
    for (i = 0; i < n; i++) { in[count].type = INPUT_KEYBOARD; in[count].ki.wVk = vks[i]; count++; }
    for (i = n - 1; i >= 0; i--) { in[count].type = INPUT_KEYBOARD; in[count].ki.wVk = vks[i]; in[count].ki.dwFlags = KEYEVENTF_KEYUP; count++; }
    SendInput(count, in, sizeof(in[0]));
}

static void move_pointer(const char *where)
{
    INPUT in = {0};
    int x = 0, y = 0;

    sscanf(where, "%d,%d", &x, &y);
    in.type = INPUT_MOUSE;
    in.mi.dx = MulDiv(x, 65535, GetSystemMetrics(SM_CXVIRTUALSCREEN) - 1);
    in.mi.dy = MulDiv(y, 65535, GetSystemMetrics(SM_CYVIRTUALSCREEN) - 1);
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    SendInput(1, &in, sizeof(in));
}

static void turn_wheel(int notches)
{
    INPUT in = {0};

    in.type = INPUT_MOUSE;
    in.mi.mouseData = notches * WHEEL_DELTA;
    in.mi.dwFlags = MOUSEEVENTF_WHEEL;
    SendInput(1, &in, sizeof(in));
}

static BOOL click_pointer(void)
{
    INPUT in[2] = {0};

    in[0].type = in[1].type = INPUT_MOUSE;
    in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    return SendInput(2, in, sizeof(in[0])) == 2;
}

static BOOL hold_button(BOOL up)
{
    INPUT in = {0};

    in.type = INPUT_MOUSE;
    in.mi.dwFlags = up ? MOUSEEVENTF_LEFTUP : MOUSEEVENTF_LEFTDOWN;
    return SendInput(1, &in, sizeof(in)) == 1;
}

static void press_key(const char *name, BOOL up)
{
    INPUT in = {0};

    if (!(in.ki.wVk = key_code(name))) return;
    in.type = INPUT_KEYBOARD;
    if (up) in.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(in));
}

/* the text comes from the wide command line: argv is in the ANSI code page, which has no room for most of it */
static void type_text(const WCHAR *text)
{
    int len = lstrlenW(text), i;

    for (i = 0; i < len; i++)
    {
        INPUT in[2] = {0};
        in[0].type = in[1].type = INPUT_KEYBOARD;
        in[0].ki.wScan = in[1].ki.wScan = text[i];
        in[0].ki.dwFlags = KEYEVENTF_UNICODE;
        in[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        SendInput(2, in, sizeof(in[0]));
    }
}

int main(int argc, char **argv)
{
    int i, delay = 150, wargc;
    WCHAR **wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    BOOL close = FALSE;

    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++)
    {
        if (!strcmp(argv[i], "-w") && i + 1 < argc) want_class = argv[++i];
        else if (!strcmp(argv[i], "-d") && i + 1 < argc) delay = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-close")) close = TRUE;
        else break;
    }

    if (want_class)
    {
        EnumWindows(find_window, 0);
        if (!found) { fprintf(stderr, "no visible window of class *%s*\n", want_class); return 1; }
        if (close)
        {
            PostMessageW(found, WM_CLOSE, 0, 0);
            printf("closing %p (%s)\n", found, want_class);
            return 0;
        }
        SetForegroundWindow(found);
        Sleep(delay);
        printf("foreground %p (%s), now %p\n", found, want_class, GetForegroundWindow());
    }

    for (; i < argc; i++)
    {
        char chord[64];

        lstrcpynA(chord, argv[i], sizeof(chord));
        if (!strncmp(argv[i], "text:", 5)) type_text(wargv && i < wargc ? wargv[i] + 5 : L"");
        else if (!strncmp(argv[i], "move:", 5)) move_pointer(argv[i] + 5);
        else if (!strcmp(argv[i], "click"))
        {
            if (!click_pointer()) { fprintf(stderr, "mouse click failed: %lu\n", GetLastError()); return 1; }
        }
        else if (!strcmp(argv[i], "press") || !strcmp(argv[i], "release"))
        {
            if (!hold_button(argv[i][0] == 'r')) { fprintf(stderr, "mouse %s failed: %lu\n", argv[i], GetLastError()); return 1; }
        }
        else if (!strncmp(argv[i], "wheel:", 6)) turn_wheel(atoi(argv[i] + 6));
        else if (!strncmp(argv[i], "down:", 5)) press_key(argv[i] + 5, FALSE);
        else if (!strncmp(argv[i], "up:", 3)) press_key(argv[i] + 3, TRUE);
        else press_chord(chord);
        printf("sent %s\n", argv[i]);
        Sleep(delay);
    }
    return 0;
}
