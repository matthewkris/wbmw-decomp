#!/bin/sh
# std.sh KIND [K]: one of the standard shadow runs, with coverage, into work/t32/.
#   newgame      4000 frames from power-on with inputs/newgame.txt
#   demo         4000 frames of the attract demo
#   plat K       from work/platform.state with inputs/inK.txt (K = 0..7), to frame 9375
# Every routine in port/ is checked against the original on every call. Prints the exit code
# and any line that is not "0 mismatches". Needs work/harness/harness.exe and the ROM in rom/.
cd "$(dirname "$0")/../.." || exit 1
mkdir -p work/t32 work/off
R=$(ls rom/*.md | head -1)
H=./work/harness/harness.exe
C=tools/checks/inputs
case "$1" in
newgame) $H "$R" --frames 4000 --out work/off --input $C/newgame.txt --quiet --port shadow --coverage work/t32/cov_newgame.txt > work/t32/log_newgame.txt 2>&1 ;;
demo)    $H "$R" --frames 4000 --out work/off --quiet --port shadow --coverage work/t32/cov_demo.txt > work/t32/log_demo.txt 2>&1 ;;
plat)    $H "$R" --load-state work/platform.state --frames 9375 --input "$C/in$2.txt" --quiet --port shadow --coverage "work/t32/cov_plat$2.txt" > "work/t32/log_plat$2.txt" 2>&1 ;;
*) echo "usage: std.sh newgame | demo | plat K"; exit 2 ;;
esac
echo "$1$2 exit=$? $(grep -E '  all:|mismatch' work/t32/log_$1$2.txt | grep -v 'mismatches 0$' | head -5 | tr '\n' ' ')"
