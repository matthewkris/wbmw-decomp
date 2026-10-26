/* The sound driver in C (port/sounddrv.h, docs/SOUND.md). Routines are named for what they do,
 * each with its Z80 address. They read and write the driver's variables in snd_io.ram at the
 * original's addresses, and the chips through snd_io; nothing waits (a write takes no time
 * here, so the original's loops on the YM2612's busy flag have nothing to wait for).
 *
 * The records: a voice (one of the chips' channels: 6 FM at $1C80, 3 PSG tones at $1E00, the
 * noise sharing the third's record, $40 bytes each) and a track (a part of a song or an effect:
 * 10 at $1EC0, $20 bytes each). */
#include "sounddrv.h"

SndIO snd_io;

#define RAM(a)      snd_io.ram[(a) & 0x1FFF]
#define RAM16(a)    (RAM(a) | RAM((a) + 1) << 8)
static void SET16(unsigned a, unsigned v)           /* (a function: v may read the word it replaces) */
{
    RAM(a) = (uint8_t)v;
    RAM(a + 1) = (uint8_t)(v >> 8);
}

#define YM_BANK     0x1C10                          /* the YM2612's half for this voice: 0 (channels 1-3) or 2 (4-6) */
#define YM_CHANNEL  0x1C11                          /* the channel within the half (0-2), or the PSG's (0-2) */
#define YM_OPERATOR 0x1C12                          /* an operator's offset: 0, 4, 8 or $C */
#define NOISE_LEVEL 0x1C13                          /* the noise voice's attenuation */
#define TRACK_NOW   0x1C07                          /* the track being stepped (its number) */
#define WANT_TYPE   0x1C20                          /* a voice wanted: its type (0 FM, 2 PSG, 4 noise), */
#define WANT_MASK   0x1C21                          /*   the voices of the type it may take, */
#define WANT_PRI    0x1C22                          /*   its priority */
#define BEST_LEFT   0x1C26                          /* the voice found so far: its time left (a word) */
#define SOUND_PRI   0x1C28                          /* the sound started: its priority */
#define FADE_A      0x1C2A
#define FADE_B      0x1C2B
#define FADE_STEPS  0x1C09
#define FADE_LEVEL  0x1C0B

#define VOICES      0x1C80                          /* 9 voices, $40 bytes each */
#define VOICE_N     9
#define TRACKS      0x1EC0                          /* 10 tracks, $20 bytes each */
#define TRACK_N     10
/* a voice's fields */
#define V_FLAGS     0                               /* bit 7 playing, 6 started, 5 a song's track's, 1 not keyed (PSG/noise) */
#define V_TYPE      1                               /* 0 FM, 2 PSG tone, 4 noise */
#define V_PRI       4                               /* the priority of the note playing */
#define V_TRACK     6                               /* the track playing on it (its number) */
#define V_NOTE      7                               /* the track's note count when it began */
#define V_PSG_LEVEL 41                              /* (+$29) a PSG voice's attenuation */
#define V_LEFT      60                              /* (+$3C) its time left, a word */
/* a track's fields */
#define T_FLAGS     0                               /* bit 7 playing */
#define T_PRI       16                              /* (+$10) its sound's priority; bit 7 a song's */
#define T_NOTES     18                              /* (+$12) notes begun */
#define T_NOTE_AT   19                              /* (+$13) the step's first note's count */

/* ---- the YM2612 ---- */

/* $06F7: register reg of the YM2612's first half set to value (its ports 0 and 1) */
static void ym_write(unsigned reg, unsigned value)
{
    snd_io.ym(0, reg);
    snd_io.ym(1, value);
}

/* $06CD: register reg of the current voice's half ($1C10) set to value */
static void ym_write_half(unsigned reg, unsigned value)
{
    unsigned half = RAM(YM_BANK);
    snd_io.ym(half, reg);
    snd_io.ym(half + 1, value);
}

/* $06C8: a channel register (reg + the channel, $1C11) of the current voice set to value */
static void ym_write_channel(unsigned reg, unsigned value) { ym_write_half(reg | RAM(YM_CHANNEL), value); }

/* $06C3: an operator register (reg + the operator, $1C12, + the channel) set to value */
static void ym_write_operator(unsigned reg, unsigned value) { ym_write_channel(reg | RAM(YM_OPERATOR), value); }

/* $06E2: the current voice's operators keyed on or off (ops: the four bits, high nibble), through
 * register $28: its channel number, 4-6 for the second half */
static void ym_key(unsigned ops)
{
    unsigned code = ops | RAM(YM_CHANNEL);
    if (RAM(YM_BANK) & 2) code |= 4;
    ym_write(0x28, code);
}

/* ---- records ---- */

/* $02E8: a track record's number */
static unsigned track_number(unsigned track) { return (track - TRACKS) >> 5 & 0xFF; }

/* $0436: a voice record's number */
static unsigned voice_number(unsigned voice) { return (voice - VOICES) >> 6 & 0xFF; }

/* $0446: voice let go: its chip told ($1C10/$1C11 set for it), its flags cleared. An FM voice:
 * its operators' release at the most ($80-$8C: $FF), keyed off unless bit 1 says it was not
 * keyed; a PSG tone: silent ($9F + its channel), its level kept at $78; the noise: silent ($FF) */
static void voice_silence(unsigned voice);
static void voice_release(unsigned voice)
{
    unsigned n = voice_number(voice);
    if (n >= 6) RAM(YM_CHANNEL) = (uint8_t)(n - 6);
    else { RAM(YM_CHANNEL) = (uint8_t)(n >> 1); RAM(YM_BANK) = (uint8_t)((n & 1) << 1); }
    voice_silence(voice);
}

