# Extracting the game's data

The long-term idea (`docs/IDEAS.md`, "Extract the assets from the ROM once") is that the game
loads its data from ordinary files instead of reading it from the ROM as it plays: graphics as
images, maps and text as files that can be looked at and changed. `tools/extract.py` writes the
data out in readable forms, `tools/rebuild.py` checks that the files hold all of it, and
`tools/pack.py` packs them for the game, which plays from them instead of the ROM (the build
makes the pack: "The game reads the files" below).

## Running it

    python tools/extract.py "rom/Wonder Boy in Monster World (USA, Europe).md"

writes `assets/` (about 1,280 files, 14 MB, a minute or so; `--no-rooms` skips drawing the rooms
and takes a few seconds). `assets/` is in `.gitignore`: the files are the game's data, as much as the
ROM is, and are never committed.

## What it writes

| Path | What | From |
|---|---|---|
| `manifest.json` | the ROM's name, size and SHA-1, the decoders' checks | |
| `layout.json` | the tables that point at the data: the data directory, the phrase tables' bases, the animation tree's offset tables | $1CC00, $1CC10, $A0000 |
| `graphics/block_NNN.bin` | the 768 blocks of 32 tiles (1,024 bytes of VRAM data each), decoded | the table at $41000 (`tools/gfx.py`) |
| `graphics/sheet.png` | all of them, a row of 32 tiles a block, in grey levels | |
| `graphics/png/block_NNN.png`, `blocks.json` | each block as an indexed picture to edit, in the palette the game draws it with most (counted over the rooms' maps and the sprites), laid out as map tiles (128 x 16) or as sprite tiles (64 x 32); the pack reads them back (`tools/gfxpng.py`, `docs/MODDING.md`) | |
| `graphics/table.json` | the graphics table: each block's format and address | $41000 |
| `palettes/palettes.json`, `swatches.png` | the 160 palette records, 16 colours each | $599C |
| `rooms/rooms.json` | every room's script: layers, graphics, palettes, objects | the area blocks (`tools/rooms.py`) |
| `rooms/areaAA_roomRR.png` | every room's map, drawn with its own graphics and palettes | |
| `rooms/maps.json` | every layer's map as data: its block table (4 tile words and an attribute a block) and its chunks (16 x 12 block numbers a place on the map) | the layers' data ($9F6A, $A068) |
| `rooms/shared_blocks.json` | the three block tables every room shares | the directory's table 1 |
| `rooms/lists.json` | the other lists room scripts point at: 199 event lists (exits, doors, things, jumps on flags), 139 spawn lists (the enemies), 71 graphics slot lists, 33 tile animations; and the 60 pictures, 9 item lists and 12 townspeople's walk scripts they point at | room commands 2, 4, 5, 8, 11 |
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
| `sprites/enemies/enemy_TTTT.png`, `enemies.json` | each of the 100 enemy types the rooms load, its animations drawn with the graphics and palettes of the first room it appears in, and its own palette in line 3 where it has one (41 of them: the first byte of its record, as the game loads it; `palette` in `enemies.json`) | the enemy types at $1BC0E |
| `sprites/rooms/areaAA_roomRR.png` | every room with its enemies at their spawn points | |
| `sound/z80_driver.bin` | the Z80 sound driver's code, as it is ($0000-$107A) | $98000 |
| `sound/instruments.json` | the driver's tables: frequencies, attenuation, 51 FM and 17 PSG instruments, 119 operator envelopes, PSG envelopes, modulation scripts, 22 drums (`tools/soundtables.py`, `docs/SOUND.md`) | $9907B (Z80 $107B) |
| `sound/music.json`, `music.txt` | the music and sound effects: the sound table, the 88 sounds' headers (26 songs, 62 effects), their 636 tracks and sections as notes and commands, and 12 unused tracks; each song's places and each effect's name and use; `music.txt` readable, notes by name (`tools/music.py`, `docs/SOUND.md`) | $99A77 (Z80 $9A77, the sound table at $9B01) |
| `sound/midi/song_NN.mid`, `sound_NN.mid` | the songs and effects as MIDI files, written by the build from `music.json` and `instruments.json` (`tools/midi.py`, `docs/SOUND.md`) | |
| `tables/code_tables.json` | the 447 tables in the code region (the data among the code): each one's name, place, size, the routines that point at it, a note, and its values in hex | the code region (`tools/codetables.py`) |

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
| Data directory | 30 longs | from `layout.json` |
| Graphics table | 768 longs | from `graphics/table.json` |
| Message, window and phrase tables | | from the messages' windows and addresses, `windows.json`, the phrases' addresses |
| Areas' room tables | 19 | from the rooms' addresses |
| Animation tree | 225 tables | from `layout.json` |
| Sound driver code | 1 | as it is |
| Sound driver tables | 1 | from `instruments.json`: byte for byte |
| Music and effects | 737 | from `music.json`: byte for byte |
| Graphics | 768 blocks | compressed: compared by content with the port's own decompressor (`wbmw --verify-graphics assets`, `DecodeGfxBlock` $6BC4) |
| Room block tables, layer maps | 201, 238 | compressed: compared by content with the port's own unpackers (`wbmw --unpack-list`, `UnpackBlocks` $9F6A and `UnpackLayerMap` $A068); the ending's scenes use rooms' layers |
| The rooms' lists | 199 event lists, 139 spawn lists, 71 slot lists, 33 tile animations, 60 pictures, 9 item lists | encoded back from `lists.json`; each read as its interpreter reads it (the event lists by $ABBE and $A3D8, the spawn lists by $12848, ...), and a code the interpreter does not know is counted as a sign of a misread list: none |
| Scenes, houses' pictures | 6, 29 records | from their fields (the houses' records partly as they are: not every field is named) |
| Extra raw tiles | 20 x 192 bytes | as they are |
| Text fragments, start points, room music | 3, 138, 19 areas | from their fields |
| Walk scripts | 12 | from `lists.json`: byte for byte |
| Tables in the code region | 447 | from their values: byte for byte |

