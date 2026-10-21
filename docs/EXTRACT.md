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
| `graphics/block_NNN.bin` | the 768 blocks of 32 tiles (1,024 bytes of VRAM data each), decoded | the table at $41000 (`tools/gfx.py`) |
| `graphics/sheet.png` | all of them, a row of 32 tiles a block, in grey levels | |
| `palettes/palettes.json`, `swatches.png` | the 160 palette records, 16 colours each | $599C |
| `rooms/rooms.json` | every room's script: layers, graphics, palettes, objects | the area blocks (`tools/rooms.py`) |
| `rooms/areaAA_roomRR.png` | every room's map, drawn with its own graphics and palettes | |
| `text/messages.txt`, `messages.json` | the 131 messages: readable, and parsed | $1CC14 (`tools/text.py`) |
| `text/phrases.txt` | the phrases messages share (4 tables) | $1CC10 |
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

reads the files back and builds the ROM's bytes from them. Each format has a parse and an encode
in `tools/assetfmt.py`, side by side; the JSON keeps whatever the bytes need (a pointer's form, a
terminator's value, a command's arguments). Everything must match:

| Data | Pieces | Bytes | How |
|---|---|---|---|
| Messages | 131 | 12,973 | encoded from `messages.json`'s parsed form |
| Phrases | 356 | 5,472 | from `phrases.json` |
| Palettes | 160 | 2,720 | from `palettes.json`'s colour words |
| Room scripts | 170 | 7,398 | from `rooms.json`'s items |
| Animation scripts | 774 | 6,808 | from `animations.json` |
| Sprite mappings | 964 | 12,264 | from `animations.json` |
| Sound | 2 | 30,583 | as they are |
| Graphics | 768 blocks | 786,432 decoded | compressed: the decoded blocks are compared with the port's own decompressor (`wbmw --verify-graphics assets`, `DecodeGfxBlock` $6BC4, which the harness verified against the original's): two decoders written apart agree |

Not built back yet: the room maps (compressed; the decoded maps are not written as data yet, only
drawn), and the tables that point at the data (the message and phrase tables, the graphics table
at $41000, the area blocks' room offsets, the animation tree's offset tables), which `rebuild.py`
takes from the ROM's layout for now.

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
* The room maps as data (block tables and cells), checked against the port's unpackers ($9F6A,
  $A068); the pointer tables built back too.
* Then the game reads its data from the files.
