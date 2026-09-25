#!/usr/bin/env python3
"""Write widl IDL for Windows Runtime types, read out of a .winmd.

A WinRT class Wine implements has to present exactly the vtables Windows
does: C++/WinRT calls a method of a typed collection through the slot the
metadata gives it, without a QueryInterface.  An interface declared by hand
with IInspectable in place of the real types, or with a method left out,
lines up until the first call that lands in the wrong slot.  This takes the
declarations from the metadata Windows ships instead:

    winmd-idl.py Windows.Web.winmd Windows.Web.Http.Headers
    winmd-idl.py Windows.Web.winmd Windows.Web.Http --type HttpClient --type IHttpClient

For every type of the namespace (or only those named) it prints the
interfaces with their IIDs, contracts, exclusiveto and requires, methods in
vtable order with [propget]/[propput]/[eventadd]/[eventremove], overload
names and WinRT arrays; runtime classes with activatable/static/composable,
default interface, threading and marshaling behaviour; enums (with [flags]),
structs and delegates.  Parameterized interfaces it uses are listed for a
declare { } block, and the namespaces it refers to for imports.

It is a starting point to review, not a finished header: Wine's IDL groups
namespaces, forward-declares, and keeps some names of its own.
"""
import argparse, importlib.util, os, re, struct, sys, uuid

_spec = importlib.util.spec_from_file_location(
    'winmd_query', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'winmd-query.py'))
_wq = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_wq)
WinMD, CODED = _wq.WinMD, _wq.CODED

PRIMITIVES = {
    0x01: 'void', 0x02: 'boolean', 0x03: 'WCHAR', 0x04: 'INT8', 0x05: 'BYTE',
    0x06: 'INT16', 0x07: 'UINT16', 0x08: 'INT32', 0x09: 'UINT32', 0x0a: 'INT64',
    0x0b: 'UINT64', 0x0c: 'FLOAT', 0x0d: 'DOUBLE', 0x0e: 'HSTRING', 0x1c: 'IInspectable *',
}
SPECIAL = {
    ('System', 'Guid'): 'GUID',
    ('Windows.Foundation', 'HResult'): 'HRESULT',
    ('Windows.Foundation', 'EventRegistrationToken'): 'EventRegistrationToken',
    ('System', 'Object'): 'IInspectable *',
}


def decode(tabs, bits, coded):
    return tabs[coded & ((1 << bits) - 1)], (coded >> bits) - 1


class Reader:
    def __init__(self, blob):
        self.b, self.p = blob, 0

    def u8(self):
        v = self.b[self.p]; self.p += 1; return v

    def compressed(self):
        b0 = self.b[self.p]
        if b0 & 0x80 == 0:
            self.p += 1; return b0
        if b0 & 0xC0 == 0x80:
            v = ((b0 & 0x3f) << 8) | self.b[self.p + 1]; self.p += 2; return v
        v = ((b0 & 0x1f) << 24) | (self.b[self.p + 1] << 16) | (self.b[self.p + 2] << 8) | self.b[self.p + 3]
        self.p += 4; return v

    def u16(self):
        v = struct.unpack_from('<H', self.b, self.p)[0]; self.p += 2; return v

    def u32(self):
        v = struct.unpack_from('<I', self.b, self.p)[0]; self.p += 4; return v

    def i32(self):
        v = struct.unpack_from('<i', self.b, self.p)[0]; self.p += 4; return v

    def serstring(self):
        if self.b[self.p] == 0xff:
            self.p += 1; return None
        n = self.compressed()
        s = self.b[self.p:self.p + n].decode('utf-8'); self.p += n; return s