/* $0460: the same, its chip already told (the voices' frame) */
static void voice_silence(unsigned voice)
{
    unsigned type = RAM(voice + V_TYPE);
    if (type < 2) {
        for (unsigned op = 0x80; op <= 0x8C; op += 4) ym_write_channel(op, 0xFF);
        if (!(RAM(voice + V_FLAGS) & 2)) ym_key(0);
    } else if (type == 2) {
        RAM(voice + V_PSG_LEVEL) = 0x78;
        snd_io.psg((unsigned)(RAM(YM_CHANNEL) << 5 | 0x9F) & 0xFF);
    } else {
        RAM(NOISE_LEVEL) = 0x78;
        snd_io.psg(0xFF);
    }
    RAM(voice + V_FLAGS) = 0;
}

/* $02FA: the voices playing track's notes let go */
static void track_release_voices(unsigned track)
{
    unsigned n = track_number(track);
    for (unsigned v = VOICES, k = 0; k < VOICE_N; k++, v += 0x40)
        if ((RAM(v + V_FLAGS) & 0x80) && RAM(v + V_TRACK) == n) voice_release(v);
}

/* $0321: the song's tracks stopped (their voices let go) */
static void song_stop(void)
{
    for (unsigned t = TRACKS, k = 0; k < TRACK_N; k++, t += 0x20)
        if ((RAM(t + T_FLAGS) & 0x80) && (RAM(t + T_PRI) & 0x80)) { track_release_voices(t); RAM(t + T_FLAGS) = 0; }
}

/* $0344: the tracks of the sound's priority ($1C28) stopped */
static void priority_stop(void)
{
    for (unsigned t = TRACKS, k = 0; k < TRACK_N; k++, t += 0x20)
        if ((RAM(t + T_FLAGS) & 0x80) && RAM(t + T_PRI) == RAM(SOUND_PRI)) { track_release_voices(t); RAM(t + T_FLAGS) = 0; }
}

/* $0369: everything stopped, the fade too */
static void all_stop(void)
{
    for (unsigned t = TRACKS, k = 0; k < TRACK_N; k++, t += 0x20)
        if (RAM(t + T_FLAGS) & 0x80) { track_release_voices(t); RAM(t + T_FLAGS) = 0; }
    RAM(FADE_A) = RAM(FADE_B) = 0;
    RAM(FADE_STEPS) = RAM(FADE_LEVEL) = 0;
}

/* $0393: a voice for a note: of the type wanted ($1C20), among the voices its mask allows ($1C21:
 * the table at $0404 gives each type's first voice and the mask of its voices), the first free;
 * else the one playing the lowest priority not above the note's ($1C22), and of those the one
 * with the most time left ($1C26 keeps it), which is let go. 1 and *voice if one was found; else
 * 0, and *voice past the last looked at. */
static int voice_find(unsigned *voice)
{
    static const struct { uint16_t first; uint8_t mask; } kinds[3] = {{0x1C80, 0x3F}, {0x1E00, 0x07}, {0x1E80, 0x01}};
    unsigned type = RAM(WANT_TYPE), k = (type + (type >> 1 | (type & 1) << 7)) & 0xFF;   /* (rrca: types 0, 2, 4) */
    unsigned v = kinds[k / 3].first, mask = RAM(WANT_MASK) & kinds[k / 3].mask, best_pri = 0x80, best = 0;
    for (; mask; mask >>= 1, v += 0x40) {
        if (!(mask & 1)) continue;
        if (!(RAM(v + V_FLAGS) & 0x80)) { *voice = v; return 1; }
        unsigned pri = RAM(v + V_PRI);
        if (RAM(WANT_PRI) < pri) continue;
        if (!(best_pri & 0x80)) {
            if (best_pri < pri) continue;
            if (best_pri == pri && RAM16(v + V_LEFT) <= RAM16(BEST_LEFT)) continue;
        }
        SET16(BEST_LEFT, RAM16(v + V_LEFT));
        best_pri = pri; best = v;
    }
    *voice = v;
    if (best_pri & 0x80) return 0;
    voice_release(best);
    *voice = best;
    return 1;
}

/* $040D: the voice playing track's note of this step (the voice's track the current one, $1C07,
 * and its note count the step's: +$12 less +$13), if one is */
static int voice_of_note(unsigned track, unsigned *voice)
{
    unsigned note = (RAM(track + T_NOTES) - RAM(track + T_NOTE_AT)) & 0xFF, v = VOICES;
    for (unsigned k = 0; k < VOICE_N; k++, v += 0x40)
        if ((RAM(v + V_FLAGS) & 0x80) && RAM(v + V_TRACK) == RAM(TRACK_NOW) && RAM(v + V_NOTE) == note) { *voice = v; return 1; }
    *voice = v;
    return 0;
}

/* ---- pitch ---- */

/* $0516: a note (whole notes) as a block (*block, 0-7) and a semitone (the result, 0-11); from
 * $60 up, the highest the table goes (block 7, semitone $0B and fraction $F8: *frac) */
static unsigned note_split(unsigned note, unsigned *block, unsigned *frac)
{
    if (note >= 0x60) { *block = 7; *frac = 0xF8; return 0x0B; }
    unsigned b = 0;
    if (note >= 0x30) { note -= 0x30; b |= 4; }
    if (note >= 0x18) { note -= 0x18; b |= 2; }
    if (note >= 0x0C) { note -= 0x0C; b += 1; }
    *block = b;
    return note;
}

