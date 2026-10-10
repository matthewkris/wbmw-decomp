# Engine notes

Status: first survey. Addresses are ROM offsets unless they start with `$FF`.

## ROM

| | |
|---|---|
| Header title | `Wonder Boy V Monster World III` / `WONDER BOY in Monster world` |
| Serial | `GM G-4060  -00`, region `UE`, header date 1990.JAN |
| Size | 786,432 bytes ($C0000); real content ends at $A4C76, rest is $FF padding |
| Checksum | $9D79, matches the header |
| SHA-1 | 87a968f773c7e807e647c0737132457b06b78276 |

## Layout

| Range | Contents |
|---|---|
| $000000–$0001FF | vectors and header |
| $000200–$01C601 | 68000 code, with tables and small data blocks mixed in |
| $01C602–$01CBFF | $FF padding |
| $01CC00–$01CC6B | data directory: 27 long pointers, each to a block in $1CC6C–$3FFFF |
| $01CC6C–$03FFFF | the 27 directory blocks, back to back with no gaps (maps, tables, text) |
| $040000–$0409F9 | unknown, not referenced by any pointer found so far; then $FF padding to $41000 |
| $041000–$041BFF | graphics table: 768 longs, see below |
| $041C00–$09622A | 726 graphics chunks; then $FF padding to $98000 |
| $098000–$099A76 | Z80 sound driver, 6,775 bytes, copied to Z80 RAM at boot by $306 |
| $099A77–$09F776 | unknown, 23.8 KB; probably music and effects for the Z80 (not yet confirmed); then padding |
| $0A0000–$0A4C76 | animation tables (three levels of word offsets), read by the interpreter at $7FC/$870 |
| $0A4C77–$0BFFFF | $FF padding |

The exact pieces are in `analysis/layout.json`, which `tools/mkasm.py` turns into labels, tables
and `incbin` ranges.

### Data directory ($1CC00)

Slots 0–7 are read directly (`movea.l $1CC00+4n.l`), e.g. slot 4 at $156E/$1ED6, slots 5 and 6
at $2772/$2792. Slots 8–26 are indexed by the area number in `$FF9668` at $93C4: the block starts
with word offsets indexed by `$FF9669` (the room or sub-area), each leading to a byte stream. In
the source they are `dir00`–`dir07` and `area00`–`area18`.

### Graphics table ($41000)

768 longs indexed by tile number / 32, so each entry covers a block of 32 tiles (1 KB of VRAM).
The low 24 bits point at a chunk; the top byte is the format. All 726 chunks are decoded by
`tools/gfx.py`, which checks that each one produces exactly 1,024 bytes and ends exactly where
the next chunk starts (`problems: 0`).

| Flag | Count | Decoder | Format |
|---|---|---|---|
| $FF | 42 | $6BD2 | empty slot: 1 KB of zeros |
| $01 | 20 | $6BE6, $22DC | raw tiles. Each is followed by 6 more raw tiles (192 bytes): $22A8 copies a caller-given tile count from inside the chunk ($6F58: `(x & $7F00) >> 5`, up to 127) |
| $00 | 11 | $6BFC | 32 tiles, each with a control byte. 0: 32 raw bytes. 1–127: that many (value byte, 32-bit position mask) fills, MSB = byte 0, then literal bytes for every position no mask covered. $80+: four bitplanes; a second byte gives two bits per plane, LSB first (fill $00/$FF; solid or masked), a masked plane has a row-mask byte (LSB = row 0) and one byte per set row, low 4 bits of the control byte turn on XOR delta down the rows for each plane; then planar to 4bpp with plane 0 as the low bit ($6D1C–$6D9B tables) |
| $02 | 695 | $6D9C | LZ77 with a Huffman code. MSB-first bit stream; the first bit is skipped (the root is always a node). Tree in preorder: 1 = node, 0 = leaf; a leaf value is 0 + 8 bits (byte) or 1 + 5 bits ($100 + n). Built in RAM at `$FF8F0A` (nodes are word pointers, leaves positive). Symbols < $100 are literals; $100–$11E are match lengths 3–33 followed by a distance symbol (distance = value + 1); $11F is an escape followed by a leaf-coded value. Output is exactly 1,024 bytes |