class Meta:
    def __init__(self, path):
        self.w = w = WinMD(path)
        self.typedefs = [(ns, name, t) for _, ns, name, t in w.typedefs()]
        self.by_name = {(ns, name): i for i, (ns, name, _) in enumerate(self.typedefs)}
        self.typerefs = [(w._str(t['Namespace']), w._str(t['Name'])) for t in w.tab.get(0x01, [])]
        self.memberrefs = w.tab.get(0x0A, [])
        self.methods = w.tab.get(0x06, [])
        self.params = w.tab.get(0x08, [])
        self.fields = w.tab.get(0x04, [])
        self.constants = w.tab.get(0x0B, [])
        self.typespecs = w.tab.get(0x1B, [])
        self.generic_params = w.tab.get(0x2A, [])
        self._attrs = {}
        tabs, bits = CODED['HasCustomAttribute']
        for ca in w.tab.get(0x0C, []):
            table, idx = decode(tabs, bits, ca['Parent'])
            self._attrs.setdefault((table, idx), []).append(ca)
        self.semantics = {}
        tabs, bits = CODED['HasSemantics']
        for ms in w.tab.get(0x18, []):
            self.semantics[ms['Method'] - 1] = ms['Semantics']
        self.impls = {}
        for ii in w.tab.get(0x09, []):
            self.impls.setdefault(ii['Class'] - 1, []).append(ii)
        self.used_generics = set()
        self.used_namespaces = set()

    # --- names --------------------------------------------------------------
    def typedeforref_name(self, coded):
        tabs, bits = CODED['TypeDefOrRef']
        table, idx = decode(tabs, bits, coded)
        return self._name(table, idx)

    def _name(self, table, idx):
        if table == 0x02:
            ns, name, _ = self.typedefs[idx]; return ns, name
        if table == 0x01:
            return self.typerefs[idx]
        raise ValueError('TypeSpec where a name was wanted')

    def type_kind(self, ns, name):
        """interface, class, delegate, enum, struct -- when defined in this file."""
        i = self.by_name.get((ns, name))
        if i is None: return None
        t = self.typedefs[i][2]
        flags = t['Flags']
        if flags & 0x20: return 'interface'
        base = self.typedeforref_name(t['Extends']) if t['Extends'] else None
        if base == ('System', 'Enum'): return 'enum'
        if base == ('System', 'ValueType'): return 'struct'
        if base == ('System', 'MulticastDelegate'): return 'delegate'
        return 'class'

    @staticmethod
    def plain(name):
        return name.split('`')[0]

    def fq(self, ns, name):
        self.used_namespaces.add(ns)
        return '%s.%s' % (ns, self.plain(name))

    # --- signatures ---------------------------------------------------------
    def read_type(self, r, generic_args=None, in_generic=False):
        """Returns (idl type text, is_reference)."""
        et = r.u8()
        while et in (0x1f, 0x20):          # custom modifiers
            r.compressed(); et = r.u8()
        if et == 0x10:                    # BYREF
            t, ref = self.read_type(r, generic_args, in_generic)
            return ('BYREF', t, ref), False
        if et == 0x1d:                    # SZARRAY
            t, ref = self.read_type(r, generic_args, in_generic)
            return ('ARRAY', t, ref), False
        if et in PRIMITIVES:
            t = PRIMITIVES[et]
            return t, t.endswith('*') or t == 'HSTRING'
        if et in (0x11, 0x12):            # VALUETYPE / CLASS
            coded = r.compressed()
            tabs = [0x02, 0x01, 0x1B]
            table, idx = tabs[coded & 3], (coded >> 2) - 1
            ns, name = self._name(table, idx)
            if (ns, name) in SPECIAL:
                t = SPECIAL[(ns, name)]
                return t, t.endswith('*')
            if et == 0x11:
                return self.fq(ns, name), False
            return self.fq(ns, name) + ' *', True
        if et == 0x13:                    # VAR
            n = r.compressed()
            return (generic_args[n] if generic_args else 'T%d' % n), True
        if et == 0x15:                    # GENERICINST
            kind = r.u8()
            coded = r.compressed()
            tabs = [0x02, 0x01, 0x1B]
            table, idx = tabs[coded & 3], (coded >> 2) - 1
            ns, name = self._name(table, idx)
            n = r.compressed()
            args = []
            for _ in range(n):
                t, ref = self.read_type(r, generic_args, True)
                args.append(t if not ref or t.endswith('*') or t == 'HSTRING' else t)
            inst = '%s<%s>' % (self.fq(ns, name), ', '.join(args))
            self.used_generics.add((self.fq(ns, name), tuple(args)))
            return inst + ' *', True
        raise ValueError('element type %#x not handled' % et)

    def method_sig(self, blob, generic_args=None):
        r = Reader(blob)
        conv = r.u8()
        if conv & 0x10: r.compressed()    # generic method arity
        n = r.compressed()
        ret, _ = self.read_type(r, generic_args)
        ps = [self.read_type(r, generic_args)[0] for _ in range(n)]
        return ret, ps

    # --- attributes ---------------------------------------------------------
    def attributes(self, table, idx):
        out = []
        for ca in self._attrs.get((table, idx), []):
            tabs, bits = CODED['CustomAttributeType']
            ttab, tidx = decode(tabs, bits, ca['Type'])
            if ttab == 0x0A:
                mr = self.memberrefs[tidx]
                ptabs, pbits = CODED['MemberRefParent']
                ptab, pidx = decode(ptabs, pbits, mr['Class'])
                aname = self._name(ptab, pidx)
                sig = self.w._blob(mr['Signature'])
            else:
                owner = self.owner_of_method(tidx)
                aname = self.typedefs[owner][:2]
                sig = self.w._blob(self.methods[tidx]['Signature'])
            args = self.attr_args(sig, self.w._blob(ca['Value']))
            out.append((aname[1], args))
        return out

    def owner_of_method(self, mi):
        tds = self.typedefs
        for i in range(len(tds)):
            start = tds[i][2]['MethodList'] - 1
            end = tds[i + 1][2]['MethodList'] - 1 if i + 1 < len(tds) else len(self.methods)
            if start <= mi < end: return i
        return None

    def attr_args(self, ctor_sig, value):
        r = Reader(ctor_sig)
        r.u8()
        n = r.compressed()
        r.u8()                            # void return
        kinds = []
        for _ in range(n):
            et = r.u8()
            if et in (0x11, 0x12):
                coded = r.compressed()
                tabs = [0x02, 0x01, 0x1B]
                table, idx = tabs[coded & 3], (coded >> 2) - 1
                ns, name = self._name(table, idx)
                kinds.append('type' if (ns, name) == ('System', 'Type') else 'enum')
            else:
                kinds.append(et)
        v = Reader(value)
        if v.u16() != 1: return []
        args = []
        for k in kinds:
            if k == 'type' or k == 0x0e: args.append(v.serstring())
            elif k == 'enum' or k == 0x08: args.append(v.i32())
            elif k == 0x09: args.append(v.u32())
            elif k == 0x02: args.append(bool(v.u8()))
            else: args.append('?')
        return args

    @staticmethod
    def version(v):
        return '%d.%d' % (v >> 16, v & 0xffff)

    def contract(self, attrs):
        for name, args in attrs:
            if name == 'ContractVersionAttribute' and len(args) == 2:
                return '%s, %s' % (args[0], self.version(args[1]))
        for name, args in attrs:
            if name == 'VersionAttribute' and args:
                return None
        return None

    def guid(self, ti):
        return self.w.guid_of_typedef(ti)

    # --- members ------------------------------------------------------------
    def method_range(self, ti):
        tds = self.typedefs
        start = tds[ti][2]['MethodList'] - 1
        end = tds[ti + 1][2]['MethodList'] - 1 if ti + 1 < len(tds) else len(self.methods)
        return range(start, end)

    def param_names(self, mi):
        m = self.methods[mi]
        start = m['ParamList'] - 1
        end = self.methods[mi + 1]['ParamList'] - 1 if mi + 1 < len(self.methods) else len(self.params)
        names, ret = {}, None
        for pi in range(start, end):
            p = self.params[pi]
            nm = self.w._str(p['Name'])
            if p['Sequence'] == 0: ret = nm
            else: names[p['Sequence']] = (nm, p['Flags'])
        return names, ret


