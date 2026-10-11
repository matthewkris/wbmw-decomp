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
  QueueSound's retry is a poll point (`$386`), so untimed code still lets sound time pass there.
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

### Platform layer

The native build will provide `port.h`'s memory accessors and a Mega Drive platform library
(VDP renderer, input, timing, later audio) that knows nothing about this game. The harness's
`vdp.c` is the starting point for the renderer; it already draws every frame the game has
produced so far.
