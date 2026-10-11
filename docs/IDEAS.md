# Ideas for the native build

Features the owner has asked for, to do later. Each entry gives the state it is in and what
doing it involves. Nothing here may change the game itself unless it says so: anything drawn or
handled by the host (the window, menus, overlays) sits outside the emulated game, so the
comparisons with the original keep working.

## Done

* **FPS display** (asked 2026-10-10): `--fps` or F3, drawn over the picture by the host.
* **Full screen** (asked 2026-10-10): `--fullscreen`, F11 or Alt+Enter; `--integer-scale` for
  even pixels.

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

### Exit confirmation (asked 2026-10-11)

Today Escape and closing the window quit at once, losing anything not saved. Instead, ask
"Quit?" (yes/no) in the same host menu. Closing the window could ask too, or skip the question
if a state was saved recently. Command-line runs used for testing (`--frames`, `--dump-at` and
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

### Wide screen (asked 2026-10-10)

Possible, but a project of its own. The game is written for a 256-pixel view: scenery is copied
into video memory just past the visible edges, objects are created and removed at the screen
edges, and the HUD and text boxes are placed for 256 pixels. A wider view means changing the
game's own logic (map copying, spawn and removal ranges, camera, HUD), so it can no longer be
compared byte for byte with the original. It belongs after the idiomatic rewrite, as an option
that is off by default, with the normal view still checked against the original.
