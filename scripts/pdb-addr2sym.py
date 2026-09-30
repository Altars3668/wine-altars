#!/usr/bin/env python3
"""Map RVAs back to the nearest preceding public symbol.

A winedbg backtrace names frames as `module (+rva)`. Turning that into
something readable needs the PDB's S_PUB32 records sorted by RVA, then a
bisect per frame -- publics have no size, so "nearest preceding" is the
best that can be said, and the +delta is printed so a wildly large one is
visible as the warning it is.

Office's DLLs (wwlib, mso and others) are reordered after linking (BBT), and
their PDBs keep the symbols at the addresses the linker gave them: the PDB
then carries the linker's section headers and an OMAP table from the
image's addresses back to those.  Without applying it, every name is some
unrelated function's (wwlib's own FMain export came out as a JSON
serializer).  So an RVA goes through OMAP_TO_SRC first, and the publics are
placed with the original section headers; `--no-omap` skips that.

`--lookup SUBSTRING` goes the other way: the image RVA of each public whose
name contains it (through OMAP_FROM_SRC), to disassemble or break at.
"""
import struct, sys, os, bisect, argparse, importlib.util

here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('pdb_symbols', os.path.join(here, 'pdb-symbols.py'))
pdb_symbols = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pdb_symbols)

S_PUB32 = 0x110E

def pe_sections(pe_path):
    d = open(pe_path, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    nsec = struct.unpack_from('<H', d, pe+6)[0]
    sh = pe + 24 + struct.unpack_from('<H', d, pe+20)[0]
    return [struct.unpack_from('<I', d, sh+i*40+12)[0] for i in range(nsec)]

def section_vas(raw):
    return [struct.unpack_from('<I', raw, i+12)[0] for i in range(0, len(raw) - 39, 40)]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('pe'); ap.add_argument('pdb')
    ap.add_argument('rvas', nargs='*', help='hex RVAs, with or without 0x')
    ap.add_argument('--no-omap', action='store_true', help='use the addresses as they are')
    ap.add_argument('--lookup', help='print the image RVAs of the publics whose names contain this ("" lists them all)')
    a = ap.parse_args()
    m = pdb_symbols.MSF(a.pdb)
    st = m.streams()
    dbi = m.stream(st, 3)
    sym_stream = struct.unpack_from('<H', dbi, 20)[0]
    # the optional debug header follows the other substreams: its stream numbers include the OMAP tables
    # and the section headers the linker made
    sizes = struct.unpack_from('<iiiiiiii', dbi, 24)
    modinfo, contrib, secmap, srcinfo, tsmap, _mfc, optdbg, ec = sizes
    o = 64 + modinfo + contrib + secmap + srcinfo + tsmap + ec
    dbg = struct.unpack_from('<%dH' % (optdbg // 2), dbi, o) if optdbg >= 2 else ()
    def dbg_stream(i):
        return dbg[i] if len(dbg) > i and dbg[i] != 0xffff else None

    omap = None
    va = pe_sections(a.pe)
    if not a.no_omap and dbg_stream(3) is not None and dbg_stream(10) is not None:
        raw = m.stream(st, dbg_stream(3))
        omap = [struct.unpack_from('<II', raw, i) for i in range(0, len(raw) - 7, 8)]
        omap_keys = [e[0] for e in omap]
        va = section_vas(m.stream(st, dbg_stream(10)))
        print("-- OMAP with %d entries, %d original sections" % (len(omap), len(va)), file=sys.stderr)
        raw = m.stream(st, dbg_stream(4)) if dbg_stream(4) is not None else b''
        omap_from = [struct.unpack_from('<II', raw, i) for i in range(0, len(raw) - 7, 8)]
        omap_from_keys = [e[0] for e in omap_from]

    data = m.stream(st, sym_stream)
    tbl = []
    o = 0
    while o + 4 <= len(data):
        ln, kind = struct.unpack_from('<HH', data, o)
        if ln < 2: break
        if kind == S_PUB32 and o + 2 + ln <= len(data):
            flags, off, seg = struct.unpack_from('<IIH', data, o+4)
            if 0 < seg <= len(va):
                e = data.index(b'\0', o+14)
                tbl.append((va[seg-1] + off, data[o+14:e].decode('utf-8', 'replace')))
        o += 2 + ln
        o = (o + 3) & ~3
    tbl.sort()
    keys = [t[0] for t in tbl]
    print("-- %d publics" % len(tbl), file=sys.stderr)
    if a.lookup is not None:
        for src, name in tbl:
            if a.lookup not in name: continue
            rva = src
            if omap is not None:
                i = bisect.bisect_right(omap_from_keys, src) - 1
                rva = omap_from[i][1] + (src - omap_from[i][0]) if i >= 0 and omap_from[i][1] else 0
            print("%08x  %s" % (rva, name))
    for r in a.rvas:
        rva = int(r, 16)
        src = rva
        if omap is not None:
            i = bisect.bisect_right(omap_keys, rva) - 1
            if i < 0 or not omap[i][1]:
                print("%08x  <not in the OMAP>" % rva); continue
            src = omap[i][1] + (rva - omap[i][0])
        i = bisect.bisect_right(keys, src) - 1
        if i < 0:
            print("%08x  <before first symbol>" % rva); continue
        print("%08x  %s+0x%x" % (rva, tbl[i][1], src - tbl[i][0]))

if __name__ == '__main__':
    main()