$22A8 (raw path) hangs on anything but a $01 entry; $6BC4 handles all four and is called from
$22F0, which streams up to 64 blocks listed at `$FF8E08` through `$FF2D00` to VRAM.

Decoded sheets show the font (Latin, digits, hiragana, katakana) in blocks 0–5, sprite tiles
after it, and background tilesets from block 96. Colours are unknown until the palettes are
found; `gfx.py --png` writes grey levels.

### Palettes

* `palette_table` ($599C–$643B): 160 records of 17 bytes. A record holds colours 1–15 of one
  palette line: a big-endian word of high blue bits (bit n for colour n+1), then 15 bytes
  `BBGGGRRR`. `UnpackPalette` ($1370) turns each into the CRAM word `0000 BBB0 GGG0 RRR0`.
  Colour 0 of each line is never loaded (transparent).
* `LoadPalettes` ($135C) takes `a0` = four record numbers, one per line, and fills the target
  palette at `$FF8B56` (64 words), starting at `$FF8B58` so colour 0 is skipped. 11 call sites.
  The in-game default list is 0, 1, 2, 3 ($91BA); the title uses 5, 1, 2, 3 ($7018) and later
  screens 94–100 ($72F0, $72F4). Area palettes come from `$FF966E`, filled from area data.
* `$FF8BD6` is the palette on screen. Fades and `$55BA` move it toward `$FF8B56`;
  `UploadCRAM` ($5370) copies it to CRAM when bit 7 of `$FF8C56` is set. Besides the boot clear at $4A16 it is the only
  `move.l #$C0xxxxxx` CRAM command in the code; DMA to CRAM has not been ruled out. `$570E` applies per-entry palette animation scripts.
* Checked by eye: line 0 colours Shion's sprites (blue, skin, silver armour) and line 2 the first
  area's background tiles (greens and browns). `tools/gfx.py --palette N` renders with record N,
  `--swatches` draws all 160.

### Rooms

Decoded by `tools/rooms.py` (`--list` parses all 170 rooms in 19 areas and decodes every map,
`problems: 0`; `--all DIR` renders every room to PNG). All word offsets in these formats are
used through `(aN,dN.w)` and are therefore **signed**; some rooms point at data before their
area block.

* **Area block** (`area00`–`area18`, `data_dir` slots 8–26): word offsets to room scripts,
  indexed by the room number in `$FF9669`. `LoadRoom` ($9330, from $93C4) runs the script.
* **Room script**: a command byte, its arguments, repeated until $FF; handlers in
  `room_commands` ($9442). A pointer argument `P` is a flag byte, then a long (flag set) or a
  signed word offset from the area block (`ReadRoomPointer`, $9500).

  | Cmd | Args | Effect |
  |---|---|---|
  | 00 / 01 | kind, width, height, word, offset | map layer for plane A / B (`SetupLayer` $946C): width in chunks (256 px), height in chunks (192 px), map data at the offset |
  | 10 / 11 | same with a long pointer | as 00 / 01 |
  | 02 | P | `SetGfxSlots`: which graphics blocks fill VRAM |
  | 03 | byte, word | palette record for line 2 (`$FF9670`) and a word to `$FF966C` |
  | 0A | byte | palette record for line 3 (`$FF9671`) |
  | 04 | P | graphics stream list for the `Mxcg` task ($23A6), probably animated tiles |
  | 07 | byte, word, byte, byte | place an object ($11BF0): byte to `d5`, word to `d4`, then X and Y in 16 px units (+$F00) |
  | 08 | P | enemy list (`$FFA11E`) |
  | 05, 06, 09, 0B–0F, 12–14 | see `tools/rooms.py` | stored to RAM or passed to routines not yet read |

  Palette lines 0 and 1 come from `$FF966E`/`$FF966F`, set to records 0 and 1 at $9378; the
  renderer assumes those.
