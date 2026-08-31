# Sending these to WineHQ

The same diffs as the directory above, each with the commit message it should
carry upstream, in `git am` form. Nothing here is applied or sent; it is the
mechanical part done in advance, so that what is left needs an account and a
judgement call rather than an afternoon.

    git clone https://gitlab.winehq.org/wine/wine.git
    cd wine
    git checkout -b rdp-client-support
    git am /path/to/wine-patches/upstream/*.patch

The order is the order they were numbered in: each applies to a pristine tree,
and the ones that share a file are sequenced so that they do not conflict.

## Before sending anything

**The author line needs a real name.** `Signed-off-by` in these files carries
the handle this fork is committed under. Wine's Developer Certificate of Origin
asks for a real name, and a merge request signed off by a handle will be asked
about. Fix it in one pass:

    git filter-branch --env-filter '
      export GIT_AUTHOR_NAME="..." GIT_AUTHOR_EMAIL="..."
      export GIT_COMMITTER_NAME="$GIT_AUTHOR_NAME" GIT_COMMITTER_EMAIL="$GIT_AUTHOR_EMAIL"' \
      -- master..HEAD

and edit the `Signed-off-by:` lines to match.

**Rebase onto current master.** These are against Wine 11.0. Anything in
`kernelbase`, `secur32` or `winegstreamer` has moved since; expect to redo parts
of the three largest by hand rather than resolving conflicts blindly.

**Send them separately, not as one series.** They are twelve unrelated changes
that happen to have been found by the same client. Wine reviews per change, and
a merge request that touches eleven DLLs at once gets no reviewer. One merge
request per patch, or per DLL where two touch the same one.

## What to expect per patch, honestly

Three of these are ready as they are: `ole32`, `msxml3`, `sspicli`. Small,
complete, and each fills in a function that was a stub or a FIXME.

`iphlpapi`, `setupapi`, `advapi32` and `schannel` are ready but will be argued
about, because each answers a question Wine previously declined to answer and
the answer is a judgement:

* `iphlpapi` derives connectivity from the routing table. A reviewer may prefer
  a constant, or may want the notification variant left out entirely rather than
  delivering one state and never another.
* `schannel`'s `SECPKG_ATTR_SESSION_KEY` is **not** the value Windows returns --
  it is RFC 5705 exported keying material, because GnuTLS does not hand out the
  master secret. Say so in the merge request; anything needing byte-for-byte
  agreement with a peer will not get it.

`srpapi` adds a DLL, which needs a `configure.ac` entry and a fresh `configure`;
check the generated file is not in the patch.

`winegstreamer` should be straightforward but is worth checking against master
first -- Wine's video decoder transforms have been reorganised more than once,
and an HEVC one may exist by now.

`kernelbase` is the one with the widest effect and the one most likely to be
sent back. It changes resource lookup for every module, and it is knowingly
incomplete: `EnumResourceTypes` and `EnumResourceNames` still enumerate only the
module's own directory. **Finish that before sending it**, or send it saying so.

`ntdll` TraceLogging is a new capability behind a debug channel, which is an
easier case to make than it looks, but it is large.

`secur32` CredSSP is the biggest by far and implements a whole protocol. It will
want to go last, after the schannel and advapi32 changes it depends on, and it
will want a test.

## Not here

`ntdll-win11-24h2-version.patch` is left out. Wine's version table is maintained
upstream and 24H2 may already be there; if it is not, that is a one-line change
better raised as a question than as a patch from here.
