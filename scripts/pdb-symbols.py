#!/usr/bin/env python3
"""List the public symbols of a PDB, including ones llvm-pdbutil refuses.

Office's PDBs use a 1024-byte MSF block, so a large one needs more block
numbers for its stream directory than a single block can hold, and the block
map spills across several consecutive blocks. llvm-pdbutil reads only the
first and gives up with "Too many directory blocks" -- which is exactly the
case for wwlib.pdb (352 blocks needed, 256 per block) and MSO.pdb.

Only what is needed to reach S_PUB32: the MSF directory, stream 3 (DBI) for
the symbol-record stream index, and that stream's records.
"""
import struct, sys, argparse

S_PUB32 = 0x110E

class MSF:
    def __init__(self, path):
        self.f = open(path, 'rb')
        sb = self.f.read(56)
        if not sb.startswith(b'Microsoft C/C++ MSF 7.00'):
            raise SystemExit('not an MSF 7.00 pdb')
        (self.block_size, _free, self.num_blocks,
         self.dir_bytes, _unk, self.block_map_addr) = struct.unpack_from('<IIIIII', sb, 32)

    def block(self, n):
        self.f.seek(n * self.block_size)
        return self.f.read(self.block_size)

    def read_blocks(self, nums, size):
        out = b''.join(self.block(n) for n in nums)
        return out[:size]

    def directory(self):
        n_dir_blocks = (self.dir_bytes + self.block_size - 1) // self.block_size
        # the block map may itself span several consecutive blocks
        map_bytes = n_dir_blocks * 4
        n_map_blocks = (map_bytes + self.block_size - 1) // self.block_size
        raw = b''.join(self.block(self.block_map_addr + i) for i in range(n_map_blocks))
        dir_blocks = struct.unpack_from('<%dI' % n_dir_blocks, raw, 0)
        return self.read_blocks(dir_blocks, self.dir_bytes)

    def streams(self):
        d = self.directory()
        n = struct.unpack_from('<I', d, 0)[0]
        sizes = struct.unpack_from('<%dI' % n, d, 4)
        o = 4 + 4*n
        out = []
        for s in sizes:
            if s == 0xffffffff:
                out.append((0, [])); continue
            cnt = (s + self.block_size - 1) // self.block_size
            blocks = struct.unpack_from('<%dI' % cnt, d, o)
            o += 4*cnt
            out.append((s, list(blocks)))
        return out

    def stream(self, streams, i):
        size, blocks = streams[i]
        return self.read_blocks(blocks, size)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('pdb'); ap.add_argument('-g', '--grep', help='case-insensitive substring')
    a = ap.parse_args()
    m = MSF(a.pdb)
    st = m.streams()
    dbi = m.stream(st, 3)
    if len(dbi) < 64:
        raise SystemExit('no DBI stream')
    sym_stream = struct.unpack_from('<H', dbi, 20)[0]
    data = m.stream(st, sym_stream)
    pat = a.grep.lower() if a.grep else None
    o, n = 0, 0
    while o + 4 <= len(data):
        ln, kind = struct.unpack_from('<HH', data, o)
        if ln < 2: break
        if kind == S_PUB32 and o + 2 + ln <= len(data):
            flags, off, seg = struct.unpack_from('<IIH', data, o+4)
            e = data.index(b'\0', o+14)
            name = data[o+14:e].decode('utf-8', 'replace')
            if pat is None or pat in name.lower():
                print("%04x:%08x  %s" % (seg, off, name)); n += 1
        o += 2 + ln
        o = (o + 3) & ~3
    print("-- %d symbols%s" % (n, ' matching' if pat else ''), file=sys.stderr)

if __name__ == '__main__':
    main()
