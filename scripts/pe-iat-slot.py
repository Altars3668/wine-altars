#!/usr/bin/env python3
"""Name the import behind an IAT slot RVA.

An `call *[rip+x]` in optimised Office code lands on an IAT (or delay-load
IAT) entry; the disassembly alone gives only the slot address. Walking both
descriptor tables turns that slot back into dll!function, which is the whole
point when the question is "what did Office call that returned NULL".
"""
import struct, sys

class PE:
    def __init__(self, path):
        self.d = open(path,'rb').read()
        d=self.d
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        self.nsec = struct.unpack_from('<H', d, pe+6)[0]
        optsz = struct.unpack_from('<H', d, pe+20)[0]
        opt = pe+24
        self.magic = struct.unpack_from('<H', d, opt)[0]
        self.base = struct.unpack_from('<Q', d, opt+24)[0] if self.magic==0x20b else struct.unpack_from('<I', d, opt+28)[0]
        ddoff = opt + (112 if self.magic==0x20b else 96)
        self.dd = [struct.unpack_from('<II', d, ddoff+i*8) for i in range(16)]
        sh = opt+optsz
        self.secs=[]
        for i in range(self.nsec):
            o=sh+i*40
            vs, va, rs, pr = struct.unpack_from('<IIII', d, o+8)
            self.secs.append((va, max(vs,rs), pr))
    def off(self, rva):
        for va, vs, pr in self.secs:
            if va <= rva < va+vs: return pr + (rva-va)
        return None
    def cstr(self, rva):
        o=self.off(rva)
        if o is None: return '?'
        e=self.d.index(b'\0', o)
        return self.d[o:e].decode('ascii','replace')

def slots(p):
    """yield (iat_rva, dll, name, kind)"""
    d=p.d
    imp_rva, imp_sz = p.dd[1]
    if imp_rva:
        o=p.off(imp_rva); i=0
        while True:
            oft, ts, fc, nm, ft = struct.unpack_from('<IIIII', d, o+i*20)
            if nm==0: break
            dll=p.cstr(nm)
            lut = oft or ft
            lo, io = p.off(lut), p.off(ft)
            j=0
            while True:
                v = struct.unpack_from('<Q', d, lo+j*8)[0] if p.magic==0x20b else struct.unpack_from('<I', d, lo+j*4)[0]
                if v==0: break
                step = 8 if p.magic==0x20b else 4
                ordbit = (1<<63) if p.magic==0x20b else (1<<31)
                name = ('#%d'%(v & 0xffff)) if v & ordbit else p.cstr((v & 0x7fffffff)+2)
                yield (ft + j*step, dll, name, 'import')
                j+=1
            i+=1
    dly_rva, dly_sz = p.dd[13]
    if dly_rva:
        o=p.off(dly_rva); i=0
        while True:
            attr, nm, hmod, ft, lut, bound, unload, tds = struct.unpack_from('<IIIIIIII', d, o+i*32)
            if nm==0: break
            dll=p.cstr(nm)
            lo=p.off(lut); j=0
            while True:
                v = struct.unpack_from('<Q', d, lo+j*8)[0] if p.magic==0x20b else struct.unpack_from('<I', d, lo+j*4)[0]
                if v==0: break
                step = 8 if p.magic==0x20b else 4
                ordbit = (1<<63) if p.magic==0x20b else (1<<31)
                name = ('#%d'%(v & 0xffff)) if v & ordbit else p.cstr((v & 0x7fffffff)+2)
                yield (ft + j*step, dll, name, 'delay')
                j+=1
            i+=1

def main():
    p=PE(sys.argv[1])
    want=[int(a,16) for a in sys.argv[2:]]
    tbl={r:(dll,n,k) for r,dll,n,k in slots(p)}
    for w in want:
        rva = w - p.base if w >= p.base else w
        if rva in tbl:
            dll,n,k = tbl[rva]
            print("%08x  %s!%s  (%s)" % (rva, dll, n, k))
        else:
            print("%08x  <not an IAT slot>" % rva)

main()
