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

1. **Shadow mode, zero mismatches, over runs that call it.** A new game played into the first
   room (`work/run3/input.txt` style script) and the attract demo (no input, 3,000 frames) are
   the standard runs. A routine with no calls in them is not accepted until a run that calls it
   is added.
2. Replace mode still plays (the owner can check with `play.exe --port replace`).

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

| Routine | Address | Shadow-checked calls (new game + demo) |
|---|---|---|
| `Random` | $5D8 | 47 |
| `SetVRAMWrite` | $5A0 | 3,851 |
| `LoadPalettes` (with `UnpackPalette` inside) | $135C | 11 |
| `ApplyVelocity` | $AB4 | 87 |
| `ApplyVelX`, `ApplyVelY` | $AB8, $AD6 | 4,232 each |
| `FrictionX` | $B92 | 1,198 |
| `UnpackPalette` called on its own | $1370 | 0: not yet accepted |
| `FrictionY` | $BAC | 0: not yet accepted |

All checked calls match: 13,658 calls, 0 mismatches.

## Decisions

### Exactness first, cleanup later

Ported routines reproduce side effects the game may not depend on (a scratch register, an
unused flag, a stack residue), because the shadow check can only prove equivalence if nothing
is left out. Once the callers of a routine are also C, the side effects nobody reads can be
dropped. Doing it in that order keeps every step checkable.

### Tasks and `Yield`: proposed, not yet built

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
