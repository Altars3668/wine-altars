#!/usr/bin/env bash
# Export the Wine commits on top of upstream as patches/altars-up/, and prove the export is exact.
#
#   scripts/export-series.sh                 # branch altars-up of $SRC, into patches/altars-up
#   TIP=other-branch OUT=/tmp/series scripts/export-series.sh
#
# What it does:
#   1. reads BASE (the upstream tag and commit the series sits on) from the output directory;
#   2. git format-patch BASE_COMMIT..TIP, one numbered file per commit (--zero-commit and no
#      signature, so re-exporting an unchanged series changes nothing);
#   3. applies every patch, in order, to a scratch index of BASE_COMMIT -- no working tree touched --
#      and checks that the result is byte-for-byte the tree of TIP;
#   4. rewrites SERIES.tsv (number, file, the commit it came from, subject).
#
# The patch files keep the author's name and email exactly as the commits have them; scrubbing for
# publication is done on the published copy, not here.
#
# Environment: SRC (Wine tree, default <repo>/wine-src), TIP (default altars-up), OUT (default
# <repo>/patches/altars-up).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="${SRC:-$ROOT/wine-src}"
TIP="${TIP:-altars-up}"
OUT="${OUT:-$ROOT/patches/altars-up}"

[ -r "$OUT/BASE" ] || { echo "no $OUT/BASE" >&2; exit 1; }
# shellcheck source=/dev/null
. "$OUT/BASE"
git -C "$SRC" rev-parse --verify --quiet "$BASE_COMMIT^{commit}" >/dev/null ||
    { echo "$SRC does not have $BASE_COMMIT ($BASE_TAG); fetch upstream first" >&2; exit 1; }
tip=$(git -C "$SRC" rev-parse --verify "$TIP^{commit}")
git -C "$SRC" merge-base --is-ancestor "$BASE_COMMIT" "$tip" ||
    { echo "$TIP does not contain $BASE_COMMIT; rebase it onto $BASE_TAG first" >&2; exit 1; }
n=$(git -C "$SRC" rev-list --count "$BASE_COMMIT..$tip")
echo "exporting $n commits: $BASE_TAG ($BASE_COMMIT) .. $TIP ($tip)"

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
git -C "$SRC" format-patch --zero-commit --no-signature --no-stat --binary -N --start-number 1 \
    -o "$stage" "$BASE_COMMIT..$tip" >/dev/null

echo "checking that the patches rebuild the tree of $TIP ..."
export GIT_INDEX_FILE="$stage/index"
git -C "$SRC" read-tree "$BASE_COMMIT"
for p in "$stage"/0*.patch; do
    git -C "$SRC" apply --cached --whitespace=nowarn "$p" || { echo "does not apply: $p" >&2; exit 1; }
done
got=$(git -C "$SRC" write-tree)
unset GIT_INDEX_FILE
want=$(git -C "$SRC" rev-parse "$tip^{tree}")
[ "$got" = "$want" ] || { echo "MISMATCH: the series gives tree $got, $TIP has $want" >&2; exit 1; }
echo "ok: tree $got"

mkdir -p "$OUT"
rm -f "$OUT"/0*.patch
cp "$stage"/0*.patch "$OUT"/
{
    printf '# number\tfile\torigin-commit\tsubject\n'
    i=0
    git -C "$SRC" log --reverse --format='%h%x09%s' "$BASE_COMMIT..$tip" | while IFS=$'\t' read -r h s; do
        i=$((i+1))
        num=$(printf '%04d' "$i")
        f=$(basename "$(printf '%s\n' "$stage"/"$num"-*.patch | head -1)")
        printf '%s\t%s\t%s\t%s\n' "$num" "$f" "$h" "$s"
    done
} > "$OUT/SERIES.tsv"
echo "wrote $n patches and SERIES.tsv to $OUT"