* **Graphics slots** (`SetGfxSlots`, $234C): 64 words at `$FF8E08`, one graphics block (32 tiles)
  per 1 KB of VRAM; `StreamGfxBlocks` ($22F0) decodes the changed ones into VRAM. List bytes:
  $00–$7F hi + lo byte = block id for the next slot; $80–$BF select slot n; $C0 skip a slot;
  $C1–$F7 n consecutive ids from a following word; $F8–$FE n+1 empty slots; $FF end. A base
  list (`base_gfx_slots`, $96BC) is applied before every room.
* **Blocks** are 16x16 pixels = four tile words (top left, top right, bottom left, bottom right).
  A block word's bits 8–9 pick one of four tables, the low byte the block. Tables 0–2 are shared
  by the whole game (`LoadSharedBlocks` $9F46, three lists from `data_dir` slot 1, into
  `$FFE500`, `$FFEE00`, `$FFF700`); table 3 belongs to the room's map. Each table is 256 x 8
  bytes, then 256 attribute bytes (collision type, read by `TileAttrAt`).
* **Block list** (`UnpackBlocks`, $9F6A): word n, then n 4-bit codes (high nibble first), then
  data. Code 0 leaves the block alone. Otherwise m = code xor $F: if m bit 3 is clear an
  attribute byte comes first; then the first tile word; then by m & 7: 0 = three more words;
  1 = +1, +$10, +$11 (a 2x2 run in the sheet); 2 = two bytes giving low-5-bit variants of the
  first; 3 = three low bytes under the first word's high byte; 4–7 = three bytes, each low 5 bits
  plus flip bits (bits 6–7 become 11–12) under the first word's other bits.
* **Layer map** (`UnpackLayerMap`, $A068): signed word offset to the room's block list, signed
  word offset to a chunk offset table, then (position, chunk) byte pairs until a negative byte.
  Position = row * 8 + column on an 8x8 grid of chunks (`$FF968C` / `$FF96CC` in RAM); chunk is a
  byte offset into the offset table.
* **Chunk** (16 x 12 block words): a count byte c. If c is negative, `ExpandZeroRuns` ($A1C4)
  first expands the rest (length byte, mask count byte, masks MSB first where 1 = zero, then the
  literal bytes) and decoding restarts from the expanded buffer. Then c groups of (block word,
  one bit per still-empty cell, LSB first) fill cells; the remaining cells take 10-bit values
  from a packed stream (four per five bytes, MSB first).

### Sprites

Decoded by `tools/sprites.py` (`--list` walks all 774 animations, 1,546 frames, `problems: 0`;
`--enemy TYPE`, `--player`, `--room-png` and `--all-rooms DIR` render them).

* **Animation tree** (`anim_data`, $A0000): three levels of signed word offsets, all relative to
  `anim_data`, each indexed by a byte offset (so indices are even): set (`-$3D00`), group
  (`-$3CFF`), animation (`-$3CFE`, the `d0` of `SetAnim`). The tables at each level are packed
  back to back. 12 sets: $00 is Shion (3 groups, 48 animations), $0A and $10 hold most enemies.
* **Animation script** (`UpdateAnim`, from $878): a frame is a duration byte below $F0 and a signed
  word offset from `anim_data` to a mapping (duration 0 = hold); commands $F0–$F7 do nothing,
  $F8/$F9 set flags, $FA dx dy moves the entity, $FB and $FC (two bytes each) set the box, $FD
  stops, $FE marks the loop point, $FF loops to it (or to the start).
