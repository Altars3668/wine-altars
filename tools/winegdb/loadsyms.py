# gdb: load the symbols of every ELF file a Wine process has mapped.
#
# Wine's preloader loads ld.so and the libraries itself, so gdb finds no link map and names no frame.
# Each ELF mapped at file offset 0 is added at the address it was mapped to (0 for an ET_EXEC).
# Separate debug files are looked up by build id in `debug-file-directory`.
import gdb
import struct

pid = gdb.selected_inferior().pid
bases = {}
for line in open('/proc/%d/maps' % pid):
    fields = line.split()
    if len(fields) < 6 or not fields[5].startswith('/') or int(fields[2], 16) != 0:
        continue
    path = fields[5]
    if path in bases:
        continue
    try:
        with open(path, 'rb') as f:
            header = f.read(18)
    except OSError:
        continue
    if header[:4] != b'\x7fELF':
        continue
    elf_type = struct.unpack('<H', header[16:18])[0]
    bases[path] = 0 if elf_type == 2 else int(fields[0].split('-')[0], 16)

loaded = 0
for path, base in bases.items():
    try:
        gdb.execute('add-symbol-file %s -o %#x' % (path, base), to_string=True)
        loaded += 1
    except gdb.error as e:
        print('no symbols for', path, str(e)[:60])
print('loaded', loaded, 'ELF files')
