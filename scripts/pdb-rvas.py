#!/usr/bin/env python3
"""Turn PDB segment:offset symbols into RVAs, filtered by name.

S_PUB32 records address a symbol as (segment, offset); a breakpoint needs an
RVA. 带 OMAP 的优化二进制需先使用 PDB 的 SectionHdrOrig，再通过
OmapFromSrc 映射到最终 RVA；不能把优化前的符号偏移直接加到 PE 节地址上。
"""
import struct, subprocess, sys, argparse, os
import bisect
import importlib.util


def address_map(dbi, read_stream, pe_sections):
    if len(dbi) < 64:
        raise ValueError('truncated DBI header')
    debug_size = struct.unpack_from('<I', dbi, 48)[0]
    if not debug_size:
        return pe_sections, []
    debug_offset = 64 + sum(struct.unpack_from('<I', dbi, pos)[0]
                            for pos in (24, 28, 32, 36, 40, 52))
    if debug_size % 2 or debug_offset + debug_size > len(dbi):
        raise ValueError('invalid DBI optional debug header')
    indexes = struct.unpack_from('<%dH' % (debug_size // 2), dbi, debug_offset)
    if len(indexes) <= 4 or indexes[4] == 0xffff:
        return pe_sections, []
    if len(indexes) <= 10 or indexes[10] == 0xffff:
        raise ValueError('OmapFromSrc requires SectionHdrOrig')
    headers = read_stream(indexes[10])
    raw_map = read_stream(indexes[4])
    if not headers or len(headers) % 40 or not raw_map or len(raw_map) % 8:
        raise ValueError('invalid original sections or OMAP stream')
    original = [struct.unpack_from('<I', headers, offset + 12)[0]
                for offset in range(0, len(headers), 40)]
    mapping = list(struct.iter_unpack('<II', raw_map))
    if any(a[0] >= b[0] for a, b in zip(mapping, mapping[1:])):
        raise ValueError('OMAP source addresses must be strictly increasing')
    return original, mapping


def remap_rva(rva, mapping):
    if not mapping:
        return rva
    i = bisect.bisect_right(mapping, (rva, 0xffffffff)) - 1
    if i < 0 or not mapping[i][1]:
        return None
    source, target = mapping[i]
    return target + rva - source

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
    spec = importlib.util.spec_from_file_location('pdb_symbols', os.path.join(here, 'pdb-symbols.py'))
    symbols = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(symbols)
    msf = symbols.MSF(a.pdb)
    try:
        streams = msf.streams()
        va, mapping = address_map(msf.stream(streams, 3),
                                  lambda i: msf.stream(streams, i), va)
    finally:
        msf.f.close()
    syms = subprocess.run([sys.executable, os.path.join(here, 'pdb-symbols.py'),
                           a.pdb, '-g', a.pattern],
                          capture_output=True, text=True, check=True).stdout
    n = 0
    for line in syms.splitlines():
        if ':' not in line: continue
        addr, name = line.split(None, 1)
        seg_s, off_s = addr.split(':')
        seg, off = int(seg_s, 16), int(off_s, 16)
        if seg == 0 or seg > len(va): continue
        if name.startswith('__imp'): continue
        rva = remap_rva(va[seg-1] + off, mapping)
        if rva is None: continue
        print("%08x %s" % (rva, name.strip()))
        n += 1
    print("-- %d symbols" % n, file=sys.stderr)

if __name__ == '__main__':
    main()
