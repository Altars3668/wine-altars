/* undecorated [seconds]: map a plain window with _MOTIF_WM_HINTS decorations=0 and report
 * when (if ever) the window manager maps it.  mutter maps it at once while its stage is
 * drawing; with the session in the background (frame clock stopped) never, until activated. */
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 20;
    setvbuf(stdout, NULL, _IONBF, 0);
    Display *dpy = XOpenDisplay(NULL);
    Window root = DefaultRootWindow(dpy);
    XSetWindowAttributes attr = {0};
    attr.event_mask = StructureNotifyMask; attr.background_pixel = 0xc04080;
    Window w = XCreateWindow(dpy, root, 300, 300, 400, 300, 0, CopyFromParent, InputOutput, CopyFromParent, CWEventMask | CWBackPixel, &attr);
    long mwm[5] = { 2, 0, 0, 0, 0 };
    Atom a = XInternAtom(dpy, "_MOTIF_WM_HINTS", False);
    XChangeProperty(dpy, w, a, a, 32, PropModeReplace, (unsigned char *)mwm, 5);
    XMapWindow(dpy, w);
    XFlush(dpy);
    printf("window %lx map requested\n", w);
    for (int i = 0; i < secs * 100; i++)
    {
        XEvent ev;
        if (XCheckTypedWindowEvent(dpy, w, MapNotify, &ev)) { printf("mapped after %d ms\n", i * 10); break; }
        usleep(10000);
    }
    XWindowAttributes wa; XGetWindowAttributes(dpy, w, &wa);
    printf("map_state %d\n", wa.map_state);
    return 0;
}