/* $04B1 (the FM table, rising) and $04E4 (the PSG's, falling): the entry for a semitone and the
 * fraction's top 3 bits (8 steps a semitone: two bytes each), and a half and a quarter of the step
 * to the next entry added by the fraction's bits 4 and 3 (only the low bytes' difference: the
 * steps are under 256) */
static unsigned table_pitch(unsigned table, unsigned semi, unsigned frac, int falling)
{
    unsigned p = table + ((semi << 4 & 0xF0) | (frac >> 4 & 0x0E));
    unsigned lo = RAM(p), hi = RAM(p + 1), next = RAM(p + 2);
    unsigned step = ((falling ? lo - next : next - lo) & 0xFF) >> 1;
    if (frac & 0x10) { lo += step; if (lo > 0xFF) { lo &= 0xFF; hi = (hi + 1) & 0xFF; } }
    step >>= 1;
    if (frac & 0x08) { lo += step; if (lo > 0xFF) { lo &= 0xFF; hi = (hi + 1) & 0xFF; } }
    return hi << 8 | lo;
}

/* ---- a voice's pitch and volume on its chip ---- */
#define V_LEVEL     2                               /* (+$02) the note's velocity, as attenuation (FM) */
#define V_PITCH     8                               /* (+$08) the pitch now: fraction, then whole notes */
#define V_WRITTEN   10                              /* (+$0A) the pitch last written: fraction (to 1/8), whole notes */
#define V_VOL_MOD   28                              /* (+$1C) the volume script's offset */
#define V_ALGORITHM 36                              /* (+$24) the FM instrument's algorithm and feedback (register $B0) */
#define V_OPS       40                              /* (+$28) the 4 operators: their level, its last written, 2 more */
#define FADE_NOW    0x1C0B                          /* the fade's level, added to a song's voices */
#define FM_LEVEL    0x1C08                          /* the FM voice's level for its carriers, this time */
#define FM_TABLE    0x107B                          /* the FM frequency numbers */
#define PSG_TABLE   0x113D                          /* the PSG periods */
#define CARRIERS    0x0674                          /* each algorithm's carriers, a bit an operator */

/* $059B, $05A3: whether the voice's pitch (to an eighth of a semitone) differs from the one last
 * written; if so it is kept as written. *note and *frac: the pitch */
static int pitch_changed(unsigned voice, unsigned *note, unsigned *frac)
{
    *frac = RAM(voice + V_PITCH) & 0xF8; *note = RAM(voice + V_PITCH + 1);
    if (*frac == RAM(voice + V_WRITTEN) && *note == RAM(voice + V_WRITTEN + 1)) return 0;
    RAM(voice + V_WRITTEN) = (uint8_t)*frac; RAM(voice + V_WRITTEN + 1) = (uint8_t)*note;
    return 1;
}

/* $053C, $0579: the voice's pitch written, if it changed: an FM voice's block and frequency
 * number ($A4 + its channel, then $A0); a PSG voice's period, its octave's entry halved by the
 * block, in the PSG's two bytes ($80 + the channel and the low 4 bits, then the high 6) */
static void voice_pitch(unsigned voice)
{
    unsigned note, frac, block;
    if (!pitch_changed(voice, &note, &frac)) return;
    unsigned semi = note_split(note, &block, &frac);
    if (RAM(voice + V_TYPE) == 0) {
        unsigned reg = (0xA4 + RAM(YM_CHANNEL)) & 0xFF, f = table_pitch(FM_TABLE, semi, frac, 0);
        ym_write_half(reg, (block << 3 | f >> 8) & 0xFF);
        ym_write_half(reg & ~4u, f & 0xFF);
        return;
    }
    unsigned period = table_pitch(PSG_TABLE, semi, frac, 1) >> block;
    snd_io.psg((unsigned)(RAM(YM_CHANNEL) << 5 | (period & 0x0F) | 0x80) & 0xFF);
    snd_io.psg(period >> 4 & 0x3F);
}

/* $0667: the voice's FM algorithm's carriers (the table at $0674): a bit an operator */
static unsigned carriers(unsigned voice) { return CARRIERS + (RAM(voice + V_ALGORITHM) & 7); }

/* the voice's operator op (0-3, as its record keeps them)'s registers' offset: 0, 8, 4, $C */
static unsigned op_offset(unsigned op) { return (op >> 1 | (op & 1) << 1) << 2; }

/* add a and b as the original's signed bytes, at most $7F: the PSG's (a result with bit 7, $7F) */
static unsigned level_sum(unsigned a, unsigned b)
{
    unsigned s = (a + b) & 0xFF;
    return s & 0x80 ? 0x7F : s;
}

/* $05BA: the voice's volume written, if it changed: a PSG tone's attenuation ($90 + its channel),
 * the noise's ($F0); an FM voice's carriers' levels ($40 + the operator), each its own level and
 * the voice's (its velocity, its volume script's offset); a song's voices the fade's level too */