def decl(t, name, extra=0):
    """'Windows.Foundation.Uri *' and 'uri' -> 'Windows.Foundation.Uri *uri', with extra levels."""
    base, stars = t.rstrip(), ''
    while base.endswith('*'):
        base, stars = base[:-1].rstrip(), stars + '*'
    stars += '*' * extra
    return '%s %s%s' % (base, stars, name)


def fmt_param(direction, t, name, retval=False):
    """Returns a list of IDL parameter strings for one WinRT parameter."""
    if isinstance(t, tuple) and t[0] == 'BYREF':
        inner = t[1]
        if isinstance(inner, tuple) and inner[0] == 'ARRAY':      # ReceiveArray
            elem = inner[1]
            return ['[out] UINT32 *%s_size' % name,
                    '[out, size_is(, *%s_size)%s] %s' % (name, ', retval' if retval else '', decl(elem, name, 2))]
        return ['[out%s] %s' % (', retval' if retval else '', decl(inner, name, 1))]
    if isinstance(t, tuple) and t[0] == 'ARRAY':
        elem = t[1]
        d = 'in' if direction == 'in' else 'out'
        return ['[in] UINT32 %s_size' % name,
                '[%s, size_is(%s_size)] %s' % (d, name, decl(elem, name, 1))]
    if retval:
        return ['[out, retval] %s' % decl(t, name, 1)]
    return ['[%s] %s' % (direction, decl(t, name))]


