# The port

How the game is being rewritten in C, how each piece is checked, and the design decisions,
with reuse for other games in mind (see `docs/PROCESS.md`).

## How it works today

* `port/` holds the C routines. Each one does exactly what one 68000 routine does: the same
  registers out, the same condition codes, the same memory writes, including the return
  address an inlined `bsr` leaves below the stack pointer. It sees the machine only through
  `port/port.h`: a register file (`Cpu`) and memory accessors the host provides. Nothing in
  `port/` knows about Musashi or the harness, so the same code will later run against a
  native machine.
* `port/registry.c` lists every ported routine by the address of the original.
* `harness/hybrid.c` traps those addresses with Musashi's instruction hook:
  * `--port replace`: the C routine runs instead of the original, then the original's `rts`.
  * `--port shadow`: on every call, snapshot the machine, run the C routine and record its
    result, restore, run the original with interrupts held, and compare registers, condition
    codes, work RAM, Z80 RAM, VRAM, CRAM, VSRAM, VDP registers and port latches when it
    returns. The run continues on the original's result, so a shadow run is the pure run plus
    checks.
  * `WBMW_PORT_BREAK=Name` corrupts one routine's X flag, to prove the shadow check catches a
    one-bit error (it does, on every call).

## How a ported routine is accepted

1. **Zero shadow mismatches** over the standard runs (a new game played into the first room,
   the attract demo with no input) and over differential fuzzing.
2. **Every instruction of the original covered.** `--coverage FILE` records which instructions
   of each ported routine's original ran inside a shadow check; `tools/coverage.py` merges runs
   and lists the ones that never did. Gameplay alone rarely reaches every branch, so:
3. **Differential fuzzing** (`--fuzz NAME COUNT`, shadow mode): from a real in-game state, call
   the routine directly with random data registers, condition codes, random seed and entity
   arrays (a4 a random entity), each call checked against the original. Routines that take
   pointers need an input generator (`SetGfxSlots`: random valid slot lists; `UpdateAnim` and
   `SetAnimNow`: any of the 774 animations, resumed at any item of its script). The
   self-test (`WBMW_PORT_BREAK`) shows both shadow runs and fuzzing catch a single flipped flag
   on every call.
4. A path that cannot run in the harness is listed below as unverified, with the reason.
5. Replace mode still plays (`play.exe --port replace`).

Replace mode is **not** compared byte for byte with the pure run. The C routines take no 68000
time, so the rest of the 68000 code runs earlier in each frame: a loading step that overran a
frame in the original fits in one, the replace run gets a frame ahead (frame counter +1, the
player one pixel ahead at the same frame number), and interrupt stack frames land at different
places in the dead stack below SP. Measured on the first batch: positions agree again at frame
3000; the differences are the frame counter, the stack area and a handful of timing-related
bytes. When enough of the game is native that 68000 timing no longer exists, comparison moves
to "same game state at the same game frame" (the game's own frame counter), not the same host
frame.

## Status

45 routines, all with zero mismatches: shadow-checked calls in the two standard runs, plus
20,000 fuzz cases each where noted.

| Routine | Address | Gameplay calls | Fuzz | Coverage |
|---|---|---|---|---|
| `Random` | $5D8 | 47 | 20,000 | all |
| `SetVRAMWrite` | $5A0 | 3,851 | 20,000 | all |
| `LoadPalettes` | $135C | 11 | | all |
| `UnpackPalette` | $1370 | inside `LoadPalettes` | | all |
| `ApplyVelocity` | $AB4 | 87 | 20,000 | all |
| `ApplyVelX`, `ApplyVelY` | $AB8, $AD6 | 4,232 each | 20,000 each | all |
| `FrictionX`, `FrictionY` | $B92, $BAC | 1,198 / 0 | 20,000 each | all |
| `AccelX`, `AccelY` | $AF4, $B44 | 1,839 / 4,319 | 20,000 each | all |
| `Accel` | $B42 | 0 | 20,000 | all |
| `QueueSound`, `QueueSoundFF` | $366, $364 | 35 / 6 | | all but the busy wait |
| `SetGfxSlots` | $234C | 20 | 20,000 random lists | all |
| `SetAnim`, `ResetAnimTimer`, `SetAnimNow` | $7E8, $7F4, $7FC | 84 / 1 / 17 | 20,000 each | all |
| `UpdateAnim` (with its command handlers) | $80E | 19,217 | 50,000 (every animation, from any item of its script) | all |
| `DecodeGfxBlock` (all four formats) | $6BC4 | 168 | 10,000 (random real blocks) | all |
| `StreamGfxBlocks` | $22F0 | 10 (each up to 64 decodes) | 300 random slot tables | all |
| `CopyRawTiles` | $22A8 | 592 | 20,000 (raw-block tiles, clears) | all; the two hang loops for bad entries are reproduced, not exercised |

| `BuildSprites` (the per-frame sprite table) | $53AA | 6,711 frames | 3,000 frames of random entities | all |
| `AddEntitySprites` | $54C4 | inside `BuildSprites` | 20,000 (real mappings, random camera, window, budget) | all |

| `CheckBodyContact`, `BodyOverlap` | $E64, $E9A | 1,277 / 64 | 20,000 each | all |
| `BodyVsAttackBoxes` (+ `Swap`) | $D76, $D6E | 1,187 / 0 | 20,000 each | all |
| `HitboxOverlap` (+ `Swap`), `BoxPairOverlap` | $C3E, $C36, $CA0 | 0 | 20,000 each | all |
| `BodyVsHurtBoxes`, `BoxVsBody` | $D2C, $DB6 | 0 | 20,000 each | all |
| `ResolveContact`, `ContactSide` | $E1A, $E4A | inside the above | 20,000 each | all |

| `MapBlockAt`, `MapBlockAtRaw` | $2542, $2570 | 1,835 / 0 | 50,000 each (real and synthetic layers) | all |
| `TileAttrAt`, `TileAttrAtPixel`, `TileTypeOf` | $30DA, $30D2, $30F4 | 23,154 / 34 / 15 | 20,000 to 40,000 each | all |
| `ProbeTileAt`/`AtA3`/`Below`/`Above`/`Feet` | $3FAC, $3FB0, $3F58, $3F74, $3F92 | 13,316 together | 20,000 each | all |

The map lookups are fuzzed from two states (the first room, and the attract demo outdoors) and
half the time against a synthetic layer (random extents up to 8x8 chunks, random chunk map),
because no room the standard runs visit is taller than one chunk.

The hitbox routines are mostly reached from enemy code the standard runs barely exercise (no
enemy fights in them), so fuzzing carries them: two entities close together with random box
lists, facing and contact flags. `BoxPairOverlap` keeps an original bug: the target box's y
offset is sign-extended before being negated, so a flipped offset of -8 becomes -248.

Once `StreamGfxBlocks` is ported, `DecodeGfxBlock` is only ever reached inside it, so its gameplay
checks now happen as part of `StreamGfxBlocks`'s (nested ported calls run as C inside an
outer check and are verified through the outer result).

Unverified: the wait in `QueueSound` while the sound driver is busy ($386-$394). The harness
does not run the Z80, so the busy flag is never set. The `$C0` (skip a slot) command of
`SetGfxSlots` is used by none of the game's 79 slot lists; it is checked only by fuzzing.

## Decisions

### A translator as scaffolding (decided 2026-10-10)

After about 1,100 instructions transcribed by hand, the owner chose to generate the exact C
mechanically instead: `tools/m68k2c.py` turns any 68000 routine (and everything it reaches)
into C with the same registers, condition codes and stack effects, built on a small runtime
of 68000 semantics (`port/m68k_rt.h`). The generated C is checked exactly like hand ports
(shadow mode, coverage, fuzzing). It is a static recompilation and therefore **scaffolding
only**: it gets the whole game running as C quickly, and the end product is still hand-written
idiomatic C, produced by rewriting the translated routines subsystem by subsystem, each rewrite
guarded by the shadow checks against the original. Generated C is derived from the ROM's code,
so like the disassembly it is generated into `work/` at build time and never committed; the
translator and runtime are committed and are game-independent (any 68000 game). The hand ports
already in `port/` stay; the translator calls them where they exist.

How the translator handles what plain translation cannot:
* calls push the real return address and `rts` pops it into `c->ret`; a caller continues only if
  that is its own return address, else it returns too, so routines that drop their caller's
  frame (and later `Yield`) need no special cases;
* computed jumps resolve through the map's jump tables, and a `jmp (aN)` just after
  `movea.l VAR,aN` is a stored continuation: its targets are the code addresses the game stores
  into VAR, which become their own entry points (`$38E4`, stored in `$FF9EE8`);
* anything else goes through `rt_dispatch` and stops the harness with the addresses if unknown.

First use, `MoveWithCollision` ($2FD0): 22 routines, 849 instructions, generated with no
unsupported instruction. Zero mismatches in the two standard runs (about 16,000 checked calls of
the generated routines) and in 5,000 fuzz cases per routine from two states; the self-test
catches a corrupted generated routine on every call. Coverage then: 12 of the 22 fully covered,
the gaps mostly collision with moving platforms, which no standard run reaches.

The owner then made a save state among the floating platforms of area 1, room 2 (F2 in
`play.exe`, kept as `work/platform.state`, not committed). From it: eight runs of 1,200 frames
with random input (30,000 to 40,000 checked calls each) and 5,000 fuzz cases for each partly
covered routine, all with zero mismatches. Coverage rose to 13 of the 22 fully covered:

| Routine | Before | With the platform state |
|---|---|---|
| `$2FD0` `MoveWithCollision` | 66/73 | 66/73 |
| `$341C` | 46/93 | 46/93 |
| `$3584` | 216/252 | 216/252 |
| `$3694` | 44/54 | 44/54 |
| `$36D4` | 29/36 | 29/36 |
| `$370A` | 14/19 | 14/19 |
| `$3860` | 30/32 | 30/32 (the 2 left are the deliberate hang, `nop; bra *`, table entry 0) |
| `$3AFC` | partial | all |
| `$3B74` | partial | 37/59 |
| `$3C22` | partial | all |
| `$3D06` | 3/78 | 50/78 |

Fuzzing added nothing over the random runs then: its generator randomised every entity field,
so positions, sizes and velocities almost never lined edges up, and it never touched the
platform records.

**The collision scene generator** (`collision_scene` in `harness/hybrid.c`, used for three
quarters of the fuzz cases of every generated routine) builds the situations instead:

* the entity with a realistic size and a small velocity, its previous position just behind it,
  its feet in a solid block or a slope found in the room (through the hand-ported
  `TileAttrAtPixel`);
* rooms without slopes, which are both rooms with states: the block under the feet, and
  sometimes those above and below it, are made slopes by writing a slope attribute into the
  room's block attribute table, which is in RAM;
* all six moving-platform records (flags, attribute, position, velocity, half size) placed with
  an edge touching the entity from the side, above or below, or near it; the platform it stands
  on; sometimes two platforms with the same top;
* for the per-platform routines fuzzed on their own (`$3B74`, `$3D06`, `$3E00`), `a2` and the
  edges in `d4`-`d6` as their callers compute them.

It counts the scenes it built and how many stand on a slope or a solid block, so a room where
it finds neither shows up in the report.

10,000 cases per routine from each of the two states (the platform state and the attract demo):
zero mismatches. **20 of the 22 generated routines were then fully covered** (counted by address range; see below). What was left:

* `$3860` and `$3584`: `nop; bra *` at $3888, table entry 0 of `$3860`. It is a deliberate hang
  that no collision type reaches.
* `$3584`: `unlk a6; rts` at $38E0. It is reached only through the other entry `$356E` (which
  sets `$FF9EE8` to -1). `$356E` is called by the sibling movement routines `$2EDE` and `$2F34`,
  which are not translated yet. These instructions get covered when those routines become roots.

Two kinds of input are invalid for fuzzing, like the stored continuation before:

* Attribute bytes $80-$FF index past the type table at $3FC2 into code. Some of the garbage
  they read looks like a slope and sends `$3860`'s jump out of the table, and the generated C
  then stops at an unknown address. The game never uses them, so the generator does not either.
* `$3694`, `$36D4` and `$370A` are not fuzzed on their own. They use `$3584`'s stack frame
  (`-$12(a6)`) and end with its `unlk a6`, so a direct call returns to garbage. They are
  checked inside `$3584`.

#### The rest of the movement family ($2E38, $2E9E, $2ED4, $2F8E)

The family is seven entry points, `$2E38`-`$2FD0`. Each loads layer A, applies the velocity, saves the
previous collision result, and runs its own mix of horizontal, vertical, slope and platform
handlers. With the last four as roots there are 39 generated routines (1,626 instructions), with
no unsupported instruction. Eight of them are new: `$2E38`, `$2E9E`, `$2ED4`, `$2F8E`, and
`$3142`, `$3310`, `$3948`, `$3A4C`, which are variants of the horizontal and vertical handlers.

* `$2E38` runs exactly once a frame in gameplay: 1,200 calls in each 1,200-frame run. It is
  almost certainly the player's movement. It checks moving platforms, and it also stores the
  attribute byte when the player is on a slope.
* `$2E9E`, `$2ED4` and `$2F8E` never ran in the standard or platform-state runs. Fuzzing alone
  covers them, so a save state where an entity uses one would be a welcome extra check.

Checks:

* The ten gameplay runs: zero mismatches.
* 10,000 fuzz cases per new routine from each of the two states: zero mismatches.
* The self-test catches a corrupted `$2E38` and `$2F8E` on every case.

**38 of the 39 generated routines are fully covered**, and the one left is `$3860`'s deliberate
hang. The whole movement-with-collision subsystem (`$2E38`-`$3E1E` and what it calls) is
now translated and checked.

#### The sibling movement routines ($2EDE, $2F34)

`$2EDE` and `$2F34` are two variants of `MoveWithCollision`. They probe the tiles first
(`$3F08`), use a different horizontal handler (`$32FE`) and the other floor entry (`$356E`),
and skip the moving platforms; `$2F34` also calls `$3AB0`. Few entities use them: 8 and 10 calls
in the standard runs. Adding them as roots (`analysis/translate.json`) brought in nine routines:
`$26F0`, `$2EDE`, `$2F34`, `$30CE`, `$32FE`, `$356E`, `$39FA`, `$3AB0`, `$3F08`. That makes 31
generated routines in all, translated with no unsupported instruction.

Checks:

* The two standard runs, plus eight random-input runs from the platform state (about 370,000
  checked calls): zero mismatches.
* 10,000 fuzz cases per generated routine from each of the two states: zero mismatches.
* The self-test catches a corrupted `$2F34` on every case.

**30 of the 31 generated routines are fully covered.** The one left is `$3860`'s deliberate
hang. `$30CE` needed one more scene: the player on a tile type that their equipment overrides
(`$5xxx` and `$6xxx`). No room with a state has such a tile, so the generator writes one of
those attributes ($09, $5E, $0A, $5F) under the feet, as it does for slopes.

**Coverage now counts a translated routine's own instructions.** `tools/m68k2c.py` writes them
to `work/gen/bodies.json`, and `tools/coverage.py` reads that file. Before this, coverage
counted every instruction in a routine's address range. `$2EDE` ends with a tail jump to `$3098`,
so its range runs past `$2F34`, `$2F8E` and `$2FD0`, which are not part of it, and they showed
up as never run. Under the new count, `$3584`'s exit at $38E0 (now run through `$356E`) and
the hang (which belongs to `$3860`) no longer count against `$3584`. Hand-port ranges are
exact and are still counted by range.

### Exactness first, cleanup later

Ported routines reproduce side effects the game may not depend on (a scratch register, an
unused flag, a stack residue), because the shadow check can only prove equivalence if nothing
is left out. Once the callers of a routine are also C, the side effects nobody reads can be
dropped. Doing it in that order keeps every step checkable.

### The idiomatic rewrite: contracts and the dead-register watch (started 2026-10-11)

The rewrite replaces translated routines with ordinary C, a subsystem at a time, in
`port/<subsystem>.c`. The first slice is the moving-platform collision in `port/movement.c`:
`PlatformsSideways` ($3AFC) and `PlatformsVertical` ($3C22), which push an entity out of the
21 platform records, stand it on one and carry it along. They are written as C functions over
named fields (`E_X(e)`, `P_HALF_W(p)`, ...) with ordinary arguments, and the adapter that
registers them only unpacks the register they take (`a4`, the entity).

Ordinary C cannot reproduce the scratch registers and flags the original leaves behind, so a
registered routine now carries a **contract**: the registers its callers depend on
(`CT_D0`..`CT_SP`, `CT_NZVC`, `CT_X`; `"contracts"` in `analysis/translate.json` for
generated routines, the fifth field of a `PortRoutine` for hand-written ones). Every shadow
check now works this way:

* Memory, the PC and the stack pointer must match exactly; the dead stack below the final
  stack pointer is not compared.
* Registers and flags outside the contract that differ are copied from the original's result
  and **watched**: `tools/liveness.py --usedef work/usedef.bin` writes, for every instruction,
  the registers it reads and writes. The harness follows the original from there; a watched
  register read before it is written is a `CONTRACT` violation naming the routine, the
  register and the reading instruction; one overwritten first is dead and only counted
  ("dead registers left different" in the report). Regenerate `work/usedef.bin` whenever
  the map changes. Data registers are watched in three parts (bits 0-7, 8-15 and 16-31): a
  `move.b` into a register whose upper part differs writes the low byte and reads nothing, so
  it is not a violation; the upper part stays watched. (Watching whole registers flagged
  exactly that in `g_00901C`, at a `move.b (a0)+,d4` that the rest of the routine only uses as
  a byte.)

Static liveness alone was not enough: past indirect jumps it has to assume every register is
live. Watching at run time decides it for the paths the game actually takes, which is what the
shadow runs check anyway. It also applies to the generated code: `g_00901C` now calls the
readable platform code, which leaves `a2` different; the game never reads it (3 calls in the
demo), and that is allowed.

A movement routine that ran the original's QueueSound busy-wait inside a check froze, because
the Z80 is held during checks; the harness lets sound time pass at $38E while a check is open,
the same way `port_idle` does for the 68000.

Result: 0 mismatches and 0 contract violations over the standard runs (new game, demo, the
eight `platform.state` runs) and the castle and after-boss states; 50,000 fuzz cases each from
`platform.state` and `demo1500.state` with 0 mismatches, every instruction of both originals
covered (97 and 159). The self-test (`WBMW_PORT_BREAK=PlatformsVertical`) is caught 100 out of
100 times. Fuzzing found the one difference reading missed: the original tests the riding
flag with `tst.w`, a word read, not a byte.

#### The rest of movement (2026-10-11)

All of movement with collision is now ordinary C in `port/movement.c` (about 1,100 68000
instructions): the seven entry points, the steps they are built from, the conveyors and the
water currents. The map lookups (`MapBlockAt`, the tile types) are shared with the rest of the
game and stay in `port/map.c`; movement calls them.