The port's decoders were verified against the original's code in the harness, and the Python
decoders were written apart from them: two decoders agreeing on every block and every map. (The
compressed data is not packed back to the same bytes: that would need the original packer. The
game will read the decoded data instead.)

At the end `rebuild.py` counts which of the ROM's bytes the checks cover:

| ROM | Bytes |
|---|---|
| built back byte for byte | 123,035 |
| checked by content (graphics, the rooms' maps) | 440,984 |
| padding ($FF) | 140,320 |
| the 68000 code ($0-$1C601; its tables are built back) | 95,358 |
| data not covered yet | 2,729 |

So over 99% of the data outside the code region is shown to come back from the files. What is
not: hundreds of small pieces (the largest 283 bytes), the waves one spawn point points at, and
chunks the maps' tables list that no place uses. None of it is read in the runs below.

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
| read and held by the files | 166,169: all of them |

Getting there took, besides the tables in the code region below:

* The data directory has 30 entries, not 27: the last three point at the three block tables every
  room shares.
* The townspeople's walk scripts (`rooms/lists.json`, `walk_scripts`): a spawn point of kind 5
  points at one for handler 11 ($16598): wait, walk a distance, walk home, speak, and a jump back.
* Animations that go on after a stop: a stop (`$FD`, or a frame of duration 0) keeps its place,
  and when the animation's timer is set again it carries on from the next byte (`UpdateAnim`,
  $80E). `animations.json` keeps each such continuation as its own script (`00.00.1E+`, with
  `continues`) where it fills the bytes up to the next script exactly: 12 of them.
* The rooms' compressed maps are counted by the bytes the port's unpackers read (`wbmw
  --unpack-list` writes them to `OUT.reads`) instead of by estimating where each one ends: the
  unpackers read a few bytes past where the estimate stopped.

### The tables in the code region

