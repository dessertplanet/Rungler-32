#!/bin/sh
# Build Rungler-32 and package it as a Mother-32 firmware SysEx file.
# usage: tools/build.sh            (needs TI C2000 Code Generation Tools; set CGT if not in ~/ti)
#        EXTRA_CFLAGS="-DM32_NO_EEPROM_CAL" tools/build.sh
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
CGT=${CGT:-$HOME/ti/ti-cgt-c2000_22.6.1.LTS}
B=$ROOT/build
mkdir -p "$B"
# the TI tools for macOS are x86-64 binaries: run them under Rosetta on Apple silicon
run() { if [ "$(uname -s)" = Darwin ] && [ "$(uname -m)" = arm64 ]; then arch -x86_64 "$@"; else "$@"; fi; }
run "$CGT/bin/cl2000" -v28 -ml -mt --abi=coffabi --opt_level=2 --opt_for_speed=2 ${EXTRA_CFLAGS:-} \
    --diag_warning=225 --display_error_number -I"$ROOT/firmware" -I"$CGT/include" \
    --obj_directory="$B" --asm_directory="$B" "$ROOT/firmware/rungler32/main.cpp"
run "$CGT/bin/cl2000" -z "$B/main.obj" "$ROOT/firmware/link/flash.cmd" -l"$CGT/lib/rts2800_ml.lib" \
    --rom_model --stack_size=0x280 --heap_size=0x20 -o "$B/rungler32.out" -m "$B/rungler32.map" --warn_sections
python3 "$ROOT/tools/mkfwimg.py" "$B" rungler32
