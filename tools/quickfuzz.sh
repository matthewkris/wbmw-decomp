#!/bin/sh
# tools/quickfuzz.sh CASES NAMES...: fuzz each routine from the castle state with coverage,
# printing the result and any instruction of its range the original never ran (a quick look
# before tools/accept.sh)
n=$1; shift
f=$(od -A n -t u4 -j 8 -N 4 work/castle.state | tr -d " ")
ROM=$(ls rom/*.md | head -1)
for a in "$@"; do
    timeout 600 work/harness/harness "$ROM" --load-state work/castle.state --frames $((f + 1)) --port shadow \
        --fuzz "$a" "$n" --coverage "work/runs/qf_$a.txt" --quiet > "work/runs/qf_$a.log" 2>&1
    echo "$a: exit $? $(grep -h -E "^fuzz|did not|m68k_rt" "work/runs/qf_$a.log" | tr '\n' ' ')"
    ${PYTHON:-python3} -I tools/coverage.py work/map.json "work/runs/qf_$a.txt" 2>/dev/null | awk -v n="$a" '/^[^ ]/ { on = $1 == n } on' | head -12
done
