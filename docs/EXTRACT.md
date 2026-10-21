# Extracting the game's data

The long-term idea (`docs/IDEAS.md`, "Extract the assets from the ROM once") is that the game
loads its data from ordinary files instead of reading it from the ROM as it plays: graphics as
images, maps and text as files that can be looked at and changed. This is the first step:
writing the data out, in readable forms, to check that every format is understood. The game does
not use these files yet.

## Running it

    python tools/extract.py "rom/Wonder Boy in Monster World (USA, Europe).md"

writes `assets/` (about 1,200 files, 9 MB, a minute or so; `--no-rooms` skips drawing the rooms
and takes a few seconds). `assets/` is in `.gitignore`: the files are the game's data, as much as the
ROM is, and are never committed.

## What it writes

| Path | What | From |
|---|---|---|
| `manifest.json` | the ROM's name, size and SHA-1, the decoders' checks | |
| `layout.json` | the tables that point at the data: the data directory, the phrase tables' bases, the animation tree's offset tables | $1CC00, $1CC10, $A0000 |
| `graphics/block_NNN.bin` | the 768 blocks of 32 tiles (1,024 bytes of VRAM data each), decoded | the table at $41000 (`tools/gfx.py`) |
| `graphics/sheet.png` | all of them, a row of 32 tiles a block, in grey levels | |
| `graphics/table.json` | the graphics table: each block's format and address | $41000 |
| `palettes/palettes.json`, `swatches.png` | the 160 palette records, 16 colours each | $599C |
| `rooms/rooms.json` | every room's script: layers, graphics, palettes, objects | the area blocks (`tools/rooms.py`) |
| `rooms/areaAA_roomRR.png` | every room's map, drawn with its own graphics and palettes | |
| `rooms/maps.json` | every layer's map as data: its block table (4 tile words and an attribute a block) and its chunks (16 x 12 block numbers a place on the map) | the layers' data ($9F6A, $A068) |
| `rooms/shared_blocks.json` | the three block tables every room shares | the directory's table 1 |
| `rooms/lists.json` | the other lists room scripts point at: 199 event lists (exits, doors, things, jumps on flags), 139 spawn lists (the enemies), 71 graphics slot lists, 33 tile animations; and the 60 pictures and 9 item lists they point at | room commands 2, 4, 5, 8, 11 |
| `rooms/scenes.json` | the ending's six scenes: each one's layers, palette and graphics | the directory's table 7 ($96D6) |
| `rooms/house_pictures.json` | the 29 houses' and shops' pictures: place and size, frame, triggers, things | the directory's table 2 ($B986) |
| `graphics/extra_NNN.bin` | the 6 raw tiles after each of the 20 raw blocks, which `CopyRawTiles` copies with it (the shields) | |
| `text/messages.txt`, `messages.json` | the 131 messages: readable, and parsed | $1CC14 (`tools/text.py`) |
| `text/phrases.txt`, `phrases.json` | the phrases messages share (4 tables) | $1CC10 |
| `text/fragments.json` | three texts messages jump to that lie outside them (after the phrases) | |
| `rooms/start_points.json` | the 138 start points: where the player appears after a door, an exit or a new game (area, room, kind, place, a word) | the directory's table 0 ($A37E) |
| `rooms/music.json` | each area's rooms' music | the directory's table 3 ($92D4) |
| `text/windows.json` | the message windows' places and sizes | $1CC18 |
| `sprites/animations.json` | the 12 animation sets: groups, animations (frames, durations, commands), and the 962 sprite mappings piece by piece | $A0000 (`tools/sprites.py`) |
| `sprites/player.png` | Shion's animations, a row each | |
| `sprites/enemies/enemy_TTTT.png`, `enemies.json` | each of the 100 enemy types the rooms load, its animations drawn with the graphics and palettes of the first room it appears in | the enemy types at $1BC0E |
| `sprites/rooms/areaAA_roomRR.png` | every room with its enemies at their spawn points | |
| `sound/z80_driver.bin`, `z80_data.bin` | the Z80 sound driver and what follows it, as they are | $98000 |

## The checks

* Graphics: all 726 chunks decode to exactly 1,024 bytes and end where the next begins (42 more
  entries are empty slots): `problems: 0`.
