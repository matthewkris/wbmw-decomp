# Process and lessons

This project is the first run of a process the owner wants to reuse on other games. This file
records the process as it actually happened, what made it work, what went wrong, and which
parts are general and which belong to this game. Decisions in this repository should be made
with a second game in mind: when a choice is otherwise even, prefer the one that keeps the
general parts general.

Written after the reference harness and before the C port (October 2026). Update it at each
major milestone.

## The pipeline, in the order it ran

| # | Stage | Output here | Proof it is right |
|---|---|---|---|
| 1 | Identify the ROM | header, checksum, SHA-1 | header checksum matches |
| 2 | Separate code from data | `tools/trace.py`, `work/map.json` | zero tracing conflicts (`problems: 0`) |
| 3 | Reassemble | `tools/mkasm.py`, vasm, `tools/build.sh` | byte-identical rebuild (SHA-1) |
| 4 | Lay out the whole ROM | `analysis/layout.json` | still byte-identical; every byte in a named piece |
| 5 | Decode assets, one format at a time | `gfx.py`, palettes, `rooms.py`, `sprites.py` | structural invariants over every instance (below) |
| 6 | Name the code | `labels` in `layout.json`, NOTES tables | still byte-identical; names read right in context |
| 7 | Reference harness | `harness/` on Musashi | boots into the game; matches the decoders' renders |
| 8 | Playable window, save states, run diff | `play.exe`, `--save-state-at`, `cmpruns.py` | a run from a state equals the straight run |
| 9 | Port routine by routine, by hand | `port/` (45 routines) | shadow check on every call, full coverage, differential fuzzing |
| 10 | Translate the rest mechanically, as scaffolding | `tools/m68k2c.py`, `port/m68k_rt.h`, `work/gen` | the same checks as hand ports |
| 11 | Rewrite into idiomatic C, subsystem by subsystem | not started | the same checks, against the original |

Each stage made the next one cheaper or checkable. Stage 3 is what made stage 2 trustworthy;
stage 7 is what made stage 5 trustworthy; stage 8 is what made stages 9 and 10 trustworthy.

Hand transcription (stage 9) proved the method and the harness tooling on 45 routines, about
1,100 instructions, but at that pace the whole game (28,000 instructions) was too long. The
owner then chose mechanical translation as scaffolding (stage 10): the first subsystem
translated, movement with collision (849 instructions), matched the original on the first run.
Understanding now comes in stage 11, rewriting translated code into idiomatic C. For the next
game, consider starting stage 10 right after the harness, and hand-porting only to prove the
checks.

## What made it work

**A hard, binary check at every stage.** Nothing was "probably right". The rebuild matches the
SHA-1 or it does not; a decoder handles every instance in the ROM with zero problems or it
does not; a run from a save state equals the straight run or it does not. Each check was run
over the whole game, not a sample, and the counts went into the commit messages.

**Structural invariants for decoders, before looking at pictures.** The graphics decoder was
accepted because every one of 726 chunks produced exactly 1,024 bytes and ended exactly where
the next chunk began. Room maps: all 170 rooms parse and decode. Enemy spawns: 497 of 503 use a
type their room loads graphics for, and the 6 exceptions share one pattern. An invariant that
holds over every instance catches errors a good-looking picture hides.

**Transcribe the game's own routine, then verify.** Every decoder is a direct transcription of
the 68000 routine that reads the format, with the routine's address in a comment. Guessing a
format from the data was never needed and would have been slower.

**The real game as the final reference.** Once the harness ran, its CRAM confirmed the palette
decoding exactly and exposed the backdrop colour the room renderer had missed. The decoders and
the harness check each other.

**Instrument before guessing.** The menu hang took one PC-sampling run to locate (stuck in the
EEPROM "no acknowledge" loop) after several rounds of reading code and forming theories had not.
Adding `--pc-trace`, `--vdp-every` and `cmpruns.py` paid for themselves immediately.

