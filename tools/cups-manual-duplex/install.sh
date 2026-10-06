#!/bin/bash
# install.sh - two-sided printing on a printer without a duplexer, on its own queue.
#
#   install.sh [--target QUEUE] [--uri DEVICE-URI]
#
# Changes QUEUE (default: the system default) in place.  Its PPD gets a
# Duplex option and the manualduplex pre-filter (mkppd.py): Edge, Chromium,
# GTK, LibreOffice and Word under Wine then offer two-sided printing, long or
# short edge, and a two-sided job prints the fronts, then the backs through
# the manual feeder, exactly as Wine prints manual duplex (see README.md).
# The name, paper, trays, defaults and description of the queue stay.
#
# Its device URI becomes the printer's dnssd:// address (or DEVICE-URI).
# With it, cups-browsed and libcups know that this queue is that printer:
# cups-browsed makes no queue of its own for it, and print dialogs do not list
# it a second time from the network.  One printer is shown as one printer.
#
# An earlier version of this tool made a second queue in front of QUEUE;
# that queue and its backend are removed.
#
# Needs sudo once, to put the filter in /usr/lib/cups/filter.  The original
# PPD and device URI are kept in ~/.local/state/cups-manual-duplex/; undo
# with uninstall.sh.
set -euo pipefail
unset -f grep 2>/dev/null || true
here=$(cd "$(dirname "$0")" && pwd)
filter=/usr/lib/cups/filter/manualduplex
old_backend=/usr/lib/cups/backend/manualduplex
state_dir=${XDG_STATE_HOME:-$HOME/.local/state}/cups-manual-duplex

target=; uri=
while [ $# -gt 0 ]; do
    case $1 in
        --target) target=$2; shift 2 ;;
        --uri) uri=$2; shift 2 ;;
        -h|--help) sed -n '2,/^set /p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option $1" >&2; exit 1 ;;
    esac
done

say() { printf '==> %s\n' "$*"; }
die() { printf '!! %s\n' "$*" >&2; exit 1; }
device_of() { lpstat -v "$1" 2>/dev/null | sed -n 's/^device for [^:]*: //p'; }
ppd_of() {      # ppd_of <queue> <file>
    curl -fsS -o "$2" "http://localhost:631/printers/$1.ppd" 2>/dev/null ||
        curl -fsS --unix-socket /run/cups/cups.sock -o "$2" "http://localhost/printers/$1.ppd"
}
service_of() {  # service_of <dnssd uri>: the DNS-SD instance name in it
    /usr/bin/python3 -I -c 'import sys, urllib.parse as u
h = u.urlsplit(sys.argv[1]).netloc
print(u.unquote(h.split("._ipp")[0]))' "$1"
}
queue_name_of() {   # what libcups and cups-browsed call a queue for a DNS-SD name
    /usr/bin/python3 -I -c 'import sys
n = ""
for c in sys.argv[1]:
    n += c if c.isascii() and c.isalnum() else ("" if n.endswith("_") else "_")
print(n[:-1] if len(n) > 1 and n.endswith("_") else n)' "$1"
}

for tool in qpdf lp lpadmin lpinfo ippfind ipptool curl /usr/bin/python3; do
    command -v "$tool" > /dev/null || die "missing $tool"
done

[ -n "$target" ] || target=$(lpstat -d 2>/dev/null | sed -n 's/^system default destination: //p')
[ -n "$target" ] || die "no --target and no system default queue"
lpstat -v "$target" > /dev/null 2>&1 || die "no such queue: $target"
case "$(device_of "$target")" in
    manualduplex:/*)    # a front queue of the earlier version: use what it fronted
        t=$(device_of "$target"); t=${t#manualduplex:/}; target=${t%%\?*}
        lpstat -v "$target" > /dev/null 2>&1 || die "no such queue: $target" ;;
esac
mapfile -t fronts < <(lpstat -v 2>/dev/null | awk -v t="manualduplex:/$target" \
    '{ d = $NF; sub(/\?.*/, "", d); if (d == t) { n = $3; sub(/:$/, "", n); print n } }')

mkdir -p "$state_dir"
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT
ppd_of "$target" "$work/current.ppd" || die "cannot read the PPD of $target"
if grep -q '^\*% manualduplex:' "$work/current.ppd"; then
    # converted before: start again from what it was then
    [ -f "$state_dir/$target.ppd" ] && [ -f "$state_dir/$target.env" ] ||
        die "$target was converted before, but its original PPD is not in $state_dir"
    orig_uri=$(sed -n 's/^ORIG_URI=//p' "$state_dir/$target.env")
