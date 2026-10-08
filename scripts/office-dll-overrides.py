#!/usr/bin/env python3
"""Find the DLLs where Wine's builtin shadows a richer copy Office ships, and
write the native overrides that stop it.

The problem this exists for, measured on PowerPoint: Office loads
`Common Files\\Microsoft Shared\\Office16\\Riched20.dll` by full path, Wine
substitutes its own builtin riched20 for it (the name matches a known system
dll, and builtin wins by default), and Office's `oart.dll` then does

    h = LoadLibrary(...riched20...)
    p = GetProcAddress(h, "MathBuildUp")     <-- NULL
    if (!p) throw Art::CTextLayoutException

which PowerPoint's exception classifier does not recognise, so it reports
"not enough memory or system resources" and never opens a window. Wine's
riched20 exports 9 names; Office's exports 66. The 57 missing ones are
Office's own -- equation conversion, LaTeX/MathML/OMML, text-box layout,
cloud fonts -- and nothing in Wine is going to grow them.

So the fix is not to implement them; it is to stop shadowing the file Office
brought. This script finds those cases by comparing export tables rather than
by guessing at a list, and prints (or, with --apply, sets) the overrides.

    scripts/office-dll-overrides.py                 # report
    scripts/office-dll-overrides.py --apply         # set them in $WINEPREFIX

A DLL is reported when Office ships one with the same name as a Wine builtin
and Office's exports are a strict superset -- "Wine's copy cannot answer
questions this copy can". Same-or-fewer exports are left alone: builtin is
preferable where it is sufficient, since it is the copy this project patches.
"""
import argparse, os, struct, subprocess, sys
from pathlib import Path

def exports(path):
    try: d = path.read_bytes()
    except OSError: return None
    if d[:2] != b'MZ': return None
    try:
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        if d[pe:pe+4] != b'PE\0\0': return None
        machine = struct.unpack_from('<H', d, pe+4)[0]
        if machine != 0x8664: return None            # x64 only, like the build
        optsz = struct.unpack_from('<H', d, pe+20)[0]
        opt = pe+24
        magic = struct.unpack_from('<H', d, opt)[0]
        ddoff = opt + (112 if magic == 0x20b else 96)
        e_rva, _ = struct.unpack_from('<II', d, ddoff)
        if not e_rva: return set()
        nsec = struct.unpack_from('<H', d, pe+6)[0]
        sh = opt + optsz
        secs = []
        for i in range(nsec):
            o = sh + i*40
            vs, va, rs, pr = struct.unpack_from('<IIII', d, o+8)
            secs.append((va, max(vs, rs), pr))
        def fo(r):
            for va, vs, pr in secs:
                if va <= r < va+vs: return pr + (r-va)
        o = fo(e_rva)
        if o is None: return set()
        # IMAGE_EXPORT_DIRECTORY: NumberOfFunctions at +20, NumberOfNames +24,
        # AddressOfFunctions +28, AddressOfNames +32, AddressOfNameOrdinals +36.
        _nfun, nname, _fn_rva, nm_rva, _ord_rva = struct.unpack_from('<IIIII', d, o+20)
        out = set()
        base = fo(nm_rva)
        if base is None: return set()
        for i in range(nname):
            nr = struct.unpack_from('<I', d, base + i*4)[0]
            p2 = fo(nr)
            if p2 is None: continue
            out.add(d[p2:d.index(b'\0', p2)].decode('latin1'))
        return out
    except Exception:
        return None

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--prefix', default=os.environ.get('WINEPREFIX', str(Path.home()/'.wine-c2r-up')))
    ap.add_argument('--dist', default=str(Path(__file__).resolve().parent.parent/'dist-up'))
    ap.add_argument('--apply', action='store_true', help='set the overrides in the prefix')
    a = ap.parse_args()

    builtin_dir = Path(a.dist)/'lib'/'wine'/'x86_64-windows'
    if not builtin_dir.is_dir():
        sys.exit(f'no builtin dlls at {builtin_dir}')
    builtins = {p.name.lower(): p for p in builtin_dir.glob('*.dll')}

    office = Path(a.prefix)/'drive_c'/'Program Files'
    if not office.is_dir(): sys.exit(f'no Office tree under {office}')

    seen, findings = set(), []
    for p in office.rglob('*.[dD][lL][lL]'):
        name = p.name.lower()
        if name not in builtins or name in seen: continue
        oe = exports(p)
        if oe is None: continue
        we = exports(builtins[name])
        if we is None: continue
        seen.add(name)
        extra = oe - we
        if extra and we < oe:
            findings.append((name, len(we), len(oe), sorted(extra)))

    if not findings:
        print('no Office dll has a strictly richer export table than its Wine builtin')
        return
    print(f'{len(findings)} dll(s) where Office ships more than Wine\'s builtin exports:\n')
    for name, nw, no, extra in sorted(findings):
        print(f'  {name:24s} wine {nw:4d} exports, Office {no:4d}  (+{len(extra)})')
        print(f'      e.g. {", ".join(extra[:6])}')
    names = ','.join(n[:-4] for n, *_ in sorted(findings))
    print(f'\nWINEDLLOVERRIDES="{"=n;".join(n[:-4] for n, *_ in sorted(findings))}=n"')
    if a.apply:
        wine = Path(a.dist)/'bin'/'wine'
        env = dict(os.environ, WINEPREFIX=a.prefix, WINEDEBUG='-all')
        for n, *_ in sorted(findings):
            subprocess.run([str(wine), 'reg', 'add', r'HKCU\Software\Wine\DllOverrides',
                            '/v', n[:-4], '/t', 'REG_SZ', '/d', 'native', '/f'],
                           env=env, capture_output=True)
            print(f'  set {n[:-4]} = native')

if __name__ == '__main__':
    main()
