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

### Tasks and `Yield`: stackful coroutines (decided 2026-10-10, not yet built)

The owner chose coroutines over state machines.

The game is cooperative tasks: `Yield` ($400) saves its return address in the task slot and
returns to the scheduler, and several subsystems keep continuation addresses in RAM. The
proposal for the C port is **stackful coroutines**: each task gets its own C stack, `Yield()` is
a call that switches back to the scheduler and returns when the task next runs, and saved
continuations become coroutine resume points. That lets task code be translated almost line by
line, keeps the control flow of the original, and is a small, game-independent library (a
coroutine switch is a few dozen lines per platform, or an existing one such as minicoro), so it
carries over to other games built the same way. The alternative, rewriting each task as a state
machine, changes the shape of every task and makes each one harder to check against the
original.

The hybrid harness cannot run a C task under a 68000 scheduler without nesting execution, so
the scheduler, `Yield` and the tasks it runs move together, with the task bodies still calling
68000 routines through a "call into 68000" helper until they are ported.

### Platform layer

The native build will provide `port.h`'s memory accessors and a Mega Drive platform library
(VDP renderer, input, timing, later audio) that knows nothing about this game. The harness's
`vdp.c` is the starting point for the renderer; it already draws every frame the game has
produced so far.