else
    cp "$work/current.ppd" "$state_dir/$target.ppd"
    orig_uri=$(device_of "$target")
    lpoptions -p "$target" > "$state_dir/$target.lpoptions" 2>/dev/null || true
fi
[ -n "$orig_uri" ] || die "no original device URI for $target"
/usr/bin/python3 -I "$here/mkppd.py" "$state_dir/$target.ppd" > "$work/queue.ppd" ||
    die "mkppd.py refused the PPD of $target"

if [ -z "$uri" ]; then
    case $orig_uri in
        dnssd://*) uri=$orig_uri ;;
        *)  # find the printer's DNS-SD address by its UUID
            uuid=$(ipptool -tv "$orig_uri" get-printer-attributes.test 2>/dev/null |
                   sed -nE 's/^ +printer-uuid \(uri\) = urn:uuid:([^ ]+)$/\1/p' | head -1 || true)
            if [ -n "$uuid" ]; then
                uri=$(lpinfo --include-schemes dnssd -v 2>/dev/null | awk '{ print $2 }' |
                      grep -F "uuid=$uuid" | head -1 || true)
            fi ;;
    esac
    # libcups lists the printer under IPPS when it offers IPPS, and only
    # recognises a queue as that printer when the queue says IPPS too
    if [[ $uri == dnssd://*._ipp._tcp.* ]] &&
       ippfind -T 5 _ipps._tcp -N "$(service_of "$uri")" -q 2>/dev/null; then
        uri=${uri/._ipp._tcp./._ipps._tcp.}
    fi
    if [ -z "$uri" ]; then
        uri=$orig_uri
        say "no dnssd:// address found for the printer: keeping $uri (dialogs may list the printer twice)"
    fi
fi

if cmp -s "$here/manualduplex" "$filter"; then
    say "filter $filter is current"
else
    say "installing $filter (sudo)"
    sudo install -o root -g root -m 0755 "$here/manualduplex" "$filter" ||
        die "could not install the filter; run: sudo install -o root -g root -m 0755 $here/manualduplex $filter"
fi

say "queue $target: $(device_of "$target") -> $uri"
# (lpadmin warns that PPD files are deprecated; CUPS 2.x still uses them)
if ! out=$(lpadmin -p "$target" -v "$uri" -P "$work/queue.ppd" 2>&1); then
    die "lpadmin failed: $out"
fi
printf '%s\n' "$out" | grep -v -e 'Printer drivers are deprecated' -e '^$' >&2 || true
{
    echo "TARGET=$target"
    echo "ORIG_URI=$orig_uri"
    echo "URI=$uri"
    echo "DATE=$(date -Iseconds)"
} > "$state_dir/$target.env"
# so that undoing does not depend on this checkout still being here
install -m 0755 "$here/uninstall.sh" "$state_dir/uninstall.sh"

default=$(lpstat -d 2>/dev/null | sed -n 's/^system default destination: //p')
for q in "${fronts[@]}"; do
    say "removing $q, the front queue of the earlier version"
    lpadmin -x "$q"
    rm -f "$state_dir/$q.env"
    [ "$default" != "$q" ] || { say "system default: $target"; lpadmin -d "$target"; }
done
if [ -e "$old_backend" ] && ! lpstat -v 2>/dev/null | grep -q ': manualduplex:'; then
    say "removing $old_backend, the backend of the earlier version (sudo)"
    sudo rm -f "$old_backend" || say "could not remove $old_backend; nothing uses it"
fi
# cups-browsed made a queue of its own for the printer while ours pointed at
# an address it did not recognise; now that ours names the printer, it goes
if [[ $uri == dnssd://* ]]; then
    cb=$(queue_name_of "$(service_of "$uri")")
    if [ "$cb" != "$target" ] && [ "$(device_of "$cb")" = "implicitclass://$cb/" ]; then
        say "removing $cb, cups-browsed's queue for the same printer"
        lpadmin -x "$cb"
    fi
fi

say "result"
lpstat -v "$target" | sed 's/^/    /'
lpoptions -p "$target" -l | grep -E '^Duplex' | sed 's/^/    /'
ipptool -tv "ipp://localhost/printers/$target" get-printer-attributes.test 2>/dev/null |
    grep -E '^\s+sides-supported' | sed 's/^ */    /' || true
echo "    what print dialogs offer (lpstat -e, network included):"
lpstat -e | sed 's/^/      /'
echo "    original PPD and device URI: $state_dir"
echo "    undo: $state_dir/uninstall.sh"