| Entry | Address | Used by | Made of |
|---|---|---|---|
| `MovePlayer` | $2E38 | the player | platforms, walls on both sides, slope walking, ceiling, floor with slopes |
| `MoveFlat` | $2E9E | 1 caller | walls and floor at the centre |
| `MoveOnSlopes` | $2EDE ($2ED4 without conveyors) | 8 callers | walls at the centre, slopes |
| `MoveOnSlopesWithCeiling` | $2F34 | 1 caller | the same, and the ceiling at the centre |
| `MoveWithPlatforms` | $2F8E | 8 callers | platforms, walls ahead, floor and ceiling under the whole box |
| `MoveWithPlatformsAndSlopes` | $2FD0 | 3 callers | like the player, without conveyors |

The steps (`WallsBothSides`, `SlopeWalk`, `FloorWithSlopes`, `CeilingSpan`, ...) are registered
as well. The game reaches them only through the entry points, so from C, but registering them
gives each its own fuzz target and coverage range.

Things the original does that the C keeps, written out:

* **The last map cell.** The player's caller stores d6/d7 after `MovePlayer` ($DD6A) as the
  last cell stood on ($107FC copies it into a list at $FF9F34). They are the last cell any
  scan looked at, moved on by one when a scan steps past its last cell; `MovePlayer`'s contract
  includes d6 and d7, and the C keeps that cell (`cell` in movement.c). The upper half of d6
  is 0 after the long scans and the caller's own on the early slope path, as in the original.
* **A continuation.** `$3584` stores `$38E4` at `$FF9EE8` and later jumps through it, inside its
  own stack frame, to go on to the plain floor check (`$356E` stores -1 instead). The C writes
  the same value and makes it an ordinary call.
* **A hang.** The slope table jumps for slope 0 to `nop; bra *` ($3888), and past slope 10 out of
  the table. No tile the game uses gets there; the C stops with a message instead of looping.

Each entry point leaves plane A's layer in a3, as the original does; nothing else.

Fuzzing caught one thing reading missed: on a hit, `$322E` stores the register it has just
tested the wall's type with (`andi.w #$102,d2; subq.w #2,d2`), so it stores 0, not the type.
The C stores 0 too, with a comment. Fuzzing the movement routines uses plane A's layer only,
since that is the one they use; `Currents` has its own input generator (water-current tiles,
speeds near the limit).

Results: 0 mismatches and 0 contract violations over the standard runs (the movement code runs
inside `g_00901C`, the gameplay frame, whose memory is compared exactly every frame) and over
3,000 frames of scripted walking and jumping from five owner states (castle, after-boss,
after-boss2, beforedeath, ocarina). 20,000 fuzz cases for each of the 23 new routines from each
of `platform.state`, `demo1500.state` and `castle.state`, 0 mismatches. Every instruction of
the originals is covered except the endless loop at $3888. The self-test (`WBMW_PORT_BREAK`)
is caught 100 out of 100 times for `MovePlayer` and `FloorWithSlopes`.

#### A door the translation missed (2026-10-11)

Fuzzing `PlayerState` called the item routine `$A8A6` (stored at `$998A` for up+A by the code
at $A806) and found that the generated code had no entry for it. The owner's next save, at a
temple door, then stopped the native build: "no translation for 00A800 (jumped to from
00A7E8)". `$A7E8` is `jmp $a7ec(pc,d0.w)` into a table of `bra.b`; the tracer counts entries
while they are `bra` and found 10, but the door type's offset 20 lands on the 11th "entry",
which is plain code (`bsr.w $a88c` at $A800) that the table runs into. `analysis/hints.json`
now names $A800 as an entry; it was the only branch table followed by code the trace had left
as an orphan. The ROM still rebuilds byte for byte.

#### The player's states (2026-10-11)

`port/player.c` holds the player's state machine (`PlayerState`, $E2C6-$ED5B, 708
instructions): each frame the player's update ($DD08) runs the handler for the state at
`-$31FF(a4)`:

| State | Handler | |
|---|---|---|
| 0 | $E346 | hurt: blinks, then stands |
| 1, 9 | $E374 | standing |
| 2 | $E3C4 | walking |
| 3 | $E3EC | in the air |
| 4 | $E44C ($E4CA in form 1) | in water (form 1 swims) |
| 5 | $E5FC | ducking |
| 6 | $E98A | the up attack (up+B with swords 4-6; sound $31, hit points at $EE70) |
| 7 | $E62E | on a ladder |
| 8 | $E71C | on a vine (of the map, or hanging from a platform) |

A handler checks in turn whether to use an item, attack, grab a ladder, jump, walk or duck. In
the original each check ends the handler itself when it acts, by unlinking the handler's frame
(`unlk a6; rts` from inside the check); in C a check returns 1 and the handler returns.

Code that is not rewritten yet is called through `call_68k` (`port/world.c`): it pushes a
return address no code has, runs the routine through the generated code's `rt_dispatch`, and
stops the program if it does not come back. Small wrappers (`queue_sound`) keep the 68000
registers out of the readable code. The map lookups movement.c has are shared through
`port/world.h`.

The original's leftovers needed three refinements of the register watch, each a case of a
difference that is moved around but never used:

* **Saved and restored.** `$E2C6` links with a positive displacement (`link.w a6,#$2`), so the
  handler's first call overwrites half of the saved a6 and the original returns with a6
  changed. `link` only saves the register and `unlk` restores it: the watch now keeps the
  watched state with the frame and gives it back at that frame's `unlk`.
* **Into the flags.** A shift by a constant sends the bits it shifts out only to C and X; the
  run-time table has a third column for those parts (`lsl.w #8,d5` after a byte load).
* **Between parts.** `swap` and `ext` move a difference between a register's parts.

`WBMW_WATCH_BREAK=NAME` scrambles every register outside NAME's contract in its C result; the
watch must report it (it does, in all 1,109 checks of `g_00901C` in the demo).

The fuzz generator for `PlayerState` puts the player in a valid state and form with random
buttons, often paints ladder tiles into the map at its hands and feet, or puts a platform
carrying a vine next to it, and picks the item routines from those the game stores. Results: 0 mismatches over
the standard runs and the five owner states, 60,000 fuzz cases (three states), every
instruction covered; the self-test is caught 100 out of 100 times.

#### The player's frame, batch 1 (2026-10-11)

Six more of the routines the player's update ($DD08) runs, in `port/player.c`:

| Routine | Address | |
|---|---|---|
| `PlayerStartFrame` | $E1A6 | clears the attack and hit boxes, loads the speed settings, counts the timers down |
| `PlayerInput` | $DE6A | the buttons from the controller or a script (input mode `$FF9F05`: 0 pad, 2 walk right to x = $1140, 4 still, 6 a routine at `$FF9F22`, 8 none); an item used with up or down keeps that direction while held; `PRESSED` = newly down |
| `PlayerBounce` | $DFA0 | a $9xxx tile under the feet springs the player up (velocity -$680, sound $33, a spark) |
| `PlayerAccel` | $EF4E | gravity, acceleration and friction. The ground's type is tested a byte at a time (`btst #n,-$30FE(a4)` reads the type's high byte): bit 9 is ice (a quarter of the acceleration, double top speed, no friction), and on icy slopes (bit 8 too) the slope's push from the table at $3E32 is added |
| `PlayerSplash` | $DD7C | entering or leaving water (or a $5000 tile) splashes at the surface (sound $47) |
| `PlayerBubbles` | $DDDE | under water, a bubble every 120 frames, sooner while moving; hurt, scattered. It writes the bubble's speed into whatever entity the spawner returned, even when no slot was free: the C does too |
| `PlayerHurtBoxes` | $EFE2 | the player's own hit-box list for the pose: knocked back, standing (by shield), the up attack (by sword) |

They call the code not yet rewritten (`AccelX`/`AccelY`/`FrictionX`, `Random`, the effect spawner
$10910, `QueueSound`, the input routines) through `call_68k`. The `PlayerState` fuzz generator
now covers them all (named `Player*`): input modes and their routines ($DB36, $D352, $D87E),
pad bits, ground types with ice and icy slopes, poses and shields, bounce and water tiles.
Results: 0 mismatches over the standard runs and the five owner states, 20,000 fuzz cases per
routine per state (three states), every instruction covered, self-test caught 100 of 100.

#### The player's frame, batch 2 (2026-10-11)

| Routine | Address | |
|---|---|---|
| `PlayerAnimate` | $F064 | the animation for the state (unless a handler chose one) and the sprite's facing; on ice the legs run faster while slipping; on a vine the climbing frames step every 8 pixels |
| `PlayerSword` | $ED5C | B swings the sword (not with A held, hurt, on $8xxx/$Axxx tiles, or within 4 frames of the last swing): the trail's effect script ($EDE0), sound $20; while the swing's animation runs, the hit points of its current step ($EE14, then $EE70, not rewritten) by form, sword and ducking |
| `PlayerSpecial` | $EF12 | A + left/right starts the special action set for that side (`$FF9F10`/`$FF9F11`, 13 kinds, likely magic) through `$11170` |

`$11170` was reached only by branching into it from $EF12, so it had no entry the C could call;
`analysis/hints.json` now names it (the ROM still rebuilds byte for byte). The animation table at
$F080 has entries for states 0-8 only; the state handlers also accept 9, so `PlayerAnimate`
stops with a message on state 9, where the original would jump into the following code.
Results: 0 mismatches over the standard runs and the five owner states, 20,000 fuzz cases per
routine per state (three states), every instruction covered, self-test caught 100 of 100.

#### The player's frame, batch 3: damage and pickups (2026-10-11)

| Routine | Address | |
|---|---|---|
| `PlayerDamage` | $DFDE | being hit (`-$2AFD`: bit 7 hurts: knocked back, the hurt state, the hit's damage after armour `$4964`; bits 4 and 6 push back only), then the surroundings: a hot area without the armour for it, a $Dxxx tile, harm signalled in `$FF9854`, $3xxx tiles at the feet, body or head; then the health. At 0 the revival item (`$2A`, its count at `$FF959D`) refills half the hearts, or the player dies |
| `PlayerGold` | $F1EC | gold: a random amount by kind, the total kept to 999,999, the message and the display |
| `PlayerItem` | $F256 | an item ($26) |
| `PlayerHeart` | $F262 | $80 health, up to the maximum |

The pickups are three of the targets of $F1B0's jump table; $F1B0 itself and the full heal
($F282) stay generated for now.

**Leaving instead of returning.** The revival refill ($F288) and death ($E22E) do not return:
they reset the stack and yield. Readable C cannot yield yet, so it hands over: `port_continue_at`
(`port/world.h`) sets `c->ret` to the address, with any return address the original would have
on the stack there (`$DFE0` when reached from inside $E04E). Generated code now calls hand ports
as `c->ret = 0; H(c); rt_after_hand(c)` (`port/m68k_rt.h`): a zero `ret` means the routine
returned (do its rts); otherwise dispatch there, and carry on only if control comes back. The
harness does the same for hand ports the 68000 reaches, and a shadow check then closes where the
C left (PC and stack pointer). `analysis/hints.json` names the targets ($E22E, $F288) and two
routines the C calls that were reached only by branching into them ($11170, $114C0).

Results: 0 mismatches over the standard runs and the five owner states, 20,000 fuzz cases per
routine per state (three states; the generator sets health near zero, the revival item present
or not, gold near the cap, the hit and harm flags), every reachable instruction covered (two
unreferenced `rts` are not), self-test caught. With the stack pointer deliberately broken, a
case that hands over can never meet the original at that point, so a broken run stops after
reporting it.

### Tasks and `Yield`: stackless now, coroutines for the rewrite (revised 2026-10-10)

The first decision (2026-10-10) was stackful coroutines: each task on its own C stack, `Yield()`
a call that switches to the scheduler. Before building it, a survey of the original settled the
question the design depended on. `WBMW_YIELD_SURVEY=1` (see `harness/README.md`) logs, for every
call of `Yield`, where the task resumes and where `Yield`'s `rts` goes. Over the new-game and
demo runs and four platform runs, all 43 resume points seen return straight to a scheduler:
`$4BD8` (system slots, 42 of them) or `$4C8A` (user slots). No task yields from inside a nested
call. So the original tasks are **stackless**: between passes a task is its resume address (slot
+$C) and RAM, and each pass starts with only the scheduler's return address on the stack.

With that, the owner chose **stackless now, coroutines later**:

* Tasks are translated like any other code. The instruction after each call of `Yield` (or of a
  routine that jumps to it, such as `YieldIfLate`) is a resume point and becomes an entry. The
  translated `Yield` pops the resume address into the slot and returns to the scheduler, as the
  original does, and the generated callers unwind (the return-address convention).
* Each task pass is a call from the scheduler (`jsr (a0)` at $4C88 or $4BD6), so the existing
  shadow check compares every pass exactly, with no new machinery.
* The scheduler and the frame loop (`$4BBC`-`$4C00`) move to C last, once everything they call
  is C.
* The coroutine library belongs to the idiomatic rewrite, where `while (1) { ...; yield(); }`
  reads better than resume points. It has not been built.

#### Coroutines (2026-10-11)

The player's death and heart refill wait for frames from deep inside the gameplay frame: the
original resets the stack to the task's base (`$FF9662`, saved by the gameplay loop at $9014),
yields, and later jumps back to just after the player's update (`$FF9F1E`) or to the top of
the loop ($8FFC). Readable C does the same with coroutines:

* `port/coro.c`: coroutines on their own C stacks (Windows fibers; ucontext elsewhere).
* `port/task.c`: the scheduler's two `jsr (a0)` ($4BD6 system slots, $4C88 user slots) are
  ported (`TaskCallSystem`, `TaskCallUser`). Each slot's pass runs on that slot's coroutine.
* `task_wait_frame(c, resume)` (`port/task.h`): from anywhere in a pass's call chain, store
  `resume` as the slot's resume address (as `Yield` does), keep the 68000 stack the chain uses
  (up to the pass's base), and suspend; the scheduler carries on as if the pass had ended. When
  the scheduler next runs that slot at that address, the chain carries on where it stopped.
* `task_end_pass(c, resume)`: end the pass and abandon the chain (the original's "reset the
  stack and yield to the loop's top").
* Anything else that starts the slot (another address, a loaded state, a reset: `task_reset`)
  discards the waiting chain and runs the pass afresh at the slot's address: the original's
  state in RAM always decides, so save states need nothing new.

Why it is exact: the original resets the stack to the base the gameplay frame had, and the
frame's code after the player's update runs with that same stack pointer; so "wait inside the
update, then return from it" is the same program. The checks still compare every task pass
(the ported `jsr (a0)` is the checked routine now).

With this, the player's update itself is C (`PlayerUpdate`, $DD08): the probes, damage
(death and revival now run as C loops: `dying`, `refill`), the frame start, input, pickups
(`pickups`, $F1B0, including the full heal), bounce tiles, the states, A + left/right, the
sword, hit boxes, acceleration, the animation, `UpdateAnim`, water, currents and `MovePlayer`;
then it hands over to the special actions' frame ($1141A).

What it needed besides:

* **Every routine the readable C calls must be generated.** When the gold pickup became C,
  nothing generated called $1FCA any more and it dropped out of the harness's translation;
  `call_68k` then had nothing to call. `tools/m68k2c.py` now scans `port/*.c` for the
  addresses passed to `call_68k`, its wrappers and `port_continue_at`, and makes them entries.
* **A check that closes at an instruction**: the instruction's own writes now count against the
  watch that check arms (`$1141A`'s `tst.b` cleared the flags, but the check had closed just
  before it, so the next `bmi` looked like a read of a stale difference).
* `analysis/hints.json` names `$1141A`, where the update hands over.
* `--poke ADDR=HEX` (harness) writes bytes into RAM after loading a state, to set up a scene:
  `--poke FFDA00=0080 --poke FF9854=01` from `castle.state` is a deadly touch at low health;
  with `--poke FF959D=00` there is no revival item.

Results: 0 mismatches over the standard runs (every task pass checked); the death and revival
scenes above, 900 frames each, 0 mismatches: the fall and GAME OVER, and the hearts refilling
to half. In replace mode the death scene matches the original frame for frame until the title
screen, which the C version reaches a little earlier (the known time shift).

#### Magic (2026-10-11)

`port/magic.c` holds the special actions, $11170-$11BA3: A + left/right starts one ($11170,
13 kinds), and a spell in progress (`$FF9F7A` bit 7) runs each frame after the player's update
($1141A). What the kinds are:

| Kind | Start | Each frame |
|---|---|---|
| 0 fire spirits | $11338: seven flames from the free magic slots (needs an enemy) | $11822: the spirits home in, hit, burst |
| 1 quake | $11316 | $11590: the world freezes, the screen shakes, a rock falls on each enemy on the ground |
| 2 thunder | $112E8 (needs an enemy) | $11740: a bolt crosses the screen; then three white flashes and every enemy is hit |
| 3 power | $111E2: `$FF9F03` bit 5 | $11454: until the meter ($FF9F16, $2000, -8 a frame) is empty |
| 4, 6, 7 shield | $1129E: `$FF9F03` bit 6 | $11474: the meter runs down; a hit on the player throws back sparks ($114C0), each costing meter |
| 5 | $111CE: `$FF9962` = $B | |
| 8-11 potions (items 43-46) | $1125C: the hearts refill by $100, $300, $500 or fully ($F288) | |
| 12 | $11204: `$FF9962` = $A and player animation set $140A, in areas 2-3 | |

Magic damage is a quarter of the player's maximum health (fire spirits: 1/32), divided by 4
per step of the enemy's resistance to that magic (two bits of the enemy's -$2300 word; 3 is
immune). Each use costs one of the spell's count at `$FF95B8 + kind` (the HUD's number).

The quake and the thunder's flashes take over the gameplay task: the original resets the stack to
the task's base, freezes the world and yields, then each pass of the task runs one frame of the
spell (the resume addresses $115BC, $115DE, $11626, $1163E; $11792) until it restarts the loop at
$8FFC. In C each is a loop with `task_wait_frame`, and each resume address is a registered entry
too (`QuakeShaking` .. `ThunderFlashes`), so that a pass starting there with no loop waiting
(a state loaded mid-spell) runs the same C from that point. Off a task pass (fuzzing) the frame
hands over to the original's code with `port_continue_at` ($11590, $11780, and $F288 for a
potion); `task_on_pass(c)` (port/task.h) tells which.

Quirks of the original kept as they are:

* A fire spirit whose enemy is gone looks for the next nearest, but steers that frame at entity
  $80, where the search loop stopped, not at the enemy it found.
* Casting fire spirits with fewer than seven free magic slots uses up the spell and does nothing.
* "Is there an enemy?" ($11B4A, $11B76) answers with the flags of the first enemy's health, so an
  enemy whose health has bit 15 set counts as none.
* A shield spark's 8-frame free period is cleared again at once by `ClearEntity`, so every spark
  costs meter.

