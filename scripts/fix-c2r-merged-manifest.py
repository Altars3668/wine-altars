#!/usr/bin/env python3
"""Put the product extensions of Click-to-Run's merged App-V manifest back into their namespace.

Click-to-Run builds "Microsoft Office\\AppXManifest.xml" (and the App-V catalog's Manifest.xml and
UserManifest.xml) by cloning the appv:Extension elements of every installed product's
PackageManifests\\AppXManifest.<product>.xml into the common manifest with MSXML.  Before
e7313aa729c ("msxml3: Keep the prefixes of a node and its attributes in a clone") Wine's clones
lost their prefixes, so a prefix installed with such a Wine got every product extension serialized
as <Extension> in the document's default (appx) namespace.  App-V ignores those, so none of
Excel's, Word's, PowerPoint's or Outlook's file types and COM classes is ever integrated, and the
Click-to-Run service answers DetermineIsRepairRequiredEx("Excel") with True because
HKLM\\Software\\Classes\\Excel.Sheet.12\\shell\\open\\command is missing: every cold start of Excel
or PowerPoint then runs TaskIntegrateRepair, which cannot help, since it integrates the same
manifest.

Click-to-Run rewrites the merged manifest only when it installs, updates or refreshes cultures.
This finds, for each misplaced element, the product manifest element it was cloned from, by
comparing names, attributes and text while ignoring namespaces (a "!(loc.name)" placeholder in the
source matches the text the merge localized it to), and gives the element and its descendants the
namespaces of that source, keeping everything else, so the result is what the merge gives with a
correct msxml.  Nothing is written unless every misplaced element is matched.

    fix-c2r-merged-manifest.py <prefix> [--write --backup <dir>]

Without --write it only reports.  The files it rewrites are copied to <dir> first, outside the
directories Click-to-Run and App-V read.
"""
import argparse, os, shutil, sys
from collections import defaultdict, deque
from lxml import etree

APPX = 'http://schemas.microsoft.com/appx/2010/manifest'
APPV = 'http://schemas.microsoft.com/appv/2010/manifest'
PACKAGE = '{9AC08E99-230B-47E8-9721-4577B7F124EA}'


def local(tag):
    return tag.split('}', 1)[-1]


def value(text):
    """A localization placeholder matches whatever the merge localized it to."""
    text = (text or '').strip()
    return '!(loc)' if text.startswith('!(loc.') else text


def signature(el, localized=False):
    """The element's names, attributes and text, without namespaces; with localized, the text
    of the elements a source placeholder may have been localized in is left out."""
    attrs = tuple(sorted((local(k), v if not localized else value(v)) for k, v in el.attrib.items()))
    text = value(el.text) if not localized else '*'
    return (local(el.tag), attrs, text, tuple(signature(c, localized) for c in el if isinstance(c.tag, str)))


def children(el):
    return [c for c in el if isinstance(c.tag, str)]


def placeholders_match(el, src):
    """Every text that differs from the source is where the source has a placeholder."""
    if (el.text or '').strip() != (src.text or '').strip() and not (src.text or '').strip().startswith('!(loc.'):
        return False
    return all(placeholders_match(a, b) for a, b in zip(children(el), children(src)))


def renamespace(el, src):
    """Give el, and in parallel its descendants and attributes, the namespaces of src."""
    ns = src.tag.split('}', 1)[0][1:] if src.tag.startswith('{') else None
    el.tag = '{%s}%s' % (ns, local(el.tag)) if ns else local(el.tag)
    src_attrs = {local(k): k for k in src.attrib}
    for k in list(el.attrib):
        if local(k) in src_attrs and src_attrs[local(k)] != k:
            el.attrib[src_attrs[local(k)]] = el.attrib.pop(k)
    for a, b in zip(children(el), children(src)):
        renamespace(a, b)


def load(path):
    parser = etree.XMLParser(remove_blank_text=False, huge_tree=True)
    return etree.parse(path, parser)


