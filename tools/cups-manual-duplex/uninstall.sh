#!/bin/bash
# uninstall.sh - put back the queue install.sh changed, and remove the
# filter once no queue uses it.
#
#   uninstall.sh [--target QUEUE] [--keep-filter]
#
# QUEUE defaults to the one install.sh recorded (if there is just one).  Its
# original PPD and device URI come back from ~/.local/state/cups-manual-duplex/.
# cups-browsed may then make a queue of its own for the printer again, as it
# did before.
set -euo pipefail
unset -f grep 2>/dev/null || true
filter=/usr/lib/cups/filter/manualduplex
state_dir=${XDG_STATE_HOME:-$HOME/.local/state}/cups-manual-duplex

target=; keep_filter=0
while [ $# -gt 0 ]; do
    case $1 in
        --target) target=$2; shift 2 ;;
        --keep-filter) keep_filter=1; shift ;;
        -h|--help) sed -n '2,/^set /p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 0 ;;
        *) echo "unknown option $1" >&2; exit 1 ;;
    esac
done

say() { printf '==> %s\n' "$*"; }
die() { printf '!! %s\n' "$*" >&2; exit 1; }
converted() {   # converted <queue>: its PPD is one mkppd.py made
    curl -fsS "http://localhost:631/printers/$1.ppd" 2>/dev/null | grep -q '^\*% manualduplex:'
}

if [ -z "$target" ]; then
    mapfile -t recorded < <(grep -l '^TARGET=' "$state_dir"/*.env 2>/dev/null)
    [ ${#recorded[@]} = 1 ] || die "say which queue: --target QUEUE (recorded: ${#recorded[@]})"
    target=$(sed -n 's/^TARGET=//p' "${recorded[0]}")
fi

if converted "$target"; then
    orig_uri=$(sed -n 's/^ORIG_URI=//p' "$state_dir/$target.env" 2>/dev/null || true)
    [ -n "$orig_uri" ] && [ -f "$state_dir/$target.ppd" ] ||
        die "the original PPD or device URI of $target is not in $state_dir"
    say "queue $target: back to $orig_uri and its original PPD"
    if ! out=$(lpadmin -p "$target" -v "$orig_uri" -P "$state_dir/$target.ppd" 2>&1); then
        die "lpadmin failed: $out"
    fi
    printf '%s\n' "$out" | grep -v -e 'Printer drivers are deprecated' -e '^$' >&2 || true
else
    say "$target is not converted; nothing to put back"
fi
rm -f "$state_dir/$target.env"

if [ $keep_filter = 0 ] && [ -e "$filter" ]; then
    used=0
    for q in $(lpstat -v 2>/dev/null | awk '{ n = $3; sub(/:$/, "", n); print n }'); do
        if converted "$q"; then used=1; fi
    done
    if [ $used = 1 ]; then
        say "filter kept: other queues still use it"
    else
        say "removing $filter (sudo)"
        sudo rm -f "$filter" || die "could not remove it; run: sudo rm -f $filter"
    fi
fi
say "done"
