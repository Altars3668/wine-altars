#!/usr/bin/env python3
"""Decode a ThrowInfo at a given RVA into the C++ type names it can be caught as.

A 0xE06D7363 record carries ExceptionInformation[2] = ThrowInfo pointer and
[3] = the throwing module's image base, so the RVA is [2]-[3] and everything
below is static data in that module's file.

    pe-throwinfo.py <module.dll> <rva-hex>...
"""
import struct, sys

def load(path):
    d = open(path, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    optsz = struct.unpack_from('<H', d, pe + 20)[0]
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    secs = []
    for i in range(nsec):
        o = pe + 24 + optsz + i * 40
        vs, va, rs, pr = struct.unpack_from('<IIII', d, o + 8)
        secs.append((va, max(vs, rs), pr))
    return d, secs

def off(secs, rva):
    for va, sz, pr in secs:
        if va <= rva < va + sz:
            return pr + (rva - va)
    return None

def cstr(d, o):
    e = d.index(b'\0', o)
    return d[o:e].decode('ascii', 'replace')

def decode(path, rva):
    d, secs = load(path)
    o = off(secs, rva)
    if o is None:
        return ["<RVA 不在任何节里>"]
    attrs, unwind, fwd, cta = struct.unpack_from('<IiiI', d, o)
    if not cta:
        return ["<没有 CatchableTypeArray>"]
    o2 = off(secs, cta)
    n = struct.unpack_from('<i', d, o2)[0]
    out = []
    for i in range(min(n, 16)):
        ct = struct.unpack_from('<I', d, o2 + 4 + 4 * i)[0]
        o3 = off(secs, ct)
        if o3 is None:
            continue
        props, ptype = struct.unpack_from('<Ii', d, o3)
        o4 = off(secs, ptype)
        if o4 is None:
            continue
        out.append(cstr(d, o4 + 16))          # TypeDescriptor.name
    return out

if __name__ == '__main__':
    mod = sys.argv[1]
    for a in sys.argv[2:]:
        names = decode(mod, int(a, 16))
        print(f"ThrowInfo RVA {a}:")
        for nm in names:
            print(f"    {nm}")
