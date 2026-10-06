#!/usr/bin/python3
"""mkppd.py - the PPD for a printer without a duplexer that prints two-sided.

    mkppd.py <printer.ppd> > queue.ppd

Takes the queue's own PPD (for this printer the one CUPS made for IPP
Everywhere) and adds two things, changing nothing else -- paper sizes, trays,
media types, quality, colour, filters and the way copies are made stay as
they are:

  * A Duplex option with the standard choices (None, DuplexNoTumble,
    DuplexTumble), which is what cupsd maps to and from the IPP attribute
    sides.  cupsd then advertises sides-supported for the queue, and Edge,
    Chromium, GTK, LibreOffice and Word under Wine offer two-sided printing.
    The choices carry no PostScript: nothing here should ask the raster or
    the printer for two sides, the pre-filter makes them.

  * *cupsPreFilter: the manualduplex filter, run on the PDF of sheet sides
    between pdftopdf and the rasteriser, which turns a two-sided job into
    the fronts now and the backs as a second job through the manual feeder.

A printer that already has a duplexer needs none of this, so a PPD that
already has a Duplex option is refused; so is a PPD this script made, whose
original the caller has to give instead.
"""
import re, sys

MARK = "*% manualduplex: two-sided printing through the manual feeder\n"

DUPLEX_UI = """\
*OpenUI *Duplex/2-Sided Printing: PickOne
*OrderDependency: 10 AnySetup *Duplex
*DefaultDuplex: None
*Duplex None/Off: ""
*Duplex DuplexNoTumble/Long Edge (Standard): ""
*Duplex DuplexTumble/Short Edge (Flip): ""
*zh_CN.Translation Duplex/双面打印: ""
*zh_CN.Duplex None/关: ""
*zh_CN.Duplex DuplexNoTumble/长边翻转（标准）: ""
*zh_CN.Duplex DuplexTumble/短边翻转: ""
*CloseUI: *Duplex
"""

PREFILTER = '*cupsPreFilter: "application/vnd.cups-pdf 0 manualduplex"\n'


def convert(text):
    if MARK.strip() in text:
        raise SystemExit("mkppd: this PPD was made by mkppd.py; give the original")
    if re.search(r"^\*OpenUI\s+\*Duplex\b", text, re.M):
        raise SystemExit("mkppd: this PPD already has a Duplex option; "
                         "the printer duplexes by itself")
    if not re.search(r"^\*(InputSlot Manual|ManualFeed True)\b", text, re.M):
        raise SystemExit("mkppd: no manual feeder (InputSlot Manual) to hold the backs")
    out = []
    for line in text.splitlines(keepends=True):
        if line.startswith("*cupsPreFilter:"):
            raise SystemExit("mkppd: this PPD has a pre-filter of its own")
        m = re.match(r'^\*cupsLanguages:\s*"(.*)"\s*$', line)
        if m:
            langs = m.group(1).split()
            if "zh_CN" not in langs:
                langs.append("zh_CN")
            out.append('*cupsLanguages: "%s"\n' % " ".join(langs))
            continue
        out.append(line)
    # In front of the first option, with the other cups* keywords.
    first_ui = next((i for i, l in enumerate(out) if l.startswith("*OpenUI")), len(out))
    out.insert(first_ui, PREFILTER)
    if not any(l.startswith("*cupsLanguages:") for l in out):
        out.insert(first_ui, '*cupsLanguages: "zh_CN"\n')
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
