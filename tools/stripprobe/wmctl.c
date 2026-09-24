/* wmctl <window> activate|max|unmax|min : ask the window manager to do to an X window what a
 * user would, as _NET_ACTIVE_WINDOW / _NET_WM_STATE requests and XIconifyWindow. */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    Display *dpy;
    Window root, win;
    XEvent cm = {0};

    if (argc < 3 || !(dpy = XOpenDisplay(NULL))) return 1;
    root = DefaultRootWindow(dpy);
    win = strtoul(argv[1], NULL, 0);
    if (!strcmp(argv[2], "min"))
    {
        XIconifyWindow(dpy, win, DefaultScreen(dpy));
        XCloseDisplay(dpy);
        return 0;
    }
    cm.xclient.type = ClientMessage;
    cm.xclient.window = win;
    cm.xclient.format = 32;
    if (!strcmp(argv[2], "activate"))
    {
        cm.xclient.message_type = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
        cm.xclient.data.l[0] = 2; /* from a pager, as a user would */
        cm.xclient.data.l[1] = CurrentTime;
    }
    else
    {
        cm.xclient.message_type = XInternAtom(dpy, "_NET_WM_STATE", False);
        cm.xclient.data.l[0] = !strcmp(argv[2], "max") ? 1 : 0;
        cm.xclient.data.l[1] = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);
        cm.xclient.data.l[2] = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
        cm.xclient.data.l[3] = 2;
    }
    XSendEvent(dpy, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &cm);
    XCloseDisplay(dpy);
    return 0;
}
