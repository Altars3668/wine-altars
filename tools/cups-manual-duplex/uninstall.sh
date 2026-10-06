#!/bin/bash
# uninstall.sh - remove a queue made by install.sh, and the backend once
# nothing uses it.
#
#   uninstall.sh [--name NAME] [--keep-backend]
#
# NAME defaults to the one install.sh recorded (if there is just one).  If
# install.sh made NAME the system default, the old default comes back.  Only
# queues whose device is manualduplex: are touched; the real queue never is.
set -euo pipefail
unset -f grep 2>/dev/null || true
backend=/usr/lib/cups/backend/manualduplex
state_dir=${XDG_STATE_HOME:-$HOME/.local/state}/cups-manual-duplex

name=; keep_backend=0
while [ $# -gt 0 ]; do
    case $1 in
        --name) name=$2; shift 2 ;;
        --keep-backend) keep_backend=1; shift ;;
        -h|--help) sed -n '2,/^set /p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option $1" >&2; exit 1 ;;
    esac
done

say() { printf '==> %s\n' "$*"; }
die() { printf '!! %s\n' "$*" >&2; exit 1; }

if [ -z "$name" ]; then
    mapfile -t recorded < <(ls "$state_dir"/*.env 2>/dev/null)
    [ ${#recorded[@]} = 1 ] || die "say which queue: --name NAME (recorded: ${#recorded[@]})"
    name=$(sed -n 's/^NAME=//p' "${recorded[0]}")
fi

OLD_DEFAULT=; MADE_DEFAULT=0
if [ -f "$state_dir/$name.env" ]; then
    while IFS='=' read -r key value; do
        case $key in
            OLD_DEFAULT) OLD_DEFAULT=$value ;;
            MADE_DEFAULT) MADE_DEFAULT=$value ;;
        esac
    done < "$state_dir/$name.env"
fi

if lpstat -v "$name" > /dev/null 2>&1; then
    case "$(lpstat -v "$name")" in
        *": manualduplex:"*) ;;
        *) die "$name is not a manual-duplex queue; not touching it" ;;
    esac
    current=$(lpstat -d 2>/dev/null | sed -n 's/^system default destination: //p')
    if [ "$current" = "$name" ] && [ "$MADE_DEFAULT" = 1 ] && [ -n "$OLD_DEFAULT" ] &&
       lpstat -v "$OLD_DEFAULT" > /dev/null 2>&1; then
        say "system default back to $OLD_DEFAULT"
        lpadmin -d "$OLD_DEFAULT"
    fi
    say "removing queue $name"
    lpadmin -x "$name"
else
    say "no queue $name"
fi
rm -f "$state_dir/$name.env"

if [ $keep_backend = 0 ] && [ -e "$backend" ]; then
    if lpstat -v 2>/dev/null | grep -q ': manualduplex:'; then
        say "backend kept: other queues still use it"
    else
        say "removing $backend (sudo)"
        sudo rm -f "$backend" || die "could not remove it; run: sudo rm -f $backend"
    fi
fi
say "done"
