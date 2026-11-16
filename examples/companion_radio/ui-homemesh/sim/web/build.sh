#!/bin/sh
# Builds homemesh.wasm (the real HomeMesh UI) and build/homemesh.html, a single
# page that runs it in any browser.
# Needs zig (pip install ziglang) and an LVGL v9.4.0 source tree.
#   ./build.sh /path/to/lvgl
set -e
LVGL=${1:?path to lvgl v9.4.0}
cd "$(dirname "$0")"
ZIG="python3 -m ziglang"
OUT=build
mkdir -p $OUT/obj
FLAGS="-target wasm32-wasi -O2 -I.. -I$LVGL -DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE"

objs=""
for f in $(find "$LVGL/src" -name '*.c') ../../fonts/*.c; do
  o=$OUT/obj/$(echo "$f" | sed 's|[/.]|_|g').o
  [ "$o" -nt "$f" ] || $ZIG cc $FLAGS -c "$f" -o "$o"
  objs="$objs $o"
done
$ZIG c++ $FLAGS -std=c++17 -I../../../../../src -c ../../HomeMeshUI.cpp -o $OUT/obj/ui.o
$ZIG c++ $FLAGS -std=c++17 -I../../../../../src -c wasm_main.cpp -o $OUT/obj/main.o
$ZIG c++ -target wasm32-wasi -O2 -mexec-model=reactor -s -o $OUT/homemesh.wasm $objs $OUT/obj/ui.o $OUT/obj/main.o
ls -la $OUT/homemesh.wasm
# single self-contained page with the wasm inlined
python3 - "$OUT/homemesh.wasm" shell.html "$OUT/homemesh.html" <<'PY'
import base64, sys
wasm, shell, out = sys.argv[1:]
b64 = base64.b64encode(open(wasm, 'rb').read()).decode()
open(out, 'w').write(open(shell).read().replace('__WASM_BASE64__', b64))
PY
ls -la $OUT/homemesh.html
