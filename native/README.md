# The native build

`wbmw` runs Wonder Boy in Monster World as C, with no 68000 emulator: every instruction of the
game is translated (`tools/m68k2c.py` with `analysis/native.json`, into `work/gen_native`) or
hand-ported (`port/`). `wbmw.c` is the host around it: the memory map, save EEPROM, controller and
VDP come from the harness (`harness/bus.c`, `harness/vdp.c`). Sound is the harness's too (`harness/sound.c`: the Z80 running the game's driver, ymfm's YM2612, an SN76489). The ROM is
still needed at run time, for the game's data (graphics, maps, text).

## Build

From `native/`, in an MSYS2 UCRT64 shell (after `make` at the repo root has made `work/map.json`):

    make PYTHON=/c/Users/<you>/AppData/Local/Programs/Python/Python312/python.exe

This makes `work/native/wbmw.exe` (and copies `SDL2.dll` next to it).

## Run

    work/native/wbmw ROM [--scale N] [--save FILE] [--fullscreen] [--integer-scale] [--fps] [--mute] [--timing]
                         [--state FILE] [--load]

plays in a window: arrows, Z = A, X = B, C = C, Enter = Start, Esc opens the menu; a controller
works too.
The window is sized for the game's 256-pixel width at the largest scale up to 3 that fits the
screen (`--scale N` to choose); it can be resized, and the picture keeps its proportions.
The save EEPROM is read from FILE at start and written at exit (default `wbmw.sav`).

* **F11** or **Alt+Enter** toggles full screen (borderless, at the desktop's resolution, picture
  centred with its proportions); `--fullscreen` starts in it.
* `--integer-scale` scales only by whole multiples, so every pixel is the same size (a slightly
  smaller picture, with a wider border).
* **F3** toggles a frame-rate display in the top right corner (`--fps` starts with it on); the
  window title always shows the rate. The game itself always runs 60 frames a second of game
  time; the display tells whether the PC keeps up.
* **Esc** opens the menu over the picture: **Show FPS** (on/off) and **Exit game**, which asks
  "Exit the game?" (No first). Up/Down choose, Enter (or Z, Space) selects, Esc goes back or
  resumes; on a controller the D-pad, A, and B or Start. Closing the window asks the same
  question. The game is frozen while the menu is open (no frames, no sound), and buttons still
  held when it closes reach the game only after they are released.
* Testing the menu without a person: `WBMW_MENU_KEYS="escape return down ..."` presses those keys
  (SDL key names), one per frame after 120 frames; `WBMW_MENU_SHOTS=DIR` writes each screen of
  the menu to `DIR/menu_NN.png`.

    work/native/wbmw ROM --frames N [--png-every K] [--out DIR] [--input FILE] [--wav FILE]

runs headless for N frames, writing `DIR/frame_NNNNN.png` every K frames (and the sound to a WAV
file with `--wav`), with the harness's
input script format, for comparison with `work/harness/harness`.

## Comparing with the original

Both this program and the harness take `--frame-log FILE`, `--input-game FILE` and `--dump-at N
DIR` (`harness/framelog.c`); `tools/framecmp.py` compares two frame logs by game frame.

## Save states

    work/native/wbmw ROM [--state FILE] [--load] ...          F2 saves to FILE, F4 loads it
    work/native/wbmw ROM --load-state FILE | --save-state-at N FILE ...   (headless too)

The format is the harness's own (`WBMWST02`, `harness/core.c`): states move freely between
`play.exe` and `wbmw.exe`, including all the earlier ones (`WBMWST01`). The default file is
`wbmw.state`. A state is taken only where the game waits for VBlank, called from the frame loop.
The harness's states stop there too (between frames). F2 saves at the next such point, normally
the same frame. A loaded state resumes by starting the translated code at its PC; every
instruction of that wait is an entry. With `--timing`, a state loads at line 0, as the harness's
next frame would, so a run from a state matches the harness from the same state exactly.

## Exact timing (`--timing`)

By default the game's code takes no time: it never slows down, and the raster always reads
line 0. With `--timing` the native build counts the 68000 cycles the original would spend and
runs scanlines, interrupts and sound exactly as the harness runs them on Musashi. It is then
identical to the original frame for frame, slowdowns and sound included (`docs/PORT.md`,
"Exact timing"). Both modes use the same program.

## How time works

Translated code takes no time of its own. The game runs until it waits for the VBlank interrupt
(the poll points, `$3DC` and `$C262`); there the host draws the whole frame, shows it, waits for
the next 60 Hz tick, reads the controller and runs the game's interrupt handler (`$4B5C`). The
original drops frames when the 68000 cannot finish a frame's work in time; the native build
never does, so it runs ahead of the original in real frames while playing the same game (see
`docs/PORT.md`, "The native build"). Raster timing (the time-sliced scheduler, `YieldIfLate`)
sees line 0 always.
