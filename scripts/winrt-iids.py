#!/usr/bin/env python3
"""Read WinRT interface IIDs out of a binary that consumes them.

`Windows.Security.winmd` is not in this prefix, and an IID that is guessed is
a guess with a plausible face on it.  C++/WinRT leaves the answer in every
binary it generates: for each type it consumes it emits a `name_v` (the
interface name, UTF-16, in .rdata) and a `guid_v` (its IID) laid down
immediately after it -- the 16 bytes that follow the name's NUL terminator,
rounded up to the next 4-byte boundary (the GUID starts with a DWORD).

Verify the rule on an IID you already know before trusting it for one you do
not -- pass --verify NAME=GUID and it will say so.

    scripts/winrt-iids.py WebView2Host.dll --match IWebAccountMonitor
    scripts/winrt-iids.py WebView2Host.dll --match IWebAuthenticationCore
    scripts/winrt-iids.py WebView2Host.dll \
        --verify IWebTokenRequestResult=c12a8305-d1f8-4483-8d54-38fe292784ff
"""
import argparse, re, struct, sys


def guid_str(b):
    d1, d2, d3 = struct.unpack_from("<IHH", b, 0)
    d4 = b[8:16]
    return "%08x-%04x-%04x-%s-%s" % (d1, d2, d3, d4[:2].hex(), d4[2:].hex())


def scan(path, want=None):
    """{fully.qualified.IName: {iid}} read out of the binary's own tables."""
    data = open(path, "rb").read()
    out = {}
    for m in re.finditer(rb"(?:[ -~]\x00){8,200}\x00\x00", data):
        name = m.group(0)[:-2].decode("utf-16-le", "ignore")
        short = name.rsplit(".", 1)[-1]
        if not short.startswith("I") or "." not in name or " " in name:
            continue
        if want and want.lower() not in name.lower():
            continue
        off = (m.end() + 3) & ~3           # the GUID is 4-aligned after the NUL
        if off + 16 > len(data):
            continue
        g = guid_str(data[off:off + 16])
        if g.startswith("00000000"):
            continue
        out.setdefault(name, set()).add(g)
        out.setdefault(short, set()).add(g)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("binary")
    ap.add_argument("--match", help="only names containing this")
    ap.add_argument("--verify", action="append", default=[],
                    help="NAME=GUID; check the rule reproduces a known IID")
    args = ap.parse_args()

    table = scan(args.binary, args.match)
    ok = True
    for pair in args.verify:
        name, guid = pair.split("=", 1)
        got = table.get(name) or scan(args.binary, name).get(name)
        good = got and guid.lower() in got
        print("verify %-28s %s%s" % (name, "OK" if good else "FAILED",
                                     "" if good else "  (got %s)" % (sorted(got) if got else "nothing")))
        ok = ok and good
    if args.verify and not ok:
        sys.exit("the rule did not reproduce a known IID here; do not trust its other answers")

    for name in sorted(table):
        for g in sorted(table[name]):
            print("%-52s %s" % (name, g))


if __name__ == "__main__":
    main()
