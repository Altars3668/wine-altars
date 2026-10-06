#!/bin/bash
# run-tests.sh - check manual duplex from end to end, without paper.
#
#   test/run-tests.sh [--direct] [case ...]
#
# Starts a fake IPP printer on localhost (ippeveprinter: one-sided, with a
# manual feeder, no DNS-SD), a temporary copy of the real queue that points
# at it -- same PPD, so the same filters and the same InputSlot -> media-source
# mapping -- and a temporary front queue using the manualduplex backend.  Then
# prints test documents through the front queue and reads back what the fake
# printer received: how many jobs, each sheet side's page number and which way
# up it is (test/urfsheets.py), the media position in each raster page header,
# and the media-source of each job.  Everything is removed afterwards.
#
# The real queue is only read (its PPD); no job goes near it or the printer.
#
#   --direct   run the backend by hand instead of through cupsd, the front
#              queue's filters with cupsfilter; for before it is installed
#   case ...   only these cases (names below)
#
# Also prints the same document the way Wine's wineps.drv does for a printer
# without a duplexer -- two PostScript jobs to the real queue, carrying the
# job options its %cupsJobTicket lines become -- and checks that both paths
# put the same thing on paper.
#
# Needs the lpadmin group, cups-ipp-utils, qpdf, poppler-utils (pdftops),
# and for the default mode the backend in /usr/lib/cups/backend.
set -u
unset -f grep 2>/dev/null   # an interactive shell may have wrapped it
here=$(cd "$(dirname "$0")" && pwd); top=$(dirname "$here")
real=${REAL_QUEUE:-$(lpstat -d 2>/dev/null | sed -n 's/^system default destination: //p')}
port=${PORT:-8634}
target=mdtest-target; front=mdtest-front
direct=0
[ "${1:-}" = "--direct" ] && { direct=1; shift; }
only=" $* "

py="/usr/bin/python3 -I"
work=$(mktemp -d "${TMPDIR:-/tmp}/mdtest.XXXXXX")
fake=
cleanup() {
    lpadmin -x $front 2>/dev/null
    lpadmin -x $target 2>/dev/null
    if [ -n "$fake" ]; then kill "$fake" 2>/dev/null; wait "$fake" 2>/dev/null; fi
    rm -rf "$work"
}
trap cleanup EXIT
trap 'exit 130' INT TERM

die() { echo "!! $*" >&2; exit 1; }

[ -n "$real" ] || die "no real queue: set REAL_QUEUE"
for q in $target $front; do
    # left over from a run that was killed?  lpadmin -x it by hand first
    lpstat -v $q > /dev/null 2>&1 && { trap - EXIT; rm -rf "$work"; die "queue $q exists already"; }
done
curl -fsS -o "$work/real.ppd" "http://localhost:631/printers/$real.ppd" ||
    curl -fsS --unix-socket /run/cups/cups.sock -o "$work/real.ppd" "http://localhost/printers/$real.ppd" ||
    die "cannot read the PPD of $real"
$py "$top/mkppd.py" "$work/real.ppd" > "$work/front.ppd" || die "mkppd.py failed"

echo "==> fake printer on localhost:$port, spool $work/spool"
# Its attributes come from the same PPD: one-sided, media-source auto, manual
# and tray-1, like the real one.  -c /bin/true: "print" at once, keep the file.
mkdir "$work/spool"
ippeveprinter -r off -n localhost -p "$port" -P "$work/real.ppd" -k \
    -d "$work/spool" -c /bin/true mdtest-fake > "$work/fake.log" 2>&1 &
fake=$!
fakeuri="ipp://localhost:$port/ipp/print"
for i in $(seq 1 40); do
    ipptool -q "$fakeuri" get-printer-attributes.test 2>/dev/null && break
    sleep 0.25
done
ipptool -q "$fakeuri" get-printer-attributes.test 2>/dev/null ||
    die "the fake printer did not come up (port $port busy? set PORT); see $work/fake.log"

echo "==> temporary queues $target (PPD of $real) and $front"
lpadmin -p $target -E -v "$fakeuri" -P "$work/real.ppd" -o printer-is-shared=false || die "lpadmin $target"
case "$(lpstat -v $target)" in
    *"$fakeuri"*) ;;
    *) die "$target does not point at the fake printer; stopping before anything prints" ;;
