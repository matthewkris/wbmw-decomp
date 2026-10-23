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
* The driver's code is $0000-$107A (`sound/z80_driver.bin`, as it is: a few small tables are in
  it, such as the lengths at $0E35 and the commands' tables at $0275 and $0E62). Its tables follow,
  $107B-$1A76, decoded by `tools/soundtables.py` into `sound/instruments.json` (below).

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

## The driver's tables (`instruments.json`)

Each read where the code reads it, with the size it uses; 7 bytes between tables that nothing
reads are kept as they are (`filler`). They encode back byte for byte.

| Table | Z80 | Read by | What |
|---|---|---|---|
| `fm_frequencies` | $107B | $0586 | 97 FM frequency numbers: 8 steps a semitone, from a C |
| `psg_periods` | $113D | $0549 | 97 PSG periods, the same steps |
| `attenuation_fm`, `attenuation_psg` | $1200, $1240 | $0E1B | a track's volume (`volume`, halved) to an FM level (0-127) or a PSG one (steps of 8) |
| `fm_instruments` | $1281 | $0A08 | 51 instruments of 19 bytes: algorithm and feedback (register $B0), a level, the LFO sensitivity ($B4, the pan added); for each operator, in register order (1, 3, 2, 4), its level, its detune and multiple ($30) and an envelope's number; two bytes not named yet (the voice's +$26, +$27); a script of each kind (`mod_a`, `mod_b`) |
| `envelopes` | $164B | $0698 | 119 operator envelopes: registers $50 (key scale, attack), $60 (AM, decay), $70 (sustain rate), $80 (sustain level, release) |
| `psg_instruments` | $1828 | $0AB2 | 17 instruments of 6 bytes: three not named yet (the voice's +$2F, +$2D, +$2E), a volume envelope's number, a script of each kind |
| `psg_envelopes` | $188F | $0AD6, $09CF | 8 volume envelopes, by pointers: a byte a step, its high nibble the attenuation (times 8), its low the frames it lasts (0: held), $F0 the end |
| `mod_a`, `mod_b` | $1901, $19D8 | $0B04 | 14 and 3 scripts by pointers (the voice's +$10 and +$20), kept as bytes: their format is not read yet |
| `drums` | $19F1 | $0CB3 | 22 drum notes (a track in drum mode plays note n as drum n): voice type, voices, instrument, a byte, a pitch (a word) |

Changing them changes the sound: with every FM instrument's algorithm set to 7, the pack built
from the edited files plays a different title.

## Which song plays where

`music.json` gives each song's `plays`, and `music.txt` lists them under each song
(`music.uses`). Most come from the rooms' music (the directory's table 3, read by `RoomMusic`,
$92D4, into $9660): a byte per room, a song under $80; $80 + n picks between the two songs of
the pair n at $932A by a game flag (area 2 room 0: song $0A, then $02 once flag $37 is set; area
6 rooms 5, 6, 8: $09, then $0D once flag $38 is set; a third pair, by flag $61, unused); $EF
picks song $0F if game flag $4D or $4E is set, else $55 ($DAC8: area 12 rooms 2 and 3); $F0 and
up keep the music playing. The rest the code queues itself (`CODE_SONGS`):

| Song | Plays |
|---|---|
| $14 | the title ($718A) |
| $0D | a boss's entrance (spawn point kind 12, once the player is below $1040, $13380); also rooms of areas 6 and 12 |
| $13 | a boss beaten, outside area 18 ($176F4) |
| $15 | the ride of room script 13 ($D48C: area 16 room 4, area 18 room 6) |
| $16 | the ending ($1B016) |
| $54 | the night at the inn (a conversation ended by a fade, $C1E8) |

All 26 songs are accounted for. Found by logging every sound command over all the save states
(a temporary hook in `QueueSound`) and then from the code: a state starts with its music
playing, so the log showed only the title's and room changes'.

## Checked

* `tools/rebuild.py`: the 737 pieces (header, table, headers, tracks) built from `music.json`
  equal the ROM's bytes.
* The game played from the pack (`docs/EXTRACT.md`), whose music is built from `music.json`,
  gives the same sound output as from the ROM, sample for sample (`--wav`): the title, and 900
  frames from two save states.

## Not done yet

* The modulation scripts' format, and names for the instruments' unnamed bytes.
* Names for the effects (where the 68000 queues each).
* A MIDI export for listening and editing in other tools, and an import back.
* The driver in C (replacing the emulated Z80), checked against it by comparing every write to
  the sound chips.
