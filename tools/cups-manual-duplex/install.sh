#!/bin/bash
# install.sh - a two-sided CUPS queue for a printer without a duplexer.
#
#   install.sh [--target QUEUE] [--name NAME] [--info TEXT] [--default]
#
# Creates a new queue NAME (default: Duplex_<QUEUE>) in front of the real
# queue QUEUE (default: the system default).  Native programs -- Edge,
# Chromium, GTK, LibreOffice -- then offer two-sided printing, long or short
# edge, on NAME; a two-sided job there becomes two jobs on QUEUE, the second
# through the manual feeder, exactly as Wine prints manual duplex (see
# README.md).  QUEUE itself is not changed, so Wine and anything else that
# prints to it behave as before.
#
#   --default   also make NAME the system default (uninstall.sh puts back
#               the old one)
#
# Needs sudo once, to put the backend in /usr/lib/cups/backend; the queue is
# made with lpadmin, which the lpadmin group may use.  Undo: uninstall.sh.
set -euo pipefail
unset -f grep 2>/dev/null || true
here=$(cd "$(dirname "$0")" && pwd)
backend_dir=/usr/lib/cups/backend
state_dir=${XDG_STATE_HOME:-$HOME/.local/state}/cups-manual-duplex

target=; name=; info=; make_default=0
while [ $# -gt 0 ]; do
    case $1 in
        --target) target=$2; shift 2 ;;
        --name) name=$2; shift 2 ;;
        --info) info=$2; shift 2 ;;
        --default) make_default=1; shift ;;
        -h|--help) sed -n '2,/^set /p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option $1" >&2; exit 1 ;;
    esac
done

say() { printf '==> %s\n' "$*"; }
die() { printf '!! %s\n' "$*" >&2; exit 1; }

old_default=$(lpstat -d 2>/dev/null | sed -n 's/^system default destination: //p')
if [ -z "$target" ] && [ -n "$old_default" ]; then
    target=$old_default
    # the default may be a front queue made earlier with --default
    front_of=$(lpstat -v "$target" 2>/dev/null | sed -n 's/^device for [^:]*: manualduplex:\/\([^?]*\).*/\1/p')
    if [ -n "$front_of" ]; then name=${name:-$target}; target=$front_of; fi
fi
[ -n "$target" ] || die "no --target and no system default queue"
lpstat -v "$target" > /dev/null 2>&1 || die "no such queue: $target"
# Edge lists printers by queue name and cuts it to about 25 characters, so
# what tells the queues apart has to come first.
name=${name:-Duplex_${target}}
[ "$name" != "$target" ] || die "--name must differ from --target"
for tool in qpdf lp lpadmin curl /usr/bin/python3; do
    command -v "$tool" > /dev/null || die "missing $tool"
done

work=$(mktemp -d); trap 'rm -rf "$work"' EXIT

say "real queue: $target ($(lpstat -v "$target" | sed 's/^device for [^:]*: //'))"
curl -fsS -o "$work/target.ppd" "http://localhost:631/printers/$target.ppd" 2>/dev/null ||
    curl -fsS --unix-socket /run/cups/cups.sock -o "$work/target.ppd" "http://localhost/printers/$target.ppd" ||
    die "cannot read the PPD of $target"
grep -qE '^\*(InputSlot Manual|ManualFeed True)' "$work/target.ppd" ||
    die "$target has no manual feeder (InputSlot Manual) to hold the second side"
/usr/bin/python3 -I "$here/mkppd.py" "$work/target.ppd" > "$work/front.ppd"

# Keep what the real queue looked like, for reference; it is not changed.
mkdir -p "$state_dir"
cp "$work/target.ppd" "$state_dir/$target.ppd"
lpoptions -p "$target" > "$state_dir/$target.lpoptions" 2>/dev/null || true

if cmp -s "$here/manualduplex" "$backend_dir/manualduplex"; then
    say "backend $backend_dir/manualduplex is current"
else
    say "installing $backend_dir/manualduplex (sudo)"
    sudo install -o root -g root -m 0755 "$here/manualduplex" "$backend_dir/manualduplex" ||
        die "could not install the backend; run: sudo install -o root -g root -m 0755 $here/manualduplex $backend_dir/manualduplex"
fi

if lpstat -v "$name" > /dev/null 2>&1; then
    case "$(lpstat -v "$name")" in
        *": manualduplex:"*) say "$name exists already: updating it" ;;
        *) die "a queue named $name already exists and is not a manual-duplex queue" ;;
    esac
fi
if [ -z "$info" ]; then
    info=$(lpstat -l -p "$target" | sed -n 's/^[[:space:]]*Description: //p' | head -1)
    info=${info% (*}              # "(HP Driver)" and the like no longer apply
    info="${info:-$target} (手动双面)"
fi
location=$(lpstat -l -p "$target" | sed -n 's/^[[:space:]]*Location: //p' | head -1)
say "queue $name -> manualduplex:/$target"
# (lpadmin warns that PPD files are deprecated; CUPS 2.x still uses them)
if ! out=$(lpadmin -p "$name" -E -v "manualduplex:/$target" -P "$work/front.ppd" \
               -D "$info" -L "${location:-}" -o printer-is-shared=false 2>&1); then
    die "lpadmin failed: $out"
fi
printf '%s\n' "$out" | grep -v -e 'Printer drivers are deprecated' -e '^$' >&2 || true
lpstat -v "$name" > /dev/null 2>&1 || die "lpadmin did not create $name"

# Run again over an earlier --default install: keep what the default was
# before that, or uninstall.sh could not put it back.
made_default=$make_default
if [ -f "$state_dir/$name.env" ] && grep -qx 'MADE_DEFAULT=1' "$state_dir/$name.env"; then
    old_default=$(sed -n 's/^OLD_DEFAULT=//p' "$state_dir/$name.env")
    made_default=1
fi
{
    echo "TARGET=$target"
    echo "NAME=$name"
    echo "OLD_DEFAULT=$old_default"
    echo "MADE_DEFAULT=$made_default"
    echo "DATE=$(date -Iseconds)"
} > "$state_dir/$name.env"
# so that undoing does not depend on this checkout still being here
install -m 0755 "$here/uninstall.sh" "$state_dir/uninstall.sh"

if [ $make_default = 1 ]; then
    say "making $name the system default (was $old_default)"
    lpadmin -d "$name"
fi

say "result"
lpstat -v "$name" | sed 's/^/    /'
lpoptions -p "$name" -l | grep -E '^Duplex' | sed 's/^/    /'
ipptool -tv "ipp://localhost/printers/$name" get-printer-attributes.test 2>/dev/null |
    grep -E '^\s+sides-supported' | sed 's/^ */    /' || true
echo "    state and a copy of $target's PPD: $state_dir"
echo "    undo: $state_dir/uninstall.sh --name $name"
