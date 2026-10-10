# Wonder Boy in Monster World — decompilation and native port

## What this project is

The owner wants the Sega Genesis game *Wonder Boy in Monster World* (USA/Europe) running as a
native x86 program: a full decompilation and port, not an emulator wrapper and not a static
recompilation. The game is hand-written 68000 assembly (38 stack frames in 28,000 instructions),
so there is no original C to recover. "Decompiling" here means understanding each routine and
rewriting it in C that behaves identically.

This is a long project spread over many sessions. Work started in a cloud session that did the
first survey; you are picking it up on the owner's machine. Read `docs/NOTES.md` before doing
anything else. It holds every address and structural fact found so far.

## Where things stand

Done:

- ROM identified and verified (details below).
- A recursive-descent tracer (`tools/trace.py`) that separates code from data in the code region
  `$200–$1D000`. It finds 95,174 bytes of code in 845 routines, with zero tracing conflicts.
- A labelled listing generator (`tools/listing.py`). The listing's bytes match the ROM exactly
  over the code region, but it is in Capstone's syntax and no assembler will accept it.
- A first survey of the engine: task scheduler, entity layout, hardware access, sound and save
  interfaces. All in `docs/NOTES.md`.

Not done, in the order I would do it:

1. **Reassemblable source with a byte-identical rebuild.** See "The next step" below.
2. **Split the rest of the ROM** (`$1D000–$A4C76`) into named binary includes, starting from the
   data directory at `$1CC00`, so the whole ROM rebuilds from source plus extracted blobs.
3. **Name the kernel.** Start with the most-called routines: `$7E8` (160 callers), `$400`
   (yield, 123), `$366` (queue sound command, 98), `$5D8` (73), `$43BC` (61), `$48C4` (49).
   Keep names in a symbol file the tools read, not hard-coded in scripts.
4. **Identify the graphics compression** and write an extractor that dumps tiles to PNG. The
   high-entropy data sits at `$40000–$97FFF`; the loaders have not been found yet. Nothing in the
   code references that area directly, so follow the pointers in the `$1CC00` directory.
5. **Disassemble the Z80 sound driver** (`$98000`, 6,775 bytes) and find its music data.
6. **Build a reference harness**: the original 68000 code running under an interpreter core
   against a reimplemented hardware layer, able to run headless, dump frames as images, and dump
   RAM per frame. This is the oracle every later step is checked against.
7. **Port subsystem by subsystem to C**, comparing RAM state frame by frame against the harness.
8. **Replace the emulated hardware** with a native renderer and input layer.

## The next step

Turn the listing into source an assembler accepts, and prove it by rebuilding the ROM and
comparing SHA-1. Nothing after this is trustworthy until it passes, because a byte-identical
rebuild is the only proof the disassembly misread nothing.

Things that make this harder than it sounds:

- **Encoding choices.** The 68000 has several encodings for the same operation: short versus
  long absolute addresses, 8-bit versus 16-bit branches, `add` versus `addi`/`adda`/`addq`,
  `move` versus `moveq`, a zero displacement versus none. An assembler left to choose will pick
  differently from the original in places. Emit explicit sizes everywhere, and for any
  instruction that still does not round-trip, fall back to emitting its raw words with the
  mnemonic in a comment. Track how many fall back and drive that number down.
- **Labels.** Every branch target, call target, table entry and data reference must become a
  symbol, including the word-offset jump tables (emit them as `dc.w target-base`). The map in
  `work/map.json` already records all of these: `calls`, `jumps`, `auto_tables`, `tables`,
  `bra_tables`, `descriptors`, `refs`.
- **Assembler choice is open.** None was installed where the survey ran. `vasm` (Motorola
  syntax) and `clownassembler` are the usual choices for Genesis disassemblies. Pick one, record
  the choice and version here, and write a `Makefile` whose default target builds the ROM and
  fails if the hash differs.

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

## Using the tools

    pip install capstone
    tools/fixpoint.sh "rom/Wonder Boy in Monster World (USA, Europe).md"

That writes `work/map.json` and `work/code_000200_01D000.lst`. Other scripts:

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
  not preserved except what the task reloads. This is the central design problem for the C port:
  each task needs either a real coroutine or a rewrite as a state machine. Decide this once,
  early, and apply it uniformly.
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
