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
  `native/README.md`.
* **The menu on a controller** (asked 2026-10-21): Back/Select opens it; Start stays the game's.
* **Settings and controls** (asked 2026-10-21): the menu has Full screen, Whole pixels and Sound
  toggles and a Controls screen (a key and a controller input for each button and for quick save
  and load, swapping on conflict, clear, left stick on/off, reset), all kept in `wbmw.ini`. Quick
  save and load show a message. Still to do from the settings menu below: volume sliders, save
  state slots, and the same menu in `play.exe`.

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

### Enemy health and damage numbers (asked 2026-10-11): done

Done 2026-10-21 as the menu's **Enemy health** (on by default): the damage of each hit rises
from the enemy as a number (gold for the killing blow), and a bar over it shows the health it
has left, for two seconds after a hit. All in the host. See `docs/ENEMYHEALTH.md`. Not done: a
place in the HUD for it (the bar over the enemy turned out to be enough so far); bosses not tried.

### Zoom in during character interactions (asked 2026-10-20): done (experimental)

Done 2026-10-21 as the menu's **Talk zoom** (on by default): in a conversation with an ordinary
character the scene eases in on Wonder Boy and the speaker, the HUD and the box slid up, the box
kept at its own size; not for bosses or the Sphinx (asked by the owner). All in the host. See
`docs/TALKZOOM.md`. Open: more conversations tried (shops, people outdoors); whether 1.5 is the
right zoom; a conversation open when a state is loaded is not zoomed.

### Wide screen (asked 2026-10-10)

**Started (2026-10-21), stages 1 and 2 done, paused there by the owner (experimental):** a 16:9
picture (400 pixels), the menu's Widescreen option, the scenery drawn wider in area 1 rooms 0-4
(after the opening) and area 2 rooms 0-2 (the first village and the areas beside it), black sides
elsewhere; sprites drawn and enemies created and removed at the wider edges; fixes for the
message under the HUD and for the layers stopping together. Since 2026-10-21 the rooms come from a
rule on their layers (121 of 170 wide); bosses whose moves depend on the screen's edges, if any,
would still need a look. See
`docs/WIDESCREEN.md`. The original assessment:

Possible, but a project of its own. The game is written for a 256-pixel view: scenery is copied
into video memory just past the visible edges, objects are created and removed at the screen
edges, and the HUD and text boxes are placed for 256 pixels. A wider view means changing the
game's own logic (map copying, spawn and removal ranges, camera, HUD), so it can no longer be
compared byte for byte with the original. It belongs after the idiomatic rewrite, as an option
that is off by default, with the normal view still checked against the original.

### Extract the assets from the ROM once (asked 2026-10-21)

**Started (2026-10-21):** `tools/extract.py` writes the graphics, palettes, rooms (scripts and
drawn maps), all the text and the sound data's raw bytes into `assets/` (git-ignored), with the
decoders' checks; the game does not use them yet. `tools/rebuild.py` checks them against the
ROM: the structured data built back byte for byte, the compressed data (graphics, room maps)
by content against the port's decoders; about 99% of the data covered so far. See
`docs/EXTRACT.md` for what comes next.

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
