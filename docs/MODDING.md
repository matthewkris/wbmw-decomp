# Modding

The game plays from `assets/game.pak`, which `tools/pack.py` builds from the files in `assets/`
(`docs/EXTRACT.md`). Change a file there, build again, and the game plays with the change:

    make -C native                (or: python tools/pack.py)
    work/native/wbmw

`assets/` is your own copy of the game's data: it is git-ignored, and nothing in it is ever
committed. Keep a copy of your changes somewhere else too.

## Safety

* **Nothing you change is overwritten.** The build extracts the ROM into `assets/` only when there
  is none, or when a newer extractor writes a different format. Then, if you changed anything in
  it, a copy of the old folder is first kept as `assets-old-DATE-TIME/`, and the build says so: copy
  your changes back from there.
* **An edit that breaks other data is refused.** What cannot move goes back at its address in the
  ROM's space, so a piece made longer would run into the next one. `pack.py` checks that no two
  pieces disagree about a byte, names them if they do, and packs nothing:

      pack: messages at $01E13D (21 bytes) and messages at $01E11C (38 bytes) disagree from $01E13D: one of them was made longer?

  The text, the rooms' lists and the room scripts are the exceptions: they move where there is
  room (see their sections).
* **The game checks what it reads.** At exit, `wbmw` says whether every byte the game read came
  from the files ("every byte the game read is held by the files" with `--pack FILE`).
