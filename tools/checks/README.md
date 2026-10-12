# Checks

The scripts used to accept a ported routine (`docs/PORT.md`, "How a ported routine is
accepted"). Run them from Git Bash or MSYS2 after building `harness/`; they find the repository
from their own location and the ROM in `rom/`. Save states are not in the repository (they hold
data from the ROM): keep them in `work/` (`platform`, `demo1500`, `castle`, `after-boss`,
`after-boss2`, `beforedeath`, `ocarina` are the ones the docs mention).

* `std.sh newgame | demo | plat K`: the standard shadow runs (newgame, demo, plat0-7).
  All ten together: `for k in newgame demo; do sh tools/checks/std.sh $k & done; for i in 0 1 2 3 4 5 6 7; do sh tools/checks/std.sh plat $i & done; wait`
* `fuzz.sh STATE FRAME ROUTINE COUNT`: fuzzing one routine; usually 20,000 cases from
  `platform 8176`, `demo1500 1501` and `castle 29684`. Time 50 cases first: a generator that
  leaves the game in a state it never reaches can make the original loop forever.
* `scene.sh STATE KIND FRAMES [options]`: cast a special action from a state (the spell
  checks; also a convenient "play these frames from this state" in shadow mode).
* `frame.sh FILE.state`: the frame a state was saved at.
* `covaddr.py RANGES FILES...`: coverage by address: which instructions of the original in the
  ranges (`12A70-133B2,1090C-10A1E`, hex, end exclusive) never ran inside a check, across the
  coverage files. Use files from the current build only. (`tools/coverage.py` reports per
  routine instead.)

`inputs/`: the button scripts (`FRAME BUTTONS` lines, see `harness/main.c`): `newgame.txt` and
`in0-7.txt` for the standard runs, `cast.txt` for `scene.sh` (frames relative to the state),
`swing_castle_game.txt` (by game frame, for `--input-game` from `castle.state`: casts and
sword swings, used to compare replace mode with the original by game frame with
`--frame-log` and `tools/framecmp.py`).
