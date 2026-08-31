#!/usr/bin/env python3
import struct, sys, os
def secs_of(d):
    pe=struct.unpack_from('<I',d,0x3c)[0]; opt=pe+24
    magic=struct.unpack_from('<H',d,opt)[0]
    nsec=struct.unpack_from('<H',d,pe+6)[0]; sh=opt+struct.unpack_from('<H',d,pe+20)[0]
    s=[]
    for i in range(nsec):
        o=sh+i*40; vs,va,rs,pr=struct.unpack_from('<IIII',d,o+8); s.append((va,max(vs,rs),pr))
    return pe,opt,magic,s
def r2o(secs,r):
    for va,sz,pr in secs:
        if va<=r<va+sz: return pr+(r-va)
def exports(path):
    try: d=open(path,'rb').read()
    except Exception: return None
    pe,opt,magic,secs=secs_of(d)
    dd=opt+(112 if magic==0x20b else 96)
    rva,sz=struct.unpack_from('<II',d,dd)
    if not rva: return set()
    o=r2o(secs,rva)
    if o is None: return set()
    ordbase,nfunc,nname,af,an,ao=struct.unpack_from('<IIIIII',d,o+16)
    names=set()
    if nname:
        no=r2o(secs,an)
        for i in range(nname):
            nr=struct.unpack_from('<I',d,no+4*i)[0]
            p=r2o(secs,nr)
            if p is None: continue
            e=d.index(b'\0',p); names.add(d[p:e].decode('ascii','replace'))
    return names
WINE=sys.argv[1]
import subprocess
cache={}
def wine_exports(dll):
    key=dll.lower()
    if key in cache: return cache[key]
    for cand in (dll, dll.lower(), dll.upper()):
        p=os.path.join(WINE,cand)
        if os.path.exists(p):
            cache[key]=exports(p); return cache[key]
    cache[key]=None; return None
missing=[]
for line in sys.stdin:
    line=line.rstrip()
    if not line.startswith('   ') or '==' in line: continue
    parts=line.split()
    if len(parts)<2: continue
    dll=parts[0]; funcs=[f for f in parts[1:] if not f.startswith('#') and f!='...']
    ex=wine_exports(dll)
    if ex is None:
        missing.append((dll,'<DLL 不存在>')); continue
    for f in funcs:
        if f not in ex: missing.append((dll,f))
for dll,f in missing: print("  %-38s %s" % (dll,f))
print("共", len(missing), "项缺失")
