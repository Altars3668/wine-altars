#!/bin/bash
# duplex-capture.sh - see what a two-sided job from Wine puts on each sheet,
# without paper.
#
#   duplex-capture.sh <wine> <prefix> <duplex 1|2|3> [pages] [ppd]
#
# Starts a fake IPP printer (ippeveprinter) and a temporary CUPS queue that
# uses the real printer's PPD -- so the job goes through the same filters and
# the same job-ticket handling as the real one -- prints tools/duptest from the
# given Wine and prefix, and decodes the URF the printer would have received:
# for each job, each sheet's page number (count of squares), blank, and
# whether it is upright or turned.  Everything is removed afterwards.
#
# The PPD defaults to the one CUPS serves for the default queue.  Needs the
# lpadmin group for the temporary queue.
set -u
here=$(cd "$(dirname "$0")/.." && pwd)
wine=$1; prefix=$2; duplex=$3; pages=${4:-5}; ppd=${5:-}
port=8633; queue=duplexcapture
work=$(mktemp -d); trap 'lpadmin -x $queue 2>/dev/null; [ -n "${fake:-}" ] && kill $fake 2>/dev/null; rm -rf "$work"' EXIT
server=$(dirname "$wine")/wineserver; [ -x "$server" ] || server=$(dirname "$wine")/server/wineserver

if [ -z "$ppd" ]; then
    dest=$(lpstat -d 2>/dev/null | sed -n 's/.*: //p')
    curl -sf "http://localhost:631/printers/$dest.ppd" -o "$work/printer.ppd" || { echo "no PPD for $dest" >&2; exit 1; }
    ppd=$work/printer.ppd
fi

mkdir "$work/spool"
ippeveprinter -p $port -f image/urf,image/pwg-raster,application/pdf -k -d "$work/spool" -c /bin/true "$queue-fake" \
    > "$work/fake.log" 2>&1 & fake=$!
for i in $(seq 1 20); do ipptool -q ipp://localhost:$port/ipp/print get-printer-attributes.test 2>/dev/null && break; sleep 0.5; done
lpadmin -p $queue -E -v ipp://localhost:$port/ipp/print -P "$ppd" 2>/dev/null || { echo "lpadmin failed" >&2; exit 1; }

cp "$here/tools/duptest/duptest.exe" "$prefix/drive_c/duptest.exe"
env -u DISPLAY -u WAYLAND_DISPLAY WINEPREFIX="$prefix" WINEDEBUG=-all "$wine" 'C:\duptest.exe' $queue "$duplex" "$pages" 2>&1 | tr -d '\r'
for i in $(seq 1 120); do
    [ -z "$(lpstat -o $queue 2>/dev/null)" ] && [ -n "$(ls "$work/spool"/*.urf 2>/dev/null)" ] && break
    sleep 0.5
done
sleep 2
WINEPREFIX="$prefix" "$server" -k 2>/dev/null
(cd "$work/spool" && python3 "$here/scripts/urf-pages.py" $(ls -tr *.urf))
