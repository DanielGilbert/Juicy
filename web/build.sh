#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build firmware/src/juicy.c for the browser (tests/web.c): build/web/juicy.wasm. Then: node web/serve.js
set -e
cd "$(dirname "$0")/.."
mkdir -p build/web
clang --target=wasm32 -O2 -ffreestanding -fno-builtin -nostdlib -Wall -Wno-unused-function \
    -DDINGHY_HOST -DHOST_WEB -DAPP_VER="\"${APP_VER:-dev}\"" -Ifirmware/hal -Ifirmware/src -Ifirmware/gen \
    -Wl,--no-entry -o build/web/juicy.wasm firmware/src/juicy.c
echo "web -> build/web/juicy.wasm"
