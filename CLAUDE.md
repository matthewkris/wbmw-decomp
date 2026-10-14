# Wonder Boy in Monster World — decompilation and native port

## What this project is

The owner wants the Sega Genesis game *Wonder Boy in Monster World* (USA/Europe) running as a
native x86 program: a full decompilation and port, not an emulator wrapper and not a static
recompilation. The game is hand-written 68000 assembly (38 stack frames in 28,000 instructions),
so there is no original C to recover. "Decompiling" here means understanding each routine and
rewriting it in C that behaves identically.

The owner intends to reuse this process on other games. `docs/PROCESS.md` records the process,
its lessons, and which parts are general (any Mega Drive or 68000 game) versus specific to this
one. Read it, weigh reuse in design decisions (configuration over constants, platform layer
separate from game logic), and update it at each major milestone.

This is a long project spread over many sessions. Work started in a cloud session that did the
first survey; you are picking it up on the owner's machine. Read `docs/NOTES.md` before doing
anything else. It holds every address and structural fact found so far.

## Where things stand

Done:

- ROM identified and verified (details below).
- A recursive-descent tracer (`tools/trace.py`) that separates code from data in the code region
  `$200–$1D000`. It finds 95,160 bytes of code (28,208 instructions) in 845 routines, with zero
  tracing conflicts.
- **A byte-identical rebuild.** `tools/build.sh` (or `make`) traces the ROM, generates
  `work/src/wbmw.asm` with `tools/mkasm.py`, assembles it with vasm and checks the SHA-1. All
  28,208 instructions are emitted as mnemonics with explicit sizes and labels; none fall back to
  raw words. Jump tables are `dc.w target-base` / `dc.l label`, task tags are `dc.b 'Kxxx'`.
- **The whole ROM is laid out.** `analysis/layout.json` names every region outside the code:
  vectors (`dc.l` handler labels), header fields, the data directory at `$1CC00` (`dc.l` to 27
  labelled blocks `dir00`–`dir07`, `area00`–`area18`), the graphics table at `$41000` (`dc.l
  gfx_xxxxxx+flag` to 726 labelled chunks), the Z80 driver, the animation data and every padding
  run (`dcb.b`). Each block is an `incbin` range of the owner's ROM under its own label. Code
  references into data are symbolic (`movea.l (data_dir+$10).l`, `adda.l #gfx_table`,
  `movea.l #task_Ksmt_004CC4`), including `lea` of absolute addresses, `movea.l #imm`, pointers
  stored to known data-pointer fields and table bases built with `addi.l`.
- **Graphics decoded.** `tools/gfx.py` decodes all 726 chunks of the graphics table (raw, a
  per-tile mask/bitplane format and Huffman-coded LZ77), verifies each against the chunk
  boundaries, and writes tile sheets as PNG (`make gfx`, output in `work/`).
- **Rooms render.** `tools/rooms.py` parses all 170 room scripts, decodes their maps, block
  tables and graphics slot lists, and renders any room (or `--all`) to PNG in real colours.
