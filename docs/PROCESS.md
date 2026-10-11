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
| Coverage by address range charged a translated routine with its neighbours' code: a tail jump stretched the range over three other routines. | Measure coverage against the exact instruction set the translator emitted for each routine, not an address range. |
| The coroutine design was chosen before measuring how tasks yield; a one-hour survey showed every task yields from its top level, so tasks are stackless and needed no coroutines to translate. | Measure the runtime property a design depends on before building it, and record the evidence with the decision. |
| Generated code took the wrong path: it was laid out by address and the entry was not the lowest one. Invisible until a routine jumped backwards. | Emit an explicit jump to the entry; test a translator on code shapes beyond the first subsystem. |
| Translated code read an earlier raster position than the original (it runs in no emulated time), and took a different branch. | Treat timing reads (VDP status, HV counter) as inputs: record what the port read, replay it to the original during the check. |
| A translated wait-for-VBlank loop spun forever: only an interrupt ends it, and translated code takes no time. | Waits on interrupts or time are boundaries (exits) where translated code hands control back to the machine. |
| A fixed-size per-routine table overflowed once the translation grew past 1,024 routines, and the report showed nonsense (negative call counts). | Size tables from the data; treat an absurd report as a tool bug before reading it as a result. |
| A check closed at the original's first return to the caller, while the C version legitimately ran on (a routine jumping back through a saved return address). | End a differential check at the point the port itself ended, and report when the original never gets there. |
| Computed jumps through data the tracer could not follow left the translation incomplete. | Let the harness report where translated code had to fall back to the original, feed those targets back as roots, and repeat until a run needs no fallback. |
| Sleeping at an inn froze the translated game: a second wait-for-interrupt loop, inside the gameplay task, that no scripted run reached. Found from the owner's save state. | Search the ROM for every reader of interrupt-set flags when choosing exits; give translated code a watchdog so a hang names its loop; owner save states at the point of failure make such bugs a one-run reproduction. |
| The playable window printed nothing: SDL's link flags made it a Windows GUI program, which has no console, so every mismatch report from the owner's sessions was lost. | Check that the tool the owner runs actually shows its reports (link it as a console program); a silent check is no check. |
| Run-time discovery of computed-jump targets only finds what was played; a native build needs them all. | Classify the unresolved sites. In this engine almost all were continuations (a routine saving its own return address), stored constants or hint tables, each with a static rule. Measure it by dropping the run-time list and checking that no run falls back. |
| The native build stopped at a task the tracer never found (started from a descriptor list taken for data); the harness had run it as original code without anyone noticing. | Running without the original code is the final test of the trace; scan for the engine's own markers (task tags) to find code the trace missed. |
| Native and original frames diverged in the attract demo, but the native picture was found 154 frames later in the original. | Translated code takes no time, so it never drops frames; compare by game frame, not real frame, or find the time shift before calling a difference a bug. |
| Adding a second processor (the Z80) turned a 68000 wait loop that had never been taken into one that could hang zero-time code. | Each new piece of hardware: look for code that waits on it, and give ported code a way to let time pass there. Hold the new processor still during differential checks so its own state changes do not count as differences. |
| Audio cannot be judged by listening here. | Record it to a file and measure it (silence, levels, clipping, DC offset, stereo); compare two builds by the sequence of chip writes, not by waveform, since timing shifts phases. |
| Comparing a zero-time port with the original frame by frame only measured the time shift. | Compare at the same point of each game frame (the game's own frame counter), with input keyed to it; then whatever still differs is timing the port does not model (here, raster-timed task scheduling), and names its own bytes. |
| Exact timing for a zero-time port looked like a big project; with the reference emulator's own cycle table and its slice and interrupt rules copied exactly, it was exact on the first full run. | Take timing from the reference implementation (dump its tables, read its execute loop), not from manuals; then the comparison tools can demand equality, not similarity. |
| Save states for translated code seemed to need the C call stack saved. | Take states only where the original's own stack is empty of pending work (the frame wait, where emulator states stop anyway), make those instructions entries, and resume by starting the code there; then states can share the emulator's format. |
| Following stored code pointers pulled in the whole game at once. | Let the translator leave to the original code for anything untranslated (always correct in the harness), and defer big subsystems explicitly, to keep each step's checks fine-grained. |

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
