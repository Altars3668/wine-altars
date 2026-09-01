#!/usr/bin/env python3
"""Print an enum's members from a PDB's type stream.

Public symbols name functions; they do not tell you what a numeric error code
means. Enum member names live in the TPI stream (stream 2) as LF_ENUM pointing
at an LF_FIELDLIST of LF_ENUMERATE records. That is how an error like Word's
"(6)" becomes a name instead of a guess.
"""
import struct, sys, argparse
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from importlib.machinery import SourceFileLoader
MSF = SourceFileLoader('pdbsym', __file__.rsplit('/', 1)[0] + '/pdb-symbols.py').load_module().MSF

LF_ENUM, LF_FIELDLIST, LF_ENUMERATE, LF_INDEX = 0x1507, 0x1203, 0x1502, 0x1404

def numeric(d, o):
    """A numeric leaf: a bare u16 below 0x8000, otherwise a tagged value."""
    v = struct.unpack_from('<H', d, o)[0]
    if v < 0x8000: return v, o + 2
    kind = v
    o += 2
    fmt = {0x8000:'<b',0x8001:'<h',0x8002:'<i',0x8003:'<h',0x8004:'<i',
           0x8005:'<f',0x8006:'<d',0x8009:'<q',0x800a:'<Q'}.get(kind)
    if not fmt: return None, o
    n = struct.calcsize(fmt)
    return struct.unpack_from(fmt, d, o)[0], o + n

def cstr(d, o):
    e = d.index(b'\0', o)
    return d[o:e].decode('utf-8', 'replace'), e + 1

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('pdb'); ap.add_argument('name', help='enum name (substring ok)')
    a = ap.parse_args()
    m = MSF(a.pdb); st = m.streams()
    tpi = m.stream(st, 2)
    ver, hdr_size, ti_begin, ti_end, rec_bytes = struct.unpack_from('<IIIII', tpi, 0)
    recs, o, ti = {}, hdr_size, ti_begin
    end = hdr_size + rec_bytes
    while o + 4 <= end and o + 4 <= len(tpi):
        ln = struct.unpack_from('<H', tpi, o)[0]
        kind = struct.unpack_from('<H', tpi, o+2)[0]
        recs[ti] = (kind, o+4, o+2+ln)
        o += 2 + ln; ti += 1
    want = a.name.lower()
    found = 0
    for idx, (kind, s, e) in recs.items():
        if kind != LF_ENUM: continue
        count, prop, utype, field = struct.unpack_from('<HHII', tpi, s)
        nm, _ = cstr(tpi, s + 12)
        if want not in nm.lower(): continue
        found += 1
        print("enum %s  (%d members, fieldlist #%d)" % (nm, count, field))
        fk = recs.get(field)
        if not fk or fk[0] != LF_FIELDLIST:
            print("   (fieldlist not in this stream)"); continue
        _, fs, fe = fk
        p = fs
        while p < fe:
            leaf = struct.unpack_from('<H', tpi, p)[0]
            if leaf == LF_ENUMERATE:
                p += 2
                attr = struct.unpack_from('<H', tpi, p)[0]; p += 2
                val, p = numeric(tpi, p)
                enm, p = cstr(tpi, p)
                print("   %4s  %s" % (val, enm))
                while p < fe and tpi[p] >= 0xf0: p += 1   # padding
            else:
                break
    if not found: print("no enum matching %r" % a.name, file=sys.stderr); sys.exit(1)

if __name__ == '__main__':
    main()
