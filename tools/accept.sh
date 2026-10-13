#!/bin/sh
# Acceptance runs for newly ported routines (docs/PORT.md, "How a ported routine is accepted"):
#   fuzzing each routine from several states with coverage, the standard runs and every save
#   state in work/ in shadow mode, the coverage report, the self-test (WBMW_PORT_BREAK) and
#   replace-mode runs. Prints one summary; the logs stay in the output directory.
#
#   tools/accept.sh OUTDIR NAME [NAME ...]
#
# Environment: PYTHON (python3), LIMIT (seconds per harness run, 1200), JOBS (fuzz runs at once, 12), CASES (fuzz cases per routine and state, 5000), FUZZ_STATES
# (state names in work/, "castle platform after-boss2 boss2 thunder ocarina"), REPLACE_STATES (states
# for replace mode, "boss2 thunder door-crash castle status shop talk pause ocarina-tunes"), ROM, SAVE (a save EEPROM for
# the title menu's runs, play.sav). Each state's input is
# work/runs/inputs/STATE.txt: made once (3,000 frames of random buttons) and kept, or recorded by play --record (to its "# end"). Run from the
# repository root in Git Bash, after building the harness.
set -u
OUT="$1"; shift
NAMES="$*"
PY="${PYTHON:-python3}"
CASES="${CASES:-5000}"
JOBS="${JOBS:-12}"                # fuzz runs at once: the cores
FUZZ_STATES="${FUZZ_STATES:-castle platform after-boss2 boss2 thunder ocarina}"
REPLACE_STATES="${REPLACE_STATES:-boss2 thunder door-crash castle status shop talk pause ocarina-tunes}"
ROM="${ROM:-rom/Wonder Boy in Monster World (USA, Europe).md}"
SAVE="${SAVE:-play.sav}"
H=work/harness/harness.exe; [ -x "$H" ] || H=work/harness/harness
T="timeout ${LIMIT:-1200}"        # a run that loops in C (no cycle budget) is cut off: exit 124
mkdir -p "$OUT" work/runs/inputs

frame() { od -A n -t u4 -j 8 -N 4 "work/$1.state" | tr -d ' '; }
input() {   # the state's random input, made once
    f="work/runs/inputs/$1.txt"
    [ -f "$f" ] || "$PY" -I -c "
import random, sys
b = int(sys.argv[1]); r = random.Random(b + 3)
f = b + 1
while f < b + 3000:
    print(f, ''.join(sorted(set(r.choice('UDLRABC-') for _ in range(r.randint(1, 2))))).replace('-', '') or '-'); f += r.randint(3, 40)
" "$(frame "$1")" > "$f"
    echo "$f"
}

last() {    # the run's last frame: a recorded session's end ("# end N", play --record), or 3,000 on
    e=$(sed -n 's/^# end \([0-9]*\).*/\1/p' "$2" | tail -1)
    if [ -n "$e" ]; then echo "$e"; else echo $(($1 + 3000)); fi
}

echo "== fuzz ($CASES cases per routine and state: $FUZZ_STATES)"
export H ROM T OUT CASES                # every state and routine at once, JOBS at a time
for s in $FUZZ_STATES; do for n in $NAMES; do echo "$s $n"; done; done |
    xargs -P "$JOBS" -n 2 sh -c 'f=$(od -A n -t u4 -j 8 -N 4 "work/$0.state" | tr -d " ")
        $T "$H" "$ROM" --load-state "work/$0.state" --frames $((f + 1)) --port shadow --fuzz "$1" "$CASES" \
            --coverage "$OUT/fcov_$0_$1.txt" --quiet > "$OUT/fz_$0_$1.log" 2>&1'
grep -h "^fuzz" "$OUT"/fz_*.log | awk '{c += $3; m += $6} END {print "cases", c, "mismatches", m}'

