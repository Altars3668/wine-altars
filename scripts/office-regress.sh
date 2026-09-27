#!/bin/bash
# office-regress.sh - run the Word, Excel and PowerPoint save probes one after
# another and check what each of them wrote.
#
#   office-regress.sh [app...]     apps: word excel powerpoint (default: all)
#
# Each probe drives the real application through COM in the Office prefix
# (see tools/officeautomationprobe/README.md), saves a local file and quits.
# A result is a line per application: the probe's exit code, whether the file
# is a valid OOXML package with the part the probe edited, and the steps the
# probe reached.  The exit code is the last failing probe's, or 0.
#
# OFFICE_DEBUG_DISPLAY picks the X display (default 2), WINEPREFIX the prefix
# (default ~/.wine-c2r-test).  The next application starts once the previous
# one's process has gone: the Click-to-Run service keeps the wineserver itself
# running, so waiting for that would wait for ever.

set -u
here=$(cd "$(dirname "$0")/.." && pwd)
export OFFICE_DEBUG_DISPLAY=${OFFICE_DEBUG_DISPLAY:-2}
out=$(mktemp -d -p /tmp wine-altars-regress-XXXXXX) || exit 1
apps=("$@")
[ ${#apps[@]} -gt 0 ] || apps=(word excel powerpoint)

win() { python3 -c 'import sys; print("Z:" + sys.argv[1].replace("/", chr(92)))' "$1"; }

status=0
for name in "${apps[@]}"; do
    case $name in
    word)       ext=docx; part=word/document.xml;        exe=WINWORD ;;
    excel)      ext=xlsx; part=xl/worksheets/sheet1.xml; exe=EXCEL ;;
    powerpoint) ext=pptx; part=ppt/slides/slide1.xml;    exe=POWERPNT ;;
    *) echo "office-regress: unknown application $name" >&2; exit 2 ;;
    esac
    timeout 240 "$here/scripts/office-debug.sh" run cscript.exe //nologo \
        "$(win "$here/tools/officeautomationprobe/$name-save.vbs")" \
        "$(win "$out")\\result.$ext" "$(win "$out")\\$name.progress" > "$out/$name.out" 2>&1
    rc=$?
    python3 - "$out" "$name" "$ext" "$part" "$rc" <<'PY'
from pathlib import Path
from zipfile import BadZipFile, ZipFile
import sys
root, name, ext, part, rc = Path(sys.argv[1]), *sys.argv[2:]
progress = root / f'{name}.progress'
steps = progress.read_text(errors='replace').replace('\r', '').split('\n') if progress.exists() else ['no progress']
path = root / f'result.{ext}'
state = 'missing'
if path.exists():
    try:
        with ZipFile(path) as z:
            state = 'valid' if z.testzip() is None and part in z.namelist() else 'invalid'
    except BadZipFile:
        state = 'invalid'
print(f'{name}: exit={rc} file={state} steps={[s for s in steps if s]}')
PY
    [ $rc -eq 0 ] || status=$rc
    for ((i = 0; i < 60; i++)); do
        ps -eo args | grep -q "[\\]$exe.EXE" || break
        sleep 1
    done
    ps -eo args | grep -q "[\\]$exe.EXE" && echo "$name: $exe.EXE still running after 60s"
done
echo "run_dir=$out"
exit $status
