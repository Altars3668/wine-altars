#!/usr/bin/env python3
"""Ordinal -> RVA for a PE that exports by ordinal only (all of Office does)."""
import struct,sys
d=open(sys.argv[1],'rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]
optsz=struct.unpack_from('<H',d,pe+20)[0]; opt=pe+24
magic=struct.unpack_from('<H',d,opt)[0]
ddoff=opt+(112 if magic==0x20b else 96)
exp_rva,_=struct.unpack_from('<II',d,ddoff)
nsec=struct.unpack_from('<H',d,pe+6)[0]; sh=opt+optsz
secs=[]
for i in range(nsec):
    o=sh+i*40; vs,va,rs,pr=struct.unpack_from('<IIII',d,o+8); secs.append((va,max(vs,rs),pr))
def off(r):
    for va,vs,pr in secs:
        if va<=r<va+vs: return pr+(r-va)
o=off(exp_rva)
ordBase,nFunc,nName,funcRva=struct.unpack_from('<IIII',d,o+16)
for a in sys.argv[2:]:
    w=int(a); i=w-ordBase
    if 0<=i<nFunc:
        print("ordinal %d -> RVA 0x%x" % (w, struct.unpack_from('<I',d,off(funcRva)+i*4)[0]))
    else:
        print("ordinal %d out of range (base %d, count %d)" % (w,ordBase,nFunc))