static void voice_volume(unsigned voice)
{
    unsigned type = RAM(voice + V_TYPE), fade = RAM(voice + V_FLAGS) & 0x20 ? RAM(FADE_NOW) : 0;
    if (type == 2) {
        unsigned a = level_sum(fade, RAM(voice + V_OPS)) & 0x78;
        if (a == RAM(voice + V_PSG_LEVEL)) return;
        RAM(voice + V_PSG_LEVEL) = (uint8_t)a;
        snd_io.psg(RAM(YM_CHANNEL) << 5 | 0x90 | a >> 3);
        return;
    }
    if (type > 2) {
        unsigned a = level_sum(fade, RAM(voice + V_OPS)) & 0x78;
        if (a == RAM(NOISE_LEVEL)) return;
        RAM(NOISE_LEVEL) = (uint8_t)a;
        snd_io.psg(0xF0 | a >> 3);
        return;
    }
    unsigned mask = RAM(carriers(voice)), c = (RAM(voice + V_LEVEL) + RAM(voice + V_VOL_MOD)) & 0xFF;
    int s = (int8_t)fade + (int8_t)c;               /* (add, then jp po: $7F on a signed overflow) */
    RAM(FM_LEVEL) = (uint8_t)(s < -128 || s > 127 ? 0x7F : s & 0xFF);
    for (unsigned op = 0; op < 4; op++, mask >>= 1) {
        unsigned at = voice + V_OPS + 4 * op;
        if (!(mask & 1)) continue;
        unsigned a = level_sum(RAM(FM_LEVEL), RAM(at));
        if (a == RAM(at + 1)) continue;
        RAM(at + 1) = (uint8_t)a;
        ym_write_channel(0x40 | op_offset(op), a);
    }
}

/* $067C: an FM operator set from an instrument's three bytes (src: its level, its detune and
 * multiple, its envelope's number), with the voice's copy (dst: 4 bytes: the level, the level
 * last written, the detune and multiple, the envelope) written only where they changed; its
 * release ($80) always. op: its registers' offset ($1C12). *src and *dst move on past them */
static void operator_set(unsigned op, unsigned *src, unsigned *dst)
{
    unsigned s = *src, d = *dst;
    RAM(YM_OPERATOR) = (uint8_t)op;
    RAM(d) = RAM(s);
    if (RAM(d + 2) != RAM(s + 1)) { RAM(d + 2) = RAM(s + 1); ym_write_operator(0x30, RAM(s + 1)); }
    unsigned env = RAM(s + 2), e = 0x164B + env * 4;
    if (RAM(d + 3) != env) {
        RAM(d + 3) = (uint8_t)env;
        ym_write_operator(0x50, RAM(e)); ym_write_operator(0x60, RAM(e + 1)); ym_write_operator(0x70, RAM(e + 2));
    }
    ym_write_operator(0x80, RAM(e + 3));
    *src = s + 3; *dst = d + 4;
}

/* $0B5B, $0B66: the voice's random number ($1BF7 + the voice's number, $1BF6) stepped: times
 * 117, and $7F added */
#define VOICE_NOW   0x1BF6
static unsigned random_at(void) { return 0x1BF7 + RAM(VOICE_NOW); }
static unsigned random_step(void)
{
    unsigned at = random_at(), v = (RAM(at) * 117 + 0x7F) & 0xFF;
    RAM(at) = (uint8_t)v;
    return v;
}

/* ---- the voices each tick ---- */
#define SONG_TICKS  0x1C17                          /* the song's steps this tick (its tempo's whole part) */
#define V_INSTRUMENT 3                              /* the instrument's number */
#define V_KEY_OPS   5                               /* FM: the operators keyed on (register $28's high nibble) */
#define V_BASE      12                              /* (+$0C) the note's pitch, a word (whole notes in the high byte) */
#define A_COUNT     14                              /* the pitch script: the ticks left of its step (0 stopped), */
#define A_LOOPS     15                              /*   its loop's count, */
#define A_AT        16                              /*   where it is (a word), */
#define A_LOOP_AT   18                              /*   where its loop starts (a word), */
#define A_VALUE     20                              /*   the pitch it adds (a word), */
#define A_DELTA     22                              /*   the step added to that (a word) */
#define V_SLIDE_TO  24                              /* (+$18) a slide's last pitch (a word) */
#define V_SLIDE_BY  26                              /* (+$1A) a slide's step a tick (a word) */
#define B_DELTA     29                              /* the volume script: the step added to its offset (+$1C), */
#define B_COUNT     30                              /*   the ticks left of its step, */
#define B_LOOPS     31                              /*   its loop's count, */
#define B_AT        32                              /*   where it is, */
#define B_LOOP_AT   34                              /*   where its loop starts */
#define V_PAN       37                              /* (+$25) FM: register $B4 as written */
#define V_OFFSET    38                              /* (+$26) the instrument's pitch offset (a word) */
#define V_TONE      40                              /* (+$28) PSG: the level to write, the envelope's and the script's */
#define E_AT        42                              /* the PSG envelope: where it is (a word), */
#define E_COUNT     44                              /*   the ticks left of its step, */
#define E_RATE      45                              /*   the ticks a release step lasts, */
#define E_RELEASE   46                              /*   a release step's change, */
#define E_SUSTAIN   47                              /*   the release's last level (0: to silence), */
#define E_LEVEL     48                              /*   its level now */
#define B_RATE      56                              /* the volume script's ticks a step, and the count */
#define B_TICK      57
#define A_RATE      58                              /* the pitch script's */
#define A_TICK      59
#define V_PANS      62                              /* (+$3E) the track's pan (bits 6, 7) */
#define A_RANDOM    63                              /* (+$3F) the pitch script's random spread (0 none) */
#define FM_INSTRUMENTS  0x1281                      /* 19 bytes each */
#define PSG_INSTRUMENTS 0x1828                      /* 6 bytes each */
#define PSG_ENVELOPES   0x188F                      /* pointers */
#define PITCH_SCRIPTS   0x1901                      /* pointers */
#define VOLUME_SCRIPTS  0x19D8                      /* pointers */

