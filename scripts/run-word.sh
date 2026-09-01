#!/usr/bin/env bash
# Start Word in the prefix from a clean slate.
#
# Word decides "the last run failed, offer safe mode" from the per-session
# directories it leaves in %TEMP% -- every crashed run leaves one, and after
# the first crash every later run stops on a modal prompt before it gets
# anywhere. The Resiliency key is the documented switch but it is not the one
# that drives this; the leftover session directories are. Both go.
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="${DIST:-$ROOT/dist-cx}"
export WINEPREFIX="${WINEPREFIX:-$HOME/.wine-altars-office}"
export PATH="$DIST/bin:$PATH"
LOG="${LOG:-/tmp/word.log}"
APP="${APP:-C:\\Program Files\\Microsoft Office\\root\\Office16\\WINWORD.EXE}"

"$DIST/bin/wineserver" -k 2>/dev/null
for _ in 1 2 3; do sleep 1; done

TEMP_DIR="$WINEPREFIX/drive_c/users/$USER/AppData/Local/Temp"
n=0
for d in "$TEMP_DIR"/{*-*-*-*-*}/; do
    [ -d "$d" ] || continue
    rm -rf "$d" && n=$((n+1))
done
rm -f "$TEMP_DIR"/*.log
echo "cleared $n stale Office session dirs"

WINEDEBUG=-all "$DIST/bin/wine" reg delete \
    "HKCU\\Software\\Microsoft\\Office\\16.0\\Word\\Resiliency" /f >/dev/null 2>&1
WINEDEBUG=-all "$DIST/bin/wine" reg add "HKCU\\Software\\Wine\\WineDbg" \
    /v ShowCrashDialog /t REG_DWORD /d 0 /f >/dev/null 2>&1

export WINEDEBUG="${WINEDEBUG:--all,+loaddll,err+all}"
echo "log -> $LOG"
exec "$DIST/bin/wine" "$@" "$APP"
