#!/bin/bash
# run-tests.sh - check manual duplex from end to end, without paper.
#
#   test/run-tests.sh [case ...]
#
# Starts a fake IPP printer on localhost (ippeveprinter: one-sided, with a
# manual feeder, no DNS-SD) and two temporary queues pointing at it: one with
# the real queue's original PPD, as a reference, and one with the PPD
# install.sh gives the real queue (mkppd.py: a Duplex option and the
# manualduplex pre-filter).  Then prints test documents through the second and
# reads back what the fake printer received: how many jobs and in which order,
# each sheet side's page number and which way up it is (test/urfsheets.py),
# the media position and duplex mode in each raster page header, and the
# media-source of each job.  Everything is removed afterwards.
#
# The real queue is only read (its PPD); no job goes near it or the printer.
#
#   case ...   only these cases (names below)
#
# Also prints the same document the way Wine's wineps.drv does -- two
# PostScript jobs for a printer without a duplexer, one job with sides for a
# printer with one, carrying the job options its %cupsJobTicket lines become --
# and checks that every path puts the same thing on paper.
#
# Needs the lpadmin group, cups-ipp-utils, qpdf, poppler-utils (pdftops), and
# the filter installed in /usr/lib/cups/filter (cupsd runs only filters there).
set -u
unset -f grep 2>/dev/null   # an interactive shell may have wrapped it
here=$(cd "$(dirname "$0")" && pwd); top=$(dirname "$here")
real=${REAL_QUEUE:-$(lpstat -d 2>/dev/null | sed -n 's/^system default destination: //p')}
state_dir=${XDG_STATE_HOME:-$HOME/.local/state}/cups-manual-duplex
port=${PORT:-8634}
plain=mdtest-plain; queue=mdtest-queue
only=" $* "

py="/usr/bin/python3 -I"
work=$(mktemp -d "${TMPDIR:-/tmp}/mdtest.XXXXXX")
fake=
cleanup() {
    lpadmin -x $queue 2>/dev/null
    lpadmin -x $plain 2>/dev/null
    if [ -n "$fake" ]; then kill "$fake" 2>/dev/null; wait "$fake" 2>/dev/null; fi
    rm -rf "$work"
}
trap cleanup EXIT
trap 'exit 130' INT TERM

die() { echo "!! $*" >&2; exit 1; }

[ -n "$real" ] || die "no real queue: set REAL_QUEUE"
for q in $plain $queue; do
    # left over from a run that was killed?  lpadmin -x it by hand first
    lpstat -v $q > /dev/null 2>&1 && { trap - EXIT; rm -rf "$work"; die "queue $q exists already"; }
done
cmp -s "$top/manualduplex" /usr/lib/cups/filter/manualduplex ||
    die "/usr/lib/cups/filter/manualduplex is missing or not this one; run install.sh, or:
    sudo install -o root -g root -m 0755 $top/manualduplex /usr/lib/cups/filter/manualduplex"
curl -fsS -o "$work/real.ppd" "http://localhost:631/printers/$real.ppd" ||
    curl -fsS --unix-socket /run/cups/cups.sock -o "$work/real.ppd" "http://localhost/printers/$real.ppd" ||
    die "cannot read the PPD of $real"
if grep -q '^\*% manualduplex:' "$work/real.ppd"; then
    # converted already: the original is where install.sh kept it
    cp "$state_dir/$real.ppd" "$work/real.ppd" || die "no original PPD of $real in $state_dir"
fi
$py "$top/mkppd.py" "$work/real.ppd" > "$work/queue.ppd" || die "mkppd.py failed"

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

echo "==> temporary queues $plain (PPD of $real) and $queue (that PPD made two-sided)"
lpadmin -p $plain -E -v "$fakeuri" -P "$work/real.ppd" -o printer-is-shared=false || die "lpadmin $plain"
lpadmin -p $queue -E -v "$fakeuri" -P "$work/queue.ppd" -o printer-is-shared=false || die "lpadmin $queue"
for q in $plain $queue; do
    case "$(lpstat -v $q)" in
        *"$fakeuri"*) ;;
        *) die "$q does not point at the fake printer; stopping before anything prints" ;;
    esac