How it was checked: the standard runs; every kind cast (A + left/right, `--input`) from five
owner states with the spell's count poked in (`--poke FF9F10=kk --poke FF95B8+k=05`), longer
runs for the power and shield meters, the potions with `--poke FF959E=03` (the potion slot:
`$23E4` has item 43+n when n <= that byte); and 20,000 fuzz cases each of `MagicStart`,
`MagicFrame` and `MagicShieldHit` from three states (`harness/hybrid.c`, "Magic"), where the
task's stack base ($FF9662) is set to the case's return address so the original's stack reset
returns there too. The parts only reachable several frames into the quake (no free slot for a
rock, the last enemy of the scan, the screen settling from below) were set up mid-spell with
`--poke-at`. Results: 0 mismatches everywhere, every instruction of $11170-$11BA3 run; a
corrupted result (`WBMW_PORT_BREAK`) is caught in every case. In replace mode, with the input
given by game frame (`--input-game`), the spirits, quake, thunder and shield runs from
`castle.state` match the original in RAM, VRAM and colours in every game frame.

#### The hearts, the item spawner, the sword's tip (2026-10-11)

The routines the player's C still called in the original most often:

* `port/hud.c`, `Hearts` ($8AA2): the eight heart cells built in `$FF95AA` (full, a quarter-
  filled one, empty up to the maximum, blanks; past eight hearts a band of "double" cells at the
  left) and drawn as two rows into VRAM. It leaves a4 the player, as the original does (its
  contract names a4).
* `port/effects.c`, `SpawnItem` / `SpawnItemAt` ($1090C / $10910): items and effects in the
  sixteen slots $80-$BC, from the kinds' table at $11094 (8-byte records: animation set,
  priority, speed limits, acceleration). With every slot busy, a kind replaces the first slot
  of a less important kind (higher priority number), else the first drawn one of the same
  priority, else it is dropped (d3 = -1, N set). Every register is as the original leaves it,
  so the routines carry no contract (all registers compared). Kept: while searching, the
  original keeps the high byte of the last record offset in the index register, and the
  table's offsets and indexes are signed words. Kinds $1000 and up (heart containers, the
  revival item: $B42A) still run the original.
* `port/player.c`, `PlayerSwordTip` ($EE70): a breakable block (collision byte 2) at the
  sword's tip is broken ($B2BA); with the power spell on, the swing's trail script leaves
  sparkles ($216) at its next points.

Checked: the standard runs, both scenes from the five owner states, 20,000 fuzz cases each
from three states (the fuzzer paints a breakable block at the tip, picks any step of the trail
script, fills the item slots with kinds of every priority), every instruction covered, the
self-test caught in every case, and in replace mode a run casting power and swinging the sword
from `castle.state` matches the original in every game frame.

#### Items and effects each frame (2026-10-11)

`port/items.c`, `RunItems` ($10A1E-$11093): each of the sixteen item slots by its kind.

* Group 0, the pickups, in four steps: appear (a dropped one keeps the speed it was given),
  pop up and fall back to where it started, then settle (gold $00-$0A rolls off, bouncing lower
  each time; gold $0C-$16 and the heart and items $18-$1C lie still), blink for 60 frames after
  240, and vanish in a puff. Touching the player hands the kind over in `$FF9F78` for the
  player's frame ($F1B0).
* Group 2, the effects: a splash or puff ($200, $202), a rising bubble ($204, $210) that pops
  when it leaves the water and drifts with currents, block debris ($206-$20C), the hit spark of
  `$FF9F30`'s attack ($20E, hurting the enemies it touches), a broken block's remains ($212,
  which writes map block $296), a pickup flying to the player ($214), a sparkle ($216).

The routines it calls read registers the calls before them left (a2 for `BodyOverlap`, a3 for
the probes), and the original carries registers from one item to the next, so the C runs those
calls on the caller's own registers, as the original does, and sets the registers the original
sets where a callee reads them. The hit spark's loop over the enemies counts with d5 and a2,
since `CheckBodyContact` could change them.

Also in this batch:

* The big items ($B42A, in `port/effects.c`): kinds $1000 and up go in the slots $E0-$FC, one
  at a time (the timer `$FF9962` = 9 keeps out the next). A heart container once the maximum is
  14 hearts, or the revival item when the player has one, becomes kind $1A instead.
* Exact kernel helpers in `port/entity.c` (no contract: every register and flag compared):
  `ClearEntityAnim` ($A20), `ClearEntity` ($A36), `FreezeEntities` ($A66),
  `UnfreezeEntities` ($A8E), `FindFreeEffect` ($27E6), `CheckGrounded` ($4362).
* The player's splash ($DD7C) now runs the spawner on its caller's registers: an item calls
  the splash and then tests "touching the player?" with the a2 the spawner left (the kinds'
  table, not the player), and the C must leave the same a2.
* Fuzzing items needs real animation sets (from the kinds' table): with a random one the
  animation interpreter loops forever.

#### The angle maths (2026-10-11)

`port/maths.c` ($EE2-$1278), exact (no contract: every register and flag compared, since
callers read d0-d6 and the flags): sine and cosine interpolated from a quarter-wave table
($EE6, $EE2); a velocity at an angle (`VelocityAt` $FA8, and 32-direction `VelocityAtCoarse`
$FC0: speed * sin, by `muls`, `asl.l #2`, `swap`); the direction from a4 to a2, 16-way
(`Direction16To` $1032) or to 1/1024 of a turn by a binary search of a tangent table
(`DirectionTo` $108C; at exactly 45 degrees its `divu` overflows and leaves d1 as it was); the
wrappers that store or return a velocity towards a2 ($1208-$1248); and `Distance` ($1252). The
fuzzer covers quarter and eighth angles, zero offsets and exact diagonals. Everything ran;
0 mismatches; a run with fire spirits (which steer with these) matches in replace mode.

#### The HUD bar (2026-10-11)

`port/hud.c`, the parts of the text system ($7600-$8900) that run during play: the window
plane's text position ($83B0, column in the high byte, row in the low), filling with a tile
($8762), strings as tiles ($786A), a string's length ($1B004). On top of them:

* `MagicUsed` ($8778): one use of a special action gone; at none left it leaves its A +
  left/right slot (`SlotEmptied`, $882C, also called when an item is used up); its name is
  shown centred in the bar's ten columns (the odd column on the left).
* `MagicMeter` ($87CE): eight cells of $400, full, half or empty, drawn only when the level's
  high byte changes.
* `HudRedraw` ($76C4): the name rows cleared, the two slots' icons (their graphics copied in by
  `CopyRawTiles` when the slot's special action changed) and the picture between them.
* `GoldDisplay` ($85A8): the gold right-aligned in six columns, at most 999,999.

The rest of the text system (the status screen, shops, dialogue) is not rewritten yet.
Checked like the others: 0 mismatches, every instruction of these routines run (the entries
$8666 and $875C belong to the status screen), the self-test caught, and replace mode matches
in casting runs that redraw the bar.

#### The enemies: the framework (2026-10-11)

`port/enemies.c`. Each frame `RunEnemies` ($133B2) runs the spawn points ($12A70, still the
original's) and then the sixteen enemy slots from `$FFA11A`: a slot's kind (group in the high
byte, 0-8; bit 7 skips it) picks one of 35 handlers through the tables at $13418 and $13422
(while `$FF9659` bit 3 is set only groups above 2 run). Group 0's ordinary monsters go through
`EnemyDispatch` ($13582): a state machine of 9 states, where the monster's kind and state pick
an action routine (the tables at $13B70 and $13DA4); `EnemySetState` ($1357E) sets the state
first, and `EnemyNextState` ($13542) moves to a pending state or the next one for the monster's
variant (the table at $13FD2).

The handlers and actions are still the original's code, called on the caller's registers as in
the original. With the dispatchers in C the translator no longer sees their jump tables, so
their 170 targets are listed in `"seen"` in `analysis/translate.json` (computed from the
tables in the ROM).

The spawn points (`port/spawns.c`, `RunSpawnPoints`, $12A70-$133B1): sixteen points per room
(`$FFA15C` + 4n), each run by its kind: triggered single spawns, reloading ones, ones that wait
for the last enemy to be beaten, a row near the player, an item unless its game flag is set,
placed and turned enemies, the room's chests, a guardian with three companions, spawns from a
map block, a scripted boss room in six phases, groups from a list of offsets, waves from a
list of positions, and one enemy when the player comes near. A point counts what it made,
what was beaten and what was lost off the screen, and stops at its maximum. Four periodic
timers (`$FFA14C`) trigger the timed ones. Where the original loops forever (a guaranteed
spawn with no free slot) the C stops with a message.

Checked: the standard runs and owner states, and 20,000 fuzz cases from each of seven states
with synthetic spawn points of the kinds that need no level data (enemy kinds from the room's
loaded list at `$FFA122`: any other kind makes the original loop forever), placed on the
screen, just off it and far. 0 mismatches. Not yet reached, because they need their rooms'
data: the guardian (kind 8, $12DCE), spawns from a map block (11, $12E66), the scripted boss
room (12, $12EE4), waves (14, $131CC) and three instructions of the chests (6). Save states in
those rooms will cover them.

The rest of the enemies, in order: the small handlers (about 20, under 70
instructions each); the medium ones; the large ones and the monsters' action routines
($14000-$1A000).

#### The enemies, batch 3: the objects ($1A464-$1AA3B, 2026-10-12)

`port/objects.c`: thirteen of the 35 handlers, the family at $1A464 that shares the off-screen
test $1A796 and the removal $1A784. They are group 2's kinds ($208-$232: things enemies throw,
fire and drop) and group 8's kinds 2-8 (a hopper, a floater, a rising column, a crawler that
drops at a floor's end); the table in the file's header names each. Each handler is a C
function over named fields, registered at its own address with the range to the next one, so
the shared code is counted under the handler that reaches it.

* **Two routines leave past their caller.** $1A796 pops its caller's return when the object has
  left the screen: in C it answers 1 and the handler returns. `DespawnIfOffscreen` ($48C4) does
  the same when it removes an enemy; `call_68k` cannot follow that, so its tests are C here
  (the off-screen routines it calls, then `Despawn`).
* **`call_68k` leaves its mark in `ret`.** A handler that ends on a call hands back
  `$FFFFFE` unless it clears `ret`, as `RunEnemies` does. The first gameplay runs showed it in
  the boss fight (`boss2.state`): the handlers run inside `TaskCallSystem`'s checks, not as
  calls of their own, so the mismatch was reported against the task. Fixed in `HANDLER`.
* **`m68k2c.py` reads named calls.** The rewrite's files call through `#define NAME 0x...`;
  the scan for routines the C still calls now resolves those names, so a routine only the C
  calls is still translated.
* **Fuzzing them** (`hybrid.c`, names starting `Obj`): the state's entities are kept, and one
  enemy slot is set up by the game's own `$12684` for one of the handler's kinds (animation set
  and stats, so the animation interpreter sees real scripts), then given positions by the
  player and the screen's edges, timers near their ends, touches, collisions, a spawn point or
  none, and random attributes (after `$12684`, which clears them).

Results: 0 mismatches over the standard runs (the new-game script, now kept in
`harness/inputs/newgame.txt`, and the demo) and 3,000 random-input frames from each of the
owner's 17 states (373,271 checked calls; the boss fight reaches `ObjFlying` and `ObjBouncer`);
325,000 fuzz cases (5,000 per handler from each of five states), 0 mismatches, and every one of
the 433 instructions of the thirteen originals covered. The self-test (`WBMW_PORT_BREAK=ObjColumn`)
is caught 100 times in 100. Registers left different are the condition codes, and for the four
handlers that move with the rewritten movement code, what that code leaves (it is allowed to);
none is read before it is written in the gameplay runs. Replace mode plays the new-game script
and four states.

Left: 21 handlers (table slots 1, 8-17, 19-21, 23-26, 28, 33, 34), then the monsters' actions.

#### The enemies, batch 4: the townspeople ($163AA-$168E7, 2026-10-12)

`port/people.c`: nine handlers, group 4 (people and props in town) and group 6's one kind. The
table in the file's header names each: people who stand, a speaker, townspeople who walk a
script, an animal that wanders between two x positions, a pose by game flag 56, two breakable
things that set their game flag when the sword hits them, and a scene (the screen dims as the
player walks past x $10E0, it speaks, and `$168E8`, left as it is, runs when the text ends).

* **Talking** ($164B8, then $1649C): no text running, the player level with the speaker and
  within its reach (-$3500), on the ground, up pressed alone; then the player stops and turns
  (animation 12, through `exg a4,a2`), and the message is TIMER's high byte.
* **The townspeople's scripts** (-$2700, when -$3200 is set; $16598): commands 0 wait, 1 walk a
  signed distance, 2 walk home (-$2100), 3 speak (a word into TIMER; it waits while text runs,
  without moving on), 4 the speak step without a new message, 5 jump. Each sets a step (0-8) of
  the table at $16578, and a step that ends runs the table again in the same frame.
* **Where the original would crash or hang, the C stops with a message**: a step or command past
  its table (the original jumps through whatever follows it), or a script that only jumps.
* **`btst #0,-$3800(a4)` tests bit 8 of x**, the high byte; the first draft read the low byte.
  Fuzzing would have caught it; reading the 68000's byte order caught it first.
* **Fuzzing them** (`hybrid.c`, names starting `Ppl`): as the objects, with each family's
  inputs: the talk conditions about the player, random valid scripts in scratch RAM (jumps
  only land on commands; a person already at home gets no walks home, which with jumps could go
  round for ever, in the original as well), the scene's player positions, dimming and attack,
  the game flags and area 15's flag word. `$12684` has no entry for group 6, so the speaker
  keeps its random fields.

Results: 0 mismatches over the standard runs and 3,000 random-input frames from each of the
owner's 17 states (373,271 checked calls; the speaker is fully run by talking in the shop and
at the inn, the townsperson partly); 225,000 fuzz cases (5,000 per handler from each of five
states), 0 mismatches, every one of the 383 instructions covered. Self-test caught 100 in 100.
Replace mode plays the new-game script and four states, three of them in town.

Left: 12 handlers (table slots 1, 16, 17, 19-21, 23-25, 28, 33, 34), then the monsters' actions.

#### The enemies, batch 5: eight small handlers (2026-10-12)

`port/handlers.c`: the dispatchers next to the framework ($135AE-$136A5: slots 1, 16, 17, 20)
and four small ones elsewhere (21 $18910, 23 $194AA, 28 $1766C, 34 $19810); the file's header
names each. Slot 17 ($060) is the Gragg & Glagg fight: the state machine, then a watch on the
place at ($1100, $1050) that opens it when the entity in `$FFA11C` stands there for 48 frames,
and the hits that blink it and its three companions (`$15E10`, `$15E2E`).

* **Three of them go on into other code** (`port_continue_at`), as their `bra` does: slot 1 to
  `$16908` in area 6 room 5, slot 16 to `$160E8` or the hopper. Slot 17 calls the state machine
  first (`bsr`), through `call_68k`, which follows `EnemyDispatch`'s own hand-over.
* **The boss state found a bug in the first draft.** Slot 16 chose its target with `?:` inside
  one `port_continue_at`; the scan for what the C calls (`m68k2c.py`) only read a name in first
  place, so `$160E8` was not translated and `call_68k` stopped. The C now makes one call per
  target, and the scan reads every defined name in a call's arguments.
* **Fuzzing them** (`hybrid.c`, names starting `Hnd`): as the objects, with valid steps for each
  table (including `$16908`'s in area 6 room 5, and the guardian's second-level table at
  `$15B44`, which jumps through whatever follows it otherwise), the guardian's flags, counts and
  target by its place, and its companions in a list at `$FFF800`. Two of its paths loop for ever
  in the original as well, and the generator keeps out of them: kind `$800` missing from the
  room's list (`$4910`, from step 6), and step 0's first frame without three free entities.

