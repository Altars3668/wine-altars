#!/usr/bin/env python3
"""Turn PDB segment:offset symbols into RVAs, filtered by name.

S_PUB32 records address a symbol as (segment, offset); a breakpoint needs an
RVA. The mapping is the PE's own section table, so the binary and its pdb have
to be read together.
"""
import struct, subprocess, sys, argparse, os

def sections(pe_path):
    d = open(pe_path, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    nsec = struct.unpack_from('<H', d, pe+6)[0]
    opt = pe + 24
    sh = opt + struct.unpack_from('<H', d, pe+20)[0]
    out = []
    for i in range(nsec):
        o = sh + i*40
        vs, va, rs, pr = struct.unpack_from('<IIII', d, o+8)
        out.append(va)
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('pe'); ap.add_argument('pdb'); ap.add_argument('pattern')
    ap.add_argument('--demangle-prefix', default='')
    a = ap.parse_args()
    va = sections(a.pe)
    here = os.path.dirname(os.path.abspath(__file__))
    syms = subprocess.run([sys.executable, os.path.join(here, 'pdb-symbols.py'),
                           a.pdb, '-g', a.pattern],
                          capture_output=True, text=True).stdout
    n = 0
    for line in syms.splitlines():
        if ':' not in line: continue
        addr, name = line.split(None, 1)
        seg_s, off_s = addr.split(':')
        seg, off = int(seg_s, 16), int(off_s, 16)
        if seg == 0 or seg > len(va): continue
        if name.startswith('__imp'): continue
        print("%08x %s" % (va[seg-1] + off, name.strip()))
        n += 1
    print("-- %d symbols" % n, file=sys.stderr)

if __name__ == '__main__':
    main()