def spaced(t):
    return t if t.endswith('*') else t + ' '


def emit_interface(m, ti, out):
    ns, name, t = m.typedefs[ti]
    attrs = m.attributes(0x02, ti)
    lines = []
    c = m.contract(attrs)
    if c: lines.append('contract(%s)' % c)
    for an, args in attrs:
        if an == 'ExclusiveToAttribute': lines.append('exclusiveto(%s)' % args[0])
    g = m.guid(ti)
    if g: lines.append('uuid(%s)' % g)
    generic = [gp for gp in m.generic_params if gp['Owner'] == (((ti + 1) << 1) | 0)]
    gargs = [m.w._str(gp['Name']) for gp in sorted(generic, key=lambda g: g['Number'])]
    out.append('    [\n        %s\n    ]' % ',\n        '.join(lines))
    head = '    interface %s%s : IInspectable' % (m.plain(name), '<%s>' % ', '.join(gargs) if gargs else '')
    reqs = [m.typedeforref_name_or_spec(ii['Interface']) for ii in m.impls.get(ti, [])]
    out.append(head)
    if reqs:
        out.append('        requires %s' % ',\n                 '.join(reqs))
    out.append('    {')
    for mi in m.method_range(ti):
        md = m.methods[mi]
        mname = m.w._str(md['Name'])
        ret, ptypes = m.method_sig(m.w._blob(md['Signature']), gargs or None)
        names, retname = m.param_names(mi)
        sem = m.semantics.get(mi, 0)
        mattrs = m.attributes(0x06, mi)
        tags = []
        if sem & 0x2: tags.append('propget'); mname = mname[4:]
        elif sem & 0x1: tags.append('propput'); mname = mname[4:]
        elif sem & 0x8: tags.append('eventadd'); mname = mname[4:]
        elif sem & 0x10: tags.append('eventremove'); mname = mname[7:]
        for an, args in mattrs:
            # the MethodDef carries the projected name, the attribute the ABI one
            if an == 'OverloadAttribute':
                tags.append('overload("%s")' % mname)
                mname = args[0]
            if an == 'DefaultOverloadAttribute': tags.append('default_overload')
            if an == 'DeprecatedAttribute': tags.append('deprecated("%s", %s, %s)' % (args[0], 'remove' if args[1] else 'deprecate', m.version(args[2])) if len(args) >= 3 else 'deprecated')
        params = []
        for i, pt in enumerate(ptypes):
            pname, flags = names.get(i + 1, ('arg%d' % i, 1))
            direction = 'out' if flags & 2 else 'in'
            params += fmt_param(direction, pt, pname)
        if ret != 'void':
            params += fmt_param('out', ('BYREF', ret, False) if not isinstance(ret, tuple) else ('BYREF', ret, False),
                                retname or ('token' if sem & 0x8 else 'value'), retval=True)
        prefix = '[%s] ' % ', '.join(tags) if tags else ''
        if not params:
            out.append('        %sHRESULT %s();' % (prefix, mname))
        elif sum(len(p) for p in params) < 70:
            out.append('        %sHRESULT %s(%s);' % (prefix, mname, ', '.join(params)))
        else:
            out.append('        %sHRESULT %s(\n            %s);' % (prefix, mname, ',\n            '.join(params)))
    out.append('    }')


def typedeforref_name_or_spec(self, coded):
    tabs, bits = CODED['TypeDefOrRef']
    table, idx = decode(tabs, bits, coded)
    if table == 0x1B:
        t, _ = self.read_type(Reader(self.w._blob(self.typespecs[idx]['Signature'])))
        return t.rstrip(' *')
    ns, name = self._name(table, idx)
    return self.fq(ns, name)


