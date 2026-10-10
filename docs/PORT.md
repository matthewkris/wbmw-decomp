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
   pointers need an input generator (`SetGfxSlots` has one: random valid slot lists). The
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

15 routines, all with zero mismatches: shadow-checked calls in the two standard runs, plus
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

Unverified: the wait in `QueueSound` while the sound driver is busy ($386-$394). The harness
does not run the Z80, so the busy flag is never set. The `$C0` (skip a slot) command of
`SetGfxSlots` is used by none of the game's 79 slot lists; it is checked only by fuzzing.

## Decisions

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
