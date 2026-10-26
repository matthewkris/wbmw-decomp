/* Sound: the Z80 running the game's own sound driver, the YM2612 (FM, harness/ym2612.cpp on ymfm)
 * and the SN76489 (tone and noise, here). Shared by the harness and the native build.
 *
 * The 68000 never touches the sound chips: it uploads the driver to Z80 RAM at boot and queues
 * one-byte commands there (docs/NOTES.md, Sound). The Z80 (third_party/chips/z80.h) runs the
 * driver from its 8 KB of RAM (zram, shared with bus.c). Its memory map:
 *   $0000-$3FFF  RAM (8 KB, mirrored)        $4000-$5FFF  YM2612 (4 ports, mirrored)
 *   $6000-$60FF  bank register (9 bits, written one bit at a time)
 *   $7F11        SN76489                     $8000-$FFFF  a 32 KB window on 68000 memory
 * Timing: the Mega Drive's master clock is divided by 7 for the 68000 and the YM2612, by 15 for the
 * Z80 and the SN76489. One YM2612 sample is 144 of its clocks = 67.2 Z80 cycles; the sound runs
 * one scanline (228 Z80 cycles) at a time, producing stereo samples at 53,267 Hz. VBlank raises the
 * Z80's interrupt for one line. While the 68000 holds the Z80's bus or reset, the Z80 stops. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "md.h"
#define CHIPS_IMPL
#include "../third_party/chips/z80.h"

void ym_reset(void);
void ym_write(unsigned port, unsigned data);
unsigned ym_read(unsigned port);
void ym_generate(int32_t out[2], const int *gain);

/* the volume: a gain for each channel (the YM2612's six, then the SN76489's three tones and
 * noise) and for the mix, 256 = as the chips give it (the default: the output is then the
 * original's, sample for sample). The host sets them (native/wbmw.c: the menu's volumes, the
 * music's voices and the effects' told apart) */
int sound_gain[10] = {256, 256, 256, 256, 256, 256, 256, 256, 256, 256}, sound_master = 256;
void ym_state_io(FILE *f, int saving);

#define Z80_CYCLES_PER_LINE 228
#define YM_PER_Z80 15                 /* per Z80 cycle, in units where one sample is 1008 */
#define YM_SAMPLE 1008                /* 144 YM clocks * 7 */

static z80_t z80;
static uint64_t pins;
static int busreq, reset_line = 1;    /* 68000 side: holding the bus, holding reset (reset on power-on) */
static int int_line;                  /* VBlank interrupt to the Z80 */
static uint32_t bank;                 /* 9 bits: bits 15-23 of the 68000 address of the window */
static uint32_t acc;                  /* time to the next YM sample */
static int held;                      /* the harness holds sound still during a shadow check */
static int owed, owed_vint;           /* scanlines skipped while held, run when released */

/* ---- SN76489: three square waves and a noise channel, 16 internal clocks per step ---- */
static struct {
    uint16_t period[4], counter[4]; uint8_t vol[4], out[4], latch; uint16_t lfsr; uint32_t acc;
} psg;
static const int16_t PSG_VOL[16] = { 4096, 3254, 2584, 2053, 1631, 1295, 1029, 817, 649, 516, 410, 325, 258, 205, 163, 0 };

static void psg_reset(void)
{
    memset(&psg, 0, sizeof psg);
    for (int i = 0; i < 4; i++) psg.vol[i] = 15;
    psg.lfsr = 0x8000;
}

static void psg_write(uint8_t d)
{
    if (d & 0x80) {                                     /* latch: channel, type, low 4 bits */
        psg.latch = (d >> 4) & 7;
        int ch = psg.latch >> 1;
        if (psg.latch & 1) psg.vol[ch] = d & 15;
        else if (ch == 3) { psg.period[3] = d & 7; psg.lfsr = 0x8000; }
        else psg.period[ch] = (psg.period[ch] & 0x3F0) | (d & 15);
    } else {                                            /* data: the high 6 bits of a tone period */
        int ch = psg.latch >> 1;
        if (psg.latch & 1) psg.vol[ch] = d & 15;
        else if (ch < 3) psg.period[ch] = (psg.period[ch] & 15) | (d & 0x3F) << 4;
    }
}

