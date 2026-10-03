#!/usr/bin/env python3
"""Decode the C++ exception handling (FH4) of the functions an address is in, and its state there.

    pe-fh4.py <module.dll> <rva-hex>...

For each RVA: the function's RUNTIME_FUNCTION, its language handler (an import such as
VCRUNTIME140_1!__CxxFrameHandler4, or code: a __GSHandlerCheck_EH4 the module links in), the FuncInfo4
header -- catch funclet, separated, unwind map, try blocks, EHs, noexcept -- and, for FH4, the whole
of its tables as vcruntime140_1 reads them: unwind map, try blocks with their catch blocks (types by
mangled name, ... for catch (...)), the IP to state map, and the state at the RVA, which for a frame
is its return address.  That is what decides which catch block takes an exception: Word's exit died
in a noexcept destructor whose catch (...) covered states 1 to 3, the call in it being state 2, while
Wine's handler, finding 0 in the state it hands itself over in fiber local storage, looked at state 0.
"""
import bisect, struct, sys

class PE:
    def __init__(self, path):
        self.d = d = open(path, 'rb').read()
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        opt = pe + 24; self.dd = opt + 112
        nsec = struct.unpack_from('<H', d, pe + 6)[0]; sh = opt + struct.unpack_from('<H', d, pe + 20)[0]
        self.secs = [struct.unpack_from('<IIII', d, sh + i * 40 + 8) for i in range(nsec)]
        exc, size = struct.unpack_from('<II', d, self.dd + 3 * 8)
        self.pdata = [struct.unpack_from('<III', d, self.off(exc) + 12 * i) for i in range(size // 12)]
        self.starts = [p[0] for p in self.pdata]
        self.imports = {}
        imp = struct.unpack_from('<I', d, self.dd + 8)[0]
        o = self.off(imp) if imp else None
        while o is not None:
            oft, _, _, name, ft = struct.unpack_from('<IIIII', d, o)
            if not name:
                break
            dll = self.cstr(name); k = 0
            while True:
                v = struct.unpack_from('<Q', d, self.off(oft or ft) + 8 * k)[0]
                if not v:
                    break
                self.imports[ft + 8 * k] = dll + '!' + ('#%d' % (v & 0xffff) if v >> 63 else self.cstr(v + 2))
                k += 1
            o += 20

    def off(self, r):
        for vs, va, rs, pr in self.secs:
            if va <= r < va + max(vs, rs):
                return pr + r - va

    def u8(self, r): return self.d[self.off(r)]
    def u32(self, r): return struct.unpack_from('<I', self.d, self.off(r))[0]
    def cstr(self, r):
        o = self.off(r)
        return self.d[o:self.d.index(b'\0', o)].decode('latin1')

    def handler_name(self, h):
        o = self.off(h)
        if self.d[o:o + 2] == b'\xff\x25':
            slot = h + 6 + struct.unpack_from('<i', self.d, o + 2)[0]
            return self.imports.get(slot, 'slot %x' % slot)
        return 'code %x' % h

class Reader:
    """vcruntime140_1's compressed integers and RVAs, at an RVA of the module"""
    def __init__(self, pe, rva): self.pe = pe; self.r = rva
    def byte(self): v = self.pe.u8(self.r); self.r += 1; return v
    def rva(self): v = self.pe.u32(self.r); self.r += 4; return v
    def uint(self):
        o = self.pe.off(self.r); p = self.pe.d[o:o + 5]
        for size, mask, val, parts in ((1, 1, 0, ()), (2, 3, 1, (6,)), (3, 7, 3, (5, 13)), (4, 15, 7, (4, 12, 20))):
            if p[0] & mask == val:
                v = p[0] >> size
                for i, shift in enumerate(parts):
                    v += p[i + 1] << shift
                self.r += size
                return v
        self.r += 5
        return struct.unpack_from('<I', p, 1)[0]

def type_name(pe, rva):
    return pe.cstr(rva + 16) if rva else '...'

def dump_fh4(pe, fstart, fi, rva):
    p = Reader(pe, fi)
    hdr = p.byte()
    if hdr & 4:
        print('  bbt flags %#x' % p.uint())
    um = p.rva() if hdr & 8 else None
    tm = p.rva() if hdr & 0x10 else None
    ipm = p.rva()
    if hdr & 2:
        q = Reader(pe, ipm)
        for i in range(q.uint()):
            f, m = q.rva(), q.rva()
            if f == fstart:
                ipm = m
                break
        else:
            # MSO's catch funclets carry an empty one: no state anywhere in them
            print('  separated, with no IP to state map for this function: state -1 throughout')
            return
    if hdr & 1:
        print('  establisher frame of the parent at %#x' % p.uint())
    if um:
        q = Reader(pe, um); n = q.uint()
        print('  unwind map, %d states:' % n)
        for i in range(n):
            t = q.uint(); back = t >> 2; t &= 3; what = ('nothing', 'destructor of object', 'destructor through pointer', 'funclet')[t]
            h = o = None
            if t in (1, 2):
                h = q.rva(); o = q.uint()
            elif t == 3:
                h = q.rva()
            print('    %d: %s%s%s, back %d byte(s)' % (i, what, ' at %x' % h if h else '', ' frame+%#x' % o if o is not None else '', back))
    if tm:
        q = Reader(pe, tm); n = q.uint()
        for i in range(n):
            lo, hi, catch = q.uint(), q.uint(), q.uint(); cb = q.rva()
            print('  try block %d: states %d to %d, catch state %d' % (i, lo, hi, catch))
            if not cb:
                continue
            r = Reader(pe, cb)
            for j in range(r.uint()):
                ch = r.byte()
                flags = r.uint() if ch & 1 else 0
                ti = r.rva() if ch & 2 else 0
                obj = r.uint() if ch & 4 else None
                h = r.rva(); conts = []
                for k in range({0x10: 1, 0x20: 2}.get(ch & 0x30, 0)):
                    conts.append(r.rva() if ch & 8 else r.uint() + fstart)
                print('    catch (%s) flags %#x%s, funclet %x, continues at %s' % (type_name(pe, ti), flags,
                      ', object at frame+%#x' % obj if obj is not None else '', h, ' or '.join('%x' % c for c in conts)))
    q = Reader(pe, ipm); ip = fstart; state = -1; entries = []
    for i in range(q.uint()):
        ip += q.uint(); entries.append((ip, q.uint() - 1))
    print('  IP to state: ' + ', '.join('%x %d' % e for e in entries))
    for e in entries:
        if rva < e[0]:
            break
        state = e[1]
    print('  state at %x: %d' % (rva, state))

def main():
    pe = PE(sys.argv[1])
    for a in sys.argv[2:]:
        rva = int(a, 16)
        i = bisect.bisect_right(pe.starts, rva) - 1
        if i < 0 or not pe.pdata[i][0] <= rva < pe.pdata[i][1]:
            print('%x: no RUNTIME_FUNCTION, a leaf' % rva)
            continue
        b, e, ui = pe.pdata[i]
        line = '%x: function %x-%x' % (rva, b, e)
        while True:
            flags = pe.u8(ui) >> 3; count = pe.u8(ui + 2)
            after = ui + 4 + 2 * ((count + 1) & ~1)
            if flags & 4:   # chained to the unwind information of another RUNTIME_FUNCTION
                cb, ce, ui = struct.unpack_from('<III', pe.d, pe.off(after))
                line += ', chained to %x' % cb
                continue
            break
        if not flags & 3:
            print(line + ', no handler')
            continue
        h = pe.u32(after); name = pe.handler_name(h)
        line += ', handler %s' % name
        if 'CxxFrameHandler4' not in name and 'code' not in name:
            print(line)
            continue
        # a handler in the module's own code is taken for __GSHandlerCheck_EH4, whose data starts the same way
        try:
            fi = pe.u32(after + 4); hdr = pe.u8(fi)
            bits = [n for bit, n in ((1, 'catch funclet'), (2, 'separated'), (4, 'bbt'), (8, 'unwind map'),
                                     (0x10, 'try blocks'), (0x20, 'EHs'), (0x40, 'noexcept')) if hdr & bit]
            print(line + ', FuncInfo4 at %x: %s' % (fi, ', '.join(bits) or 'nothing'))
            dump_fh4(pe, b, fi, rva)
        except (TypeError, IndexError, struct.error, UnicodeDecodeError, ValueError):
            print('  (the handler data is not FH4)')

main()