- **Reference harness runs the game.** `harness/` (C, built with MSYS2 GCC) runs the original code
  on Musashi (`third_party/musashi`, MIT, 68000 only) against a reimplemented memory map, VDP,
  controller and serial EEPROM, headless, at about 7x real time. It boots through the SEGA logo,
  title and menu into a new game, and dumps frames, RAM and VDP state per frame. `play.exe` is
  the same machine in an SDL2 window with keyboard and pad input, so the owner can play builds
  with sound (the Z80 and its driver, ymfm's YM2612, an SN76489). See `harness/README.md` for
  what it does and does not model.
- **The port has started.** `port/` holds C routines that replace 68000 routines one at a time
  (45 so far: `Random`, `SetVRAMWrite`, palettes, velocity, friction and acceleration, the sound
  queue, `SetGfxSlots`, the animation interpreter, graphics decompression and streaming, sprite building, the hitbox tests, the map lookups and tile probes). The harness runs them in place of the originals (`--port replace`) or
  checks every call against the original (`--port shadow`), measures which branches were checked
  (`--coverage`) and drives the rest by differential fuzzing (`--fuzz`): zero mismatches, 44 of 45
  fully covered. `docs/PORT.md` is the port's design document: how routines are accepted, why
  replace mode is not compared byte for byte, and how tasks are ported (stackless, see below).
- **Sprites decoded.** `tools/sprites.py` walks every animation and mapping, draws any enemy type
  with the graphics allocation the game uses, assembles Shion from his equipment, and renders
  rooms with their enemies at their spawn points (`--all-rooms`).
- **Kernel named.** 109 routines and data labels in the `labels` section of
  `analysis/layout.json`: scheduler, sound queue, VDP setup, random, entity animation and physics,
  hitboxes, the enemy library. Documented in `docs/NOTES.md` under "Entities" (field map of the
  entity arrays) and "Named routines".
- **Palettes decoded.** 160 packed palette records at `$599C`; `gfx.py --palette N` renders in
  real colours and `--swatches` shows them all. Hand-given names (`palette_table`,
  `LoadPalettes`, `DecodeGfxBlock`, ...) go in the `labels` section of `analysis/layout.json`.
- A first survey of the engine: task scheduler, entity layout, hardware access, sound and save
  interfaces, data directory, graphics table and its formats. All in `docs/NOTES.md`.

Not done, in the order I would do it:

1. **Decode the rest of the room data**: objects (room command 07: doors, chests, signs), the
   remaining room commands and spawner layouts, enemy category 2. The other directory blocks
   (`dir00`–`dir07`) still have unknown formats.
2. **Disassemble the Z80 sound driver** (`$98000`, 6,775 bytes) and confirm whether
   `$99A77–$9F776` is its music data.
3. **Harden the harness**: explain the remaining pixel differences in the first room, and
   record input scripts that reach each area (save states make that cheap).
4. **Port subsystem by subsystem to C**, comparing RAM state frame by frame against the harness.
5. **Replace the emulated hardware** with a native renderer and input layer.

## The next step

The port now has two kinds of C: hand ports (`port/*.c`, 45 routines) and translated routines
(`tools/m68k2c.py` into `work/gen`, roots in `analysis/translate.json`; the owner chose this as
scaffolding, see `docs/PORT.md`). Next:

Done so far: every task is translated, including `Kgam`, the gameplay task: 1,094 generated
routines, zero mismatches over the standard runs, no generated code left in them, and replace
mode matches the original pixel for pixel in gameplay (see `docs/PORT.md`, "Tasks and Yield"
and "The gameplay task"). Only the frame loop, the scheduler and the VBlank interrupt are still
68000. Coverage is 36% of the translated instructions. Next:

Owner save states in `work/` (not committed): `platform.state` (area 1 room 2, floating
platforms), `inn2.state` (at an innkeeper's question; answering yes found the `$C262` hang),
`owner2.state` (Sega logo after a game over), `castle.state` (inside a castle, frame 29,683), `ocarina.state` (on a note block, frame 35,051; the tunes are in `docs/NOTES.md`), `beforedeath.state` (past the ocarina doors, 2 of 4 hearts, frame 48,363), `fullhp2.state` (the same, health refilled with `--call Hearts`), `after-boss.state` (a cave past the boss, frame 83,367), `after-boss2.state` (a forest with hanging spiders, frame 166,373), `boss2.state` (the Gragg & Glagg boss intro, 4 hearts, frame 287,023), `after-boss3.state` (a forest town with an inn after beating them, 4 hearts, frame 298,964; `after-boss3-14hp.state` is the same with the 14 hearts it was played at), `door-crash.state`
(before the castle door that found the `lea` gap, frame 305,313), `thunder.state` (a new area
with the thunder spell, frame 384,572: right + A casts thunder, left + A the other slot). Edit hearts with `harness --load-state S --poke FF9F00=0E00 --poke FFDA00=0E00 --call Hearts --frames F+1 --save-state-at F+1 OUT` (maximum, then health; whole hearts in the high byte). Copy `play.state`/F2 states to their own names:
F2 overwrites the same file each time. `status.state`, `shop.state` and `talk.state` (2026-10-13) are recorded sessions
(`play --record NAME`: the state, and every button in `work/runs/inputs/NAME.txt` up to its `# end`):
the status screen used throughout, a shop, conversations. Ask the owner for recordings like these
(give the `play ... --port shadow --coverage FILE --record NAME` command) when new code needs play.

1. Coverage, from play (53% so far): the owner plays with `play.exe --state S --load --port shadow --coverage FILE` (any
   mismatch is printed to the console and in the report at exit; save states with F2 for
   anything worth re-checking). Merge the files with `tools/coverage.py`. Add any new "left
   generated code" targets to `"seen"` in `analysis/translate.json` (the harness report names
   them).
2. The native build runs (`native/`, `work/native/wbmw.exe`; `docs/PORT.md`, "The native
   build"): the whole game as C, no 68000 emulator, pixel-identical to the original once the
   time shift from the original's dropped frames is accounted for, with sound (`docs/PORT.md`,
   "Sound"). Compared by game frame (`--frame-log`, `tools/framecmp.py`), its picture and game
   state match the original; only raster-timed scheduling differs (three bytes; `docs/PORT.md`,
   "Comparing by game frame"). With `--timing` (optional) it counts the 68000's cycles and is
   identical to the original in every byte and every frame, sound included ("Exact timing").
   It has save states in the harness's format (F2/F4, `--load`), so states move between
   `play.exe` and `wbmw.exe`. The sound driver itself is still Z80 code run by an emulator; translating it is a
   later, separate project.
3. The idiomatic rewrite, subsystem by subsystem, checked against the original the same way
   (`docs/PORT.md`, "The idiomatic rewrite"); that is where the coroutine library belongs.
   Movement and collision is done: all of it ($2E38-$3E1F, the conveyors and the water
   currents) is ordinary C in `port/movement.c`, checked on memory and the stack pointer (and
   d6/d7 for the player), with every other register watched at run time (`work/usedef.bin`
   from `tools/liveness.py`, data registers in three parts; regenerate it when the map
   changes). The player's states ($E2C6) and batches 1 and 2 of its frame ($E1A6, $DE6A,
   $DFA0, $EF4E, $DD7C, $DDDE, $EFE2; $F064, $ED5C, $EF12) are done too, in `port/player.c`.
   Batch 3 (damage $DFDE, pickups $F1EC/$F256/$F262) too; a rewritten routine that jumps away
   instead of returning hands over with `port_continue_at` (docs/PORT.md, batch 3). Still to
   do in the player's update: nothing; with coroutines (`port/coro.c`, `port/task.c`, task
   passes on per-slot coroutines, `task_wait_frame`) the update itself ($DD08), death, the
   revival and full heal and the pickup dispatcher are C too (docs/PORT.md, "Coroutines").
   The special actions (magic, potions: $11170-$11BA3) are C too, in `port/magic.c`; the quake
   and thunder are coroutine loops, their resume addresses registered entries (docs/PORT.md,
   "Magic"). The hearts display ($8AA2, `port/hud.c`), the item and effect spawner ($1090C/$10910,
   `port/effects.c`) and the sword's tip ($EE70) are C too. Next (show the owner a checklist):
   the enemies, in batches (docs/PORT.md, "The enemies"): the framework is done
   (`port/enemies.c`: $133B2, the state machine $13582), and the spawn points
   (`port/spawns.c`, $12A70; kinds 8, 11, 12, 14 still need save states in their rooms for
   coverage), and thirteen handlers, the objects at $1A464-$1AA3B (`port/objects.c`, batch 3),
   and nine more, the townspeople at $163AA-$168E7 (`port/people.c`, batch 4), and eight small
   ones and the last four (`port/handlers.c`, batches 5 and 6): all 35 handlers are C. Next the
   monsters' action routines (135 entries at $140F2-$1A3E4, about 7,000 instructions), small
   to large, in batches of related routines: batches 7-27 (`port/actions.c`, appearing,
   defeated, hurt, walking, choosing, patrolling, jumping, attacking, swimming, flying,
   Gragg & Glagg, the water creatures, the chain, bouncers, a dodger, a boss and its brood, the two-part boss, two riders and their followers, a hoverer, a runner, a node walker and the last boss, $140F2-$1A437: all 135) are done. `tools/accept.sh OUTDIR NAMES...` runs the whole acceptance. Now the rest of the text system (status screen, shops,
   dialogue, $7600-$8900). In progress: the status screen ($7582 and $7294-$8C34, `port/status.c`; batches T1-T4: all of it, the menus as coroutines, and the pause screen); next the shops and dialogue: the message interpreter ($13BC, called each frame, its commands at $14F4-$1CBC; batches T5-T7, the window, its helpers and the interpreter with its commands, the quiz and the inn, `port/message.c`, done; and the ocarina's tune, $2AAE, T8; then the boss's life bar, the special actions' uses and the game's start, $885A-$8EB0, T9, `port/status.c`; the title's tasks and menu, $7042-$72F0, T10, `port/title.c`; and the rooms' unpackers, $9F46-$A1FE, T11, `port/rooms.c`; loading a room, $9330-$9720 and $92D4, T12; setting the layers up, $9808-$9E60, T13; the layers each frame, $9720-$9F46, T14; the map's cells, the game's flags and rectangles, $260A-$276A and $A1FE-$A3D6, T15; the room's triggers, $A3D8-$A990, T16; the room changes' helpers, $B8D4-$BD9A, T17-T18; their sequences for doors, shops and houses, $BDB0-$C184, T19; arriving in a room, $C184-$C75C, T20; breaking and crumbling walls and the doors' records, $B32E-$B8D4, T21; the gates, the doors' records each frame, $B50C-$B852, T22; three room objects and a shattered block, $B00A-$B32E, T23; the rooms' scripts 0 and 1, the drops and a dropped thing, $B42A and $C75C-$CB5C, T24; the rooms' scripts 2-4, a lift, $CB5C-$CCF2, T25; the rooms' scripts 5-10, $CCF2-$D00C, T26; the sequences of a conversation, a thing received and a statue, $C1E8-$C572, T27; the rooms' scripts 11 and 12, $D00C-$D1FE, T28; the script 12's frames and 13's setup, $D1FE-$D44C, T29; the script 13's ride, 14's and 15's setups, $D44C-$D766, T30; the scripts 15 and 19-23 and the scripts' helpers, $D766-$DB3C, T31; the room's event list's commands, $A990-$ABBE, T32; the room's event list, $ABBE-$AEA2, T33; the room objects' runner and kinds, $ACFA-$B00A, T34; the game task's room entry, next room and game over, $8EB4-$92A2, T35; the HUD's panel and the status screen's labels, T36; the change of equipment and the player's looks, $DB3C and $F5FC-$F76C, T37; the player placed in a room and the companion set up, $DBBE-$DD08 and $F880-$FA6C, T38; the companion each frame, kinds 0 and 4, $FA6C-$FFE6, T39; the door poses, the exits and the pickups' entry, $DF80-$DFA0, $F76C-$F880 and $F1B0, T40), then the townspeople's $1609E, $164B8. The angle maths ($EE2-$1278, `port/maths.c`, exact) and the HUD
   bar (magic name and uses, meter, slot icons, gold: `port/hud.c`) are C too. The items and effects each frame ($10A1E, `port/items.c`), the big items
   ($B42A) and exact kernel helpers (ClearEntity, Freeze/UnfreezeEntities, FindFreeEffect,
   CheckGrounded, in `port/entity.c`) are C too. Readable C
   calls what is not rewritten yet through `call_68k` (`port/world.c`). Fuzz
   input generators live in
   `harness/hybrid.c` and are game-specific; move them to a game file when the second game
   starts.

Features the owner wants later (settings menu on pause, exit confirmation, enemy health and damage numbers, wide screen) are
listed in `docs/IDEAS.md`; add new requests there.

Every ported routine is accepted only with zero shadow mismatches and every instruction of the
original covered (gameplay runs plus fuzzing); see "How a ported routine is accepted" in
`docs/PORT.md`.
Write C string edits with the editor, never through a shell heredoc or a script run from the
shell: the tool call adds a layer of escaping, and a backslash-n in C source has come out as a
real line break five times.

The source rebuilds identically but is **not fully relocatable**. Still numeric:

- pointers inside the embedded data of the code region (20.7 KB of `dc.b`), including the
  descriptor lists spawned through `$5106`;
- pointers inside the `incbin` blocks (directory blocks, animation data);
- a few `move.l #imm` whose role is undecided (`-$2700` field: `$D35A`, `$D362`), and the
  `addi.l #$418c` at `$3756`, which indexes off `a1` and is probably a RAM offset.

Relocatability matters for modding and for proving the data/code split, not for the C port, so
do it as each data format is decoded rather than as a separate pass.

How the rebuild works, and what to keep:

- vasm is run with `-no-opt`, so every size the source states is the size it gets. Capstone's
  text is the input; `mkasm.py` fixes its quirks: bit operations (`btst` etc.) are sized by the
  destination (`.b` on memory, `.l` on `Dn`), not as Capstone prints them; `move usp` comes out
  unsized; short absolute RAM addresses must be written sign-extended (`($ffff8a49).w`); `moveq`
  immediates must be signed.
- `tools/checkasm.py` assembles with a listing and compares every line against the ROM. Any
  instruction that differs goes into a raw list (`--raw` to `mkasm.py`) and is emitted as `dc.w`
  with the mnemonic in a comment. It is the tool to run when a change breaks the hash.
- The rebuild found three tracer errors, all fixed: Capstone prints undecodable words as `dc.w`
  and `trace.py` accepted that as an instruction (it made `"PAUSE",0` at `$7F4E` look like a
  task descriptor); the `data_dst` hint only matched `a4`-based entity fields, so data stored
  through `a2` at `$18B9E` was traced as code; and field `-$2A00` was not hinted, so the data at
  `$1A3DE` was traced as code.
- Two hints with different meanings: `data_dst` says "values stored here are never code
  pointers" (the tracer uses it), `data_ptr` says "values stored here are always data addresses"
  (the generator labels them). A velocity field belongs in the first, not the second.
- `mkasm.py` labels an immediate only by rule (see the `addr_imm` pass). Do not label every
  immediate that happens to equal an address: constants like `#$2000` collide with code labels.

## The ROM

- File: `Wonder Boy in Monster World (USA, Europe).md`, 786,432 bytes.
- SHA-1 `87a968f773c7e807e647c0737132457b06b78276`, header checksum `$9D79` (verified).
- It belongs in `rom/`, which is git-ignored. Ask the owner for it if it is missing.

## Rules for this repository

- **Nothing derived from the ROM is committed.** `rom/` and `work/` are git-ignored. The
  repository is public. Tools, notes, hand-written port code and symbol names are fine to commit;
  ROM bytes, the generated listing, and extracted graphics, maps, text or music are not. The port
  must extract assets from the owner's ROM at build or run time. Whether the disassembly source
  itself should be committed once it exists is the owner's call. Ask before committing it.
- **Verify, do not assume.** Every claim about the ROM should be checkable by a script in
  `tools/`. When a tool's heuristics change, re-run `tools/fixpoint.sh` and confirm it still
  reports `problems: 0` and the same or better coverage.
- **Record findings in `docs/NOTES.md`** as you go, with addresses. Later sessions start from it.
- **Hints, not hard-coding.** Manual knowledge the tracer cannot derive goes in
  `analysis/hints.json` (extra entry points, manual tables, data ranges), not into the scripts.
  The same goes for every game fact a tool needs (addresses, sizes, hashes, file names): read it
  from `analysis/`, so the tool works on the next game. `docs/PROCESS.md` lists the places that
  still hard-code this game; fix them when you next touch those tools.

## Using the tools

    pip install capstone
    tools/fixpoint.sh "rom/Wonder Boy in Monster World (USA, Europe).md"

That writes `work/map.json` and `work/code_000200_01D000.lst`. On Windows, where only
`python.exe` exists, run it as `PYTHON=python tools/fixpoint.sh ...` from Git Bash.

Owner's machine (Windows 11): Python 3.12.10 and capstone 5.0.7 in
`%LOCALAPPDATA%\Programs\Python\Python312`. Assembler: vasm 2.0f (`vasmm68k_mot`, motorola
syntax module 3.19f, official Win64 binary) in `%LOCALAPPDATA%\Programs\vasm`. Neither is on
PATH, so prepend both before running tools (in Git Bash, through `cygpath -u "$LOCALAPPDATA"`:
a Windows path in PATH breaks it). The machine has 12 cores: build with `make -j12` (the
translated code is split into 12 files for this; harness about 80 s, native about 150 s from
scratch). Per batch, build the harness and run `tools/accept.sh`; build and smoke-test the
native build once at the end of a session (the owner's choice). Other scripts:

- `tools/header.py ROM` prints vectors and header fields and checks the checksum.
- `tools/dis.py ROM ADDR [COUNT]` does a quick linear disassembly from a hex address.
- `tools/gaps.py ROM work/map.json [MINSIZE]` lists untraced ranges with a rough code-or-data
  score (how much decodes, how many `rts`).

`work/map.json` fields: `kind` is one byte per ROM byte as hex (0 unknown, 1 instruction start,
2 instruction body, 3 table or tag). `insn` maps address to size, mnemonic and operands. `orphans`
lists entry points found by heuristic with no static reference.

## Things that will trip you up

- **Capstone accepts 68020 addressing in 68000 mode.** `trace.py` rejects any decode whose
  operands contain `[` or `*`. Keep that check; without it data decodes as valid code.
- **Short absolute addresses sign-extend.** `$8A49.w` is RAM at `$FF8A49`. Nearly all RAM access
  is written this way. Work RAM in use is mostly `$FF8000–$FFAFFF` plus the entity arrays at
  `$FFC000–$FFDFFF`.
- **Code runs after a yield.** `jsr $400` pops its own return address into the task slot and
  returns to the scheduler. The instructions after it execute on a later pass, with registers
  not preserved except what the task reloads. Every task yields from its top level (measured
  with `WBMW_YIELD_SURVEY=1`), so tasks are stackless: a resume address and RAM. The translator
  makes each resume point an entry; coroutines wait for the idiomatic rewrite (`docs/PORT.md`).
- **Continuations are stored in RAM.** Several subsystems save a popped return address and jump
  to it later (`$FF9630–$FF9654`, a stack at `$FF9C14`, `$FF9F1E/22`, `$FFA3BE`). The tracer
  treats these as resolved because the saved address is always the fall-through of a call it
  already traced. The 53 "unresolved" indirect jumps it reports are almost all of this kind.
- **The scheduler can time-slice on the raster position** (`$C00008`, bit 6 of `$FF8A5D`). The
  harness must model the HV counter well enough to reproduce this, and the port needs a fixed
  budget in its place.
- **The 46 orphan routines are guesses.** They decode cleanly and follow a terminator, but
  nothing references them statically. Some are real code reached through tables not yet
  understood; some may be data. Treat them as unconfirmed until the harness sees them execute.
- **Jump-table lengths are inferred**, bounded by the first target, known references and a
  sandboxed trace of each entry. A table that reads one entry too far shows up as a tracing
  conflict, which is why `problems: 0` matters.

## Working with the owner

The owner supplied the ROM and will be the one playing builds. You can run things headless and
inspect rendered frames as images, but only they can judge how the game feels and sounds, so give
them something runnable as early as the harness allows and ask for specific checks. Keep reports
short and lead with what changed and what you need from them.
