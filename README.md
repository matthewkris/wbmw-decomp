# Wonder Boy in Monster World (Genesis/Mega Drive) — C port

A port of the Sega Genesis/Mega Drive game *Wonder Boy in Monster World* to C. It runs natively
with SDL2. The game's 68000 program is not emulated: every routine was rewritten in C, then
checked against the original in a reference harness (shadow runs, fuzzing, coverage). The game
is playable from start to finish.

One part is still emulated: sound. On the Genesis/Mega Drive, the music and sound effects are
played by a small program on the console's second CPU, a Z80. The port loads that driver from your
ROM and runs it on an emulated Z80, which drives emulated sound chips (ymfm for the YM2612, plus
the SN76489).

## Added features

On top of the original game. They are all on by default; each can be turned off in the menu
(Esc), and the setting is kept.

* **Pause menu** (Esc, or Back/Select on a controller) with a confirmation before exiting.
* **Custom controls**: set a key and a controller button, stick direction or trigger for each game
  button, and for quick save and quick load.
* **Settings saved** between sessions (`wbmw.ini`): full screen, whole-pixel scaling, sound, frame
  rate display, controls.
* **Quick save and load** of the whole game state, with an on-screen confirmation.
* **Full screen** by default, with window mode and whole-pixel scaling.
* **Frame rate display.**
* **Enemy health bars and damage numbers**: when you hit an enemy, the damage rises from it as a
  number (gold for the blow that defeats it), and a small bar over it shows the health it has
  left for two seconds: green, then yellow below two thirds, red below one third. Bosses have
  them too. They stay under the text boxes and the status bar. See `docs/ENEMYHEALTH.md`.
* **Talk zoom (experimental)**: during a conversation with an ordinary character, the view eases
  in on Wonder Boy and the one he is talking to, with the text box kept at its own size. See
  `docs/TALKZOOM.md`.
* **Widescreen (experimental)**: a 16:9 picture. Most rooms fill it (115 of the 170, those
  whose maps are wide enough); one-screen rooms, houses and shops keep the original picture with
  black sides. See
  `docs/WIDESCREEN.md`.

## No ROM data is in this repository

**You need your own dump of the cartridge.** This repository contains only source code, tools and
notes. It contains no ROM, no graphics, maps, text, music or sound, and no file made from the ROM:

* `rom/` (where you put your dump), `work/` (everything built from it) and `assets/` (the data
  `tools/extract.py` writes out of it) are listed in `.gitignore`. So are save files (`*.sav`)
  and save states (`*.state`).
* When the game runs, it reads all of its data (graphics, maps, text, music, sound, and the Z80
  sound driver) from your ROM file. The C code holds only the game's logic, with a few small numeric tables (jump targets,
  lookup tables) as part of that logic.

The port expects this dump (the common "USA, Europe" release):

* **File name:** `Wonder Boy in Monster World (USA, Europe).md`
* **Size:** 786,432 bytes (768 KB), no header
* **SHA-1:** `87a968f773c7e807e647c0737132457b06b78276`

A `.bin` with the same contents works too; rename it, or give its path when you run the game. The
game reads data from fixed places in the ROM, so other versions (Japanese, Wii Virtual Console
repacks, hacks) will not work properly. It warns you if your file's SHA-1 doesn't match.

## Play it (Windows)

1. **Install MSYS2** from <https://www.msys2.org> (the default location, `C:\msys64`, is fine).
2. **Open the "MSYS2 UCRT64" shell** from the Start menu and install the compiler, make, SDL2 and
   git:

   ```bash
   pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-SDL2 make git
   ```

3. **Get the code:**

   ```bash
   git clone https://github.com/matthewkris/wbmw-decomp.git
   cd wbmw-decomp
   ```

4. **Add your ROM.** Make a folder called `rom` in the repository and put your dump in it, named
   exactly `Wonder Boy in Monster World (USA, Europe).md`:

   ```
   wbmw-decomp/
     rom/
       Wonder Boy in Monster World (USA, Europe).md
   ```

   You can do this in Windows Explorer. In the MSYS2 shell, your C: drive is at `/c/`, so
   `cp "/c/Users/<you>/Downloads/your-dump.md" "rom/Wonder Boy in Monster World (USA, Europe).md"`
   works too.

5. **Build** (takes about a minute):

   ```bash
   make -C native
   ```

   This makes `work/native/wbmw.exe` and copies `SDL2.dll` next to it.

