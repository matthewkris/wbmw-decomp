# The native build

`wbmw` runs Wonder Boy in Monster World as C, with no 68000 emulator: every instruction of the
game is translated (`tools/m68k2c.py` with `analysis/native.json`, into `work/gen_native`) or
hand-ported (`port/`). `wbmw.c` is the host around it: the memory map, save EEPROM, controller and
VDP come from the harness (`harness/bus.c`, `harness/vdp.c`). There is no sound yet. The ROM is
still needed at run time, for the game's data (graphics, maps, text).

## Build

From `native/`, in an MSYS2 UCRT64 shell (after `make` at the repo root has made `work/map.json`):

    make PYTHON=/c/Users/<you>/AppData/Local/Programs/Python/Python312/python.exe

This makes `work/native/wbmw.exe` (and copies `SDL2.dll` next to it).

## Run

    work/native/wbmw ROM [--scale N] [--save FILE]

plays in a window: arrows, Z = A, X = B, C = C, Enter = Start, Esc quits; a controller works too.
The window is sized for the game's 256-pixel width at the largest scale up to 3 that fits the
screen (`--scale N` to choose); it can be resized, and the picture keeps its proportions.
The save EEPROM is read from FILE at start and written at exit (default `wbmw.sav`).

    work/native/wbmw ROM --frames N [--png-every K] [--out DIR] [--input FILE]

runs headless for N frames, writing `DIR/frame_NNNNN.png` every K frames, with the harness's
input script format, for comparison with `work/harness/harness`.

## How time works

Translated code takes no time of its own. The game runs until it waits for the VBlank interrupt
(the poll points, `$3DC` and `$C262`); there the host draws the whole frame, shows it, waits for
the next 60 Hz tick, reads the controller and runs the game's interrupt handler (`$4B5C`). The
original drops frames when the 68000 cannot finish a frame's work in time; the native build
never does, so it runs ahead of the original in real frames while playing the same game (see
`docs/PORT.md`, "The native build"). Raster timing (the time-sliced scheduler, `YieldIfLate`)
sees line 0 always.