done
lpoptions -p $queue -l | grep -E '^Duplex' | sed 's/^/    /'

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
        if [ "$n" -ge "$want" ] && [ -z "$(lpstat -o $plain 2>/dev/null)" ] &&
           [ -z "$(lpstat -o $queue 2>/dev/null)" ]; then
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
check() {       # check <name> <njobs> <expect job 1> [<expect job 2> ...]
    local name=$1 njobs=$2; shift 2
    local want=("$@") got=() f i src dup line ok=1
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
        dup=$($py "$here/urfsheets.py" --duplex "$work/spool/$f")
        src=$(job_attr "$f" media-source)
        printf '    job %d: %s, duplex %s, media-source %s, copies %s, sides %s, name "%s"\n' $((i + 1)) \
            "$line" "$dup" "${src:-none}" "$(job_attr "$f" copies)" "$(job_attr "$f" sides)" \
            "$(job_attr "$f" job-name)"
        [ "$line ${src:-none}" = "${want[$i]:-}" ] || ok=0
        # nothing may ask this printer for two sides: it would print one, or refuse
        [ "$dup" = 1 ] || ok=0
    done
    if [ $ok = 1 ]; then
        echo "PASS $name"
    else
        echo "FAIL $name, expected (and one-sided raster):"; printf '    %s\n' "${want[@]}"; fails=$((fails + 1))
    fi
}

# expected lines below: "<pages> pages: <sides> (pos <media positions>) <media-source>"
#   n@TL upright, n@BR turned 180, b blank; position 4 / media-source manual is the manual feeder

print_on() {    # print_on <queue> <pdf> <options...>
    local q=$1 pdf=$2 o args=(); shift 2
    for o in "$@"; do args+=(-o "$o"); done
    lp -d "$q" -t "$(basename "$pdf")" "${args[@]}" "$pdf" > /dev/null || die "lp $q"
}

want() { [ "$only" = "  " ] || [[ $only == *" $1 "* ]]; }

if want simplex; then
    print_on $queue "$work/p5.pdf" sides=one-sided
    check simplex 1 "5 pages: 1@TL 2@TL 3@TL 4@TL 5@TL (pos 0) none"
fi
if want long5; then
    print_on $queue "$work/p5.pdf" sides=two-sided-long-edge
    check long5 2 "3 pages: 1@TL 3@TL 5@TL (pos 0) none" "3 pages: b 4@BR 2@BR (pos 4) manual"
fi
if want short5; then
    print_on $queue "$work/p5.pdf" sides=two-sided-short-edge
    check short5 2 "3 pages: 1@TL 3@TL 5@TL (pos 0) none" "3 pages: b 4@TL 2@TL (pos 4) manual"
fi
if want long4; then
    print_on $queue "$work/p4.pdf" sides=two-sided-long-edge
    check long4 2 "2 pages: 1@TL 3@TL (pos 0) none" "2 pages: 4@BR 2@BR (pos 4) manual"
fi
if want ppd-option; then
    print_on $queue "$work/p4.pdf" Duplex=DuplexTumble
    check ppd-option 2 "2 pages: 1@TL 3@TL (pos 0) none" "2 pages: 4@TL 2@TL (pos 4) manual"
fi
if want collated; then
    # each copy starts on a new sheet: pdftopdf gives it an even number of sides
    print_on $queue "$work/p5.pdf" sides=two-sided-long-edge copies=2 collate=true
    check collated 2 "6 pages: 1@TL 3@TL 5@TL 1@TL 3@TL 5@TL (pos 0) none" \
                     "6 pages: b 4@BR 2@BR b 4@BR 2@BR (pos 4) manual"
fi
if want uncollated; then
    # copies made in software for two sides are always collated (pdftopdf, as
    # for any printer that has to have its copies made for it)
    print_on $queue "$work/p5.pdf" sides=two-sided-long-edge copies=2 collate=false
    check uncollated 2 "6 pages: 1@TL 3@TL 5@TL 1@TL 3@TL 5@TL (pos 0) none" \
                       "6 pages: b 4@BR 2@BR b 4@BR 2@BR (pos 4) manual"
fi
if want one-page; then
    print_on $queue "$work/p1.pdf" sides=two-sided-long-edge
    check one-page 1 "1 pages: 1@TL (pos 0) none"
