# The native build

`wbmw` runs Wonder Boy in Monster World as C, with no 68000 emulator. The game is the
hand-written C of `port/` (verified routine by routine against the original in the harness) and
`machine.c`, the machine core the harness must leave to the 68000: the reset and restart, the
frame loop and its scheduler, the frame wait, the VBlank interrupt, and the original's hangs.
None of the original's code is translated or emulated: the build needs nothing from `work/`.
`wbmw.c` is the host around it: the memory map, save EEPROM, controller and
VDP come from the harness (`harness/bus.c`, `harness/vdp.c`). Sound is the harness's too
(`harness/sound.c`: ymfm's YM2612, an SN76489), driven by the game's sound driver written in C
(`port/sounddrv.c`, a tick each time the YM2612's timer B overflows; `--z80-sound` runs the
original driver on the emulated Z80 instead; `docs/SOUND.md`). The game's data
(graphics, maps, text, the sound driver) comes from `assets/game.pak`, which the build makes from
the ROM, or from the ROM itself when there is no pack (`docs/EXTRACT.md`).

## Build

From `native/`, in an MSYS2 UCRT64 shell:

    make

This makes `work/native/wbmw.exe` (and copies `SDL2.dll` next to it), then runs
`tools/makepack.sh`, which extracts the ROM in `rom/` into `assets/` and packs it into
`assets/game.pak` when they are missing or out of date (it needs Python 3; without it, or without
a ROM, it says so and the game reads the ROM).

## Run

    work/native/wbmw [ROM] [--pack FILE] [--windowed] [--widescreen] [--talk-zoom] [--enemy-health]
                           [--scale N] [--save FILE] [--integer-scale] [--fps] [--mute]
                           [--state FILE] [--load] [--config FILE] [--z80-sound]

plays, full screen unless `--windowed` is given. Without ROM, it plays from `assets/game.pak` (or
`game.pak`) in the current folder, next to the program, or in the repository (`../../assets/` from
`work/native/`); without a pack, it looks for `rom/Wonder Boy in Monster World (USA, Europe).md`
in the same places. A ROM must be 786,432 bytes; a SHA-1 other than the expected dump's gives a
warning. Errors are also shown in a message box. Keys: arrows, Z = A, X = B, C = C, Enter = Start, Esc opens the menu; a controller
works too.
The window is sized for the game's 256-pixel width at the largest scale up to 3 that fits the
screen (`--scale N` to choose); it can be resized, and the picture keeps its proportions.
The save EEPROM is read from FILE at start and written at exit (default `wbmw.sav`).

* **F11** or **Alt+Enter** toggles full screen (borderless, at the desktop's resolution, picture
  centred with its proportions), where the game starts; `--windowed` starts in a window.
* `--integer-scale` scales only by whole multiples, so every pixel is the same size (a slightly
  smaller picture, with a wider border).
* **F3** toggles a frame-rate display in the top right corner (`--fps` starts with it on); the
  window title always shows the rate. The game itself always runs 60 frames a second of game
  time; the display tells whether the PC keeps up.
* **Esc** opens the menu over the picture: **Show FPS**, **Full screen**, **Whole pixels**,
  **Widescreen**, **Talk zoom** and **Enemy health** (on/off), **Volume**, **Music** and
  **Effects** (0-100 in tens: left and right, or Enter to step up; Volume 0 is the sound off: an
  old `mute=1` in `wbmw.ini` becomes it), **Controls**, and **Exit game**, which asks "Exit the game?" (No first).
  Up/Down choose, Enter (or Z, Space) selects, Esc goes back or resumes. On a controller,
  **Back/Select** (View, Share/Create, Minus) opens it, as the Genesis pad has no such button and
  Start stays the game's (the item screen); in the menu the D-pad (or left stick) chooses, A
  selects, and B, Start or Back/Select go back. The menu's own keys never change.
* **Controls** binds a key and a controller input to each of the game's buttons and to quick save
  and quick load (F2 and F4 by default; nothing on the controller). Enter (A) on a cell waits five
  seconds for the new key, or for a button, stick direction or trigger; one used by another action
  swaps with it; Delete (Y) clears a cell. Esc, Back/Select, F11, F3 and Home cannot be bound.
  Buttons are by position (SDL's layout, with `SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS` off, so a
  Nintendo pad is read like the others). **Left stick as D-pad** makes the stick move the player too.
* **Widescreen** (`--widescreen`, also headless, for pictures): 400 pixels wide, the game drawn in
  the extra 72 at each side in the rooms `wide_room()` allows (121 of 170), black sides elsewhere,
  with the one-row HUD in every room and gold as a rising number (`docs/WIDESCREEN.md`). With it off, nothing changes: the build is the original's frame for
  frame.
* **Enemy health** (`--enemy-health`, also headless, for pictures): the damage of each hit as a
  number rising from the enemy, and a bar of its health over it (`docs/ENEMYHEALTH.md`).
* `--verify-graphics DIR`: decodes the 768 graphics blocks with the port's `DecodeGfxBlock` and
  compares them with `DIR/graphics/block_NNN.bin` from `tools/extract.py` (`tools/rebuild.py`
  runs it; `docs/EXTRACT.md`). `--unpack-list REQ OUT` likewise unpacks the rooms' block tables
  and layer maps listed in REQ with the port's `UnpackBlocks` and `UnpackLayerMap`, for
  `rebuild.py` to compare (and `OUT.reads`, the ROM bytes they read). `--rom-reads FILE` marks every ROM byte the game reads into FILE
  (added to what it holds), for `rebuild.py --reads FILE`.
* The game's data comes from `assets/game.pak`, which the build makes from the ROM
  (`tools/makepack.sh`; `docs/EXTRACT.md`, "The game reads the files"), unless a ROM is named on
  the command line; without a pack it reads the ROM. `--pack FILE` names a pack and reports at
  exit whether the game read any byte the files do not hold (a default pack reports only that).
* **Talk zoom** (`--talk-zoom`): during a conversation with an ordinary character, the host zooms
  the scene in on the two, the HUD and the box slid up (`docs/TALKZOOM.md`). For testing it,
  `--input FILE` works in a window too, and `WBMW_SHOT_EVERY=N WBMW_SHOT_EVERY_DIR=DIR` writes the
  window's own picture every N frames.
* The volumes (`volume_frame`): a gain for each sound chip channel and for the mix (`harness/sound.c`,
  `sound_gain`, `sound_master`; the YM2612's channels weighted in `harness/ym2612.cpp`, which
  compiles ymfm's OPN code itself to reach its FM engine). Each frame the driver's voices are read
  from the Z80's RAM to tell the music's from the effects' (`docs/SOUND.md`). All at 100 (the
  default, and always in headless runs unless `--volume`, `--music-volume` or `--effects-volume`
  are given) the sound is the original's, sample for sample.
* `--play-sound N`: queue sound N (hex: a song $00-$17, $54, $55, or an effect) ten frames into
  the run, through the game's own `QueueSound`, to hear or log it (`docs/SOUND.md`).
* The settings are kept in `wbmw.ini` next to the program (`--config FILE` for another), written
  when the menu closes or the game exits after a change. The command line overrides a setting for
  one run without changing the file; headless runs (`--frames`) do not read it.
* Quick load happens at the frame loop's own frame wait, like a save: only there is the C on the
  main stack (a wait inside a task pass is on that pass's fiber). The jump back to `main` is GCC's
  `__builtin_longjmp`: MinGW's `longjmp` unwinds the Windows way and fails once fibers are in use
  (it crashed with STATUS_BAD_STACK). A message ("STATE SAVED", "STATE LOADED", "NO STATE TO
  LOAD") shows at the bottom of the picture. Closing the window asks the same
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
`--frame-log-skip LO-HI` (hex RAM addresses, repeatable) leaves more of RAM out of the hash, for
runs that differ only in scratch memory (the pack's decoders write none).

## Save states

    work/native/wbmw ROM [--state FILE] [--load] ...          F2 saves to FILE, F4 loads it
    work/native/wbmw ROM --load-state FILE | --save-state-at N FILE ...   (headless too)

The format is the harness's own (`WBMWST02`, `harness/core.c`): states move freely between
`play.exe` and `wbmw.exe`, including all the earlier ones (`WBMWST01`). The default file is
`wbmw.state`, the first of the menu's four slots; the others are beside it with `-2`, `-3`, `-4`
in their names, each slot with its picture at half size (the state's name with `.png`, written by
`harness/png.c` and read back by its `png_read_rgb`). F2 and F4 use the current slot. A state is taken only where the game waits for VBlank, called from the frame loop.
The harness's states stop there too (between frames). F2 saves at the next such point, normally
the same frame. A loaded state resumes in `machine.c`'s frame wait at its PC. The tasks then
carry on from the addresses they keep in RAM, as the original's do: every waiting hand port can
start at each of its resume and continuation addresses (`docs/PORT.md`, batch N3), since the
coroutines it waits on cannot be saved.

## Exact timing (`--timing`): off for now

`--timing` counted the 68000 cycles of the translated code, which made the native build
identical to the original frame for frame, slowdowns included (`docs/PORT.md`, "Exact timing").
The hand-written C has no cycle counts, so the option is off (it says so) until the C has a
timing model of its own; the host's code for it (scanline slices, interrupts by time) is kept
for that. Without it the native build plays like the harness's replace mode: the
same game, with small differences where the original's timing shows (fades, the time-sliced
user tasks).

## How time works

The game's code takes no time of its own. It runs until it waits for the VBlank interrupt
(`machine.c`'s frame wait, and the inn's sleep); there the host draws the whole frame, shows it, waits for
the next 60 Hz tick, reads the controller and runs the game's interrupt handler (`$4B5C`). The
original drops frames when the 68000 cannot finish a frame's work in time; the native build
never does, so it runs ahead of the original in real frames while playing the same game (see
`docs/PORT.md`, "The native build"). Raster timing (the time-sliced scheduler, `YieldIfLate`)
sees line 0 always.