Results: 0 mismatches over the standard runs and every owner state (25 runs, 402,685 checked
calls; the boss fight runs 49 of the guardian's 60 instructions); 200,000 fuzz cases from five
states, 0 mismatches, every one of the 192 instructions covered. Self-test caught 100 in 100.
Replace mode plays the new-game script and four states, the boss fight among them.

Left: 4 handlers (table slots 19, 24, 25, 33), then the monsters' actions.

#### The enemies, batch 6: the last four handlers (2026-10-12)

`port/handlers.c` again: 19 ($17EE6, fired off backwards, then aimed at `$FFA11C`'s entity), 24
($19C86, waits, then chases the player's height and bursts), 25 ($1A0B0, flies at its angle and
turns an eighth towards the player once) and 33 ($19544, falls, grows, stays, shrinks). Every
one of the 35 handlers is now C; what is left of the enemies is the monsters' action routines.

* **The burst** (`$19D60`/`$19D64`: kind `$218` with animation 0 or 2) is shared: 24 falls into
  it, 25 jumps to it, and `$19C72` and `$19F2E` (actions) call it. The C does it itself; the
  original stays translated for the actions.
* **Handler 25's table** at `$1A196` holds longs at 6-byte steps by its angle in eighths
  (multiples of `$20`); another angle would be an odd address and an address error on the
  68000, so the fuzz keeps to eighths, as the game does.

Results: 0 mismatches over the standard runs and every owner state (26 runs, 424,231 checked
calls, `thunder.state` among them); 300,000 fuzz cases for the twelve handlers of the file from
five states, and 25,000 more for handler 19 aimed on the frame its wait ends; 0 mismatches,
every one of the 465 instructions covered. Self-test caught 100 in 100. Replace mode plays the
new-game script and four states.

Left of the enemies: the monsters' action routines, 135 entries at `$140F2`-`$1A3E4` (about
7,000 instructions; 73 under 30 instructions, 49 under 100, 13 larger, the largest about 900).

#### The monsters' actions, batch 7: appearing and defeated ($140F2-$1436B, 2026-10-12)

`port/actions.c`, named by address (`Act140F2`, ...): six appearing actions (state 0 of most
kinds) with their shared wait and set-up ($14250, $14228, $14208, $14202), and five defeated
ones with the fade ($1432C). The file's header describes each.

* **How an action is entered.** EnemyDispatch jumps to it with d0 = the action number * 4, d1 =
  the state * 2, a0 = the action. The fuzz sets exactly that, for a kind and state the tables
  at `$13B70`/`$13DA4` actually map to the action (`$142D6` is in the table at `$13DB6` but no
  kind's row uses it: any kind, any state).
* **Helpers that drop their caller.** Much of the enemy library pops the action's return
  address and goes elsewhere: `EnemyCheckHit` ($43BC: hit, so `EnemySetState` with state 2),
  and `$436C`, `$4428`, `$4444`, `$4474`, `$4592`, `$45DC`, `$462A` on their own tests.
  `call_68k` cannot follow that, so an action that calls one does the test in C and goes on as
  the original does (`go_on`, then returns). This batch calls none of them; the next ones will.
* **`bra EnemyNextState`** and **`jmp EnemyDefeated`** return normally to the action's caller,
  so the C calls them and returns.
* **`tools/accept.sh`** now runs the whole acceptance for a list of routines: fuzzing from
  five states with coverage, the standard runs and every save in shadow mode, coverage, the
  self-test and replace mode. Each state's random input is made once and kept in
  `work/runs/inputs/`.

Results: 0 mismatches over the standard runs and every owner state (26 runs, 420,931 checked
calls; play reaches four of the eleven); 275,000 fuzz cases from five states, 0 mismatches,
every one of the 170 instructions covered. Self-test caught every time. Replace mode plays the
new-game script and four states.

#### The monsters' actions, batch 8: hurt ($1436C-$14517, 2026-10-12)

Six of state 2's actions, in `port/actions.c`: the first frame clears the hit records and takes
the damage (beaten: state 1), then the monster blinks and is knocked back; each frame after
(`$144E8`) it can be hit again, and after TIMER frames it goes on to the next state. `$1436C`
and `$14394` stand, `$143BC`, `$143F2` and `$14420` are knocked back and fall, `$14456` drifts
in water and comes to rest at step 3 or 7.

* **`bra EnemySetState`** hands over to the new state's action: the C sets d0 and calls
  `EnemySetState` (enemies.c), which carries on there (`new_state`). `$14456` reaches the hurt
  frame by `bsr`, so there the new action returns into `$14456`, which carries on: the C calls
  `$1357E` through `call_68k` instead, and goes on with its own code.
* **The self-test** corrupts the stack pointer of the C's result; when the C has handed over to
  another action (`$1436C` beaten), that action's `rts` reads the corrupted stack and the
  original then runs into garbage, so a self-test run of `$1436C` stops early (37 cases caught,
  then the 38th does not return). Every case that returns is caught; plain fuzzing never hangs.

Results (with batch 7's actions again, the fuzz having changed): 0 mismatches over the standard
runs and every owner state (420,931 checked calls; play reaches `$143BC` in full and part of
`$14456`); 425,000 fuzz cases from five states, 0 mismatches, every instruction of the seventeen
covered (118 of them this batch). Replace mode plays the new-game script and four states.

#### The monsters' actions, batch 9: walking, hovering, shooting ($14518-$14821, 2026-10-12)

Seven actions in `port/actions.c`: `$14518` stands a random while; `$1454C` the same until it is
`$168` frames old, then three shots and gone; `$145C4` hovers about its home height and swoops
(state 7) on a player below; `$1463C` walks; `$14682` stands shooting; `$146FC` hops, shooting
once per hop; `$1476C` rises `$60` and waits for the player below. Shots come from `$15182`
(`$15A28`, `$159FA`, `$1A438`: set up by `$12684`, no room-list lookup).

* **The first actions to call the library helpers that leave past them**: `EnemyCheckHit`
  (`hit()`: state 2 through `new_state`, the action returns) and `DespawnIfOffscreen`
  (`despawned()`, the C of `objects.c`, now shared through `world_despawn_offscreen`).
* **A hang in the fuzz, not in the game.** Randomising the action's flag byte at `-$27FE`
  gave the appearing actions a bad saved animation set there (the same byte), and the
  animation interpreter, which is C, went round for ever: no cycle budget stops C. The flag is
  now set only for `$1454C`, and `tools/accept.sh` gives every harness run a time limit
  (`LIMIT`, 1,200 seconds) so such a loop ends the run instead of stalling it.
* **The frame counter** is random in the actions' fuzz now: every one of the five states had a
  frame number with bits 0-2 set, so `$1476C`'s every-eighth-frame check never ran before.

Results (batches 7-9 together, the fuzz having changed): 0 mismatches over the standard runs
and every owner state (420,931 checked calls); 600,000 fuzz cases from five states, 0
mismatches, every instruction of the twenty-four covered. Self-test caught every case that
returned (`$1436C` and `$14682` hand over to other actions, so their runs stop early). Replace
mode plays the new-game script and four states.

#### The monsters' actions, batch 10: choosing ($14822-$14C4F, 2026-10-12)

Ten actions in `port/actions.c` that stand until their wait is over, then choose a step:
`$14822`, `$14888` (with the shield, and its moves from its kind's stats: `$15A3A`, `$15A76`),
`$1492A`, `$14974`, `$14AEA`/`$14AFA` (a range of their own in VAR), `$14B7E` (rows for water);
`$149D0` walks back home first; `$14A82` turns to a player it faces close by; `$14C1A` waits
for the player, then falls.

* **`$42AA` leaves past the action too** (`addq.w #4,a7` at `$42C0`), unless the monster is on
  the ground and its wait is over; the first search for such helpers started at `$4300` and
  missed it. The whole library (`$3F00`-`$4A00`) leaves past its caller at `$42C0`, `$4378`,
  `$43C4`, `$4432`, `$444E`, `$447C`, `$45A0`, `$45E0`, `$4632` and `$490C`. Its ground test is
  `$4356`'s, whose flags on the ground are FrictionX's: the C reads the flags after the call.
* **`$428C`** picks the step: a random 0-7 plus the row (d2) indexes a table of bytes, 0 stays,
  negative goes on to the next state (`pick_step`).
* **`$14B7E` tests its first frame the other way round** (`beq` after `bset`): it sets its
  animation and wait on every frame but the first. The C does the same.

Results: 0 mismatches over the standard runs and every owner state (420,931 checked calls);
250,000 fuzz cases from five states, 0 mismatches, every instruction covered. Self-test caught
100 in 100 for each. Replace mode plays the new-game script and four states.

#### The monsters' actions, batch 11: patrolling and jumping ($14C50-$14FD1, 2026-10-12)

Twelve actions in `port/actions.c`. The patrols walk for a while and turn round at walls
(`$4428`), touches (`$4444`), the ends of floors and platforms (`$462A`, from `$4646` and
`$4686`) and a random range from home (`$45DC`): `$14C50`, `$14CA2`, `$14CF0` (away from a
player close by; at a wall it jumps up where there is room, `$45AC`), `$14D74`, `$14DC8` (with
the shield), `$14E1A` (runs from a player `$50` away), `$14E6C` (charges until something is in
the way). The jumps go on until landing: `$14EC8` (straight up), `$14EF4` (on platforms),
`$14F28` (slow and high in water), `$14F54`, `$14F80` (with the shield).

* **The turns** each drop the action's return when they turn the monster (`$448C`: the other
  way; `$445C`: away from the touch); the C does the test, makes the same turn and returns.
* **`$140E0`**, which `$14CF0` jumps to, makes a detour: the state in d0 now, the current one
  pending in -$2200, for EnemyNextState to come back to.
* **`bsr EnemyNextState`** in `$14DC8` and `$14F80` (a touch backs it off) returns into the
  action, which carries on with its frame; the C calls it and carries on.

Results: 0 mismatches over the standard runs and every owner state (420,931 checked calls);
300,000 fuzz cases from five states, 0 mismatches, every instruction covered. Self-test caught
every case that returned (`$14C50` hands over to the hurt state when hit, so its run stops
early). Replace mode plays the new-game script and four states.

So far the actions cover `$140F2`-`$14FD1`: 46 of the 135 entries.

#### Faster builds and acceptance (2026-10-12)

The translated routines were one C file of 11 MB (harness) or 15 MB (native), which GCC
compiles on one core: about 5 and 10-12 minutes, every batch. `m68k2c.py` now writes them in 12
parts (`gen_00.c`-`gen_11.c`, declared in `gen.h`; `"parts"` in the config), and the Makefiles
compile the parts in parallel: with `make -j12` about 80 s for the harness and 150 s for the
native build from scratch, and nothing when the translation did not change (one stamp per
translation, so a parallel make runs `m68k2c.py` once). `tools/accept.sh` runs every fuzz run at
once, `JOBS` (12) at a time: a batch's whole acceptance takes about a minute and a half. The
native build is built and smoke-tested once at the end of a session, not per batch.

#### The monsters' actions, batch 12: leaping and attacking ($14FD2-$15351, 2026-10-12)

Nine actions: `$14FD2` floats at the top of a jump; `$14FFE` and `$15010` leap at the player,
farther the farther it is (`$15020`, sometimes twice as far by the bits of `$15064`); `$15066`
hops three times on the spot, then leaps; `$150D8`, `$15126` and `$151A0` attack (jumping,
hopping, standing) and fire the shot `$15182` on the frame the animation marks (attribute bit
0); `$151D6` walks or hops at the player with the shield and attacks close by, its hit records
from its kind's attacks frame by frame (`$15A4E`); `$152A0` hops 4-7 times. `$4438` turns at a
wall by a branch, not by leaving past its caller, so it is called as it is. The jump's frame
(`$14FBC`, `$14FC0`) is shared with batch 11 (`jump_frame`).

Results: 0 mismatches over the standard runs and every owner state (420,931 checked calls; play
reaches most of `$15126`); 225,000 fuzz cases from five states, 0 mismatches, every instruction
covered. Self-test caught every case that returned. Replace mode plays the new-game script and
four states. 55 of the 135 actions are C.

#### The monsters' actions, batch 13: more attacks, a dive ($15352-$155E1, 2026-10-12)

Seven actions: `$15352` and `$154B2` attack with their kind's hit records frame by frame (with
the shield's touch, `$46F4`); `$153BE` plays its animation and goes on; `$153F0` fires a shot it
keeps for 60 frames, and if it is hit meanwhile removes the shot before going to state 2 (its
own `EnemyTakeHitFlag`, not `EnemyCheckHit`); `$15464` shoots 8 times (16 one time in 64);
`$15504` hops forward and turns to attack on the way down, until it faces away from the player
or meets a wall; `$15576` dives to `$10` above the target's height and flies back up home.

Results: 0 mismatches over the standard runs and every owner state (420,931 checked calls);
175,000 fuzz cases from five states, 0 mismatches, every instruction covered, the generator
unchanged. Self-test caught 100 in 100 for each. Replace mode plays the new-game script and
four states. 62 of the 135 actions are C.

#### The monsters' actions, batch 14: swimming, flying, hopping ($155E2-$159F9, 2026-10-12)

Eleven actions: `$155E2` swims after the target until level with it; `$15630` swims about it,
jumping out of the ground first, biting (step 6) from just above the player or going to step 3
level with it on the ground; `$15704` bobs about a height that moves now and then; `$1577A`
stands 20 frames; `$157A6` is a bare `rts`; `$157A8` and `$157E4` hop three times on the spot;
`$15830` flies back home; `$1589A` faces the player, then charges at four times its speed;
`$158E6` climbs up and down a thread hanging from its partner (LINK2), giving the thread the
animation of its length (`exg a4,a2` around SetAnim); `$15994` walks to a wall or a stop, then
hops. `$157A6` and `$15830` are in the table but in no kind's row (fuzzed on any kind and state).
`$15994`'s range ends at its own code (`$159FA`): the shot's helpers after it are reached only
through `$15182`.

Results: 0 mismatches over the standard runs and every owner state (420,931 checked calls);
275,000 fuzz cases from five states, 0 mismatches, every instruction covered, the generator
unchanged. Self-test caught every case that returned. Replace mode plays the new-game script
and four states. 73 of the 135 actions are C.

#### The monsters' actions, batch 15: Gragg & Glagg ($15A86-$16029, 2026-10-12)

Six actions of the boss's kind ($060): `$15A86` appears with its three companions (a group
record from `$127FE`, three free entities), falls, lands with a shake, and in the boss room throws
up four rocks, drops back and opens the fight (message `$67` the first time); `$15C9E` is
defeated (the floor opened by `$2812`, its partner told, puffs for 16 x 4 frames, then gone with
its companions); `$15DD2` is hurt; `$15E54` waits and fires in turn; `$15EE6` rises bringing up a
companion object (kind `$800`) and sinks back; `$15FB0` rises and sinks when its group says.

* **Two more helpers leave past the action**: `$15F9C` (its group calls it back: step 8) and
  `$1600C` (the touch; outside the boss room it ends in `jmp EnemyCheckHit`). The group's other
  helpers (`$15C84`, `$15DA4`, `$15E10`, `$15E2E`, the glide home `$1602A`) are called.
* **Where the original waits for ever**, the C stops with a message: no free group record
  (`$15AD2`) or entity (`$15AE4`).
* **The fuzz found two holes in its own inputs**, not in the C: a sub-step of `$15B44` past the
  table, and an enemy size of 0, which the puffs divide by (`RandomSigned`, `$4340`): a division
  by zero stops the translated code. Sizes are now 4-31, as the animations give them. Kind
  `$800` is put in the room's list and room made for the companions, as for the guardian handler.

Results: 0 mismatches over the standard runs and every owner state (420,931 checked calls; the
boss fight runs most of `$15EE6` and `$15FB0`); 150,000 fuzz cases from five states, 0
mismatches, every instruction covered. Self-test caught 100 in 100 for each. Replace mode plays
the new-game script and four states. 79 of the 135 actions are C.

#### The monsters' actions, batch 16: on the water ($16D70-$17013, 2026-10-12)

Four actions of kinds `$1E`, `$26` and `$3C`: `$16D70` swims along the surface (its height read
from the map column by column), turning at walls, at touches from behind and at its range, and
goes on when the player is `$10`-`$6F` away in front; `$16ED0` is hurt, knocked away from the side
it was hit on (hit again while sliding, the damage is taken again); `$16F50` swims about its
home height burning what it touches and now and then leaps at a player below; `$16FF0` is the
leap. `$16E8E` (the sword's hit: state 2, leaving past the action; the touch hurts the player by
20 less its armour, a quarter for kind `$26`, at least 1) is C; the other helpers are called.

Results: 0 mismatches over the standard runs and every owner state (420,931 checked calls);
100,000 fuzz cases from five states, 0 mismatches, every instruction covered. Self-test caught
100 in 100 for each. Replace mode plays the new-game script and four states. The native build
(built once, at the end of the session) plays four of the owner's states. 83 of the 135 actions
are C.

#### The monsters' actions, batch 17: the hopper and the chain ($17014-$173A9, 2026-10-12)

Four actions. `$17014` (kind `$42`) waits about its home height and, with the player ahead within
`$60` and below the right height, crouches (TIMER: the height / 3, a `divu` of the whole of d0)
and springs, then swings about for `$15B` frames. The chain of kinds `$2E`/`$34` moves through
`$17132`: a follower traces the link ahead of it two frames late (the offsets in -$2A00/-$2900);
the head wanders about its home by its own offsets, every 16 frames towards the target, grows
the chain (kind `$2E`, up to 5 links) and, when no link is on the screen, removes the whole chain
and leaves past the action, so `$17132` is C. `$1729A` (a link defeated: the chain closes up, or
the next link becomes the head with its looks copied, `$1730A`) then goes on as `$142E4`;
`$17310` is a link hurt; `$17362` a link moving. The fuzz builds chains that end (a loop of links
goes round for ever in the original too) and puts kind `$2E` in the room's list.

Results: 0 mismatches over the standard runs and every owner state (422,373 checked calls);
100,000 fuzz cases from five states, 0 mismatches, every instruction covered. Self-test caught
100 in 100 for each. Replace mode plays the new-game script and four states. 87 of the 135
actions are C.

#### The monsters' actions, batch 18: bouncers, a shooter, a dodger ($173AA-$1766B, 2026-10-12)

Six actions. `$173AA` drifts as it was thrown, slowing (`$17418`, |velocity| / 64) and bouncing
off walls (`$17420`) for 60 frames, then goes on (kind `$18`, one time in two, to step 6).
`$1743C` flies at the target within an eighth of a turn of level (`$174B0`), speeding up to
`$100`, for 32 frames. `$174E6` stands and fires three shots (kind `$16`, `$17546`) when its
animation marks a frame; `$17548` is such a shot set off at the target, give or take a random
angle, at `$180`, its graphics from the room's list (`$4910`); `$17588` the shot flying.
`$1759A` burns (`$16E8E`, which leaves past the action when hit) and swims at the player about
its home height; when the player faces it with the sword out (the target's hit records) it
dodges back (`$17652`) at half its speed for 16 frames.

The fuzz gave the target a random attack-box pointer, and one case pointed it at the stack
(RAM `$09F0`, a 256-box list read through the `$FD0000` mirror): the original's stack holds its
return addresses there and the C version's does not, so the two saw different boxes. The pointer
now lands in ROM. `WBMW_FUZZ_TRACE=N` prints every instruction the original runs in fuzz case N.

Results: 0 mismatches over the standard runs and every owner state (422,373 checked calls);
150,000 fuzz cases from five states, 0 mismatches, every instruction covered. Self-test caught
100 in 100 for each. Replace mode plays the new-game script and four states. 93 of the 135
actions are C.

#### The monsters' actions, batch 19: a boss and its brood ($1771C-$179A9, 2026-10-12)

Six actions. `$1771C` raises a boss by its height and, a frame later, marks it the room's boss
(`$9748`) and sends it to state 3. `$1774E` is that boss beaten: the music stopped (`$176E6`,
unless `$9668` is `$12`), the room frozen, its tiles shown (`$177C4`), 20 puffs 4 frames apart,
then it is gone (sound 19, `$9748` = `$FF`). `$177D6` stands a random while and then walks (4)
or leaps (5); `$17822` walks, turning at walls; both leap at once when touched from behind
(`$17704`, which leaves past the action, so it is C). `$17860` leaps at the target (or back from
the player) and lands into a random state (`$178C8`). `$178D0` leaps away when five of the room's
enemies are about; otherwise it stops and lays one young (kind 8 or 6, `$1793C`, by `$12650`)
when its animation marks a frame.

The fuzz now puts kinds 8 and 6 in the room's list (`$12650` looks for its kind there for ever),
gives timers their last frame, clears `$178D0`'s laid flag (otherwise the saved animation set's
high byte), and keeps the body size at -$3500/-$34FF above 0: the puffs divide by it, and a
division by zero stops the translated code.

Results: 0 mismatches over the standard runs and every owner state (422,373 checked calls);
150,000 fuzz cases from five states, 0 mismatches, every instruction covered. Self-test caught
100 in 100 for each. Replace mode plays the new-game script and four states. 99 of the 135
actions are C.

#### The monsters' actions, batch 20: the two-part boss ($17A44-$17E87, 2026-10-12)

Five actions of a boss in two entities, a head and a body, partners through MAKER (-$2CFE).
`$17A44` makes the body and throws both in from $100 to the left (`$17AD2`); a phase byte steps
through the body's two landings and the head's two (`$17B76`/`$17B7A`), then sets the room's
walls (`$9C16`, `$9962`) and goes to state 3. `$17B8A` is the pair beaten: the head's tiles
shown, two strips of the screen cleared at `$FF5200`, the body's tiles faded from VRAM a strip at
a time (`$17C84`), the head broken up tile by tile (`$17C32`, `$182AA`) over `$80` frames.
`$17D22` is the pair hurt, knocked from the sword at `$400` and slowing; `$17DC4` rocks the head
on the body about its home, then picks a random state (`$17E44`); `$17E4C` carries it left,
`$80` past home.

The pair's helpers step a4 to the partner and back (`movea.w -$2CFE(a4),a4` twice), so the
code after them works on whatever a4 then holds. The C reads `c->a[4]` again after each of them
and does not assume the entity it began with. The fuzz gives these actions a partner that
points back, an even phase (the phase indexes a jump table), and frames near the timers' ends
and the carry's end. Reaching the carry's end caught a slip in the C: `rdw` reads a signed word,
and `$17E4C` compared it with an unsigned one (`cmp.w` then `bcs`), so x at `$8000` and above
ended the carry early. The other `rdw` comparisons in the actions are sign tests, as intended.

Results: 0 mismatches over the standard runs and every owner state (422,373 checked calls);
125,000 fuzz cases from five states, 0 mismatches, every instruction covered. Self-test caught
100 in 100 for each. Replace mode plays the new-game script and four states. 104 of the 135
actions are C.

#### The monsters' actions, batch 21: the head's shots, the rider and its mount ($17E88-$186C5, 2026-10-12)

Five actions. `$17E88` is the two-part boss's head off the body, shooting: up to five shots
(by `$18256`, offsets `$17EE4`) 30 frames apart. The other four are a second pair, a rider on
its mount, partners through MAKER like the first and handled the same way (`c->a[4]` read again
after each helper that steps to the partner and back). `$183E0` makes the mount, marks the rider
the room's boss and drops both from `$100` above: `$20` frames falling, then landed with the
screen shaken (`$183D4`), the walls set when the shake ends, and state 3. `$184C4` is the pair
beaten: the rider's tiles shown (`$18572`), the mount broken up every other frame (`$182AA`) and
`$20` puffs 3 frames apart over `$80` frames. `$18584` is the pair hurt, the mount knocked from
the sword at `$200`. `$185EC` rides about: it stands a random while, the mount slowing, then picks
the next state by the player's distance (`$186C6`, a second row between `$40` and `$B0`); in the
air it lands with a shake; touched from behind it runs (5); a pending state from before (-$2200)
decides its first frame. `$1838A`, which leaves past the action when hit, is the same C as
`$179CC` with the mount's touch (`$1839C`) in place of the head's.

The fuzz gives these a partner that points back, the drop's three phases, the shake over or not,
a pending state or none, and the mount on the ground or not.
The fuzz also gives `$184C4` its puff's frame.

Results: 0 mismatches over the standard runs and every owner state (422,373 checked calls);
125,000 fuzz cases from five states, 0 mismatches, every instruction covered. Self-test caught
100 in 100 for each. Replace mode plays the new-game script and four states. 109 of the 135
actions are C.

A slip found after the commit: batch 21 defined `SHAKE` (`$183D4`) where `SHAKE` (`$9738`, the
shake counter) already existed, so `$15A86` wrote its shake to ROM. The fuzz of `$15A86` catches
it, but acceptance only fuzzes the batch it is given. The routine is now `SHAKE_SCREEN`, batch 15
and batch 21 pass again (275,000 fuzz cases, 0 mismatches), and `tools/accept.sh` reports any name
`#define`d twice in one `port/` file among its problems.

#### The monsters' actions, batch 22: the rider and its mount, on ($186D6-$18BB7, 2026-10-12)

Four actions. `$186D6` walks the mount at the player a random while, until it is blocked
(`$183C8`). `$18710` leaps the mount: away at `$200` when a state is pending or, three times in
four, when the player is within `$40`; otherwise at the player, 8 times the distance up to `$300`;
landed or blocked, a shake. `$187AC` turns both to the player, aims from the rider's muzzle
(`$1890C`, level or down) with the angle held to its side (`$109C`, then `$FA8` for the shot's
velocity at `$200`), and fires up to six shots of kind `$21A`, 4 frames apart, after `$20` frames.
`$18ADE` sets up a party of four in one frame: a mount under the rider and two followers (kinds
+2 and +4, `$41AA`) chained through GOAL, each with its own pending state and delay; the rider
rides at `Y_OFFSET` (-$36FD) above the mount (`$18A3E`).

One slip the fuzz caught: `$18710` tests d1 again after calling `Random`, which leaves its own
value in d1; the C now reads d1 after the call. The fuzz also gives `$187AC` its first shot and,
one time in three, no free entity for a shot. `tools/quickfuzz.sh CASES NAMES...` fuzzes from
one state and lists what the original never ran, a quick look before the full acceptance.

Results: 0 mismatches over the standard runs and every owner state (422,373 checked calls);
100,000 fuzz cases from five states, 0 mismatches, every instruction covered. Self-test caught
100 in 100 for each. Replace mode plays the new-game script and four states. 113 of the 135
actions are C.

#### The monsters' actions, batch 23: a third pair, a rider and its beast ($18BBE-$19025, 2026-10-12)

Four actions of a third pair, partners through MAKER, with their own helpers (`$18A24`-`$18AD2`,
the same shapes as the mount's). `$18BBE` is the pair beaten, in six phases through a jump table:
`$20` puffs about the rider; the rider thrown off the beast step by step, a table of five bytes a
step (`$18C94`, from WORD_31FE) giving each half-step's frames and the beast's looks (`$18D74`);
then the rider alone, game flag 17 set and a routine left at `$9F22` for another to call; the
room's walls; a jump and gone. `$18E3A` is the pair hurt (the beast knocked from the sword at
`$400`); beaten, a follower (GOAL) takes the rider's place (`$18E4C`) and, in phases 1-3, becomes
its kind (`$1274A`), rises to a height of `$E0` and settles at `$E4`. `$18F62` stands the beast a
random while; `$18FC2` walks it at the player, turning back past x `$1140` or `$10C0`.

The fuzz gives `$18BBE` and `$18E3A` their phases, the step index (and, one time in four, the
last step), the half-step bit and the rider's height. `analysis/coverage_skip.txt` lists code
inside a ported range that is not the routine's, which `tools/coverage.py` no longer counts:
here an unreachable `rts` and the two little routines `$18BBE` leaves at `$9F22`.

Results: 0 mismatches over the standard runs and every owner state (422,373 checked calls);
100,000 fuzz cases from five states, 0 mismatches, every instruction covered. Self-test caught
100 in 100 for each. Replace mode plays the new-game script and four states. 117 of the 135
actions are C.

#### The monsters' actions, batch 24: the third pair's throw, and its followers ($19026-$191FF, 2026-10-12)

Five actions. `$19026` is the rider throwing: it turns the beast to the player, throws a shot of
kind `$220` (`$190C6`) and holds it in WORD_31FE while the beast's animation runs; the beast's
marks (its ATTR bits 0 and 1, cleared as they are seen) step the shot on; hit, the shot is gone and
state 2. The other four are the followers': `$190C8` readies one (speeds, flags, no health yet),
`$190E4` hides one as a puff (kind `$218`), `$19102` is one hurt (knocked from the sword at `$400`,
its animation and next state from DELAY's two bytes), and `$1916A` is one thrown off: up and away,
upside down while falling, `$78` frames lying and `$1E` more, then its health back and state 8.

The fuzz gives `$19026` a held shot, the beast's marks and, one time in three, no free entity,
and gives `$1916A` its phases. The self-test of `$19102` stopped at its fifth case (caught 4 in
4): the known early stop of an action that goes on to another state after the deliberately broken
run.

Results: 0 mismatches over the standard runs and every owner state (422,373 checked calls);
125,000 fuzz cases from five states, 0 mismatches, every instruction covered. Replace mode plays
the new-game script and four states. 122 of the 135 actions are C.

#### The monsters' actions, batch 25: a hoverer and a creature that runs round the room ($19200-$19489, 2026-10-12)

Three actions. `$19200` hovers about a height (`$1032`, or `$1070` when it picks high, one time
in 16), drifting at the player unless within `$20`, and on frames where the counter `& $4F` is 0
drops a young one (kind `$80A`, `$12650`). `$192C8` shows its DELAY animation and, when it ends,
goes on to DELAY's second byte. `$192EC` falls, then runs round the inside of the room: TIMER is
its heading (0-7: right, down, left, up, the second four at double speed; `$19434` its velocity,
`$19468` its facing and animation), turning at each wall (`$193CA`) or, after WORD_31FE frames,
back the way it came (`$1942C`) at normal speed; VAR counts its turns on floor and ceiling, and at
256 it begins over. Its `bpl` after `EnemyCheckHit` is always taken (`EnemyCheckHit` leaves past
the action when hit), so those three instructions are in `analysis/coverage_skip.txt`.

**The self-test no longer stops early.** It corrupted the stack pointer after the C version ran.
For an action that goes on to another state, the code carrying on then never came back, so the
check never closed: earlier batches noted these early stops, and `$192C8` stopped at its first
case. Now, when the C version goes on elsewhere, the self-test corrupts a byte of RAM above the
stack (`$FF1000`) through the bus instead, and such actions report 100 in 100 like the others.

The fuzz puts kind `$80A` in the room's list, gives `$19200` its frames to drop on and a full
entity table, and gives `$192EC` its phase, heading, the room's edges and its 256th turn.

Results: 0 mismatches over the standard runs and every owner state (422,373 checked calls);
75,000 fuzz cases from five states, 0 mismatches, every instruction covered. Self-test caught
100 in 100 for each. Replace mode plays the new-game script and four states. 125 of the 135
actions are C.

#### The monsters' actions, batch 26: a creature that walks between five nodes ($19EAE-$1A025, 2026-10-12)

Five actions of one creature that moves a pixel a frame between five places (`$19DA6`). NODE
(-$37FD) is the node it is at, NEXT_NODE (-$36FD) the one it heads for, and TAKEN (`$FFA112`, a
byte a node) marks a node someone already heads for; VAR is its heading, SHOT the heading it turns
to, DELAY's first byte its direction. `$19EAE` appears (its kind by `$1274A`) and steps on for 32
frames; `$19F6A`, at a node, turns to heading `$40` and every 8 frames picks one of the node's
exits at random (`$19D82`: a count, then node and direction pairs; a `divu` by the count) that is
not taken; `$19FE6` walks to it and frees the old node; `$19F32` is the creature hurt; `$19F00`
the creature beaten, its nodes freed and a pause set for its spawn point before it becomes a
puff. Its touch (`$19DD2`) tests the player a little ahead of it, by its heading.

The fuzz gives it one of the five nodes (or none), a next node, a direction, the five marks, its
place at or beside the next node, and its heading turned or not.

Results: 0 mismatches over the standard runs and every owner state (422,373 checked calls);
125,000 fuzz cases from five states, 0 mismatches, every instruction covered. Self-test caught
100 in 100 for each. Replace mode plays the new-game script and four states. 130 of the 135
actions are C.

#### The monsters' actions, batch 27: the last boss's parts ($1A026-$1A437, 2026-10-12)

Five actions, the last of the table. `$1A026` is the node walker firing: it turns to the target
(`$1032`), fires a shot of kind `$224` from its heading's muzzle (`$1A0A0`) and holds it, MAKER
both ways, until the shot lets go. `$1A218` readies a part of the last boss (its body and attack
boxes `$1A25C`/`$1A261`, its attack's strength). `$1A3E4` is the last boss standing: before its
second form only its attack meets the player (unless `$99FA` bit 7), and in the second form it
takes animation 2 after TIMER frames; struck (TOUCH bit 3), state 2. `$1A2E2` is it hurt: beaten
the first time, its health comes back, it moves to its second place (`$10A8`, `$1058`) with new
attack boxes (`$1A3DE`), makes its piece (`$1985A`, kind `$80C`) and sets the room's effects;
beaten the second time, state 1. `$1A266` is the last boss beaten: the room frozen, its pieces
gone, a puff every 4 frames (`$19C34`) and, after TIMER2 of them, the ending's flag (`$965F` bit 1).

The fuzz puts kind `$80C` in the room's list and gives these actions their phases, forms, whiles,
`$99DE`/`$99FA` and a held shot.

Results: 0 mismatches over the standard runs and every owner state (422,373 checked calls);
125,000 fuzz cases from five states, 0 mismatches, every instruction covered. Self-test caught
100 in 100 for each. Replace mode plays the new-game script and four states.

**All 135 of the monsters' actions are now C** (batches 7-27): every entry of the action table
at `$13DB6` has a port in `port/actions.c`, checked against the original by shadow runs, fuzzing
from five states with full instruction coverage, and the self-test.

#### The status screen, batch T1: its primitives (2026-10-13)

The rest of the text system starts with the status screen: `$7582` and about 45 routines under
it ($7294-$8C34), `port/status.c`. The screen is a set of nested menu loops that save their
return address, wait a frame at a time (`$805A`, `$7E12` yield) and jump back when done; they
come later, as coroutines. Under them are ordinary routines, ported first. Their callers carry on
with what they leave (a1 past the string drawn, d7 on the next row, the flags of a test), so they
are ported register for register and registered with every register and flag in the contract
(`CT_ALL`): any difference is a mismatch, not a watch.

Batch T1, 21 routines: the text position `$83B0`/`$83B8` (window plane at `$5800`); strings as
tiles `$786A`/`$7870`/`$7874` (`$A000` bright, `$8000` plain) and the fixed-width names `$7B70`
(`$8CDE` long); the menus' text places `$7AAA`, `$7C44`, `$7C4A`, `$7C56` (word tables indexed by
a signed d0.w); the two-row meters `$7C68`/`$7C9A`; the button tests `$7D7A`/`$7D7E`/`$7D8A` (d6
and the flags); `$83A4` (more of the list below); a blank `$84AE`; numbers: `$8522` (d0 / 10 by
32 shifts), `$84EC` (up to 999,999 as digits at `$FF959F`), `$8552` (two digits) and the level
`$853C`.

What they needed: the X flag (`add.w d0,d0` in the text position, the adds in `$83A4` and
`$8552` set it); signed table indexes; `c->ret` cleared after `call_68k` (the level's call left
it set, and the harness carried on at its mark; the whole screen pass caught it). The status
screen runs inside a system task, which is translated code, so these run nested inside the
task's check (`TaskCallSystem`) and every pass of the screen is compared as a whole; per routine,
the fuzz checks them, with the menus' indexes, a meter's cells, a list's length and strings in RAM
as the game gives them.

**Recorded sessions.** No earlier run used these screens (the status screen opened twice in 25
runs, no shop, no talk), so `play --record NAME` records a session: the starting state as
`work/NAME.state`, every change of the buttons in `work/runs/inputs/NAME.txt` (the harness's
`--input` format, ending `# end FRAME`; F4 or F5 begins NAME-2, NAME-3). `tools/accept.sh`
replays every state with its input to its end, in shadow mode and (for `status`, `shop` and
`talk`) in replace mode. The owner's three sessions replay exactly: the harness's coverage
equals `play`'s. `WBMW_PC_COUNT=FILE` writes how often each instruction ran (any mode), which is
how the screens' code was found: the routines a session runs that ordinary play does not.

Results: 0 mismatches over the standard runs, every owner state and the three sessions (29 runs,
481,775 checked calls); 525,000 fuzz cases from five states, 0 mismatches, every instruction
covered. Self-test caught 100 in 100 for each. Replace mode plays the new-game script, four
states and the three sessions.

#### The status screen, batch T2: the cursors and the tile loaders (2026-10-13)

25 routines. The screen has three blinking marks, each a state byte (bit 0 on, bit 1 shown) and
a count of 20 frames to its next change, drawn by the shared `$8200` (tile d6 at column d4, row
d5): the menu cursor (`$81A8` its blink, `$81C0` hides it, `$81D6` shows it at the place in
`$9624` and remembers it in `$9626`), and the list's scroll arrows above (`$80A8`, `$80C4`, and
`$80D4` reached only by branches) and below (`$80FA`, `$8116`, `$8126`). `$8212` moves the list's
line up or down within its length, scrolling to keep it among the four shown (`$83D0` redraws,
still the original's), with the cursor on it and a click. The frame and boxes are tile rows: `$802A`
draws 32 tiles from 32 bytes (low nibble the tile, high nibble its palette and flip from `$804A`),
`$8024` at a row, `$8010` 64 tiles from long words; `$7F54`-`$8000` are the screen's frames built
from them. Also the stats page's frame, level and labels (`$7E7C`, which draws the frame's foot
twice, as the original does), the palette steps (`$7F00`, `$7EEE`), the sprite records at `$FF3300`
(`$8E22`) and the two clicks (`$8BAA`, `$8BB0`).

One slip: `$8212` loads the line with `move.w`, whose flags its early returns keep. The fuzz gives
the marks their states and counts, the list a length, top and line, and the list's page and
entries real values: `$1E6A`, which builds an entry's name, hangs on purpose (`bcs` to itself) on
a bad name.

Results: 0 mismatches over the standard runs, every owner state and the three sessions (29 runs,
481,775 checked calls); 625,000 fuzz cases from five states, 0 mismatches, every instruction
covered. Self-test caught 100 in 100 for each. Replace mode plays the new-game script, four
states and the three sessions.

#### The status screen, batch T3: the icons, the list and the pages (2026-10-13)

25 routines: everything the screen draws. The icons: a 2 x 2 icon `$85F0`/`$85FC`, two rows of
two tiles `$8608`, the marks `$85CE`/`$85CC`, and the A + left/right slots' icons `$8620`/`$863C`
(the thing's graphics copied in by CopyRawTiles when the slot changed since last drawn; shields
and armour from `$8690`, swords from `$869C`, none from `$8C80`) with the items page's `$8666`.
The lists (pages 1-4: swords, armour, shields, boots): `$82F4` builds one from the bits of the
menu's byte (the things owned; the one in use marked) and sets the scroll arrows (`$8326`);
`$83D0` draws four lines from the list's top, each name (`$1E6A`, still the original's) centred
in 14 columns, with blank lines past the end; `$76B4` is a list page. The pages: the title in its
box `$8272`, the main menu `$77CA` (six lines, the cursor on the line chosen), the panel cleared
`$7808`, moving round the menu `$7D00`, opening a line's page `$7820`, the special actions' page
`$7BB2` (each name with its meter of uses), the items page `$7AC0` (names, pictures, counts and
the icon of the thing in use), `$7B86`, taking a thing out of its slot `$7CA4`, the stats bars
`$76E8` (AP, DP, SP from the points of the things in use, `$77A2`, as bars of five) and the view
limits for after the screen `$86BC`. And the fills `$8762`/`$875C`, `$7E72`, `$757A`.

`$83D0` keeps its locals in a `link` frame as the original does (the line count, the blank
lines, the entry, the tile base, the two pads), so the stack below the final pointer and a6
come out the same. Its pads: `lsr.w #1` then `addx.w`, the odd column goes right.

The fuzz gives these the screen's state as the game keeps it: the list's length and top, the
line, the menu's own line, the page, real entries (`$1E6A` hangs on purpose on a bad name), the
marks' states, the slots' things and what was last drawn, the things in use, the things owned
and each list's owned bits.

Results: 0 mismatches over the standard runs, every owner state and the three sessions (29 runs,
481,775 checked calls); 625,000 fuzz cases from five states, 0 mismatches, every instruction
covered. Self-test caught 100 in 100 for each. Replace mode plays the new-game script, four
states and the three sessions. Of the status screen only the menu loops are left (T4).

#### The status screen, batch T4: the menus, and the pause screen (2026-10-13)

The rest of the screen: the loops that wait for frames. `$7582` (from the game's frame at
`$90E0`, Start pressed) fades in, runs the main menu, opens a line's page (the three kinds at
`$763E`, `$766E`, `$7684`, through the tables `$75F8`, whether it may be opened, and `$7604`),
and fades out; `$7F0E` is the pause screen, which Start opens instead (`$90EA`) while `$9659` bit
2 is set: during the player's update (`$DD0C` sets it, `$E3BA` clears it) unless the player is in
the air or a spell is running. That is, Start in the middle of a jump pauses.

