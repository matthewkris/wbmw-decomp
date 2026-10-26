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
| `fm_instruments` | $1281 | $0A08 | 51 instruments of 19 bytes: algorithm and feedback (register $B0), a level, the LFO sensitivity ($B4, the pan added); for each operator, in register order (1, 3, 2, 4), its level, its detune and multiple ($30) and an envelope's number; its pitch offset (`pitch_lo`, `pitch_hi`: a word, the voice's +$26/+$27, added to every note: whole notes in the high byte, so most instruments' $F400 is an octave down; a PSG voice's is always $E800, $0AFC); a script of each kind (`mod_a`, `mod_b`) |
| `envelopes` | $164B | $0698 | 119 operator envelopes: registers $50 (key scale, attack), $60 (AM, decay), $70 (sustain rate), $80 (sustain level, release) |
| `psg_instruments` | $1828 | $0AB2 | 17 instruments of 6 bytes: three not named yet (the voice's +$2F, +$2D, +$2E), a volume envelope's number, a script of each kind |
| `psg_envelopes` | $188F | $0AD6, $09CF | 8 volume envelopes, by pointers, as `steps`: [level, frames] (a byte each: its high nibble the attenuation, times 8, its low the frames it lasts, 0 held), `["end"]` for $F0 (the voice let go, $0460) |
| `mod_a`, `mod_b` | $1901, $19D8 | $0B04 | 14 pitch and 3 volume modulation scripts by pointers (the voice's +$10 and +$20), as `items` (below); an instrument names one of each |
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

## Volume: the music's and the effects' apart

The native build's menu has a volume for the music and one for the effects (`native/wbmw.c`,
`volume_frame`). Both come from the same chips, and an effect takes a voice from the music while
it plays, so the split follows the driver's voices: each frame, voice v's record (Z80 $1C80 +
$40 v) says whether it is playing (bit 7) and whether a song's track has it (bit 5, set with the
note at $0DF0). Voices 0-5 are the YM2612's channels 0, 3, 1, 4, 2, 5 (`$0446`: the two halves
alternate), 6-8 the SN76489's tones, and voice 8 its noise too (a noise track takes that record).
A voice not playing keeps its last owner, so a note's release fades at its own volume. Each chip
channel is then weighted (`harness/sound.c`, `sound_gain`; the YM2612's in `harness/ym2612.cpp`,
ymfm's sum of its channels with a gain each), and the mix by the overall volume. At 100 nothing is
multiplied: the sound is the original's, sample for sample (checked on the title and a boss).
Checked: the title with the music at 0 is near silence until the demo's effects start; in play,
the sword's swings stay with the music at 0 and go with the effects at 0; the overall volume at
50 halves the level.

## Checked

* `tools/rebuild.py`: the 737 pieces (header, table, headers, tracks) built from `music.json`
  equal the ROM's bytes.
* The game played from the pack (`docs/EXTRACT.md`), whose music is built from `music.json`,
  gives the same sound output as from the ROM, sample for sample (`--wav`): the title, and 900
  frames from two save states.

## The modulation scripts

An instrument names a pitch script (`mod_a`, read by $07D7) and a volume script (`mod_b`, $08F3).
A pitch script moves the voice's pitch offset (+$14, in 1/256 of a note, added to the note), a
volume script its volume offset (+$1C, added to the attenuation). Every `rate` frames (+$3A /
+$38, 1 at first) the offset moves by the current step's delta; when the step's count runs out
the next item is read. In `instruments.json` each script has its address (a `goto` names one)
and its items:

| Byte(s) | Item | Meaning |
|---|---|---|
| $00 | `end` | nothing more: the offset held |
| n, delta | `steps`, `delta` | n steps of the delta (a pitch script's a word, a volume script's a byte) |
| $FF word | `goto` | go on at the address |
| $FE | `loop_end` | back to the loop's start until its count runs out |
| $FD n | `loop` | the loop's start, n times |
| $FC n | `reverse` | the delta turned round, for n steps |
| $FB value | `set` | the offset set (a word, or a byte) |
| $FA | `stop` | the delta 0: the offset held |
| $F9 n | `rate` | n frames between steps |
| $F8 depth n | `random` | (pitch only) a random offset for n steps, the random byte shifted up by depth ($0B66) |

So a vibrato is a delay, a few steps one way, then `reverse` in a loop: script 1 ($191E) is 4
steps of nothing, 8 steps of -1 every 4 frames, then turns every 16 steps. The others: drops and
rises (a sound sliding down or up), trills built of single big steps, random wobbles (scripts 7
and 10, for noise-like sounds), and 0 (none). Bytes after a script's end that are never read
are kept (`tail`: $19D3's last byte, whose step would run into the next table).

The songs' instruments use scripts 0, 1, 5 (vibratos, 8 or 12 steps of 1/256 of a note: about
3-5 cents) and 12 (a dive, twice); the effects use the rest. The MIDI files leave them out: the
vibratos are too slight to show with MIDI's modulation.

## The sound effects

Named by where the game queues each (`music.EFFECTS`; in `music.json` each effect's `name` and
`used`, and in `music.txt`): the queue's calls in the C with their numbers, the pickups' table at
$10BD4, the ocarina's notes at $2B34, the messages' `SOUND` command (message 40's `{SOUND 45}`).
Five are not queued by the code anywhere found: the three ocarina tunes played whole, a low rumble
and two low notes (data could still name them; the tunes may be leftovers of a demonstration).

| Sound | Name | Where |
|---|---|---|
| $18 | ocarina note A (C5) | $2B08: the ocarina played, button A |
| $19 | ocarina note B (D5) | $2B08: button B |
| $1A | ocarina note C (G5) | $2B08: button C |
| $1B | ocarina tune 0, played whole (B A B A B C B) | not queued by the code |
| $1C | ocarina tune 1, played whole (A B C B C A B) | not queued by the code |
| $1D | ocarina tune 2, played whole (A C C A B A B) | not queued by the code |
| $1E | jump | $EC08: leaving the ground upwards |
| $1F | an enemy hit | $4986: an enemy hurt by the player; a darter hit ($17EE6) |
| $20 | sword swing | $ED5C: B swings the sword |
| $21 | a low rumble, fading over four | not queued by the code |
| $22 | a door closing | $BD52: the door's frame, closing |
| $23 | a door opening | $BD0A: the door's frame, opening |
| $24 | spirit magic | $1194C: a spirit sets off |
| $25 | cursor | a move in a menu or a message's choice ($8212, the title's menu $71C6, $13BC) |
| $26 | confirm | a choice made ($13BC, the title's menu $71C6) |
| $27 | gold dropped, rolling | $10BD4: gold that rolls away appears (pickup kinds $00-$0A) |
| $28 | gold or an item picked up | $F1EC, $F256 |
| $29 | a heart picked up | $F262 |
| $2A | a thud | a sinking step ($12160), a weight ($1234C), a townsperson struck ($1649C) |
| $2B | a hit spark | $10EC2: an attack's spark, when it hits |
| $2C | a chest opening | $A666 |
| $2D | a thing received | message 40: "Oh, it's the ...!" |
| $2E | quake magic | $11590: the world stops and the screen shakes |
| $2F | a quake's rock falling | $118BE |
| $30 | a hit blocked | the shield's spark ($11868); a hit that only pushes back ($E04E, bit 4) |
| $31 | up attack, or grabbing a ladder or vine | $EA2A, $EC26 |
| $32 | a thing shaking | $1221C |
| $33 | a spring | a spring ($12298), a bounce tile ($DFA0), a lift ($B0DA) |
| $34 | two low notes, alternating | not queued by the code |
| $35 | buzz: cannot | the status screen: a line that cannot be chosen ($75F8, $7882, $78DC, $7D9C) |
| $36 | the player hurt | $E12E |
| $37 | a shot | an enemy's shot ($15456, $18620); $DB36 |
| $38 | a room's message coming | $9228: the screen fades for a message |
| $39 | a crash: the screen shaken | $183D4; Gragg and Glagg appearing ($15A86); a landing ($17DB8) |
| $3A | thunder magic | $11170, kind 2 |
| $3B | a seeker setting off | handler 25 ($1A0B0) |
| $3C | leaving by an exit | $F76C; a beaten beast's rider thrown up ($18D74) |
| $3D | hearts refilling | $F288; a message's command ($15C8) |
| $3E | healing at the inn | $1BE6, $F2A6 |
| $3F | a puff of debris | $42EA, $19C34 |
| $40 | a picture appearing | $CF5A: room script 9 |
| $41 | a lift starting | $11FD2 |
| $42 | a lift stopping | $11FD2 |
| $43 | the companion on the player's head | $FC02: the companion, kind 0, state 10 |
| $44 | the player transformed | $CA66 |
| $45 | a beaten beast's rider | $18D74: the pair beaten |
| $46 | pushed back by a hit | $E04E, bit 6 |
| $47 | a splash | $DD7C: into or out of the water |
| $48 | gold appearing | $10BD4: pickup kinds $12-$16 |
| $49 | the companion turning | $FDAC: the companion, kind 0, state 6 |
| $4A | a jump (C), swimming up, climbing | $EB9E, $EC26 |
| $4B | a wall crumbling | $B400 |
| $4C | room objects let out | $C9B4: room script 0's release |
| $4D | shield magic | $11170, kinds 4, 6, 7 |
| $4E | a darter fired | handler 19 ($17EE6) |
| $4F | a message window opening | $279C |
| $50 | a diver diving | handler 24 ($19C86) |
| $51 | the ride | $D48C: room script 13 |
| $52 | a beaten boss's body | $19946 |
| $53 | the player's death | $E22E |
| $56 | the status screen opening | $901C: Start (the music paused first, $FC) |
| $57 | a lift opened | $2C4E: a puff and four sparks |

## MIDI

`tools/midi.py` writes every song (and with `--effects` every effect) as a standard MIDI file,
`assets/sound/midi/song_NN.mid` and `sound_NN.mid` (the build writes them with the pack). Each of
a song's tracks is played as the driver plays it, from `music.json` and `instruments.json`: the
lengths, loops, calls, notes that sound with the next, ties, the gate, rests, key offs, the
detune's whole notes, tempo changes; a song's loop is played twice (`--loops N`).

* Time: the driver steps its tracks every three ticks of timer B ($ED: 175.27 Hz), a song's track
  by what its tempo adds up over them (tempo / 256 a tick, $00C2), an effect's by 9; a length is
  its byte times 3. A MIDI tick is one of those units, 144 to a quarter note.
* Pitch: FM note n sounds at MIDI n + 12 plus its instrument's offset (most, -12); PSG note n at
  MIDI n - 24 + 48 (the period table starts at C3). Drum mode and the noise go to the drum channel.
* Volume: the attenuation tables in dB (an FM level's step 0.75 dB, a PSG one's 2 dB), as velocity.
* Not carried over: the chips' instruments (no General MIDI match: an FM track gets a lead, or low
  a synth bass, the PSG a square lead; each MIDI track is named for its FM instruments), slides,
  modulation, the detune's fraction, the pan.

Checked against the emulated chips (the sound log with `WBMW_SOUND_LOG_TIME=1`, which adds the
samples made to each write): the title's note starts, 69 of 73 in its first 12 seconds within
20 ms of a key-on (the rest drift by the ticks the driver misses while busy, 0.6%); its FM notes
and song $00's FM and PSG notes the same as the frequencies the driver wrote. `wbmw --play-sound N`
queues sound N (hex) through the game's own `QueueSound` ten frames into a run, to hear or log any
song or effect.

## The driver in C (in progress)

`port/sounddrv.c` is the driver written anew in C, to replace the emulated Z80. It keeps the
driver's state in the Z80's RAM at the original's addresses (its variables at $1B80-$1FFF: the
command ring at $1C30, the voices at $1C80, the tracks at $1EC0), so save states and the
original's RAM compare byte for byte, and reaches the chips and the bank window through `snd_io`.
It is written by hand from the reading above, not translated: a translation would be derived from
the ROM.

