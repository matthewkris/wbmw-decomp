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

### Pointer fields

Entity fields `-$2900` and `-$2A00` (animation/script pointers; `-$2900` is copied into
`-$2A00` at $1713E), `$FF8C7A` (text pointer, odd values) and `$FF9F2C` (word table) always hold
ROM data addresses: hint `data_ptr`. `-$3600` is a velocity (8.8 fixed, negated with `neg.w`),
not a pointer. `-$2700` is written both as words and longs with values like $D35A and $2000;
undecided.

Code region $200–$1D000 (118,272 bytes): 95,160 bytes traced as code (28,208 instructions,
845 call targets), 2,344 bytes of jump tables and task tags, 20,768 bytes of embedded data (of which
1,534 are the padding at $1C602).
1,355 of the 1,361 `rts`-shaped words in the region are inside traced instructions.
46 routines have no static reference (`orphan_` / `unreferenced` in the listing).

The code is hand-written assembly: only 38 `link a6` frames in 28k instructions.

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

Structure-of-arrays in `$FFC000–$FFDFFF`: `a4` holds the entity offset (step 4) and fields are
addressed as `$C000(a4)`, `$C800(a4)`, `$D400(a4)`, `$D800(a4)` and so on, one array per $100.
Behaviour dispatch goes through `$13DA4` (9 word offsets) into 135 handler pointers at `$13DB6`.

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

## Open items

* Short data blocks stored to `-$2900`/`-$2A00` sit right before the code that uses them
  ($18BB8, $1A3DE); the tracer read two of them as code until the fields were hinted. What
  interprets that data is not yet known.
* Palettes: where they live and how they are loaded (needed for real colours).
* Which tiles each sprite uses and in what layout (Genesis sprites are column-major).
* Formats of the 27 directory blocks; several contain long pointer tables of their own
  ($211CA, $2246C–$224B8), which stay inside `incbin` ranges and are not relocatable yet.
* $40000–$409F9 and $99A77–$9F776: nothing found that references them.
* The 46 unreferenced routines and 6 stray `rts` words ($3E4A $8934 $9C5C $1005A $100F0 $14B7C).
* Contents of the 20.7 KB of data embedded in the code region.
* Z80 driver disassembly and its music data.