echo "== shadow runs (the standard runs and every state)"
$T "$H" "$ROM" --frames 4000 --input harness/inputs/newgame.txt --port shadow --coverage "$OUT/cov_newgame.txt" --quiet > "$OUT/run_newgame.log" 2>&1 &
$T "$H" "$ROM" --frames 4000 --port shadow --coverage "$OUT/cov_demo.txt" --quiet > "$OUT/run_demo.log" 2>&1 &
title_runs() {  # the title: Start on the Sega logo (the title fades in), and with a saved game (SAVE,
                # default play.sav, if there is one) the menu's continue and new game
    $T "$H" "$ROM" --frames 1200 --input harness/inputs/title-skip.txt --port "$1" ${2:+--coverage "$OUT/cov_title-skip.txt"} --quiet > "$OUT/$3_title-skip.log" 2>&1 &
    [ -f "$SAVE" ] && for t in continue new; do
        $T "$H" "$ROM" --eeprom "$SAVE" --frames 1200 --input "harness/inputs/title-$t.txt" --port "$1" ${2:+--coverage "$OUT/cov_title-$t.txt"} --quiet > "$OUT/$3_title-$t.log" 2>&1 &
    done
}
title_runs shadow cov run
for st in work/*.state; do
    s=$(basename "$st" .state); f=$(frame "$s"); in=$(input "$s")
    $T "$H" "$ROM" --load-state "$st" --frames "$(last "$f" "$in")" --input "$in" --port shadow --coverage "$OUT/cov_$s.txt" --quiet > "$OUT/run_$s.log" 2>&1 &
done
wait
grep -h "all:" "$OUT"/run_*.log | awk '{c += $2; m += $5} END {print "runs", NR, "calls", c, "mismatches", m}'

echo "== problems (logs naming a mismatch, contract, hang or failed call)"
grep -l -E "MISMATCH|CONTRACT|did not|call_68k|STUCK|task_wait|\.c: " "$OUT"/*.log || echo "none"
# a name #defined twice in one file: the later value silently wins for the code after it
for f in port/*.c; do
    d=$(sed -n 's/^#define \([A-Za-z_0-9]*\).*/\1/p' "$f" | sort | uniq -d | tr '\n' ' ')
    [ -n "$d" ] && echo "$f defines twice: $d"
done

echo "== coverage (fuzz and runs)"
"$PY" -I tools/coverage.py work/map.json "$OUT"/fcov_*.txt "$OUT"/cov_*.txt > "$OUT/coverage.txt" 2>&1
show() { awk -v names=" $NAMES " '/^[^ ]/ { on = index(names, " " $1 " ") > 0 } on' "$1"; }
show "$OUT/coverage.txt"
echo "== coverage from play alone"
"$PY" -I tools/coverage.py work/map.json "$OUT"/cov_*.txt > "$OUT/coverage_play.txt" 2>&1
show "$OUT/coverage_play.txt" | grep -v -E "^ |  0/"

echo "== self-test (each must report 100 mismatches)"
s=castle; f=$(frame $s)
for n in $NAMES; do
    WBMW_PORT_BREAK="$n" $T "$H" "$ROM" --load-state "work/$s.state" --frames $((f + 1)) --port shadow --fuzz "$n" 100 --quiet 2>&1 | grep "^fuzz"
done

echo "== replace mode"
$T "$H" "$ROM" --frames 4000 --input harness/inputs/newgame.txt --port replace --quiet > "$OUT/rep_newgame.log" 2>&1 &
title_runs replace "" rep
for s in $REPLACE_STATES; do
    f=$(frame "$s"); in=$(input "$s")
    $T "$H" "$ROM" --load-state "work/$s.state" --frames "$(last "$f" "$in")" --input "$in" --port replace --quiet > "$OUT/rep_$s.log" 2>&1 &
done
wait
for l in "$OUT"/rep_*.log; do echo "$(basename "$l"): $(grep -c -i -E 'error|stuck|did not|call_68k|task_wait|\.c: ' "$l") problems"; done
