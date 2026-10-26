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
#define SET16(a, v) (RAM(a) = (uint8_t)(v), RAM((a) + 1) = (uint8_t)((v) >> 8))

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
static void voice_release(unsigned voice)
{
    unsigned n = voice_number(voice);
    if (n >= 6) RAM(YM_CHANNEL) = (uint8_t)(n - 6);
    else { RAM(YM_CHANNEL) = (uint8_t)(n >> 1); RAM(YM_BANK) = (uint8_t)((n & 1) << 1); }
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

const SndRoutine snd_routines[] = {
    {0x02E8, z_02e8, "TrackNumber", SR_A},
    {0x02FA, z_02fa, "TrackReleaseVoices", 0},
    {0x0321, z_0321, "SongStop", 0},
    {0x0344, z_0344, "PriorityStop", 0},
    {0x0369, z_0369, "AllStop", 0},
    {0x0393, z_0393, "VoiceFind", SR_ZF | SR_IY},
    {0x040D, z_040d, "VoiceOfNote", SR_CF | SR_IY},
    {0x0436, z_0436, "VoiceNumber", SR_A},
    {0x0446, z_0446, "VoiceRelease", 0},
    {0x04B1, z_04b1, "FmPitch", SR_D | SR_E},
    {0x04E4, z_04e4, "PsgPitch", SR_D | SR_E},
    {0x0516, z_0516, "NoteSplit", SR_C | SR_D | SR_E},
    {0x06C3, z_06c3, "YmWriteOperator", 0},
    {0x06C7, z_06c7, "YmWriteChannelA", 0},
    {0x06C8, z_06c8, "YmWriteChannel", 0},
    {0x06CB, z_06cb, "YmWriteHalfOr", 0},
    {0x06CD, z_06cd, "YmWriteHalf", 0},
    {0x06E2, z_06e2, "YmKey", 0},
    {0x06F7, z_06f7, "YmWrite", 0},
};
const int snd_routine_count = sizeof snd_routines / sizeof snd_routines[0];
