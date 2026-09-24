#!/usr/bin/env python3
"""Read a Windows Runtime metadata file (.winmd) and answer two questions:

    what is the IID of interface X, and what methods does it have?

Wine implements WinRT classes but not always every interface on them, and a
caller that QueryInterfaces for a missing one gets E_NOINTERFACE — which
C++/WinRT turns into a thrown exception that usually kills the caller. To add
the missing interface you need its IID and its exact vtable order, and both are
in the .winmd Windows ships in C:\\Windows\\System32\\WinMetadata.

Just enough of ECMA-335 to walk TypeDef / MethodDef / Param / CustomAttribute.
No dependencies.

    winmd-query.py FILE.winmd --guid 101704ea-a7f9-46d2-ab94-016865afdb25
    winmd-query.py FILE.winmd --name IAnalyticsInfoStatics2
    winmd-query.py FILE.winmd --list Windows.System.Profile
"""
import argparse, struct, sys, uuid

# (name, [(field, kind)]) in table order. kind: int, str, blob, guid,
# or a tuple ('idx', target_tables) for a simple or coded index.
TABLES = {
0x00: ('Module',        [('Generation','u2'),('Name','str'),('Mvid','guid'),('EncId','guid'),('EncBaseId','guid')]),
0x01: ('TypeRef',       [('ResolutionScope',('coded','ResolutionScope')),('Name','str'),('Namespace','str')]),
0x02: ('TypeDef',       [('Flags','u4'),('Name','str'),('Namespace','str'),('Extends',('coded','TypeDefOrRef')),('FieldList',('simple',0x04)),('MethodList',('simple',0x06))]),
0x03: ('FieldPtr',      [('Field',('simple',0x04))]),
0x04: ('Field',         [('Flags','u2'),('Name','str'),('Signature','blob')]),
0x05: ('MethodPtr',     [('Method',('simple',0x06))]),
0x06: ('MethodDef',     [('RVA','u4'),('ImplFlags','u2'),('Flags','u2'),('Name','str'),('Signature','blob'),('ParamList',('simple',0x08))]),
0x07: ('ParamPtr',      [('Param',('simple',0x08))]),
0x08: ('Param',         [('Flags','u2'),('Sequence','u2'),('Name','str')]),
0x09: ('InterfaceImpl', [('Class',('simple',0x02)),('Interface',('coded','TypeDefOrRef'))]),
0x0A: ('MemberRef',     [('Class',('coded','MemberRefParent')),('Name','str'),('Signature','blob')]),
0x0B: ('Constant',      [('Type','u2'),('Parent',('coded','HasConstant')),('Value','blob')]),
0x0C: ('CustomAttribute',[('Parent',('coded','HasCustomAttribute')),('Type',('coded','CustomAttributeType')),('Value','blob')]),
0x0D: ('FieldMarshal',  [('Parent',('coded','HasFieldMarshal')),('NativeType','blob')]),
0x0E: ('DeclSecurity',  [('Action','u2'),('Parent',('coded','HasDeclSecurity')),('PermissionSet','blob')]),
0x0F: ('ClassLayout',   [('PackingSize','u2'),('ClassSize','u4'),('Parent',('simple',0x02))]),
0x10: ('FieldLayout',   [('Offset','u4'),('Field',('simple',0x04))]),
0x11: ('StandAloneSig', [('Signature','blob')]),
0x12: ('EventMap',      [('Parent',('simple',0x02)),('EventList',('simple',0x14))]),
0x13: ('EventPtr',      [('Event',('simple',0x14))]),
0x14: ('Event',         [('EventFlags','u2'),('Name','str'),('EventType',('coded','TypeDefOrRef'))]),
0x15: ('PropertyMap',   [('Parent',('simple',0x02)),('PropertyList',('simple',0x17))]),
0x16: ('PropertyPtr',   [('Property',('simple',0x17))]),
0x17: ('Property',      [('Flags','u2'),('Name','str'),('Type','blob')]),
0x18: ('MethodSemantics',[('Semantics','u2'),('Method',('simple',0x06)),('Association',('coded','HasSemantics'))]),
0x19: ('MethodImpl',    [('Class',('simple',0x02)),('MethodBody',('coded','MethodDefOrRef')),('MethodDeclaration',('coded','MethodDefOrRef'))]),
0x1A: ('ModuleRef',     [('Name','str')]),
0x1B: ('TypeSpec',      [('Signature','blob')]),
0x1C: ('ImplMap',       [('MappingFlags','u2'),('MemberForwarded',('coded','MemberForwarded')),('ImportName','str'),('ImportScope',('simple',0x1A))]),
0x1D: ('FieldRVA',      [('RVA','u4'),('Field',('simple',0x04))]),
0x20: ('Assembly',      [('HashAlgId','u4'),('Major','u2'),('Minor','u2'),('Build','u2'),('Rev','u2'),('Flags','u4'),('PublicKey','blob'),('Name','str'),('Culture','str')]),
0x21: ('AssemblyProcessor',[('Processor','u4')]),
0x22: ('AssemblyOS',    [('OSPlatformID','u4'),('OSMajor','u4'),('OSMinor','u4')]),
0x23: ('AssemblyRef',   [('Major','u2'),('Minor','u2'),('Build','u2'),('Rev','u2'),('Flags','u4'),('PublicKeyOrToken','blob'),('Name','str'),('Culture','str'),('HashValue','blob')]),
0x24: ('AssemblyRefProcessor',[('Processor','u4'),('AssemblyRef',('simple',0x23))]),
0x25: ('AssemblyRefOS', [('OSPlatformId','u4'),('OSMajor','u4'),('OSMinor','u4'),('AssemblyRef',('simple',0x23))]),
0x26: ('File',          [('Flags','u4'),('Name','str'),('HashValue','blob')]),
0x27: ('ExportedType',  [('Flags','u4'),('TypeDefId','u4'),('TypeName','str'),('TypeNamespace','str'),('Implementation',('coded','Implementation'))]),
0x28: ('ManifestResource',[('Offset','u4'),('Flags','u4'),('Name','str'),('Implementation',('coded','Implementation'))]),
0x29: ('NestedClass',   [('NestedClass',('simple',0x02)),('EnclosingClass',('simple',0x02))]),
0x2A: ('GenericParam',  [('Number','u2'),('Flags','u2'),('Owner',('coded','TypeOrMethodDef')),('Name','str')]),
0x2B: ('MethodSpec',    [('Method',('coded','MethodDefOrRef')),('Instantiation','blob')]),
0x2C: ('GenericParamConstraint',[('Owner',('simple',0x2A)),('Constraint',('coded','TypeDefOrRef'))]),
}
CODED = {
 'TypeDefOrRef':        ([0x02,0x01,0x1B], 2),
 'HasConstant':         ([0x04,0x08,0x17], 2),
 'HasCustomAttribute':  ([0x06,0x04,0x01,0x02,0x08,0x09,0x0A,0x00,0x0B,0x0C,0x0D,0x0E,0x0F,0x10,0x17,0x14,0x11,0x1A,0x1B,0x20,0x23,0x26,0x27,0x28], 5),
 'CustomAttributeType': ([None,None,0x06,0x0A,None], 3),
 'ResolutionScope':     ([0x00,0x1A,0x23,0x01], 2),
 'MemberRefParent':     ([0x02,0x01,0x1A,0x06,0x1B], 3),
 'HasFieldMarshal':     ([0x04,0x08], 1),
 'HasDeclSecurity':     ([0x02,0x06,0x20], 2),
 'HasSemantics':        ([0x14,0x17], 1),
 'MethodDefOrRef':      ([0x06,0x0A], 1),
 'MemberForwarded':     ([0x04,0x06], 1),
 'Implementation':      ([0x26,0x23,0x27], 2),
 'TypeOrMethodDef':     ([0x02,0x06], 1),
}

