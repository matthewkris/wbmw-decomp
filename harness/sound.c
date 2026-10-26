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
#include "../port/sounddrv.h"

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
static int c_mode;                    /* the driver in C runs in place of the Z80 (below) */

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

/* ---- the shadow check of the sound driver in C (port/sounddrv.h), WBMW_Z80_SHADOW=1: when the
 * Z80 starts a routine of snd_routines, the C runs on a copy of the RAM, its chip writes kept;
 * when the Z80 has returned from it (its stack pointer back above the return address), the
 * driver's variables ($1B80-$1FFF) and the chip writes, in order, must be the same. At exit, the
 * routines' calls and differences. ---- */
static int sh_on = -1;
static const SndRoutine *sh_r;                     /* the routine being checked, or none */
static uint16_t sh_sp;                              /* the Z80's stack pointer at its entry */
static uint8_t sh_ram[0x2000];
static uint16_t sh_c[1024], sh_z[1024];             /* the chip writes, C and Z80: port << 8 | data, $800 the PSG */
static int sh_cn, sh_zn;
static struct { unsigned calls, bad; } sh_stats[256];
static unsigned sh_skipped;                         /* whole ticks a command reached mid-way */

static void sh_ym(unsigned port, unsigned d) { if (sh_cn < 1024) sh_c[sh_cn++] = (uint16_t)((port & 3) << 8 | (d & 0xFF)); }
static void sh_psg(unsigned d) { if (sh_cn < 1024) sh_c[sh_cn++] = (uint16_t)(0x800 | (d & 0xFF)); }
static uint8_t sh_window(uint16_t a) { return (uint8_t)bus_read8(bank << 15 | (a & 0x7FFF)); }

static void sh_report(void)
{
    int called = 0;
    for (int i = 0; i < snd_routine_count; i++) {
        if (!sh_stats[i].calls) continue;
        called++;
        fprintf(stderr, "sound shadow: %-20s $%04X  %7u calls  %u differ\n", snd_routines[i].name, snd_routines[i].addr,
                sh_stats[i].calls, sh_stats[i].bad);
    }
    fprintf(stderr, "sound shadow: %d of %d routines called\n", called, snd_routine_count);
    if (sh_skipped) fprintf(stderr, "sound shadow: %u ticks a command reached mid-way, not compared\n", sh_skipped);
}

static uint8_t sh_pre[0x2000];                      /* the RAM at the routine's entry, and its ix (WBMW_Z80_SHADOW_DUMP) */
static uint16_t sh_ix;
static SndRegs sh_out;                              /* the C's results */
static uint8_t sh_marks[0x2000];                    /* the bytes the 68000 wrote while the Z80 ran the routine: not compared */
static void sh_start(const SndRoutine *r)
{
    SndRegs g = { z80.a, z80.f, z80.b, z80.c, z80.d, z80.e, z80.h, z80.l, z80.ix, z80.iy, z80.sp };
    memcpy(sh_ram, zram, sizeof sh_ram);
    memcpy(sh_pre, zram, sizeof sh_pre); sh_ix = z80.ix;
    SndIO keep = snd_io;
    snd_io.ram = sh_ram; snd_io.ym = sh_ym; snd_io.psg = sh_psg; snd_io.window = sh_window;
    sh_cn = sh_zn = 0;
    r->fn(&g);
    sh_out = g;
    snd_io = keep;
    sh_r = r; sh_sp = z80.sp;
    memset(sh_marks, 0, sizeof sh_marks); bus_zram_marks = sh_marks;   /* what the 68000 writes meanwhile */
}

