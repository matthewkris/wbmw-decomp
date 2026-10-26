# Modding

The game plays from `assets/game.pak`, which `tools/pack.py` builds from the files in `assets/`
(`docs/EXTRACT.md`). Change a file there, build again, and the game plays with the change:

    make -C native                (or: python tools/pack.py)
    work/native/wbmw

`assets/` is your own copy of the game's data: it is git-ignored, and nothing in it is ever
committed. Keep a copy of your changes somewhere else too.

## Safety

* **Nothing you change is overwritten.** The build extracts the ROM into `assets/` only when there
  is none, or when a newer extractor writes a different format. Then the old folder is first moved
  aside as `assets-old-DATE-TIME/`, and the build says so: copy your changes over from there.
* **An edit that breaks other data is refused.** Every piece of data goes back at its address in
  the ROM's space, so a piece made longer would run into the next one. `pack.py` checks that no two
  pieces disagree about a byte, names them if they do, and packs nothing:

      pack: messages at $01E13D (21 bytes) and messages at $01E11C (38 bytes) disagree from $01E13D: one of them was made longer?

* **The game checks what it reads.** At exit, `wbmw` says whether every byte the game read came
  from the files ("every byte the game read is held by the files" with `--pack FILE`).

## What can be changed

For now, **each piece must keep its size**: a message as long as it was (or shorter), a list with
as many entries. Values can change freely.

| File | What to change | Notes |
|---|---|---|
| `palettes/palettes.json` | `words`: the 16 colours of each of the 160 palettes | Genesis colours, `0BGR` in hex with even digits `0`-`E` (`0E0E` is magenta). `rgb` is only for reading: it is not packed. A palette is loaded when a room (or screen) loads, so a loaded save state keeps the old colours until the next room. |
| `tables/code_tables.json` | `rows`: the values of the 447 tables in the code, in hex | The game's balance is here: `prices` (each thing's price, 4 bytes, gold), `kind_fighting` (each monster kind's record: health / 32, level, damage, points), `kind_movement` (speeds), `sword_swing` (the sword's hit points), `demo_input`, the colour scripts. `note` and `used_by` say what each table is and which routine reads it. |
| `text/messages.json` | `parsed`: each message as characters, phrases and codes | Not `messages.txt`: that one is written for reading and is not read back yet (the text phase below). A character is `["char", code]`, ASCII for letters. |
| `rooms/maps.json` | each layer's `blocks` (its block table: 4 tile words and an attribute a block) and `places` (16 x 12 block numbers each) | The game reads these already unpacked, so any block numbers work. |
| `rooms/lists.json` | the rooms' event lists (doors, exits, things), spawn lists (enemies and where), graphics slots, tile animations | Same number of entries. |
| `rooms/rooms.json`, `start_points.json`, `music.json` | room scripts, where the player appears, each room's music | |
| `sprites/animations.json` | animations (frames, durations) and sprite mappings (pieces) | |
| `sound/music.json`, `instruments.json` | the songs' and effects' notes and commands, the instruments | Same length; `tools/music.py` and `docs/SOUND.md` explain the format. |
| `graphics/block_NNN.bin` | the 768 blocks of 32 tiles, 1,024 bytes each (8 x 8 pixels a tile, 4 bits a pixel, a colour number) | Any change works, but there is no picture editing yet: `sheet.png` is only for looking at (the graphics phase below). |

Not yet: making anything longer, new rooms or bigger maps, editing the graphics as pictures, and
editing the text as text. See "Next" below.

## Examples

**A colour.** In `palettes/palettes.json`, set colour 2 of every palette to `0E0E` and build: from
the title screen on, everything drawn in colour 2 is magenta.

**A price.** `prices` in `tables/code_tables.json` holds each thing's price as 4 bytes, in the
order of the things' numbers: the first row's `00 00 4D 58` is thing 0's price, $4D58 = 19,800
gold. Write `00 00 00 0A` there and that thing costs 10 gold in the shops.

**A monster's health.** `kind_fighting` starts with a word for each group of monster types, the
offset of the group's records from the table's start; a monster's record follows at its kind's
number times 3: health divided by 32 (a byte), level (a byte), damage (a word), points (a word).
The types are named `GGKK` (group, kind) in `sprites/enemies/enemies.json`, with the room each
first appears in and its picture, `sprites/enemies/enemy_GGKK.png`. A monster reads its record
when it appears, so enter its room again to see the change (its health bar shows it). Step 5
below will make this a named field.

## Next

The plan, in the order the owner chose (2026-10-26):

1. **Safety and this guide** (done).
2. **Graphics as pictures**: the tiles written as PNG files in their real colours, and a changed
   picture read back into the blocks.
3. **Text as text**: `messages.txt` read back, and messages of any length (the data moved where
   it fits, and the tables that point at it updated).
4. **Levels**: longer lists (more enemies, doors, things in a room), then bigger maps and new
   rooms.
5. **Balance by name**: the monsters' records and the prices as named fields (a monster's name,
   a thing's name) instead of hex rows.
