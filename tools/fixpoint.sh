#!/bin/sh
# Usage: tools/fixpoint.sh ROM
# Runs the tracer until jump-table boundaries stop changing, then writes
# work/map.json (code/data map) and work/code_000200_01D000.lst (listing).
set -e
R="$1"; EX=analysis/hints.json; mkdir -p work
echo '[]' > work/bounds.json
for i in 1 2 3 4 5 6 7 8; do
  python3 -I tools/trace.py "$R" --extra "$EX" --bounds work/bounds.json --out work/map.json > /dev/null
  if python3 -I -c "
import json,sys
m=json.load(open('work/map.json')); old=json.load(open('work/bounds.json'))
b=sorted(set(old)|set(x[0] for x in m['bad'] if 'table data' in x[1])|set(int(k,16) for k in m['refs'])|set(int(k,16) for k in m['auto_tables'])|set(int(k,16) for k in m['tables']))
json.dump(b,open('work/bounds.json','w')); sys.exit(0 if b==old else 1)"; then break; fi
done
python3 -I tools/trace.py "$R" --extra "$EX" --bounds work/bounds.json --orphans -v --out work/map.json
python3 -I tools/listing.py "$R" work/map.json work/code_000200_01D000.lst