The game's code holds tables of its own: the colour scripts, the monsters' records, the title's
layouts, the sine and tangent tables, the prices, the demo's input, the ending's scripts and so
on. `tools/codetables.py` lists all 447 of them: 18,148 bytes, every place the disassembler's
code/data map (`tools/fixpoint.sh`) has data, jump tables included (the C reads their offsets as
data). Each one runs from an address an instruction points at to the next such address or the
next code, so a table is written whole even where the runs read only part of it. All of them, not
only the 215 the recorded runs read: the first runs from the pack found a jump table at $FE26 (the
companion's states) that no recorded run had reached. Bytes another file already holds (the palettes at $599C) are left out. The list keeps
only addresses, sizes and names: the values are the ROM's, and `extract.py` writes them to
`tables/code_tables.json`.

Two need a word:

* `owned_thresholds` ($24D8, 4 bytes): the disassembler took it for an unreferenced `bra`; it is
  the table of thresholds `$23E4` reads.
* `before_back_palettes` ($C366, 8 bytes): the back palette list at $C36E is read with a negative
  index too, so the 8 bytes of code before it are read as data.

The names come from the port's own names where it has one (`HANDLERS`, `TAN_TABLE`, ...), are
given for the larger tables, and are `table_XXXXXX` for the rest, with a note from the port's
comment on the routine that reads it.

## The game reads the files

    python tools/pack.py                      (after extract.py: writes assets/game.pak)
    work/native/wbmw                          (plays from assets/game.pak when it finds one)

The build (`make -C native`) runs `tools/makepack.sh`, which extracts the ROM into `assets/` when
`assets/` is missing or was written by another version of the extractor (its manifest's
`format`, `extract.FORMAT`), and packs `assets/` when the pack is missing or older than the files
and tools it is made from. Otherwise `assets/` is left as it is, so files changed by hand are
packed rather than overwritten; when it does extract again over an `assets/`, it first moves that
one aside as `assets-old-DATE-TIME/`. `pack.py` refuses pieces that disagree about a byte (an
edit made one longer). What can be changed: `docs/MODDING.md`. Without a ROM or without Python it makes nothing, and the game
reads the ROM. The game plays from `assets/game.pak` (or `game.pak` beside it) unless a ROM is
named on the command line; `--pack FILE` names a pack, and then says at exit that every byte read
was held, as well as when one was not.

`tools/pack.py` packs the files into the one file the game loads, reading nothing from the ROM:

* the data image: every piece the files hold, built by the same code as `rebuild.py`'s check
  (`rebuild.build_pieces`), at its place in the ROM's address space, and the raw graphics blocks.
  The game's C reads its data at the ROM's addresses, so it reads the image instead, unchanged;
* its mask: the 143,515 bytes the files hold. A read of any other byte is counted and reported
  when the game exits ("the game read N bytes the files do not hold"); it should never happen;
* the compressed data's decoded form, which is not in the image: the 768 graphics blocks, the 201
  block tables and the 238 layers' maps, by address. With the pack loaded, `DecodeGfxBlock` ($6BC4)
  copies a compressed block from it, and `UnpackBlocks` ($9F6A) and `UnpackLayerMap` ($A068) copy
  the tables and chunks (`port_pack_find`, `port/port.h`). A layer's places are kept in its
  data's order, which numbers its chunks (`rebuild.py` checks it).

The sound driver and its data go into the image as they are (`sound/*.bin`), so the music works
the same until its format is decoded.

Checked against the ROM's runs by game frame (`--frame-log`, `tools/framecmp.py`), with the
decoders' scratch RAM left out of the hash (`--frame-log-skip`: the decoder's rows and Huffman
tree at $FF8DE8-$FF8E07 and $FF8F0A-$FF8FFF, the unpacker's $FF970C-$FF9723 and $FF9862-$FF9961,
which the pack's copies do not write): 6,000 frames from power-on (the title and the demo) and 600
frames from each of the 98 save states. RAM, VRAM, CRAM and VSRAM are identical in every frame,
and every byte the game read is held by the files. (Four of the states never reach the frame wait
the log samples at, in either run.)

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

* The enemies' category 2 types (spawners with their own layouts) are not decoded; none of the
  rooms' lists needs them for drawing so far.

## Next

* The sound driver's instruments' last unnamed bytes (`docs/SOUND.md`).
* Raise `extract.FORMAT` whenever what `extract.py` writes changes, so the build extracts again.
