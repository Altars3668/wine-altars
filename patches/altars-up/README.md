# patches/altars-up — the Wine series

This is the code. 578 patches, in order, on top of upstream Wine's tag `wine-11.19`
(the exact commit is in [`BASE`](BASE)). They are plain `git format-patch` output and
need nothing but git:

    git clone --branch wine-11.19 --depth 1 https://github.com/wine-mirror/wine.git
    cd wine
    git am --keep-cr /path/to/wine-altars/patches/altars-up/0*.patch

or let [`scripts/build-from-series.sh`](../../scripts/build-from-series.sh) do the
fetch, the apply and the build in one go.

| file | what it is |
|---|---|
| `0001-*.patch` … `0578-*.patch` | the series, one upstream-style commit each |
| [`SERIES.tsv`](SERIES.tsv) | number, file, the commit it was exported from, subject |
| [`BASE`](BASE) | the upstream tag and commit the series applies to |

**About the commit ids.** The notes under `docs/` cite commits of the author's Wine tree
(`altars-up 1b0ac62bb2f`, `wine-src 048eaaf`, …). That tree is not published; the third
column of `SERIES.tsv` maps those ids to the patch files, so `grep 1b0ac62bb2f SERIES.tsv`
finds the patch.

**What is in it.** 571 patches written for this project, and 7 taken from someone
else's work in progress — Zhiyi Zhang's `NtCreate/Associate/CancelWaitCompletionPacket`
series, with his authorship kept in the `From:` line. Subjects follow Wine's conventions (`dll: Imperative sentence.`). The
bodies say what was measured on Windows 11 and on Wine and why the change has the shape
it has; a large part of the series implements Windows behaviour *after measuring it* with
the probes in [`tools/`](../../tools), where the probe, its Windows output and its Wine
output sit side by side.

**Not upstream.** None of this is part of upstream Wine, and the series is not being
submitted to it. The commit style follows Wine's, so a single patch can be lifted into a
merge request if you want it there; if a fix is useful to you, take it.