fi
if want landscape-long; then
    print_on $queue "$work/l4.pdf" sides=two-sided-long-edge
    check landscape-long 2 "2 pages: 1@TR 3@TR (pos 0) none" "2 pages: 4@BL 2@BL (pos 4) manual"
fi
if want landscape-short; then
    print_on $queue "$work/l4.pdf" sides=two-sided-short-edge
    check landscape-short 2 "2 pages: 1@TR 3@TR (pos 0) none" "2 pages: 4@TR 2@TR (pos 4) manual"
fi
if want utf8-title; then
    cp "$work/p4.pdf" "$work/双面测试.pdf"
    print_on $queue "$work/双面测试.pdf" sides=two-sided-long-edge
    check utf8-title 2 "2 pages: 1@TL 3@TL (pos 0) none" "2 pages: 4@BR 2@BR (pos 4) manual"
fi
if want tray; then
    # a tray chosen for the job holds for the fronts; the backs still use the manual feeder
    print_on $queue "$work/p4.pdf" sides=two-sided-long-edge InputSlot=Tray1
    check tray 2 "2 pages: 1@TL 3@TL (pos 20) tray-1" "2 pages: 4@BR 2@BR (pos 4) manual"
fi
if want order; then
    # a second job waiting does not come between the fronts and the backs
    print_on $queue "$work/p4.pdf" sides=two-sided-long-edge
    print_on $queue "$work/p5.pdf" sides=two-sided-long-edge
    check order 4 "2 pages: 1@TL 3@TL (pos 0) none" "2 pages: 4@BR 2@BR (pos 4) manual" \
                  "3 pages: 1@TL 3@TL 5@TL (pos 0) none" "3 pages: b 4@BR 2@BR (pos 4) manual"
fi

# Layout options: rather than spell out each sheet, compare with the same
# document and options printed one-sided on the plain queue (test/oracle.py
# says what the two passes must then contain).
oracle_case() { # oracle_case <name> <long|short> <pdf> <option>...
    local name=$1 edge=$2 pdf=$3 ref files=(); shift 3
    want "$name" || return 0
    print_on $plain "$pdf" "$@"
    wait_jobs 1 || { echo "FAIL $name: no reference job"; fails=$((fails + 1)); return; }
    ref=$(new_jobs); seen=$(( seen + 1 ))
    print_on $queue "$pdf" "$@" "sides=two-sided-$edge-edge"
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
    # a queue without Duplex in its PPD: Wine does the two passes itself
    qpdf --rotate=+180 "$work/p5.pdf" "$work/p5r.pdf"
    wine_job $plain wine-odd "$work/p5.pdf" media=A4 page-set=odd AP_D_InputSlot=
    wine_job $plain wine-even "$work/p5r.pdf" media=A4 page-set=even outputorder=reverse InputSlot=Manual
    check wine-own 2 "3 pages: 1@TL 3@TL 5@TL (pos 0) none" "3 pages: b 4@BR 2@BR (pos 4) manual"
fi
if want wine-queue; then
    # this queue's PPD has a Duplex: Wine sends one job with sides
    wine_job $queue wine-sides "$work/p5.pdf" media=A4 sides=two-sided-long-edge AP_D_InputSlot=
    check wine-queue 2 "3 pages: 1@TL 3@TL 5@TL (pos 0) none" "3 pages: b 4@BR 2@BR (pos 4) manual"
fi

if want queue-attrs; then
    # what GTK, Chromium and LibreOffice are told about the queue, and whose
    # the backs jobs are (the filter runs as lp)
    echo "----  queue-attrs"
    sides=$(ipptool -tv "ipp://localhost/printers/$queue" get-printer-attributes.test 2>/dev/null |
            sed -nE 's/^ +sides-supported \([^)]*\) = //p')
    owners=$(lpstat -W completed -o $queue 2>/dev/null | awk '{print $2}' | sort -u | tr '\n' ' ')
    echo "    sides-supported: $sides"
    echo "    owners of the jobs on $queue: $owners"
    if [ "$sides" = "one-sided,two-sided-long-edge,two-sided-short-edge" ] && [ "$owners" = "$USER " ]; then
        echo "PASS queue-attrs"
    else
        echo "FAIL queue-attrs"; fails=$((fails + 1))
    fi
fi

echo
if [ $fails = 0 ]; then echo "all passed"; else echo "$fails failed"; fi
[ $fails = 0 ]
