#!/usr/bin/env python3
"""Read an X window's real pixels, including its alpha channel.

Everything this project has measured about "the window is black" until now
read only RGB.  Under a compositor that is half the picture: a 32-bit ARGB
window whose alpha is zero holds perfectly correct colours and is shown as
nothing at all, and "nothing at all" over a dark parent looks exactly like a
black rectangle.  So this reports depth, visual and the alpha histogram
alongside the colour count, and it does it straight through Xlib -- no code
from the tree under test is involved in the answer.

    scripts/xwin-pixels.py --tree                 # every window, with depth
    scripts/xwin-pixels.py 0x1c00006              # one window, in detail
    scripts/xwin-pixels.py --match NUIDialog      # by class or name
"""
import argparse, ctypes, ctypes.util, os, sys
from collections import Counter
import numpy as np

X = ctypes.CDLL(ctypes.util.find_library("X11"))

ZPixmap, AllPlanes = 2, (1 << 32) - 1
IsUnmapped, IsUnviewable, IsViewable = 0, 1, 2
MAP_STATE = {0: "unmapped", 1: "unviewable", 2: "viewable"}


class XWindowAttributes(ctypes.Structure):
    _fields_ = [("x", ctypes.c_int), ("y", ctypes.c_int),
                ("width", ctypes.c_int), ("height", ctypes.c_int),
                ("border_width", ctypes.c_int), ("depth", ctypes.c_int),
                ("visual", ctypes.c_void_p), ("root", ctypes.c_ulong),
                ("c_class", ctypes.c_int), ("bit_gravity", ctypes.c_int),
                ("win_gravity", ctypes.c_int), ("backing_store", ctypes.c_int),
                ("backing_planes", ctypes.c_ulong), ("backing_pixel", ctypes.c_ulong),
                ("save_under", ctypes.c_int), ("colormap", ctypes.c_ulong),
                ("map_installed", ctypes.c_int), ("map_state", ctypes.c_int),
                ("all_event_masks", ctypes.c_long), ("your_event_mask", ctypes.c_long),
                ("do_not_propagate_mask", ctypes.c_long),
                ("override_redirect", ctypes.c_int), ("screen", ctypes.c_void_p)]


class XImage(ctypes.Structure):
    _fields_ = [("width", ctypes.c_int), ("height", ctypes.c_int),
                ("xoffset", ctypes.c_int), ("format", ctypes.c_int),
                ("data", ctypes.c_void_p), ("byte_order", ctypes.c_int),
                ("bitmap_unit", ctypes.c_int), ("bitmap_bit_order", ctypes.c_int),
                ("bitmap_pad", ctypes.c_int), ("depth", ctypes.c_int),
                ("bytes_per_line", ctypes.c_int), ("bits_per_pixel", ctypes.c_int),
                ("red_mask", ctypes.c_ulong), ("green_mask", ctypes.c_ulong),
                ("blue_mask", ctypes.c_ulong), ("obdata", ctypes.c_void_p),
                ("f", ctypes.c_void_p * 6)]


class XClassHint(ctypes.Structure):
    _fields_ = [("res_name", ctypes.c_char_p), ("res_class", ctypes.c_char_p)]


X.XOpenDisplay.restype = ctypes.c_void_p
X.XDefaultRootWindow.restype = ctypes.c_ulong
X.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
X.XGetImage.restype = ctypes.POINTER(XImage)
X.XGetImage.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_int,
                        ctypes.c_uint, ctypes.c_uint, ctypes.c_ulong, ctypes.c_int]
X.XGetWindowAttributes.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
                                   ctypes.POINTER(XWindowAttributes)]
X.XFetchName.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
                         ctypes.POINTER(ctypes.c_char_p)]
X.XGetClassHint.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
                            ctypes.POINTER(XClassHint)]
X.XQueryTree.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
                         ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_ulong),
                         ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)),
                         ctypes.POINTER(ctypes.c_uint)]
