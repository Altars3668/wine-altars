#!/usr/bin/python3
"""oracle.py - is a two-pass job what the one-sided job says it should be?

    oracle.py <long|short> <one-sided.urf> <fronts.urf> <backs.urf>

The one-sided job is the same document with the same options printed straight
on the real queue: its sheet sides are the reference.  Manual duplex with the
stack put back as it came out must then give

  fronts = sides 1, 3, 5, ...                       as they are
  backs  = sides ..., 6, 4, 2, last first, with a blank first when the count
           is odd (that sheet has no back); turned 180 degrees for the long
           edge (top-left lands bottom-right), as they are for the short edge

and the backs, and only they, from the manual feeder (media position 4).
Pages are compared by what test/urfsheets.py reads off them: the number of
squares and the corner the first one is in.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from urfsheets import summary

TURN = {"TL": "BR", "BR": "TL", "TR": "BL", "BL": "TR", "?": "?"}

def turned(side):
    if side == "b":
        return side
    n, corner = side.split("@")
    return "%s@%s" % (n, TURN[corner])

def main(edge, ref_path, front_path, back_path):
    ref = summary(ref_path)
    sides = [s for s, *_ in ref]
    fronts = sides[0::2]
    backs = sides[1::2] + (["b"] if len(sides) % 2 else [])
    backs = backs[::-1]
    if edge == "long":
        backs = [turned(s) for s in backs]
    got_f = summary(front_path)
    got_b = summary(back_path)
    ok = True
    for what, want, got, pos in (("fronts", fronts, got_f, {p for _, p, *_ in ref}),
                                 ("backs", backs, got_b, {4})):
        sides_got = [s for s, *_ in got]
        pos_got = {p for _, p, *_ in got}
        good = sides_got == want and pos_got <= pos
        ok &= good
        print("   %-6s %s  want %s (pos %s), got %s (pos %s)" % (
            what, "ok  " if good else "FAIL", " ".join(want), ",".join(map(str, sorted(pos))),
            " ".join(sides_got), ",".join(map(str, sorted(pos_got)))))
    if "?" in " ".join(sides) or not any(s != "b" for s in sides):
        print("   (reference has unreadable sides: %s)" % " ".join(sides))
        ok = False
    return ok

if __name__ == "__main__":
    if len(sys.argv) != 5 or sys.argv[1] not in ("long", "short"):
        sys.exit(__doc__)
    sys.exit(0 if main(*sys.argv[1:]) else 1)
