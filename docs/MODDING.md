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
| `text/messages.txt`, `text/phrases.txt` | all the game's text, as text | Any length: see "Text" below. (`messages.json` and `phrases.json` are the same as the game had it, for reference; the pack reads the `.txt` files.) |
| `rooms/maps.json` | each layer's `blocks` (its block table: 4 tile words and an attribute a block) and `places` (16 x 12 block numbers each) | The game reads these already unpacked, so any block numbers work. |
| `rooms/lists.json` | the rooms' event lists (doors, exits, things), spawn lists (enemies and where), graphics slots, tile animations | Same number of entries. |
| `rooms/rooms.json`, `start_points.json`, `music.json` | room scripts, where the player appears, each room's music | |
| `sprites/animations.json` | animations (frames, durations) and sprite mappings (pieces) | |
| `sound/music.json`, `instruments.json` | the songs' and effects' notes and commands, the instruments | Same length; `tools/music.py` and `docs/SOUND.md` explain the format. |
| `graphics/png/block_NNN.png` | the 768 blocks of 32 tiles as pictures | See "Graphics" below. |

Not yet: making other things longer (lists, maps), new rooms. See "Next" below.

## Text

All the game's text is in two files, `text/messages.txt` and `text/phrases.txt`. Change the
words, build, and the game says what you wrote. A text can be any length.

* **Messages** (`messages.txt`) are what people say, signs, the story: 131 of them, each starting
  with a line `--- message N (window W)`. The game asks for them by number, so keep every number;
  W is the window it opens in (`text/windows.json`), which can be changed too.
* **Phrases** (`phrases.txt`) are texts the game shares: words many messages use, the things'
  names (table 1: `phrase 1.N` is thing N's name, as the status screen and shops show it), and
  whole conversations that many people have, like the shops' and the inn's (`phrase 3.0`,
  `phrase 3.1`). A phrase changed here changes everywhere it is used. `--- fragment N` are three
  texts phrases jump to.
* **What is in a text:**
  * the words, as they show; a new line is a new line. The game does not wrap lines: keep each
    line within its window (window 0, which most messages use, is 23 columns with its frame
    and margin: the game's own lines are up to 21 characters with the margin), and use `{MORE}` (wait for
    a button) to go on in the next box;
  * `{INDENT}` at the start of a line, and `{INDENT 2}`, set the left margin; `{SPACES n}` moves on
    n places;
  * `{END}` ends the text the window shows; text after it is reached only by a jump;
  * the other `{TAGS}` are the game's codes and commands, with their numbers: `{SPEED 6}`,
    `{ATTR 160}` (another colour, `{ATTR 128}` back), `{THING_NAME}`, `{GIVE 255}`, `{SET_FLAG 54}`,
    ... (`docs/EXTRACT.md`, "The text", and `tools/text.py` list them);
  * **jumps**: a choice's answer, a check (`{IF_FLAG 54 >2.1}`: if game flag 54 is set, go on at
    label 2.1) or a `{JUMP >...}` leads to a label, written `{@2.1}` where it leads. A label can be
    in another message. Move a label with the text it marks; a new label is any name (`{@mine}`,
    `>mine`) as long as it is put once;
  * `{PHRASE 3.0}`: a shared phrase that has jumps of its own, kept as it is (edit it in
    `phrases.txt`). Other phrases are written out in the messages: what you write is put back into
    phrases wherever it matches one, to save room.
  * Characters: ASCII, and the font's other signs as `tools/text.py` names them (Ⓐ, Ⅰ, kana);
    `{x7B}` for a sign with no name.
* **Room.** The text goes back where it was when it fits. When it does not (the messages grew
  past the room they had, or any phrase changed), the text moves past the end of the cartridge's
  data, with its tables, and the game finds it there. All the messages must stay within 32 KB.
* **Mistakes are named.** A tag that is not a code, a label put twice or nowhere, a character the
  font does not have: `pack.py` names the message and the place, and packs nothing.
* **Save states and text changes.** A quick save taken during a conversation remembers where in
  the text it was. After a text change, that place is somewhere else: finish conversations before
  taking a save you want to keep across text changes (the game's own save at the inn is not
  affected).

Example: in `phrases.txt`, phrase 3.0 is the shops' conversation. Change its first lines,
`You have a` / `discriminating eye.`, to `Ah, a fine choice,` / `young traveller!` and build: every
shopkeeper greets you so (the title screen's demo shows one).

## Graphics

The game's graphics are 768 blocks of 32 tiles (8 x 8 pixels each). Each block is a picture in
`graphics/png/`, `block_000.png` to `block_767.png`, to open in any paint program that keeps
indexed colours (Aseprite, GIMP, Photoshop in Indexed Color mode, Paint.NET with a plugin, ...).
Change it, save it under the same name, build, and the game draws your version everywhere it uses
those tiles.

* **Colours.** A tile pixel is a colour number, 0 to 15, from one of the palettes; the same tiles
  can be drawn with different palettes in different places. Each picture comes in the palette the
  game uses most for it (counted over every room's map and the sprites); `blocks.json` says which
  (`palette`, a record of `palettes/palettes.json`) and lists its 16 colours. Colour 0 is
  transparent, as in the game. Draw with these 16 only. Blocks the counting never sees (the
  title's, the ending's, the font) are in grey levels: colour number n is grey level n.
* **Keep it indexed** if you can: then the colour numbers are read directly. A picture saved in
  full colour (RGB) still works when every colour in it is one of the block's 16, matched exactly;
  where two of the 16 are the same colour, the first is taken. Any other colour is an error that
  names the picture and the pixel.
* **Keep the size.** A block drawn mostly as map tiles is 128 x 16: two rows of 16 tiles, so a map
  cell's 2 x 2 tiles (n, n+1, n+16, n+17) show whole. A block drawn mostly as sprites is 64 x 32:
  8 columns of 4 tiles going down, as a sprite's pieces take their tiles (down each column, up to 4
  tiles tall), so most characters show whole. `blocks.json` gives each block's `layout`.
* **To find a block**, look through the pictures (they are in colour), or see which blocks a room
  or a monster uses: `rooms/rooms.json` (a room's graphics slot lists), `sprites/enemies/enemies.json`
  (a monster type's `graphics`).
* **When it shows.** Graphics are loaded when a room loads: after a quick load, walk into another
  room (or start from the title) to see a change.

`block_NNN.bin` beside them is the same data as bytes (32 bytes a tile, 4 bits a pixel); the pack
takes the picture when there is one. `sheet.png` is all the blocks in grey, for looking only.

Example: Wonder Boy is drawn from blocks 8 to 11, in palette 0, where colours 2 and 3 are his blue.
Repaint those pixels with colours 11 and 12 and he has red hair from the next room on.

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
2. **Graphics as pictures** (done): the tiles as PNG files in their real colours, read back.
3. **Text as text** (done): `messages.txt` and `phrases.txt` read back, at any length.
4. **Levels**: longer lists (more enemies, doors, things in a room), then bigger maps and new
   rooms.
5. **Balance by name**: the monsters' records and the prices as named fields (a monster's name,
   a thing's name) instead of hex rows.
