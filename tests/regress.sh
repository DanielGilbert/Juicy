#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Did anything change what Juicy does? Every host script (tests/scripts) runs on the FM-1 build (smk37.txt on the
# SMK-37's), and a hash of what comes out (the sound, each screen, what it printed: MIDI out too) goes into a list.
#   tests/regress.sh record    the list as it is now: build/regress/baseline.txt
#   tests/regress.sh           the list again, compared with that: the scripts whose output differs, or "SAME"
# For refactors: record before, change the code, run it after. A difference is not wrong by itself, it is a question.
set -e
cd "$(dirname "$0")/.."
OUT=build/regress
mkdir -p "$OUT"
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER='"regress"' -Ifirmware/hal -Ifirmware/src -Ifirmware/gen \
   -o "$OUT/juicy" firmware/src/juicy.c
cc -O2 -Wall -Wno-unused-function -Wno-unused-variable -DDINGHY_HOST -DBOARD_SMK37=1 -DAPP_VER='"regress"' -Ifirmware/hal -Ifirmware/src -Ifirmware/gen \
   -o "$OUT/juicy-smk37" firmware/src/juicy.c

run() {                                         # run <app> <name> [script]: one line per output file, "name file hash"
    dir="$OUT/run/$2"
    mkdir -p "$dir"
    find "$dir" -type f -name 'out*' -exec rm -f {} +
    "$OUT/$1" "$dir/out" ${3:+"$3"} > "$dir/out-printed.txt" 2>&1
    for file in "$dir"/out*; do
        echo "$2 $(basename "$file") $(sha256sum < "$file" | cut -c1-16)"
    done
}

{
    run juicy default
    run juicy-smk37 smk37-default
    for script in tests/scripts/*.txt; do
        name=$(basename "$script" .txt)
        if [ "$name" = smk37 ]; then run juicy-smk37 "$name" "$script"; else run juicy "$name" "$script"; fi
    done
} > "$OUT/now.txt"

if [ "$1" = record ]; then
    cp "$OUT/now.txt" "$OUT/baseline.txt"
    echo "recorded: $(wc -l < "$OUT/baseline.txt") outputs of $(cut -d' ' -f1 "$OUT/baseline.txt" | sort -u | wc -l) runs"
elif [ ! -f "$OUT/baseline.txt" ]; then
    echo "no baseline yet: tests/regress.sh record"; exit 1
elif cmp -s "$OUT/baseline.txt" "$OUT/now.txt"; then
    echo "SAME: $(wc -l < "$OUT/now.txt") outputs of $(cut -d' ' -f1 "$OUT/now.txt" | sort -u | wc -l) runs"
else
    echo "DIFFERENT (baseline, now):"
    diff "$OUT/baseline.txt" "$OUT/now.txt" || true
    exit 1
fi
