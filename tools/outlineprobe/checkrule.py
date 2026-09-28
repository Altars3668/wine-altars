#!/usr/bin/env python3
"""Checks outlineprobe results against the rule by which GGO_NATIVE turns a cubic curve into quadratic ones.

    checkrule.py <outlineprobe output>...

For every cubic curve of GGO_BEZIER (hinted and unhinted, default quality), the GGO_NATIVE record in the same place
must have n control points, n = 1 when the third difference d = p3 - 3 p2 + 3 p1 - p0 is at most 1 in both directions,
otherwise the least n >= 2 with max(|dx|, |dy|) <= 10 n^3; and control point k must be (3 (a1 + a2) - a0 - a3) / 4 of
the piece a0..a3 of the cubic between t = k/n and (k+1)/n.  Prints the mismatches and the largest distance of a
control point from where the rule puts it.
"""
import math
import re
import sys

def parse(path):
    """{(quality, char): {section: [(start, [(type, [points])])]}}"""
    data, quality, char, section = {}, None, None, None
    for line in open(path, encoding='utf-8', errors='replace'):
        line = line.rstrip('\r\n')
        m = re.match(r'height (-?\d+), quality (\w+)', line)
        if m:
            quality = m.group(2)
            continue
        m = re.match(r'  (?:char|glyph) (\S+)', line)
        if m:
            char = m.group(1)
            continue
        m = re.match(r'    (GGO_\w+(?: unhinted)?): (\d+) bytes', line)
        if m:
            section = m.group(1)
            data.setdefault((quality, char), {})[section] = []
            continue
        m = re.match(r'      contour \d+ bytes, start \(([-\d.]+),([-\d.]+)\)', line)
        if m:
            data[(quality, char)][section].append(((float(m.group(1)), float(m.group(2))), []))
            continue
        m = re.match(r'        ([LQC])(\d+) (.*)', line)
        if m:
            points = [(float(x), float(y)) for x, y in re.findall(r'\(([-\d.]+),([-\d.]+)\)', m.group(3))]
            data[(quality, char)][section][-1][1].append((m.group(1), points))
    return data

def cubic_curves(path, hinted):
    """(char, number of quadratic pieces in GGO_NATIVE, p0, p1, p2, p3, native points) for each cubic curve"""
    suffix = '' if hinted else ' unhinted'
    for (quality, char), sections in parse(path).items():
        if quality != 'default':
            continue
        bezier, native = sections.get('GGO_BEZIER' + suffix), sections.get('GGO_NATIVE' + suffix)
        if not bezier or not native:
            continue
        for (start, bezier_records), (_, native_records) in zip(bezier, native):
            if len(bezier_records) != len(native_records):
                print(path, char, 'has', len(bezier_records), 'GGO_BEZIER records and', len(native_records), 'GGO_NATIVE ones')
                continue
            last = start
            for (kind, points), (_, native_points) in zip(bezier_records, native_records):
                if kind == 'C':
                    yield char, len(native_points) - 1, last, points[0], points[1], points[2], native_points
                last = points[-1]

def pieces(d):
    c = max(abs(d[0]), abs(d[1]))
    if c <= 1:
        return 1
    n = 2
    while c > 10 * n ** 3:
        n += 1
    return n

def control_points(p0, p1, p2, p3, n):
    def at(t):
        s = 1 - t
        return tuple(s * s * s * p0[i] + 3 * s * s * t * p1[i] + 3 * s * t * t * p2[i] + t * t * t * p3[i] for i in range(2))
    def slope(t):
        s = 1 - t
        return tuple(3 * s * s * (p1[i] - p0[i]) + 6 * s * t * (p2[i] - p1[i]) + 3 * t * t * (p3[i] - p2[i]) for i in range(2))
    result = []
    for k in range(n):
        t0, t1 = k / n, (k + 1) / n
        a0, a3, d0, d1 = at(t0), at(t1), slope(t0), slope(t1)
        result.append(tuple((a0[i] + a3[i]) / 2 + (t1 - t0) * (d0[i] - d1[i]) / 4 for i in range(2)))
    return result

total = mismatches = 0
largest = 0.0
for path in sys.argv[1:]:
    for hinted in (False, True):
        for char, n, p0, p1, p2, p3, native in cubic_curves(path, hinted):
            total += 1
            d = tuple(p3[i] - 3 * p2[i] + 3 * p1[i] - p0[i] for i in range(2))
            if pieces(d) != n:
                mismatches += 1
                print('%s %s %s: %d pieces, the rule gives %d for d (%.5f,%.5f)' %
                      (path, 'hinted' if hinted else 'unhinted', char, n, pieces(d), d[0], d[1]))
                continue
            for expected, got in zip(control_points(p0, p1, p2, p3, n) + [p3], native):
                largest = max(largest, math.hypot(expected[0] - got[0], expected[1] - got[1]))
print('%d cubic curves, %d with another number of pieces, control points at most %.6f pixels off' % (total, mismatches, largest))
