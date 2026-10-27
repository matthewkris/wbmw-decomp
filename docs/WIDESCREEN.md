# Widescreen

The native build can show the game 400 pixels wide (16:9 at the game's 224 lines) instead of
256: 72 more pixels at each side. It is an option (menu: **Widescreen**, `--widescreen`, or
`widescreen=0` in `wbmw.ini` turns it off), on by default. 121 of the 170 rooms use the extra
width (a rule on their layers, below); the rest show the normal picture centred with black sides.

## How the game draws, and what changes

* The background layers (planes A and B) are 512 × 256 pixels (64 × 32 tiles, VDP register 16 =
  $01). The game shows 256 of the 512 and draws the map into the rest as the view moves.
* Each layer keeps a window of map columns drawn (16 pixels each): the first at `$8` of its record,
  how many at `$A`, at most 32 (the whole plane). `ColumnsIn` ($9D46) draws the columns that come
  into view each frame, which is only up to the screen's right edge (one column past the left
  when moving left): the 256 the game shows, plus columns behind you.
* A layer's x (`$4`) follows the camera at its parallax and is kept between the map's left end
  and its limit (`$24`), so that the 256 never show beyond the map.
* The HUD is the window plane over the top 64 lines (registers 17 and 18: lines 0–63, the full
  width). Where its tiles are transparent, plane B shows through.
* Sprites are placed from the same layer x (the `$8B34` table), so they stay with the scenery.

In a room that allows it (`port_wide_now()`, which reads nothing unless the host asks, so the
harness always runs the original's), five routines change. The scenery, in `port/rooms.c`:

* `columns_screens` ($99AC, a room's start): the columns of the 400-pixel view, not of the 256.
* `columns_in` ($9D46, each frame): `columns_in_wide` draws whatever columns of the 400-pixel
  view are missing, on either side, keeping at most 32.
* `layer_x` ($9A8E): the camera it reads kept where plane A's wider view stays inside plane A's
  map (72 pixels further in from each end), so every layer, at its own parallax, stops where
  plane A does; then each layer's x kept inside its own map too. Near a room's ends the view
  therefore stops a little earlier than the original's. (At first only each layer's x was kept
  in: in the field, whose plane B moves at half speed with too short a range to need it, plane B
  went on sliding for the last 72 pixels after plane A, with the flowers, had stopped.)

The things in the room (stage 2):

* `BuildSprites` ($53AA) and `AddEntitySprites` ($54C4), `port/sprites.c`: the sprites' window is
  the wider view (right limit `$8B2E` $180 + 72, left $80 − 72), so whatever is in it is drawn.
  Before this, Wonder Boy vanished near a room's ends (the view stops sooner there, so he could
  walk past the old edge) and when he came in at one.
* `OffScreenXAt` ($BD0, and `$BC6`, `$BEE` through it), `port/kernhelp.c`: "off the screen across"
  is off the wider view (still with its 32 pixels of margin). Everything that asks goes by it:
  the spawn points' rules (`port/spawns.c`: whether a point is on the screen, or not too far off
  it), `DespawnIfOffscreen` ($48C4) and the enemies' own tests. So enemies appear at the edges of
  the wider view and go away past them, as they did at the 256's.

The renderer (`harness/vdp.c`, shared with the harness) draws the picture `256 + 2 × vdp_pad`
wide. With `vdp_pad_fill` (the room is allowed) the planes and sprites are drawn across the
whole width; otherwise the sides are black. (With the one-row HUD, below, the band beside and
under the HUD is drawn as described there; this paragraph is how it began, with the original's
64-line HUD.) The HUD stays where it is, centred; beside it plane A
is not drawn, as it never is behind the HUD, so the band shows plane B and sprites there, as
through the HUD's own gaps. That is only the HUD's own 64 lines: a message under the HUD
("Found 5 GOLD.") stretches the window down for a while, and the sides of those lines are the
play field's, plane A included (at first they were not, and the scenery beside the message
lost its plane A: the top of the forest's cave entrance vanished while it showed). The sprite limits per line grow in proportion to the width. H40
screens (320 wide) are centred with 40-pixel black sides.

## The rooms

