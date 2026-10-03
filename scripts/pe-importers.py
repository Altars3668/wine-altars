#!/usr/bin/env python3
"""Find the modules that import a function, by name or by ordinal, directly or delay-loaded.

    pe-importers.py <dll> <name-or-ordinal> <file>...

Prints each 64-bit file that imports it and the RVA of its IAT slot, which pe-xrefs.py then turns
into the code that calls through it.  Office exports nearly everything by ordinal only, so "who calls
MSO's GetVoiceRoamingSettings" is "who imports MSO.DLL #50922" (pe-export-rva.py gives the ordinal
of an RVA the other way round): run it over every module of the install,

    find .../root -iname '*.dll' -o -iname '*.exe' | xargs -d '\\n' pe-importers.py mso.dll 50922
"""
import struct, sys

def scan(path, want_dll, want):
    try:
        d = open(path, 'rb').read()
    except OSError:
        return
    if d[:2] != b'MZ':
        return
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    if d[pe:pe + 4] != b'PE\0\0' or struct.unpack_from('<H', d, pe + 24)[0] != 0x20b:
        return
    opt = pe + 24; dd = opt + 112
    nsec = struct.unpack_from('<H', d, pe + 6)[0]; sh = opt + struct.unpack_from('<H', d, pe + 20)[0]
    secs = [struct.unpack_from('<IIII', d, sh + i * 40 + 8) for i in range(nsec)]
    def off(r):
        for vs, va, rs, pr in secs:
            if va <= r < va + max(vs, rs):
                return pr + r - va
    def cstr(r):
        o = off(r)
        return d[o:d.index(b'\0', o)].decode('latin1') if o is not None else ''
    def matches(v):
        if v >> 63:
            return isinstance(want, int) and (v & 0xffff) == want
        return isinstance(want, str) and cstr((v & 0x7fffffff) + 2) == want
    def walk(names, iat, kind):
        k = 0; th = off(names)
        while th is not None:
            v = struct.unpack_from('<Q', d, th + 8 * k)[0]
            if not v:
                break
            if matches(v):
                print('%s %s slot %x' % (path, kind, iat + 8 * k))
            k += 1
    imp = struct.unpack_from('<I', d, dd + 8)[0]
    o = off(imp) if imp else None
    while o is not None:
        oft, _, _, name, ft = struct.unpack_from('<IIIII', d, o)
        if not name:
            break
        if cstr(name).lower() == want_dll:
            walk(oft or ft, ft, 'import')
        o += 20
    dimp = struct.unpack_from('<I', d, dd + 13 * 8)[0]
    o = off(dimp) if dimp else None
    while o is not None:
        _, name, _, iat, names = struct.unpack_from('<IIIII', d, o)
        if not name:
            break
        if cstr(name).lower() == want_dll:
            walk(names, iat, 'delay')
        o += 32

def main():
    want_dll = sys.argv[1].lower()
    want = int(sys.argv[2]) if sys.argv[2].isdigit() else sys.argv[2]
    for path in sys.argv[3:]:
        scan(path, want_dll, want)

main()