class WinMD:
    def __init__(self, path):
        d = self.d = open(path,'rb').read()
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        nsec = struct.unpack_from('<H', d, pe+6)[0]
        optsz = struct.unpack_from('<H', d, pe+20)[0]
        opt = pe+24
        magic = struct.unpack_from('<H', d, opt)[0]
        # CLI header is data directory 14
        dd = opt + (112 if magic == 0x20b else 96)
        cli_rva = struct.unpack_from('<I', d, dd + 14*8)[0]
        self.sections = []
        sh = opt + optsz
        for i in range(nsec):
            o = sh + i*40
            # section header: +8 VirtualSize, +12 VirtualAddress,
            # +16 SizeOfRawData, +20 PointerToRawData
            vs, va, rs, pr = struct.unpack_from('<IIII', d, o+8)
            self.sections.append((va, max(vs, rs), pr))
        cli = self.rva2off(cli_rva)
        meta_rva, meta_sz = struct.unpack_from('<II', d, cli+8)
        m = self.meta = self.rva2off(meta_rva)
        assert d[m:m+4] == b'BSJB', 'not a CLI metadata file'
        # metadata root: +12 version-string length, +16 the string (already
        # padded to 4), then Flags(2) and Streams(2).
        vlen = struct.unpack_from('<I', d, m+12)[0]
        p = m + 16 + vlen
        nstreams = struct.unpack_from('<H', d, p+2)[0]
        p += 4
        self.streams = {}
        for _ in range(nstreams):
            off, size = struct.unpack_from('<II', d, p); p += 8
            e = d.index(b'\0', p)
            name = d[p:e].decode('ascii')
            p = e + 1
            p = m + ((p - m + 3) & ~3)     # name padded to 4 within the root
            self.streams[name] = (m+off, size)
        self._parse_tables()

    def rva2off(self, rva):
        for va, sz, pr in self.sections:
            if va <= rva < va+sz: return pr + (rva-va)
        raise ValueError('bad rva %#x' % rva)

    def _str(self, i):
        o = self.streams['#Strings'][0] + i
        e = self.d.index(b'\0', o)
        return self.d[o:e].decode('utf-8', 'replace')

    def _blob(self, i):
        o = self.streams['#Blob'][0] + i
        b0 = self.d[o]
        if   b0 & 0x80 == 0:    n, o = b0 & 0x7f, o+1
        elif b0 & 0xC0 == 0x80: n, o = ((b0 & 0x3f)<<8) | self.d[o+1], o+2
        else:                   n, o = ((b0 & 0x1f)<<24)|(self.d[o+1]<<16)|(self.d[o+2]<<8)|self.d[o+3], o+4
        return self.d[o:o+n]

    def _parse_tables(self):
        d = self.d
        t0 = self.streams['#~'][0]
        heap = d[t0+6]
        self.wide_str  = 4 if heap & 1 else 2
        self.wide_guid = 4 if heap & 2 else 2
        self.wide_blob = 4 if heap & 4 else 2
        valid = struct.unpack_from('<Q', d, t0+8)[0]
        p = t0 + 24
        self.rows = {}
        for i in range(64):
            if valid >> i & 1:
                self.rows[i] = struct.unpack_from('<I', d, p)[0]; p += 4
        def idx_size(kind):
            if kind == 'str':  return self.wide_str
            if kind == 'blob': return self.wide_blob
            if kind == 'guid': return self.wide_guid
            if kind == 'u2':   return 2
            if kind == 'u4':   return 4
            k, arg = kind
            if k == 'simple':
                return 4 if self.rows.get(arg,0) >= (1<<16) else 2
            tabs, bits = CODED[arg]
            mx = max((self.rows.get(t,0) for t in tabs if t is not None), default=0)
            return 4 if mx >= (1 << (16-bits)) else 2
        self.tab = {}
        for tid in sorted(self.rows):
            if tid not in TABLES:
                raise SystemExit('table %#x not modelled; file uses features this tool lacks' % tid)
            name, cols = TABLES[tid]
            sizes = [idx_size(k) for _, k in cols]
            rowsz = sum(sizes)
            n = self.rows[tid]
            recs = []
            for r in range(n):
                base = p + r*rowsz
                vals, off = {}, 0
                for (cn, kind), sz in zip(cols, sizes):
                    raw = int.from_bytes(d[base+off:base+off+sz], 'little'); off += sz
                    vals[cn] = raw
                recs.append(vals)
            self.tab[tid] = recs
            p += rowsz * n

    def typedefs(self):
        for i, t in enumerate(self.tab.get(0x02, [])):
            yield i, self._str(t['Namespace']), self._str(t['Name']), t

    def methods_of(self, ti):
        tds, mds = self.tab[0x02], self.tab.get(0x06, [])
        start = tds[ti]['MethodList'] - 1
        end = tds[ti+1]['MethodList'] - 1 if ti+1 < len(tds) else len(mds)
        out = []
        for mi in range(start, end):
            m = mds[mi]
            params = []
            ps = self.tab.get(0x08, [])
            pstart = m['ParamList'] - 1
            pend = mds[mi+1]['ParamList'] - 1 if mi+1 < len(mds) else len(ps)
            for pi in range(pstart, pend):
                params.append(self._str(ps[pi]['Name']))
            out.append((self._str(m['Name']), params))
        return out

    def guid_of_typedef(self, ti):
        """The GuidAttribute blob on a TypeDef: prolog 0x0001, the 16 bytes of
        the GUID, and no named arguments -- exactly 20 bytes.  Other attributes
        on the same type start with the same prolog: ExclusiveToAttribute
        carries a type name, and taking its first 16 bytes gives a "GUID" made
        of the letters of Windows.Web.Http.... """
        tabs, bits = CODED['HasCustomAttribute']
        tag = tabs.index(0x02)
        want = ((ti+1) << bits) | tag
        for ca in self.tab.get(0x0C, []):
            if ca['Parent'] != want: continue
            b = self._blob(ca['Value'])
            if len(b) == 20 and b[0] == 1 and b[1] == 0 and b[18:20] == b'\0\0':
                return uuid.UUID(bytes_le=bytes(b[2:18]))
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('winmd')
    ap.add_argument('--guid'); ap.add_argument('--name'); ap.add_argument('--list')
    a = ap.parse_args()
    w = WinMD(a.winmd)
    want = uuid.UUID(a.guid) if a.guid else None
    hits = 0
    for ti, ns, name, _ in w.typedefs():
        if a.list and not ns.startswith(a.list): continue
        if a.name and name != a.name: continue
        g = w.guid_of_typedef(ti)
        if want and g != want: continue
        if not (a.list or a.name or want): continue
        hits += 1
        print("%s.%s" % (ns, name))
        if g: print("    IID  %s" % g)
        if not a.list:
            for i, (mn, ps) in enumerate(w.methods_of(ti)):
                print("    [%2d] %s(%s)" % (i, mn, ', '.join(ps)))
        print()
    if not hits: print("no match", file=sys.stderr); sys.exit(1)

if __name__ == '__main__':
    main()