def sources(prefix):
    manifests = os.path.join(prefix, 'drive_c/Program Files/Microsoft Office/PackageManifests')
    exact, localized = defaultdict(deque), defaultdict(deque)
    for name in sorted(os.listdir(manifests)):
        if not name.endswith('.xml') or name.startswith('AppXManifestLoc'):
            continue
        root = load(os.path.join(manifests, name)).getroot()
        for ext in root.iter('{%s}Extension' % APPV):
            exact[signature(ext)].append((name, ext))
            localized[signature(ext, True)].append((name, ext))
    return exact, localized


def retag(el, ns):
    """Put el and its descendants in ns, as App-V writes every extension in its user manifest."""
    for d in el.iter():
        if isinstance(d.tag, str):
            d.tag = '{%s}%s' % (ns, local(d.tag))


def fix(path, table, write, backup=None):
    tree = load(path)
    root = tree.getroot()
    extensions = next((c for c in root if isinstance(c.tag, str) and local(c.tag) == 'Extensions'), None)
    if extensions is None:
        print(f'{path}: no Extensions')
        return True
    ns = extensions.tag.split('}', 1)[0][1:]
    wrong = [e for e in extensions if isinstance(e.tag, str) and e.tag == '{%s}Extension' % APPX]
    right = [e for e in extensions if isinstance(e.tag, str) and e.tag != '{%s}Extension' % APPX]
    print(f'{path}: {len(right)} extensions in {ns}, {len(wrong)} in appx')
    if not wrong:
        return True
    exact, localized = table
    replacements = []
    for e in wrong:
        candidates = exact.get(signature(e)) or [c for c in localized.get(signature(e, True), ())
                                                  if placeholders_match(e, c[1])]
        if not candidates:
            print(f'  unmatched: {etree.tostring(e)[:200]!r}')
            return False
        name, src = candidates[0]
        replacements.append((e, src, name))
    per_source = defaultdict(int)
    for _, _, name in replacements:
        per_source[name] += 1
    for name, n in sorted(per_source.items()):
        print(f'  {n:5d} from {name}')
    if not write:
        return True
    for e, src, _ in replacements:
        # the merged manifests keep each element's own appv namespace; App-V's user manifest has
        # them all in the one its Extensions element is in
        if ns == APPV:
            renamespace(e, src)
        else:
            retag(e, ns)
    # drop the declarations the copies brought along, but keep every one the root had: its
    # IgnorableNamespaces names appv1.1 and appv1.2 by prefix, whether an element uses them or not
    declared = {k: v for k, v in root.nsmap.items() if k}
    etree.cleanup_namespaces(root, top_nsmap=declared, keep_ns_prefixes=list(declared))
    check = [e for e in extensions if isinstance(e.tag, str) and e.tag == '{%s}Extension' % APPX]
    if check:
        print(f'  {len(check)} still in appx, not written')
        return False
    with open(path, 'rb') as f:
        declaration = f.read(5) == b'<?xml'
    shutil.copy2(path, os.path.join(backup, path.strip('/').replace('/', '_')))
    data = etree.tostring(root, encoding='utf-8', xml_declaration=False)
    if declaration:
        data = b'<?xml version="1.0" encoding="UTF-8"?>\n' + data
    with open(path + '.new', 'wb') as f:
        f.write(data)
    os.replace(path + '.new', path)
    print(f'  written ({len(data)} bytes)')
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('prefix')
    ap.add_argument('--write', action='store_true')
    ap.add_argument('--backup')
    args = ap.parse_args()
    if args.write and not args.backup:
        ap.error('--write needs --backup')
    if args.backup:
        os.makedirs(args.backup, exist_ok=True)
    table = sources(args.prefix)
    catalog = os.path.join(args.prefix, 'drive_c/ProgramData/Microsoft/ClickToRun/MachineData/Catalog/Packages', PACKAGE)
    paths = [os.path.join(args.prefix, 'drive_c/Program Files/Microsoft Office/AppXManifest.xml')]
    for version in sorted(os.listdir(catalog)) if os.path.isdir(catalog) else []:
        for name in ('Manifest.xml', 'UserManifest.xml'):
            p = os.path.join(catalog, version, name)
            if os.path.exists(p):
                paths.append(p)
    ok = all([fix(p, table, args.write, args.backup) for p in paths])
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