`wide_room()` in `native/wbmw.c` decides, from each room's script in the ROM (read once and
kept): a room is wide when its layers allow it. A layer's kind (its command's first byte) picks
from the table at `$94CC` how it follows the camera across and down, and how its columns and rows
are drawn:

| Kind | Across | Down | Drawn | Rooms like |
|---|---|---|---|---|
| 0 | follows | follows | as screens | rooms scrolling both ways |
| 1 | fixed | fixed | as screens | one screen (the opening room) |
| 2 | follows | fixed | as screens | side-scrolling rooms (the village) |
| 3 | follows | fixed | one screen repeated | a repeated plane B |
| 4, 5 | follows | follows | repeated | repeated planes B |
| 6 | fixed | follows | as screens | shafts |
| 7 | moves by itself | moves by itself | repeated | moving planes B |

The rule: plane A of kind 0 or 2 and at least two screens wide (so a 400-pixel view fits in its
map); plane B repeated (3, 4, 5, 7), fixed (1), or, drawn as screens, two screens wide too. A
fixed plane B is one screen that stays put, but the map lookup wraps its columns, so the sides
show it repeated: area 5's castle wall behind its bridge carries on seamlessly (at first these
six rooms were left at the 256). A narrower plane A, or one that stays put, would show what is
beyond its map at the sides: those rooms (one-screen rooms, shafts) keep the 256 with black
sides. 121 of the 170 rooms qualify. A narrow room also qualifies when its sides are painted
(2026-10-27, `docs/MODDING.md`, "Widescreen sides"): each narrow plane drawn as screens (and a
plane A that stays put) has a place just beyond its width in every band of its map, which the
camera never reaches; `port/map.c` (`port_margin_draw`, set only while `port/rooms.c` draws the
wide view: the whole layer as a room loads, `draw_layer`, and the columns and rows scrolling in) reads the sides' columns from it, the right side from its left end,
the left side from its right end, instead of the edge's column repeated (`MapBlockAt` clamps).
The game's own lookups never see it. It is asked of the loaded room's maps each time (`wide_room`,
`layer_margin`); area 13's room 7 is left out, its plane A having places beyond its width of its
own. Checked (2026-10-27): marker blocks in a margin's left and right columns show at the right
and left sides, its middle nowhere; a room entered through an exit (area 14's room 6, from area
12's room 8) is wide from its first frame; the strip under the one-row HUD shows the map's top
rows, as in every wide room. Such a room's sides are walked into: `wide_room` answers 2 for it
and the room's triggers (`trig_exit`, `port_wide_sides`) move an exit whose zone lies beyond the
room's columns out by the side's 72 pixels, so the room changes at the wide picture's edge, not
at the 256's (at first it changed there, with the painted side still ahead of the player).
Collision past the room's edge is the edge column's, as in the original. The rows above a
layer's top come the same way from a place painted below its height (`margin_row` in
`port/map.c`: row -1 is that place's last row; `port_margin_top` lets `rows_wide` draw up to 64
lines above the play field there; `draw_column` reads such rows only as far as row 0; nothing
painted reads as an empty chunk). Checked (2026-10-27, area 0's room 0, its map 24 rows with
markers in rows 21-23 and in the corners): every visible pixel of the strip above the map and of
the corners matched its marker (5,053 pixels), the room's height ($46) stayed 12 rows, and no
other room's picture changed in the band survey. Walking in through an
exit (arrival 2) near such a room's edge, the player starts as far out (`wide_arrival`, in
`room_enter` once `$9808` has set the layers up, which the sides' check reads). For the fade out
`wide_room` keeps the loaded room's answer: the game sets the next room's number as the player
walks out, before its maps load, and the sides went black for the fade. Checked (2026-10-27,
area 0's room 0): walking right, the room changes at x $1148 instead of $1100, wide through the
fade; walking back from area 1 the player arrives at $1140 instead of $10F8, wide through the
fade in; with widescreen off at $1100 and $10F8 as before. The attract demo is never wide (`$8A50` bit 7, its recorded input): a recording of buttons only
fits the original's game, and things waking at the wider view's edges threw it off (2026-10-27:
Wonder Boy stuck at the start and hurt instead of walking into the shop); with it narrow the
game's RAM is identical to a run with widescreen off at every frame checked. One exception: area
15 (its moving floor is placed from the screen's edge, `$18C8C`). Houses and shops are not rooms
of their own: their picture opens over the room outside (`$BE6C`, which sets `$9659` bit 4 while
inside), so they stay as wide as it. (At first bit 4 was taken for "a boss's scripted room" and
turned widescreen off, which only ever happened at a door: the sides went black at once, a jarring
shift.) Bosses' rooms qualify by their layers like any other; the ones tried (the snowman, Gragg
and Glagg, area 6's) look right, but a boss whose moves depend on the screen's edges would need a
look.

### The HUD in one row

With widescreen on, the HUD is laid out for the wide picture in every room, wide or not
(`vdp_hud_wide`, `harness/vdp.c`): its top row only (32 lines), with the gold's box (the bottom
row's right) moved beside the items', the three centred (hearts, items, gold: 352 pixels). The
bottom row's left, the last enemy's health, is left out: the host shows each enemy's health as it
is hit (`docs/ENEMYHEALTH.md`). The bottom row's lines (32-63) are the play field's where the map
is drawn there, black elsewhere, as beside the HUD. Where the map is drawn but a pixel has
nothing on it, it is black there too, not the room's backdrop colour: area 12's cave has gaps in
its map that the original's 64-line HUD always covered, and its purple backdrop showed through
them as a line beside the one-row HUD when Wonder Boy jumped. Rooms whose sky is their backdrop
show it there instead, from a per-room list (`band_colours` in `native/wbmw.c`, into
`vdp_band_colour`: black, the backdrop, or a CRAM entry), since in them the tops of clouds drawn
in the map's first rows looked cut out of black (2026-10-27: area 1's room 0, the backdrop
$E60). `tools/bandsurvey.py` enters every room (`analysis/warp.json`) and draws the band black and
in the backdrop: 58 more rooms have empty pixels there; the owner chose the backdrop for 40 of them (skies, open
water, area 6's forest), the other 18 (caves, walls, area 2's purple forest, area 12's cave) stay
black. Area 6's forest takes its canopy's dark green (CRAM entry 43, $040; the owner's choice: its
purple backdrop above the treetops looked wrong). The seas (area 1's rooms 2-4, area 8's 1, 2, 6, 8) take their sky's navy instead (CRAM
entry 26, $800): their sky is a gradient of tiles whose bottom is the backdrop, and the backdrop's
bright blue beside the HUD's row sat over the navy (the survey's check: the pixel beside the HUD's
row and the one under it agree in every listed room). Each plane has its own top there (`vdp_band_top_a`, `_b`); above it that plane draws nothing
beside the HUD (its wrapped rows), and the band starts where either map does (2026-10-27: with one
top for both, the lower plane's, area 12's rock was cut off where its sky's mountains begin; 25 rooms
changed, each a plane carrying on up). A layer repeated down the plane fills the band with its repeat, except in
a room whose band has a colour: there it stops at its map's top (2026-10-27: area 12 room 10's
sky brought its mountains round again under the HUD; the survey found no other room changed). Where the map is drawn there is the band's top
(`vdp_band_top`, from each layer's first drawn row and its y), taken as the higher of this frame's
and the frame before's: the picture shows the scroll the game sent the frame before (in VBlank),
so coming back down from a jump near a room's top (area 12 room 0), this frame's alone let a few
lines above the map show, the plane's wrapped rows (its floor) beside the HUD. In a room that scrolls down too, `layer_y`
keeps the camera it reads 32 lines below the map's top (`WIDE_ABOVE`, as `layer_x` does at the
ends), so those lines always show the map: at first the camera scrolled into them near a room's
top, and a strip of sky came and went as Wonder Boy jumped (area 5's town). The game draws its HUD as always; the renderer
only places it. Only while the window is the HUD (from the top, the full width, 8 rows or more).

The status screen (Start) grows the window down over the HUD's bottom row, and draws there a frame
before it starts to grow. So the gold's box is drawn from a copy of its tiles, kept each frame
while the window is 8 rows and the bottom row's top edge is the HUD's (a changed edge is taken
only after half a second); and while that edge differs and the window is taller, the status
screen is shown whole, in the game's width. It hung from the HUD's bottom row too, its edge its
top: it is drawn 8 lines lower, with its own bottom row (its last, drawn before it slides open)
turned over above it. With a state loaded while it is open (no copy yet) it is told by its size
(12 rows or more), and the gold's box is left empty until it closes. A message
under the HUD ("Found 5 GOLD.") leaves the edge alone.

The gold's box is drawn in the items' box's frame (its own had no top: the items' box's bottom was
its top), with its words inside, level with the items. A message under the HUD ("Found a Potion.",
"PAUSE") hung from the bottom row the same way, its edge its top: it gets its own bottom row turned
over, on the line above it. Gold found is not shown in a box at all: its amount rises in gold from
where Wonder Boy picked it up, like the damage numbers (`port_found_gold`, noted by `$1FCA`; the
box is hidden, `vdp_hide_msg`, until the game closes it or another thing's box opens). While it
is hidden the game's masks still cover the message's lines (it moves them down to 56-88), so
there they hide nothing: at first Wonder Boy jumping just after picking up gold went behind
nothing visible.

Beside the HUD, what the game puts behind it shows where the map is drawn there, and stays
hidden in the black bar (as behind the HUD: area 7's spears rise back into the ceiling there, and
showed above it at first). The game hides what is behind the
HUD in two ways: two masking sprites (a sprite at x = 0 hides every later one on its lines) and,
once they are added, its sprite builder's top limit raised to the HUD's bottom (`$8B28`), so a
piece wholly above line 64 is not built at all. In widescreen the limit is not raised
(`port/sprites.c`), and on the HUD's lines the renderer draws every sprite after the mask at the
sides only, from `vdp_band_top` down. At first Wonder Boy jumping up beside the HUD vanished
where the map is drawn, then kept only his sword and legs.

The host takes the room number for this only when the screen is dark (the palette all black, or
the display off), which is when the game draws the next room: it sets the next room's number as
the player walks out, while the old room is still shown for the fade out. At first the number was
taken at once, and walking out of area 5's castle tower (one screen) into the town widened the
tower for the ten frames of its fade: sky at the sides, its walls cut at the 256's edges, which
looked like the screen drawing in. A room number that stays the same for a second, or a state
loaded, is taken without waiting.

Tried, walking both ways: the first village, the forest path and the field (area 2), the beach
(area 1), area 8's beach and its underwater room, area 9's green dungeon (scrolling both ways; it
stops 72 pixels sooner at its ends, so its own black void shows at the edge, as in the original
at those ends), area 10's desert, area 12's cave, area 18's snowman, area 7's Gragg and Glagg,
area 6's temple.

## Checks

* With widescreen off, the native build is the same as before frame for frame (frame logs of RAM
  and VRAM, and the pictures, compared with the build before it over 2,000 frames of five
  states), and so is the harness, in all three modes, with no mismatches in shadow mode.
* `OffScreenXAt` and `AddEntitySprites` fuzzed against the original (3,000 cases each, no
  mismatches); the shadow runs check `BuildSprites`.
* With it on: the village, the forest path and the walk between them (Wonder Boy seen as he comes
  in at the forest's left end, mushrooms appearing at the wider edges), the inside of the inn (the
  game draws a house as a picture over the village, so the village shows at its sides, as in the
  original), the title screen and a dungeon (black sides).

## Not done yet

* The rooms are chosen by the rule above (121 of 170), not one at a time; the owner's play finds
  what is left. Things to look out for: code that places something from the screen's edge itself
  rather than through `OffScreenXAt` (one found: the moving floor of area $0F, `port/objhelp.c`,
  placed from plane A's x + $70, so that area stays narrow), and enemies or bosses that come in
  from a side or move by the screen's edges.
* In rooms taller than the screen the original's camera follows a jump up and back (area 12 room
  0: about 38 lines), the same with widescreen off; a steadier camera is an idea for later
  (`docs/IDEAS.md`).
* More of them can be alive at once now (the view holds more): the game's entity slots and the
  sprite limits have not been a problem in the rooms tried.
* The window opens 400 wide when widescreen is on at the start; switching it on later keeps the
  window's size (the picture fits inside it).
