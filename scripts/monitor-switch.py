#!/usr/bin/env python3
"""Leave one monitor of a mutter enabled, the way a remote desktop swaps its virtual monitor.

gnome-remote-desktop turns a phone from portrait to landscape by taking one
virtual monitor down and putting another up; the X server sees an output go
and a new one arrive.  A mutter started with several virtual monitors
(scripts/measure-desktop.sh with MEASURE_GEOM=2992x1440,1440x2992) does the
same when all but one are switched off, which is what this asks it to do,
as a temporary configuration through org.gnome.Mutter.DisplayConfig.

    scripts/monitor-switch.py Meta-1          # only Meta-1, at (0,0), primary
    scripts/monitor-switch.py Meta-0 2        # only Meta-0, at scale 2

Use the bus of the mutter meant: measure-desktop.sh prints it.
"""
import sys

import gi
gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib

bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)


def call(method, args=None, sig=None):
    return bus.call_sync("org.gnome.Mutter.DisplayConfig", "/org/gnome/Mutter/DisplayConfig",
                         "org.gnome.Mutter.DisplayConfig", method,
                         GLib.Variant(sig, args) if sig else None, None,
                         Gio.DBusCallFlags.NONE, -1, None)


serial, monitors, logical, props = call("GetCurrentState").unpack()
want = sys.argv[1]
scale = float(sys.argv[2]) if len(sys.argv) > 2 else 1.0
for (connector, vendor, product, ser), modes, mprops in monitors:
    if connector != want:
        continue
    mode = next((m for m in modes if m[6].get("is-current")), modes[0])
    config = [(0, 0, scale, 0, True, [(connector, mode[0], {})])]
    call("ApplyMonitorsConfig", (serial, 1, config, {}), "(uua(iiduba(ssa{sv}))a{sv})")
    print("applied", connector, mode[0], "scale", scale)
    break
else:
    sys.exit("no connector %s among %s" % (want, [m[0][0] for m in monitors]))
