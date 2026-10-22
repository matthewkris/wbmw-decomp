# Talk zoom

The native build can zoom in on Wonder Boy and the one he is talking to while a conversation
runs (menu: **Talk zoom**; `--no-talk-zoom` or `talk_zoom=0` in `wbmw.ini` turns it off). It is on
by default, and experimental. It is all done in the host (`native/wbmw.c`, as the picture goes to the
window): the game's code and RAM are untouched, so the harness's comparisons are unaffected.

## When

* **A conversation:** the sequence `$C1E8` sets `$9659` bit 3 while it runs, and its message is
  open while `$8CA4` bit 7 is set. Signs and the "Found ..." boxes are not conversations.
* **With an ordinary character.** The speaker is the character under the box's pointer (two
  tiles in plane A's row above the box, drawn from the message's data over the one speaking; only
  tiles that differ from the map kept from before the box count, as the scenery has tiles in that
  row too: the top of area 4's temple, at first taken for a 96-pixel pointer aimed at no one),
  or, for a box without a pointer, the one Wonder Boy could have talked to (within its reach,
  `-$3500`, and level with him, as `$164B8` has it) whatever its size, else the person nearest
  him (within 64 pixels, at least 24 tall by its size, so not Priscilla). The reach came second:
  a character 22 lines tall in area 6, standing where Wonder Boy stands, was at first too small to
  count. No zoom when there is none (the Sphinx is drawn in the
  background), or when the speaker is taller than 48 pixels (a boss: they fill the screen as it
  is). A character's size is its `-$34FD` (half its height).
* Not in H40 screens, and not for a conversation already open when a state is loaded (see below).

## How

* The box: the message's window, at column `$8D92`, row `$8D94` (counted from below the HUD's
  64 lines), `$8D96` × `$8D98` tiles; on plane A at `$8D9A`, `$8D9C` (scrolled).
* The zoom eases in over about a second (6% of the way each frame; at first 12%, which felt
  hurried), to at most 1.5×, about an anchor between Wonder Boy and the speaker, at
  Wonder Boy's feet, so the two grow upward. The HUD slides up out of the picture and the box
  with it, as far as the zoomed play field reaches (all 64 lines from 1.4× up), so the scene
  takes the height; the zoom is as large as keeps their heads (about 28 lines above their feet)
  below the box. Only the play field's lines (64 down) are ever shown: the lines the HUD covers
  have nothing of the scene (plane B, and in widescreen the band's purple sides), so near the top
  the scene is moved up a little rather than show them (`zoom_layout`). The box and its pointer stay at their
  own size, the pointer moved over the speaker as the zoom moves him. When the conversation
  ends, it all eases back.
* The box is drawn into plane A, so the zoomed scene cannot be the frame itself (the box would
  be zoomed too). The host keeps plane A's map each frame while there is no conversation, and
  while zoomed renders the scene a second time with that map, and without the window (the HUD's
  band, whose lines the zoom brings into the picture: they must be the scenery's): the scene
  without the box. A state loaded mid-conversation has no such map, so that conversation is not
  zoomed.
* The box and its pointer are drawn over it from their own tiles (plane A's, decoded with the
  palette), their transparent pixels left transparent. A copy of the picture there would bring
  the unzoomed scene with them, around the frame's corners and the pointer.
* The zoom is drawn by SDL at the window's resolution, so it is smooth.

## Testing

`--input FILE` works in a window too (with the keyboard and controller), and
`WBMW_SHOT_EVERY=N WBMW_SHOT_EVERY_DIR=DIR` writes the window's own picture every N frames. Tried:
the girl in the village's house (no pointer: zoomed in and back out), the Sphinx (not zoomed),
and the inn from a state loaded mid-conversation (not zoomed, as above).
