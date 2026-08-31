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
