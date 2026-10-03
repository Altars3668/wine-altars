#!/usr/bin/env python3
"""Find the code in a PE that refers to an RVA: rip-relative operands and direct calls or jumps.

    pe-xrefs.py <module.dll> <rva-hex>...

Prints one line per candidate: the RVA of the instruction's displacement or opcode, what kind of
reference it is, and the target.  Optimised Office code reaches its statics through rip-relative
operands (a `lea rcx,[rip+x]` before calling a smart pointer's Clear, say) and its functions through
`call rel32`; a breakpoint tells which static or function misbehaves, this tells who else touches
it.  Every 4-byte window of the executable sections is tried as a disp32 with the instruction ending
0, 1, 2 or 4 bytes after it (no immediate, imm8, imm16, imm32), so these are candidates: disassemble
each one to confirm.  Found that only MSO's atexit callback named OfficeVoice's roaming settings
static, and that nothing at all called its UnInit.
"""
import struct, sys

def main():
    d = open(sys.argv[1], 'rb').read()
    targets = set(int(x, 16) for x in sys.argv[2:])
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    sh = pe + 24 + struct.unpack_from('<H', d, pe + 20)[0]
    for i in range(nsec):
        vsize, va, rsize, raw = struct.unpack_from('<IIII', d, sh + i * 40 + 8)
        if not struct.unpack_from('<I', d, sh + i * 40 + 36)[0] & 0x20000000:
            continue    # code only
        sec = d[raw:raw + min(vsize, rsize)]
        branch = None
        for off in range(len(sec) - 4):
            disp = struct.unpack_from('<i', sec, off)[0]
            for extra in (0, 1, 2, 4):
                t = va + off + 4 + extra + disp
                # a call's or jump's rel32 reads as a disp32 too: that one is said once, as the branch
                if t in targets and not (extra == 0 and branch == off - 1):
                    print('%x rip-relative (imm %d) -> %x' % (va + off, extra, t))
            if sec[off] in (0xe8, 0xe9) and off + 5 <= len(sec):
                t = va + off + 5 + struct.unpack_from('<i', sec, off + 1)[0]
                if t in targets:
                    print('%x %s -> %x' % (va + off, 'call' if sec[off] == 0xe8 else 'jmp', t))
                    branch = off

main()
