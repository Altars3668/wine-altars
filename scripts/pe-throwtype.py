#!/usr/bin/env python3
# Usage: scripts/pe-throwtype.py <module.dll> <ThrowInfo RVA>
#
# The RVA comes from a Wine "+seh" trace of an EXCEPTION_WINE_CXX_EXCEPTION:
#   info[2] - info[3]   (ThrowInfo pointer minus the throwing module base)
"""Read the C++ type a ThrowInfo names, straight out of the PE.

An MSVC C++ throw raises 0xe06d7363 with ExceptionInformation[2] = &ThrowInfo
and [3] = the throwing module's base, so the RVA is one subtraction away.  From
there every pointer inside is itself an RVA, ending at a TypeDescriptor whose
name is a plain mangled string.
"""
import struct, sys

def sections(d):
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    nsec = struct.unpack_from('<H', d, pe+6)[0]
    opt  = struct.unpack_from('<H', d, pe+20)[0]
    out=[]
    for i in range(nsec):
        o = pe+24+opt+i*40
        name = d[o:o+8].rstrip(b'\0').decode('latin1')
        vsize, va, rsize, raw = struct.unpack_from('<IIII', d, o+8)
        out.append((name, va, vsize, raw, rsize))
    return out

def r2o(secs, rva):
    for name, va, vsize, raw, rsize in secs:
        if va <= rva < va + max(vsize, rsize):
            return raw + (rva - va)
    return None

def main(path, rva):
    d = open(path,'rb').read()
    s = sections(d)
    o = r2o(s, rva)
    if o is None: print("ThrowInfo RVA not in any section"); return
    attrs, unwind, fwd, cta = struct.unpack_from('<Iiii', d, o)
    print("ThrowInfo rva %#x attrs %#x pCatchableTypeArray rva %#x" % (rva, attrs, cta))
    o = r2o(s, cta)
    n = struct.unpack_from('<i', d, o)[0]
    print("  catchable types: %d" % n)
    for i in range(n):
        ct = struct.unpack_from('<i', d, o+4+4*i)[0]
        co = r2o(s, ct)
        props, ptype = struct.unpack_from('<Ii', d, co)
        to = r2o(s, ptype)
        name = d[to+16:d.index(b'\0', to+16)].decode('latin1')
        print("    [%d] props %#x  type %s" % (i, props, name))

main(sys.argv[1], int(sys.argv[2], 16))
