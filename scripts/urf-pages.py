#!/usr/bin/env python3
"""urf-pages.py - what is on each sheet of a URF (UNIRAST) job.

For each page: blank, or the number of dark squares along its top or bottom
edge (tools/duptest prints N squares on page N) and whether they sit top-left
(upright) or bottom-right (turned 180 degrees).  Used by
scripts/duplex-capture.sh; the page count in the file header is 0 when the
writer streamed the job, so pages are read until the data ends."""
import struct, sys

def pages(path):
    d = open(path, "rb").read()
    assert d[:8] == b"UNIRAST\0", path
    n = struct.unpack(">I", d[8:12])[0]   # 0 when the writer streamed it
    pos = 12
    while pos + 32 <= len(d) and (n == 0 or n > 0):
        bpp, cs = d[pos], d[pos + 1]
        w, h, dpi = struct.unpack(">III", d[pos + 12:pos + 24])
        pos += 32
        bpc = bpp // 8
        rows = []          # (y, [(x0, x1) dark runs])
        y = 0
        while y < h:
            rep = d[pos] + 1; pos += 1
            x = 0; runs = []
            while x < w:
                c = d[pos]; pos += 1
                if c == 128:
                    x = w
                elif c < 128:
                    px = d[pos:pos + bpc]; pos += bpc
                    if sum(px) / bpc < 128: runs.append((x, x + c + 1))
                    x += c + 1
                else:
                    k = 257 - c
                    for i in range(k):
                        px = d[pos + i * bpc:pos + (i + 1) * bpc]
                        if sum(px) / bpc < 128: runs.append((x + i, x + i + 1))
                    pos += k * bpc; x += k
            if runs: rows.append((y, y + rep, runs))
            y += rep
        yield w, h, dpi, rows

def describe(w, h, rows):
    if not rows: return "blank"
    ys = [r[0] for r in rows]; ye = [r[1] for r in rows]
    top, bottom = min(ys), max(ye)
    # merge runs of the middle dark row into squares
    mid = rows[len(rows) // 2][2]
    squares = []
    for a, b in sorted(mid):
        if squares and a - squares[-1][1] < 3: squares[-1][1] = b
        else: squares.append([a, b])
    where = "top-left, upright" if top < h / 2 and squares[0][0] < w / 2 else \
            "bottom-right, turned 180" if top > h / 2 and squares[-1][1] > w / 2 else "elsewhere"
    return "%d square(s) %s" % (len(squares), where)

for path in sys.argv[1:]:
    out = []
    for w, h, dpi, rows in pages(path):
        out.append(describe(w, h, rows))
    print("%s: %d page(s)" % (path.split("/")[-1], len(out)))
    for i, t in enumerate(out, 1): print("   sheet %d: %s" % (i, t))
