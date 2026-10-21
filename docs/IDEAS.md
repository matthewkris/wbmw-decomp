# Ideas for the native build

Features the owner has asked for, to do later. Each entry gives the state it is in and what
doing it involves. Nothing here may change the game itself unless it says so: anything drawn or
handled by the host (the window, menus, overlays) sits outside the emulated game, so the
comparisons with the original keep working.

## Done

* **FPS display** (asked 2026-10-10): `--fps` or F3, drawn over the picture by the host.
* **Full screen** (asked 2026-10-10): `--fullscreen`, F11 or Alt+Enter; `--integer-scale` for
  even pixels.
* **The menu, first version** (asked 2026-10-11, native build): Esc opens it over the frozen
  game; Show FPS and Exit game, with a confirmation (also when closing the window). See
  `native/README.md`. Still to do from the settings menu below: more toggles, volume, saving
  the settings, and the same menu in `play.exe`.

## To do

### Settings menu on pause (asked 2026-10-11)

A host menu that opens over the paused game, with settings that are kept between sessions.

* **Toggles first:** show FPS (today F3 and `--fps`), then the other window options that exist
  as keys or command-line flags (full screen, whole-number scaling, mute).
* **Volume sliders later:** master, and probably music and sound effects separately. Master is
  simple (the host mixes the output). Music versus effects is harder: both come from the same
  sound driver on the Z80 and the same two chips, so splitting them means knowing which
  channels the driver gives to effects, or reading the driver's own state.
* **More options later**, as they come up (key bindings, controller setup, save state slots).
* **Design points:**
  * The game has its own pause on Start. The host menu should be separate (Escape, say), stop
    the game completely while it is open (no frames, no sound time), and resume exactly where
    it was, so the game cannot tell it was opened.
  * Settings are saved in a small file next to the executable (for example `wbmw.ini`);
    command-line flags override it.
  * The menu needs its own text and drawing; the host already draws the FPS counter, which can
    be grown into a small font and menu renderer.

### Exit confirmation (asked 2026-10-11): done in the native build

Esc and closing the window no longer quit at once: the menu asks first. Still open: skipping
the question if a state was saved recently, and `play.exe`. Command-line runs used for testing (`--frames`, `--dump-at` and
the like) must still exit without asking.

### Enemy health and damage numbers (asked 2026-10-11)

* **Enemy HP in the HUD:** when an enemy is fought (the last one hit, or the nearest), show its
  remaining health in the status bar at the top.
* **Damage numbers:** when an enemy is hit, show the damage dealt just above it, rising and
  fading over a second or so.

What it involves:

* Finding, in the original, where each enemy's health is kept (an entity field) and the code
  that subtracts damage when an attack lands. The hit-box code is already ported
  (`port/hitbox.c`), which is the place to start looking; the damage step is probably just
  after it.
* Drawing it as a host overlay, like the FPS counter: the host reads the health field and
  watches it drop, and draws the numbers over the picture, using the camera position to place
  them above the enemy. The game itself is not changed, so the comparisons with the original
  keep working. An option in the settings menu turns it on and off (off by default, to keep
  the original look).
* Space in the HUD: the status bar's free area is used by the game for item names and
  messages, so the HP display either takes a small spot of its own (e.g. beside the gold) or
  is drawn as a small bar near the enemy instead. Decide by trying both.

### Zoom in during character interactions (asked 2026-10-20)

When the player talks to someone (a shopkeeper, a villager, the Sphinx and so on), the view
zooms in on the two of them, and zooms back out when the conversation ends.

What it involves:

* Knowing when a conversation is on: the game's own state says so (the conversation sequence,
  room change 7/8 at `$C1E8`, and the message window's flag while the interpreter at `$13BC`
  runs), so the host can read it from RAM without changing the game.
* Where to zoom: on the player (`$9EEE`) and the person spoken to, placed on screen through the
  camera (`$9758`/`$975A`), with a short smooth zoom in and out instead of a cut.
* Done by the host, like the FPS counter: it scales and crops the finished picture, so the game
  and the comparisons with the original are untouched. An option in the settings menu turns it
  on and off (off by default).
* The catch: the message window and the HUD are part of the same picture (the window plane), so
  a plain crop would cut or enlarge the text too. Either zoom only the play field and keep the
  rows of the HUD and the message window at their normal size, or have the host draw the
  playfield planes and the window plane separately. Decide by trying.

### Wide screen (asked 2026-10-10)

Possible, but a project of its own. The game is written for a 256-pixel view: scenery is copied
into video memory just past the visible edges, objects are created and removed at the screen
edges, and the HUD and text boxes are placed for 256 pixels. A wider view means changing the
game's own logic (map copying, spawn and removal ranges, camera, HUD), so it can no longer be
compared byte for byte with the original. It belongs after the idiomatic rewrite, as an option
that is off by default, with the normal view still checked against the original.

### Extract the assets from the ROM once (asked 2026-10-21)

Today the C reads the game's data straight from the ROM image as it plays, at the same addresses
the cartridge has (graphics, maps, text, palettes, music, the Z80 sound driver). Instead, a
one-time step could unpack the ROM into ordinary files, and the game would load those.

* **What it gives:** assets that can be looked at and changed (tiles and sprites as PNG, maps and
  text as JSON or text, palettes), so mods, translations and redrawn graphics become possible.
  After extracting, the ROM itself is no longer needed to play.
* **The extracted files are still the game's data:** they go in a git-ignored folder (`assets/`,
  say) and are never committed, the same as `rom/` and `work/`. The README's steps become
  "add your ROM, run the extractor once, play".
* **What it involves:**
  * Knowing the format of every kind of data and where each piece is. Much of it is packed
    (the room blocks and maps unpacked at `$9F6A`/`$A068`, the compressed graphics), and the C
    ports already decode those formats, so the extractor can reuse them. `work/map.json`
    (the code/data map) lists the data regions to start from.
  * Changing the C to load from the extracted files instead of reading ROM addresses. That
    means going through every ROM read in `port/` and the tables of addresses that point into
    the data. It is a big change, and the comparisons with the original stop working for any
    routine whose data access changes. So it comes after the idiomatic rewrite, with a check
    that the extracted files, packed back up, give the same bytes as the ROM.
  * Sound: the music and effects can be extracted as data, but they are commands for the game's
    Z80 driver, which still runs from the ROM's code. Taking the ROM out completely needs that
    driver rewritten in C too.
* **A cheap first step:** an extractor that only writes viewable copies (PNG sheets of the
  tiles, the maps, the text) without the game using them, to see the formats and check the
  decoding.
