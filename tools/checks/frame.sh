#!/bin/sh
# frame.sh FILE.state: the frame number a save state was taken at (the harness's --frames and
# --input frames continue from it).
od -An -t d4 -j 8 -N 4 "$1" | tr -d ' '