* **Mapping** (read by `AddEntitySprites`, $54C4): 5-byte pieces until a negative byte: VDP sprite
  size (bits 2–3 width − 1, bits 0–1 height − 1, in tiles), tile word, signed X and Y offsets.
  The tile word is XORed with the entity's flip bits and the entity's tile base (`-$3DFE`) is
  added unless bit 3 of `-$3FFD` is set. When flipped, an offset becomes −offset − size. Tiles in
  a piece run down columns, as the VDP lays them out.
* **Enemy types** (`enemy_types`, $1BC0E): a type word's high byte is a byte offset into a table of
  category offsets; record n = (low byte) / 2 is 5 bytes: flag, word offset (from $1BC0E) of a
  graphics list (word count, then graphics block ids), animation set, animation group
  (`EnemyGfxRecord` $125A6, `EnemyAnimRecord` $126AA). Category 2 is laid out differently and
  is not decoded.
* **Enemy graphics** (`AllocEnemyGfx`, $125C4): for each type in a room's list, its graphics
  blocks go into VRAM slots counting down from 48 (an identical list already loaded is reused),
  and the type's tile base, (first slot) × 32, is kept in a table at `$FFA122` and copied into
  each spawned enemy's `-$3DFE`.
* **Enemy list** (room command 08, `SpawnRoomEnemies` $12848): type words until a negative word
  (`LoadEnemyGfxLists` $12640), then a spawn script. A byte below $10 is a spawner with its own
  arguments (handlers at $128AC), most ending in a type word and X, Y (origin $1000, in pixels,
  at the entity's feet); $F8–$FE are modifiers (skip if a game flag is set or clear, set values,
  jump); $FF ends. 503 spawns in the game; 497 use a type from their room's list, the other 6
  are spawner 4 with category-4 types (rooms 9-2, 9-3, 15-1), whose graphics must come from
  elsewhere.
* **Shion** is assembled from equipment: body graphics into slots 8–11 (`LoadPlayerBodyGfx`
  $F698: list $F6E0, or $F6E8 / $F6F0 depending on flags in `$FF966B` and `$FF9EF1`) and slot 12
  ($F6DC or $F6D8), the sword into slot 13 (`LoadSwordGfx` $F6F8, `sword_gfx_lists` $F710 by
  `$FF95CE`), and the shield as 32 raw tiles copied to tile $5C (`LoadShieldGfx` $F742, tile number
  from `shield_tiles` $F75E by `$FF95D0`, copied by `CopyRawTiles` linearly through the ROM).
  His tile base is 0.

### Pointer fields

`-$2900` (hurtbox list) and `-$2A00` (attack box list) hold ROM addresses of hitbox lists: a
count byte, then the boxes. `HitboxOverlap` ($C3E) tests an attacker's `-$2A00` list against a
target's `-$2900` list; `$1713E` copies `-$2900` into `-$2A00` so the body itself hurts. The
six-byte blocks at $18BB8 and $1A3DE are one-box lists. `$FF8C7A` (text pointer, odd values) and
`$FF9F2C` (word table) also always hold ROM data addresses. All four are in hint `data_ptr`.
`-$3600` is a velocity and `-$2700` is undecided (see Entities).

## Kernel

* Reset $200 is the standard Sega init stub, then $4A06. Bus error, address error, all other
  exceptions, traps and unused interrupts go to hang loops ($49F8–$4A04, $4B58).
* The only live interrupt is VBlank ($4B5C): it bumps `$FF8A49` and sets bit 0 of `$FF8006`. All
  real work happens in the main loop; `$3B8` waits for that flag and calls `$5262`.
* Cooperative tasks. Slots are $80 bytes: 4 system slots at `$FF8048`, 16 at `$FF8248`.
  Slot+0 bit 15 = active, +4 = four-character tag, +8 = exit handler, +$C = resume address.
* `$400` is yield: it pops its own return address into slot+$C and returns to the scheduler,
  so code after `jsr $400` runs on the next pass. 123 call sites.