/* the registers the routine answers in, the Z80's against the C's: the first that differs */
static void sh_regs(char *why, size_t n)
{
    unsigned o = sh_r->outs;
    const SndRegs *c = &sh_out;
    struct { unsigned bit; const char *name; unsigned z, cc; } t[] = {
        {SR_A, "a", z80.a, c->a}, {SR_B, "b", z80.b, c->b}, {SR_C, "c", z80.c, c->c}, {SR_D, "d", z80.d, c->d},
        {SR_E, "e", z80.e, c->e}, {SR_H, "h", z80.h, c->h}, {SR_L, "l", z80.l, c->l}, {SR_IX, "ix", z80.ix, c->ix},
        {SR_IY, "iy", z80.iy, c->iy}, {SR_ZF, "the zero flag", z80.f >> 6 & 1, c->f >> 6 & 1},
        {SR_CF, "the carry flag", z80.f & 1, c->f & 1}};
    for (unsigned k = 0; k < sizeof t / sizeof t[0]; k++)
        if ((o & t[k].bit) && t[k].z != t[k].cc) { snprintf(why, n, "%s: the Z80's %X, the C's %X", t[k].name, t[k].z, t[k].cc); return; }
}

static void sh_end(void)
{
    int i = (int)(sh_r - snd_routines);
    char why[160] = "";
    sh_stats[i].calls++;
    bus_zram_marks = NULL;
    if (sh_r->end)                                  /* a whole tick: a command queued while the Z80 ran it (the */
        for (int a = 0x1C2E; a < 0x1C40; a++)       /* 68000 waiting on it) reaches it, not the C: not compared */
            if (sh_marks[a]) { sh_stats[i].calls--; sh_skipped++; sh_r = NULL; return; }
    for (int a = 0x1B80; a < 0x2000 && !why[0]; a++)
        if (zram[a] != sh_ram[a] && !sh_marks[a]) snprintf(why, sizeof why, "RAM $%04X: the Z80's %02X, the C's %02X", a, zram[a], sh_ram[a]);
    for (int k = 0; !why[0] && k < (sh_cn > sh_zn ? sh_cn : sh_zn); k++)
        if (k >= sh_cn || k >= sh_zn || sh_c[k] != sh_z[k]) {
            char z[16] = "none", cc[16] = "none";
            if (k < sh_zn) snprintf(z, sizeof z, sh_z[k] & 0x800 ? "psg %02X" : "ym%d %02X", sh_z[k] & 0x800 ? sh_z[k] & 0xFF : sh_z[k] >> 8, sh_z[k] & 0xFF);
            if (k < sh_cn) snprintf(cc, sizeof cc, sh_c[k] & 0x800 ? "psg %02X" : "ym%d %02X", sh_c[k] & 0x800 ? sh_c[k] & 0xFF : sh_c[k] >> 8, sh_c[k] & 0xFF);
            snprintf(why, sizeof why, "chip write %d: the Z80's %s, the C's %s (writes: %d, %d)", k, z, cc, sh_zn, sh_cn);
        }
    if (!why[0]) sh_regs(why, sizeof why);
    if (why[0] && ++sh_stats[i].bad <= 3) {
        fprintf(stderr, "sound shadow: %s ($%04X): %s\n", sh_r->name, sh_r->addr, why);
        if (getenv("WBMW_Z80_SHADOW_DUMP")) {       /* the record at ix: before, the Z80's after, the C's after */
            const uint8_t *b[3] = {sh_pre, zram, sh_ram};
            for (int q = 0; q < 3; q++) {
                fprintf(stderr, "  %04X:", sh_ix);
                for (int k = 0; k < 64; k++) fprintf(stderr, " %02X", b[q][(sh_ix + k) & 0x1FFF]);
                fprintf(stderr, "\n");
            }
        }
    }
    sh_r = NULL;
}

/* at the start of each Z80 instruction */
static void sh_step(uint16_t pc)
{
    if (sh_on < 0) {
        const char *e = getenv("WBMW_Z80_SHADOW");
        sh_on = !e ? 0 : !strcmp(e, "tick") ? 2 : 1;    /* tick: the whole ticks and the start alone */
        if (sh_on) atexit(sh_report);
    }
    if (!sh_on) return;
    if (sh_r) {
        if (sh_r->end ? pc == sh_r->end : z80.sp == (uint16_t)(sh_sp + 2)) sh_end(); else return;
    }
    for (int i = 0; i < snd_routine_count; i++)
        if (snd_routines[i].addr == pc && !snd_routines[i].end == (sh_on == 1)) { sh_start(&snd_routines[i]); return; }
}