**Port by equivalence, measured three ways.** Each C routine is checked against the original
on every call (shadow mode), its branches are counted (coverage), and branches gameplay does not
reach are driven by differential fuzzing from a real game state. All three are game-independent
and live in the harness.

**Owner checks what only a person can.** The owner played builds and reported a drop that
seemed odd; reading the drop routine answered it with exact odds. Give the owner something
runnable as early as possible and ask for specific checks.

## What went wrong, and the rule each one taught

| What happened | Rule |
|---|---|
| The tracer accepted Capstone's `dc.w` (its spelling of "undecodable") as an instruction, so the string `"PAUSE"` looked like a task. Found only by the rebuild. | Do not trust a disassembler's output as validation of code. Reassemble. |
| Three short data blocks (hitbox lists) were traced as code because a hint matched only one address register. | Hints describe the program's meaning (a field), not one spelling of it (`-$2900(a4)`). |
| A hint called "data_dst" was read as "always a data pointer" when it meant "never a code pointer"; a velocity got a label. | Give each hint one meaning and name it for that meaning. |
| Room data offsets were read as unsigned; one room rendered black. The 68000 sign-extends `(aN,dN.w)`. | Take operand semantics from the instruction that uses the data, not from the data. |
| I explained that black room with an invented hardware quirk ("writes go to ROM") and coded a workaround. A check over all 170 lists showed it was false. | Before claiming a quirk, check every instance. Remove a wrong explanation as soon as it is disproved. |
| The generated listing's `dc.b` line breaks made a table look misaligned. | Read table bytes from the ROM, not from the listing. |
| Saving a frame every 120 frames made a blinking sprite look permanently missing. | Do not sample at a period that could alias with the game's (even intervals, blink rates). |
| The EEPROM model counted the falling edge after START as a data bit, so it never acknowledged and the menu hung forever. | Model the protocol the game drives, step by step from its code; the game will hang, not degrade, if the device is wrong. |
| The "in game" frames of the first harness run were the attract demo. | Check which mode the game is in (running tasks, RAM state) before drawing conclusions from a frame. |
| A shell heredoc turned `\n` in C strings into real newlines (five times in all). | Edit C source with the editor, not with scripted string replacement run from the shell. |
| A fuzz run of a long routine stopped after one case at the cycle cap, and still reported the full count. | Report what actually ran; run each case to completion with a cap that is reported as a hang. |
| Coverage counted the first instruction of every routine as never run (the check opened after it). | Validate a new measuring tool on a case with a known answer before trusting its gaps. |
| Fuzzing a stored continuation on its own, and a jump table entry that is a deliberate `bra *` hang, produced garbage and a hang in both versions. | Fuzz inputs must be inputs the routine can really receive; continuations are checked through their callers. |
| Gameplay never reached most hitbox paths (no fights in the standard runs) nor any moving platform. | Coverage, not call counts, decides when a routine is checked; ask the owner for save states where scripts cannot reach. |
| Random fields left collision half covered even from a state among platforms, and neither room with a state had a slope. | Generate *situations* (edges touching, small velocities), and synthesise missing level data in RAM (slope attributes) rather than hunting for a room. Have the generator report how often it built what it aimed for. |
| Scene fuzzing hung the harness inside generated C: a garbage attribute sent a computed jump out of its table. | Fuzz with values the game's own data can produce; a hang in translated code cannot be caught, so a run that never ends should be suspected first of invalid input. |

## General versus game-specific

What is reusable as it stands, what is reusable after changes, and what belongs to this game.

**General for any Mega Drive game:**

* `harness/` VDP, bus, controller, core loop, save states, `play.exe`. Gaps another game may
  need: sound (Z80, YM2612, PSG; this game never drives them from the 68000), shadow/highlight,
  interlace, per-line sprite limits, DMA timing, bank-switching mappers for ROMs over 4 MB, and
  other save hardware (battery SRAM at `$200000`, other EEPROM chips). The EEPROM here is an
  X24C01 at `$200001`, which is this game's choice, not the platform's.