esac
if [ $direct = 0 ]; then
    [ -x /usr/lib/cups/backend/manualduplex ] || die "backend not installed; use --direct"
    lpadmin -p $front -E -v "manualduplex:/$target" -P "$work/front.ppd" -o printer-is-shared=false ||
        die "lpadmin $front"
    lpstat -v $front
    lpoptions -p $front -l | grep -E '^Duplex' | sed 's/^/    /'
fi

# test documents
$py "$here/mkpdf.py" 5 "$work/p5.pdf"
$py "$here/mkpdf.py" 4 "$work/p4.pdf"
$py "$here/mkpdf.py" 1 "$work/p1.pdf"
$py "$here/mkpdf.py" 4 "$work/l4.pdf" --landscape

seen=0
wait_jobs() {   # wait_jobs <count>: wait for that many new files at the fake printer
    local want=$(( seen + $1 )) i n
    for i in $(seq 1 240); do
        n=$(ls "$work/spool" | grep -c '\.urf$')
        if [ "$n" -ge "$want" ] && [ -z "$(lpstat -o $target 2>/dev/null)" ] &&
           { [ $direct = 1 ] || [ -z "$(lpstat -o $front 2>/dev/null)" ]; }; then
            sleep 1
            return 0
        fi
        sleep 0.5
    done
    return 1
}

new_jobs() {    # the files received since the last call, oldest first
    ls "$work/spool" | grep '\.urf$' | sort -n | tail -n +$(( seen + 1 ))
}

job_attr() {    # job_attr <file> <attr>: what the fake printer was given for that job
    local id=${1%%-*}
    ipptool -tv -d job_id="$id" "$fakeuri" "$here/get-job.test" 2>/dev/null > "$work/job.txt"
    case $2 in
        media-source)   # inside media-col, or on its own
            grep -oE 'media-source(=| \(keyword\) = )[a-z0-9-]+' "$work/job.txt" | head -1 | sed -E 's/.*[= ]//' ;;
        *)  sed -nE "s/^ +$2 \([a-zA-Z]+\) = (.*)/\1/p" "$work/job.txt" | head -1 ;;
    esac
}