X.XTranslateCoordinates.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong,
                                    ctypes.c_int, ctypes.c_int,
                                    ctypes.POINTER(ctypes.c_int),
                                    ctypes.POINTER(ctypes.c_int),
                                    ctypes.POINTER(ctypes.c_ulong)]
X.XVisualIDFromVisual.restype = ctypes.c_ulong
X.XVisualIDFromVisual.argtypes = [ctypes.c_void_p]


def open_display(name=None):
    d = X.XOpenDisplay((name or os.environ.get("DISPLAY", ":0")).encode())
    if not d:
        sys.exit("cannot open display %s" % (name or os.environ.get("DISPLAY")))
    return d


def attrs(dpy, win):
    a = XWindowAttributes()
    if not X.XGetWindowAttributes(dpy, win, ctypes.byref(a)):
        return None
    return a


def name_of(dpy, win):
    p = ctypes.c_char_p()
    n = X.XFetchName(dpy, win, ctypes.byref(p)) and p.value or b""
    ch = XClassHint()
    cls = b""
    if X.XGetClassHint(dpy, win, ctypes.byref(ch)):
        cls = ch.res_class or b""
    return n.decode("utf8", "replace"), cls.decode("utf8", "replace")


def children(dpy, win):
    root = ctypes.c_ulong(); parent = ctypes.c_ulong()
    kids = ctypes.POINTER(ctypes.c_ulong)(); n = ctypes.c_uint()
    if not X.XQueryTree(dpy, win, ctypes.byref(root), ctypes.byref(parent),
                        ctypes.byref(kids), ctypes.byref(n)):
        return []
    return [kids[i] for i in range(n.value)]


def abs_pos(dpy, win, root):
    x = ctypes.c_int(); y = ctypes.c_int(); child = ctypes.c_ulong()
    X.XTranslateCoordinates(dpy, win, root, 0, 0,
                            ctypes.byref(x), ctypes.byref(y), ctypes.byref(child))
    return x.value, y.value


def read_pixels(dpy, win, a, region=None):
    """-> (rgb Counter, alpha Counter, npixels) or None if unreadable."""
    if a.width <= 0 or a.height <= 0:
        return None
    x, y, w, h = region if region else (0, 0, a.width, a.height)
    if x + w > a.width or y + h > a.height:
        return None
    img = X.XGetImage(dpy, win, x, y, w, h, AllPlanes, ZPixmap)
    if not img:
        return None
    im = img.contents
    bpp = im.bits_per_pixel // 8
    buf = np.frombuffer(ctypes.string_at(im.data, im.bytes_per_line * im.height),
                        dtype=np.uint8)
    buf = buf.reshape(im.height, im.bytes_per_line)[:, :im.width * bpp]
    px = buf.reshape(im.height, im.width, bpp)
    if bpp == 4:
        b, g, r, al = px[..., 0], px[..., 1], px[..., 2], px[..., 3]
    elif bpp == 3:
        b, g, r = px[..., 0], px[..., 1], px[..., 2]
        al = None
    else:
        return None
    key = (r.astype(np.uint32) << 16) | (g.astype(np.uint32) << 8) | b
    vals, counts = np.unique(key, return_counts=True)
    rgb = Counter({((int(v) >> 16) & 255, (int(v) >> 8) & 255, int(v) & 255): int(c)
                   for v, c in zip(vals, counts)})
    alpha = Counter()
    if al is not None:
        av, ac = np.unique(al, return_counts=True)
        alpha = Counter({int(v): int(c) for v, c in zip(av, ac)})
    return rgb, alpha, im.width * im.height


def black_map(dpy, win, a):
    """Row/column runs that are >=90% pure black -- where a black band is."""
    img = X.XGetImage(dpy, win, 0, 0, a.width, a.height, AllPlanes, ZPixmap)
    if not img:
        return None
    im = img.contents
    bpp = im.bits_per_pixel // 8
    buf = np.frombuffer(ctypes.string_at(im.data, im.bytes_per_line * im.height),
                        dtype=np.uint8)
    buf = buf.reshape(im.height, im.bytes_per_line)[:, :im.width * bpp]
    px = buf.reshape(im.height, im.width, bpp)
    black = (px[..., 0] == 0) & (px[..., 1] == 0) & (px[..., 2] == 0)
    return runs(black.mean(axis=1) >= 0.9), runs(black.mean(axis=0) >= 0.9)


