# Extracting the game's data

The long-term idea (`docs/IDEAS.md`, "Extract the assets from the ROM once") is that the game
loads its data from ordinary files instead of reading it from the ROM as it plays: graphics as
images, maps and text as files that can be looked at and changed. This is the first step:
writing the data out, in readable forms, to check that every format is understood. The game does
not use these files yet.

## Running it

    python tools/extract.py "rom/Wonder Boy in Monster World (USA, Europe).md"

writes `assets/` (about 950 files, 6 MB, half a minute; `--no-rooms` skips drawing the rooms and
takes a few seconds). `assets/` is in `.gitignore`: the files are the game's data, as much as the
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
| `sound/z80_driver.bin`, `z80_data.bin` | the Z80 sound driver and what follows it, as they are | $98000 |

## The checks

* Graphics: all 726 chunks decode to exactly 1,024 bytes and end where the next begins (42 more
  entries are empty slots): `problems: 0`.
* Rooms: all 170 rooms of the 19 areas parse, and every map decodes: `problems: 0`.
* Text: all 131 messages decode to their end. Each holds every branch of what one character can
  say (jumps on flags, things owned, choices), up to where the next message begins.

## The text

Messages are bytes: `$10` and up are characters (ASCII for the Latin letters; they are the font's
tile numbers), `$0C`-`$0F` and a byte insert a phrase (a word or a few the messages share), and
`$00`-`$0B` are codes, `$0B` followed by one of 51 commands with its arguments (port/message.c,
`command`). In `messages.txt` the codes and commands are `{TAGS}` (`{MORE}` waits for a button,
`{IF_FLAG 54 187}` jumps 187 bytes on when game flag 54 is set, `{GIVE 255}` gives the thing named
before) and the phrases are written out; `messages.json` keeps them as numbers. 299 characters
are outside ASCII: the font's other glyphs (arrows, marks, the quiz's letters), written `{xNN}`.

## Next

* Name the glyphs outside ASCII from the font (blocks 0-5), so the text reads fully.
* Sprites and their animations (`tools/sprites.py` already reads them) as sheets and JSON.
* Decode the music and sound effects (the Z80 driver's data): the format is not known yet.
* Then the other way: build the same data back from the files and check it matches the ROM byte
  for byte, before the game reads anything from them.