/* $07A2-$086C: the pitch script each tick, at its rate: the step added to the value; at the end of
 * a step, its items read on (docs/SOUND.md, `mod_a`): a step ($00-$F7: its ticks, then the step,
 * a word), a goto ($FF), a loop's end and start ($FE, $FD), the step reversed ($FC), the value set
 * ($FB), a stop ($FA), the rate ($F9), a random spread ($F8); 0 ends it */
static void pitch_script(unsigned v)
{
    if (--RAM(v + A_TICK)) return;
    RAM(v + A_TICK) = RAM(v + A_RATE);
    SET16(v + A_VALUE, RAM16(v + A_VALUE) + RAM16(v + A_DELTA));
    if (!RAM(v + A_COUNT) || --RAM(v + A_COUNT)) return;
    RAM(v + A_RANDOM) = 0;
    unsigned at = RAM16(v + A_AT), a, count;
    for (;;) {
        a = RAM(at); at++;
        if (a == 0) return;
        if (a < 0xF8) { SET16(v + A_DELTA, RAM16(at)); at += 2; count = a; break; }
        if (a == 0xFF) { at = RAM16(at); continue; }
        if (a == 0xFE) { if (--RAM(v + A_LOOPS)) at = RAM16(v + A_LOOP_AT); continue; }
        if (a == 0xFD) { RAM(v + A_LOOPS) = RAM(at); at++; SET16(v + A_LOOP_AT, at); continue; }
        if (a == 0xFC) { SET16(v + A_DELTA, -RAM16(v + A_DELTA)); count = RAM(at); at++; break; }
        if (a == 0xFB) { SET16(v + A_VALUE, RAM16(at)); at += 2; continue; }
        if (a == 0xFA) { SET16(v + A_DELTA, 0); RAM(v + A_COUNT) = 0; return; }
        if (a == 0xF9) { RAM(v + A_TICK) = RAM(v + A_RATE) = RAM(at); at++; continue; }
        SET16(v + A_DELTA, 0); RAM(v + A_RANDOM) = RAM(at); count = RAM(at + 1); at += 2;   /* $F8 */
        break;
    }
    RAM(v + A_COUNT) = (uint8_t)count;
    SET16(v + A_AT, at);
}

/* $086D-$08CC: the voice's pitch this tick: its note's (moved by a slide, which stops at its
 * end), the pitch script's value (or with a random spread, a random number shifted up by it),
 * the instrument's offset */
static void pitch_now(unsigned v)
{
    unsigned p = RAM16(v + V_BASE), add;
    if (RAM(v + V_FLAGS) & 0x10) {
        unsigned by = RAM16(v + V_SLIDE_BY), to = RAM16(v + V_SLIDE_TO);
        p = (p + by) & 0xFFFF;
        if (p == to || (by & 0x8000 ? p < to : p > to)) { p = to; RAM(v + V_FLAGS) &= ~0x10; }
        SET16(v + V_BASE, p);
    }
    if (RAM(v + A_RANDOM)) {
        unsigned n = RAM(v + A_RANDOM), r = random_step();
        add = r & 0x80 ? 0xFF00 | r : r;
        add = n < 16 ? add << n & 0xFFFF : 0;
    } else add = RAM16(v + A_VALUE);
    SET16(v + V_PITCH, p + add + RAM16(v + V_OFFSET));
}

/* $08CD-$0961: the volume script each tick, at its rate: the step added to the offset; at the end
 * of a step its items read on (`mod_b`): a step ($00-$F8: its ticks, then the step), a goto
 * ($FF), a loop's end and start ($FE, $FD), the step reversed ($FC), the offset set ($FB), a stop
 * ($FA), the rate ($F9); 0 ends it */
static void volume_script(unsigned v)
{
    if (--RAM(v + B_TICK)) return;
    RAM(v + B_TICK) = RAM(v + B_RATE);
    RAM(v + V_VOL_MOD) += RAM(v + B_DELTA);
    if (!RAM(v + B_COUNT) || --RAM(v + B_COUNT)) return;
    unsigned at = RAM16(v + B_AT), a, count;
    for (;;) {
        a = RAM(at); at++;
        if (a == 0) return;
        if (a < 0xF9) { RAM(v + B_DELTA) = RAM(at); at++; count = a; break; }
        if (a == 0xFF) { at = RAM16(at); continue; }
        if (a == 0xFE) { if (--RAM(v + B_LOOPS)) at = RAM16(v + B_LOOP_AT); continue; }
        if (a == 0xFD) { RAM(v + B_LOOPS) = RAM(at); at++; SET16(v + B_LOOP_AT, at); continue; }
        if (a == 0xFC) { RAM(v + B_DELTA) = (uint8_t)-RAM(v + B_DELTA); count = RAM(at); at++; break; }
        if (a == 0xFB) { RAM(v + V_VOL_MOD) = RAM(at); at++; continue; }
        if (a == 0xFA) { RAM(v + B_DELTA) = RAM(v + B_COUNT) = 0; return; }
        RAM(v + B_TICK) = RAM(v + B_RATE) = RAM(at); at++;                                  /* $F9 */
    }
    RAM(v + B_COUNT) = (uint8_t)count;
    SET16(v + B_AT, at);
}

/* $0976: a PSG voice's envelope each tick: its steps (a level, its ticks) until the note ends;
 * then the release, a step each E_RATE ticks, down to its last level (held there) or to silence
 * (the voice let go, as at the envelope's $F0); its level and the volume script's to V_TONE */
