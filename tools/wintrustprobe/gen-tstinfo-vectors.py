#!/usr/bin/env python3
# gen-tstinfo-vectors.py > tstinfo-vectors.h: RFC 3161 TSTInfo structures for tstinfoprobe, each optional part on its
# own, DER by hand: version 1, policy 1.2.3.4, a SHA-256 imprint of 0x11 bytes, serial 0102030405, 2023-07-03 07:14:34
def der(tag, content):
    l = len(content)
    if l < 0x80: hdr = bytes([tag, l])
    elif l < 0x100: hdr = bytes([tag, 0x81, l])
    else: hdr = bytes([tag, 0x82, l >> 8, l & 0xff])
    return hdr + content

def integer(v):
    return der(0x02, v.to_bytes((v.bit_length() + 8) // 8 or 1, 'big'))

def oid(s):
    p = [int(x) for x in s.split('.')]
    out = bytes([p[0] * 40 + p[1]])
    for x in p[2:]:
        enc = [x & 0x7f]; x >>= 7
        while x: enc.append(0x80 | (x & 0x7f)); x >>= 7
        out += bytes(reversed(enc))
    return der(0x06, out)

imprint = der(0x30, der(0x30, oid('2.16.840.1.101.3.4.2.1') + bytes([5, 0])) + der(0x04, bytes([0x11] * 32)))

def tst(time=b'20230703071434.074Z', acc=None, ordering=None, nonce=None, tsa=None, ext=None, version=1):
    c = integer(version) + oid('1.2.3.4') + imprint + der(0x02, bytes([1, 2, 3, 4, 5])) + der(0x18, time)
    if acc is not None: c += der(0x30, acc)
    if ordering is not None: c += der(0x01, bytes([0xff if ordering else 0]))
    if nonce is not None: c += der(0x02, nonce)
    if tsa is not None: c += der(0xa0, tsa)
    if ext is not None: c += der(0xa1, ext)
    return der(0x30, c)

name = der(0xa4, der(0x30, der(0x31, der(0x30, oid('2.5.4.3') + der(0x13, b'TSA')))))
ext = der(0x30, oid('1.2.3.5') + der(0x04, b'\x05\x00'))
vectors = [
    ('plain', tst()),
    ('seconds', tst(acc=integer(1))),
    ('seconds_millis', tst(acc=integer(1) + der(0x80, bytes([1, 0xf4])))),
    ('millis', tst(acc=der(0x80, bytes([1, 0xf4])))),
    ('micros', tst(acc=der(0x81, bytes([7])))),
    ('all_accuracy', tst(acc=integer(2) + der(0x80, bytes([3])) + der(0x81, bytes([4])))),
    ('empty_accuracy', tst(acc=b'')),
    ('ordering', tst(ordering=True)),
    ('ordering_false', tst(ordering=False)),
    ('nonce', tst(nonce=bytes([1, 2, 3, 4, 5, 6, 7, 8]))),
    ('tsa', tst(tsa=name)),
    ('extensions', tst(ext=ext)),
    ('everything', tst(acc=integer(1) + der(0x80, bytes([1, 0xf4])) + der(0x81, bytes([0x63])), ordering=True,
                       nonce=bytes([0x7f, 0xfe]), tsa=name, ext=ext)),
    ('no_fraction', tst(time=b'20230703071434Z')),
    ('one_digit', tst(time=b'20230703071434.5Z')),
    ('four_digits', tst(time=b'20230703071434.0745Z')),
    ('six_digits', tst(time=b'20230703071434.074123Z')),
    ('version2', tst(version=2)),
]
for n, v in vectors:
    print('static const BYTE tst_%s[] = {' % n)
    for i in range(0, len(v), 16):
        print('    ' + ', '.join('0x%02x' % x for x in v[i:i + 16]) + ',')
    print('};')
print('static const struct { const char *name; const BYTE *data; DWORD size; } tst_vectors[] = {')
for n, v in vectors: print('    { "%s", tst_%s, sizeof(tst_%s) },' % (n, n, n))
print('};')
