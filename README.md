# Wonder Boy in Monster World (Genesis/Mega Drive) — C port

A port of the Sega Genesis/Mega Drive game *Wonder Boy in Monster World* to hand-written C (the
console was sold as the Genesis in North America and the Mega Drive in Europe and elsewhere).
It runs natively with SDL2. The game's 68000 program is not emulated: every routine was rewritten by
hand, then checked against the original in a reference harness (shadow runs, fuzzing, coverage).
The game is playable from start to finish.

One part is still emulated: sound. On the Genesis/Mega Drive, the music and sound effects are played by a
small program on the console's second CPU, a Z80. The port loads that driver from your ROM and runs
it on an emulated Z80, which drives emulated sound chips (ymfm for the YM2612, plus the SN76489).

## No ROM data is in this repository

**You need your own dump of the cartridge.** This repository contains only source code, tools and
notes. It contains no ROM, no graphics, maps, text, music or sound, and no file made from the ROM:

* `rom/` (where you put your dump) and `work/` (everything built or extracted from it) are listed in
  `.gitignore`. So are save files (`*.sav`) and save states (`*.state`).
* When the game runs, it reads all of its data (graphics, maps, text, music, sound, and the Z80
  sound driver) from your ROM file. The C code holds only the game's logic, with a few small numeric tables (jump targets,
  lookup tables) as part of that logic.

The port expects this dump (the common "USA, Europe" release):

| | |
|---|---|
| File name | `Wonder Boy in Monster World (USA, Europe).md` |
| Size | 786,432 bytes (768 KB), no header |
| SHA-1 | `87a968f773c7e807e647c0737132457b06b78276` |

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

| Genesis/Mega Drive | Keyboard |
|---|---|
| D-pad | Arrow keys |
| A / B / C | Z / X / C |
| Start | Enter |

A game controller works too (SDL's standard mapping).

| Key | Does |
|---|---|
| Esc | Opens the menu (Show FPS, Exit game). The game pauses while it's open |
| F11 or Alt+Enter | Switches between full screen and a window |
| F3 | Shows or hides the frame rate |
| F2 / F4 | Save state / load state (`wbmw.state`) |

The game's own save, kept on the cartridge's EEPROM, is written to `wbmw.sav` in the current folder
when you exit. Use `--save FILE` to use a different file.

### Options

```
wbmw [ROM] [--windowed] [--scale N] [--integer-scale] [--fps] [--mute] [--save FILE] [--state FILE] [--load]
```

* `--windowed`: start in a window instead of full screen. `--scale N` sets the window's size, N
  times 256×224.
* `--integer-scale`: scale only by whole numbers, so every pixel is the same size.
* `--fps`: show the frame rate from the start. `--mute`: no sound.
* `--state FILE`: the file F2 and F4 use. `--load`: load it at start.

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

* `port/`: the hand-written C of every routine in the game. `native/`: the machine core
  (`machine.c`) and the SDL host (`wbmw.c`).
* `harness/`: the reference harness. It runs the original 68000 code (Musashi) with the C routines
  checked against it (shadow mode) or used in its place (replace mode). See `harness/README.md`.
* `tools/`: the disassembler, code/data mapper and test tools. These also need Python 3 with
  `capstone` (`pip install capstone`); `tools/fixpoint.sh "rom/Wonder Boy in Monster World (USA,
  Europe).md"` writes the code/data map to `work/`. Running `make` at the repository root rebuilds
  the ROM from generated assembly (it needs vasm's `vasmm68k_mot`) and checks that it's
  byte-identical.
* Documentation: `docs/NOTES.md` (what the game does), `docs/PORT.md` (the port and how each routine
  is verified), `docs/PROCESS.md` (the process as a whole) and `native/README.md` (the native
  build).

Third-party code is in `third_party/`, each part under its own licence: Musashi (68000, harness
only, with its SoftFloat), ymfm (YM2612 sound) and the Z80 core from `chips`.