static void tone_envelope(unsigned v)
{
    unsigned f = RAM(v + V_FLAGS), level;
    if (f & 2) {
        if ((f & 4) || --RAM(v + E_COUNT)) goto done;
        goto release;
    }
    if (f & 1) {
        RAM(v + V_FLAGS) |= 2;
        unsigned s = RAM(v + E_SUSTAIN);
        if (s && s < RAM(v + E_LEVEL)) { RAM(v + V_FLAGS) |= 4; goto done; }
        goto release;
    }
    if (!RAM(v + E_COUNT) || --RAM(v + E_COUNT)) goto done;
    {
        unsigned at = RAM16(v + E_AT), a = RAM(at);
        if (a == 0xF0) { voice_silence(v); return; }
        RAM(v + E_COUNT) = a & 0x0F;
        SET16(v + E_AT, at + 1);
        level = level_sum(a >> 1 & 0x78, RAM(v + V_LEVEL));
        goto set;
    }
release:
    level = (RAM(v + E_LEVEL) + RAM(v + E_RELEASE)) & 0xFF;
    if (!RAM(v + E_SUSTAIN)) { if (level & 0x80) { voice_silence(v); return; } }
    else if ((level & 0x80) || level >= RAM(v + E_SUSTAIN)) { level = RAM(v + E_SUSTAIN); RAM(v + V_FLAGS) |= 4; }
    RAM(v + E_COUNT) = RAM(v + E_RATE);
set:
    RAM(v + E_LEVEL) = (uint8_t)level;
done:
    RAM(v + V_TONE) = (uint8_t)(RAM(v + E_LEVEL) + RAM(v + V_VOL_MOD));
}

/* $0B04: the instrument's two scripts (their numbers at src) started, the voice's random seed $32 */
static void scripts_set(unsigned v, unsigned src)
{
    SET16(v + A_AT, RAM16(PITCH_SCRIPTS + RAM(src) * 2));
    SET16(v + B_AT, RAM16(VOLUME_SCRIPTS + RAM(src + 1) * 2));
    SET16(v + A_VALUE, 0); SET16(v + A_DELTA, 0);
    RAM(v + V_VOL_MOD) = RAM(v + B_DELTA) = RAM(v + A_RANDOM) = 0;
    RAM(v + A_COUNT) = RAM(v + A_RATE) = RAM(v + A_TICK) = RAM(v + B_COUNT) = RAM(v + B_RATE) = RAM(v + B_TICK) = 1;
    RAM(random_at()) = 0x32;
}

/* $0A08: an FM voice's instrument set up (the table at $1281): its algorithm ($B0) and pan and LFO
 * ($B4, the track's pan in place of the instrument's when it has one) where they changed, the four
 * operators, the pitch offset, the scripts, the modulators' levels where they changed (the
 * carriers' are the volume's, $05BA); then keyed on */
static void fm_load(unsigned v)
{
    unsigned at = FM_INSTRUMENTS + RAM(v + V_INSTRUMENT) * 19, a = RAM(at++);
    if (a != RAM(v + V_ALGORITHM)) { RAM(v + V_ALGORITHM) = (uint8_t)a; ym_write_channel(0xB0, a); }
    RAM(v + V_KEY_OPS) = RAM(at++);
    unsigned pan = RAM(v + V_PANS) & 0xC0;
    a = (RAM(at++) & ~(pan ? 0xC0u : 0u)) | pan;
    if (a != RAM(v + V_PAN)) { RAM(v + V_PAN) = (uint8_t)a; ym_write_channel(0xB4, a); }
    unsigned dst = v + V_OPS;
    for (unsigned op = 0; op < 4; op++) operator_set(op_offset(op), &at, &dst);
    RAM(v + V_OFFSET) = RAM(at); RAM(v + V_OFFSET + 1) = RAM(at + 1);
    scripts_set(v, at + 2);
    unsigned mask = RAM(carriers(v));
    for (unsigned op = 0; op < 4; op++, mask >>= 1) {
        unsigned o = v + V_OPS + 4 * op;
        if (mask & 1 || RAM(o) == RAM(o + 1)) continue;
        RAM(o + 1) = RAM(o);
        ym_write_channel(0x40 | op_offset(op), RAM(o));
    }
    ym_key(RAM(v + V_KEY_OPS));
}

/* $0AB2: a PSG voice's instrument set up (the table at $1828): its release (last level, rate,
 * step), its envelope started, the pitch offset two octaves down, the scripts */
static void tone_load(unsigned v)
{
    unsigned at = PSG_INSTRUMENTS + RAM(v + V_INSTRUMENT) * 6;
    RAM(v + E_SUSTAIN) = RAM(at); RAM(v + E_RATE) = RAM(at + 1); RAM(v + E_RELEASE) = RAM(at + 2);
    unsigned env = RAM16(PSG_ENVELOPES + RAM(at + 3) * 2), a = RAM(env);
    SET16(v + E_AT, env + 1);
    RAM(v + E_COUNT) = a & 0x0F;
    RAM(v + E_LEVEL) = (uint8_t)level_sum(a >> 1 & 0x78, RAM(v + V_LEVEL));
    SET16(v + V_OFFSET, 0xE800);
    scripts_set(v, at + 4);
}

/* $0AA9: the noise's: the third tone silenced first (its level $78 as written, $DF) */
static void noise_load(unsigned v)
{
    RAM(v + V_PSG_LEVEL) = 0x78;
    snd_io.psg(0xDF);
    tone_load(v);
}

/* $0756: a playing voice's tick: its instrument set up at its first; its time counted (a song's
 * by the song's steps, an effect's by one), the note ended at $8000 (an FM voice keyed off, and
 * nothing more this tick) and the voice let go at 0; the scripts, the pitch, a PSG voice's
 * envelope; the pitch and the volume written */
