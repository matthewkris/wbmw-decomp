# Reference harness

The original 68000 code of the game, running on the Musashi CPU core
(`third_party/musashi`, MIT) against a reimplemented Mega Drive, headless. It is the oracle
for the port: anything rewritten in C is checked against what this produces, frame by frame.

## Build

Needs GCC and GNU make. On the owner's machine they come from MSYS2 (UCRT64). Run from an MSYS2
UCRT64 shell:

    cd harness && make

or from PowerShell:

    $env:MSYSTEM="UCRT64"; $env:CHERE_INVOKING="1"; C:\msys64\usr\bin\bash.exe -lc "cd harness && make"

The build also translates the routines listed in `analysis/translate.json` (and everything they
call) into `work/gen/` with `tools/m68k2c.py`, which needs Python and `work/map.json` (from
`tools/build.sh`). From the MSYS2 shell, pass Python's path:
`make PYTHON=/c/Users/matth/AppData/Local/Programs/Python/Python312/python.exe`.

This builds two programs in `work/harness/` (git-ignored, like everything in `work/`):
`harness.exe`, headless, and `play.exe`, the same machine in a window. `play` needs SDL2
(`pacman -S mingw-w64-ucrt-x86_64-SDL2`); the build copies `SDL2.dll` next to it.

## Play

    work/harness/play ROM [--scale N] [--save FILE] [--state FILE] [--load] [--port replace|shadow]
                          [--coverage FILE]

`--load` starts from the state file instead of power-on, as if F4 were pressed at once (the
state carries the save EEPROM too). F2 then saves back into the same file, so copy a state you
want to keep to its own name first.

`--port replace` plays with the translated C running the game; `--port shadow` plays the
original while checking every translated call against it (mismatches print to the console, and
a report at exit). With `--coverage FILE` the session also records which translated code it
checked; `tools/coverage.py work/map.json FILE ...` merges sessions.

| Key | Pad | Mega Drive |
|---|---|---|
| arrows | D-pad / left stick | D-pad |
| Z | X | A |
| X | A | B |
| C | B | C |
| Enter | Start | Start |

Esc quits, P pauses, Tab (held) runs as fast as possible, F12 writes a screenshot to the
current directory, F5 resets, F2 saves a save state and F4 loads it. F11 or Alt+Enter switches
full screen (`--fullscreen` starts in it). The save EEPROM is read from FILE at start and written back on
exit (default `play.sav` in the current directory), so CONTINUE works across sessions. There is
sound (the Z80 and both sound chips; `--mute` to turn it off).

## Run

    work/harness/harness ROM [--frames N] [--out DIR] [--png-every K] [--ram-every K]
                             [--vdp-every K] [--input FILE] [--pc-trace FRAME] [--quiet]
                             [--save-state-at F FILE] [--load-state FILE] [--port replace|shadow]
                             [--coverage FILE] [--fuzz NAME COUNT]

* `--png-every K`: `DIR/frame_NNNNN.png` every K frames.
* `--ram-every K`: `DIR/ram_NNNNN.bin`, the 64 KB of work RAM (`$FF0000`–`$FFFFFF`).
* `--vdp-every K`: `DIR/vdp_NNNNN.bin`: VRAM (64 KB), CRAM (64 words), VSRAM (40 words),
  registers (32 bytes), words big-endian.
* `--input FILE`: lines `FRAME BUTTONS`; from FRAME on, hold BUTTONS (any of `U D L R A B C S`,
  or `-` for none).
* `--pc-trace FRAME`: print the PC, SP and the long at SP every 61 cycles of one frame.
* `--save-state-at F FILE`: write a save state after frame F. `--load-state FILE`: start from a
  save state instead of power-on; frame numbers (for `--frames`, the dumps and the input file)
  carry on from the frame it was saved at.

With `--port`, the report at the end lists every routine's calls (and mismatches in shadow mode).
It also lists each place where generated code reached code with no translation and handed
control back to the 68000 ("left generated code at X for 68000 code at Y").

In shadow mode, a check that never reaches the point where the C version ended is reported as
`UNFINISHED` after 50 million instructions, and the run goes on. During a check, the VDP status
and HV counter values the C version read are replayed to the original, because C runs in no
emulated time (`docs/PORT.md`, Tasks).

`--call NAME` runs one ported or translated routine once, right after loading a state and
before the first frame, and puts the registers back. With `--save-state-at` it edits states:
after patching the health in a state file, `--call g_008AA2` redraws the hearts.