fails=0
check() {       # check <name> <njobs> <expect job 1> [<expect job 2>]
    local name=$1 njobs=$2; shift 2
    local want=("$@") got=() f i src line ok=1
    if ! wait_jobs "$njobs"; then
        echo "FAIL $name: timed out waiting for $njobs job(s)"; fails=$((fails + 1)); return
    fi
    mapfile -t got < <(new_jobs)
    seen=$(( seen + ${#got[@]} ))
    [ ${#got[@]} -eq "$njobs" ] || ok=0
    printf '%s  %s\n' "----" "$name"
    for i in "${!got[@]}"; do
        f=${got[$i]}
        line=$($py "$here/urfsheets.py" --short "$work/spool/$f")
        src=$(job_attr "$f" media-source)
        printf '    job %d: %s, media-source %s, copies %s, name "%s"\n' $((i + 1)) "$line" "${src:-none}" \
            "$(job_attr "$f" copies)" "$(job_attr "$f" job-name)"
        [ "$line ${src:-none}" = "${want[$i]:-}" ] || ok=0
    done
    if [ $ok = 1 ]; then
        echo "PASS $name"
    else
        echo "FAIL $name, expected:"; printf '    %s\n' "${want[@]}"; fails=$((fails + 1))
    fi
}

# expected lines below: "<pages> pages: <sides> (pos <media positions>) <media-source>"
#   n@TL upright, n@BR turned 180, b blank; position 4 / media-source manual is the manual feeder

print_front() { # print_front <pdf> <options...>: one job through the front queue
    local pdf=$1; shift
    if [ $direct = 0 ]; then
        local o args=()
        for o in "$@"; do args+=(-o "$o"); done
        lp -d $front -t "$(basename "$pdf")" "${args[@]}" "$pdf" > /dev/null || die "lp $front"
    else
        local copies=1 o opts=""
        for o in "$@"; do
            case $o in copies=*) copies=${o#copies=} ;; esac
            opts="$opts $o"
        done
        # shellcheck disable=SC2046
        cupsfilter -p "$work/front.ppd" -m application/vnd.cups-pdf -n "$copies" \
            $(for o in "$@"; do printf -- '-o %s ' "$o"; done) "$pdf" > "$work/front-out.pdf" 2> "$work/cupsfilter.log" ||
            die "cupsfilter (front queue filters)"
        DEVICE_URI="manualduplex:/$target" PRINTER=$front TMPDIR="$work" \
            $py "$top/manualduplex" 1 "$USER" "$(basename "$pdf")" "$copies" "${opts# }" "$work/front-out.pdf" \
            2> "$work/backend.log" || { cat "$work/backend.log"; die "backend failed"; }
        grep -E '^(INFO|ERROR|WARNING)' "$work/backend.log" | sed 's/^/    backend /'
    fi
}

want() { [ "$only" = "  " ] || [[ $only == *" $1 "* ]]; }

if want simplex; then
    print_front "$work/p5.pdf" sides=one-sided
    check simplex 1 "5 pages: 1@TL 2@TL 3@TL 4@TL 5@TL (pos 0) none"
fi
if want long5; then
    print_front "$work/p5.pdf" sides=two-sided-long-edge
    check long5 2 "3 pages: 1@TL 3@TL 5@TL (pos 0) none" "3 pages: b 4@BR 2@BR (pos 4) manual"
fi
if want short5; then
    print_front "$work/p5.pdf" sides=two-sided-short-edge
    check short5 2 "3 pages: 1@TL 3@TL 5@TL (pos 0) none" "3 pages: b 4@TL 2@TL (pos 4) manual"
fi
if want long4; then
    print_front "$work/p4.pdf" sides=two-sided-long-edge
    check long4 2 "2 pages: 1@TL 3@TL (pos 0) none" "2 pages: 4@BR 2@BR (pos 4) manual"
fi
if want ppd-option; then
    print_front "$work/p4.pdf" Duplex=DuplexTumble
    check ppd-option 2 "2 pages: 1@TL 3@TL (pos 0) none" "2 pages: 4@TL 2@TL (pos 4) manual"
fi
if want collated; then
    print_front "$work/p5.pdf" sides=two-sided-long-edge copies=2 collate=true
    check collated 2 "6 pages: 1@TL 3@TL 5@TL 1@TL 3@TL 5@TL (pos 0) none" \
                     "6 pages: b 4@BR 2@BR b 4@BR 2@BR (pos 4) manual"
fi
if want uncollated; then
    print_front "$work/p5.pdf" sides=two-sided-long-edge copies=2 collate=false
    check uncollated 2 "6 pages: 1@TL 1@TL 3@TL 3@TL 5@TL 5@TL (pos 0) none" \
                       "6 pages: b b 4@BR 4@BR 2@BR 2@BR (pos 4) manual"
fi
if want one-page; then
    print_front "$work/p1.pdf" sides=two-sided-long-edge
    check one-page 1 "1 pages: 1@TL (pos 0) none"
fi
if want landscape-long; then
    print_front "$work/l4.pdf" sides=two-sided-long-edge
    check landscape-long 2 "2 pages: 1@TR 3@TR (pos 0) none" "2 pages: 4@BL 2@BL (pos 4) manual"
fi
if want landscape-short; then
    print_front "$work/l4.pdf" sides=two-sided-short-edge
    check landscape-short 2 "2 pages: 1@TR 3@TR (pos 0) none" "2 pages: 4@TR 2@TR (pos 4) manual"
fi
if want utf8-title; then
    cp "$work/p4.pdf" "$work/双面测试.pdf"
    print_front "$work/双面测试.pdf" sides=two-sided-long-edge
    check utf8-title 2 "2 pages: 1@TL 3@TL (pos 0) none" "2 pages: 4@BR 2@BR (pos 4) manual"
fi
if want tray; then
    # a tray chosen for the job holds for the fronts; the backs still use the manual feeder
    print_front "$work/p4.pdf" sides=two-sided-long-edge InputSlot=Tray1
    check tray 2 "2 pages: 1@TL 3@TL (pos 20) tray-1" "2 pages: 4@BR 2@BR (pos 4) manual"
fi

# Layout options: rather than spell out each sheet, compare with the same
# document and options printed one-sided straight on the real queue
# (test/oracle.py says what the two passes must then contain).
oracle_case() { # oracle_case <name> <long|short> <pdf> <option>...
    local name=$1 edge=$2 pdf=$3 o args=() ref files=(); shift 3
    want "$name" || return 0
    for o in "$@"; do args+=(-o "$o"); done
    lp -d $target -t "$name-reference" "${args[@]}" "$pdf" > /dev/null || die "lp $target"
    wait_jobs 1 || { echo "FAIL $name: no reference job"; fails=$((fails + 1)); return; }
    ref=$(new_jobs); seen=$(( seen + 1 ))
    print_front "$pdf" "$@" "sides=two-sided-$edge-edge"
    wait_jobs 2 || { echo "FAIL $name: timed out"; fails=$((fails + 1)); return; }
    mapfile -t files < <(new_jobs); seen=$(( seen + ${#files[@]} ))
    echo "----  $name ($edge edge, $*)"
    if [ ${#files[@]} = 2 ] && [ "$(job_attr "${files[1]}" media-source)" = manual ] &&
       $py "$here/oracle.py" "$edge" "$work/spool/$ref" "$work/spool/${files[0]}" "$work/spool/${files[1]}"; then
        echo "PASS $name"
    else
        echo "FAIL $name (${#files[@]} job(s); backs media-source $(job_attr "${files[1]:-0}" media-source))"
        fails=$((fails + 1))
    fi
}
oracle_case nup2 long "$work/p5.pdf" number-up=2
oracle_case nup2-short short "$work/p5.pdf" number-up=2
oracle_case ranges long "$work/p5.pdf" page-ranges=2-5
oracle_case orient-landscape long "$work/l4.pdf" orientation-requested=4
oracle_case portrait-as-landscape short "$work/p5.pdf" orientation-requested=4 print-scaling=fit
oracle_case fit long "$work/p4.pdf" fit-to-page media=Letter

# What Wine sends.  wineps.drv writes PostScript with %cupsJobTicket lines;
# localspl (dlls/localspl/cups.c, cups_write_doc) takes those lines out and
# gives them to Create-Job as job options, because cupsd only reads tickets
# in Print-Job requests.  So: PostScript, and the ticket as lp options.  The
# long-edge backs are turned in the PostScript (ps.c, PSDRV_WriteNewPage);
# here pdftops of a turned PDF stands in for that.
wine_job() {    # wine_job <queue> <title> <pdf> <ticket option>...
    local q=$1 t=$2 pdf=$3 o args=(); shift 3
    pdftops -level3 "$pdf" "$work/$t.ps" || die pdftops
    for o in "$@"; do args+=(-o "$o"); done
    lp -d "$q" -t "$t" "${args[@]}" "$work/$t.ps" > /dev/null || die "lp $q"
}
if want wine-own; then
    # on the real queue, no Duplex in its PPD: Wine does the two passes itself
    qpdf --rotate=+180 "$work/p5.pdf" "$work/p5r.pdf"
    wine_job $target wine-odd "$work/p5.pdf" media=A4 page-set=odd AP_D_InputSlot=
    wine_job $target wine-even "$work/p5r.pdf" media=A4 page-set=even outputorder=reverse InputSlot=Manual
    check wine-own 2 "3 pages: 1@TL 3@TL 5@TL (pos 0) none" "3 pages: b 4@BR 2@BR (pos 4) manual"
fi
if want wine-front && [ $direct = 0 ]; then
    # on the front queue its PPD has a Duplex: Wine sends one job with sides
    wine_job $front wine-front "$work/p5.pdf" media=A4 sides=two-sided-long-edge AP_D_InputSlot=
    check wine-front 2 "3 pages: 1@TL 3@TL 5@TL (pos 0) none" "3 pages: b 4@BR 2@BR (pos 4) manual"
fi

if [ $direct = 0 ] && want front-attrs; then
    # what GTK, Chromium and LibreOffice are told about the front queue, and
    # whose the jobs on the real queue are (the backend runs as lp)
    echo "----  front-attrs"
    sides=$(ipptool -tv "ipp://localhost/printers/$front" get-printer-attributes.test 2>/dev/null |
            sed -nE 's/^ +sides-supported \([^)]*\) = //p')
    owners=$(lpstat -W completed -o $target 2>/dev/null | awk '{print $2}' | sort -u | tr '\n' ' ')
    echo "    sides-supported: $sides"
    echo "    owners of the jobs on $target: $owners"
    if [ "$sides" = "one-sided,two-sided-long-edge,two-sided-short-edge" ] && [ "$owners" = "$USER " ]; then
        echo "PASS front-attrs"
    else
        echo "FAIL front-attrs"; fails=$((fails + 1))
    fi
fi

echo
if [ $fails = 0 ]; then echo "all passed"; else echo "$fails failed"; fi
[ $fails = 0 ]