* Task descriptors are a 4-byte ASCII tag followed by code. Tags found: Ksmt Kseg Kttl Ktt2 Ktmt
  Ktm2 Ktm3 Kmnu Kdym Kgmt Kgam Kcol Kedm Keds Mxcg. (`PAUS` at $7F4E, listed here before, is the
  string `"PAUSE",0` passed to $7870, not a task.) Spawned by $50D4/$50E2 (system task),
  $5106 (list of descriptors), $444/$47A (pool), killed by tag with $4A2/$4A4.
* The scheduler at $4C00 can time-slice against the VDP HV counter (`$C00008`, cut-off $D0xx)
  when bit 6 of `$FF8A5D` is set. A port has to replace that with a fixed budget.
* Several subsystems keep continuation pointers in RAM (`$FF9630–$FF9654`, a stack at `$FF9C14`,
  `$FF9F1E/22`, `$FFA3BE`) filled from popped return addresses.

## Entities

Structure-of-arrays in `$FFC000–$FFDFFF`: `a4` holds the entity offset (step 4, 64 entities, so
`$0–$FC`) and each field is one array, addressed as `-$4000(a4)` = `$FFC000+a4` and so on. The
routines in "Named routines" all take the entity in `a4` and a second one, when needed, in `a2`.

| Field | Array | Meaning (from the routines that use it) |
|---|---|---|
| -$4000 | $C000 | flags: bit 7 active, bit 5 frozen (`FreezeEntities`), bit 0 exempt from `DespawnIfOffscreen`; bit 6 is set on spawn, meaning unknown |
| -$3FFE | $C002 | bit 3 facing left (negates X in anim moves and `WalkForward`), bit 4 flips Y moves |
| -$3FFD | $C003 | animation state bits; bit 6 blinking |
| -$3F00 | $C100 | current sprite mapping (set by the animation script) |
| -$3D00 / -$3CFF / -$3CFE | $C300 | animation set / group / number: three levels of word offsets into `anim_data` |
| -$3C00 / -$3B00 | $C400 / $C500 | animation script pointer / loop point |
| -$3A00 / -$39FE | $C600 / $C602 | animation timer / speed (`$100` = 1 per frame) |
| -$3900 / -$38FF | $C700 | blink pattern / blink counter |
| -$3800 / -$37FE | $C800 | X (pixels) / X fraction |
| -$3700 / -$36FE | $C900 | Y / Y fraction |
| -$3600 / -$35FE | $CA00 / $CA02 | X / Y velocity, 8.8 fixed |
| -$3500 / -$34FE / -$34FD | $CB00 | box offset; width; height |
| -$3400 / -$33FE | $CC00 / $CC02 | X / Y acceleration |
| -$3300 / -$32FE | $CD00 / $CD02 | X / Y speed limit (also the walk speed) |
| -$3100 | $CF00 | collision result, rebuilt each move: `CheckGrounded` tests bit 1 set with bit 3 clear; the turn-at-wall helpers ($4428) test bit 0 set with bit 6 clear |
| -$2F00 | $D100 | physics mode: bit 0 clamp X, bit 1 clamp Y |
| -$2E00 / -$2DFE | $D200 | previous X / Y |
| -$2BFF / -$2BFD | $D400 | enemy state (dispatched by `EnemyDispatch` through $13B70 and $13DA4) / enemy type |
| -$2AFD | $D503 | contact flags from `CheckBodyContact` / `CheckPlayerContact`: bit 7 hit, bit 2 touched, bit 0 side |
| -$2A00 / -$2900 | $D600 / $D700 | attack / hurt box lists (see Pointer fields) |
| -$2700 | $D900 | contact damage (copied into the other entity's `-$23FE` on contact) |
| -$2600 | $DA00 | health |
| -$21FF / -$21FE | $DE01 / $DE02 | hit pending / damage taken |

Entity pointers in RAM: `$FF9EEE` is the player, `$FFA11C` the enemies' target (set from
`$FF9EEE` at $137C8), `$FFA11A` the first enemy slot (`$40`; `FindFreeEntity` searches 16 from
there). Effects live in slots `$E0–$FC` (`FindFreeEffect`).

