#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
sources=(); for f in m4_*.c; do [[ $f == *_test.c ]] || sources+=("$f"); done
zig cc -target x86_64-windows-gnu -O2 -shared -o m4_autofire.asi "${sources[@]}" -luser32 -lwinmm
echo "built plugin/m4_autofire.asi"
