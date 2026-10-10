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

This builds two programs in `work/harness/` (git-ignored, like everything in `work/`):
`harness.exe`, headless, and `play.exe`, the same machine in a window. `play` needs SDL2
(`pacman -S mingw-w64-ucrt-x86_64-SDL2`); the build copies `SDL2.dll` next to it.

## Play

    work/harness/play ROM [--scale N] [--save FILE]

| Key | Pad | Mega Drive |
|---|---|---|
| arrows | D-pad / left stick | D-pad |
| Z | X | A |
| X | A | B |
| C | B | C |
| Enter | Start | Start |

Esc quits, P pauses, Tab (held) runs as fast as possible, F12 writes a screenshot to the
current directory, F5 resets. The save EEPROM is read from FILE at start and written back on
exit (default `play.sav` in the current directory), so CONTINUE works across sessions. There is
no sound.

## Run

    work/harness/harness ROM [--frames N] [--out DIR] [--png-every K] [--ram-every K]
                             [--vdp-every K] [--input FILE] [--pc-trace FRAME] [--quiet]

* `--png-every K`: `DIR/frame_NNNNN.png` every K frames.
* `--ram-every K`: `DIR/ram_NNNNN.bin`, the 64 KB of work RAM (`$FF0000`–`$FFFFFF`).
* `--vdp-every K`: `DIR/vdp_NNNNN.bin`: VRAM (64 KB), CRAM (64 words), VSRAM (40 words),
  registers (32 bytes), words big-endian.
* `--input FILE`: lines `FRAME BUTTONS`; from FRAME on, hold BUTTONS (any of `U D L R A B C S`,
  or `-` for none).
* `--pc-trace FRAME`: print the PC, SP and the long at SP every 61 cycles of one frame.

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
  3-button pad with the TH protocol, Z80 bus request always granted.
* The serial EEPROM at `$200001` (X24C01, 128 bytes, starts blank). The game hangs at `$1C552`
  if a byte is not acknowledged, so this is not optional.
* VDP: all registers, VRAM/CRAM/VSRAM, the control and data ports with auto-increment, 68000
  DMA, VRAM fill and VRAM copy (all completing at once), status (VBlank, HBlank, FIFO empty,
  interrupt pending), HV counter. Scanline renderer: planes A and B with full, cell and line
  horizontal scroll and full or 2-cell vertical scroll, the window, sprites through the link
  list, and the six-layer priority order, H32 and H40.

## What is not

* The Z80 and sound chips. The 68000 never touches the YM2612 or PSG; commands queued at
  Z80 `$1C30` simply accumulate. No audio is produced.
* Shadow/highlight, interlace, sprite limits per line, sprite collision and overflow flags,
  DMA and FIFO timing. None has been seen to matter for this game yet; each is a known gap if a
  frame ever disagrees with real hardware.
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
