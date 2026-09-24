/* stripprobe: where does the window manager put a window with the properties winex11 gives
 * Office's shadow strips (MSO_BORDEREFFECT_WINDOW_CLASS)?  Maps a parent window, then a strip
 * transient for it (utility type, input=False, MWM decorations 0, min=max size hints, ARGB visual),
 * waits for MapNotify and prints where it ended up against where it asked to be.
 *
 *   stripprobe [x y w h] [then x y]... [notransient] [normaltype] [input] [nomwm] [nosizehints]
 *              [argb=0] [mwmflags=N] [mwmfunc=N] [mwmdeco=N] [hold]
 *
 * "then x y" moves the mapped strip and prints where it lands, for the constraints applied to a
 * window that is already managed.  Measured against mutter 50: see docs/office365-under-wine.md,
 * "窗口互相透视". */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

static Display *dpy;
static int has(int argc, char **argv, const char *s) { for (int i = 1; i < argc; i++) if (!strcmp(argv[i], s)) return 1; return 0; }
static Atom A(const char *n) { return XInternAtom(dpy, n, False); }

static int wait_map(Window w, int ms)
{
    XEvent ev;
    struct timeval start, now;
    gettimeofday(&start, NULL);
    for (;;)
    {
        while (XCheckTypedWindowEvent(dpy, w, MapNotify, &ev)) return 1;
        gettimeofday(&now, NULL);
        if ((now.tv_sec - start.tv_sec) * 1000 + (now.tv_usec - start.tv_usec) / 1000 > ms) return 0;
        usleep(10000);
    }
}

int main(int argc, char **argv)
{
    int x = 192, y = 192, w = 800, h = 8, argb = !has(argc, argv, "argb=0");
    for (int i = 1; i + 3 < argc && strcmp(argv[i], "then"); i++) if ((argv[i][0] >= '0' && argv[i][0] <= '9') || (argv[i][0] == '-' && argv[i][1] >= '0' && argv[i][1] <= '9'))
    { x = atoi(argv[i]); y = atoi(argv[i+1]); w = atoi(argv[i+2]); h = atoi(argv[i+3]); break; }
    setvbuf(stdout, NULL, _IONBF, 0);
    dpy = XOpenDisplay(NULL);
    if (!dpy) return 1;
    Window root = DefaultRootWindow(dpy);
    XSetWindowAttributes attr = {0};
    attr.event_mask = StructureNotifyMask | PropertyChangeMask;
    attr.background_pixel = 0x808080;
    Window parent = XCreateWindow(dpy, root, 200, 200, 800, 600, 0, CopyFromParent, InputOutput, CopyFromParent, CWEventMask | CWBackPixel, &attr);
    XStoreName(dpy, parent, "stripprobe parent");
    XMapWindow(dpy, parent);
    printf("parent mapped: %d\n", wait_map(parent, 3000));

    XVisualInfo vi;
    Visual *visual = CopyFromParent; int depth = CopyFromParent; unsigned long mask = CWEventMask | CWBorderPixel;
    attr.border_pixel = 0;
    if (argb && XMatchVisualInfo(dpy, DefaultScreen(dpy), 32, TrueColor, &vi))
    {
        visual = vi.visual; depth = 32;
        attr.colormap = XCreateColormap(dpy, root, visual, AllocNone); mask |= CWColormap;
        attr.background_pixel = 0x80000000; mask |= CWBackPixel;
    }
    Window strip = XCreateWindow(dpy, root, x, y, w, h, 0, depth, InputOutput, visual, mask, &attr);
    if (!has(argc, argv, "notransient")) XSetTransientForHint(dpy, strip, parent);
    Atom type = A(has(argc, argv, "normaltype") ? "_NET_WM_WINDOW_TYPE_NORMAL" : "_NET_WM_WINDOW_TYPE_UTILITY");
    XChangeProperty(dpy, strip, A("_NET_WM_WINDOW_TYPE"), XA_ATOM, 32, PropModeReplace, (unsigned char *)&type, 1);
    XWMHints *hints = XAllocWMHints();
    hints->flags = InputHint | StateHint | WindowGroupHint; hints->input = has(argc, argv, "input"); hints->initial_state = NormalState; hints->window_group = parent;
    XSetWMHints(dpy, strip, hints);
    if (!has(argc, argv, "nomwm"))
    {
        long mwm[5] = { 3, 0x24, 0, 0, 0 };
        for (int i = 1; i < argc; i++) if (!strncmp(argv[i], "mwmfunc=", 8)) mwm[1] = strtol(argv[i] + 8, NULL, 0);
        for (int i = 1; i < argc; i++) if (!strncmp(argv[i], "mwmflags=", 9)) mwm[0] = strtol(argv[i] + 9, NULL, 0);
        for (int i = 1; i < argc; i++) if (!strncmp(argv[i], "mwmdeco=", 8)) mwm[2] = strtol(argv[i] + 8, NULL, 0);
        XChangeProperty(dpy, strip, A("_MOTIF_WM_HINTS"), A("_MOTIF_WM_HINTS"), 32, PropModeReplace, (unsigned char *)mwm, 5);
    }
    if (!has(argc, argv, "nosizehints"))
    {
        XSizeHints *sh = XAllocSizeHints();
        sh->flags = PPosition | PMinSize | PMaxSize | PWinGravity; sh->x = x; sh->y = y;
        sh->min_width = sh->max_width = w; sh->min_height = sh->max_height = h; sh->win_gravity = StaticGravity;
        XSetWMNormalHints(dpy, strip, sh);
    }
    Atom states[2] = { A("_NET_WM_STATE_SKIP_PAGER"), A("_NET_WM_STATE_SKIP_TASKBAR") };
    XChangeProperty(dpy, strip, A("_NET_WM_STATE"), XA_ATOM, 32, PropModeReplace, (unsigned char *)states, 2);
    Atom protocols[2] = { A("WM_DELETE_WINDOW"), A("_NET_WM_PING") };
    XSetWMProtocols(dpy, strip, protocols, 2);
    XMapWindow(dpy, strip);
    int mapped = wait_map(strip, 3000);
    XWindowAttributes wa; Window child; int rx, ry;
    XGetWindowAttributes(dpy, strip, &wa);
    XTranslateCoordinates(dpy, strip, root, 0, 0, &rx, &ry, &child);
    printf("strip %lx mapped %d map_state %d at %d,%d %dx%d (asked %d,%d %dx%d)\n", strip, mapped, wa.map_state, rx, ry, wa.width, wa.height, x, y, w, h);
    for (int i = 1; i + 2 < argc; i++) if (!strcmp(argv[i], "then"))
    {
        int nx = atoi(argv[i+1]), ny = atoi(argv[i+2]);
        XMoveWindow(dpy, strip, nx, ny); XSync(dpy, False); usleep(300000);
        XTranslateCoordinates(dpy, strip, root, 0, 0, &rx, &ry, &child);
        XGetWindowAttributes(dpy, strip, &wa);
        printf("  moved to %d,%d -> at %d,%d map_state %d\n", nx, ny, rx, ry, wa.map_state);
        i += 2;
    }
    if (has(argc, argv, "hold")) sleep(30);
    XCloseDisplay(dpy);
    return 0;
}