The original's menu routines keep their return addresses in RAM (`$9630` the screen's, `$9634`
the frame wait's, `$9638`-`$9654` the pages' and the choosing's), not on the stack, so that
`Yield`, which returns to the scheduler, can be called from any depth of them. The C versions are
ordinary functions: each writes the address the original would return to (after its call: the
values are constants) and waits with `task_wait_frame` at the original's resume addresses
(`$8062` in the frame wait `$805A`, `$7E2A` in the worn line's blink `$7E12`, `$76A8`, `$7F36`),
with the stack at the pass's own level as `Yield` leaves it. The screen and the pause leave by
`port_continue_at` to the saved address. Under them, also ported: the choosing of a slot `$799E`
and putting a thing in one (`$79EE`, `$7A44`), the items and special actions' choosing (`$7882`,
`$78DC`), a list page's buttons `$7D9C` (wearing the thing of the line), the fades `$7E8C`/`$7EBA`,
the sprite cursor `$794E`/`$7952` with `$7924`/`$7938`, the setup `$8BB6` and reset `$8C34`.

`port/task.c` now resumes a waiting chain with the scheduler's registers (and its own stack
pointer), as the original does: after `Yield` a task carries on with the registers the scheduler
had at its `jsr (a0)`, not its own.

How it is checked: `$7582` and `$7F0E` cannot be fuzzed (they wait for frames, which needs a task
pass), so they are registered (for coverage) with a plain contract and checked by the task passes
that run them: the owner's status session (every instruction of `$7582` but one branch back) and
`pause`, a scripted run from `castle.state` (`work/runs/inputs/pause.txt`: a jump, Start in the
air, then B, five times; every instruction of `$7F0E`). The routines under them that do not wait
are fuzzed as before.