/* advance by one step of the chip (16 of its input clocks) */
static void psg_step(void)
{
    for (int ch = 0; ch < 3; ch++) {
        if (psg.counter[ch]) psg.counter[ch]--;
        if (!psg.counter[ch]) { psg.counter[ch] = psg.period[ch] ? psg.period[ch] : 1; psg.out[ch] ^= 1; }
    }
    if (psg.counter[3]) psg.counter[3]--;
    if (!psg.counter[3]) {
        int rate = psg.period[3] & 3;
        psg.counter[3] = rate == 3 ? (psg.period[2] ? psg.period[2] : 1) * 2 : 0x10 << rate;
        /* the Mega Drive's 16-bit register: white noise taps bits 0 and 3 */
        int fb = psg.period[3] & 4 ? ((psg.lfsr & 1) ^ ((psg.lfsr >> 3) & 1)) : (psg.lfsr & 1);
        psg.lfsr = (uint16_t)(psg.lfsr >> 1 | fb << 15);
        psg.out[3] = psg.lfsr & 1;
    }
}

static int32_t psg_sample(void)
{
    int32_t s = 0;
    for (int ch = 0; ch < 4; ch++) {
        if (!(ch == 3 ? psg.out[3] : psg.out[ch] || psg.period[ch] <= 1)) continue;
        int32_t v = PSG_VOL[psg.vol[ch]], g = sound_gain[6 + ch];
        s += g == 256 ? v : v * g / 256;
    }
    return s;
}

/* ---- output: stereo 16-bit samples at 53,267 Hz, kept until taken ---- */
#define OUT_MAX 32768
static int16_t out_buf[OUT_MAX][2]; static int out_n;

