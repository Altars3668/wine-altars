# Method

The rule: **every claim is a register read, a memory read, or bytes on the
wire.** An inference from a true fact is not a measurement. Both wrong turns
in this project so far were confident inferences.

## Instruments used here, cheapest first

- **`WINEDEBUG=+loaddll`, read the tail.** The last module a process loads
  before it goes quiet says more than any amount of disassembly. This is what
  located Word's stopping point in one run.
- **`WINEDEBUG=+seh`, count the exception codes.** `dispatch_exception code=`
  and `handle_syscall_fault code=`, sorted and counted. One line of grep
  separates "the client refused" from "the client crashed".
- **`WINEDEBUG=err+all,warn+module`.** Names every missing import and every
  DLL that failed to load, at load time, before anything runs.
- **A 10-line `.exe` that asks one question.** When "Wine does not have it" and
  "the client will not use it" are confounded, they are two separate facts
  only if you ask them separately.

## Traps this project has already hit

- **Prove an instrument before believing its silence.** `stub_entry_point`
  prints nothing at all — it raises `EXCEPTION_WINE_STUB` and spins. Grepping
  a log for "stub function" therefore returns nothing whether or not a stub was
  called. The self-test that proved the path is live took one run; trusting the
  silence would have cost a wrong conclusion in the other direction.
- **A patch series exported per-patch is not a linear history.** `0012` here
  contained `0008`'s hunks verbatim, so applying both in order conflicts, and
  `patch --forward` "resolved" it by applying the duplicate somewhere else —
  producing a C file with the same struct defined twice. Verify a replayed
  series against a tree that is known to work; here `git diff` against the
  inherited tree caught it.
- **Library API shapes.** hivex's `value_type()` returns `(type, LENGTH)`.
  Reading it as `(type, data)` threw on every binary value and, with a
  `try/except` around it, dropped 11836 of them without a word. Any counter
  that only ever goes up is worth printing.
- **`pgrep -f` / `pkill -f` match the shell that ran them.** Resolve the PID
  another way.

## Office symbols are published — I was wrong about this

An earlier session of this project asserted that Office has no public symbols,
by analogy with it not being a Windows component. That was an inference, and it
was wrong. Measured:

    scripts/pe-pdb-id.py <binary>     # read the CodeView RSDS record
    https://msdl.microsoft.com/download/symbols/<pdb>/<GUID><age>/<pdb>

| pdb | result |
|---|---|
| `wwlib.pdb` (Word's core) | **200**, 92.7 MB |
| `MSO.pdb` (Office shared layer) | **200**, 87 MB |
| `c2r64.pdb` | **200**, 6 MB |
| `winword.pdb` | **200**, 118 KB |

`kernel32.pdb` and `mstscax.pdb` were fetched in the same run as a control, so
a 200 here means the request shape and the network are both right rather than
the server answering everything.

`wwlib.pdb` yields **264,870 public symbols** — `BootDialog`, `EndBootDialog`,
`HrFatalError`, the whole `FInit*` family. Anything in this project that was
deferred because "Office has no symbols" should be reconsidered.

**llvm-pdbutil cannot read these.** Office builds with a 1024-byte MSF block, so
a large PDB needs more directory block numbers than one block holds (wwlib: 352
needed, 256 per block) and the block map spills across consecutive blocks;
llvm-pdbutil reads only the first and fails with "Too many directory blocks".
`scripts/pdb-symbols.py` handles it — it does the MSF directory, stream 3 for
the symbol-record stream index, and S_PUB32, and nothing else.
