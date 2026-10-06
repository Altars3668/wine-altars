#!/usr/bin/env bash
# office-print-passthrough.sh [--check] - give Office the same printer list as the rest of the prefix.
#
# On Windows, Office never writes the printer keys under
# HKLM\SYSTEM\CurrentControlSet\Control\Print: winspool.drv asks the spooler
# service, and spoolsv.exe writes them.  Under Wine, winspool.drv writes them
# from inside the calling process -- the CUPS printer sync too, which the
# first process of a session to load winspool.drv runs.  In a Click-to-Run
# Office process those writes go through Office's own App-V registry
# virtualization into its copy under
# HKLM\Software\Microsoft\Office\ClickToRun\REGISTRY\MACHINE: Word and the
# rest of the prefix then disagree about the printers, and a printer removed
# from CUPS stays in Word's list.
#
# This adds the Print key to the virtualization's PassThroughPaths, the way
# the Click-to-Run configuration already passes through other keys the
# system owns (crypt32, TCPIP, Xerox's...), removes the virtual copy, and
# lets a process outside Office run the sync once, listing what is left.
# Office must not be running.  An Office update may rewrite PassThroughPaths;
# run this again then (--check says whether it is needed).
set -euo pipefail
unset -f grep 2>/dev/null || true
export WINEPREFIX="${WINEPREFIX:-$HOME/.wine-c2r-up}"
WINE="${WINE:-wine}"
export WINEDEBUG="${WINEDEBUG:--all}"
here=$(cd "$(dirname "$0")/.." && pwd)
vreg='HKLM\Software\Microsoft\Office\ClickToRun\REGISTRY\MACHINE\Software\Microsoft\AppV\Subsystem\VirtualRegistry'
vprint='HKLM\Software\Microsoft\Office\ClickToRun\REGISTRY\MACHINE\SYSTEM\CurrentControlSet\Control\Print'
want='HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Print'
check=0; [ "${1:-}" = "--check" ] && check=1

die() { printf '!! %s\n' "$*" >&2; exit 1; }
[ -f "$WINEPREFIX/system.reg" ] || die "no prefix at $WINEPREFIX"
for p in $(pgrep -x wineserver || true); do
    tr '\0' '\n' < "/proc/$p/environ" 2>/dev/null | grep -qx "WINEPREFIX=$WINEPREFIX" &&
        die "a wineserver is running for $WINEPREFIX; close Office first"
done

# The current list, from the prefix as it is on disk (no wineserver runs).
mapfile -t paths < <(/usr/bin/python3 -I - "$WINEPREFIX/system.reg" <<'PY'
import re, sys
key = r"[Software\\Microsoft\\Office\\ClickToRun\\REGISTRY\\MACHINE\\Software\\Microsoft\\AppV\\Subsystem\\VirtualRegistry]"
inside = False
for line in open(sys.argv[1], encoding="utf-8", errors="surrogateescape"):
    if line.startswith("["):
        inside = line.split("]")[0] + "]" == key
    elif inside and line.startswith('"PassThroughPaths"='):
        m = re.match(r'"PassThroughPaths"=str\(7\):"(.*)"\s*$', line)
        if not m:
            sys.exit("PassThroughPaths is not in the form expected")
        v = m.group(1).replace("\\\\", "\x01").replace("\\0", "\x00").replace("\x01", "\\")
        for p in v.split("\x00"):
            if p:
                print(p)
        break
PY
)
[ ${#paths[@]} -gt 0 ] || die "no PassThroughPaths for Office's App-V registry in $WINEPREFIX"
have=0
for p in "${paths[@]}"; do [ "${p,,}" = "${want,,}" ] && have=1; done
copy=0; grep -qF '[Software\\Microsoft\\Office\\ClickToRun\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Print' \
    "$WINEPREFIX/system.reg" && copy=1
echo "PassThroughPaths has the Print key: $([ $have = 1 ] && echo yes || echo no); virtual copy of it: $([ $copy = 1 ] && echo yes || echo no)"
if [ $check = 1 ]; then [ $have = 1 ] && [ $copy = 0 ]; exit; fi

if [ $have = 0 ]; then
    reg=$(mktemp --suffix=.reg); trap 'rm -f "$reg"' EXIT
    /usr/bin/python3 -I - "$reg" "$vreg" "$want" "${paths[@]}" <<'PY'
import sys
out, key, want, paths = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
data = "\0".join(paths + [want]) + "\0\0"
hexed = ",".join("%02x" % b for b in data.encode("utf-16-le"))
with open(out, "w", encoding="utf-16") as f:
    f.write("Windows Registry Editor Version 5.00\r\n\r\n")
    f.write("[%s]\r\n" % key.replace("HKLM", "HKEY_LOCAL_MACHINE", 1))
    f.write('"PassThroughPaths"=hex(7):%s\r\n' % hexed)
PY
    "$WINE" regedit /S "Z:${reg//\//\\}"
    echo "added $want to PassThroughPaths"
fi
if [ $copy = 1 ]; then
    "$WINE" reg delete "$vprint" /f > /dev/null
    echo "removed Office's virtual copy of the Print key"
fi

# Any process outside Office now runs the sync; printprobe also lists the result.
echo "printers now:"
"$WINE" "$here/tools/printprobe/printprobe.exe" 2>/dev/null | grep -E '^[^ ]' || true
"$WINE" reg query 'HKCU\Software\Microsoft\Windows NT\CurrentVersion\Devices' 2>/dev/null |
    sed -n 's/^    \([^ ]*\) .*/  Devices: \1/p'
wineserver_bin=$(dirname "$(command -v "$WINE")")/wineserver
[ -x "$wineserver_bin" ] && "$wineserver_bin" -w