## Named routines

Names live in the `labels` section of `analysis/layout.json`. Registers are inputs unless said.

| Routine | Address | What it does |
|---|---|---|
| `LoadZ80Driver` / `ResetZ80` | $306 / $34A | upload `z80_driver` to Z80 RAM (length from bytes 6–7 of the driver) / pulse Z80 reset |
| `QueueSound` | $366 | queue sound command `d0` in the Z80 ring at $1C30; `QueueSoundFF` ($364) queues $FF |
| `WaitVBlank` | $3B8 | count frames (`$FF8002`, `$FF8A56`), wait for VBlank, run `VBlankWork` ($5262) |
| `Yield` | $400 | save the return address in the task slot (`$FF8A4C` + $C) and return to the scheduler |
| `YieldIfLate` | $40C | yield only when the raster is past line $D0 (time-sliced mode, bit 6 of `$FF8A5D`) |
| `SpawnTask` | $444 | `a1` = descriptor (tag + code); first free user slot; returns slot number in `d0`; `FatalError` if full |
| `SpawnTaskOnce` | $47A | as `SpawnTask` unless a task with the same tag runs; `d0` = 1 if it did |
| `KillTasksByTag` | $4A2 | end every task whose tag matches `(a0)` (`KillTasksByTagD2`: tag in `d2`) |
| `EndTask` | $4E6 | run the current task's exit handler (slot + 8), free its slot and memory blocks |
| `SetVRAMWrite` | $5A0 | `d0` = VRAM address; writes the VDP write command |
| `Random` | $5D8 | next value of the generator in `$FF8A52` (seed `$2A6D365A`), returned in `d0` |
| `DisplayOn` / `DisplayOff` / `EnableVInt` | $618 / $62A / $64E | VDP register 1 mirror (`$FF8A5C`) |
| `SetPlaneAAddr` ... `SetPlaneSize` | $716–$79C | VDP registers 2, 3, 4, 5, 13 and 16 from their RAM mirrors |
| `SetAnim` / `SetAnimNow` | $7E8 / $7FC | set animation number `d0`; restart next frame / at once |
| `UpdateAnim` | $80E | run the entity's animation script (commands $F0–$FF via the table at $890) |
| `ClearEntity` | $A36 | zero an entity's state fields |
| `ApplyVelocity` | $AB4 | add velocity to position (`ApplyVelX` $AB8, `ApplyVelY` $AD6) |
| `Accel` | $B42 | apply acceleration with optional speed limit (`AccelX` $AF4, `AccelY` $B44) |
| `FrictionX` / `FrictionY` | $B92 / $BAC | move velocity toward 0 by `d0` |
| `Offscreen` | $BEE | `d0` = 0 if the entity is within the camera (`$FF9758`, `$FF975A`) plus margin |
| `HitboxOverlap` | $C3E | test `a2`'s attack boxes against `a4`'s hurt boxes |
| `CheckBodyContact` | $E64 | contact between `a2` and `a4`; sets `-$2AFD` and swaps contact damage |
| `MoveWithCollision` | $2FD0 | move the entity and resolve it against the map |
| `TileAttrAt` | $30DA | map attribute at (`d6`, `d7`), provisional |
| `TestGameFlag` / `SetGameFlag` | $274C / $2752 | game progress bits at `$FF9992` (`d0` = flag number) |
| `FindFreeEntity` | $12812 | free enemy slot in `a2`; N flag set when none |
| `SpawnItem` | $1090C | spawn item `d0` (codes from $1000 go elsewhere) |
| `EnemyMove` / `EnemyFallAndMove` | $46DA / $46D6 | standard enemy step: probe tiles, collide, move (with gravity) |
| `CheckPlayerContact` | $4792 | sword hit or body contact with the player, then update the enemy health bar |
| `DamageEnemy` | $4986 | play sound 31 and subtract the hit's damage from `-$2600` |
| `EnemyDefeated` | $482C | count the kill and maybe drop an item (random, per enemy type, table $1C170) |
| `DespawnIfOffscreen` | $48C4 | despawn and drop the caller's frame when off camera |
| `EnemyCheckHit` | $43BC | if a hit is pending, switch to state 2 through `EnemyDispatch` and drop the caller's frame |
| `FacePlayer`, `WalkForward`, `DistXToPlayer`, ... | $4492–$457A | small enemy AI helpers; see the labels |

