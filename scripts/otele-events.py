#!/usr/bin/env python3
"""Read Office's own telemetry: which activities ran, and what they returned.

Office keeps a per-application event store at

    %LOCALAPPDATA%\\Microsoft\\Office\\OTele\\<app>.exe.db

and it is the only place that says, in Office's own words, why something did
not work. The events are Bond CompactBinary inside a BLOB column, but the parts
worth reading -- the activity name, how long it took, and its HRESULT -- decode
without the schema, because each value is preceded by its own name as a string.

Two things will cost you the data if you do not know them:

  * Nearly everything recent lives in the -wal, not the .db, and sqlite
    checkpoints and *deletes* the -wal the moment it opens the database. Copy
    both files first and read the copy. This is not hypothetical; it destroyed
    1.2 MB of a live run here once, and a second copy the next day.
  * A field header is (id << 5) | type, so the byte after a name is not the
    value. Reading it as one turns 0xc004f015 into a 38-bit number that fits
    nothing.

Usage:  scripts/otele-events.py [path to winword.exe.db | prefix drive_c]
"""
import os, re, shutil, sys, tempfile

BT_SIGNED   = {14, 15, 16, 17}
BT_UNSIGNED = {3, 4, 5, 6}

def uvarint(b, i):
    r = s = 0
    while i < len(b):
        c = b[i]; r |= (c & 0x7f) << s; i += 1; s += 7
        if not c & 0x80: break
    return r, i

def zigzag(n):
    return (n >> 1) ^ -(n & 1)

def read_value(b, i, depth=0):
    """Parse one Bond struct of scalars; returns {field_id: value}."""
    out = {}
    while i < len(b):
        h = b[i]; i += 1
        t, fid = h & 0x1f, h >> 5
        if t == 0: return out, i                      # BT_STOP
        if fid == 6: fid = b[i]; i += 1
        elif fid == 7: fid = b[i] | (b[i + 1] << 8); i += 2
        if t in BT_SIGNED:      raw, i = uvarint(b, i); out[fid] = zigzag(raw)
        elif t in BT_UNSIGNED:  raw, i = uvarint(b, i); out[fid] = raw
        elif t == 2:            out[fid] = bool(b[i]); i += 1
        elif t == 9:
            n, i = uvarint(b, i); out[fid] = b[i:i + n].decode('utf8', 'replace'); i += n
        elif t == 8:            out[fid] = None; i += 8
        elif t == 10 and depth < 3:
            sub, i = read_value(b, i, depth + 1); out[fid] = sub
        else: return out, i
    return out, i

def named(blob, name):
    """Every occurrence of a named value, as {field_id: value}."""
    for m in re.finditer(re.escape(name.encode()), blob):
        v, _ = read_value(blob, m.end())
        yield m.start(), v

def find_db(arg):
    """The biggest store, counting its -wal: an app that never ran has a .db too."""
    if arg and os.path.isfile(arg): return arg
    root = arg or os.path.expanduser('~/.wine-altars-office/drive_c')
    best, best_size = None, -1
    for dirpath, _, files in os.walk(root):
        if os.path.basename(dirpath).lower() != 'otele': continue
        for f in files:
            if not f.endswith('.db'): continue
            p = os.path.join(dirpath, f)
            size = os.path.getsize(p) + (os.path.getsize(p + '-wal') if os.path.exists(p + '-wal') else 0)
            if size > best_size: best, best_size = p, size
    return best

def main():
    db = find_db(sys.argv[1] if len(sys.argv) > 1 else None)
    if not db: sys.exit("no OTele database found (pass one as an argument)")
    tmp = tempfile.mkdtemp(prefix='otele-')
    blob = b''
    for suffix in ('', '-wal'):
        src = db + suffix
        if os.path.exists(src):
            dst = os.path.join(tmp, os.path.basename(src))
            shutil.copy2(src, dst)                     # copy before anything opens it
            blob += open(dst, 'rb').read()
    print("read %s (+wal): %d bytes\n" % (os.path.basename(db), len(blob)))

    # each activity record carries its name, then its measurements, then a CV
    events = []
    for pos, _ in named(blob, 'Activity.AggMode'):
        rec = {}
        for key in ('Activity.Count', 'Activity.Duration', 'Activity.Success',
                    'Activity.Result.Code', 'Activity.CV'):
            m = re.compile(re.escape(key.encode())).search(blob, pos, pos + 6000)
            if m: rec[key], _ = read_value(blob, m.end())
        m = re.compile(rb'Event\.Name.{0,2}?(Office[A-Za-z0-9_.]+)', re.S).search(blob, pos, pos + 8000)
        rec['name'] = m.group(1).decode() if m else '?'
        events.append(rec)

    def cv(rec):
        s = rec.get('Activity.CV', {}).get(3, '')
        base, _, tail = s.rpartition('.')
        return (base, int(tail) if tail.isdigit() else -1)

    seen, rows = set(), []
    for e in sorted(events, key=cv):
        k = (cv(e), e['name'])
        if k in seen: continue
        seen.add(k); rows.append(e)

    print("%-14s %-6s %-42s %8s %8s %s" % ('session', 'seq', 'activity', 'dur', 'ok', 'result'))
    for e in rows:
        base, seq = cv(e)
        code = e.get('Activity.Result.Code', {}).get(4, 0)
        code = '%#010x' % (code & 0xffffffff) if code else 'S_OK'
        print("%-14s .%-5d %-42s %8s %8s %s" % (
            base[:14], seq, e['name'],
            e.get('Activity.Duration', {}).get(4, ''),
            'yes' if e.get('Activity.Success', {}).get(4) else 'no', code))

if __name__ == '__main__':
    main()
