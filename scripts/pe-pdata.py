#!/usr/bin/env python3
"""Find the x64 RUNTIME_FUNCTION covering an RVA.

A winedbg frame gives an address inside a function; .pdata gives that
function's real bounds, which public symbols cannot (they are folded and
name only the nearest preceding entry). Leaf functions have no .pdata
entry, so a miss here is information too.
"""
import struct, sys

def main():
    path = sys.argv[1]
    d = open(path,'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    nsec = struct.unpack_from('<H', d, pe+6)[0]
    optsz = struct.unpack_from('<H', d, pe+20)[0]
    opt = pe+24
    magic = struct.unpack_from('<H', d, opt)[0]
    ddoff = opt + (112 if magic==0x20b else 96)
    exc_rva, exc_sz = struct.unpack_from('<II', d, ddoff + 3*8)
    sh = opt+optsz
    secs=[]
    for i in range(nsec):
        o=sh+i*40
        va, rs, pr = struct.unpack_from('<III', d, o+12)[:3]
        vs = struct.unpack_from('<I', d, o+8)[0]
        secs.append((va, vs, pr))
    def off(rva):
        for va, vs, pr in secs:
            if va <= rva < va+max(vs,1): return pr + (rva-va)
        return None
    base = off(exc_rva)
    n = exc_sz//12
    tbl=[]
    for i in range(n):
        b,e,u = struct.unpack_from('<III', d, base+i*12)
        tbl.append((b,e,u))
    for arg in sys.argv[2:]:
        rva=int(arg,16)
        hit=[t for t in tbl if t[0] <= rva < t[1]]
        if hit:
            b,e,u = hit[0]
            print("%08x  func %08x..%08x  size %d  (+0x%x)" % (rva,b,e,e-b,rva-b))
        else:
            print("%08x  <no .pdata entry: leaf or chained>" % rva)

main()