static void voice_tick(unsigned v)
{
    if (!(RAM(v + V_FLAGS) & 0x40)) {
        RAM(v + V_FLAGS) |= 0x40;
        unsigned t = RAM(v + V_TYPE);
        if (t < 2) fm_load(v); else if (t == 2) tone_load(v); else noise_load(v);
    }
    unsigned lo = RAM(v + V_LEFT) + (RAM(v + V_FLAGS) & 0x20 ? RAM(SONG_TICKS) : 1);
    RAM(v + V_LEFT) = (uint8_t)lo;
    if (lo > 0xFF) {
        unsigned hi = (RAM(v + V_LEFT + 1) + 1) & 0xFF;
        RAM(v + V_LEFT + 1) = (uint8_t)hi;
        if (hi == 0) { voice_silence(v); return; }
        if (hi == 0x80) {
            RAM(v + V_FLAGS) |= 1;
            if (RAM(v + V_TYPE) == 0) { ym_key(0); return; }
        }
    }
    pitch_script(v);
    pitch_now(v);
    volume_script(v);
    if (RAM(v + V_TYPE)) {
        tone_envelope(v);
        if (!(RAM(v + V_FLAGS) & 0x80)) return;
    }
    voice_pitch(v);
    voice_volume(v);
}

/* $070B: every playing voice's tick, in their order (the FM channels' halves in turn, then the
 * PSG's), the chips' channel ($1C10/$1C11) and the voice's number ($1BF6) set for each */
static void voices_tick(void)
{
    unsigned v = VOICES;
    RAM(VOICE_NOW) = 0;
    for (unsigned ch = 0; ch < 3; ch = RAM(YM_CHANNEL) + 1u) {
        RAM(YM_CHANNEL) = (uint8_t)ch;
        unsigned half = 0;
        do {
            RAM(YM_BANK) = (uint8_t)half;
            if (RAM(v + V_FLAGS) & 0x80) voice_tick(v);
            v += 0x40; RAM(VOICE_NOW)++;
            half = RAM(YM_BANK) ^ 2;
        } while (half);
    }
    for (unsigned ch = 0; ch < 3; ch = RAM(YM_CHANNEL) + 1u) {
        RAM(YM_CHANNEL) = (uint8_t)ch;
        if (RAM(v + V_FLAGS) & 0x80) voice_tick(v);
        v += 0x40; RAM(VOICE_NOW)++;
    }
}

/* ---- the start ---- */

/* rst $18, rst $10: register reg (+ *op) of the current half set to value, for the 3 channels
 * of an operator (*op then 4 on) */
static void ym_write_3(unsigned reg, unsigned *op, unsigned value)
{
    for (int k = 0; k < 3; k++) { ym_write_half(reg | *op, value); *op = (*op + 1) & 0xFF; }
    *op = (*op + 1) & 0xFF;
}

/* $0106, $0100: register reg of every operator of the half (and of both), set to value */
static void ym_write_all_ops(unsigned half, unsigned reg, unsigned value)
{
    unsigned op = 0;
    RAM(YM_BANK) = (uint8_t)half;
    for (int k = 0; k < 4; k++) ym_write_3(reg, &op, value);
}
static void ym_write_both(unsigned reg, unsigned value) { ym_write_all_ops(0, reg, value); ym_write_all_ops(2, reg, value); }

/* $00E3: the YM2612 quietened: the SSG envelopes off ($90), the releases at the most ($80: $FF),
 * the six channels keyed off */
static void ym_silence(void)
{
    ym_write_both(0x90, 0);
    ym_write_both(0x80, 0xFF);
    for (unsigned ch = 0; ch < 3; ch++) ym_write(0x28, ch);
    for (unsigned ch = 4; ch < 7; ch++) ym_write(0x28, ch);
}

/* $013E: everything stopped, and the voices' types set: six FM, three PSG */
static void voices_reset(void)
{
    all_stop();
    for (unsigned k = 0; k < VOICE_N; k++) RAM(VOICES + 0x40 * k + V_TYPE) = k < 6 ? 0 : 2;
}

/* ---- the routines' Z80 entries, for the shadow check: each adapter takes the routine's
 * arguments from the registers the original takes them in, and leaves its results where the
 * original does ---- */