* **Quick saves remember places.** A quick save taken in the middle of a conversation, or inside a
  room, remembers where that text or that room's lists were. After you change them they may be
  elsewhere: load a save taken outside them (the game's own save at the inn is not affected).

## What can be changed

| File | What to change | Notes |
|---|---|---|
| `graphics/png/block_NNN.png` | the 768 blocks of 32 tiles as pictures | See "Graphics". |
| `text/messages.txt`, `text/phrases.txt` | all the game's text, as text | Any length: see "Text". (`messages.json` and `phrases.json` are the text as the game had it, for reference; the pack reads the `.txt` files.) |
| `rooms/lists.json`, `rooms/rooms.json` | the rooms' lists (enemies, doors, things) and scripts | Any length: see "Levels". |
| `tables/balance.json` | the monsters' health, defence and damage, and the things' prices, by name | See "Balance". |
| `palettes/palettes.json` | `words`: the 16 colours of each of the 160 palettes | Genesis colours, `0BGR` in hex with even digits `0`-`E` (`0E0E` is magenta). `rgb` is only for reading: it is not packed. A palette is loaded when a room (or screen) loads. |
| `rooms/tiled/*.tmx` | the rooms' maps, for Tiled | See "Maps". |
| `rooms/maps.json` | each layer's `blocks` (its block table: 4 tile words and an attribute a block: what a block looks like and how solid it is) and `places` | The maps themselves are best edited in Tiled ("Maps"); the blocks here, the same number of them. |
| `rooms/start_points.json`, `music.json` | where the player appears, each room's music | |
| `sprites/animations.json` | animations (frames, durations) and sprite mappings (pieces) | The same length. |
| `sound/music.json`, `instruments.json` | the songs' and effects' notes and commands, the instruments | The same length; `tools/music.py` and `docs/SOUND.md` explain the format. |
| `tables/code_tables.json` | `rows`: the values of the 447 tables in the code, in hex | More of the game's workings: `kind_movement` (the monsters' speeds), `sword_swing` (the sword's hit points), `demo_input`, the colour scripts, ... `note` and `used_by` say what each table is and which routine reads it. `balance.json` is written over `kind_fighting` and `prices`. |

Not yet: new rooms (rebuild an existing one instead: "Maps"). See "Next".

A quick one to start: in `palettes/palettes.json`, set colour 2 of every palette to `0E0E` and
build: from the title screen on, everything drawn in colour 2 is magenta.

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
    line within its window (window 0, which most messages use, is 23 columns with its frame and
    margin: the game's own lines are up to 21 characters with the margin), and use `{MORE}` (wait
    for a button) to go on in the next box;
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
    phrases wherever it matches one, to save room;
  * characters: ASCII, and the font's other signs as `tools/text.py` names them (Ⓐ, Ⅰ, kana);
    `{x7B}` for a sign with no name.
* **Room.** The text goes back where it was when it fits. When it does not (the messages grew
  past the room they had, or any phrase changed), the text moves past the end of the cartridge's
  data, with its tables, and the game finds it there. All the messages must stay within 32 KB.
* **Mistakes are named.** A tag that is not a code, a label put twice or nowhere, a character the
  font does not have: `pack.py` names the message and the place, and packs nothing.

Example: in `phrases.txt`, phrase 3.0 is the shops' conversation. Change its first lines,
`You have a` / `discriminating eye.`, to `Ah, a fine choice,` / `young traveller!` and build: every
shopkeeper greets you so (the title screen's demo shows one).

## Levels

A room is its script (`rooms/rooms.json`) and the lists the script points at
(`rooms/lists.json`, by address): which enemies appear and where (a spawn list), doors, exits,
chests and other things (an event list), which graphics it loads (a graphics slot list), its
animated tiles. Lists and scripts can be any length: add enemies, take some away, add things.

* **Spawn lists** (`spawns`): first `words`, the kinds of enemy whose graphics the room loads
  (a negative word ends them; a kind you add must be among them), then the spawn points, each a
  `code` (how it appears: 0-15) and its `args`; for most codes the last three are the enemy's
  kind, x and y, in the room's coordinates as its neighbours have them. Copy an entry and change
  its x to add one more. Codes $F8-$FE are commands (some jump on game flags) and $FF the end.
  `sprites/enemies/enemies.json` names the kinds (`CCKK`).
* **Event lists** (`events`): the room's doors, exits, chests, signs, things to pick up and their
  conditions, read by the game as `tools/assetfmt.py` (`event_fmt`) lays them out.
* **Jumps and pointers by name.** A jump in a list goes to an entry by its label: `{"j": "L9"}`
  goes to the entry with `"label": "L9"` (`"end"`: past the last). Move the label with its
  entry; a new label is any name, put once. An entry that points at other data names it:
  `{"o": "walk_scripts/026780"}` is the walk script of `lists.json` at that key (item lists,
  pictures and walk scripts the same way); `{"at": "XXXXXX"}` is data at an address.
* **Room scripts** (`rooms.json`, `items`): the commands that set a room up, in order: its layers,
  palette, graphics, lists (`args` with `flag`: 0 an offset from the area's block, 1 an address
  in `long`), objects. `script` beside them is the same, read for you (addresses worked out); the
  pack reads `items`.
* **Room.** A list or script that grew moves past the end of the cartridge's data, taking along a
  copy of the data it points at; a room script then points at it by address, and its whole area
  (all its rooms' scripts and the table of them) moves too. The houses' and shops' lists are pointed
  at by the houses' table: when one of them moves, the table moves too, with a copy of each.
  `pack.py` says what moved.
* **Seeing it.** A room reads its lists when it loads: walk into it. The title screen's demo plays
  in area 2, room 0 (the first village): its spawn list is the one at `026714`. To see every room
  with its enemies, extract into another folder (`python tools/extract.py ROM work/view`, without
  `--no-rooms`): `sprites/rooms/` has them.

Example: in the village's spawn list (`spawns`, `026714`), copy the entry
`{"code": 2, "args": [0, 112, 2, 6, 4416, 4224]}` (a mushroom, kind 6) twice before it with x
4180 and 4340, and build: the demo has three mushrooms by the inn, and Wonder Boy walks into them.

## Maps

The rooms' maps can be drawn in [Tiled](https://www.mapeditor.org), a free map editor. First
write them out (about a minute; not part of the build, since it draws every block):

    python tools/tiled.py export

That makes `rooms/tiled/`: a Tiled map (`.tmx`) for each layer of the rooms, named after the first
room that uses it (`area02_room00_A.tmx`: area 2, room 0, plane A, the front one; `_B` is the one
behind), and beside each its tileset, every block it can use, drawn in that room's colours.
`index.json` lists them with the rooms that use each (many rooms share their backgrounds: a change
to a shared one shows in all of them). Open a map in Tiled, draw with its tileset, save, build.

* **Blocks.** A tile is one of the game's blocks: 16 x 16 pixels with its collision (its attribute:
  how solid it is), so the room plays as it looks. The tileset's rows 0-23 are the
  blocks every room shares, rows 24-31 the layer's own. Keep to that one tileset, and do not flip
  or rotate tiles (Tiled's flips are not the game's: a flipped block is another block).
* **Places.** A map is a grid of places, each 16 x 12 blocks (one screen); a room has up to 8 x 8
  of them (2,048 x 1,536 pixels), of which at most 16 can be used: a place left all empty is not.
  Tiled's grid shows the blocks; a place starts every 16 blocks across and 12 down.
* **Size.** Map > Resize Map, by whole places (a width a multiple of 16, a height of 12): every
  room that uses the layer takes that size (its script's layer command), which is how far it
  scrolls. Its exits stay where they were: they are events in the room's event list, at their
  places (see "Levels"); move them to the new edge.
* **What is read.** A map whose cells or size changed replaces that layer's places; a map left as
  it was changes nothing. Keep each map's one tile layer and its properties (`layer` names the
  layer; `exported_places`, its size when written out). The data can be CSV or base64 (Map >
  Map Properties, Tile Layer Format).
* **Seeing it.** A room's map is read when the room loads: walk into it. The title screen's demo
  starts in `area02_room00_A`.

Example: in `area02_room00_A.tmx`, copy the second place's houses over the first, and resize the
map from 64 to 80 cells across (one more place): the demo starts among two houses, with the ground
raised as they are, and the village is a screen longer.

**New rooms.** The game's rooms are numbered in their areas, and a new one would need its own
number, a script, its music, start points and doors leading to it, and the game keeps some of
that per room in tables made for the rooms it has: that is not done yet. To make a room of your
own, rebuild one: its map in Tiled (any size up to 8 x 8 places), its enemies and things in its
lists, its exits and doors in its event list.

## Balance

`tables/balance.json` holds the numbers most mods want, by name:

* **`monsters`**: each monster type (`type`, `CCKK` as in `sprites/enemies/enemies.json`; where it
  is first seen and its picture, to know which it is):
  * `health`: how much damage it takes to beat, a multiple of 32 (the damage numbers show the
    hits: the first sword does 32 a hit);
  * `defence`: 0 to 15, how much of your hit it takes: 0 double, 1 all of it, 2 three quarters,
    3 five eighths, 4 a half, then less, down to 15 about a tenth;
  * `damage`: what touching it costs you;
  * `flags`: a word the game reads with the record; its meaning is not all known yet (bits 6-7
    shorten a double hit's time), so leave it;
  * `speed` and `acceleration`: `[x, y]`, the speed it moves at at most and how fast it gets there
    (in the game's own units: higher is faster; an acceleration can be negative); a townsperson
    has one `speed` and a `palette`; `spawn`: two bytes the game reads with them (leave them);
    `moves_like`: other types that share this movement record, which must keep the same values;
  * `drops`: what it leaves when beaten, four choices taken with the chances 3/4, 3/16, 3/64 and
    1/64: `"nothing"`, `"heart"`, `"full health"`, `"magic charge"`, gold (`"gold 50-81"`; the
    smaller amounts roll away: `"gold 3-6, rolling"`), or `"$XX"` for another item kind. The list
    `drops` at the top of the file has every name.

  Some types have only `damage`, or only speeds (their categories keep no more).
* **`prices`**: each thing's price in gold (`thing`: its number; `name`: as the game names it).

A monster reads its numbers when it appears: walk into its room again to see a change. A shop
shows a new price at once.

Examples: give the mushrooms (`0006`, first seen in area 2, room 0) `"health": 320` instead of
96: they take ten hits instead of three, and their health bars show it (the title screen's demo
hits one). Give them `"drops": ["heart", "heart", "heart", "heart"]` and `"speed": [48, 64]`:
they leave a heart every time and run at three times their speed. Set the Potion's `"price"` to 1
and every shop sells it for 1 gold.

## Next

The plan, in the order the owner chose (2026-10-26):

1. **Safety and this guide** (done).
2. **Graphics as pictures** (done): the tiles as PNG files in their real colours, read back.
3. **Text as text** (done): `messages.txt` and `phrases.txt` read back, at any length.
4. **Levels**: lists, room scripts and the houses' lists of any length (done); the maps in
   Tiled, any size up to 8 x 8 places (done). Open: new rooms (their numbers, and the tables the
   game keeps per room), and the events' formats by name.
5. **Balance by name** (done): the monsters' health, defence, damage, speeds and drops, and the
   prices, in `tables/balance.json`.
