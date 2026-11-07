#!/usr/bin/env bash
#
# test/run.sh - one-command test gate for the zlib package.
#
#   bash test/run.sh
#   CT=/path/to/ct bash test/run.sh
#
# Builds both fixtures with `-Wall -Wextra -lz`, rejects any compiler warning,
# runs demo.ct against demo.expected.out, then runs unit.ct and folds its
# `SUMMARY pass=N fail=M` into the final totals. All build products go to a
# fresh /tmp directory that is removed on exit.
#
# Exit code 0 means every check and every unit case passed.

set -u

here="$(cd "$(dirname "$0")" && pwd)"
pkg="$(cd "$here/.." && pwd)"
CT="${CT:-/tmp/ct-0.20.0-worktree/ct}"

if [ ! -x "$CT" ]; then
    echo "ct not found at $CT (set CT=/path/to/ct)" >&2
    exit 1
fi

work="$(mktemp -d /tmp/ct-zlib-test.XXXXXX)" || exit 1
trap 'rm -rf "$work"' EXIT HUP INT TERM

pass=0
fail=0
ok()  { pass=$((pass + 1)); printf '  ok    %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf '  FAIL  %s: %s\n' "$1" "$2"; }

# Build one fixture from the scratch directory, so .ctbuild never lands in the
# package tree. A compiler warning counts as a failed check.
build_one() {
    local src="$1" out="$2" label="$3" log rc
    log="$work/$label.build.log"

    ( cd "$work" && "$CT" build -I "$pkg" \
        --cflags '-Wall -Wextra' --ldflags '-lz' "$src" -o "$out" ) \
        >"$log" 2>&1
    rc=$?
    if [ "$rc" -ne 0 ]; then
        bad "$label builds" "ct exit $rc"
        sed 's/^/    /' "$log"
        return 1
    fi
    if grep -q 'warning:' "$log"; then
        bad "$label builds without warnings" "compiler output:"
        sed 's/^/    /' "$log"
        return 1
    fi
    ok "$label builds with -Wall -Wextra, zero warnings"
    return 0
}

echo "ct: $CT ($("$CT" --version 2>&1 | head -1))"
echo "package: $pkg"
echo "work: $work"
echo

echo "== demo (golden output) =="
if build_one "$here/demo.ct" "$work/demo" "demo.ct"; then
    "$work/demo" >"$work/demo.out" 2>&1
    rc=$?
    if [ "$rc" -eq 0 ]; then
        if diff -u "$here/demo.expected.out" "$work/demo.out" >"$work/demo.diff"; then
            ok "demo output matches demo.expected.out"
        else
            bad "demo output matches demo.expected.out" "diff below"
            sed 's/^/    /' "$work/demo.diff"
        fi
    else
        bad "demo exits 0" "exit $rc"
        sed 's/^/    /' "$work/demo.out"
    fi
fi

echo
echo "== unit =="
if build_one "$here/unit.ct" "$work/unit" "unit.ct"; then
    "$work/unit" >"$work/unit.out" 2>&1
    rc=$?
    cat "$work/unit.out"

    summary="$(grep '^SUMMARY ' "$work/unit.out" | tail -1)"
    if [ -z "$summary" ]; then
        bad "unit.ct reports SUMMARY" "missing (exit $rc)"
    else
        # shellcheck disable=SC2086
        set -- $summary
        upass="${2#pass=}"
        ufail="${3#fail=}"
        pass=$((pass + upass))
        fail=$((fail + ufail))
        if [ "$rc" -ne 0 ] && [ "$ufail" -eq 0 ]; then
            bad "unit exit status" "non-zero with no failed case"
        fi
    fi
fi

echo
printf 'TOTAL pass=%d fail=%d\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
