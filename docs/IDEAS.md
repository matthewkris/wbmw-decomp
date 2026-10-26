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
  state slots, and the same menu in `play.exe`. The menu has since grown the Widescreen, Talk
  zoom and Enemy health toggles.
* **Quick save asks before overwriting** (asked 2026-10-22): with a save already there, the first
  press shows "PRESS SAVE AGAIN TO OVERWRITE"; a second within two seconds replaces it.
* **A state load clears the host's overlays** (asked 2026-10-22): health bars, damage and gold
  numbers and the talk zoom from before the load are dropped.

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
has left, for two seconds after a hit. All in the host. See `docs/ENEMYHEALTH.md`. Bosses have
them too. A boss health bar in the HUD was tried (2026-10-22) and taken out at the owner's request:
the bar over the enemy is enough. With widescreen on, the HUD's enemy health box is gone too (see
Wide screen).

### Zoom in during character interactions (asked 2026-10-20): done (experimental)

Done 2026-10-21 as the menu's **Talk zoom** (on by default): in a conversation with an ordinary
character the scene eases in on Wonder Boy and the speaker, the HUD and the box slid up, the box
kept at its own size; not for bosses or the Sphinx (asked by the owner). All in the host. See
`docs/TALKZOOM.md`. Since 2026-10-22 it eases in and out smoothly (about a second in, three
quarters out), finds speakers the game gives no pointer for, and holds through a conversation
that ends in a fade to black (the inn). Open: whether 1.5 is the right zoom; a conversation open
when a state is loaded is not zoomed.

### Wide screen (asked 2026-10-10)

**Done (experimental, on by default; 2026-10-21 to 22):** a 16:9 picture (400 pixels), the
menu's Widescreen option. The rooms come from a rule on their layers: 121 of 170 are drawn wide,
the rest keep the original picture with black sides; houses and shops stay wide over the room
outside, and rooms switch between wide and narrow only while the screen is dark. Sprites are drawn
and enemies created and removed at the wider edges. A one-row HUD (hearts, items and gold side by
side, the enemy health box left out) in every room, with the item menu and messages opening below
it; gold picked up rises as a number instead of a "Found ... GOLD" box. Above the play field the
scenery shows where the room's map reaches, black where it does not. See `docs/WIDESCREEN.md`.
Open: bosses whose moves depend on the screen's edges, if any.

The original assessment:

Possible, but a project of its own. The game is written for a 256-pixel view: scenery is copied
into video memory just past the visible edges, objects are created and removed at the screen
edges, and the HUD and text boxes are placed for 256 pixels. A wider view means changing the
game's own logic (map copying, spawn and removal ranges, camera, HUD), so it can no longer be
compared byte for byte with the original. It belongs after the idiomatic rewrite, as an option
that is off by default, with the normal view still checked against the original.

### Extract the assets from the ROM once (asked 2026-10-21)

**Done (2026-10-21 to 23):** `tools/extract.py` writes the graphics, palettes, rooms
(scripts, maps and lists), all the text, the sprites, the tables inside the code and the sound
data's raw bytes into `assets/` (git-ignored). `tools/rebuild.py` checks them against the ROM
(structured data byte for byte, compressed data by content against the port's decoders), and
every byte the game reads over all the save states is in them. `tools/pack.py` packs them into
`assets/game.pak`, and `wbmw --pack assets/game.pak` plays from the files instead of the ROM,
identical to the ROM's runs frame by frame. Since 2026-10-23 the build makes the pack
(`tools/makepack.sh`) and the game plays from it by default, the ROM only when there is no pack
or one is named. See `docs/EXTRACT.md`. Since 2026-10-23 the music and effects are decoded
too (`sound/music.json`, `docs/SOUND.md`). The driver's instruments and other tables too
(`sound/instruments.json`).

The original plan (the C did not need changing after all: the pack's data image sits where the
ROM was, and only the three decoders take the decoded data):

Before, the C read the game's data straight from the ROM image as it played, at the same addresses
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

### Steady camera (asked 2026-10-23): for later

In some rooms taller than the screen (area 12 room 0, for one) the original's camera follows
Wonder Boy up when he jumps, about 38 lines, and eases back down when he lands. It is smooth and
faithful (the same with widescreen off), but it reads as the screen jumping. An option, off by
default, could keep the camera still for ordinary jumps and follow only when he climbs well above
its view (onto a higher ledge), as most later platformers do. It would change the game's camera
(`layer_y` in `port/rooms.c` reads it; the camera itself is the game's), so it goes behind a
setting with the normal camera kept for comparisons. The cost: it hides terrain above that the
original shows.

### Widescreen pictures for one-screen rooms (asked 2026-10-26): on hold

Rooms too narrow for widescreen (one-screen rooms, such as area 0 room 0, where a new game
starts: start point 0) keep black sides. The idea: an extended 400 x 224 picture of the room,
its sides filled by an AI outpainting tool, drawn by the host in the sides when widescreen is on.

* The plan: export the room's picture and a 400 x 224 template (the original 256 in the middle);
  the sides filled outside the project; a tool puts the original pixels back in the middle and
  snaps the sides to the room's palette and pixel grid; the picture kept as palette indices
  (CRAM entries), so the game's fades and flashes apply to it as to the room; packed with the
  room's other files and drawn where the sides are black. Sprites stay over it. One-screen rooms
  first (they never scroll); a scrolling room would need a picture per layer, moved at its
  parallax.
* Tools: Anthropic has no image generator. Free: Krita with the Krita AI Diffusion plugin, or
  Stable Diffusion (ComfyUI, Forge) with a pixel-art LoRA; Adobe Firefly's Generative Expand has
  a small free allowance; PixelLab and Retro Diffusion are made for pixel art (mostly paid).
* The pictures come from the game's graphics: they stay in `assets/` and are never committed.
