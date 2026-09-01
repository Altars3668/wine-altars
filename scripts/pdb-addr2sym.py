#!/usr/bin/env python3
"""Map RVAs back to the nearest preceding public symbol.

A winedbg backtrace names frames as `module (+rva)`. Turning that into
something readable needs the PDB's S_PUB32 records sorted by RVA, then a
bisect per frame -- publics have no size, so "nearest preceding" is the
best that can be said, and the +delta is printed so a wildly large one is
visible as the warning it is.
"""
import struct, subprocess, sys, os, bisect, argparse

def sections(pe_path):
    d = open(pe_path, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    nsec = struct.unpack_from('<H', d, pe+6)[0]
    sh = pe + 24 + struct.unpack_from('<H', d, pe+20)[0]
    return [struct.unpack_from('<I', d, sh+i*40+12)[0] for i in range(nsec)]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('pe'); ap.add_argument('pdb')
    ap.add_argument('rvas', nargs='+', help='hex RVAs, with or without 0x')
    a = ap.parse_args()
    va = sections(a.pe)
    here = os.path.dirname(os.path.abspath(__file__))
    out = subprocess.run([sys.executable, os.path.join(here, 'pdb-symbols.py'), a.pdb],
                         capture_output=True, text=True).stdout
    tbl = []
    for line in out.splitlines():
        if ':' not in line or line.startswith('--'): continue
        addr, name = line.split(None, 1)
        seg_s, off_s = addr.split(':')
        seg, off = int(seg_s, 16), int(off_s, 16)
        if seg == 0 or seg > len(va): continue
        tbl.append((va[seg-1] + off, name.strip()))
    tbl.sort()
    keys = [t[0] for t in tbl]
    print("-- %d publics" % len(tbl), file=sys.stderr)
    for r in a.rvas:
        rva = int(r, 16)
        i = bisect.bisect_right(keys, rva) - 1
        if i < 0:
            print("%08x  <before first symbol>" % rva); continue
        print("%08x  %s+0x%x" % (rva, tbl[i][1], rva - tbl[i][0]))

if __name__ == '__main__':
    main()
