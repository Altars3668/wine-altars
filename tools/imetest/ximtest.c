/*
 * ximtest - an X client with an input context of a given style, printing what the input method does with it: the
 * preedit callbacks and the strings it commits.  With a spot, it also sets XNSpotLocation after making the context.
 *
 *   gcc -o ximtest ximtest.c -lX11
 *   XMODIFIERS=@im=ibus ./ximtest [style [spot x spot y]]     (default style 0x202, preedit and status callbacks)
 *
 * Copyright 2026 AltarsCN.  LGPL 2.1 or later, as Wine.
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int preedit_start(XIC xic, XPointer client, XPointer call) { printf("preedit start\n"); fflush(stdout); return -1; }
static void preedit_done(XIC xic, XPointer client, XPointer call) { printf("preedit done\n"); fflush(stdout); }
static void preedit_draw(XIC xic, XPointer client, XPointer call)
{
    XIMPreeditDrawCallbackStruct *draw = (XIMPreeditDrawCallbackStruct *)call;
    printf("preedit draw caret %d first %d length %d text %s\n", draw->caret, draw->chg_first, draw->chg_length,
           draw->text ? (draw->text->encoding_is_wchar ? "(wchar)" : draw->text->string.multi_byte) : "(none)");
    fflush(stdout);
}
static void preedit_caret(XIC xic, XPointer client, XPointer call) { printf("preedit caret\n"); fflush(stdout); }
static void status_start(XIC xic, XPointer client, XPointer call) { printf("status start\n"); fflush(stdout); }
static void status_done(XIC xic, XPointer client, XPointer call) { printf("status done\n"); fflush(stdout); }
static void status_draw(XIC xic, XPointer client, XPointer call) { printf("status draw\n"); fflush(stdout); }

int main(int argc, char **argv)
{
    XIMCallback start = { NULL, (XIMProc)preedit_start }, done = { NULL, (XIMProc)preedit_done }, draw = { NULL, (XIMProc)preedit_draw },
                caret = { NULL, (XIMProc)preedit_caret }, sstart = { NULL, (XIMProc)status_start }, sdone = { NULL, (XIMProc)status_done },
                sdraw = { NULL, (XIMProc)status_draw };
    XIMStyle style = argc > 1 ? strtoul(argv[1], NULL, 0) : XIMPreeditCallbacks | XIMStatusCallbacks;
    Display *display;
    XVaNestedList preedit, status;
    Window window;
    XIM xim;
    XIC xic;

    setlocale(LC_ALL, "");
    XSetLocaleModifiers("");
    if (!(display = XOpenDisplay(NULL))) return 1;
    window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 400, 200, 0, 0, 0xffffff);
    XStoreName(display, window, "ximtest");
    XSelectInput(display, window, KeyPressMask | KeyReleaseMask | FocusChangeMask | ExposureMask);
    XMapWindow(display, window);
    if (!(xim = XOpenIM(display, NULL, NULL, NULL))) { printf("no input method\n"); return 1; }
    preedit = XVaCreateNestedList(0, XNPreeditStartCallback, &start, XNPreeditDoneCallback, &done,
                                  XNPreeditDrawCallback, &draw, XNPreeditCaretCallback, &caret, NULL);
    status = XVaCreateNestedList(0, XNStatusStartCallback, &sstart, XNStatusDoneCallback, &sdone,
                                 XNStatusDrawCallback, &sdraw, NULL);
    xic = XCreateIC(xim, XNInputStyle, style, XNClientWindow, window, XNFocusWindow, window,
                    XNPreeditAttributes, preedit, XNStatusAttributes, status, NULL);
    printf("style %#lx, xic %p\n", style, (void *)xic);
    fflush(stdout);
    if (!xic) return 1;
    XSetICFocus(xic);
    if (argc > 2)
    {
        XPoint spot = { atoi(argv[2]), atoi(argv[3]) };
        XVaNestedList attr = XVaCreateNestedList(0, XNSpotLocation, &spot, NULL);
        char *failed = XSetICValues(xic, XNPreeditAttributes, attr, NULL);
        printf("spot %d,%d: XSetICValues %s\n", spot.x, spot.y, failed ? failed : "(ok)");
        fflush(stdout);
        XFree(attr);
    }
    for (;;)
    {
        XEvent event;
        XNextEvent(display, &event);
        if (XFilterEvent(&event, None)) continue;
        if (event.type == KeyPress)
        {
            char buffer[256];
            KeySym keysym;
            Status got;
            int len = Xutf8LookupString(xic, &event.xkey, buffer, sizeof(buffer) - 1, &keysym, &got);
            buffer[len > 0 ? len : 0] = 0;
            printf("key %#lx status %d text \"%s\"\n", keysym, got, buffer);
            fflush(stdout);
        }
    }
}
