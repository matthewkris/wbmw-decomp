/* The sound driver in C: the work of the game's Z80 driver (ROM $98000, docs/SOUND.md), written
 * anew. Its state is the Z80's 8 KB of RAM, at the original's addresses (the driver's variables
 * at $1B80-$1FFF: the command ring at $1C30, the voices at $1C80, the tracks at $1EC0), so a save
 * state and the original's RAM compare byte for byte. The sound chips and the bank window are
 * reached through snd_io, which the host sets (harness/sound.c).
 *
 * Until every routine is C, the emulated Z80 still runs the original, and harness/sound.c can
 * check each routine of snd_routines against it as it runs (WBMW_Z80_SHADOW=1): the C runs on a
 * copy of the RAM when the Z80 enters the routine, and when the Z80 returns, the variables and
 * the chip writes, in order, must be the same. */
#ifndef SOUNDDRV_H
#define SOUNDDRV_H
#include <stdint.h>

typedef struct {
    uint8_t *ram;                                   /* the Z80's RAM ($0000-$1FFF) */
    void (*ym)(unsigned port, unsigned data);       /* a write to the YM2612: port 0-3 ($4000-$4003) */
    void (*psg)(unsigned data);                     /* a write to the SN76489 ($7F11) */
    uint8_t (*window)(uint16_t a);                  /* a read through the bank window ($8000-$FFFF) */
} SndIO;
extern SndIO snd_io;

/* the Z80's registers at a routine's entry, for the shadow check's adapters; an adapter leaves
 * the routine's results in them, and `outs` says which the original's must equal at its return */
typedef struct { uint8_t a, f, b, c, d, e, h, l; uint16_t ix, iy, sp; } SndRegs;
enum { SR_A = 1, SR_B = 2, SR_C = 4, SR_D = 8, SR_E = 16, SR_H = 32, SR_L = 64, SR_IX = 128, SR_IY = 256,
       SR_ZF = 512, SR_CF = 1024 };                 /* SR_ZF, SR_CF: the zero and carry flags */
typedef struct { uint16_t addr; void (*fn)(SndRegs *); const char *name; unsigned outs; } SndRoutine;
extern const SndRoutine snd_routines[];
extern const int snd_routine_count;

#endif
