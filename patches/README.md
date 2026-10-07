# Patches

Four kinds of patch live here. They differ in what they are for.

## `altars-up/` — the code

578 patches on top of upstream Wine `wine-11.19`, in order. This is what gets built.
[`altars-up/README.md`](altars-up/README.md) explains the layout; `scripts/build-from-series.sh`
fetches upstream, applies them and builds; `scripts/export-series.sh` regenerates them from the
Wine tree they are developed in.

## `office/NNNN-*.patch` — the discovery record

Numbered in the order the problems were found, each with its reasoning: what was observed, what was
ruled out, why this change and not another. **Their value is the explanation**, not the diff.

They do not form a clean sequence you can `git am`, because they honestly record the exploration:
the same file is changed again by later patches (`dlls/d3d11/shared.c` appears in 0009, 0015,
0016, 0018, 0019 and 0020), so an early patch's context no longer matches the final state. Read
them for *why*; to rebuild the code use `altars-up/`.

Several are marked "diagnostic" (0007, 0009, 0013, 0016, 0017, 0019): temporary code added to see a
phenomenon, not part of the final state. They are kept because they record how the phenomenon was
measured, so the next one of its kind does not need that instrument invented again.

## `mstsc/` — the inherited RDP-client series

The 13 patches this project started from, written for running Microsoft's RDP client engine
(`mstscax.dll`) against `gnome-remote-desktop`: CredSSP, MUI resource redirection, ETW
TraceLogging decoding, `srpapi`, and others. [`mstsc/README.md`](mstsc/README.md) describes them;
[`mstsc/upstream/`](mstsc/upstream) is the same work reshaped as upstream-style submissions.
They are part of `altars-up/` now.

## `mesa/` and `ported/`

`mesa/` is a fix that does not belong to Wine: Office hung now and then because a new GL context
entered Mesa's shared-context list before it had its state tracker, and another thread walking the
list with the lock held crashed. Wine swallows that fault and the lock is never released. Patch,
evidence and a workaround are in [`mesa/README.md`](mesa/README.md); it has not been submitted
to Mesa.

`ported/` holds work taken from other trees (here, one Valve patch), each with its source named.
[`../docs/upstream-evaluation.md`](../docs/upstream-evaluation.md) is the survey of what else was
looked at and rejected, and why; that list is the useful half.
