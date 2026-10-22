# Widescreen

The native build can show the game 400 pixels wide (16:9 at the game's 224 lines) instead of
256: 72 more pixels at each side. It is an option (menu: **Widescreen**, `--widescreen`, or
`widescreen=0` in `wbmw.ini` turns it off), on by default. For now only a few rooms use the extra width; the
rest show the normal picture centred with black sides.

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
whole width; otherwise the sides are black. The HUD stays where it is, centred; beside it plane A
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
map); plane B repeated (3, 4, 5, 7) or, drawn as screens, two screens wide too. A narrower layer,
or one that stays put, would show what is beyond its map at the sides: those rooms (one-screen
rooms, shafts) keep the 256 with black sides. 115 of the 170 rooms qualify. One exception: area
15 (its moving floor is placed from the screen's edge, `$18C8C`). Houses and shops are not rooms
of their own: their picture opens over the room outside (`$BE6C`, which sets `$9659` bit 4 while
inside), so they stay as wide as it. (At first bit 4 was taken for "a boss's scripted room" and
turned widescreen off, which only ever happened at a door: the sides went black at once, a jarring
shift.) Bosses' rooms qualify by their layers like any other; the ones tried (the snowman, Gragg
and Glagg, area 6's) look right, but a boss whose moves depend on the screen's edges would need a
look.

Beside the HUD, sprites still show (over the black bar too). The game hides what is behind the
HUD in two ways: two masking sprites (a sprite at x = 0 hides every later one on its lines) and,
once they are added, its sprite builder's top limit raised to the HUD's bottom (`$8B28`), so a
piece wholly above line 64 is not built at all. In widescreen the limit is not raised
(`port/sprites.c`), and on the HUD's lines the renderer draws every sprite after the mask at the
sides only. At first Wonder Boy jumping up beside the HUD vanished into the bar, then kept only
his sword and legs.

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

* More rooms, one at a time, starting with the outdoor areas. Things to look at in each: code that
  places something from the screen's edge itself rather than through `OffScreenXAt` (one found:
  the moving floor of area $0F, `port/objhelp.c`, placed from plane A's x + $70), and enemies
  that come in from a side.
* More of them can be alive at once now (the view holds more): the game's entity slots and the
  sprite limits have not been a problem in the rooms tried.
* The window opens 400 wide when widescreen is on at the start; switching it on later keeps the
  window's size (the picture fits inside it).
