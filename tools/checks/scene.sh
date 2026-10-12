#!/bin/sh
# scene.sh STATE KIND FRAMES [harness options...]: cast special action KIND (0-12) from a save
# state, in shadow mode, and run FRAMES frames. The state's spell slots get KIND and five
# uses; inputs/cast.txt presses A + left, A + right and A + left again (frames relative to the
# state). Extra options are passed to the harness, e.g. --poke-at FRAME ADDR=HEX.
# Output in work/magic/: log_TAG.txt, cov_TAG.txt and a frame every 20 in png_TAG/, where TAG
# is STATE_KIND plus $TAG if set. Prints the exit code, the total and the number of reports.
cd "$(dirname "$0")/../.." || exit 1
R=$(ls rom/*.md | head -1)
st=$1; k=$2; n=$3; shift 3
b=$(sh tools/checks/frame.sh "work/$st.state")
kk=$(printf %02X "$k"); cnt=$(printf %X $((0x95B8 + k))); tag=${st}_$k${TAG}
mkdir -p work/magic/png_$tag
awk -v b="$b" '{ $1 = $1 + b; print }' tools/checks/inputs/cast.txt > "work/magic/in_$tag.txt"
timeout 1200 ./work/harness/harness.exe "$R" --load-state "work/$st.state" --frames $((b + n)) --input "work/magic/in_$tag.txt" --quiet \
  --port shadow --coverage "work/magic/cov_$tag.txt" --out "work/magic/png_$tag" --png-every 20 \
  --poke FF9F10=$kk --poke FF9F11=$kk --poke FF$cnt=05 --poke FF9F06=00 "$@" > "work/magic/log_$tag.txt" 2>&1
echo "$tag exit=$? $(grep -E '  all:' "work/magic/log_$tag.txt") $(grep -c '^MISMATCH\|^UNFINISHED\|CONTRACT' "work/magic/log_$tag.txt") reports"
