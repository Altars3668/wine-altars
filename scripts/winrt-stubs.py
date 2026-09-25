#!/usr/bin/env python3
"""Write the C skeleton of a WinRT interface implementation, out of a widl header.

An object Wine implements has to carry every method of every interface it
answers for, in vtable order, whether or not anything calls them yet.  Writing
those by hand is where slots get lost.  This reads the C vtable widl generated
for each interface named and prints, for each, the DEFINE_IINSPECTABLE line,
one stub per method (a FIXME and E_NOTIMPL, the signature exactly the
header's) and the vtable, ready to have the methods that matter filled in:

    winrt-stubs.py build/include/windows.ui.composition.h \\
        Windows.UI.Composition.ICompositor:compositor:struct compositor:ICompositor_iface

Each argument is NAMESPACE.INTERFACE:PREFIX:IMPL:BASE -- the function prefix,
the implementing struct, and the member the IInspectable methods forward to
(leave BASE out for the interface that implements IInspectable itself; its
QueryInterface/AddRef/Release/IInspectable methods are then left to write).

Type names are shortened to their last component, as the WIDL_using_*
defines of the namespaces involved allow; parameterized types keep widl's
mangled names.
"""
import argparse, re, sys


def mangle(full):
    ns, name = full.rsplit('.', 1)
    return '__x_ABI_C' + ns.replace('.', '_C') + '_C' + name


def shorten(text):
    return re.sub(r'__x_ABI_C(?:\w+?_C)*?(?:_C)?([A-Za-z0-9]+)\b', lambda m: m.group(1), text)


def methods_of(header, full):
    name = mangle(full)
    m = re.search(r'typedef struct %sVtbl \{(.*?)END_INTERFACE' % re.escape(name), header, re.S)
    if not m:
        sys.exit('no vtable for %s (%s)' % (full, name))
    body = m.group(1)
    short = full.rsplit('.', 1)[1]
    section = body.split('/*** %s methods ***/' % short, 1)
    if len(section) != 2:
        # an interface that only marks a type, like ICompositionBrush, has none
        if '/*** IInspectable methods ***/' not in body:
            sys.exit('no methods section for %s' % short)
        return []
    out = []
    for mm in re.finditer(r'(\w[\w\s\*]*?)\s*\(STDMETHODCALLTYPE \*(\w+)\)\(\s*(.*?)\);', section[1], re.S):
        ret, meth, params = mm.group(1).strip(), mm.group(2), mm.group(3)
        params = [p.strip() for p in re.sub(r'\s+', ' ', params).split(',')]
        params = params[1:]  # This
        out.append((ret, meth, [shorten(p) for p in params]))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('header')
    ap.add_argument('specs', nargs='+', help='NAMESPACE.INTERFACE:PREFIX:IMPL[:BASE]')
    a = ap.parse_args()
    header = open(a.header).read()
    for spec in a.specs:
        parts = spec.split(':')
        full, prefix, impl = parts[0], parts[1], parts[2]
        base = parts[3] if len(parts) > 3 else None
        iface = full.rsplit('.', 1)[1]
        meths = methods_of(header, full)
        print()
        if base:
            print('DEFINE_IINSPECTABLE( %s, %s, %s, %s )' % (prefix, iface, impl, base))
            print()
        for ret, meth, params in meths:
            args = ', '.join(['%s *iface' % iface] + params)
            print('static %s WINAPI %s_%s( %s )' % (ret, prefix, meth, args))
            print('{')
            print('    FIXME( "iface %p stub!\\n", iface );')
            print('    return E_NOTIMPL;')
            print('}')
            print()
        print('static const struct %sVtbl %s_vtbl =' % (iface, prefix))
        print('{')
        print('    %s_QueryInterface,' % prefix)
        print('    %s_AddRef,' % prefix)
        print('    %s_Release,' % prefix)
        print('    /* IInspectable methods */')
        print('    %s_GetIids,' % prefix)
        print('    %s_GetRuntimeClassName,' % prefix)
        print('    %s_GetTrustLevel,' % prefix)
        print('    /* %s methods */' % iface)
        for _, meth, _ in meths:
            print('    %s_%s,' % (prefix, meth))
        print('};')


if __name__ == '__main__':
    main()