static int16_t clamp16(int32_t v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

static unsigned long long samples_made;            /* for the chip log's times */
static void sample(void)
{
    samples_made++;
    int32_t fm[2];
    ym_generate(fm, sound_gain);
    /* the PSG steps 3,579,545 / 16 times a second; one sample is 53,267 Hz: about 4.2 steps */
    psg.acc += 3579545;
    while (psg.acc >= 16u * 53267u) { psg.acc -= 16u * 53267u; psg_step(); }
    int32_t p = psg_sample() / 2;
    /* the chips' outputs sit above zero; the console's output capacitor removes that. A DC
     * blocker (a one-pole high-pass at about 20 Hz) does the same here. */
    static double prev_in[2], prev_out[2];
    if (out_n < OUT_MAX) {
        for (int ch = 0; ch < 2; ch++) {
            double x = fm[ch] * 3.0 + p;
            double y = x - prev_in[ch] + 0.9976 * prev_out[ch];
            prev_in[ch] = x; prev_out[ch] = y;
            out_buf[out_n][ch] = clamp16(sound_master == 256 ? (int32_t)y : (int32_t)(y * sound_master / 256));
        }
        out_n++;
    }
}

int sound_take(int16_t *dst, int max)
{
    int n = out_n < max ? out_n : max;
    memcpy(dst, out_buf, (size_t)n * 4);
    memmove(out_buf, out_buf + n, (size_t)(out_n - n) * 4);
    out_n -= n;
    return n;
}

/* ---- the Z80's bus ---- */
static uint8_t z80_read(uint16_t a)
{
    if (a < 0x4000) return zram[a & 0x1FFF];
    if (a < 0x6000) return (uint8_t)ym_read(a & 3);
    if (a >= 0x8000) return (uint8_t)bus_read8(bank << 15 | (a & 0x7FFF));
    return 0xFF;
}

/* WBMW_SOUND_LOG=FILE: every write to the sound chips, in order (no timing), to compare runs;
 * with WBMW_SOUND_LOG_TIME=1 too, each line starts with the samples made so far (53,267 a second),
 * to time the driver against (tools/midi.py) */
static FILE *chip_log; static int chip_log_init, chip_log_time;
static void log_chip(int chip, unsigned port, unsigned d)
{
    if (!chip_log_init) {
        chip_log_init = 1; const char *p = getenv("WBMW_SOUND_LOG"); if (p) chip_log = fopen(p, "w");
        chip_log_time = getenv("WBMW_SOUND_LOG_TIME") != NULL;
    }
    if (chip_log && chip_log_time) fprintf(chip_log, "%llu ", samples_made);
    if (chip_log) fprintf(chip_log, "%s%u %02X\n", chip ? "psg" : "ym", port, d);
}

static void z80_write(uint16_t a, uint8_t d)
{
    if (a >= 0x4000 && a < 0x6000) log_chip(0, a & 3, d);
    if (a == 0x7F11) log_chip(1, 0, d);
    if (a < 0x4000) zram[a & 0x1FFF] = d;
    else if (a < 0x6000) ym_write(a & 3, d);
    else if (a < 0x6100) bank = (bank >> 1 | (uint32_t)(d & 1) << 8) & 0x1FF;
    else if (a == 0x7F11) psg_write(d);
    else if (a >= 0x8000) bus_write8(bank << 15 | (a & 0x7FFF), d);
}

static void z80_cycle(void)
{
    if (int_line) pins |= Z80_INT; else pins &= ~Z80_INT;
    pins = z80_tick(&z80, pins);
    if (pins & Z80_MREQ) {
        uint16_t a = Z80_GET_ADDR(pins);
        if (pins & Z80_RD) { Z80_SET_DATA(pins, z80_read(a)); }
        else if (pins & Z80_WR) z80_write(a, Z80_GET_DATA(pins));
    } else if (pins & Z80_IORQ) {
        if (pins & (Z80_M1 | Z80_RD)) { Z80_SET_DATA(pins, 0xFF); }   /* interrupt acknowledge, or no I/O */
    }
}

/* ---- the 68000's side ---- */
void sound_reset(void)
{
    pins = z80_init(&z80);
    busreq = 0; reset_line = 1; int_line = 0; bank = 0; acc = 0; out_n = 0;
    ym_reset(); psg_reset();
}

void sound_set_busreq(int on) { busreq = on; }
int sound_bus_granted(void) { return busreq || reset_line; }   /* the 68000 may use Z80 RAM */
void sound_set_reset(int on)
{
    if (on && !reset_line) ym_reset();                  /* the reset line resets the YM2612 too */
    if (!on && reset_line) pins = z80_reset(&z80);
    reset_line = on;
}
/* A check holds sound still while the original routine runs, so that the Z80 changes nothing the
 * check compares. That time still passes in the game, so the sound catches up on release: without
 * it, every check made the music fall behind (9% slower with the movement checks). */
void sound_hold(int on)
{
    held = on;
    if (on) return;
    int n = owed, vint = owed_vint;
    owed = owed_vint = 0;
    for (int i = 0; i < n; i++) sound_run_line(vint && i == 0);
}
void sound_psg_write(uint8_t d) { psg_write(d); }

/* The 68000 is waiting on the Z80 (QueueSound found the driver busy): let 64 Z80 cycles pass, even
 * while held, as the hardware would while the 68000 spins. Used by code that takes no time. */
void sound_idle(void)
{
    for (int i = 0; i < 64; i++) {
        if (!busreq && !reset_line) z80_cycle();
        acc += YM_PER_Z80;
        if (acc >= YM_SAMPLE) { acc -= YM_SAMPLE; sample(); }
    }
}

/* one scanline of sound; vint: the VBlank interrupt is raised on this line */
void sound_run_line(int vint)
{
    if (held) { owed++; owed_vint |= vint; return; }
    int_line = vint;
    for (int i = 0; i < Z80_CYCLES_PER_LINE; i++) {
        if (!busreq && !reset_line) z80_cycle();
        acc += YM_PER_Z80;
        if (acc >= YM_SAMPLE) { acc -= YM_SAMPLE; sample(); }
    }
    int_line = 0;
}

void sound_state_io(FILE *f, int saving)
{
    state_io(f, &z80, sizeof z80, saving); state_io(f, &pins, sizeof pins, saving);
    state_io(f, &busreq, sizeof busreq, saving); state_io(f, &reset_line, sizeof reset_line, saving);
    state_io(f, &bank, sizeof bank, saving); state_io(f, &acc, sizeof acc, saving);
    state_io(f, &psg, sizeof psg, saving);
    ym_state_io(f, saving);
}