Results: 0 mismatches over the standard runs, every owner state, the three sessions and `pause`
(30 runs, 485,890 checked calls); 175,000 fuzz cases from five states, 0 mismatches, every
instruction covered. Self-test caught 100 in 100 for each fuzzed routine. Replace mode plays the
new-game script, four states, the three sessions and `pause`. The whole status screen is C.

#### The messages, batch T5: the window and its helpers (2026-10-13)

Conversations, shops, signs and the "Found ..." boxes all go through the message interpreter
(`$13BC`), with its command handlers between `$14F4` and `$183A`. It waits for frames and comes
next (T6); first the routines under it, in a new `port/message.c`, ported register for register
with every register and flag in the contract, as the status screen's are. 34 routines:

* The window: a message's header (`$276C`, from the tables at `$1CC14`/`$1CC18`) gives the window's
  column, row, text width and lines; `$27B0` opens it and `$2110` (entered by `jmp`) draws the
  frame where plane A is scrolled to and hides the sprites it covers (ATTR bit 7 off, ALIVE bit 2
  on); `$225C` redraws the map under it (`$220A`, a block's quarter through MapBlockAt) and shows
  them again. Characters go in with `$2050` (column d6, row d7 in the window); `$21E8` is a frame
  row; `$2082` scrolls the text up a line by reading the window back from VRAM; `$16C6` is a new
  line (scrolling at the last). `$2002` places a window without its frame; `$279C` opens one
  with its sound.
* The blinking marks, every 20 frames: the "more" arrow (`$172E`, `$173C`, `$1754`) and the choice
  cursor (`$1774`, `$1782`, `$1790`).
* Numbers (`$183E`: decimal digits written backwards to a 0, by 32 shifts per digit), names
  (`$1E6A`/`$1ED0`: a thing's name from table 1 of the names at `$1CC10`, two rows, marks above),
  the "Found the X." and "Found N GOLD." boxes (`$1EEC`, `$1F46`, `$1F8E`, `$1FAC`/`$1FBC`, shown
  by `$1FCA`/`$1FD0` for a second), prices (`$1D92`) and the cheaper towns' 5/6 (`$203A`).
* The things owned: `$23E4` tests, `$245A` gives, `$24DC` takes thing d0 (under `$20` a bit of
  `$95CA`, `$20`-`$25` a special action's count, `$26`-`$27` the slots', `$28`-`$5F` an item's count
  at `$958C`, the potions `$2B`-`$2E` sharing one cell that holds their level, `$60` up a game
  flag; `$2510`, `$2536` find the cells). The thresholds are the bytes at `$24D8`, which are also
  a `bra.s`: the original reads its own instruction as data.

What they needed: `$1FCA` reaches `$1F46` with a `bsr`, and `$1F46`'s digits sit in its link frame
with a1 left pointing at them, so the C pushes that return address too. Three routines end in, or
call, hand ports that only promise the stack pointer (`$225C` PlayerInput, `$24DC` SlotEmptied,
`$245A` HudRedraw) and inherit their leftovers, so they have a plain contract (the other
registers watched, as for every routine). `$1E6A`'s nested names (bytes `$0C`-`$0F`) and its hang
on a bad name never happen: no name in the ROM's names table (65 of them) has either, so those
instructions are in `analysis/coverage_skip.txt`. The fuzz gives these the window's place and
size, the text's size and line, the marks, scrolling, real things (under 65) and messages,
buffers for numbers and texts, a window's record, potions, the slots' things and the counts.
`tools/quickfuzz.sh` now stops a run after 120 s instead of 600.

Results: 0 mismatches over the standard runs, every owner state, the three sessions and `pause`
(30 runs, 485,890 checked calls); 850,000 fuzz cases from five states, 0 mismatches, every
instruction covered. Self-test caught 100 in 100 for each. Replace mode plays the new-game
script, four states, the three sessions and `pause`.

#### The messages, batch T6: the interpreter's core (2026-10-13)

`$13BC` runs once a frame while a message is open (`$8CA4` bit 7): it writes characters until a
delay, a "more" (waiting for a button), a choice or the end, and returns. It does not wait inside
itself: its place in the text, and in any texts it has stepped into, is a stack of pointers at
`$FF8C7A` (`$8CA8` bytes deep), and the shop's menu keeps a step address at `$8CDA` that the next
frames jump to. So unlike the status screen it needs no coroutine: it is ordinary C, called each
frame.

In C now (`port/message.c`): the first frame's setup; the choice between two answers (left/right
move the cursor, B picks the second, A or C the one shown; the text then skips past the first
answer's part); the "more" wait; the delays (C held fast-forwards, unless `$8CA4` bit 4); the
characters (two rows each, the mark above; `$FE`/`$FF` before one sets its row of marks), with a
new line at the window's width; stepping into a name's text (`$0C`-`$0F`); and the 12 basic codes
(`$15A0`: the end of a text, the attributes, a new line, the delay, spaces, "more", a jump, fast-
forward on or off, an indented line, the indent). Also `$13AE`, a character inside the frame.

Still the original's: the 51 further commands (code `$0B` and a number, `$15C8`: game flags,
items, gold, prices, sounds, choices of three, the shop and so on), the three-way choice
(`$8CA4` bit 3, `$1CBC`) and the shop's menu step (bit 2, `$8CDA`). The C hands over to them as
the original jumps to them (`port_continue_at`, with d0 and a0 as the original's jump leaves
them), and they carry on there and back into the original's loop. They are next.

The fuzz opens a real message (one of 64, from `$1CC14`), at its start or part-way, with the
message's state bits (a choice, "more", fast-forward, rarely the shop or the three-way choice),
delays, attributes and buttons, and sometimes a short text with marked characters. The owner's
shop and talk sessions run it throughout.

Results: 0 mismatches over the standard runs, every owner state, the three sessions and `pause`
(30 runs, 486,304 checked calls); 50,000 fuzz cases from five states, 0 mismatches, every
instruction covered. Self-test caught 100 in 100 for each. Replace mode plays the new-game
script, four states, the three sessions and `pause`.

#### The messages, batch T7: the interpreter's commands, the quiz and the inn (2026-10-13)

The rest of `$13BC` is C: its 51 further commands (code `$0B` and a number, `$15C8`), the quiz's
answer step (`$1CBC`) and the inn's heal (`$1BE6`), with `$1B50` (a 16-bit division by shifts). The
commands, by what they do: step into a text (a thing's name, a quiz answer, a table and entry), a
choice of two, prices and the amount (`$8CB2`: a thing's price, a table at `$1906`, a long), the
amount as digits, paying and gaining gold (the HUD's gold redrawn), giving and taking things and
testing them, game flags (test, set, by bit), a pause, a sound, the cursor's place, the text
cleared or its lines, tests that jump in the text (gold short, a game flag, a thing owned, the
most hearts, every special action at 12, health full, `$9F32`), a heart more, the speaker gone,
the quiz (its six questions from ten, each question's three answers in random order, the answer
awaited for 240 frames, a jump by the right answers) and the inn. A test's jump is the word after
it (`$168C`). The quiz: A, B or C answers ("Correct." with sound 52, "Incorrect." 53, or "Time's
up." when the 240 frames run out), the answer's text stepped into and the text cleared for it.
The inn: `$10` health a frame up to the most, then the message goes on.

Still the original's: command 26, the ocarina's tune. Its `$2AAE` waits for frames itself, keeping
its return addresses in a RAM stack at `$9C14`, so the command moves the interpreter's own return
address out of the way first (`$8CD6`); the C hands over to it as before. It wants a coroutine,
as the status screen's menus have.

How it is checked: the owner's shop and talk sessions run it throughout. The fuzz opens one of 64
real messages from its start (from part-way, a byte could be read in the middle of a command), or
builds a short text of one command with its argument and a jump word that lands inside it, then
"more"s; it gives the commands real things (under 65, or `$60`-`$65`), game flags, gold and
amounts, the hearts and their most, the special actions' counts (all at 12 for command 44), the
quiz's state and answers, a speaker, and command 29's scene (`$968A` 0-6, `$F906`'s jump table).
Commands 11 and 21 (loading and saving through the cartridge's serial EEPROM, `$1C494`, `$1C330`)
are not fuzzed: the check cannot run the EEPROM's protocol twice from one state. They and command
26 are the 22 instructions not yet covered; a recorded session that saves the game and plays an
ocarina tune would cover them. Command 50's lines are 1 to 4 (0 would scroll 65534 rows).

Results: 0 mismatches over the standard runs, every owner state, the three sessions and `pause`
(30 runs, 485,890 checked calls); 25,000 fuzz cases from five states, 0 mismatches; 649 of 671
instructions covered (the 22 above). Self-test caught 100 in 100. Replace mode plays the new-game
script, four states, the three sessions and `pause`.

#### The messages, batch T8: the ocarina's tune (2026-10-13)

`$2AAE` checks a tune: the player's notes (A, B, C, read by `$2B08` with their sounds `$18`-`$1A`),
each within 60 frames, against tune d0 (`$2B38`, ending `$FF`), answering 0 (right), -1 (a wrong
note) or 1 (no note in time). It is what the ocarina plays on a note block (`$C48C`) and the
messages' command 26. It waits for frames: the original moves its return address to a RAM stack
(`$9C14`), calls `Yield` each frame and at the end pops that stack and jumps back; command 26 first
moves the interpreter's own return address off the 68000 stack (`$8CD6`) so the wait happens at the
task's level. The C writes the same RAM, waits with `task_wait_frame` at the original's resume
address (`$2AD2`), and leaves by the popped address (`port_continue_at`). Command 26 in C calls it
directly when it runs on a task pass; when the interpreter is fuzzed (not on a task) it still hands
the command to the original.

How it is checked: `ocarina-tunes`, a scripted run from `ocarina.state` (the ocarina used with
left + A on the note block, 200 frames for its first note; its tune 0, B A B A B C B, played right,
then a wrong note, no note, another tune, and right again), every instruction of `$2AAE` covered,
all three answers. `$2B08` is fuzzed. Command 26's own lines in `$13BC` need a conversation that
asks for a tune; no run has one yet. Replace mode now includes `ocarina-tunes`.

Results: 0 mismatches over the standard runs, every owner state, the three sessions, `pause` and
`ocarina-tunes` (31 runs, 505,205 checked calls); 50,000 fuzz cases, 0 mismatches. Self-test caught
100 in 100. Of the message interpreter, 22 instructions are not yet covered: saving and loading
(commands 11, 21) and command 26's.

#### The HUD's last routines, batch T9: the boss's bar, the special actions' uses (2026-10-13)

The routines left beside the status screen, in `port/status.c`, every register and flag in the
contract. 12 entries:

* The boss's life bar: three rows of 16 cells on the window plane, column 4, row 2. A hit leaves
  two words in `$9628` (the boss's full life, its life now). Each frame of the game `$885A` takes
  them and redraws the bar if they changed. The scale depends on the full life: under `$200`, a
  cell is `$20` of life; `$200`-`$3FF` is shown as `$200`; from `$400`, a cell is `$100`. The life
  now is rounded up to half a cell and kept under the full life (`$88B4`). The bar is drawn by
  `$893C` or `$8950` (reached with `jmp (a1)` from a table at `$88C2`), then the rows (`$88D6`:
  whole cells, a half, the rest of the full life, blanks to 16). It is cleared `$C0` frames after
  the last hit (`$89B6`, also when the status screen opens and closes).
* The special actions' uses, given through the messages (`$245A`):
  * `$8A04` raises one's most by 3, 2 or 1 (more for the cheaper ones), up to 12, or by 1 for the
    last, and adds the gain to its uses (the gain is kept at `$95B2`).
  * `$8A3A` (a slot's thing) gives a use back to the action in a slot, the left first, else to
    the first action short of its most (`$8A88`).
* The game's start (`$8E56`: the player's entity, health full) and the things in use kept and
  compared (`$8E72`, `$8E9C`, by address pairs at `$8E84`).

What they needed: nothing new. The fuzz gives the bar real hits (full lives up to `$1000`, now
and then the words already shown, so the unchanged path runs too), the drawing a life the bar
can show, the special actions their uses and most, and the slots their things. Two names
first began with `Act`, which the fuzz keeps for the monsters' actions (it sets up an entity
and overwrote d0), so they are `SpecialGiven` and `SpecialUseBack`. `$8148`, a cursor blink with
no callers, stays as it is.

Results: 0 mismatches over the standard runs, every owner state and the sessions (31 runs, 505,205
checked calls). The bar runs inside the game's task pass, so it is checked as part of that pass.
300,000 fuzz cases from five states, 0 mismatches, every instruction covered. Play alone covers
the bar's drawing and clearing, and the game's start. Self-test caught 100 in 100 for each. Replace
mode: ten runs, no problems.

#### The title, batch T10: its tasks and menu (2026-10-13)

The title screen is drawn by four tasks, each started by name from the scheduler's tables. They
are in a new `port/title.c`:

* Kttl (`$7042`): the opening. The two halves of the logo come in from either side, `$20` a frame,
  until they meet at x `$80`.
* Ktt2 (`$715C`): the title fading in, as when Start is pressed on the Sega logo or in the demo.
* K_mn (`$713A`): the title shown at once, after the opening.
* Kmnu (`$71C6`): the menu after Start. Without a saved game, Start begins a new one. With one,
  up and down move the cursor between a new game (y `$78`) and continue (`$88`), and Start
  chooses; a new game also resets the save's buffer (`$8E22`).

Ktt2 and K_mn share the end (`$718A`): the music for `$314` frames, then a fade to the demo. Each
task hands over to the next scene through `$4CA8` (the task ended, d0 the scene).

These are the first plain tasks in C. A task's pass ends when it calls `Yield` (its resume
address kept in the slot) or just returns (the address it had kept stays). Either way the next
pass carries on there with the scheduler's registers. So each task is a function on its slot's
coroutine (`port/task.c`) that waits with `task_wait_frame` at the original's resume addresses,
and a pass that returns early is a wait at the same address. The routines under them that do not
wait are ported with every register and flag in the contract:

* `$70D6`: the screen, its graphics, two layouts, the logo's halves.
* `$711C`: an entity placed with its picture.
* `$7272`: the title's "press Start".
* `$7294`: the menu's entities. The "continue" line appears only if the saved game can be
  continued (`$1C430`, which reads the save EEPROM).
* `$724E`: up or down, on the one pad in use or either.

How it is checked: the tasks wait, so they are not fuzzed (registered with a plain contract
for coverage, like the status screen). The harness has a new `--eeprom FILE` option, which loads
the save EEPROM at power-on. With it, `tools/accept.sh` has three new runs from reset, in shadow
and replace mode:

* `harness/inputs/title-skip.txt`: Start on the Sega logo, so the title fades in.
* `harness/inputs/title-continue.txt` and `title-new.txt`: with a saved game (`SAVE`, default
  the owner's `play.sav`, run only if it is there), the menu's cursor moved down and up, then
  continue or a new game.

With the new-game and demo runs, they run every instruction of the four tasks. The routines under
them are fuzzed as before.

Results: 0 mismatches over the standard runs, every owner state, the sessions and the three title
runs (34 runs, 517,965 checked calls), every instruction of the four tasks run. 125,000 fuzz cases
from five states, 0 mismatches, every instruction covered. Self-test caught 100 in 100 for each.
Replace mode: thirteen runs, no problems.

#### The rooms, batch T11: the unpackers (2026-10-13)

A room has two layers, each described by a record: `$FF9754` for plane A, `$FF97D4` for plane B.
Among a record's fields are the room's data (`$34`), its blocks' table (`$1C`) and its map (`$22`).
The data starts with offsets to its packed blocks and its packed map. The routines that unpack
them are in a new `port/rooms.c`, with every register and flag in the contract:

* The blocks (`$9F6A`). A block is four tiles (a word each) and a byte, stored as 8 bytes in the
  table, with the bytes `$800` after. Each block has a nibble: 0 is an empty block; otherwise the
  nibble is inverted, its top bit then set to leave out the block's byte (0), and its low three
  bits say how the other three tiles follow from the first:
  * 0: all given.
  * 1: the next tiles in a sheet 16 wide.
  * 2: their numbers and flips, packed in 2 bytes.
  * 3: their low bytes given.
  * 4-7: a number and flips, a byte each.

  `$9F64` unpacks a room's blocks and `$9F46` the three sets every room shares (`$FFE500`, `$FFEE00`, `$FFF700`).
* A layer's map (`$A068`). The map (64 bytes) gets a chunk number for each place the room uses (-1
  for the others). Each chunk's 192 cells go to `$FF5000` plus `$180` a chunk (plane B's chunks
  are numbered from `$10`). A chunk is packed as runs of one block, each a block and a bitmap of
  the cells it fills (`$970C` tracks which are filled); then the cells left, as 10-bit numbers four
  to five bytes. A count byte with its top bit set first expands zero runs (`$A1C4`: a mask, a bit
  set for a 0, then the other bytes).

What they needed: the C follows `$A068` label by label (as gotos), because its runs loop jumps
into the middle of the cells loop when a run has filled every cell. It keeps its frame's locals
in C. They sit below the stack pointer the routine ends with, which the checks treat as dead,
and its pushes are written as the original's. a3 ends as the word it had, sign-extended (pushed
and popped as a word).

The fuzz gives these the layer records of five states (the rooms there) and the shared
blocks' table, and random zero runs in scratch RAM. Every room loaded in play runs them too.

Results: 0 mismatches over the standard runs, every owner state, the sessions and the title runs
(34 runs, 517,970 checked calls). 125,000 fuzz cases from five states, 0 mismatches. Every
instruction is covered, by play alone too. Self-test caught 100 in 100 for each. Replace mode:
thirteen runs, no problems.

#### The rooms, batch T12: loading a room (2026-10-13)

A room is a list of commands, found by its area (`$9668`) and room (`$9669`) through the data
directory's table at `$1CC20`, and ended by `$FF`. `LoadRoom` (`$9330`) does the following:
1. Sets up both layers' records (`$967E`, `$9622`) and calls five routines that reset the
   things, doors and effects.
2. Clears the room's state.
3. Runs the commands, by their offsets at `$9442`. Each handler is entered with `jsr (a0)`, so
   it starts with a0 at its own address.
4. Finishes: the things' graphics, then the music and palette. A dark room (command 14) gets
   palette `$76` unless the player owns thing 49, which lights it. A room with a flag's music
   (command 15) keeps its usual music once the flag is set.

The commands, all in `port/rooms.c`:

| Command | Handler | What it does |
|---|---|---|
| 0, 1 | `$94AA` | A layer: its kind, size in screens, a word, and its data at an offset in the list (`$946C` fills the record) |
| 16, 17 | `$94B4` | A layer, its data at an address |
| 2 | `$9510` | The graphics' slot list |
| 3, 10 | `$9520`, `$952A` | Palettes |
| 4 | `$9530` | An effect's script (`$23A6`) |
| 5, 8 | `$9540`, `$9578` | Pointers kept for later (`$996C`, `$A11E`) |
| 6 | `$9548` | Under water |
| 7 | `$954E` | A thing at a place (`$11BF0`) |
| 9, 13, 14, 15 | `$958C`, `$95C0`, `$95CA`, `$95D0` | Bytes and words kept: command 14 the dark room, 15 the flag's music |
| 11 | `$9592` | A block drawn (`$2974`) |
| 12 | `$95AC` | A palette effect (`$5658`) |
| 18 | `$95DA` | The door's kind; if it changed, `$F880` |
| 19 | `$95EE` | A door's record (`$B49C`) |
| 20 | `$95FE` | A thing in a free slot (`$11CE8`) |

A pointer in a command is a byte, then a long if the byte is not 0, or else a word offset from
the room's list (`$9500`). With them:
* `$96D6`: one of the ending's six scenes, from the directory's table at `$1CC1C`. It sets both
  layers' data, a palette and the graphics, and unpacks both maps.
* `$92D4`: the room's music. Values `$00`-`$7F` and `$F0`-`$FF` are used as they are. `$EF`
  depends on two game flags (`$DAC8`). The others pick one of two from `$932A` by a game flag.

All are ported with every register and flag in the contract.

What they needed:
* **Fuzz inputs.** The command handlers get random command bytes in scratch RAM. The graphics
  command gets a real slot list, and command 18 a door kind of 0-6 or the one in use.
  `RoomMusic` gets any room of the 22 areas with random game flags.
* **LoadRoom's fuzz.** `LoadRoom` keeps the state's own room. In another room the things it sets
  up depend on how the player got there, and the original goes wrong too. From area 3's first
  room it also loads the next room, which has a flag's music. Thing 49's count (`$958D`) and the
  flags vary, so both endings of the dark rooms and of the flag's music run.
* **`ocarina` joins the fuzz states**, for that room.
* **Command 7 is not fuzzed.** A random thing's kind sends the original astray. Play runs every
  instruction of it.

Results: 0 mismatches over the standard runs, every owner state, the sessions and the title runs
(37 runs, 558,595 checked calls). 750,000 fuzz cases from six states, 0 mismatches. Every
instruction is covered (command 7 by play). Self-test caught 100 in 100 for each. Replace mode:
thirteen runs, no problems.

#### The rooms, batch T13: setting the layers up (2026-10-13)

Each layer's record has four kinds: how it moves across (`$30`), how it moves down (`$31`), how
its columns are drawn (`$32`) and how its rows are drawn (`$33`). After unpacking a layer's map,
`$981A` sets the layer up by these, through the tables at `$9868`, `$9872`, `$987A` and `$987E`;
`$9808` does both layers. In `port/rooms.c`, with every register and flag in the contract:

* Across: at a parallax (`$9884`, its limit from the room's width), fixed (`$98A6`), or moving
  by itself from 0 (`$98BA`).
* Down: at a parallax (`$98C8`), fixed (`$98F4`, `$9906`), moving by itself (`$990C`), or carried
  by a thing, a lift (`$991A`). The lift takes a free slot (`$11CE8`) and a script from `$9988`,
  and is placed by `$9B6C`.
* Columns and rows: as screens (`$99AC`, `$99F6`), or one screen repeated (`$99C8`, `$9A1A`: the
  map's empty places filled from the first). `$9A4A` is rows that wrap vertically.
* The camera's parallax (`$9AC4`): the camera's position above `$1000`, shifted by the layer's
  parallax and kept between `$1000` and the layer's limit. `$9A8E` and `$9AAA` use it for x and y.
* The scroll (`$9D16`, `$9E2E`): from the layer's position. Plane A's is also kept for the HUD
  (`$8B34`, `$8B36`) and its moves since the last frame (`$9740`, `$9742`). The y's move is taken
  from `$9744`, the x's, as the original does.

The fuzz gives these one of the layer records with valid kinds, small parallaxes (and larger
ones for the across limit's other branch), a random camera, random positions and a slot for the
lift. The lift's script table has a single entry, so its `$3B` is 0 or 1: with others the
original reads a jump from outside its table.

Results: 0 mismatches over the standard runs, every owner state, the sessions and the title runs
(37 runs, 558,595 checked calls). The layers are set up inside the game's task pass, so they are
checked as part of it. 630,000 fuzz cases from six states, 0 mismatches, every instruction
covered. Self-test caught 100 in 100 for each. Replace mode: thirteen runs, no problems.

#### The rooms, batch T14: the layers each frame (2026-10-13)

Each frame of the game, `$9CD2` moves the camera, moves both layers and sends the scroll to the
VDP's tables. In `port/rooms.c`, with every register and flag in the contract:

* The camera (`$9720`): from the player, with a lead of up to 64 pixels toward a second entity
  (`$9748`), eased a pixel a frame (`$974A`). Bits of `$9858` take x or y from `$985A`/`$985E`
  instead, or keep y within the room's height. The shakes (`$9734`, `$9738`, the y's eased back)
  are added.
* The layers' moves by their kinds (`$9C5E`, tables `$9C80`/`$9C8A`): following the camera at a
  parallax (`$9AA4`, `$9ABE`), drifting by their own speed in 16ths of a pixel (`$9AF2`, `$9B46`;
  `$9B18` also moves with plane A), or carried by a lift (`$9BA6`). The lift runs a script (`$68`:
  counts of frames and speeds, 0 to jump back) and sets `$9854` bit 0 when the player stands on
  its thing (`$9B96` gives the thing its speed).
* The blocks that scroll into view (`$9CE6`, tables `$9D0C`/`$9D10`): new columns (`$9D46`, each
  `$9DC8`, wrapping every `$300` of rows) and new rows (`$9E60`, each `$9EF0`, wrapping every 16
  columns), up to 32 columns and `$400` of rows kept drawn (`$8`-`$E`). `$9DC8` and `$9EF0` keep
  their counts in their caller's `link` frame, and the C does too.
* The scroll out (`$9C92`): both layers' to `$FF2400`/`$FF2600` and `$8A8C`/`$8AB4`, with the
  screen's shake.
* A room's start (`$97F0`): both layers drawn whole (`$9A58`), the scroll out, and the HUD's
  panel (`$7544`, not yet C).

What they needed:
* **A plain contract for `$97F0`.** It ends in `$7544`, which calls hand ports that promise only
  the stack pointer.
* **Fuzz inputs:**
  * The column and row routines get a layer scrolled up to 20 columns or 14 rows from what is
    drawn, so the "more than kept" paths run.
  * The lift gets its real script at any of its steps.
  * The camera gets random sources, leads, limits and shakes.
  * `$9CD2` and `$9CE6` keep the state's own layers and camera. With random kinds the layers
    redraw thousands of columns, valid but too slow to fuzz.

Results: 0 mismatches over the standard runs, every owner state, the sessions and the title runs
(37 runs, 558,595 checked calls; these run inside the game's task pass and are checked with it).
540,000 fuzz cases from six states, 0 mismatches, every instruction covered. Self-test caught 100
in 100 for each. Replace mode: thirteen runs, no problems.

#### The rooms, batch T15: the map's cells, the game's flags, rectangles (2026-10-13)

A layer's cells live in its chunks at `$FF5000`, `$180` bytes a chunk: 192 cells, 16 columns by
12 rows. A cell is named by a column in d6 (from `$100`) and a row in d7 (from `$4000`, `$40` a
row). It is found through the layer's map of places (`$22`), each place 16 columns by 12 rows
(`$300`). In `port/rooms.c`, with every register and flag in the contract:

* Cells (`$260A`: its address; `$2666`: set). `$2692` sets and draws it. `$2670` and `$266C`
  (plane A) set it and draw it if inside what is drawn (`$2672`: columns `$8`-`$A`, rows
  `$C`-`$E`).
* A block drawn (`$2694`): its four tiles, from the layer's tables at `$10`, on the layer's plane
  (`$20`) at the column and row mod 32, with the tiles' priority set for bit 15. `$26F0` reads a
  block's byte (its table plus `$800`).
* The game's flags: `$2738` finds flag d0's byte (from `$99BE`) and bit, `$274C` tests it and
  `$2752` sets it. `$270C`, `$2728` and `$2730` do the same with flags under `$20` from `$99BA`.
  `$2758` is the room flags at `$9992`, whose bytes are swapped in pairs.
* A rectangle of plane A's cells (`$974C`: column, row, width, height): `$A1FE` saves it to
  `$FF3100`, `$A23C` puts it in front (bit 15), `$A276` restores it and `$A2B4` redraws it.
  `$A2EE` draws blocks straight to VRAM through `MapBlockAtRaw`, for the ending's pictures.
* The start points (`$A37E`: start point `$9676`'s area, room, kind and the player's place) and
  their conversions (`$A3A4`, `$A3B4`: bytes as positions, 8 pixels each from `$1000`). `$A3CA`
  doubles an entity's word under water.
* The palette's fade step (`$92A4`): the room's 15 colours through `$594A`, by `$9666` / 4.

The fuzz gives these a layer's record, cells all over its map (including places it does not
use: their chunk number `$FF` points outside the chunks, as in the original), blocks of either
priority, rectangles, raw places and VRAM addresses.

Results: 0 mismatches over the standard runs, every owner state, the sessions and the title runs
(37 runs, 558,595 checked calls). 750,000 fuzz cases from six states, 0 mismatches, every
instruction covered. Self-test caught 100 in 100 for each. Replace mode: thirteen runs, no
problems.

#### The rooms, batch T16: the room's triggers (2026-10-13)

A room's triggers are a list (`$996C`, from its command 5) that `$A3D8` reads each frame, with
the player in a4. The list has three kinds of entry:
* **Zones** of `$10` x `$10` cells. Their size is set by `$C0`-`$CF` and `$D0`-`$DF`, their origin
  by some events.
* **Triggers**, commands under `$C0`, each with what touching its zone does.
* **Events**, commands `$E0` and up (`$A490`, not yet C), with `$FF` the end. The end's handler
  (`$ABB8`) pops its own return address to leave `$A3D8`, so the C handles it itself.

A trigger that fires usually stores a routine at `$998A` (or `$998E`) and sets `$9980` (or
`$9981`); the game runs it when the player presses up there. Both flags are dropped each frame
unless re-armed (`$9966`). In `port/rooms.c`, with every register and flag in the contract:

| Command | Handler | What it does |
|---|---|---|
| 0-3 | `$A4EE`-`$A52E` | Exits to another room (`$A962`), keeping the player's place across or down, or adding a word |
| 4 | `$A552` | A warp to a start point |
| 5 | `$A562` | A door: up goes to a start point (`$A5AA`) |
| 6 | `$A5BC` | A house's door (`$A60A`) |
| 7 | `$A612` | A chest, if its flag is clear: up opens it (`$A68E`: the flag set, `$A666`, its two cells changed) |
| 9 | `$A79A` | A start point's entrance, by its kind: by place (`$A898`, `$A8A6`), a shop's only when outside one, a side entrance when facing it |
| 10, 11 | `$A8B4`, `$A8E6` | Events by place (`$A990`), up runs them (`$A8D8`, `$A902`) |
| 14 | `$A90A` | A sound |

With them: the zone's place (`$A914`), whether the player is in it (`$A92C`, carry set) and
within 4 across (`$A88C`).

What they needed:
* **A jump straight into the loop.** The handlers are entered with `jmp (a0)` and go on with a
  `bra` into `$A3D8`'s loop. Each is registered on its own (for coverage) and, when entered that
  way, goes on in the original at `$A46E`, as its `bra` does.
* **Synthetic lists for the fuzz.** `$A3D8` gets one-trigger lists in scratch RAM: perhaps a size
  or origin first, then a trigger of any kind, `$FF` where the list goes on after it, and the
  player in or near its zone.
  * Entrances get a real start point, half the time one of the rarer kinds, with the player at
    its place, facing either way.
  * Events get one of the table's events, with the player at its place.
  * The callbacks get their stored places, and chests real cells.
* **Lines skipped in coverage.** The start points' kinds 2, 6 and 12 have entries in the
  entrance's table, but none of the ROM's 138 start points has them. Those lines, and the
  facing-right path that only kind 12 reaches, are in `analysis/coverage_skip.txt`.

Results: 0 mismatches over the standard runs, every owner state, the sessions and the title runs
(37 runs, 558,595 checked calls). 390,000 fuzz cases from six states, 0 mismatches. Every
instruction is covered, the handlers included, except the skipped lines. Self-test caught 100 in
100 for each. Replace mode: thirteen runs, no problems.

#### What translating tasks needed (`tools/m68k2c.py`, `analysis/translate.json`)

* **Resume points**: `"yield": "000400"`. A routine yields if it is `Yield`, or reaches it by a
  jump or branch (`YieldIfLate` branches to it; three routines tail-jump to it).
* **Exits**: addresses where generated code stops and hands control back to the machine, with
  `c->ret` = the address:
  * `$3B8`: wait for the next frame. Only interrupts end that wait, and generated code takes no
    emulated time. Called by `$62A` (display off) during game-state changes.
  * `$4BF8`: reset the stack and re-enter the frame loop. This is how a new system task starts.
  * `$4A06`: restart.
  * `$4CB4`: hang, when no task slot is free.
* **Stored code pointers become entries**: a task's exit handler (`move.l #$569C,8(a0)`) is
  called later by `EndTask` through the slot.
* **`defer`**: entries deliberately left untranslated. This step defers `Kgam` ($8EB4), the
  gameplay task: about 900 routines and most of the game. Without the deferral, the demo task
  pulls it in by starting it.
* **Leaving to the 68000**: a call or jump to code with no translation goes through
  `rt_dispatch`, then `rt_leave`, which the host provides. The harness continues in the original
  68000 code at that address. That is always correct, because the ROM's code is always there.
  The report lists where it happened. A native build will stop there instead.
* **A translator bug** that only tasks exposed. Code is emitted in address order, and a routine
  whose body includes code below its entry (`Kseg` tail-jumps to `$4CA8`) started executing at
  the lowest address. Generated functions now begin with a `goto` to the entry when the entry is
  not the lowest address.

#### What checking tasks needed (`harness/hybrid.c`, `harness/bus.c`)

* **A check ends where the C version ended.** Besides returning to the caller, a check also
  closes when the original reaches the C version's final PC with the same stack pointer (`Yield`
  returns to the scheduler, not its caller), or an exit. A check that never closes is reported as
  `UNFINISHED` after 50 million instructions instead of hanging the run.
* **Timing inputs are replayed.** C runs in no emulated time, so it reads an earlier raster
  position from the VDP than the original does. `Mxcg` calls `YieldIfLate` and read line $6A
  against the original's $6B. During a check, the bus records the VDP status and HV counter
  values the port read and gives the original the same values in the same order. The reads still
  happen, for their side effects.

#### Results

Translated: all 15 tasks except `Kgam` (`Ksmt`, `Ktmt`, `Ktm2`, `Ktm3`, two `Kmnu`, `Kdym`,
`Kedm`, `Keds`, `Kgmt`, `Kcol`, `Mxcg`, `Kseg`, `Kttl`, `Ktt2`) with the movement family: 273
generated routines, 8,065 instructions, 40 resume points, no unsupported instruction.

* The ten standard runs: **zero mismatches**, about 280,000 checked calls. They include
  thousands of task passes: boot, the Sega logo, the title, menus, game mode (`Kgmt`, 3,333
  passes), `Kcol` (3,712), the demo task (2,119) and the animated tiles (`Mxcg`, 2,115).
* Replace mode (the C runs the tasks) plays the new-game script. Its frames match the original's
  exactly at frames 2,000, 3,000 and 4,000. At frame 1,000 the intro text is a few characters
  ahead, because the time-sliced text printer gets more done per frame when the C takes no time.
* **Coverage is not complete** for the task layer. 4,423 of 8,065 generated instructions ran
  inside checks, and 78 routines never ran. Most of it is screens the standard runs never reach:
  the ending (`Kedm`: 12 of 327 instructions, and most of `$1AB28`-`$1C4xx`), parts of the menus
  and the continue screen. Fuzzing cannot stand in for these; they need save states (the
  ending, game over and continue, each menu option). Until then these routines stay unaccepted.

#### The gameplay task (`Kgam`, $8EB4): the whole game logic

With `Kgam` added as a root, every task is translated: 1,094 generated routines, 66,163
instructions counted per routine (20,765 distinct; the tracer knows 28,267), 132 resume points,
no unsupported instruction. Only the frame loop, the scheduler and the VBlank interrupt are still
68000 code. Every gameplay frame is one checked pass of `Kgam` (`g_00901C`), compared byte for
byte at its end, so the checks are coarse (about six per frame). To localise a mismatch, defer
parts again (`defer`) so they run as 68000 and the checks fall back to smaller routines.

Getting there needed:

* **Targets of computed jumps found at run time.** Some indirect jumps go through data the
  tracer cannot follow, such as behaviour pointers and dispatch records. The "left generated
  code" report names each one, and `analysis/translate.json` `"seen"` lists them as roots (31,
  over three rounds, until no standard run leaves generated code). Targets that only other
  parts of the game reach will show up the same way, and until then the harness runs them as
  68000.
* **A tracer gap.** `$88F2`-`$89B4` was taken for data, but it is code reached through two
  dispatch records at `$88C2`. `analysis/hints.json` now names `$893C` and `$8950` as entries,
  and the rebuild is still byte-identical.
* **Checks end where the C version ended.** `$C7E6` saves its return address on a continuation
  stack (`$FF9C14`) and jumps back to it later. The original reached its caller (`$908A`) that
  way while the C version, with `$908A` translated, ran on to the end of the pass, so the check
  compared them at different moments. A check now closes only at the C version's final PC and
  stack pointer, or at an exit; if the original never gets there, it is reported `UNFINISHED`.
* **A harness bug.** The per-routine counters were a fixed table of 1,024, and there are now
  1,139 routines. It overflowed and corrupted the report; the table is now sized at start-up.
* **Build time.** The generated C is about 7 MB and is now compiled once into an object file
  for both programs (3½ minutes).

Results:

* The ten standard runs: zero mismatches and no generated code left. The self-test reports a
  corrupted `Kgam` pass on every frame.
* Replace mode (the C runs all of the game logic): from the platform state, all 12 frames
  sampled over 1,200 frames are pixel-identical to the original. In the new-game run the frames
  match from frame 2,000 on. Before that, the title arrives a few frames early: the original
  drops frames while it decompresses, and the C takes no emulated time.
* Coverage: 7,489 of the 20,765 distinct translated instructions (36%) ran inside checks. The
  rest is game the standard runs never reach (other areas, bosses, shops, most enemies), and it
  stays unaccepted until something runs it. The way to get there is playing with checks on:
  `play.exe --port shadow --coverage FILE` records which code each session checked, and
  `tools/coverage.py` merges the sessions.

#### The native build (`native/`, 2026-10-10)

`work/native/wbmw.exe` runs the game with no 68000 emulator. Every instruction is translated
from the reset vector (`analysis/native.json`) or hand-ported; `native/wbmw.c` is the host. How
it is put together (`native/README.md`):

* **The machine.** The memory map, EEPROM, controller and VDP are the harness's own files. The
  ROM is read only as data.
* **Time.** At the poll points (the two VBlank-flag waits, `$3DC` and `$C262`), the generated
  code calls `rt_poll`. If the interrupt has not come yet this frame, the host renders the 224
  lines, shows the frame, keeps 60 Hz, reads input, then runs the translated interrupt handler
  `$4B5C` on an interrupt frame it pushes (the translator now supports `rte`).
* **Restart points.** `$4BF8` (reset the stack, re-enter the frame loop) and `$4A06` (restart)
  end the generated call. The return-address convention unwinds every C frame back to the host,
  which calls the point again from the top, so the C stack never grows.
* **Exits.** `$4CB4`, the "no free task slot" hang, is reported by the host. `rt_leave` (no
  translation) is fatal here: everything must be translated.
* **Translator additions.** `rte`, `move #imm,sr` (the interrupt mask is the host's business)
  and `move aN,usp` (ignored: no user mode).
* **One more tracer gap.** A task, `K_mn` at `$7136`, is started from a descriptor list at
  `$4D5C` that the tracer took for data. In the harness its code simply ran as 68000 code; the
  native build stopped at it. It is now a hint entry, and the rebuild is still byte-identical.

Results:

* **Runs.** The native build runs from power-on through the title, the attract demo and the
  new-game script (4,000 frames each) without stopping. Headless it does about 400 frames a
  second, as the harness does, because rendering dominates.
* **Matches the original.** The new-game run is pixel-identical to the original in every
  sampled frame from frame 1,800 to 4,000. Before that, and throughout the attract demo, it is
  ahead of the original in real frames: the original drops frames when the 68000 runs out of
  time, and the native build never does. The same pictures appear, later in the original:
  native frame 2,000 of the demo is identical to the original's frame 2,154.
* **To do.** Sound. Save states (the harness's states hold a 68000 mid-instruction, which a
  native build cannot resume). Optionally, emulating the original's dropped frames for exact
  timing. And comparison by game frame rather than real frame (the game's counter at
  `$FF8002`), so long runs can be compared exactly despite the time shift.

#### Save states in the native build (2026-10-11)

The native build reads and writes the harness's state format. A native program has no 68000
call stack to restore, only C frames, so a state is taken where nothing is pending: the frame
wait in `$3B8` ($3D6-$3FE), called from the frame loop (the stack holds only `$4BF6`). Every
harness state examined stopped there, including all of the owner's. Each instruction of that wait
is a translated entry (`analysis/native.json`), so loading a state starts the code at its PC;
when it returns to `$4BF6`, the host calls that next. Loading during play unwinds the C stack
with `longjmp`. Saving waits for the next such point. With `--timing`, a load starts at line 0,
slice 0, as the harness's next frame would.

Checks:

* Saving at frame 1,500 and resuming matches the uninterrupted run in every game frame to
  3,000, timed and untimed.
* The owner's castle state (saved by `play.exe`, version 1), run 1,800 frames in both programs
  with the same input, is identical in every game frame, every sampled picture and the sound.
* A state saved by the native build, continued in the harness, matches the native build's own
  uninterrupted run in every game frame.

#### Exact timing, optional (`--timing`, 2026-10-11)

With `--timing` the native build counts the 68000 cycles the original would spend, and runs
time exactly as the harness runs it on Musashi. Without it, nothing changes: the code takes no
time, and the game never slows down. The owner asked for it to be optional.

* **Cycle counts.** `harness/cycles_dump.c` writes Musashi's own table of cycles for every opcode
  (`work/cycles.bin`). The translator reads it (`analysis/native.json` `"cycles"`) and ends every
  instruction with `RT_CYC(cycles, next pc)`, where Musashi charges it: after the effects, before
  any jump, call or return. The dynamic costs follow Musashi's 68000 rules: Bcc not taken (.b
  -2, .w +2), DBcc (branch -2, expiry +2), Scc true to a register (+2), MOVEM (+4 a word
  register, +8 a long), shifts and rotates (+2 a bit counted), MULU/MULS (+2 per one, or per
  change, in the source).
* **Every routine translated.** Hand ports take no time, so the native build now translates
  every routine (`"hand_ports": false`); they were proven equivalent anyway. That exposed one
  more idiom, a computed call through the stack (`pea R; move.l X,-(a7); rts` in the animation
  interpreter at `$88E`), which the translator now turns into a call of X that comes back at R.
  QueueSound's retry is a poll point (`$38E`, after the bus is given back; see "The shop
  crash"), so untimed code still lets sound time pass there.
* **The host.** `native/wbmw.c` reproduces the harness's loop. A scanline is 8 slices of 61
  cycles; an instruction's cycles are charged after it; a slice ends at the first instruction
  boundary at or past zero, and the overshoot is dropped (as `m68k_execute` does). Interrupts
  are taken at a slice's start, or just after an instruction that set the status register: the
  frame is pushed, the cost is 44 cycles, and the translated handler runs from its vector, with
  the interrupt mask tracked (`rt_set_sr`). The raster the game reads is the line and the
  slice's starting cycle. Lines render and play their sound at their end, and VBlank is at line
  224. Reset uses 40 of the first slice's cycles.

Result: **identical to the original**. Through the new-game script, every one of 3,864 game
frames is identical in RAM, VRAM, CRAM and VSRAM, with the same 3,864 game frames from 4,000 real
frames (the original's slowdowns, reproduced). The attract demo, which went its own way without
timing, is identical in all 3,781 game frames and in every sampled picture, frame for frame. Its
sound is identical sample for sample. The cost is about 285 frames a second headless, against
about 400 without timing; both are well above 60.

#### Comparing by game frame (2026-10-11)

The native build never drops frames and the original does, so comparing them frame by real
frame only shows the time shift. `harness/framelog.c` (in both builds) samples the machine at the
same point of every game frame, where the game waits for VBlank (`$3DC`, after the frame counter
`$FF8002` went up):

* `--frame-log FILE` writes one line per game frame with hashes of work RAM, VRAM and
  CRAM+VSRAM. The stack area and the VBlank counter `$FF8A49` are left out, because they depend on
  where the interrupt struck.
* `--input-game FILE` gives the buttons by game frame (same format as `--input`), so both builds
  see each press on the same game frame.
* `--dump-at N DIR` writes the memory at game frame N.
* `tools/framecmp.py A B` compares two logs.

Results, from power-on through the new-game script (3,864 game frames in both):

* **The picture.** VRAM is identical from game frame 884 on, and CRAM and VSRAM from 619. The
  earlier differences are while the title loads.
* **Work RAM** differs in every game frame from 233 on, but in very few bytes. At game frame
  3,000, three bytes differ: `$FF8A4A` (the user scheduler's round-robin position), `$FF8109` (a
  counter in a system task's slot) and `$FFC709` (one object's blink counter). The player, every
  object, the map and the camera are identical.
* **The cause** is the time-sliced scheduler. With bit 6 of `$FF8A5D` set (nearly all the time,
  gameplay included), `$4C00` runs user tasks only until the raster reaches line `$D0`, carrying
  on round-robin next frame, and `YieldIfLate` does the same within tasks. The original's raster
  moves with the 68000's cycles; the native build takes no time, so its raster is always line 0
  and it runs a different set of task passes per frame. In the attract demo the same timing
  makes the recorded demo play out differently.

So the native build's game logic matches the original; what it lacks without `--timing` is the
original's timing, which shows only through raster-dependent scheduling and the dropped frames.
With `--timing` (above) it matches exactly.

#### Sound (2026-10-11)

The game's sound is the Z80 running the game's own driver (6,775 bytes at `$98000`, uploaded at
boot), driving a YM2612 (FM) and an SN76489 (tone and noise). The 68000 only queues one-byte
commands in Z80 RAM. `harness/sound.c` emulates all of it, and both the harness and the native
build use it:

* **Parts.** The Z80 is floooh's `z80.h` (zlib licence, `third_party/chips`). The YM2612 is ymfm
  (BSD-3, `third_party/ymfm`, behind the C wrapper `harness/ym2612.cpp`, with the chip's timers
  counted in its clocks). The SN76489 is written here. The owner chose ymfm over Nuked-OPN2 (LGPL).
* **Clocking.** One master clock, as on the console: the Z80 runs one scanline (228 cycles) at a
  time, and a YM2612 sample falls every 67.2 Z80 cycles (the clocks are 7:15 apart). Output is
  stereo at 53,267 Hz, through a DC blocker standing in for the console's output capacitor; SDL
  converts it to the sound card's rate (`harness/audio.c`).
* **The 68000's side.** `$A11100` (bus request) and `$A11200` (reset) are real now: the Z80 stops
  while the 68000 holds its bus or reset.
* **Waiting on the Z80 in ported code.** `QueueSound` retries while the driver's busy flag (Z80
  `$1C00`) is set. The driver sets it only for the few instructions in which it reads the queue,
  but code that takes no time would spin on it forever. The hand port now calls `port_idle()`
  there, and the host lets 64 Z80 cycles pass.
* **Shadow checks.** The sound is held still while a check is open, like the interrupts, so the
  Z80's own writes to its RAM do not count as differences. The standard runs are still clean.
* **Save states.** Version 2 (`WBMWST02`) adds the sound. Version-1 states, including all the
  owner's, still load: the driver restarts.

Checks: the attract mode recorded headless (`--wav`) is silent through start-up, then has music
from the title on, in stereo, with no clipping and no DC offset. The native build plays the same
music: 99.8% of its musical register writes match the harness's over 30 seconds. The rest are
small reorderings, where a command lands one driver tick earlier because the native build never
drops frames. `WBMW_SOUND_LOG=FILE` lists every chip write, for such comparisons.

#### Computed jump targets, found statically

A native build has no original code to fall back to, so every address the game can jump to
through a register must be a translated entry. The tracer leaves 53 indirect jump sites
unresolved. Until now the harness discovered their targets at run time (`"seen"`: 32
addresses). The translator now finds them all from the code and data, and `"seen"` is empty:

* **Continuations** (43 sites). A routine that pops its caller's return address into memory
  (`move.l (a7)+,<memory>`) is "returned to" later through a jump. This covers the stack at
  `$FF9C14` (23 sites) and the variables `$FF9630`-`$9654`, `$9F1E`, `$A3BE` and `$8CD6`. The
  instruction after every call to such a routine, direct or through a jump table, becomes an
  entry: 81 continuation points, by the same rule as `Yield`'s resume points.
* **Code addresses stored as constants** (`move.l #code,X`). These are a task's exit handler and
  continuations such as `$9EE8`, `$9F22` and `$998E`. All of them are entries now, including
  ones the tracer did not record (`Mxcg`'s exit handler `$6BAE`).
* **`lea` before the jump.** `lea X,aN; jsr (aN)` goes to X (`$19E0E`). A jump that indexes from
  X goes to the entries of the hint table based at X: the two-level behaviour tables at
  `$10A5A`.
* **Hints are code.** Every `"entries"` and `"tables"` address in `analysis/hints.json` is code
  reached only through computed jumps, so all of it is translated. That includes the enemy
  behaviour table at `$13DB6` (135 entries), which brings in enemies no run has met yet.
* **One tracer gap.** `jmp $A166(pc,d4.w)` has four cases, not the three the tracer counted.
  `$A16C` is now a hint entry, and the rebuild is still byte-identical.

Result: 1,429 generated routines, 132 resume points, 81 continuation points. Without `"seen"`,
the standard runs, the owner's states (castle, before the death, after the boss, the inn) and
the run that first found `$E5FC` all pass with zero mismatches, and none leaves generated code.
Translated or hand-ported code now covers 27,526 of the 28,267 traced instructions (97.4%). The
741 left are reset and start-up (`$200`-`$316`, `$4A06`-`$4B02`), the frame loop and scheduler
(`$4BBC`-`$4C6E`), the per-frame work behind the frame wait (`$3B8` → `$5262`), exception
handlers, and a few routines only start-up calls (`$9F46`). That is the native build's part.

#### The inn crash (found by the owner, 2026-10-10)

Saving at an inn froze `play.exe`. The owner saved a state at the innkeeper's question
(`work/inn2.state`, frame 13,701), and from it, answering yes hung the C version every time;
the original was fine. The game's own save (the EEPROM write, `$1C494`) works in both.

The cause was a second wait for time. After the yes, `Kgam` plays the night music and waits 300
frames by polling the VBlank flag itself (`bclr #0,$8006; beq` at `$C262`), then restarts the
gameplay task. Only the VBlank interrupt sets that flag, and translated code takes no time, so
it spun forever. Only `$3B8` and `$C262` read that flag anywhere in the ROM.

* **Exits can be inside a routine now.** `$C262` is an exit: generated code reaching it stops
  and leaves that address to the 68000, which waits the 300 frames on real emulated time, then
  enters the translated gameplay task again.
* **A watchdog for hangs in generated code.** Every branch target in generated code counts its
  passes (`RT_AT`, `port/m68k_rt.h`). A call that passes 100 million prints `STUCK` with the 16
  most recent branch targets and stops. It named `$C262` on the first run, and turns any future
  hang of this kind into a report instead of a frozen window.

A later session of the owner's (`--coverage`, ending in a castle, kept as `work/castle.state`,
frame 29,683) raised coverage from 37% to 44% of the translated instructions. Six
random-input shadow runs from the castle state: zero mismatches, no fallback to the 68000. In
replace mode the frames match the original except a wall torch whose flame is one animation
step apart: the animated tiles come from `Mxcg`, the raster-timed task, so it is the same timing
difference as the intro text.

From the inn state with yes answered, shadow mode reports zero mismatches. Replace mode matches
the original's frames until the morning text, where the C is one letter ahead (the same timing
difference as the intro).

#### The shop crash (found by the owner, 2026-10-12)

Buying the Hard Armor stopped `wbmw.exe` with `STUCK`, the recent targets all `$376`/`$390`:
`QueueSound` retrying while the driver's busy flag (Z80 `$1C00`) was set. The native build's
poll point for that retry was `$386`, which runs *before* the instruction there gives the Z80
its bus back, so `sound_idle` found the bus held and let no Z80 cycle pass. The flag could never
clear. The hand port (`port/kernel.c`) and the harness's own check (`hybrid.c`, at `$38E`)
idle after the release and were right; only the native build's poll point was wrong.

* **The fix.** The poll point is `$38E` (`analysis/native.json`, `rt_poll`), after the release.
* **Why it was rare.** Untimed, the Z80 runs only at the end of each frame, and it holds the
  flag for about a dozen instructions while it reads the queue (Z80 `$019C`-`$01C8`). A hang
  needs a frame to end inside that window and a sound to be queued in the next frame.
* **Checks.** Random input from `platform.state` (seeded by its frame number, 6,000 frames)
  reproduces the hang with the old build; the new build waits once (frame 9,568) and plays on.
  30 random runs of 5,000 frames from ten of the owner's states: no hang, three waits survived.
  The owner's own purchase (`session5.state`) did not hang headless in 136 input timings with
  the old build either, so its exact trigger was the owner's timing.
* **A report.** The native build prints `QueueSound waited for the sound driver (frame N)` the
  first ten times it takes that path, so a session that goes through it shows it.

#### The castle door crash (found by the owner, 2026-10-12)

Going left through a door in the town after the second boss stopped `play.exe` (shadow mode)
with `call_68k: $00C1E0 did not return normally (control went to $000000)`; the owner saved a
state just before it (`work/door-crash.state`, frame 305,313). The door's scene gives the player
an input routine: `lea $C1E0(pc),a0; bsr $C0C2`, which stores a0 in `$FF9F22`, and the
player's input (mode 6, `port/player.c`) calls it. `$C1E0` is two instructions (`move.b
#4,$FF9F0B; rts`), an orphan to the tracer, and the translator only took code addresses stored
by `move.l #code`, so it had no translation; `rt_dispatch` failed and `call_68k` stopped. In
the harness, 68000 code reaching it would have run it as it is; C calling through the pointer
cannot.

* **The fix.** `tools/m68k2c.py` counts `lea code,aN` (an address that is a traced instruction)
  as a stored code pointer, like `move.l #code`. That brought in 31 routines (1,280 to 1,311 in
  the harness), `$C1E0` and the two routines at `$18DB8`/`$18DC0` that `$18D36` installs the
  same way among them.
* **Checks.** From the owner's state, walking left through the door, 900 frames: 0 mismatches,
  into the next area. The standard runs and every owner state: 0 mismatches.

### Platform layer

The native build will provide `port.h`'s memory accessors and a Mega Drive platform library
(VDP renderer, input, timing, later audio) that knows nothing about this game. The harness's
`vdp.c` is the starting point for the renderer; it already draws every frame the game has
produced so far.
