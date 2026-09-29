#!/usr/bin/env python3
"""Unpack the package opcprobe printed as base64 and show its ZIP entries and XML parts.

    unzip-output.py <opcprobe output> [zip32|zipdefault]
"""
import sys, base64, zipfile, io
text = open(sys.argv[1], encoding='utf-8', errors='replace').read().splitlines()
tag = sys.argv[2] if len(sys.argv) > 2 else 'zip32'
data = b''.join(base64.b64decode(l[len(tag) + 1:]) for l in text if l.startswith(tag + ' '))
z = zipfile.ZipFile(io.BytesIO(data))
for i in z.infolist():
    print('%-40s method %d flags %#06x size %5d csize %5d extra %d' % (i.filename, i.compress_type, i.flag_bits,
          i.file_size, i.compress_size, len(i.extra)))
for i in z.infolist():
    if i.filename.endswith('.rels') or i.filename == '[Content_Types].xml':
        print('---', i.filename)
        print(z.read(i).decode('utf-8', 'replace'))
