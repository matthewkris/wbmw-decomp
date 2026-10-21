# Widescreen

The native build can show the game 400 pixels wide (16:9 at the game's 224 lines) instead of
256: 72 more pixels at each side. It is an option (menu: **Widescreen**, `--widescreen`, or
`widescreen=1` in `wbmw.ini`), off by default. For now only a few rooms use the extra width; the
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

In a room that allows it, `port/rooms.c` changes three routines (only when the host asks, so the
harness always runs the original's):

* `columns_screens` ($99AC, a room's start): the columns of the 400-pixel view, not of the 256.
* `columns_in` ($9D46, each frame): `columns_in_wide` draws whatever columns of the 400-pixel
  view are missing, on either side, keeping at most 32.
* `layer_x` ($9A8E): the layer's x kept 72 pixels further in from each end of its map, so the
  wider view stays inside it. Near a room's ends the view therefore stops a little earlier than
  the original's.

The renderer (`harness/vdp.c`, shared with the harness) draws the picture `256 + 2 × vdp_pad`
wide. With `vdp_pad_fill` (the room is allowed) the planes and sprites are drawn across the
whole width; otherwise the sides are black. The HUD stays where it is, centred; beside it plane A
is not drawn, as it never is behind the HUD, so the band shows plane B and sprites there, as
through the HUD's own gaps. The sprite limits per line grow in proportion to the width. H40
screens (320 wide) are centred with 40-pixel black sides.

## The rooms

`wide_room()` in `native/wbmw.c` decides. Now: area 2, rooms 0–2 (`$FF9668`, `$FF9669`):

| Area, room | What | Width |
|---|---|---|
| 2, 0 | the first village | 1,024 |
| 2, 1 | the field with the statues | 512 |
| 2, 2 | the forest path east of the village | 768 |

A room needs a map at least 400 pixels wide (narrower rooms would show beyond their map: they
should stay 256 with black sides, as houses and shops are in most widescreen ports). Before
adding a room, look at its layers (`tools/rooms.py ROM --area A --room R`): screen layers (kind
2) are drawn wider like these; strip layers (kind 3, one screen repeated) are drawn whole
already. Rooms with layers that move by themselves, scroll per line, or carry things, and the
bosses and cutscenes, each need a look of their own.

## Checks

* With widescreen off, the native build is the same as before frame for frame (frame logs of RAM
  and VRAM, and the pictures, compared with the build before it over 2,000 frames of five
  states), and so is the harness, in all three modes, with no mismatches in shadow mode.
* With it on: the village, the forest path and the walk between them, the inside of the inn (the
  game draws a house as a picture over the village, so the village shows at its sides, as in the
  original), the title screen and a dungeon (black sides).

## Not done yet (stage 2 and on)

* **Things appear at the old edges.** Enemies and objects are created when they come within the
  original 256 (plus a margin) and removed when they leave it, so in the wide view they can
  appear or vanish in plain sight at the sides. Next: widen the range where they are created,
  removed and drawn (`OffscreenXAt`/`$BD0` and its users, `DespawnIfOffscreen` $48C4, the spawn
  rules in `port/spawns.c`, the sprite builder's culling).
* More rooms, one at a time, starting with the outdoor areas.
* The window opens 400 wide when widescreen is on at the start; switching it on later keeps the
  window's size (the picture fits inside it).
