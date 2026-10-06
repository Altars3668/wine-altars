#!/usr/bin/python3
"""urfsheets.py - what is on each sheet side of a URF job, and which way up.

    urfsheets.py [--short] file.urf...

Reads the pages test/mkpdf.py draws (page N: N solid squares along its top
edge, starting at the top-left corner) back out of the raster a printer gets.
For each page it prints the page number, the sheet corner nearest the first
square -- that corner is where the top-left of the page landed on the sheet --
and the media position the raster header asks for (4 is the manual feeder in
the PPDs CUPS generates).

  TL  upright                      BR  turned 180 degrees
  TR / BL  turned a quarter, as landscape pages are put on portrait sheets

--short prints one line per file: "3 pages: b 4@BR 2@BR (pos 4)".

The page count in the file header is 0 when the job was streamed, so pages are
read until the data ends.  Standard library only.
"""
import struct, sys

def pages(path):
    d = open(path, "rb").read()
    if d[:8] != b"UNIRAST\0":
        raise SystemExit("%s: not URF" % path)
    pos = 12
    while pos + 32 <= len(d):
        hdr = d[pos:pos + 32]
        bpp, position = hdr[0], hdr[5]
        w, h, dpi = struct.unpack(">III", hdr[12:24])
        pos += 32
        bpc = max(bpp // 8, 1)
        rows = {}                       # y -> list of dark (x0, x1)
        y = 0
        while y < h:
            rep = d[pos] + 1; pos += 1
            x = 0; runs = []
            while x < w:
                c = d[pos]; pos += 1
                if c == 128:            # rest of the line is white
                    x = w
                elif c < 128:           # one pixel, c + 1 times
                    px = d[pos:pos + bpc]; pos += bpc
                    if sum(px) < 128 * bpc:
                        runs.append((x, x + c + 1))
                    x += c + 1
                else:                   # 257 - c literal pixels
                    k = 257 - c
                    for i in range(k):
                        px = d[pos + i * bpc:pos + (i + 1) * bpc]
                        if sum(px) < 128 * bpc:
                            if runs and runs[-1][1] == x + i:
                                runs[-1] = (runs[-1][0], x + i + 1)
                            else:
                                runs.append((x + i, x + i + 1))
                    pos += k * bpc; x += k
            if runs:
                for yy in range(y, min(y + rep, h)):
                    rows[yy] = runs
            y += rep
        yield w, h, dpi, position, rows

def blobs(rows):
    """Connected dark regions, as bounding boxes with their pixel counts."""
    active = []                         # (x0, x1, blob index) on the previous row
    boxes = []                          # [x0, y0, x1, y1, area]
    parent = []
    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]; i = parent[i]
        return i
    prev_y = None
    for y in sorted(rows):
        if prev_y is None or y != prev_y + 1:
            active = []
        cur = []
        for x0, x1 in rows[y]:
            hit = [b for (a0, a1, b) in active if a0 < x1 and x0 < a1]
            if hit:
                b = find(hit[0])
                for o in hit[1:]:
                    o = find(o)
                    if o != b:
                        parent[o] = b
                        bb, ob = boxes[b], boxes[o]
                        bb[0] = min(bb[0], ob[0]); bb[1] = min(bb[1], ob[1])
                        bb[2] = max(bb[2], ob[2]); bb[3] = max(bb[3], ob[3])
                        bb[4] += ob[4]
            else:
                b = len(boxes)
                boxes.append([x0, y, x1, y + 1, 0]); parent.append(b)
            bb = boxes[b]
            bb[0] = min(bb[0], x0); bb[2] = max(bb[2], x1)
            bb[3] = max(bb[3], y + 1); bb[4] += x1 - x0
            cur.append((x0, x1, b))
        active = cur
        prev_y = y
    return [boxes[i] for i in range(len(boxes)) if find(i) == i]

def describe(w, h, dpi, rows):
    if not rows:
        return None, None
    side = 24 / 72 * dpi                # mkpdf.py squares are 24 pt
    squares = []
    for x0, y0, x1, y1, area in blobs(rows):
        bw, bh = x1 - x0, y1 - y0
        # scaled down for n-up, but still solid and square
        if 0.3 * side < bw < 1.25 * side and 0.8 < bw / bh < 1.25 \
                and area > 0.9 * bw * bh:
            squares.append(((x0 + x1) / 2, (y0 + y1) / 2))
    if not squares:
        return 0, "?"
    corners = {"TL": (0, 0), "TR": (w, 0), "BL": (0, h), "BR": (w, h)}
    best = min(((sx - cx) ** 2 + (sy - cy) ** 2, name)
               for sx, sy in squares for name, (cx, cy) in corners.items())
    return len(squares), best[1]

def summary(path):
    out = []
    for w, h, dpi, position, rows in pages(path):
        n, corner = describe(w, h, dpi, rows)
        out.append(("b" if n is None else "%d@%s" % (n, corner), position, w, h, dpi))
    return out

if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    short = "--short" in sys.argv
    for path in args:
        res = summary(path)
        if short:
            pos = sorted({p for _, p, _, _, _ in res})
            print("%d pages: %s (pos %s)" % (len(res), " ".join(r[0] for r in res),
                                             ",".join(map(str, pos))))
            continue
        print("%s: %d page(s)" % (path.split("/")[-1], len(res)))
        for i, (txt, position, w, h, dpi) in enumerate(res, 1):
            print("   %d: %-8s media-position %d  %dx%d@%d" % (i, txt, position, w, h, dpi))