static void z80_write(uint16_t a, uint8_t d)
{
    if (sh_r && sh_zn < 1024) {
        if (a >= 0x4000 && a < 0x6000) sh_z[sh_zn++] = (uint16_t)((a & 3) << 8 | d);
        else if (a == 0x7F11) sh_z[sh_zn++] = (uint16_t)(0x800 | d);
    }
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
    if ((pins & (Z80_M1 | Z80_MREQ | Z80_RD)) == (Z80_M1 | Z80_MREQ | Z80_RD) && !z80.prefix_active)
        sh_step(Z80_GET_ADDR(pins));                /* an instruction starts */
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
    busreq = 0; reset_line = 1; int_line = 0; bank = 0; acc = 0; out_n = 0; c_mode = 0;
    ym_reset(); psg_reset();
}

/* ---- the driver in C (port/sounddrv.c) in place of the Z80, sound_c_driver (the native build's
 * default): started when the Z80 would first run after its reset (the driver loaded), and a tick of
 * its main loop each time the YM2612's timer B overflows, as the original's waits for it, while
 * the 68000 holds neither the bus nor reset. Its state is the Z80's RAM; the Z80 itself is left
 * where the original waits for the timer ($0127, its return to the main loop on the stack), so a
 * save state taken here plays on in the harness's Z80 too. Not with WBMW_Z80_SHADOW, which needs
 * the Z80. ---- */
int sound_c_driver;

static void c_ym(unsigned port, unsigned d) { log_chip(0, port & 3, d & 0xFF); ym_write(port & 3, d & 0xFF); }
static void c_psg(unsigned d) { log_chip(1, 0, d & 0xFF); psg_write((uint8_t)d); }
static uint8_t c_window(uint16_t a) { return (uint8_t)bus_read8(bank << 15 | (a & 0x7FFF)); }

static int c_resume(void)                          /* the C driver to run from here (its state is the RAM) */
{
    c_mode = sound_c_driver && !reset_line && !getenv("WBMW_Z80_SHADOW");
    if (c_mode) { snd_io.ram = zram; snd_io.ym = c_ym; snd_io.psg = c_psg; snd_io.window = c_window; }
    return c_mode;
}

/* the driver's start, when the Z80 would first run after its reset (c_mode 2: the reset let go,
 * the 68000 perhaps still holding the bus) */
static void c_start(void)
{
    if (!c_resume()) return;
    bank = SND_WINDOW >> 15;                        /* (the driver's start sets it) */
    snd_init();
    zram[0x1B7E] = 0xBC; zram[0x1B7F] = 0x00;       /* the Z80: in $0110's wait, called from $00B9 */
    z80.sp = 0x1B7E;
    pins = z80_prefetch(&z80, 0x0127);
}

/* the C driver's start or tick (timer B has overflowed), when the Z80 would run */
static void c_poll(void)
{
    if (!c_mode || busreq || reset_line) return;
    if (c_mode == 2) c_start();
    else if (ym_read(0) & 2) snd_tick();
}

void sound_set_busreq(int on) { busreq = on; }
int sound_bus_granted(void) { return busreq || reset_line; }   /* the 68000 may use Z80 RAM */
void sound_set_reset(int on)
{
    if (on && !reset_line) ym_reset();                  /* the reset line resets the YM2612 too */
    int start = !on && reset_line;
    if (start) pins = z80_reset(&z80);
    reset_line = on;
    if (start && c_resume()) c_mode = 2;
    else if (on) c_mode = 0;
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
        if (!busreq && !reset_line && !c_mode) z80_cycle();
        acc += YM_PER_Z80;
        if (acc >= YM_SAMPLE) { acc -= YM_SAMPLE; sample(); c_poll(); }
    }
}

/* one scanline of sound; vint: the VBlank interrupt is raised on this line */
void sound_run_line(int vint)
{
    if (held) { owed++; owed_vint |= vint; return; }
    int_line = vint;
    for (int i = 0; i < Z80_CYCLES_PER_LINE; i++) {
        if (!busreq && !reset_line && !c_mode) z80_cycle();
        acc += YM_PER_Z80;
        if (acc >= YM_SAMPLE) { acc -= YM_SAMPLE; sample(); c_poll(); }
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
    if (!saving) c_resume();
}