#define ZF(r, on)  ((r)->f = (uint8_t)(((r)->f & ~0x40) | ((on) ? 0x40 : 0)))
#define CF(r, on)  ((r)->f = (uint8_t)(((r)->f & ~0x01) | ((on) ? 0x01 : 0)))
static void z_06f7(SndRegs *r) { ym_write(r->b, r->c); }
static void z_06cd(SndRegs *r) { ym_write_half(r->b, r->c); }
static void z_06cb(SndRegs *r) { ym_write_half(r->a | r->b, r->c); }    /* (b or a, then the half) */
static void z_06c8(SndRegs *r) { ym_write_channel(r->b, r->c); }
static void z_06c7(SndRegs *r) { ym_write_channel(r->a, r->c); }       /* (b from a) */
static void z_06c3(SndRegs *r) { ym_write_operator(r->b, r->c); }
static void z_06e2(SndRegs *r) { ym_key(r->a); }
static void z_02e8(SndRegs *r) { r->a = (uint8_t)track_number(r->ix); }
static void z_0436(SndRegs *r) { r->a = (uint8_t)voice_number(r->ix); }
static void z_0446(SndRegs *r) { voice_release(r->ix); }
static void z_02fa(SndRegs *r) { track_release_voices(r->ix); }
static void z_0321(SndRegs *r) { (void)r; song_stop(); }
static void z_0344(SndRegs *r) { (void)r; priority_stop(); }
static void z_0369(SndRegs *r) { (void)r; all_stop(); }
static void z_0393(SndRegs *r) { unsigned v; ZF(r, voice_find(&v)); r->iy = (uint16_t)v; }
static void z_040d(SndRegs *r) { unsigned v; CF(r, !voice_of_note(r->ix, &v)); r->iy = (uint16_t)v; }
static void z_0516(SndRegs *r)
{
    unsigned block, frac = r->e, semi = note_split(r->d, &block, &frac);
    r->c = (uint8_t)block; r->d = (uint8_t)semi; r->e = (uint8_t)frac;
}
static void z_04b1(SndRegs *r) { unsigned f = table_pitch((unsigned)(r->h << 8 | r->l), r->d, r->e, 0); r->d = (uint8_t)(f >> 8); r->e = (uint8_t)f; }
static void z_04e4(SndRegs *r) { unsigned f = table_pitch((unsigned)(r->h << 8 | r->l), r->d, r->e, 1); r->d = (uint8_t)(f >> 8); r->e = (uint8_t)f; }
#define HL(r, v)   ((r)->h = (uint8_t)((v) >> 8), (r)->l = (uint8_t)(v))
#define DE(r, v)   ((r)->d = (uint8_t)((v) >> 8), (r)->e = (uint8_t)(v))
static void z_053c(SndRegs *r) { voice_pitch(r->ix); }
static void z_05ba(SndRegs *r) { voice_volume(r->ix); }
static void z_0667(SndRegs *r) { HL(r, carriers(r->ix)); }
static void z_067c(SndRegs *r)
{
    unsigned s = (unsigned)(r->h << 8 | r->l), d = (unsigned)(r->d << 8 | r->e);
    operator_set(r->a, &s, &d);
    HL(r, s); DE(r, d);
}
static void z_0b5b(SndRegs *r) { HL(r, random_at()); }
static void z_0b66(SndRegs *r) { HL(r, random_at()); r->a = (uint8_t)random_step(); }
static void z_0460(SndRegs *r) { voice_silence(r->ix); }
static void z_070b(SndRegs *r) { (void)r; voices_tick(); }
static void z_0756(SndRegs *r) { voice_tick(r->ix); }
static void z_0976(SndRegs *r) { tone_envelope(r->ix); }
static void z_0a08(SndRegs *r) { fm_load(r->ix); }
static void z_0aa9(SndRegs *r) { noise_load(r->ix); }
static void z_0ab2(SndRegs *r) { tone_load(r->ix); }
static void z_0b04(SndRegs *r) { scripts_set(r->ix, (unsigned)(r->h << 8 | r->l)); }
static void z_00e3(SndRegs *r) { (void)r; ym_silence(); }
static void z_013e(SndRegs *r) { (void)r; voices_reset(); }

const SndRoutine snd_routines[] = {
    {0x00E3, z_00e3, "YmSilence", 0},
    {0x013E, z_013e, "VoicesReset", 0},
    {0x02E8, z_02e8, "TrackNumber", SR_A},
    {0x02FA, z_02fa, "TrackReleaseVoices", 0},
    {0x0321, z_0321, "SongStop", 0},
    {0x0344, z_0344, "PriorityStop", 0},
    {0x0369, z_0369, "AllStop", 0},
    {0x0393, z_0393, "VoiceFind", SR_ZF | SR_IY},
    {0x040D, z_040d, "VoiceOfNote", SR_CF | SR_IY},
    {0x0436, z_0436, "VoiceNumber", SR_A},
    {0x0446, z_0446, "VoiceRelease", 0},
    {0x0460, z_0460, "VoiceSilence", 0},
    {0x04B1, z_04b1, "FmPitch", SR_D | SR_E},
    {0x04E4, z_04e4, "PsgPitch", SR_D | SR_E},
    {0x0516, z_0516, "NoteSplit", SR_C | SR_D | SR_E},
    {0x053C, z_053c, "VoicePitch", 0},
    {0x05BA, z_05ba, "VoiceVolume", 0},
    {0x0667, z_0667, "Carriers", SR_H | SR_L},
    {0x067C, z_067c, "OperatorSet", SR_H | SR_L | SR_D | SR_E},
    {0x06C3, z_06c3, "YmWriteOperator", 0},
    {0x06C7, z_06c7, "YmWriteChannelA", 0},
    {0x06C8, z_06c8, "YmWriteChannel", 0},
    {0x06CB, z_06cb, "YmWriteHalfOr", 0},
    {0x06CD, z_06cd, "YmWriteHalf", 0},
    {0x06E2, z_06e2, "YmKey", 0},
    {0x06F7, z_06f7, "YmWrite", 0},
    {0x070B, z_070b, "VoicesTick", 0},
    {0x0756, z_0756, "VoiceTick", 0},
    {0x0976, z_0976, "ToneEnvelope", 0},
    {0x0A08, z_0a08, "FmLoad", 0},
    {0x0AA9, z_0aa9, "NoiseLoad", 0},
    {0x0AB2, z_0ab2, "ToneLoad", 0},
    {0x0B04, z_0b04, "ScriptsSet", 0},
    {0x0B5B, z_0b5b, "RandomAt", SR_H | SR_L},
    {0x0B66, z_0b66, "Random", SR_A | SR_H | SR_L},
};
const int snd_routine_count = sizeof snd_routines / sizeof snd_routines[0];
