#!/usr/bin/env python3
"""Replay a +d3d11 log and say what each draw was aimed at.

Every other instrument in this tree stops at the surface: it can say the
pixels Office handed over were right, or that the ones on screen were wrong.
Neither answers the question left over from the Backstage rail -- *was a draw
ever issued over that part of the window at all* -- because a region that
holds exactly the clear colour looks the same whether nothing was drawn there
or something was drawn from an empty texture.

The `altars-` traces in dlls/d3d11 (patch 0018) print the pieces: render
targets and shader resource views with the size of the resource behind them,
viewports, scissor rectangles, and every draw.  This puts them back together
-- per device context, in order -- so each draw carries the state in force
when it was issued.

    scripts/d3d-draw-map.py /tmp/word.log                 # summary per target
    scripts/d3d-draw-map.py /tmp/word.log --target 1438x808
    scripts/d3d-draw-map.py /tmp/word.log --covers 0,48,66,808
"""
import argparse
import re
from collections import Counter, defaultdict

RE = {
    "rtvres": re.compile(r"altars-rtv resource (\w+) (\d+)x(\d+) view (\w+)"),
    "srvres": re.compile(r"altars-srvres resource (\w+) (\d+)x(\d+) view (\w+)"),
    "rt": re.compile(r"altars-rt ctx (\w+) slot (\d+) view (\w+|\(none\))"),
    "vp": re.compile(r"altars-viewport ctx (\w+) ([-\d.]+),([-\d.]+) ([\d.]+)x([\d.]+)"),
    "sc": re.compile(r"altars-scissor ctx (\w+) \((-?\d+),(-?\d+)\)-(?:\((-?\d+),(-?\d+)\))?"),
    "scn": re.compile(r"altars-scissor ctx (\w+) \(none\)"),
    "srv": re.compile(r"altars-srv ctx (\w+) slot (\d+) view (\w+)"),
    "draw": re.compile(r"altars-draw ctx (\w+) kind ([\w-]+) count (\d+)(?: instances (\d+))?"),
}


def parse(path):
    view_size = {}          # view pointer -> (w, h, kind)
    state = defaultdict(lambda: {"rt": None, "vp": None, "sc": None, "srv": {}})
    draws = []

    for line in open(path, errors="replace"):
        m = RE["rtvres"].search(line)
        if m:
            view_size[m.group(4)] = (int(m.group(2)), int(m.group(3)), "rtv")
            continue
        m = RE["srvres"].search(line)
        if m:
            view_size[m.group(4)] = (int(m.group(2)), int(m.group(3)), "srv")
            continue
        m = RE["rt"].search(line)
        if m:
            state[m.group(1)]["rt"] = None if m.group(3) == "(none)" else m.group(3)
            continue
        m = RE["vp"].search(line)
        if m:
            state[m.group(1)]["vp"] = tuple(float(v) for v in m.groups()[1:])
            continue
        m = RE["sc"].search(line)
        if m:
            g = m.groups()
            state[g[0]]["sc"] = (int(g[1]), int(g[2]),
                                 int(g[3]) if g[3] else None, int(g[4]) if g[4] else None)
            continue
        if RE["scn"].search(line):
            state[RE["scn"].search(line).group(1)]["sc"] = None
            continue
        m = RE["srv"].search(line)
        if m:
            state[m.group(1)]["srv"][int(m.group(2))] = m.group(3)
            continue
        m = RE["draw"].search(line)
        if m:
            ctx = m.group(1)
            st = state[ctx]
            draws.append({
                "ctx": ctx, "kind": m.group(2), "count": int(m.group(3)),
                "instances": int(m.group(4) or 1),
                "rt": st["rt"], "vp": st["vp"], "sc": st["sc"],
                "srv": dict(st["srv"]),
            })
    return view_size, draws


def size_of(view_size, view):
    if view is None:
        return "(no target)"
    w, h, _ = view_size.get(view, (0, 0, "?"))
    return "%dx%d" % (w, h) if w else "view %s (size unknown)" % view


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--target", help="only draws whose render target is this size, e.g. 1438x808")
    ap.add_argument("--covers", help="x,y,w,h -- report whether any draw's scissor covers it")
    ap.add_argument("--limit", type=int, default=25)
    args = ap.parse_args()

    view_size, draws = parse(args.log)
    print("%d draws, %d views with a known size" % (len(draws), len(view_size)))

    per_target = Counter(size_of(view_size, d["rt"]) for d in draws)
    print("\ndraws per render target:")
    for k, n in per_target.most_common(12):
        print("  %-24s %d" % (k, n))

    sel = draws
    if args.target:
        sel = [d for d in draws if size_of(view_size, d["rt"]) == args.target]
        print("\n%d draws into a %s target" % (len(sel), args.target))

    scissors = Counter(d["sc"] for d in sel)
    print("\nscissor rectangles in force for those draws:")
    for k, n in scissors.most_common(args.limit):
        print("  %-32s %d" % (str(k), n))

    vps = Counter(d["vp"] for d in sel)
    print("\nviewports:")
    for k, n in vps.most_common(8):
        print("  %-32s %d" % (str(k), n))

    if args.covers:
        x, y, w, h = (int(v) for v in args.covers.split(","))
        hit = 0
        for d in sel:
            sc = d["sc"]
            if sc is None:                      # no scissor: the whole target
                hit += 1
                continue
            l, t, r, b = sc
            if r is None:
                continue
            if l < x + w and r > x and t < y + h and b > y:
                hit += 1
        print("\ndraws whose scissor overlaps (%d,%d %dx%d): %d of %d"
              % (x, y, w, h, hit, len(sel)))
        srvs = Counter()
        for d in sel:
            for v in d["srv"].values():
                srvs[size_of(view_size, v)] += 1
        print("shader resources bound across those draws:")
        for k, n in srvs.most_common(8):
            print("  %-24s %d" % (k, n))


if __name__ == "__main__":
    main()