* Rooms: all 170 rooms of the 19 areas parse, and every map decodes: `problems: 0`.
* Text: all 131 messages decode to their end. Each holds every branch of what one character can
  say (jumps on flags, things owned, choices), up to where the next message begins (the last, up
  to the phrases' tables). Every character is named (`unnamed_glyphs: 0`).
* Sprites: all 774 animations and their 962 mappings parse: `problems: 0`; all 100 enemy types the
  rooms load are drawn.

## Building it back: are the files complete?

    python tools/rebuild.py "rom/Wonder Boy in Monster World (USA, Europe).md"

reads the files back and checks them against the ROM (it needs the native build for the
compressed data: `make -C native`). Each structured format has a parse and an encode in
`tools/assetfmt.py`, side by side; the JSON keeps whatever the bytes need (a pointer's form, a
terminator's value, a command's arguments). Everything must match:

| Data | Pieces | How |
|---|---|---|
| Messages, phrases | 131, 356 | encoded back from the parsed text: byte for byte |
| Palettes | 160 | from the colour words |
| Room scripts | 170 | from `rooms.json`'s items |
| Animation scripts, sprite mappings | 774, 964 | from `animations.json` |
| Data directory | 27 longs | from `layout.json` |
| Graphics table | 768 longs | from `graphics/table.json` |
| Message, window and phrase tables | | from the messages' windows and addresses, `windows.json`, the phrases' addresses |
| Areas' room tables | 19 | from the rooms' addresses |
| Animation tree | 225 tables | from `layout.json` |
| Sound | 2 | as they are (not understood yet: the music's format is still unknown) |
| Graphics | 768 blocks | compressed: compared by content with the port's own decompressor (`wbmw --verify-graphics assets`, `DecodeGfxBlock` $6BC4) |
| Room block tables, layer maps | 201, 238 | compressed: compared by content with the port's own unpackers (`wbmw --unpack-list`, `UnpackBlocks` $9F6A and `UnpackLayerMap` $A068); the ending's scenes use rooms' layers |
| The rooms' lists | 199 event lists, 139 spawn lists, 71 slot lists, 33 tile animations, 60 pictures, 9 item lists | encoded back from `lists.json`; each read as its interpreter reads it (the event lists by $ABBE and $A3D8, the spawn lists by $12848, ...), and a code the interpreter does not know is counted as a sign of a misread list: none |
| Scenes, houses' pictures | 6, 29 records | from their fields (the houses' records partly as they are: not every field is named) |
| Extra raw tiles | 20 x 192 bytes | as they are |
| Text fragments, start points, room music | 3, 138, 19 areas | from their fields |

The port's decoders were verified against the original's code in the harness, and the Python
decoders were written apart from them: two decoders agreeing on every block and every map. (The
compressed data is not packed back to the same bytes: that would need the original packer. The
game will read the decoded data instead.)

At the end `rebuild.py` counts which of the ROM's bytes the checks cover:

| ROM | Bytes |
|---|---|
| built back byte for byte | 104,685 |
| checked by content (graphics, the rooms' maps) | 440,535 |
| padding ($FF) | 140,320 |
| the 68000 code and the tables in it ($0-$1C601) | 113,506 |
| data not covered yet | 3,380 |

So over 99% of the data outside the code region is shown to come back from the files. What is
not: hundreds of small pieces (the largest 283 bytes), the paths and waves some spawn points point
at, and chunks the maps' tables list that no place uses.

### What the game reads

Coverage counts bytes, but what matters for the game loading its data from the files is what it
reads. The native build's `--rom-reads FILE` marks every ROM byte read (by the C, by the VDP's DMA,
by the Z80 through its window: all through `harness/bus.c`), adding to the file, so that runs from
many states add up; `rebuild.py --reads FILE` then lists what was read that no file holds. In the
native build no 68000 code runs, so a read in the code region is a table read as data.

Over a run from power-on through the title and the opening, and 2,000 frames of play from each of
the 107 save states:

| | Bytes |
|---|---|
| ROM bytes read | 166,169 |
| read and held by the files | 159,457 |
| read, not held, data region | 96 (the paths some spawn points point at, a few bytes elsewhere) |
| read, not held, code region (tables) | 6,616 |

The tables in the code region are the last thing between the files and the game reading them:
the largest at $6616 (761 bytes), $72F0 (596, the title's menu), $1B5DC (310), $1B96C (262), $1106
(250), $8CA2 (224), $1AC82 (223), $4E5C (172, the demo), $6D1C (128: the graphics decoder's
planar-to-chunky tables). A feature none of these runs reached could read others.

## The text

Messages are bytes: `$10` and up are characters (ASCII for the Latin letters; they are the font's
tile numbers), `$0C`-`$0F` and a byte insert a phrase (a word or a few the messages share), and
`$00`-`$0B` are codes, `$0B` followed by one of 51 commands with its arguments (port/message.c,
`command`). In `messages.txt` the codes and commands are `{TAGS}` (`{MORE}` waits for a button,
`{IF_FLAG 54 187}` jumps 187 bytes on when game flag 54 is set, `{GIVE 255}` gives the thing named
before, `{CHOICE 3 8 n}` a choice of two whose second answer's text is n bytes on) and the
phrases are written out; `messages.json` keeps them as numbers. The font's glyphs outside ASCII
(read through the graphics slots at $96BC, the font being in VRAM's first blocks) are named:
`$10`-`$12` the quiz's Ⓐ Ⓑ Ⓒ, `$13`-`$18` marks, `$19`-`$1F` Roman numerals, `$A0`-`$FD` kana left
from the Japanese release (`$FE`/`$FF` put ゛ or ゜ over the next one); `$7F`-`$9F` are pieces of
pictures and `$C0`-`$C5`, `$D0` icons, left as `{xNN}` (one phrase is made of them).

## Known gaps

* Enemies are drawn with the palettes of the first room they appear in. Bosses load palettes of
  their own when they appear (the Myconid's sheet has its shapes right and its colours wrong).
* The enemies' category 2 types (spawners with their own layouts) are not decoded; none of the
  rooms' lists needs them for drawing so far.

## Next

* The bosses' own palettes, for their sheets.
* Decode the music and sound effects (the Z80 driver's data): the format is not known yet.
* The tables in the code region the game reads (`rebuild.py --reads`): each named and written
  out, then built back like the rest.
* Then the game reads its data from the files.