Meta.typedeforref_name_or_spec = typedeforref_name_or_spec


def emit_class(m, ti, out):
    ns, name, t = m.typedefs[ti]
    attrs = m.attributes(0x02, ti)
    lines = []
    for an, args in attrs:
        if an == 'ActivatableAttribute':
            if len(args) == 2 and isinstance(args[0], int):
                lines.append('activatable(%s, %s)' % (args[1], m.version(args[0])))
            elif len(args) == 3:
                lines.append('activatable(%s, %s, %s)' % (args[0], args[2], m.version(args[1])))
            else:
                lines.append('activatable(%s)' % ', '.join(map(str, args)))
    c = m.contract(attrs)
    if c: lines.append('contract(%s)' % c)
    for an, args in attrs:
        if an == 'ComposableAttribute' and len(args) >= 4:
            lines.append('composable(%s, %s, %s, %s)' % (args[0], {1: 'protected', 2: 'public'}.get(args[1], args[1]),
                                                          args[3], m.version(args[2])))
    for an, args in attrs:
        if an == 'MarshalingBehaviorAttribute':
            lines.append('marshaling_behavior(%s)' % {1: 'none', 2: 'agile', 3: 'standard'}.get(args[0], args[0]))
    for an, args in attrs:
        if an == 'StaticAttribute' and len(args) == 3:
            lines.append('static(%s, %s, %s)' % (args[0], args[2], m.version(args[1])))
    for an, args in attrs:
        if an == 'ThreadingAttribute':
            lines.append('threading(%s)' % {1: 'sta', 2: 'mta', 3: 'both'}.get(args[0], args[0]))
    out.append('    [\n        %s\n    ]' % ',\n        '.join(sorted(lines, key=lambda l: l.split('(')[0])))
    out.append('    runtimeclass %s' % name)
    out.append('    {')
    for ii in m.impls.get(ti, []):
        iattrs = m.attributes(0x09, m.w.tab[0x09].index(ii))
        default = any(an == 'DefaultAttribute' for an, _ in iattrs)
        c = m.contract(iattrs)
        pre = []
        if c: pre.append('contract(%s)' % c)
        if default: pre.append('default')
        out.append('        %sinterface %s;' % ('[%s] ' % ', '.join(pre) if pre else '', m.typedeforref_name_or_spec(ii['Interface'])))
    out.append('    }')


def emit_enum(m, ti, out):
    ns, name, t = m.typedefs[ti]
    attrs = m.attributes(0x02, ti)
    lines = []
    c = m.contract(attrs)
    if c: lines.append('contract(%s)' % c)
    if any(an == 'FlagsAttribute' for an, _ in attrs): lines.append('flags')
    out.append('    [\n        %s\n    ]' % ',\n        '.join(lines))
    out.append('    enum %s' % name)
    out.append('    {')
    tds = m.typedefs
    fstart = t['FieldList'] - 1
    fend = tds[ti + 1][2]['FieldList'] - 1 if ti + 1 < len(tds) else len(m.fields)
    consts = {}
    tabs, bits = CODED['HasConstant']
    for c in m.constants:
        table, idx = decode(tabs, bits, c['Parent'])
        if table == 0x04: consts[idx] = m.w._blob(c['Value'])
    items = []
    for fi in range(fstart, fend):
        f = m.fields[fi]
        fname = m.w._str(f['Name'])
        if fname == 'value__': continue
        v = consts.get(fi)
        val = struct.unpack('<i', v)[0] if v and len(v) == 4 else (struct.unpack('<I', v)[0] if v else 0)
        fattrs = m.attributes(0x04, fi)
        fc = m.contract(fattrs)
        items.append('        %s%s = %d' % ('[contract(%s)] ' % fc if fc and fc != c else '', fname, val))
    out.append(',\n'.join(items))
    out.append('    };')


