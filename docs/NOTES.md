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
| $01C602–$01CBFF | data |
| $01CC00–$01CC6B | data directory: 27 long pointers into $1CC6C–$3E9F8 |
| $01CC6C–$03FFFF | game data reached through the directory (medium entropy: maps, tables, text) |
| $040000–$097FFF | mostly high-entropy data, presumably compressed graphics (format not yet identified) |
| $098000–$099A76 | Z80 sound driver, 6,775 bytes, copied to Z80 RAM at boot by $306 |
| $0A0000–$0A4C76 | animation tables (three levels of word offsets), read by the interpreter at $7FC/$870 |

Code region $200–$1D000 (118,272 bytes): 95,174 bytes traced as code (28,213 instructions,
845 call targets), 2,348 bytes of jump tables and task tags, 20,750 bytes of embedded data.
1,355 of the 1,361 `rts`-shaped words in the region are inside traced instructions.
46 routines have no static reference (`orphan_` / `unreferenced` in the listing).

The code is hand-written assembly: only 38 `link a6` frames in 28k instructions.

## Kernel

* Reset $200 is the standard Sega init stub, then $4A06.
* The only live interrupt is VBlank ($4B5C): it bumps `$FF8A49` and sets bit 0 of `$FF8006`. All
  real work happens in the main loop; `$3B8` waits for that flag and calls `$5262`.
* Cooperative tasks. Slots are $80 bytes: 4 system slots at `$FF8048`, 16 at `$FF8248`.
  Slot+0 bit 15 = active, +4 = four-character tag, +8 = exit handler, +$C = resume address.
* `$400` is yield: it pops its own return address into slot+$C and returns to the scheduler,
  so code after `jsr $400` runs on the next pass. 123 call sites.
* Task descriptors are a 4-byte ASCII tag followed by code. Tags found: Ksmt Kseg Kttl Ktt2 Ktmt
  Ktm2 Ktm3 Kmnu Kdym Kgmt Kgam Kcol Kedm Keds Mxcg PAUS. Spawned by $50D4/$50E2 (system task),
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

* Byte-identical reassembly of the listing (needs an assembler in the loop).
* Graphics compression format and the loaders that use the $1CC00 directory.
* The 46 unreferenced routines and 6 stray `rts` words ($3E4A $8934 $9C5C $1005A $100F0 $14B7C).
* Contents of the 20.7 KB of data embedded in the code region.
* Z80 driver disassembly and its music data.
