# Sound: the Z80 driver and its music

The 68000 never touches the sound chips. At start-up (`LoadZ80Driver`, $306) it copies the sound
driver (ROM $98000, 6,775 bytes) into the Z80's RAM, and from then on it only queues one-byte
sound commands (`QueueSound`, $366: a 16-entry ring at Z80 $1C30, with a busy flag at $1C00). The
Z80 runs the driver, which plays the music and effects on the YM2612 (6 FM voices) and the
SN76489 (3 tone voices and noise). The native build still runs the driver on an emulated Z80;
the music it plays is decoded here, as files that build back byte for byte.

## Tools

* `tools/z80dis.py BIN [--entry ADDR ...]`: a Z80 disassembler (decoded by the opcode's fields,
  the documented set and the common undocumented ones) and a recursive-descent tracer, as
  `tools/trace.py` is for the 68000. The driver traces from `$0000` with the special commands'
  table ($0275) and the track commands' table ($0E62) added as entries: 4,052 of its 6,775 bytes
  are code, the rest its tables (below).
* `tools/music.py ROM`: the music's parser, encoder and checks; `tools/extract.py` uses it to
  write `assets/sound/music.json` and `music.txt`, and `tools/rebuild.py` builds the data back
  from the JSON (`docs/EXTRACT.md`).

## The driver

* Memory: the driver at $0000-$1A76, its stack below $1B80, its variables at $1C00-$1C2F, the
  command ring at $1C30, the 10 voices' records at $1C80 (6 FM, 3 PSG tone, 1 noise: $40 bytes
  each for the first 9), the 10 tracks' records at $1EC0 ($20 bytes each).
* The bank window: it writes bank $98000 to $6000 at start ($006A), so the ROM's $98000-$9FFFF
  is at Z80 $8000-$FFFF: a Z80 address $8000 and up is ROM $98000 + (it - $8000). The music data
  is at ROM $99A77-$9F776, Z80 $9A77-$F776.
* No interrupts (the handler at $38 is `reti`): the main loop ($00B0) waits on the YM2612's timer
  B ($4000 bit 1) for its tempo, reads a command from the ring ($019C), steps the tracks ($0B7F)
  and updates the voices.
* A command $FA-$FF is the driver's own (the table at $0275: stop all, fades, pause); $58-$F9
  are ignored; $00-$57 are sounds, from the sound table at Z80 $9B01.
* Tracks are logical. A note takes a voice of the track's type (`voice_type`), among those its
  mask allows (`voices`), the lowest priority one if all are busy ($0393, $040D). A song's
  tracks may be pushed off a voice by an effect of a higher priority, and come back after it.
* Tables in the driver: the FM frequency numbers at $107B (8 steps a semitone, from a C), the PSG
  periods at $113D, the attenuation tables at $1200 and $1240, the lengths at $0E35, the FM
  instruments from $1281, the drum notes at $19F1 (6 bytes each). Not decoded yet: kept in
  `sound/z80_driver.bin` as they are.

## The music's format

The sound table (Z80 $9B01, 88 words) points at each sound's header. Before it, at $9A77, 138
bytes: seven words pointing into the driver's own tables, zeros, and the count ($58); the code
does not read them (kept as they are, `header` in the JSON).

* **A song** (26: $00-$17, $54, $55): a priority byte with bit 7 set ($88), its tempo (a word:
  timer B's step), then its tracks' addresses, to a zero word. Its tracks go to the track
  records in order ($01E2).
* **A sound effect** (62): a priority byte, then (channel, track address) pairs, to an $FF
  ($022D). Two sounds ($4B, $4C) share one header.

A track is a stream of bytes ($0C18):

| Bytes | Meaning |
|---|---|
| $00-$5F | a note, then the step's length passes (note 0 is C0, note 48 C4: block = n / 12, $0516) |
| $60-$BF | a note (minus $60) that takes no time: it sounds with the next (chords) |
| $C0-$CF | the step's length from the table at $0E35 (its byte times 3) |
| $D0-$D7 + word | call the section at the word, 1-8 times |
| $D8-$DF | loop start, 1-8 times (to the next `loop_end`) |
| $E0 b | slide: speed, slides on |
| $E1 | slides off |
| $E2 b, $E3 w | the step's length, a byte or a word (times 3) |
| $E4 w | tempo |
| $E5-$EF | nothing |
| $F0 b | instrument |
| $F1 b | gate: how much of the step a note sounds |
| $F2 w, $F3 w | detune (in 1/256 of a note), set or added |
| $F4 b, $F5 b | volume (attenuation: higher is quieter), set or added |
| $F6 b | voice type: 0 FM, 2 PSG tone, 4 noise |
| $F7 b | the voices of that type it may take, a bit each |
| $F8 | drum mode on/off (notes from the drum table) |
| $F9 b | FM pan (left, right) |
| $FA | rest: the step's length without a note |
| $FB | key off |
| $FC | tie: the next note carries on |
| $FD w | go to |
| $FE | loop end |
| $FF | return from a call; the track's end at the top |

All 88 sounds parse: 636 tracks and sections, 86 of them labels inside others (a go to back into
a track's own loop, a call into one), and 12 tracks nothing points at (written as `unused`). With
the header and the table they cover the 23,808 bytes of the data exactly, and encode back byte
for byte. In `music.txt` a track reads like `voice_type 0 instrument 7 l4 gate 16 C5 rest D#5`:
notes by name, `+X` for a note that sounds with the next, `lN` a length, `[N ... ]` a loop.

## Checked

* `tools/rebuild.py`: the 737 pieces (header, table, headers, tracks) built from `music.json`
  equal the ROM's bytes.
* The game played from the pack (`docs/EXTRACT.md`), whose music is built from `music.json`,
  gives the same sound output as from the ROM, sample for sample (`--wav`): the title, and 900
  frames from two save states.

## Not done yet

* The driver's own tables (the FM instruments, the PSG and drum tables) decoded and made files.
* Which song is which in the game (the commands the 68000 queues for each room, `music.json`
  in `rooms/`), and names for the effects.
* A MIDI export for listening and editing in other tools, and an import back.
* The driver in C (replacing the emulated Z80), checked against it by comparing every write to
  the sound chips.