Until it is whole, the emulated Z80 runs the original and each C routine is checked against it
as it runs (`WBMW_Z80_SHADOW=1`, `harness/sound.c`): when the Z80 starts a routine of
`snd_routines`, the C runs on a copy of the RAM, its chip writes kept; when the Z80 has returned
(its stack pointer back above the return address), the driver's variables, the chip writes in
order, and the registers the routine answers in must be the same (bytes the 68000 wrote in the
meantime, through `bus_zram_marks`, are not compared). `tools/sndaccept.py` runs it from power-on,
from every save state and playing every song and effect (`--play-sound`), and adds up the
routines' calls and differences.

Done (batches 1-2): the YM2612's writes ($06C3-$06F7, the key $06E2), the records' numbers
($02E8, $0436), letting voices go ($0446, $02FA), stopping the song, a priority or all ($0321,
$0344, $0369), finding a voice ($0393, $040D), and the pitch ($0516, $04B1, $04E4): 19 routines,
over a million calls in 187 runs, none different. A mutation (the key's second half left out)
was caught at once. Batch 2b: a voice's pitch and volume written ($053C/$0579 with $05A3, $05BA,
the carriers $0667), an FM operator set from an instrument ($067C), the random numbers ($0B5B,
$0B66: times 117, plus $7F), and the start's silencing and reset ($00E3 with $0100/$0106 and the
`rst $10`/`rst $18` it uses, $013E): 27 routines, 3.6 million calls in 187 runs, none different
(routines only ever reached inside another, like $05A3 or the `rst`s, are checked as part of it).
Batch 3: the voices each tick ($070B): a voice's instrument set up at its first tick (FM $0A08,
PSG $0AB2, the noise $0AA9, the scripts $0B04), its time counted (the note ended at $8000, the
voice let go at 0, $0460), the two modulation scripts, a slide, the random spread, the PSG
envelope and its release ($0976), and the pitch and volume written: 34 routines, every tick of
187 runs (322,748 whole frames of the voices) the same. A slip in it (a word written a byte at a
time from a sum that read the word: `SET16` is a function for that reason) was found at once;
`WBMW_Z80_SHADOW_DUMP=1` prints the voice's record before and after, the Z80's and the C's.
Batch 4: the tracks ($0B7F, $0B9C: the data's notes, lengths, calls, loops and commands; a note's voice, $0CA5), the commands from the ring ($019C, the sounds started), the fade ($0155): every tick of the 187 runs the same (107,607 track steps, 322,799 commands and fades). `snd_init` and `snd_tick` (the start, and a tick of the main loop) are written, not yet run in place of the Z80. Next: the native build on the C driver. Formerly next: the track interpreter ($0B9C,
$0CA5, the commands), the main loop's parts (the commands from the 68000, the fades, the timer),
then the C driver in place of the Z80.

## Not done yet

* Names for the instruments' last unnamed bytes (an FM instrument's `level`'s use beyond the
  carriers, `b0_high`; a PSG instrument's `b0`-`b2`).
* A MIDI import back into `music.json`.
* The driver in C, finished (above), replacing the emulated Z80; checked against it by comparing every write to
  the sound chips.