Several routines end a behaviour by dropping their caller's return address (`addq.w #4,a7`),
like the continuations under Kernel. A C port must treat those as an early return from the
caller.

## Hardware touched by the 68000

| Address | Use |
|---|---|
| $C00000 / $C00004 / $C00008 | VDP data, control, HV counter (47 / 30 / 4 sites) |
| $A10003, $A10005, $A10009–$A1000D | controller ports |
| $A11100, $A11200 | Z80 bus request and reset |
| $A00000 | Z80 RAM: driver upload and the command queue |
| $200001 | serial EEPROM, bit-banged (bit 0 data, bit 1 clock), all in $1C49A–$1C600 |

Sound: the 68000 never writes the YM2612 or PSG. `$366` queues one command byte (d0) in a
16-entry ring at Z80 `$1C30`, guarded by a busy flag at Z80 `$1C00`. 98 call sites.

## Boot and menu (seen in the harness)

* Power-on runs `Ksmt` (setup) and `Kseg` (SEGA logo, ~4 s), then `Ktmt`/`Kttl` (title), then on
  Start `Ktm3`, then `Kmnu`. Without input the title falls through to `Kdym`, the attract demo,
  which plays the opening room with the intro text box.
* The menu (`Kmnu` at $71C2, `sub_7294`) reads the EEPROM through `sub_1C430` to decide whether
  to offer CONTINUE. Its items are sprite entities 8, $C and $10. Start on NEW GAME runs `Kgmt`
  and `Kgam`; the first room is area 0, room 0, inside Shion's house.
* The visible sky is the backdrop: colour 0 of palette line 0, which `$8F2C` sets from the word
  in room command 03.
* Sprites are only built for entities with `-$4000` bits 7 and 6 set; bit 2 of `-$3FFD` hides an
  entity for the blink off-phase. Entities are drawn in 16 buckets by `-$3CFD`.

## Open items

* Short data blocks stored to `-$2900`/`-$2A00` sit right before the code that uses them
  ($18BB8, $1A3DE); the tracer read two of them as code until the fields were hinted. What
  interprets that data is not yet known.
* Room commands 05, 06, 09, 0B–0F and 12–14, the object and enemy lists, and parallax: the
  room renderer draws a narrower background layer once, with no scrolling.
* Enemy category 2, the spawner types that have their own layouts (6, $0A, $0D, $0E), and
  where the graphics for the six unlisted spawns come from.
* Objects from room command 07 (doors, chests, signs) are not drawn yet.
* Which tiles each sprite uses and in what layout (Genesis sprites are column-major).
* Formats of the 27 directory blocks; several contain long pointer tables of their own
  ($211CA, $2246C–$224B8), which stay inside `incbin` ranges and are not relocatable yet.
* $40000–$409F9 and $99A77–$9F776: nothing found that references them.
* The 46 unreferenced routines and 6 stray `rts` words ($3E4A $8934 $9C5C $1005A $100F0 $14B7C).
* Contents of the 20.7 KB of data embedded in the code region.
* Z80 driver disassembly and its music data.
