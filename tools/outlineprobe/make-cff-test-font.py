#!/usr/bin/env python3
"""Makes the small CFF font that gdi32's font test loads to check the outlines GetGlyphOutline gives of cubic curves.

    make-cff-test-font.py <output.otf> [c array output]

One glyph, "a", drawn here: two contours in a 1000-unit em, so that at a height of -1000 and unhinted every point is
a whole pixel.  The first contour is a line and four cubic curves whose third differences p3 - 3 p2 + 3 p1 - p0 make
Windows split them into 1, 2, 3 and 4 quadratic curves in GGO_NATIVE, the last one ending where the contour starts;
the second contour, a curve and a line, starts right after that closing curve.
"""
import sys
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.t2CharStringPen import T2CharStringPen

def draw_a(pen):
    pen.moveTo((100, 0))
    pen.lineTo((500, 0))
    pen.curveTo((700, 0), (800, 100), (800, 300))      # third difference (0, 0): one piece
    pen.curveTo((800, 400), (750, 500), (650, 560))    # (0, -40): two
    pen.curveTo((500, 650), (300, 650), (150, 500))    # (100, -60): three
    pen.curveTo((0, 400), (100, 100), (100, 0))        # (-350, 400): four, back to the start
    pen.closePath()
    pen.moveTo((300, 200))
    pen.curveTo((350, 200), (400, 250), (400, 300))    # (-50, -50): two
    pen.lineTo((300, 300))
    pen.closePath()

def main():
    fb = FontBuilder(1000, isTTF=False)
    fb.setupGlyphOrder([".notdef", "a"])
    fb.setupCharacterMap({0x61: "a"})
    strings = {}
    pen = T2CharStringPen(500, None)
    strings[".notdef"] = pen.getCharString()
    pen = T2CharStringPen(900, None)
    draw_a(pen)
    strings["a"] = pen.getCharString()
    fb.setupCFF("WineTestCFF-Regular", {"FullName": "Wine Test CFF"}, strings, {})
    fb.setupHorizontalMetrics({".notdef": (500, 0), "a": (900, 0)})
    fb.setupHorizontalHeader(ascent=800, descent=-200)
    fb.setupNameTable({"familyName": "Wine Test CFF", "styleName": "Regular", "uniqueFontIdentifier": "Wine Test CFF",
                       "fullName": "Wine Test CFF", "psName": "WineTestCFF-Regular", "version": "Version 1.0"})
    fb.setupOS2(sTypoAscender=800, sTypoDescender=-200, usWinAscent=800, usWinDescent=200, fsType=0,
                ulUnicodeRange1=1, ulCodePageRange1=1)
    fb.setupPost()
    fb.save(sys.argv[1])
    if len(sys.argv) > 2:
        data = open(sys.argv[1], "rb").read()
        with open(sys.argv[2], "w") as out:
            out.write("static const BYTE cff_font[] =\n{\n")
            for i in range(0, len(data), 16):
                out.write("    " + ", ".join("0x%02x" % b for b in data[i:i + 16]) + ",\n")
            out.write("};\n")

main()
