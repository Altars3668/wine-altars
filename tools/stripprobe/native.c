/* native x y w h [seconds] [pixel]: a plain decorated X window filled with one colour (red by
 * default), activated so the window manager puts it on top: read the screen under it to see
 * whether anything of another program shows through. */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(int argc, char **argv)
{
    int x = atoi(argv[1]), y = atoi(argv[2]), w = atoi(argv[3]), h = atoi(argv[4]), secs = argc > 5 ? atoi(argv[5]) : 30;
    unsigned long pixel = argc > 6 ? strtoul(argv[6], NULL, 0) : 0xff0000;
    setvbuf(stdout, NULL, _IONBF, 0);
    Display *dpy = XOpenDisplay(NULL);
    Window root = DefaultRootWindow(dpy);
    XSetWindowAttributes attr = {0};
    attr.background_pixel = pixel; attr.event_mask = StructureNotifyMask | ExposureMask;
    Window win = XCreateWindow(dpy, root, x, y, w, h, 0, CopyFromParent, InputOutput, CopyFromParent, CWBackPixel | CWEventMask, &attr);
    XSizeHints *sh = XAllocSizeHints(); sh->flags = USPosition | PPosition; sh->x = x; sh->y = y;
    XSetWMNormalHints(dpy, win, sh);
    XStoreName(dpy, win, "native probe");
    XMapWindow(dpy, win);
    XEvent ev;
    do XNextEvent(dpy, &ev); while (ev.type != MapNotify);
    XEvent cm = {0};
    cm.xclient.type = ClientMessage; cm.xclient.window = win; cm.xclient.format = 32;
    cm.xclient.message_type = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    cm.xclient.data.l[0] = 2; cm.xclient.data.l[1] = CurrentTime;
    XSendEvent(dpy, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &cm);
    XFlush(dpy);
    Window child; int rx, ry;
    usleep(300000);
    XTranslateCoordinates(dpy, win, root, 0, 0, &rx, &ry, &child);
    printf("native %lx at %d,%d\n", win, rx, ry);
    sleep(secs);
    return 0;
}
