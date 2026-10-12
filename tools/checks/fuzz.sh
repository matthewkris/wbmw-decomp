#!/bin/sh
# fuzz.sh STATE FRAME ROUTINE COUNT [PREFIX]: fuzz one ported routine from a save state.
#   STATE   a save state name in work/ (platform, demo1500, castle, ...)
#   FRAME   the state's frame + 1 (the harness runs that frame first; see frame.sh)
# COUNT random calls of ROUTINE (its name in port/registry.c), each checked against the
# original; the generators are in harness/hybrid.c. Coverage goes to work/plat/PREFIX_STATE_ROUTINE.txt
# (PREFIX defaults to cov_fuzz), the log to work/plat/log_STATE_ROUTINE.txt.
# Usual: the three states platform 8176, demo1500 1501 and castle 29684, 20,000 cases each.
cd "$(dirname "$0")/../.." || exit 1
mkdir -p work/plat
R=$(ls rom/*.md | head -1)
P=${5:-cov_fuzz}
timeout 1500 ./work/harness/harness.exe "$R" --load-state "work/$1.state" --frames "$2" --quiet --port shadow \
    --coverage "work/plat/${P}_$1_$3.txt" --fuzz "$3" "$4" > "work/plat/log_$1_$3.txt" 2>&1
echo "$3 $1 exit=$? $(grep -E '^fuzz' "work/plat/log_$1_$3.txt" | tr '\n' ' ')"
