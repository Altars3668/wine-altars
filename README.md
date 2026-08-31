# wine-altars

A Wine fork aimed at running closed-source Microsoft clients on Linux —
currently **Microsoft 365 (Click-to-Run)** and, from the work this is built on,
Microsoft's RDP client engine.

Base: **Wine 11.0** + a 13-patch series carried over from the
`gnome-remote-desktop` / `mstsc` interop work, plus whatever this project adds.

## Layout

    patches/mstsc/      the inherited 13-patch series (CredSSP, MUI redirection,
                        TraceLogging, srpapi, ...) — see patches/mstsc/README.md
    patches/office/     patches written for Microsoft 365 (empty until earned)
    patches/ported/     work ported from wine-staging / Valve / wine-tkg
                        (see docs/upstream-evaluation.md for what was rejected
                         and why — that list is the useful half)
    scripts/            build, and the Office import pipeline
    docs/               what was measured, and where it currently stops
    wine-src/           the Wine tree (own git repo, gitignored here)

`wine-src` is a git repository with three refs that mean something:

| ref                        | what it is                                        |
|----------------------------|---------------------------------------------------|
| `base` (tag `wine-11.0`)   | pristine upstream tarball, nothing applied        |
| `altars`                   | `base` + the 13 patches, one commit each          |
| `provenance/original-tree` | the tree as inherited, kept to prove equivalence  |

`altars` was verified byte-identical to the inherited tree, so the patch series
is known to reproduce a Wine that was already shown to work.

## Build

    scripts/build-wine.sh          # configure + make + install into dist/

64-bit only (`--enable-archs=x86_64`) — every client targeted here is x64 and a
multiarch build costs several times as much for nothing.

## Microsoft 365

    scripts/setup-prefix.sh              # create the prefix, Windows 11 mode
    scripts/import-office.sh             # copy Office out of a real C: drive
    scripts/export-office-registry.py    # SOFTWARE + SYSTEM + NTUSER + App-V vreg
    scripts/apply-office-registry.sh     # import the .reg into the prefix

**Status: Word does not start yet.** It gets 186 modules in and stops inside
Click-to-Run's bootstrap, before any Office code of its own is loaded. What was
measured, and what has been ruled out, is in
[`docs/office365-under-wine.md`](docs/office365-under-wine.md) — read that
before repeating any of it.

## Docs

* [`docs/office365-under-wine.md`](docs/office365-under-wine.md) — the Office
  work: what was measured, what it rules out, where it stops.
* [`docs/upstream-evaluation.md`](docs/upstream-evaluation.md) — wine-staging,
  Valve and wine-tkg surveyed against this project's actual blocker. 131 + 400
  commits looked at, one taken.
* [`docs/method.md`](docs/method.md) — the instruments, and the traps already
  hit.

## Method

Every claim here is a register read, a memory read, or bytes on the wire. An
inference from a true fact is not a measurement — several plausible ones have
already been wrong. The instruments and the traps are described in
`docs/method.md`.
