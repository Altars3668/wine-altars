#!/usr/bin/env python3
"""List a PE's delay-load imports. A delay import that cannot bind raises
VcppException(ERROR_PROC_NOT_FOUND) = 0xC06D007F instead of returning an error,
so any of these that Wine does not export is a candidate crash."""
import struct, sys
def parse(path):
    d=open(path,'rb').read()
    pe=struct.unpack_from('<I',d,0x3c)[0]
    opt=pe+24; magic=struct.unpack_from('<H',d,opt)[0]
    dd=opt+(112 if magic==0x20b else 96)
    rva,sz=struct.unpack_from('<II',d,dd+13*8)   # delay import descriptor
    if not rva: return []
    nsec=struct.unpack_from('<H',d,pe+6)[0]; sh=opt+struct.unpack_from('<H',d,pe+20)[0]
    secs=[]
    for i in range(nsec):
        o=sh+i*40; vs,va,rs,pr=struct.unpack_from('<IIII',d,o+8); secs.append((va,max(vs,rs),pr))
    def r2o(r):
        for va,s,pr in secs:
            if va<=r<va+s: return pr+(r-va)
        return None
    out=[]; o=r2o(rva)
    while True:
        attrs,name_rva,hmod,iat,ilt=struct.unpack_from('<IIIII',d,o)
        if name_rva==0: break
        no=r2o(name_rva)
        if no is None: break
        e=d.index(b'\0',no); dll=d[no:e].decode('ascii','replace')
        t=r2o(ilt); funcs=[]
        if t:
            while True:
                v=struct.unpack_from('<Q',d,t)[0] if magic==0x20b else struct.unpack_from('<I',d,t)[0]
                if v==0: break
                if not (v>>(63 if magic==0x20b else 31)):
                    fo=r2o(v & 0x7fffffff)
                    if fo:
                        en=d.index(b'\0',fo+2); funcs.append(d[fo+2:en].decode('ascii','replace'))
                else: funcs.append("#%d" % (v & 0xffff))
                t += 8 if magic==0x20b else 4
        out.append((dll,funcs)); o+=32
    return out
for p in sys.argv[1:]:
    r=parse(p)
    if r:
        print("==", p.rsplit('/',1)[-1])
        for dll,fs in r: print("   %-34s %s" % (dll, ' '.join(fs[:6]) + (' ...' if len(fs)>6 else '')))
