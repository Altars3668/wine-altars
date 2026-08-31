#!/usr/bin/env bash
# Port a single upstream commit (wine-staging, Valve, anywhere) onto altars.
#
#   scripts/port-upstream.sh https://github.com/ValveSoftware/wine 9f7ab6c
#
# Keeps the provenance in the commit message and archives the patch under
# patches/ported/. Does not pretend the result is tested: build and measure.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/wine-src"
REPO="${1:?usage: port-upstream.sh <repo-url> <commit-ish>}"
REF="${2:?usage: port-upstream.sh <repo-url> <commit-ish>}"
CACHE="${CACHE:-/tmp/wine-altars-upstream/$(basename "$REPO")}"

mkdir -p "$(dirname "$CACHE")"
if [ ! -d "$CACHE" ]; then
    echo "==> fetching $REPO (metadata only)"
    git clone --filter=blob:none --no-checkout "$REPO" "$CACHE"
fi
git -C "$CACHE" fetch -q --filter=blob:none origin || true

SUBJ=$(git -C "$CACHE" log -1 --format=%s "$REF")
AUTH=$(git -C "$CACHE" log -1 --format='%an <%ae>' "$REF")
FULL=$(git -C "$CACHE" rev-parse --short "$REF")
OUT="$ROOT/patches/ported/$(printf '%s' "$SUBJ" | tr -c 'A-Za-z0-9' '-' | cut -c1-60).patch"

mkdir -p "$ROOT/patches/ported"
git -C "$CACHE" format-patch -1 "$REF" --stdout > "$OUT"

cd "$SRC"
git checkout -q altars
if ! patch -p1 --dry-run --silent < "$OUT" >/dev/null 2>&1; then
    echo "!! does not apply cleanly to altars; adapt it by hand:" >&2
    patch -p1 --dry-run < "$OUT" 2>&1 | head -10 >&2
    exit 1
fi
patch -p1 --silent < "$OUT"
git add -A
git commit -q -m "$SUBJ

Ported from $REPO ($FULL), by $AUTH.

TODO: say here whether this changes anything measurable in this project, and
say so plainly if it does not."
echo "applied: $SUBJ"
echo "  commit  $(git rev-parse --short HEAD)"
echo "  archived $OUT"
echo "  now: scripts/build-wine.sh, then measure."