def emit_struct(m, ti, out):
    ns, name, t = m.typedefs[ti]
    attrs = m.attributes(0x02, ti)
    c = m.contract(attrs)
    out.append('    [\n        contract(%s)\n    ]' % c if c else '    [\n    ]')
    out.append('    struct %s' % name)
    out.append('    {')
    tds = m.typedefs
    fstart = t['FieldList'] - 1
    fend = tds[ti + 1][2]['FieldList'] - 1 if ti + 1 < len(tds) else len(m.fields)
    for fi in range(fstart, fend):
        f = m.fields[fi]
        r = Reader(m.w._blob(f['Signature']))
        r.u8()                            # FIELD
        ft, _ = m.read_type(r)
        out.append('        %s;' % decl(ft, m.w._str(f['Name'])))
    out.append('    };')


def emit_delegate(m, ti, out):
    ns, name, t = m.typedefs[ti]
    attrs = m.attributes(0x02, ti)
    lines = []
    c = m.contract(attrs)
    if c: lines.append('contract(%s)' % c)
    g = m.guid(ti)
    if g: lines.append('uuid(%s)' % g)
    for mi in m.method_range(ti):
        if m.w._str(m.methods[mi]['Name']) != 'Invoke': continue
        ret, ptypes = m.method_sig(m.w._blob(m.methods[mi]['Signature']))
        names, _ = m.param_names(mi)
        params = []
        for i, pt in enumerate(ptypes):
            pname, flags = names.get(i + 1, ('arg%d' % i, 1))
            params += fmt_param('out' if flags & 2 else 'in', pt, pname)
        out.append('    [\n        %s\n    ]' % ',\n        '.join(lines))
        out.append('    delegate HRESULT %s(%s);' % (name, ', '.join(params)))


FC = 'Windows.Foundation.Collections.'
F = 'Windows.Foundation.'


def closure(generics):
    """The parameterized interfaces an instantiation brings with it: what it
    requires, and the delegates its methods take."""
    todo, done = list(generics), set()
    while todo:
        g = todo.pop()
        if g in done: continue
        done.add(g)
        name, args = g
        more = []
        if name in (FC + 'IVector', FC + 'IVectorView', FC + 'IIterable'):
            more += [(FC + 'IIterable', args), (FC + 'IIterator', args)]
            if name == FC + 'IVector': more.append((FC + 'IVectorView', args))
        elif name in (FC + 'IMap', FC + 'IMapView'):
            kv = '%sIKeyValuePair<%s> *' % (FC, ', '.join(args))
            more += [(FC + 'IKeyValuePair', args), (FC + 'IIterable', (kv,)), (FC + 'IIterator', (kv,))]
            if name == FC + 'IMap': more.append((FC + 'IMapView', args))
        elif name == FC + 'IObservableVector':
            more += [(FC + 'IVector', args), (FC + 'VectorChangedEventHandler', args)]
        elif name == FC + 'IObservableMap':
            more += [(FC + 'IMap', args), (FC + 'MapChangedEventHandler', args)]
        elif name == F + 'IAsyncOperation':
            more.append((F + 'AsyncOperationCompletedHandler', args))
        elif name == F + 'IAsyncOperationWithProgress':
            more += [(F + 'AsyncOperationProgressHandler', args), (F + 'AsyncOperationWithProgressCompletedHandler', args)]
        elif name == F + 'IAsyncActionWithProgress':
            more += [(F + 'AsyncActionProgressHandler', args), (F + 'AsyncActionWithProgressCompletedHandler', args)]
        todo += more
    return done


def generic_text(g):
    return '%s<%s>' % (g[0], ', '.join(g[1]))


def declared_in_imports(imports, include_dirs):
    """Instantiations the imported IDL files already declare, following their
    own imports: widl takes a second declaration of one as a different type."""
    import re
    seen, found = set(), set()
    todo = list(imports)
    while todo:
        f = todo.pop()
        if f in seen: continue
        seen.add(f)
        for d in include_dirs:
            path = os.path.join(d, f)
            if os.path.exists(path): break
        else:
            continue
        text = open(path).read()
        todo += re.findall(r'^\s*import\s+"([^"]+)"', text, re.M)
        for block in re.findall(r'declare\s*\{(.*?)\}', text, re.S):
            for decl in re.findall(r'interface\s+([^;]+);', block):
                found.add(re.sub(r'\s+', ' ', decl).replace('< ', '<').replace(' >', '>').strip())
    return found