* `tools/mkasm.py` and `tools/checkasm.py`: the Capstone-to-vasm translation, the per-line
  byte check and the raw fallback are general 68000.
* `tools/cmpruns.py`: general, except its RAM name table, which is this game's.
* `tools/m68k2c.py` and `port/m68k_rt.h` (the translator and its runtime), `tools/coverage.py`,
  `tools/reach.py`, and the shadow/coverage/fuzz machinery in `harness/hybrid.c`: general for
  any 68000 game. The fuzz input generators in `hybrid.c` are this game's and should move to a
  game file when a second game starts.
* `tools/gfx.py`'s PNG writing and palette conversion; `tools/header.py`.

**General for any 68000 game, after removing hard-coded values:**

* `tools/trace.py`: the recursive descent, jump-table bounding and plausibility checks are
  general. Game-specific inside it: `CODE_END` (`$1D000`) and `SPAWN`/`SPAWN_LIST`, the
  addresses of this game's task-spawn routines (`$50D4`, `$50E2`, `$47A`, `$4A2`, `$444`,
  `$5106`). These belong in `analysis/hints.json`.
* `tools/mkasm.py` and `tools/listing.py`: `END = $1D000`.
* `tools/build.sh` and `Makefile`: the ROM file name and the expected SHA-1.

**This game only:**

* Every format decoder: the graphics table and its three formats, palette records, rooms,
  blocks and chunks, animation scripts, mappings, enemy records, spawn scripts.
  The *method* carries over; the code does not, beyond small helpers.
* `analysis/hints.json`, `analysis/layout.json`, `docs/NOTES.md`.
* The engine design: cooperative tasks with `Yield`, continuations in RAM, structure-of-arrays
  entities. Another game will have its own, and the port's central design question (how to
  express code that runs after a yield) has to be answered per game.

## Decisions to make with reuse in mind

1. **Configuration, not constants.** When a tool needs a game fact (an address, a size, a
   hash, a file name), read it from `analysis/` rather than writing it into the script. Fix the
   existing cases listed above when those tools are next touched.
2. **Keep the platform layer separate from the game in the port.** The port's renderer, input,
   audio and timing should be a library that knows the Mega Drive and nothing about this game,
   so a second Mega Drive port starts with it. Game logic calls it; it never calls game logic.
3. **The harness stays the platform oracle.** Its job is to be a faithful Mega Drive, so fixes
   to it go in for every game. Game-specific knowledge (RAM names, mode detection) goes in
   files the tools load, not in the harness.
4. **One repository per game for now**, with the general parts kept clean enough to lift out.
   Extract a shared toolkit when the second game starts and it is clear which parts it needs.
   Extracting earlier means guessing the boundary from one example.
5. **Nothing derived from a ROM in any repository.** Tools extract from the owner's ROM at build
   or run time. This held here and should hold everywhere.

## The process as a checklist for the next game

1. Verify the ROM (size, header checksum, hash). Record it.
2. Get the vectors and header; trace code from the vectors; fix tracer conflicts with hints until
   `problems: 0`.
3. Reassemble byte-identically before trusting anything. Record the assembler and version.
4. Lay out the rest of the ROM from the pointers the code uses; keep the rebuild identical.
5. Find the graphics loaders from the code that writes VRAM, transcribe them, verify over every
   chunk, then palettes (from the code that writes CRAM), then maps, then sprites.
6. Name routines as they are understood, with a line of documentation each.
7. Bring up the harness. Expect the game to hang on hardware details (save chips, controller
   protocol, status bits); sample the PC to find where.
8. Give the owner a playable window early; add save states and run comparison.
9. Port one subsystem at a time against the harness.

Environment notes for a Windows machine: Python's `python3` name does not exist (use
`python`); the MSYS2 GCC needs its own shell (Git Bash cannot give it a temp directory); SDL2
comes from MSYS2 and its DLL must sit next to the executable.