6. **Play:**

   ```bash
   work/native/wbmw.exe
   ```

   You can also double-click `wbmw.exe` in `work\native\`. The game starts **full screen**. It
   finds the ROM in the repository's `rom/` folder, in a `rom/` folder next to `wbmw.exe`, or
   next to `wbmw.exe` itself. To use a ROM somewhere else, give its path:
   `work/native/wbmw.exe path/to/rom.md`.

To play on another Windows PC, copy `wbmw.exe`, `SDL2.dll` and your ROM (in a `rom` folder next to
them, or beside them). MSYS2 isn't needed to run the game.

### Controls

These are the defaults. You can change all of them in the menu (**Esc → Controls**).

| Genesis/Mega Drive | Keyboard | Controller |
|---|---|---|
| D-pad | Arrow keys | D-pad (and the left stick) |
| A / B / C | Z / X / C | Left / bottom / right face button (X / A / B on an Xbox pad) |
| Start | Enter | Start |
| *Quick save / load* | F2 / F4 | *not set* |

Controller buttons are named by position, because each make of pad labels them differently
(the Switch's A is where the Xbox's B is).

| Key | Does |
|---|---|
| Esc (controller: Back/Select) | Opens the menu. The game pauses while it's open |
| F11 or Alt+Enter | Switches between full screen and a window |
| F3 | Shows or hides the frame rate |

Back/Select is View on Xbox, Share/Create on PlayStation and Minus on Switch. It opens the menu,
so Start stays the game's own: it opens the item screen.

### The menu

* **Show FPS, Full screen, Whole pixels** (every pixel the same size), **Widescreen**, **Talk
  zoom**, **Enemy health** (the health bars and damage numbers), **Sound**: on or off. Widescreen
  is a test for now: the picture is 16:9, and most rooms fill it; one-screen rooms, houses and
  shops keep the normal picture, centred with black sides. See `docs/WIDESCREEN.md`.
* **Controls**: a key and a controller input for each button, plus quick save and quick load.
  Choose one and press Enter (A on a controller), then press the new key or button; on the
  controller side, a stick direction or a trigger works too. A key already used elsewhere swaps
  places with the old one. Delete (Y on a controller) clears one. **Left stick as D-pad** lets
  the stick move you too (turn it off if your stick drifts, or to use its directions for
  something else), and **Reset to defaults** puts everything back.
* **Exit game** asks first.

The menu itself always works with the arrow keys, Enter and Esc, and on a controller with the
D-pad, A and B, whatever you set the game's controls to.

Your settings are saved in `wbmw.ini` next to `wbmw.exe`. Command-line options override them for
one run without changing the file. Delete the file to go back to the defaults.

Quick save stores the game in `wbmw.state` and quick load returns to it. A message at the bottom
of the screen confirms each one. The game's own save, kept on the cartridge's EEPROM, is written
to `wbmw.sav` in the current folder when you exit. Use `--save FILE` for a different file.

### Options

```
wbmw [ROM] [--windowed] [--widescreen] [--talk-zoom] [--enemy-health] [--scale N] [--integer-scale] [--fps] [--mute] [--save FILE] [--state FILE] [--load] [--config FILE]
```

* `--windowed`: start in a window instead of full screen. `--scale N` sets the window's size, N
  times 256×224.
* `--integer-scale`: scale only by whole numbers, so every pixel is the same size.
* `--no-widescreen`, `--no-talk-zoom`, `--no-enemy-health`: turn that feature off for this run
  (`--widescreen`, `--talk-zoom` and `--enemy-health` turn it on).
* `--fps`: show the frame rate from the start. `--mute`: no sound.
* `--state FILE`: the file quick save and quick load use. `--load`: load it at start.
* `--config FILE`: use this settings file instead of `wbmw.ini`.

`native/README.md` has the rest, including headless runs and frame logs for testing.

## Linux and macOS

These aren't tested yet, but the build is plain C/C++ with SDL2 and should work. Install a C/C++
compiler, make and the SDL2 development package (Debian/Ubuntu: `sudo apt install build-essential
libsdl2-dev`; macOS with Homebrew: `brew install sdl2`). Then follow steps 3 to 6, running
`work/native/wbmw` instead of `wbmw.exe`.

## Troubleshooting

* **"No ROM found"**: the file must be in `rom/` and named exactly as above, including the
  spaces, the brackets and the `.md` extension. Windows may hide extensions, so check the file
  isn't called `....md.md` or `....bin`. Or give the ROM's path on the command line.
* **"... is N bytes; the game's ROM is 786,432 bytes"**: the file isn't a plain dump. It may still
  be zipped, or it may have a 512-byte copier header (`.smd` files are in a different format).
* **"warning: ... is not the expected dump"**: this is a different version or a modified ROM. The
  game may start, but it won't play correctly.
* **`sdl2-config: command not found`** or **`gcc: command not found`** while building: you are in
  the wrong MSYS2 shell. Use "MSYS2 UCRT64", not "MSYS2 MSYS", and run the `pacman` line from step 2.

## For developers

How the port was made, and how to check it against the original:

* `port/`: the C of every routine in the game. `native/`: the machine core
  (`machine.c`) and the SDL host (`wbmw.c`).
* `harness/`: the reference harness. It runs the original 68000 code (Musashi) with the C routines
  checked against it (shadow mode) or used in its place (replace mode). See `harness/README.md`.
* `tools/`: the disassembler, code/data mapper and test tools. These also need Python 3 with
  `capstone` (`pip install capstone`); `tools/fixpoint.sh "rom/Wonder Boy in Monster World (USA,
  Europe).md"` writes the code/data map to `work/`. Running `make` at the repository root rebuilds
  the ROM from generated assembly (it needs vasm's `vasmm68k_mot`) and checks that it's
  byte-identical.
* `tools/extract.py "rom/Wonder Boy in Monster World (USA, Europe).md"` writes the game's data out
  of your ROM into `assets/` (graphics, palettes, every room's map, all the text, the sprites and
  their animations; git-ignored, as the ROM is): the first step towards the game loading its data from files. `tools/rebuild.py` checks the files
  are complete by building the ROM's bytes back from them. See `docs/EXTRACT.md`.
* Documentation: `docs/NOTES.md` (what the game does), `docs/PORT.md` (the port and how each routine
  is verified), `docs/PROCESS.md` (the process as a whole) and `native/README.md` (the native
  build).

## Licence

This repository's own code and documentation are under the MIT licence (see `LICENSE`).
Third-party code is in `third_party/`, each part under its own licence: Musashi (68000, harness
only, with its SoftFloat; MIT), ymfm (YM2612 sound; BSD 3-clause) and the Z80 core from `chips`
(zlib).

This is an unofficial fan project, not affiliated with or endorsed by Sega or the game's other
rights holders. *Wonder Boy in Monster World*, Sega, Genesis and Mega Drive are their owners'
trademarks. The licence does not cover the game itself: none of its data is in this repository,
and you need your own copy of the game to play.
