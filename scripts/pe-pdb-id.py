#!/usr/bin/env python3
"""Print the symbol-server path for a PE: <pdb>/<GUID><age>/<pdb>.

Microsoft's symbol servers are addressed by the CodeView RSDS record a linker
stamps into the binary -- a GUID plus an age plus the pdb's base name. Reading
it out of the file is the only way to ask whether symbols for *this exact build*
are published, and it is also how you find out that they are not.
"""
import struct, sys, uuid, os

def codeview(path):
    d = open(path, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    opt = pe + 24
    magic = struct.unpack_from('<H', d, opt)[0]
    dd = opt + (112 if magic == 0x20b else 96)
    rva, size = struct.unpack_from('<II', d, dd + 6*8)      # debug directory
    if not rva:
        return None
    nsec = struct.unpack_from('<H', d, pe+6)[0]
    sh = opt + struct.unpack_from('<H', d, pe+20)[0]
    secs = []
    for i in range(nsec):
        o = sh + i*40
        vs, va, rs, pr = struct.unpack_from('<IIII', d, o+8)
        secs.append((va, max(vs, rs), pr))
    def r2o(r):
        for va, sz, pr in secs:
            if va <= r < va+sz: return pr + (r-va)
    o = r2o(rva)
    if o is None: return None
    for i in range(size // 28):
        e = o + i*28
        dtype = struct.unpack_from('<I', d, e+12)[0]
        dsize = struct.unpack_from('<I', d, e+16)[0]
        doff  = struct.unpack_from('<I', d, e+24)[0]
        if dtype != 2 or not doff:                          # IMAGE_DEBUG_TYPE_CODEVIEW
            continue
        if d[doff:doff+4] != b'RSDS':
            continue
        g = uuid.UUID(bytes_le=d[doff+4:doff+20])
        age = struct.unpack_from('<I', d, doff+20)[0]
        end = d.index(b'\0', doff+24)
        pdb = d[doff+24:end].decode('utf-8', 'replace').replace('\\', '/').split('/')[-1]
        return pdb, ('%s%X' % (g.hex.upper(), age)), age
    return None

for p in sys.argv[1:]:
    r = codeview(p)
    name = os.path.basename(p)
    if not r:
        print("%-34s (no CodeView record)" % name)
        continue
    pdb, key, age = r
    print("%-34s %s/%s/%s" % (name, pdb, key, pdb))