def declare_order(generics):
    """Inner instantiations before the ones built on them: widl wants an
    argument declared before it is used.  IReference<> first of all: a struct
    with one in a field (HttpProgress) is part of the signature of whatever
    takes the struct as an argument, and widl works that out as soon as it
    meets the instantiation."""
    return sorted((generic_text(g) for g in generics),
                  key=lambda t: (not t.startswith(F + 'IReference<'), t.count('<'), t))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('winmd')
    ap.add_argument('namespaces', nargs='+')
    ap.add_argument('--type', action='append', help='only this type (repeatable)')
    ap.add_argument('--skip', action='append', default=[], help='leave this type out (repeatable)')
    ap.add_argument('--file', action='store_true', help='a whole IDL file: forward declarations, declare {}, definitions')
    ap.add_argument('--import', dest='imports', action='append', default=[], help='an IDL to import (repeatable)')
    ap.add_argument('-I', dest='include_dirs', action='append', default=[],
                    help='where the imported IDL files are, to leave out what they declare already')
    a = ap.parse_args()
    m = Meta(a.winmd)
    per_ns = {}
    for ti, (ns, name, t) in enumerate(m.typedefs):
        if ns not in a.namespaces: continue
        if a.type and m.plain(name) not in a.type: continue
        if m.plain(name) in a.skip: continue
        kind = m.type_kind(ns, name)
        out = []
        {'interface': emit_interface, 'class': emit_class, 'enum': emit_enum,
         'struct': emit_struct, 'delegate': emit_delegate}[kind](m, ti, out)
        per_ns.setdefault(ns, {'enum': [], 'struct': [], 'delegate': [], 'interface': [], 'class': []})
        per_ns[ns][kind].append((m.plain(name), '\n'.join(out)))
    if not a.file:
        print('/* generated by scripts/winmd-idl.py from %s */' % os.path.basename(a.winmd))
        print('/* namespaces referred to: %s */' % ', '.join(sorted(m.used_namespaces)))
        if m.used_generics:
            print('    declare {')
            for gi in declare_order(closure(m.used_generics)):
                print('        interface %s;' % gi)
            print('    }')
        for ns in a.namespaces:
            for kind in ('enum', 'struct', 'delegate', 'interface', 'class'):
                for _, text in per_ns.get(ns, {}).get(kind, []):
                    print(); print(text)
        return
    print('/* Generated from %s by scripts/winmd-idl.py.  Namespaces referred to: */' % os.path.basename(a.winmd))
    print('/* %s */' % ', '.join(sorted(m.used_namespaces)))
    print()
    print('#ifdef __WIDL__')
    print('#pragma winrt ns_prefix')
    print('#endif')
    print()
    for imp in ['inspectable.idl', 'asyncinfo.idl', 'eventtoken.idl', 'windowscontracts.idl'] + a.imports:
        print('import "%s";' % imp)
    for ns in a.namespaces:
        if ns not in per_ns: continue
        print()
        print('namespace %s {' % ns)
        for name, _ in per_ns[ns]['enum']: print('    typedef enum %s %s;' % (name, name))
        for name, _ in per_ns[ns]['struct']: print('    typedef struct %s %s;' % (name, name))
        for name, _ in per_ns[ns]['delegate']: print('    delegate %s;' % name)
        for name, _ in per_ns[ns]['interface']: print('    interface %s;' % name)
        for name, _ in per_ns[ns]['class']: print('    runtimeclass %s;' % name)
        print('}')
    print()
    known = declared_in_imports(a.imports, a.include_dirs)
    print('namespace %s {' % a.namespaces[0])
    print('    declare {')
    for gi in declare_order(closure(m.used_generics)):
        # One declared in an import is not defined in this file, and widl has no
        # signature for it here: a runtime class that lists it, or a struct that
        # holds one, stops the build.  Declaring it again is harmless.
        print('        interface %s;' % gi)
    print('    }')
    print('}')
    for ns in a.namespaces:
        if ns not in per_ns: continue
        print()
        print('namespace %s {' % ns)
        first = True
        for kind in ('enum', 'struct', 'delegate', 'interface', 'class'):
            for _, text in per_ns[ns][kind]:
                if not first: print()
                first = False
                print(text)
        print('}')


if __name__ == '__main__':
    main()
