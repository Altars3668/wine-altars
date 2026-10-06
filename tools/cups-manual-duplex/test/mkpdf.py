#!/usr/bin/python3
"""mkpdf.py - a numbered test document for checking two-sided output.

    mkpdf.py <pages> <out.pdf> [--landscape]

Page N carries N black squares along its top edge, starting at the left
corner, the same marks tools/duptest draws from Wine, plus its number in large
type for a person to read.  The squares give both the page number and which
way up the page was put on the sheet; test/urfsheets.py reads them back from
the raster a printer would receive.

A4.  --landscape makes the pages 297 x 210 mm, so the filters have to turn
them onto the sheet, which is the case where "long edge" and "turned" are
easiest to get wrong.  Standard library only.
"""
import sys

def page_stream(n, w, h):
    s = 24.0                       # square side, points
    ops = ["0 g"]
    for i in range(n):
        x = 30 + i * 36
        ops.append("%.2f %.2f %.2f %.2f re f" % (x, h - 30 - s, s, s))
    ops.append("BT /F1 96 Tf %.2f %.2f Td (%d) Tj ET" % (w / 2 - 40, h / 2 - 30, n))
    ops.append("BT /F1 14 Tf %.2f %.2f Td (top of page %d) Tj ET" % (w / 2 - 50, h - 80, n))
    return "\n".join(ops).encode("ascii")

def build(pages, landscape):
    w, h = (841.89, 595.28) if landscape else (595.28, 841.89)
    objs = []                      # index 0 -> object 1
    def add(body):
        objs.append(body)
        return len(objs)
    catalog = add(None)
    pages_obj = add(None)
    font = add(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>")
    kids = []
    for n in range(1, pages + 1):
        data = page_stream(n, w, h)
        content = add(b"<< /Length %d >>\nstream\n" % len(data) + data + b"\nendstream")
        kids.append(add(("<< /Type /Page /Parent %d 0 R /MediaBox [0 0 %.2f %.2f] "
                         "/Resources << /Font << /F1 %d 0 R >> >> /Contents %d 0 R >>"
                         % (pages_obj, w, h, font, content)).encode("ascii")))
    objs[catalog - 1] = b"<< /Type /Catalog /Pages %d 0 R >>" % pages_obj
    objs[pages_obj - 1] = ("<< /Type /Pages /Count %d /Kids [%s] >>"
                           % (pages, " ".join("%d 0 R" % k for k in kids))).encode("ascii")
    out = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
    offsets = []
    for i, body in enumerate(objs, 1):
        offsets.append(len(out))
        out += b"%d 0 obj\n" % i + body + b"\nendobj\n"
    xref = len(out)
    out += b"xref\n0 %d\n0000000000 65535 f \n" % (len(objs) + 1)
    for off in offsets:
        out += b"%010d 00000 n \n" % off
    out += b"trailer\n<< /Size %d /Root %d 0 R >>\nstartxref\n%d\n%%%%EOF\n" % (len(objs) + 1, catalog, xref)
    return bytes(out)

if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    with open(sys.argv[2], "wb") as f:
        f.write(build(int(sys.argv[1]), "--landscape" in sys.argv[3:]))
