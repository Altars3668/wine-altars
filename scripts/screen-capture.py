#!/usr/bin/env python3
"""Read what the compositor actually shows, one frame at a time.

Under Xwayland the X11 root window holds no other client's pixels, so
`import -window root` and `xwd -root` -- the instrument most of this project's
earlier measurements used -- cannot answer "what is on the screen" at all, and
GNOME Shell's own org.gnome.Shell.Screenshot answers AccessDenied to a caller
it does not recognise as an app.  mutter's screen-cast interface has no such
check: it hands out a PipeWire stream of the composited output, which is the
one read that includes everything the compositor did.

Pair it with scripts/measure-desktop.sh, which starts a mutter that has a
monitor to cast.  Against a logged-in session it works too, as long as that
session's mutter is on the bus this process can see.

    scripts/screen-capture.py out.png            # the virtual monitor
    scripts/screen-capture.py out.png HDMI-1     # a named connector
"""
import os
import subprocess
import sys

import gi
gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib

BUS = "org.gnome.Mutter.ScreenCast"
ROOT = "/org/gnome/Mutter/ScreenCast"


def capture(dest, connector="Meta-0", timeout=20):
    bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)

    def call(path, iface, method, args=None, sig=None):
        params = GLib.Variant(sig, args) if sig else None
        return bus.call_sync(BUS, path, iface, method, params, None,
                             Gio.DBusCallFlags.NONE, -1, None)

    session = call(ROOT, BUS, "CreateSession", ({},), "(a{sv})")[0]
    stream = call(session, BUS + ".Session", "RecordMonitor",
                  (connector, {"cursor-mode": GLib.Variant("u", 1)}), "(sa{sv})")[0]

    node = {}
    loop = GLib.MainLoop()
    bus.signal_subscribe(BUS, BUS + ".Stream", "PipeWireStreamAdded", stream, None,
                         Gio.DBusSignalFlags.NONE,
                         lambda *a: (node.__setitem__("id", a[5][0]), loop.quit()))
    call(session, BUS + ".Session", "Start")
    GLib.timeout_add_seconds(timeout, loop.quit)
    loop.run()
    if "id" not in node:
        raise RuntimeError("mutter never announced a PipeWire stream for %r" % connector)

    # One buffer: with a static screen mutter produces frames on damage, so
    # asking for several can wait forever for a change that never comes.
    tmp = dest + ".0"
    proc = subprocess.run(
        ["timeout", str(timeout), "gst-launch-1.0", "-q",
         "pipewiresrc", "path=%d" % node["id"], "num-buffers=1", "!",
         "videoconvert", "!", "pngenc", "snapshot=false", "!",
         "multifilesink", "location=%s.%%d" % dest],
        capture_output=True, text=True)
    call(session, BUS + ".Session", "Stop")

    if not os.path.exists(tmp):
        raise RuntimeError("gst-launch produced nothing: %s%s" % (proc.stdout, proc.stderr))
    os.replace(tmp, dest)
    return dest


if __name__ == "__main__":
    out = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "screen.png")
    print(capture(out, sys.argv[2] if len(sys.argv) > 2 else "Meta-0"))
