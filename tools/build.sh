#!/bin/sh
# Usage: tools/build.sh [ROM]
# Traces the ROM, generates work/src/wbmw.asm, assembles it to work/wbmw.bin with vasm and fails
# unless the result is byte-identical to the original. PYTHON and VASM override the tool names.
set -e
PY="${PYTHON:-python3}"; VASM="${VASM:-vasmm68k_mot}"
R="${1:-rom/Wonder Boy in Monster World (USA, Europe).md}"
WANT=87a968f773c7e807e647c0737132457b06b78276
[ -f "$R" ] || { echo "missing ROM: $R" >&2; exit 1; }
mkdir -p work
PYTHON="$PY" sh tools/fixpoint.sh "$R" > work/fixpoint.log
tail -n 2 work/fixpoint.log | head -n 1
mkdir -p work/src
"$PY" -I tools/mkasm.py "$R" work/map.json work/src/wbmw.asm --lines work/src/lines.json
"$VASM" -quiet -Fbin -m68000 -no-opt -nowarn=2028 -o work/wbmw.bin work/src/wbmw.asm
GOT=$("$PY" -I -c "import hashlib,sys; print(hashlib.sha1(open(sys.argv[1],'rb').read()).hexdigest())" work/wbmw.bin)
if [ "$GOT" = "$WANT" ]; then echo "work/wbmw.bin: SHA-1 matches ($GOT)"; else
  echo "work/wbmw.bin: SHA-1 $GOT, expected $WANT" >&2
  echo "run tools/checkasm.py to find the lines that differ" >&2; exit 1; fi
