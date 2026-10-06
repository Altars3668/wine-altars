#!/usr/bin/python3
"""mkppd.py - the PPD for a manual-duplex queue, made from the real queue's.

    mkppd.py <target.ppd> > front.ppd

The front queue offers everything the real queue offers -- paper sizes, trays,
media types, quality, colour -- plus a Duplex option, so that GTK, Chromium,
Edge and LibreOffice show "two-sided, long edge / short edge" and cupsd
advertises sides-supported for it.  Two things change:

  * The filters.  The front queue runs only pdftopdf (layout, n-up, page
    ranges, scaling) and hands the backend a PDF of sheet sides; the real
    queue rasterises later, once per pass.  So every *cupsFilter/*cupsFilter2
    line is replaced by a pass-through to application/pdf, the same shape
    cups-browsed gives its implicitclass queues.

  * Duplex.  The choices carry the standard names (None, DuplexNoTumble,
    DuplexTumble), which is what CUPS maps to and from the IPP attribute
    sides.  Nothing ever sends the PostScript in them to a printer.

  * Copies.  *cupsManualCopies becomes False, so pdftopdf here leaves them
    alone and the backend hands copies and collation to both passes, the way
    Wine does; the real queue then repeats each half identically.  Were the
    copies made here, the halves would have to be cut out of one long
    document instead.

A printer that already has a duplexer needs none of this, so a PPD that
already has a Duplex option is refused.
"""
import re, sys

DUPLEX_UI = """\
*OpenUI *Duplex/2-Sided Printing: PickOne
*OrderDependency: 10 AnySetup *Duplex
*DefaultDuplex: None
*Duplex None/Off: "<</Duplex false>>setpagedevice"
*Duplex DuplexNoTumble/Long Edge (Standard): "<</Duplex true/Tumble false>>setpagedevice"
*Duplex DuplexTumble/Short Edge (Flip): "<</Duplex true/Tumble true>>setpagedevice"
*zh_CN.Translation Duplex/双面打印: ""
*zh_CN.Duplex None/关: ""
*zh_CN.Duplex DuplexNoTumble/长边翻转（标准）: ""
*zh_CN.Duplex DuplexTumble/短边翻转: ""
*CloseUI: *Duplex
"""

FILTER = ('*cupsManualCopies: False\n'
          '*cupsFilter2: "application/vnd.cups-pdf application/pdf 0 -"\n')
MARK = "*% manual duplex: front queue for a printer without a duplexer\n"


def convert(text):
    if re.search(r"^\*OpenUI\s+\*Duplex\b", text, re.M):
        raise SystemExit("mkppd: this PPD already has a Duplex option; "
                         "the printer duplexes by itself")
    out, filter_done = [], False
    for line in text.splitlines(keepends=True):
        if line.startswith(("*cupsFilter:", "*cupsFilter2:", "*cupsPreFilter:",
                            "*cupsUrfSupported:")):
            if not filter_done:
                out.append(FILTER)
                filter_done = True
            continue
        if line.startswith("*cupsManualCopies:"):
            continue
        m = re.match(r'^\*(NickName|ShortNickName):\s*"(.*)"\s*$', line)
        if m:
            name = m.group(2)
            if m.group(1) == "NickName":
                name += ", manual duplex"
            else:          # at most 31 characters, says the PPD spec
                name = name[:27].rstrip() + " MD"
            out.append('*%s: "%s"\n' % (m.group(1), name))
            continue
        m = re.match(r'^\*cupsLanguages:\s*"(.*)"\s*$', line)
        if m:
            langs = m.group(1).split()
            if "zh_CN" not in langs:
                langs.append("zh_CN")
            out.append('*cupsLanguages: "%s"\n' % " ".join(langs))
            continue
        out.append(line)
    if not filter_done:
        # A PostScript printer's PPD has no filter lines at all.
        idx = next((i for i, l in enumerate(out) if l.startswith("*OpenUI")), len(out))
        out.insert(idx, FILTER)
    if not any(l.startswith("*cupsLanguages:") for l in out):
        idx = next((i for i, l in enumerate(out) if l.startswith("*OpenUI")), len(out))
        out.insert(idx, '*cupsLanguages: "zh_CN"\n')
    # The option itself goes in front of the first UI group that comes after
    # the paper settings, or at the end.
    idx = next((i for i, l in enumerate(out) if l.startswith("*OpenUI *OutputBin")), len(out))
    out.insert(idx, DUPLEX_UI)
    out.insert(1, MARK)
    return "".join(out)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    # Bytes outside ASCII in the original are passed through untouched; the
    # translations added here are UTF-8, as CUPS requires of *ll_CC strings.
    with open(sys.argv[1], encoding="utf-8", errors="surrogateescape") as f:
        text = f.read()
    sys.stdout.buffer.write(convert(text).encode("utf-8", "surrogateescape"))
