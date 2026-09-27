#!/bin/bash
MAKE_FLAGS="$1"
STAMP="$2"
SRC_DIR="$3"
PATCHES_DIR="$4"

echo "$MAKE_FLAGS" > "${STAMP}.tmp"
if ! cmp -s "${STAMP}.tmp" "$STAMP"; then
    echo "vitaGL flags changed, rebuilding..."
    cd "$SRC_DIR" && make clean
    cp "${STAMP}.tmp" "$STAMP"
fi

# Applies this project's own patches to the vendored vitaGL checkout (see
# CLAUDE.md) -- kept as .patch files instead of committing directly into the
# submodule so `git submodule update --init` on a fresh clone always lands on
# a clean, fetchable upstream commit; this step is what actually makes the
# fix durable. Idempotent: `git apply -R --check` tells us if a patch is
# already applied so re-running this (every build) is a no-op after the first.
if [ -n "$PATCHES_DIR" ]; then
    for patch in "$PATCHES_DIR"/vitagl-*.patch; do
        [ -f "$patch" ] || continue
        if ! git -C "$SRC_DIR" apply -R --check "$patch" 2>/dev/null; then
            echo "Applying $(basename "$patch")..."
            git -C "$SRC_DIR" apply "$patch"
        fi
    done
fi