def runs(mask):
    out, s = [], None
    for i, v in enumerate(mask):
        if v and s is None:
            s = i
        if not v and s is not None:
            out.append((s, i - 1)); s = None
    if s is not None:
        out.append((s, len(mask) - 1))
    return out


def describe(dpy, win, root, deep=False, indent=0, region=None):
    a = attrs(dpy, win)
    if a is None:
        return
    nm, cls = name_of(dpy, win)
    ax, ay = abs_pos(dpy, win, root)
    vid = X.XVisualIDFromVisual(a.visual) if a.visual else 0
    head = ("%s0x%-9x %-16s %-22s %4dx%-4d @%5d,%-5d depth=%-2d vis=0x%-3x %-9s%s"
            % (" " * indent, win, cls[:16], nm[:22], a.width, a.height, ax, ay,
               a.depth, vid, MAP_STATE.get(a.map_state, "?"),
               " ovr" if a.override_redirect else ""))
    print(head)
    if not deep or a.map_state != IsViewable:
        return
    got = read_pixels(dpy, win, a, region)
    if not got:
        print("%s  (unreadable)" % (" " * indent))
        return
    rgb, alpha, n = got
    black = rgb.get((0, 0, 0), 0)
    top = ", ".join("%s x%d" % (c, k) for c, k in rgb.most_common(3))
    print("%s  rgb: %d distinct, black %d/%d (%.0f%%), top: %s"
          % (" " * indent, len(rgb), black, n, 100.0 * black / n, top))
    bm = black_map(dpy, win, a)
    if bm and (bm[0] or bm[1]):
        print("%s  solid-black bands: rows %s cols %s" % (" " * indent, bm[0], bm[1]))
    if alpha:
        opaque = alpha.get(255, 0)
        print("%s  alpha: %d distinct, 255 on %d/%d (%.0f%%), 0 on %d (%.0f%%), top: %s"
              % (" " * indent, len(alpha), opaque, n, 100.0 * opaque / n,
                 alpha.get(0, 0), 100.0 * alpha.get(0, 0) / n,
                 ", ".join("a=%d x%d" % (v, k) for v, k in alpha.most_common(3))))


def walk(dpy, win, root, depth, maxdepth, want, deep):
    a = attrs(dpy, win)
    if a is None:
        return
    nm, cls = name_of(dpy, win)
    hit = want is None or want.lower() in (nm + " " + cls).lower() or want == hex(win)
    if hit:
        describe(dpy, win, root, deep=deep, indent=depth * 2)
    if depth < maxdepth:
        for k in children(dpy, win):
            walk(dpy, k, root, depth + 1, maxdepth, want, deep)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("window", nargs="?", help="window id, e.g. 0x1c00006")
    ap.add_argument("--display", default=None)
    ap.add_argument("--tree", action="store_true", help="walk the whole tree")
    ap.add_argument("--match", help="only windows whose name/class contains this")
    ap.add_argument("--depth", type=int, default=6)
    ap.add_argument("--pixels", action="store_true", help="read pixels for every hit")
    ap.add_argument("--region", help="x,y,w,h -- read only this part of the window")
    args = ap.parse_args()

    dpy = open_display(args.display)
    root = X.XDefaultRootWindow(dpy)
    region = tuple(int(v) for v in args.region.split(",")) if args.region else None
    if args.window:
        describe(dpy, int(args.window, 0), root, deep=True, region=region)
    else:
        walk(dpy, root, root, 0, args.depth, args.match,
             deep=args.pixels or bool(args.match))


if __name__ == "__main__":
    main()