By game frame (`framelog.c`, the same in the native build): `--frame-log FILE` writes a line
per game frame (counter, hashes of RAM, VRAM, CRAM+VSRAM) when the game waits for VBlank;
`--input-game FILE` keys input to the game's frame counter instead of real frames; `--dump-at N
DIR` writes the memory at game frame N. `tools/framecmp.py A B` compares two logs.

If translated code loops 100 million branches in one call (waiting for time or an interrupt,
which it never gets), the harness prints `STUCK` with the recent branch targets and exits with
status 3, instead of hanging.

Environment variables:

Checks compare memory, the PC and the stack pointer, and the registers a routine's contract
names; any other register left different is watched, and the run reports `CONTRACT` if the
game reads it before writing it. That needs `work/usedef.bin` (`python tools/liveness.py
work/map.json --usedef work/usedef.bin`, after every map change).

* `WBMW_PORT_BREAK=NAME`: self-test. Corrupts the stack pointer in that routine's result, so every
  checked call of it must be reported.
* `WBMW_TRACE=ADDR[,ADDR...]` (hex, any mode): each time the 68000 reaches one of these
  addresses, print the game's frame counter, `d0`-`d3`, `a0` and the return address. For
  example `WBMW_TRACE=366` lists every sound the game queues.
* `WBMW_YIELD_SURVEY=1` (any mode): at the end, list every task resume point and where `Yield`
  returned to. If every line names a scheduler ($4BD8 or $4C8A), tasks only yield from their
  top level.

## Save states

The native build (`native/`) reads and writes the same files.


Taken between frames: the 68000 registers and pending interrupts, RAM, Z80 RAM, the EEPROM and
its protocol state, the pad latch, and all VDP state. About 137 KB. Integers are stored in host
byte order, so a state belongs to the machine that wrote it. Checked exact: a run loaded from a
state saved at frame 2000 matches the straight run in RAM (every 10 frames), VDP state and
screenshots to frame 4000. In the window, F2 saves and F4 loads (`--state FILE`, default
`play.state`).

## Comparing runs

    python tools/cmpruns.py DIR_A DIR_B [--kind ram|vdp|frame] [--all] [--max N]

Compares the dumps two runs share, frame by frame, and reports the first frame that differs:
the differing bytes grouped into runs, each labelled with the entity field and slot or the named
RAM area it falls in (`--kind vdp`: VRAM tile, CRAM line and colour, VSRAM, register; `--kind
frame`: pixel count and bounding box). `--all` also counts every later frame that differs, which
shows whether two runs re-converge. Exit status 1 when they differ. This is the check every
port step goes through: the same input from the same state, one run all 68000, one with a
routine replaced, RAM compared every frame.
* `--port replace|shadow`: run the C routines in `port/` instead of the originals, or check every
  call of them against the originals; see `docs/PORT.md`. `play.exe` takes `--port` too.
* `--coverage FILE` (shadow): record which instructions of each ported routine's original ran
  inside a check; `python tools/coverage.py work/map.json FILE...` reports what never ran.
* `--fuzz NAME COUNT` (shadow): after the frames, call one ported routine COUNT times with random
  inputs, each checked against the original; exit status 1 on any mismatch.

`core.c` (power-on, interrupts, the frame loop) is shared by both programs, so a frame in the
window is the same frame the headless harness produces.

It runs at about seven times real time. To start a game from power-on: Start at frame 480,
Start again at 600 (the menu), then face buttons to page the intro text.

## What is modelled

* 68000 (Musashi, 68000 mode), NTSC timing: 262 lines of 488 cycles, executed in 61-cycle slices
  so the HV counter and HBlank flag move within a line. VBlank interrupt (level 6) at line 224,
  HBlank interrupt (level 4) from the line counter in register 10.
* Memory map: ROM, 64 KB work RAM (mirrored from `$E00000`), Z80 RAM (8 KB, written by the
  driver upload and the sound queue), I/O version register (overseas NTSC), controller 1 as a
  3-button pad with the TH protocol, the Z80's bus request and reset lines.
* The serial EEPROM at `$200001` (X24C01, 128 bytes, starts blank). The game hangs at `$1C552`
  if a byte is not acknowledged, so this is not optional.
* VDP: all registers, VRAM/CRAM/VSRAM, the control and data ports with auto-increment, 68000
  DMA, VRAM fill and VRAM copy (all completing at once), status (VBlank, HBlank, FIFO empty,
  interrupt pending), HV counter. Scanline renderer: planes A and B with full, cell and line
  horizontal scroll and full or 2-cell vertical scroll, the window, sprites through the link
  list, and the six-layer priority order, H32 and H40.

* Sound (`sound.c`, `ym2612.cpp`): the Z80 (`third_party/chips/z80.h`) runs the game's own sound
  driver from Z80 RAM, one scanline (228 cycles) at a time, with its VBlank interrupt, its bank
  window on 68000 memory and the bus request and reset lines. The YM2612 is ymfm
  (`third_party/ymfm`, with its timers counted in chip clocks); the SN76489 is written here.
  Output is stereo at 53,267 Hz (one YM2612 sample), through a DC blocker like the console's
  output capacitor. `play` plays it (`--mute` to turn it off); `--wav FILE` records it headless.
  During a shadow check the sound is held still, like the interrupts, so the Z80's own RAM
  writes do not count as differences. `WBMW_SOUND_LOG=FILE` lists every write to the sound
  chips. Save states are version 2 (`WBMWST02`, with the sound); version-1 states still load,
  with the sound driver restarted.

## What is not

* Shadow/highlight, interlace, sprite collision and overflow flags, DMA and FIFO timing. None
  has been seen to matter for this game yet; each is a known gap if a frame ever disagrees with
  real hardware. (Sprite masking and the per-line sprite limits were in this list until the owner
  saw spider webs drawn over the HUD: the game hides sprites under the HUD's bottom edge with
  masking sprites at x = 0.)
* Controller 2 always reads as nothing pressed.

## Checked so far

* Power-on to the SEGA logo, the title, the menu (which reads the EEPROM to decide whether to
  offer CONTINUE) and into the first room of a new game.
* The first room (area 0, room 0) against `tools/rooms.py`, harness frame 3300 of a new game
  aligned 48 lines down: all four CRAM palette lines equal the records the room script names
  (0, 1, 103, 3) and the backdrop equals the script's colour word. The ground strip is 100%
  identical; the tree area 89%, the sky 84%, the whole room 66%. Known causes: the HUD window
  covers the top of the room, sprites (Shion, furniture), and the game opening inside the house
  (front wall cut away, interior shown), which is game state the room renderer does not apply.
  The rest of the tree and sky difference is not explained yet; plane B scrolling separately
  from plane A is the first thing to check.
