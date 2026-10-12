/* Ported routines (port/) running inside the harness in place of their 68000 originals.
 *
 * Musashi calls md_instr_hook before every instruction. When the PC reaches the entry of a
 * ported routine:
 *   replace mode: run the C routine on the live machine, then do the original's rts.
 *   shadow mode:  snapshot the machine, run the C routine and record everything it produced,
 *                 restore the snapshot, and let the original 68000 routine run (interrupts held,
 *                 nested ported routines left alone). When it returns, compare registers,
 *                 condition codes, work RAM, Z80 RAM and VDP memory and latches with the C
 *                 result, and count the call as a match or report the difference.
 * The run that continues is always the original's in shadow mode, so a shadow run is the
 * pure 68000 run plus checks (and interrupts deferred while a check is open). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "md.h"
#include "../port/port.h"
#include "../third_party/musashi/m68k.h"

/* ported code waiting on the Z80: let sound time pass (undone with the snapshot in shadow mode) */
void port_idle(void) { sound_idle(); }

/* memory for port/, through the bus so VDP and Z80 writes behave as on the machine */
uint8_t rd8(uint32_t a) { return (uint8_t)bus_read8(a); }
uint16_t rd16(uint32_t a) { return (uint16_t)bus_read16(a); }
void wr8(uint32_t a, uint8_t v) { bus_write8(a, v); }
void wr16(uint32_t a, uint16_t v) { bus_write16(a, v); }

/* every routine the harness can run in C: hand ports (port/registry.c), then the generated ones
 * (work/gen, tools/m68k2c.py). A generated routine does its own rts; a hand port does not. */
extern const PortRoutine gen_routines[];
extern const int gen_routine_count;
static int NR;
static const PortRoutine *R(int i) { return i < port_routine_count ? &port_routines[i] : &gen_routines[i - port_routine_count]; }
static int pops(int i) { return i >= port_routine_count; }

/* generated code reached code that is not translated: continue in the 68000 there. The original
 * code is always in the ROM, so this is always correct; the report lists where it happened. */
static struct { uint32_t from, to; long n; } leaves[64]; static int nleaves;
void rt_leave(Cpu *c, uint32_t from, uint32_t target)
{
    c->ret = target;
    int i = 0;
    while (i < nleaves && (leaves[i].from != from || leaves[i].to != target)) i++;
    if (i == nleaves && nleaves < 64) { leaves[i].from = from; leaves[i].to = target; nleaves++; }
    if (i < nleaves) leaves[i].n++;
}

/* the generated code's watchdog (port/m68k_rt.h): a call into C that passes 100 million branch
 * targets is taken to be stuck in a loop that waits for time; say where, and stop */
uint32_t rt_ring[16]; unsigned rt_ring_i; unsigned long rt_steps, rt_limit = 100000000;
void rt_stuck(void)
{
    fprintf(stderr, "STUCK: generated code looped %lu times without returning; recent branch targets:", rt_steps);
    for (unsigned i = 0; i < 16; i++) fprintf(stderr, " %06X", (unsigned)rt_ring[(rt_ring_i + i) & 15]);
    fprintf(stderr, "\n");
    exit(3);
}

/* exits (analysis/translate.json): the generated code stops there, e.g. where a task resets the
 * stack and re-enters the frame loop, so a check also ends when the original reaches one */
extern const uint32_t gen_exits[];
extern const int gen_exit_count;
static int is_exit(uint32_t pc)
{
    for (int i = 0; i < gen_exit_count; i++) if (gen_exits[i] == pc) return 1;
    return 0;
}

static int mode;
static int break_idx = -1;            /* self-test: corrupt this routine's result (WBMW_PORT_BREAK=name) */
static int watch_break_idx = -1;      /* self-test of the watch: scramble its leftovers (WBMW_WATCH_BREAK=name) */
static int16_t *slot_of;              /* routine index for each even ROM address, -1 if none */
#define SLOTS 0x20000
static struct Stat { long calls, bad, dead; } *stats;   /* dead: left registers different, never read */    /* one per routine, sized in hybrid_init */

static void get_cpu(Cpu *c)
{
    for (int i = 0; i < 8; i++) c->d[i] = m68k_get_reg(NULL, M68K_REG_D0 + i);
    for (int i = 0; i < 8; i++) c->a[i] = m68k_get_reg(NULL, M68K_REG_A0 + i);
    c->ccr = (uint16_t)(m68k_get_reg(NULL, M68K_REG_SR) & 0x1F);
}

static void set_cpu(const Cpu *c)
{
    for (int i = 0; i < 8; i++) m68k_set_reg(M68K_REG_D0 + i, c->d[i]);
    for (int i = 0; i < 8; i++) m68k_set_reg(M68K_REG_A0 + i, c->a[i]);
    m68k_set_reg(M68K_REG_SR, (m68k_get_reg(NULL, M68K_REG_SR) & ~0x1Fu) | c->ccr);
}

/* what a routine produced: registers and the memories it can touch */
typedef struct {
    Cpu cpu; uint32_t pc;
    uint8_t ram[0x10000], zram[0x2000], vram[0x10000], reg[32];
    uint16_t cram[64], vsram[40];
    int latch[4];
} Result;

static void capture(Result *r)
{
    get_cpu(&r->cpu); r->pc = m68k_get_reg(NULL, M68K_REG_PC);
    memcpy(r->ram, ram, sizeof ram); memcpy(r->zram, zram, sizeof zram);
    memcpy(r->vram, vram, sizeof vram); memcpy(r->reg, vdp_reg, sizeof r->reg);
    memcpy(r->cram, cram, sizeof cram); memcpy(r->vsram, vsram, sizeof vsram);
    vdp_latch(r->latch);
}

static Result *want, *got;
static int fuzzing;
static uint8_t *snap; static size_t snap_n;
static struct { int open, idx; uint32_t ret, sp; long call, steps; } shadow;
static int reports;

static void diff_bytes(const char *what, const uint8_t *a, const uint8_t *b, size_t n, uint32_t base)
{
    for (size_t i = 0; i < n; i++) {
        if (a[i] == b[i]) continue;
        size_t j = i; while (j < n && j < i + 16 && a[j] != b[j]) j++;
        fprintf(stderr, "    %s $%06X: original", what, (unsigned)(base + i));
        for (size_t k = i; k < j; k++) fprintf(stderr, " %02X", a[k]);
        fprintf(stderr, "  port");
        for (size_t k = i; k < j; k++) fprintf(stderr, " %02X", b[k]);
        fprintf(stderr, "\n");
        i = j;
    }
}

/* Contracts (port.h): for an ordinary C rewrite, the registers outside its contract may differ
 * from the original's, provided the game never reads them before writing them. After such a
 * check, the registers that differ are watched instruction by instruction (work/usedef.bin, from
 * tools/liveness.py: what each instruction reads and writes); reading one is a violation. Data
 * registers are watched in three parts (bits 0-7, 8-15, 16-31), so a byte written over a register
 * whose upper part differs is not a read of it. Bits: 3i..3i+2 di's parts, 24+i ai, 32 NZVC, 33 X. */
#define W_NZVC (1ull << 32)
#define W_X    (1ull << 33)
static uint64_t *usedef;                  /* reads, writes for each instruction address / 2 */
static uint64_t watch;                    /* parts of registers and flags that differ from the original's */
static int watch_of[34];                  /* the routine that left each one different */
static long violations;

static uint64_t reg_diff(const Cpu *a, const Cpu *b)
{
    uint64_t m = 0;
    for (int i = 0; i < 8; i++) {
        uint32_t x = a->d[i] ^ b->d[i];
        if (x & 0xFF) m |= 1ull << (3 * i);
        if (x & 0xFF00) m |= 2ull << (3 * i);
        if (x & 0xFFFF0000u) m |= 4ull << (3 * i);
        if (a->a[i] != b->a[i]) m |= 1ull << (24 + i);
    }
    if ((a->ccr ^ b->ccr) & 0x0F) m |= W_NZVC;
    if ((a->ccr ^ b->ccr) & 0x10) m |= W_X;
    return m;
}

/* a contract (whole registers, port.h) in the watch's bits */
static uint64_t contract_parts(uint32_t ct)
{
    uint64_t m = 0;
    for (int i = 0; i < 8; i++) {
        if (ct >> i & 1) m |= 7ull << (3 * i);
        if (ct >> (8 + i) & 1) m |= 1ull << (24 + i);
    }
    if (ct & CT_NZVC) m |= W_NZVC;
    if (ct & CT_X) m |= W_X;
    return m;
}

/* link An saves An in the new frame and unlk An puts it back: a watched An that is only saved
 * and restored is not used. So link does not count as a read; the watch bit is kept with the
 * frame (by its address) and given back to An by the unlk of that frame. ($E2C6 links with a
 * positive displacement, so its first call overwrites half of the saved a6: the original comes
 * back with a6 changed, and the next link would otherwise count as reading it.) */
static struct { uint32_t frame; int who, reg; } frames[64];
static int nframes;

static void watch_step(unsigned pc)
{
    if ((!watch && !nframes) || !usedef || pc >= 0x20000) return;
    uint64_t reads = usedef[(pc >> 1) * 3], writes = usedef[(pc >> 1) * 3 + 1], to_flags = usedef[(pc >> 1) * 3 + 2];
    uint16_t op = (uint16_t)bus_read16(pc);
    if ((op & 0xFFF8) == 0x4840 || (op & 0xFFB8) == 0x4880) {
        /* swap Dn, ext.w/ext.l Dn: the difference moves between the register's parts (and into
         * the flags) instead of being used */
        int n = op & 7;
        uint64_t p0 = 1ull << (3 * n), p1 = 2ull << (3 * n), p2 = 4ull << (3 * n), all = p0 | p1 | p2;
        uint64_t was = watch & all;
        if (!was) { watch &= ~W_NZVC; return; }
        int who = watch_of[__builtin_ctzll(was)];
        uint64_t now;
        if ((op & 0xFFF8) == 0x4840) now = ((was & p2) ? p0 | p1 : 0) | ((was & (p0 | p1)) ? p2 : 0);
        else if (!(op & 0x40)) now = (was & p2) | ((was & p0) ? p0 | p1 : 0);   /* ext.w: from bits 0-7 */
        else now = (was & (p0 | p1)) | ((was & p1) ? p2 : 0);                    /* ext.l: from bits 0-15 */
        watch = (watch & ~all & ~W_NZVC) | now | W_NZVC;
        for (int i = 0; i < 3; i++) if (now >> (3 * n + i) & 1) watch_of[3 * n + i] = who;
        watch_of[32] = who;
        return;
    }
    if ((op & 0xFFF8) == 0x4E50) {                  /* link.w An */
        int n = op & 7; uint64_t bit = 1ull << (24 + n);
        reads &= ~bit;
        if ((watch & bit) && nframes < 64) {
            frames[nframes].frame = m68k_get_reg(NULL, M68K_REG_SP) - 4;
            frames[nframes].who = watch_of[24 + n]; frames[nframes].reg = n; nframes++;
        }
    }
    if (reads & watch) {
        int bitn = 0; while (!((reads & watch) >> bitn & 1)) bitn++;
        int who = watch_of[bitn];
        stats[who].bad++; violations++;
        if (reports++ < 20) {
            static const char *parts[3] = {"bits 0-7", "bits 8-15", "bits 16-31"};
            char what[32];
            if (bitn < 24) snprintf(what, sizeof what, "d%d (%s)", bitn / 3, parts[bitn % 3]);
            else if (bitn < 32) snprintf(what, sizeof what, "a%d", bitn - 24);
            else snprintf(what, sizeof what, "%s", bitn == 32 ? "NZVC" : "X");
            fprintf(stderr, "CONTRACT %s ($%06X): left %s different from the original's, and the game reads it at %06X\n",
                    R(who)->name, (unsigned)R(who)->addr, what, pc);
        }
        watch = 0; nframes = 0;
        return;
    }
    int into_flags = (to_flags & watch) != 0;      /* a shift moved a difference into C and X */
    int flags_from = into_flags ? watch_of[__builtin_ctzll(to_flags & watch)] : 0;
    watch &= ~writes;
    if (into_flags) { watch |= W_NZVC | W_X; watch_of[32] = watch_of[33] = flags_from; }
    if ((op & 0xFFF8) == 0x4E58 && nframes) {      /* unlk An: An comes back from its frame */
        int n = op & 7;
        uint32_t frame = m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_A0 + n));
        for (int i = nframes - 1; i >= 0; i--) {
            if (frames[i].frame < frame) { nframes = i; continue; }   /* deeper frames: gone */
            if (frames[i].frame == frame && frames[i].reg == n) {
                watch |= 1ull << (24 + n); watch_of[24 + n] = frames[i].who;
                nframes = i;
            }
            break;
        }
    }
}

static void check(void)
{
    const PortRoutine *p = R(shadow.idx);
    capture(got);                                   /* the original's result */
    Cpu *a = &got->cpu, *b = &want->cpu;
    /* What must match: memory, the PC and the stack pointer, and the registers a contract names.
     * The stack below the final stack pointer is dead. Other registers and flags that differ are
     * watched (watch_step): a difference the game never reads cannot change what it does. Every
     * check works this way; a translated routine that calls ordinary C inherits its leftovers. */
    uint32_t contract = p->contract | CT_SP;
    uint32_t sp = a->a[7] & 0xFFFF;
    if ((a->a[7] & 0xFF0000) == 0xFF0000)
        for (uint32_t i = 0; i < sp && i < 0x1000; i++) got->ram[i] = want->ram[i];
    uint64_t diff = reg_diff(a, b) & ~contract_parts(contract);
    if (diff) {
        stats[shadow.idx].dead++;
        for (int i = 0; i < 34; i++) if (diff >> i & 1) watch_of[i] = shadow.idx;
        if (!fuzzing) watch |= diff;               /* a fuzz case has no caller afterwards: nothing to watch */
    }
    for (int i = 0; i < 8; i++) { if (!(contract >> i & 1)) a->d[i] = b->d[i]; if (!(contract >> (8 + i) & 1)) a->a[i] = b->a[i]; }
    if (!(contract & CT_NZVC)) a->ccr = (uint16_t)((a->ccr & ~0x0F) | (b->ccr & 0x0F));
    if (!(contract & CT_X)) a->ccr = (uint16_t)((a->ccr & ~0x10) | (b->ccr & 0x10));
    int bad = memcmp(a->d, b->d, sizeof a->d) || memcmp(a->a, b->a, sizeof a->a) || a->ccr != b->ccr
           || got->pc != want->pc || memcmp(got->ram, want->ram, sizeof got->ram)
           || memcmp(got->zram, want->zram, sizeof got->zram) || memcmp(got->vram, want->vram, sizeof got->vram)
           || memcmp(got->reg, want->reg, sizeof got->reg) || memcmp(got->cram, want->cram, sizeof got->cram)
           || memcmp(got->vsram, want->vsram, sizeof got->vsram) || memcmp(got->latch, want->latch, sizeof got->latch);
    if (bad) {
        stats[shadow.idx].bad++;
        if (reports++ < 20) {
            fprintf(stderr, "MISMATCH %s ($%06X) call %ld\n", p->name, (unsigned)p->addr, shadow.call);
            for (int i = 0; i < 8; i++) if (a->d[i] != b->d[i]) fprintf(stderr, "    d%d original %08X port %08X\n", i, a->d[i], b->d[i]);
            for (int i = 0; i < 8; i++) if (a->a[i] != b->a[i]) fprintf(stderr, "    a%d original %08X port %08X\n", i, a->a[i], b->a[i]);
            if (a->ccr != b->ccr) fprintf(stderr, "    ccr original %02X port %02X (XNZVC)\n", a->ccr, b->ccr);
            if (got->pc != want->pc) fprintf(stderr, "    pc original %06X port %06X\n", got->pc, want->pc);
            diff_bytes("RAM", got->ram, want->ram, sizeof got->ram, 0xFF0000);
            diff_bytes("Z80", got->zram, want->zram, sizeof got->zram, 0xA00000);
            diff_bytes("VRAM", got->vram, want->vram, sizeof got->vram, 0);
            diff_bytes("VDP reg", got->reg, want->reg, sizeof got->reg, 0);
            diff_bytes("CRAM", (uint8_t *)got->cram, (uint8_t *)want->cram, sizeof got->cram, 0);
            if (memcmp(got->latch, want->latch, sizeof got->latch)) fprintf(stderr, "    VDP port latch differs\n");
        }
    }
    shadow.open = 0;
    bus_timing(TIMING_OFF);
    md_irq_hold(0);
    if (fuzzing) m68k_end_timeslice();              /* one fuzz case per execute */
}

static void run_port(int idx)
{
    Cpu c; get_cpu(&c);
    rt_steps = 0;
    c.ret = 0;
    R(idx)->fn(&c);
    if (idx == break_idx) c.a[7] ^= 2;              /* the stack pointer: compared in every check */
    if (idx == watch_break_idx)                     /* every register outside the contract */
        for (int i = 0; i < 7; i++) {
            if (!(R(idx)->contract >> i & 1)) c.d[i] ^= 0x5A5A5A5Au;
            if (!(R(idx)->contract >> (8 + i) & 1)) c.a[i] ^= 0x10u;
        }
    uint32_t ret;
    if (pops(idx)) ret = c.ret;                     /* generated: it did the rts (maybe elsewhere) */
    else if (c.ret) ret = c.ret;                    /* hand port that carries on in the original there (world.h) */
    else { ret = rd32(c.a[7]); c.a[7] += 4; }       /* hand port: the original's rts */
    set_cpu(&c);
    m68k_set_reg(M68K_REG_PC, ret);
}

/* coverage: which instructions the originals executed inside shadow checks */
static uint8_t *visited; static const char *cov_path;
void hybrid_coverage(const char *path) { cov_path = path; }

/* Yield survey (WBMW_YIELD_SURVEY=1, any mode): for every call of Yield, where the task resumes
 * and where Yield's rts goes. If that is always a scheduler (here $4BD8, system slots, and
 * $4C8A, user slots), no task yields from inside a nested call: a task's state between passes
 * is its resume address and RAM, nothing on the stack (docs/PORT.md, Tasks). Game-specific:
 * Yield is at $400. */
#define YIELD_ADDR 0x400
static long sv_n[0x20000]; static uint32_t sv_to[0x20000]; static int sv_on = -1;
void yield_survey_report(void)
{
    if (sv_on != 1) return;
    for (int i = 0; i < 0x20000; i++)
        if (sv_n[i]) fprintf(stderr, "yield: resume %06X, returns to %06X, %ld times\n", i, sv_to[i], sv_n[i]);
}
/* call trace (WBMW_TRACE=ADDR[,ADDR...] in hex, any mode): each time the 68000 reaches one of
 * these addresses, print the game's frame counter ($FF8002), d0-d7, a0 and the return address */
static uint32_t tr_addr[8]; static int tr_n = -1;
static void trace_at(unsigned pc)
{
    if (tr_n < 0) {
        tr_n = 0;
        const char *s = getenv("WBMW_TRACE");
        while (s && *s && tr_n < 8) { char *e; tr_addr[tr_n++] = (uint32_t)strtoul(s, &e, 16); s = *e ? e + 1 : e; }
    }
    for (int i = 0; i < tr_n; i++)
        if (tr_addr[i] == pc) {
            uint32_t sp = m68k_get_reg(NULL, M68K_REG_SP);
            fprintf(stderr, "trace %06X game frame %u d0 %08X d1 %08X d2 %08X d3 %08X d4 %08X d5 %08X d6 %08X d7 %08X a0 %08X from %06X\n",
                    pc, (unsigned)(ram[0x8002] << 24 | ram[0x8003] << 16 | ram[0x8004] << 8 | ram[0x8005]),
                    m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1), m68k_get_reg(NULL, M68K_REG_D2),
                    m68k_get_reg(NULL, M68K_REG_D3), m68k_get_reg(NULL, M68K_REG_D4), m68k_get_reg(NULL, M68K_REG_D5),
                    m68k_get_reg(NULL, M68K_REG_D6), m68k_get_reg(NULL, M68K_REG_D7),
                    m68k_get_reg(NULL, M68K_REG_A0), bus_read16(sp) << 16 | bus_read16(sp + 2));
        }
}

static long ft_case = -2, ft_k = -1;           /* WBMW_FUZZ_TRACE=N: every instruction of fuzz case N */
int md_instr_hook(unsigned int pc)
{
    if (ft_case == -2) { const char *f = getenv("WBMW_FUZZ_TRACE"); ft_case = f ? atol(f) : -1; }
    if (fuzzing && ft_k == ft_case) fprintf(stderr, "ft %06X d0 %08X d1 %08X a0 %08X a2 %08X a7 %08X\n", pc, m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1), m68k_get_reg(NULL, M68K_REG_A0), m68k_get_reg(NULL, M68K_REG_A2), m68k_get_reg(NULL, M68K_REG_SP));
    if (tr_n) trace_at(pc);
    framelog_at(pc);
    if (watch) watch_step(pc);                     /* registers a C rewrite left different: still dead? */
    if (sv_on < 0) sv_on = getenv("WBMW_YIELD_SURVEY") != NULL;
    if (sv_on && pc == YIELD_ADDR) {
        uint32_t sp = m68k_get_reg(NULL, M68K_REG_SP);
        uint32_t at = bus_read16(sp) << 16 | bus_read16(sp + 2), to = bus_read16(sp + 4) << 16 | bus_read16(sp + 6);
        if (at < 0x20000) { sv_n[at]++; sv_to[at] = to; }
    }
    if (mode == PORT_OFF) return 0;
    if (shadow.open) {
        if (visited && pc < SLOTS) visited[pc] = 1;
        /* the sound is held during a check; QueueSound retrying on a busy driver would wait for
         * ever. Each retry lets 64 Z80 cycles pass, as the port's port_idle does on its side, so
         * both see the same Z80. (Game-specific: $38E follows the bus release in QueueSound's retry.) */
        if (pc == 0x38E) sound_idle();
        if (++shadow.steps > 50000000) {           /* the original never got where the port ended */
            stats[shadow.idx].bad++;
            fprintf(stderr, "UNFINISHED %s ($%06X) call %ld: the original did not reach the port's end, pc %06X\n",
                    R(shadow.idx)->name, (unsigned)R(shadow.idx)->addr, shadow.call, (unsigned)want->pc);
            shadow.open = 0; bus_timing(TIMING_OFF); md_irq_hold(0);
            return 0;
        }
        /* the check ends where the C version ended: usually the return to the caller, but Yield
         * returns to the scheduler, and a routine that saves its return address and jumps back
         * to it later ($C7E6) ends after that; or at an exit. If the original never gets there,
         * the check is reported unfinished. */
        uint32_t sp = m68k_get_reg(NULL, M68K_REG_SP);
        if ((pc == want->pc && sp == want->cpu.a[7]) || is_exit(pc)) {
            check();
            if (watch) watch_step(pc);              /* the instruction here runs next: under the new watch */
        }
        return 0;
    }
    if (pc >= SLOTS || slot_of[pc] < 0) return 0;
    int idx = slot_of[pc];
    stats[idx].calls++;
    if (mode == PORT_REPLACE) { run_port(idx); return 0; }

    /* shadow: port first, then rewind and let the original run */
    uint32_t sp = m68k_get_reg(NULL, M68K_REG_SP);
    shadow.ret = bus_read16(sp) << 16 | bus_read16(sp + 2);
    shadow.sp = sp + 4; shadow.idx = idx; shadow.call = stats[idx].calls;
    const uint8_t *s = md_snapshot(&snap_n);
    snap = realloc(snap, snap_n); memcpy(snap, s, snap_n);
    if (visited) visited[pc] = 1;                   /* the entry instruction runs inside the check */
    md_irq_hold(1);                                 /* before the restore, so no interrupt is taken here */
    bus_timing(TIMING_RECORD);                      /* the raster as the port saw it ... */
    run_port(idx);
    capture(want);
    bus_timing(TIMING_REPLAY);                      /* ... is what the original sees */
    md_restore(snap, snap_n);
    m68k_set_reg(M68K_REG_PC, pc);
    shadow.open = 1; shadow.steps = 0;
    return 0;
}

void hybrid_init(int m)
{
    mode = m;
    if (mode == PORT_OFF) return;
    slot_of = malloc(SLOTS * sizeof *slot_of);
    for (int i = 0; i < SLOTS; i++) slot_of[i] = -1;
    NR = port_routine_count + gen_routine_count;
    stats = calloc((size_t)NR, sizeof *stats);
    for (int i = NR - 1; i >= 0; i--) slot_of[R(i)->addr] = (int16_t)i;   /* a hand port wins over a translation */
    const char *brk = getenv("WBMW_PORT_BREAK");
    for (int i = 0; brk && i < NR; i++)
        if (!strcmp(brk, R(i)->name)) { break_idx = i; fprintf(stderr, "self-test: corrupting %s\n", brk); }
    const char *wbrk = getenv("WBMW_WATCH_BREAK");
    for (int i = 0; wbrk && i < NR; i++)
        if (!strcmp(wbrk, R(i)->name)) { watch_break_idx = i; fprintf(stderr, "self-test: scrambling the leftovers of %s\n", wbrk); }
    want = malloc(sizeof *want); got = malloc(sizeof *got);
    if (cov_path && mode == PORT_SHADOW) visited = calloc(SLOTS, 1);
    FILE *ud = fopen("work/usedef.bin", "rb");     /* for contracts; from tools/liveness.py --usedef */
    if (!ud) ud = fopen("../work/usedef.bin", "rb");
    if (ud) { usedef = malloc(0x10000 * 24); if (fread(usedef, 24, 0x10000, ud) != 0x10000) { free(usedef); usedef = NULL; } fclose(ud); }
}

/* Differential fuzzing: call one ported routine directly, many times, from the current machine
 * state with random inputs, each call checked by the shadow machinery (so --coverage records the
 * paths). Inputs: every data register, the condition codes, the random seed, all of the
 * entity arrays ($FFC000-$FFDFFF) and a4 = a random entity. Address registers other than a4 and
 * a7 keep their values, so this suits routines that take numbers and entities, not pointers. */
static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)(rng >> 16); }

/* every valid (set, group, animation) of anim_data, as the byte offsets entities store; the
 * tables of each level are packed back to back (docs/NOTES.md, Sprites) */
#define ANIM_DATA 0xA0000u
static int anim_n; static uint8_t anim_sga[2048][3];

static int16_t aw(uint32_t off) { return (int16_t)bus_read16(ANIM_DATA + off); }

/* packed tables (as tools/sprites.py): sorted unique starts; each table ends at the next start,
 * the last where the lowest offset any of them holds begins. Fills end[] for each start. */
static int packed(int *starts, int n, int *end)
{
    for (int i = 0; i < n; i++)                       /* sort and dedupe */
        for (int j = i + 1; j < n; j++) if (starts[j] < starts[i]) { int t = starts[i]; starts[i] = starts[j]; starts[j] = t; }
    int m = 0;
    for (int i = 0; i < n; i++) if (!m || starts[i] != starts[m - 1]) starts[m++] = starts[i];
    int low = 0x7FFF;
    for (int i = 0; i < m; i++) {
        int o = starts[i];
        while (o < (i + 1 < m ? starts[i + 1] : low)) { if (aw(o) < low) low = aw(o); o += 2; }
        end[i] = o;
    }
    return m;
}

static int table_end(const int *starts, const int *end, int m, int start)
{
    for (int i = 0; i < m; i++) if (starts[i] == start) return end[i];
    return start;
}

static void anim_walk(void)
{
    if (anim_n) return;
    static int s1[64], s2[512], s3[512], e2[512], e3[512];
    int n1 = aw(0) / 2;
    for (int i = 0; i < n1; i++) s1[i] = aw(2 * i);
    int c2 = 0;
    for (int i = 0; i < n1; i++) s2[c2++] = s1[i];
    int m2 = packed(s2, c2, e2);
    int c3 = 0;
    for (int i = 0; i < m2; i++) for (int o = s2[i]; o < e2[i] && c3 < 512; o += 2) s3[c3++] = aw(o);
    int m3 = packed(s3, c3, e3);
    for (int s = 0; s < n1; s++) {
        int g0 = aw(2 * s), g1 = table_end(s2, e2, m2, g0);
        for (int g = 0; g0 + 2 * g < g1; g++) {
            int a0 = aw(g0 + 2 * g), a1 = table_end(s3, e3, m3, a0);
            for (int a = 0; a0 + 2 * a < a1 && anim_n < 2048; a++) {
                anim_sga[anim_n][0] = (uint8_t)(2 * s); anim_sga[anim_n][1] = (uint8_t)(2 * g);
                anim_sga[anim_n][2] = (uint8_t)(2 * a); anim_n++;
            }
        }
    }
    fprintf(stderr, "fuzz: %d animations\n", anim_n);
}

/* the script address of animation k, and the item addresses in it */
static uint32_t anim_script(int k)
{
    const uint8_t *t = anim_sga[k];
    uint32_t l2 = ANIM_DATA + (uint32_t)aw(t[0]);
    uint32_t l3 = ANIM_DATA + (uint32_t)(int16_t)bus_read16(l2 + t[1]);
    return ANIM_DATA + (uint32_t)(int16_t)bus_read16(l3 + t[2]);
}

/* a mapping used by some frame of a random animation */
static uint32_t random_mapping(void)
{
    anim_walk();
    for (;;) {
        uint32_t p = anim_script((int)(rnd() % (unsigned)anim_n)), maps[64]; int n = 0;
        for (int i = 0; i < 64; i++) {
            unsigned b = bus_read8(p);
            if (b >= 0xF0) { unsigned k = b & 15; p += 1 + (k >= 10 && k <= 12 ? 2 : 0); if (k == 13 || k == 15) break; continue; }
            maps[n++] = ANIM_DATA + (uint32_t)(int16_t)bus_read16(p + 1);
            p += 3;
            if (!b) break;
        }
        if (n) return maps[rnd() % (unsigned)n];
    }
}

static void put_word(uint32_t a, uint32_t v) { ram[a & 0xFFFF] = (uint8_t)(v >> 8); ram[(a + 1) & 0xFFFF] = (uint8_t)v; }
static void put_long(uint32_t a, uint32_t v) { put_word(a, v >> 16); put_word(a + 2, v); }
static int32_t get_sword(uint32_t a) { return (int16_t)(ram[a & 0xFFFF] << 8 | ram[(a + 1) & 0xFFFF]); }
static int32_t small(int n) { return (int32_t)(rnd() % (unsigned)(2 * n + 1)) - n; }

/* collision type of the block at pixel (x, y) of layer a3 as entity e sees it, via the hand port */
void TileAttrAtPixel(Cpu *), TileTypeOf(Cpu *);
void call_68k(Cpu *, uint32_t);                 /* port/world.c: set up an enemy with the game's code */
static uint32_t type_at(uint32_t layer, uint32_t e, uint32_t x, uint32_t y)
{
    Cpu c = {0};
    c.a[3] = 0xFFFF0000u | layer; c.a[4] = e; c.a[7] = m68k_get_reg(NULL, M68K_REG_SP);
    c.d[6] = x & 0xFFFF; c.d[7] = y & 0xFFFF;
    TileAttrAtPixel(&c);
    return c.d[2] & 0xFFFF;
}

/* A scene for the movement-with-collision routines: entity e standing on or moving into a slope
 * or solid block of the layer, with a small velocity, and the six moving platforms (records at
 * a2 = 0, 4 .. 20: flags $FF9F8E, attribute $FF9F8F, X/Y $FF9FE2/4, velocity $FFA036/8, half
 * width/height $FFA08A/C) placed beside, above or below it. Fully random fields almost never
 * line edges up, which is what those paths test. */
/* overwrite the attribute of the block at pixel (x, y) of layer a3: attributes live in RAM, 256
 * bytes after each block table (docs/NOTES.md, Blocks), and the case's RAM is restored after */
static void set_block_attr(uint32_t layer, uint32_t e, uint32_t x, uint32_t y, uint8_t attr)
{
    Cpu c = {0};
    c.a[3] = 0xFFFF0000u | layer; c.a[4] = e; c.a[7] = m68k_get_reg(NULL, M68K_REG_SP);
    c.d[6] = x & 0xFFFF; c.d[7] = y & 0xFFFF;
    TileAttrAtPixel(&c);
    uint32_t block = c.d[0] & 0xFFFF, t = layer + 0x10 + ((block >> 6) & 0xC);
    uint32_t table = (uint32_t)get_sword(t) << 16 | (uint16_t)get_sword(t + 2);
    if ((table & 0xFF0000) == 0xFF0000) ram[(table + 0x800 + (block & 0xFF)) & 0xFFFF] = attr;
}

static long scenes, on_slope, on_solid;          /* how often the scene found what it looked for */
static void collision_scene(const char *name, uint32_t layer, uint32_t cols, uint32_t rows, uint32_t e)
{
    uint32_t hw = 4 + rnd() % 12, hh = 4 + rnd() % 28;
    ram[0xCB02 + e] = (uint8_t)hw; ram[0xCB03 + e] = (uint8_t)hh;
    int32_t x = get_sword(0xC800 + e), y = get_sword(0xC900 + e);
    uint32_t prev_type = rnd();
    int want_slope = rnd() % 2; scenes++;
    for (int t = 0; t < 400; t++) {                 /* the feet in a slope or solid block */
        uint32_t bx = 0x1000 + (rnd() % cols) * 16, by = 0x1000 + (rnd() % rows) * 16;
        uint32_t ty = type_at(layer, e, bx + 8, by + 8);
        if (want_slope ? (ty & 0x108) == 0x108 : (ty & 8) != 0) {
            x = (int32_t)(bx + rnd() % 16); y = (int32_t)(by + rnd() % 24) - 4 - (int32_t)hh;
            if (rnd() % 2) prev_type = ty;
            if (want_slope) on_slope++; else on_solid++;
            break;
        }
    }
    if (want_slope && rnd() % 2) {
        /* rooms without slopes (most): make the block at the feet a slope, and its neighbours
         * above and below sometimes too. Attributes live in RAM, 256 bytes after each block
         * table (docs/NOTES.md, Blocks), and the case's RAM is restored afterwards. */
        static uint8_t slopes[256]; static int ns = -1;
        if (ns < 0) {
            ns = 0;
            /* attributes $80-$FF index past the type table into code: slope-like garbage that
             * sends $3860's jump table out of range, never used by the game */
            for (uint32_t a = 0; a < 0x80; a++) {
                Cpu c = {0}; c.a[4] = 0xFFFF; c.d[1] = a; TileTypeOf(&c);
                if ((c.d[2] & 0x108) == 0x108) slopes[ns++] = (uint8_t)a;
            }
        }
        x = (int32_t)(0x1000 + (rnd() % cols) * 16 + rnd() % 16);
        int32_t by = (int32_t)(0x1000 + (rnd() % rows) * 16);
        y = by + (int32_t)(rnd() % 24) - 4 - (int32_t)hh;
        uint8_t shape = slopes[rnd() % (unsigned)ns];
        for (int32_t dy = -16; dy <= 16; dy += 16) {
            if (dy && rnd() % 2) continue;
            uint8_t a = rnd() % 2 ? shape : slopes[rnd() % (unsigned)ns];
            set_block_attr(layer, e, (uint32_t)x, (uint32_t)(by + dy + 8), a);
            if (!dy && rnd() % 2) { Cpu t = {0}; t.a[4] = e; t.d[1] = a; TileTypeOf(&t); prev_type = t.d[2]; }
        }
        on_slope++;
    }
    put_word(0xC800 + e, (uint32_t)x); put_word(0xC900 + e, (uint32_t)y);
    ram[0xCA00 + e] = (uint8_t)small(8); ram[0xCA02 + e] = (uint8_t)small(8);       /* velocity */
    put_word(0xD200 + e, (uint32_t)(x - small(8))); put_word(0xD202 + e, (uint32_t)(y - small(8)));
    put_word(0xD102 + e, prev_type);                /* the previous collision type */
    for (uint32_t d = 0; d < 4; d += 2) put_word(0xD000 + e + d, rnd() % 3 ? 0 : (uint32_t)small(16));
    ram[0x9F0B] = (uint8_t)rnd();
    for (uint32_t p = 0; p < 24; p += 4) {
        ram[0x9F8E + p] = (uint8_t)(rnd() % 3 ? 0x80 | rnd() : rnd() & 0x7F);
        uint32_t attr = rnd() & 0x7F;
        for (int t = 0; t < 32 && rnd() % 4; t++) {  /* mostly an attribute that blocks some side */
            Cpu c = {0}; c.a[4] = e; c.d[1] = attr = rnd() & 0x7F; TileTypeOf(&c);
            if (c.d[2] & 0xF) break;
        }
        ram[0x9F8F + p] = (uint8_t)attr;
        uint32_t pw = 4 + rnd() % 28, ph = 4 + rnd() % 12;
        int32_t px = x + small(16), py = y + small(16);
        switch (rnd() % 3) {                         /* edges touching, or near */
        case 0: px = x + (rnd() % 2 ? 1 : -1) * (int32_t)(hw + pw) + small(4); break;
        case 1: py = y + (rnd() % 2 ? 1 : -1) * (int32_t)(hh + ph) + small(4); break;
        }
        put_word(0x9FE2 + p, (uint32_t)px); put_word(0x9FE4 + p, (uint32_t)py);
        put_word(0xA036 + p, rnd() % 3 ? (uint32_t)small(4) : 0); put_word(0xA038 + p, rnd() % 3 ? (uint32_t)small(4) : 0);
        put_word(0xA08A + p, pw); put_word(0xA08C + p, ph);
    }
    uint32_t on = (rnd() % 6) * 4;                   /* the platform the entity stands on */
    put_word(0xD300 + e, on);
    if (rnd() % 4 == 0) {                            /* another platform with the same top */
        uint32_t p = (rnd() % 6) * 4;
        put_word(0x9FE4 + p, (uint32_t)(get_sword(0x9FE4 + on) - get_sword(0xA08C + on) + get_sword(0xA08C + p)));
    }
    put_word(0x9EE4, rnd() % 2 ? 0xFFFF : rnd() % 32); put_word(0x9EE6, rnd() % 32);
    /* the per-platform routines called directly: a2 and the edges their callers compute */
    if (!strcmp(name, "g_0030CE")) {
        /* a pixel at the feet, often as the player on a tile whose type the player's equipment
         * overrides ($5xxx: attributes $09, $5E; $6xxx: $0A, $5F; none in the rooms with states) */
        static const uint8_t over[] = {0x09, 0x5E, 0x0A, 0x5F};
        uint32_t px = (uint32_t)x, py = (uint32_t)(y + (int32_t)hh);
        if (rnd() % 2) set_block_attr(layer, e, px, py, over[rnd() % 4]);
        m68k_set_reg(M68K_REG_D6, (rnd() << 16) | (px & 0xFFFF));
        m68k_set_reg(M68K_REG_D7, (rnd() << 16) | (py & 0xFFFF));
    }
    uint32_t a2 = (rnd() % 6) * 4;
    if (!strcmp(name, "g_003B74") || !strcmp(name, "g_003E00")) {
        m68k_set_reg(M68K_REG_A2, a2);
        m68k_set_reg(M68K_REG_D4, (uint32_t)(x - get_sword(0xD200 + e)) & 0xFFFF);
        m68k_set_reg(M68K_REG_D5, (uint32_t)(x + (int32_t)hw) & 0xFFFF);
        m68k_set_reg(M68K_REG_D6, (uint32_t)(x - (int32_t)hw) & 0xFFFF);
    } else if (!strcmp(name, "g_003D06")) {
        m68k_set_reg(M68K_REG_A2, a2);
        m68k_set_reg(M68K_REG_D4, (uint32_t)(y - get_sword(0xD202 + e)) & 0xFFFF);
        m68k_set_reg(M68K_REG_D5, (uint32_t)(y + (int32_t)hh) & 0xFFFF);
        m68k_set_reg(M68K_REG_D6, (uint32_t)(y - (int32_t)hh) & 0xFFFF);
    }
}

/* --call NAME: run one ported or translated routine once on the machine as it is (a debugging
 * and state-editing aid, e.g. g_008AA2 redraws the hearts after editing the health). The
 * registers are put back afterwards, so the interrupted 68000 code carries on unaware; memory
 * changes stay. Works in any port mode. */
int hybrid_call(const char *name)
{
    int n = port_routine_count + gen_routine_count;
    for (int i = 0; i < n; i++) {
        const PortRoutine *r = i < port_routine_count ? &port_routines[i] : &gen_routines[i - port_routine_count];
        if (strcmp(r->name, name)) continue;
        Cpu saved, c; get_cpu(&saved); c = saved;
        c.a[7] -= 4; wr32(c.a[7], 0);              /* a return address for a generated routine's rts */
        r->fn(&c);
        set_cpu(&saved);
        fprintf(stderr, "called %s ($%06X)\n", name, (unsigned)r->addr);
        return 0;
    }
    fprintf(stderr, "--call: no routine named %s\n", name);
    return -1;
}

int hybrid_fuzz(const char *name, long count)
{
    int idx = -1;
    for (int i = 0; i < NR; i++) if (!strcmp(R(i)->name, name)) idx = i;
    if (idx < 0 || mode != PORT_SHADOW) { fprintf(stderr, "fuzz: %s is not a ported routine (or not in shadow mode)\n", name); return -1; }
    size_t n; const uint8_t *s = md_snapshot(&n);
    uint8_t *base = malloc(n); memcpy(base, s, n);
    long before = stats[idx].bad;
    fuzzing = 1;
    for (long k = 0; k < count; k++) {
        md_restore(base, n); ft_k = k + 1;
        for (int i = 0; i < 8; i++) m68k_set_reg(M68K_REG_D0 + i, rnd() << 16 ^ rnd());
        m68k_set_reg(M68K_REG_A4, (rnd() & 63) * 4);
        m68k_set_reg(M68K_REG_SR, (m68k_get_reg(NULL, M68K_REG_SR) & ~0x1Fu) | (rnd() & 0x1F));
        if (strncmp(name, "Obj", 3) && strncmp(name, "Ppl", 3) && strncmp(name, "Hnd", 3) && strncmp(name, "Act", 3))   /* these keep the state's entities (below) */
            for (uint32_t a = 0xC000; a < 0xE000; a++) ram[a] = (uint8_t)rnd();
        for (uint32_t a = 0x8A52; a < 0x8A56; a++) ram[a] = (uint8_t)(rnd() & (k & 1 ? 0xFF : 0));  /* seed 0 half the time */
        if (!strcmp(name, "SetGfxSlots")) {         /* a random valid slot list in scratch RAM */
            uint32_t p = 0x4E00;                    /* restored after every case, so any RAM will do */
            int slot = (int)(rnd() % 48);
            ram[p++] = (uint8_t)(0x80 | slot);
            for (int cmds = 1 + (int)(rnd() % 8); cmds-- && slot < 56; ) {
                switch (rnd() % 5) {
                case 0: ram[p++] = (uint8_t)(rnd() & 0x7F); ram[p++] = (uint8_t)rnd(); slot++; break;
                case 1: slot = (int)(rnd() % 48); ram[p++] = (uint8_t)(0x80 | slot); break;
                case 2: ram[p++] = 0xC0; slot++; break;
                case 3: { int n = 1 + (int)(rnd() % 4); ram[p++] = (uint8_t)(0xC0 | n); ram[p++] = (uint8_t)rnd(); ram[p++] = (uint8_t)rnd(); slot += n; break; }
                case 4: { int n = (int)(rnd() % 4); ram[p++] = (uint8_t)(0xF8 | n); slot += n + 1; break; }
                }
            }
            ram[p] = 0xFF;
            m68k_set_reg(M68K_REG_A0, 0xFFFF4E00);
        }
        if (!strcmp(name, "CopyRawTiles")) {       /* a tile of a raw block (or 0: clear), any count */
            static const int raw[] = {35, 96, 97, 98, 99, 100, 101, 102, 103, 246, 256, 257, 258, 259, 260, 261, 262, 263, 368, 369};
            uint32_t tile = rnd() % 8 ? (uint32_t)raw[rnd() % 20] * 32 + rnd() % 32 : 0;
            m68k_set_reg(M68K_REG_D0, (rnd() << 16) | (rnd() & 0xFFFE));
            m68k_set_reg(M68K_REG_D1, (rnd() << 16) | tile);
            m68k_set_reg(M68K_REG_D2, (rnd() << 16) | (rnd() % 64 + 1));
        }
        if (!strcmp(name, "StreamGfxBlocks")) {    /* random wanted and loaded slot tables */
            for (int i = 0; i < 64; i++) {
                uint32_t want = rnd() % 4 ? rnd() % 768 : 0xFFFF;
                uint32_t have = rnd() % 4 ? want : rnd() % 768;
                ram[0x8E08 + 2 * i] = (uint8_t)(want >> 8); ram[0x8E09 + 2 * i] = (uint8_t)want;
                ram[0x8E88 + 2 * i] = (uint8_t)(have >> 8); ram[0x8E89 + 2 * i] = (uint8_t)have;
            }
        }
        if (!strcmp(name, "AddEntitySprites") || !strcmp(name, "BuildSprites")) {
            /* real mappings and animations; camera, window and budget around realistic values */
            anim_walk();
            for (uint32_t i = 0; i < 4; i++) put_word(0x8B34 + 2 * i, 0xFF00 + (rnd() & 0x1FF));
            put_word(0x8B28, 0x60 + rnd() % 0x40); put_word(0x8B2A, 0x140 + rnd() % 0x40);
            put_word(0x8B2E, 0x160 + rnd() % 0x40);
            ram[0x8B30] = (uint8_t)(rnd() % 40); ram[0x8B31] = (uint8_t)(1 + rnd() % 64);
            put_word(0x8AE4, rnd() % 3 ? 0 : rnd()); ram[0x8A77] = (uint8_t)rnd();
            for (uint32_t e = 0; e < 0x100; e += 4) {
                put_word(0xC200 + e, (rnd() % 2) * 4);                      /* camera index */
                put_word(0xC800 + e, (uint32_t)(0x80 + rnd() % 0x180 - 0x100 + (rnd() & 0x1FF)));
                put_word(0xC900 + e, (uint32_t)(0x80 + rnd() % 0x140 - 0x100 + (rnd() & 0x1FF)));
                put_long(0xC100 + e, random_mapping());
                ram[0xC303 + e] = (uint8_t)((rnd() % 16) * 2);                /* sprite bucket */
                const uint8_t *t = anim_sga[rnd() % (unsigned)anim_n];
                ram[0xC300 + e] = t[0]; ram[0xC301 + e] = t[1]; ram[0xC302 + e] = t[2];
                uint32_t sc = anim_script((int)(t - anim_sga[0]) / 3);
                put_long(0xC400 + e, sc); put_long(0xC500 + e, sc);
            }
            if (!strcmp(name, "AddEntitySprites")) m68k_set_reg(M68K_REG_A3, 0x00FF2800 + (rnd() % 64) * 8);
        }
        if (strstr(name, "Overlap") || strstr(name, "Body") || strstr(name, "Contact")) {
            /* two entities close together, with small random box lists (or none) */
            uint32_t a4 = m68k_get_reg(NULL, M68K_REG_A4), a2 = (rnd() & 63) * 4;
            m68k_set_reg(M68K_REG_A2, a2);
            put_word(0xC800 + a2, (uint32_t)(int32_t)(int16_t)(ram[0xC800 + a4] << 8 | ram[0xC801 + a4]) + rnd() % 96 - 48);
            put_word(0xC900 + a2, (uint32_t)(int32_t)(int16_t)(ram[0xC900 + a4] << 8 | ram[0xC901 + a4]) + rnd() % 96 - 48);
            uint32_t p = 0x4E00;
            uint32_t ents[2] = {a2, a4};
            for (int e = 0; e < 2; e++)
                for (int list = 0; list < 2; list++) {              /* -$2A00 attack, -$2900 hurt */
                    uint32_t field = (list ? 0xD700 : 0xD600) + ents[e];
                    if (rnd() % 8 == 0) { put_long(field, 0); continue; }
                    put_long(field, 0x00FF0000 + p);
                    int n = 1 + (int)(rnd() % 4);
                    ram[p++] = (uint8_t)n;
                    for (int b = 0; b < n; b++) {
                        ram[p++] = (uint8_t)(rnd() % 64 - 32); ram[p++] = (uint8_t)(rnd() % 64 - 32);
                        ram[p++] = (uint8_t)(rnd() % 24); ram[p++] = (uint8_t)(rnd() % 24);
                    }
                }
            for (int e = 0; e < 2; e++) {
                ram[0xCB00 + ents[e]] = (uint8_t)(rnd() % 32); ram[0xCB01 + ents[e]] = (uint8_t)(rnd() % 32);
            }
            if (!strcmp(name, "BoxPairOverlap") || !strcmp(name, "BoxVsBody")) {
                m68k_set_reg(M68K_REG_A0, 0x00FF4E01); m68k_set_reg(M68K_REG_A1, 0x00FF4E05);
                for (uint32_t q = 0x4E00; q < 0x4E10; q++) ram[q] = (uint8_t)(rnd() % 64 - 32);
            }
        }
        int generated = !strncmp(name, "g_", 2)
                     || (R(idx)->contract && R(idx)->addr >= 0x2E38 && R(idx)->addr < 0x3F54);   /* movement.c */
        int movement = generated && strncmp(name, "g_", 2);
        if (strstr(name, "MapBlock") || strstr(name, "Tile") || generated) {
            /* a position on (or just off) a real layer, sometimes as the player */
            uint32_t layer = rnd() % 4 || movement ? 0x9754 : 0x97D4;   /* movement.c: always plane A */
            if (rnd() % 2) {                        /* a synthetic layer: up to 8x8 chunks */
                ram[layer] = (uint8_t)rnd();
                put_word(layer + 0x44, rnd() % 128); put_word(layer + 0x46, (rnd() % 96) * 64);
                uint32_t map = (uint32_t)(ram[layer + 0x22] << 8 | ram[layer + 0x23]) & 0xFFFF;
                for (uint32_t i = 0; i < 64; i++) ram[(map + i) & 0xFFFF] = (uint8_t)(rnd() % 8 ? rnd() % 32 : 0xFF);
            }
            uint32_t cols = (uint32_t)(ram[layer + 0x44] << 8 | ram[layer + 0x45]) + 1;
            uint32_t rows = (uint32_t)(ram[layer + 0x46] << 8 | ram[layer + 0x47]) / 64 + 1;
            uint32_t col = rnd() % (cols + 8) - 4, row = rnd() % (rows + 8) - 4;
            m68k_set_reg(M68K_REG_A3, 0xFFFF0000 | layer);
            if (rnd() % 3 == 0) m68k_set_reg(M68K_REG_A4, (uint32_t)(int32_t)(int16_t)(ram[0x9EEE] << 8 | ram[0x9EEF]));
            ram[0x9EF1] = (uint8_t)rnd();
            if (!strcmp(name, "MapBlockAtRaw")) {
                m68k_set_reg(M68K_REG_D2, (rnd() << 16) | (col % cols));
                m68k_set_reg(M68K_REG_D3, (rnd() << 16) | ((row % rows) * 64));
            } else if (!strcmp(name, "TileAttrAtPixel")) {
                m68k_set_reg(M68K_REG_D6, (rnd() << 16) | ((0x1000 + col * 16 + rnd() % 16) & 0xFFFF));
                m68k_set_reg(M68K_REG_D7, (rnd() << 16) | ((0x1000 + row * 16 + rnd() % 16) & 0xFFFF));
            } else if (!strcmp(name, "TileTypeOf")) {
                m68k_set_reg(M68K_REG_D1, rnd());
            } else if (strstr(name, "Probe") || generated) {     /* an entity on the layer, moving */
                uint32_t e = m68k_get_reg(NULL, M68K_REG_A4);
                if (generated) {
                    ram[0x966B] = (uint8_t)rnd();                /* area flags the collision code tests */
                    put_long(0x9EE8, rnd() % 2 ? 0xFFFFFFFF : 0x38E4);  /* the stored continuation */
                    /* $3860 jumps through an 11-entry table by the collision type's high nibble;
                     * entry 0 is a deliberate hang (nop; bra *), never reached in the game */
                    m68k_set_reg(M68K_REG_D2, (rnd() << 16) | ((1 + rnd() % 10) << 4) | (rnd() & 0xF00F));
                }
                put_word(0xC800 + e, 0x1000 + col * 16 + rnd() % 16);
                put_word(0xC900 + e, 0x1000 + row * 16 + rnd() % 16);
                if (generated && rnd() % 4) collision_scene(name, layer, cols, rows, e);
            } else {
                m68k_set_reg(M68K_REG_D6, (rnd() << 16) | ((0x100 + col) & 0xFFFF));
                m68k_set_reg(M68K_REG_D7, (rnd() << 16) | ((0x4000 + row * 64) & 0xFFFF));
            }
        }
        if (!strncmp(name, "Player", 6)) {
            /* the player, in a valid state and form, with random buttons and ground; often at a
             * ladder or vine of the map, or by a platform carrying a vine */
            uint32_t e = (uint32_t)(int32_t)(int16_t)(ram[0x9EEE] << 8 | ram[0x9EEF]);
            m68k_set_reg(M68K_REG_A4, e);
            uint32_t layer = 0x9754;
            uint32_t cols = (uint32_t)(ram[layer + 0x44] << 8 | ram[layer + 0x45]) + 1;
            uint32_t rows = (uint32_t)(ram[layer + 0x46] << 8 | ram[layer + 0x47]) / 64 + 1;
            collision_scene(name, layer, cols, rows, e & 0xFF);
            ram[0x966B] = (uint8_t)rnd(); ram[0x9EF1] = (uint8_t)rnd();
            ram[0xCE01 + e] = (uint8_t)(rnd() % (strcmp(name, "PlayerAnimate") ? 10 : 9));   /* the state, -$31FF ($F080 has 9) */
            ram[0x9F04] = (uint8_t)(rnd() % 3);                         /* the form */
            ram[0x9F0B] = (uint8_t)rnd();
            ram[0x9F0D] = (uint8_t)(rnd() & (rnd() % 2 ? ram[0x9F0B] : 0xFF));
            ram[0x9F03] = (uint8_t)rnd(); ram[0x9F02] = (uint8_t)rnd(); ram[0x9F06] = (uint8_t)rnd();
            for (uint32_t a = 0x9F07; a <= 0x9F09; a++) ram[a] = (uint8_t)(rnd() % 3 ? 0 : rnd() % 8);
            ram[0x9F0E] = (uint8_t)(rnd() % 4); ram[0x9F12] = (uint8_t)(rnd() % 40);
            put_word(0x9EF2, (uint32_t)(-0x300 + small(0x100))); put_word(0x9EF4, (uint32_t)(rnd() % 5));
            static const uint16_t tiles[] = {0, 0x4000, 0x4008, 0x7000, 0x8000, 0xA000, 0x1008, 0x3000, 0x9008, 0x5000};
            put_word(0x9EDE, tiles[rnd() % 10]); put_word(0x9EE0, tiles[rnd() % 10]); put_word(0x9EE2, tiles[rnd() % 10]);
            ram[0x95CE] = (uint8_t)(rnd() % 8);
            static const uint32_t items[] = {0xA5AA, 0xA60A, 0xA68E, 0xA898, 0xA8A6, 0xA8D8, 0xA902, 0xADAC, 0x1822A};   /* the item routines the game stores */
            ram[0x9980] = (uint8_t)(rnd() % 2); ram[0x9981] = (uint8_t)(rnd() % 2);
            put_long(0x998A, items[rnd() % 9]); put_long(0x998E, items[rnd() % 9]);
            static const uint32_t scripts[] = {0xF5C8, 0xF594, 0xF530, 0xF4F6, 0xF55E};
            put_long(0x9F2C, scripts[rnd() % 5]); put_word(0x9F2A, rnd() % 3 ? rnd() % 4 : 0xFFFF);
            ram[0xD400 + e] = (uint8_t)(rnd() % 4);                     /* the attack step, -$2C00 */
            if (rnd() % 2) { ram[0xD800 + e] = 0; ram[0xD801 + e] = (uint8_t)(rnd() % 10); }   /* near a rung's end */
            /* the rest of the player's frame: input modes and scripts, the pad, what it stands on
             * (ice, icy slopes), poses, shields, the timers */
            static const uint8_t modes[] = {0, 0, 2, 4, 6, 8};
            static const uint32_t scripts_in[] = {0xDB36, 0xD352, 0xD87E};
            ram[0x9F05] = modes[rnd() % 6]; put_long(0x9F22, scripts_in[rnd() % 3]);
            ram[0x8A7A] = (uint8_t)rnd(); ram[0x9F0C] = (uint8_t)rnd();
            static const uint16_t grounds[] = {0, 0x0208, 0x0308, 0x0318, 0x0368, 0x0119, 0x1008, 0x9008};
            put_word(0xCF02 + e, rnd() % 4 ? grounds[rnd() % 8] : rnd());
            static const uint8_t poses[] = {0, 0, 2, 0x80, 0x82};
            ram[0x9F0F] = poses[rnd() % 5];
            ram[0x95D0] = (uint8_t)(rnd() % 4 ? rnd() % 6 : 0x80 | rnd());
            ram[0x9F0A] = (uint8_t)(rnd() % 3 ? 0 : rnd() % 4);
            if (rnd() % 4 == 0) put_word(0xC800 + e, 0x1140 + small(4));          /* the walk's end */
            /* batch 2: the special actions for A + left/right (13, or none), the animation
             * playing and whether it runs on, the sprite flags */
            ram[0x9F10] = (uint8_t)(rnd() % 4 ? rnd() % 13 : 0x80 | rnd());
            ram[0x9F11] = (uint8_t)(rnd() % 4 ? rnd() % 13 : 0x80 | rnd());
            ram[0x9F7A] = (uint8_t)(rnd() % 4 ? 0 : 0x80);
            ram[0xC600 + e] = (uint8_t)(rnd() % 2 ? 0 : rnd());                  /* -$3A00 */
            static const uint8_t anims[] = {0, 2, 4, 6, 8, 0xA, 0xE, 0x10, 0x12, 0x14, 0x18, 0x1A};
            ram[0xC302 + e] = anims[rnd() % 12];                                  /* -$3CFE */
            /* batch 3: a pickup's kind, being hit, harm from outside, health near the end (so
             * the revival item and death are reached), the tile above */
            if (!strcmp(name, "PlayerGold")) m68k_set_reg(M68K_REG_D0, (rnd() % 12) * 2);
            static const uint8_t hits[] = {0, 0x80, 0x81, 0x10, 0x40, 0x50, 0x90};
            ram[0xD503 + e] = rnd() % 4 ? hits[rnd() % 7] : (uint8_t)rnd();        /* -$2AFD */
            ram[0x9854] = (uint8_t)(rnd() % 3 ? rnd() & 3 : 0);
            put_word(0x9856, rnd() % 0x100);
            put_word(0xDA00 + e, rnd() % 2 ? 0x40 + rnd() % 0x100 : rnd() % 0x800);   /* health, -$2600 */
            put_word(0x9F00, 0x200 + (rnd() % 8) * 0x100);
            put_word(0xDC02 + e, rnd() % 0x200);                                 /* the hit's damage */
            put_word(0x9EDC, tiles[rnd() % 10]);
            ram[0x95D1] = (uint8_t)(rnd() % 4);
            ram[0x959D] = (uint8_t)(rnd() % 2 ? 0 : 1 + rnd() % 3);              /* revival items ($23E4: item $2A) */
            put_long(0x962C, rnd() % 3 ? rnd() % 1000000 : 999999 - rnd() % 300);   /* gold, near the cap */
            if (rnd() % 2) {                        /* at a ladder or vine: painted into the map */
                static uint8_t ladders[16]; static int nl = -1;  /* attributes of type $4xxx */
                if (nl < 0) {
                    nl = 0;
                    for (uint32_t a = 0; a < 0x80 && nl < 16; a++) {
                        Cpu t = {0}; t.a[4] = e; t.d[1] = a; TileTypeOf(&t);
                        if ((t.d[2] & 0xF000) == 0x4000) ladders[nl++] = (uint8_t)a;
                    }
                }
                int32_t x = get_sword(0xC800 + e), y = get_sword(0xC900 + e);
                int32_t hh = ram[(0xCB03 + e) & 0xFFFF], grip = (int8_t)(uint8_t)(hh - ram[0x9F12]);
                for (int32_t dy = -16; nl && dy <= 16; dy += 16) {          /* at the hands and feet */
                    if (rnd() % 3 == 0) continue;
                    set_block_attr(layer, e, (uint32_t)x, (uint32_t)(y + grip + dy), ladders[rnd() % (unsigned)nl]);
                    if (rnd() % 2) set_block_attr(layer, e, (uint32_t)x, (uint32_t)(y + hh + dy), ladders[rnd() % (unsigned)nl]);
                }
            }
            if (rnd() % 4 == 0) {                   /* a platform carrying a vine, at the player */
                uint32_t p = (rnd() % 6) * 4, attr = 0;
                for (uint32_t a = 0; a < 0x80; a++) {
                    Cpu t = {0}; t.a[4] = e; t.d[1] = a; TileTypeOf(&t);
                    if ((t.d[2] & 0xFFFF) == 0x4000) { attr = a; break; }
                }
                ram[0x9F8E + p] = 0x80; ram[0x9F8F + p] = (uint8_t)attr;
                put_word(0x9FE2 + p, (uint32_t)(get_sword(0xC800 + e) + small(6)));
                put_word(0x9FE4 + p, (uint32_t)(get_sword(0xC900 + e) + small(20)));
                put_word(0xD300 + e, p);
                ram[0xD101 + e] |= (uint8_t)(rnd() % 2 ? 0x20 : 0);  /* PREV_COLL: on it */
            }
        }
        if (!strncmp(name, "Magic", 5)) {
            /* magic (port/magic.c): a kind and its state, enemies round the player, the magic's
             * own entities part-way through; where the original resets the stack to the task's
             * base ($FF9662) and yields, that base is this case's return address */
            uint32_t e = (uint32_t)(int32_t)(int16_t)(ram[0x9EEE] << 8 | ram[0x9EEF]);
            m68k_set_reg(M68K_REG_A4, e);
            put_long(0x9662, m68k_get_reg(NULL, M68K_REG_SP) - 4);
            int start = !strcmp(name, "MagicStart");
            static const uint8_t frames[] = {0, 1, 2, 3, 4, 5, 6, 7, 0, 2, 3, 4, 0, 2};
            uint8_t kind = start ? (uint8_t)(rnd() % 13 | (rnd() % 8 ? 0 : 0x80)) : frames[rnd() % 14];   /* 13-127: off the table */
            if (start) m68k_set_reg(M68K_REG_D0, (rnd() << 16) | kind);
            ram[0x9F7A] = start ? (uint8_t)(rnd() % 4 ? 0 : 0x80) : (uint8_t)(rnd() % 8 ? 0x80 | (rnd() & 1) : rnd() & 1);
            ram[0x9F7B] = start ? (uint8_t)rnd() : (uint8_t)(kind | (rnd() % 3 ? 0x80 : 0));
            ram[0x9F06] = (uint8_t)(rnd() % 3 ? rnd() & ~4u : rnd());
            put_word(0x9F16, rnd() % 4 ? rnd() % 0x2001 : rnd() % 3 ? rnd() % 16 : rnd());   /* the meter */
            put_word(0x9962, rnd() % 2 ? 0 : rnd() % 16);
            ram[0x9EF1] = (uint8_t)rnd(); ram[0x9659] = (uint8_t)rnd(); ram[0x9668] = (uint8_t)(rnd() % 5);
            ram[0x9F04] = (uint8_t)(rnd() % 3);
            ram[0x959E] = (uint8_t)(rnd() % 3 ? rnd() % 4 : rnd());               /* the potion */
            for (uint32_t k2 = 0; k2 < 13; k2++) ram[0x95B8 + k2] = (uint8_t)(rnd() % 4 ? 1 + rnd() % 5 : rnd());
            put_word(0x9F00, 0x200 + (rnd() % 16) * 0x100);
            put_word(0xDA00 + e, rnd() % 3 ? rnd() % 0x1000 : (uint32_t)get_sword(0x9F00) & 0xFFFF);   /* the player's health */
            ram[0x9F13] = (uint8_t)(rnd() % 2 ? rnd() : 0);
            ram[0x9F7C] = (uint8_t)(rnd() % 3 ? rnd() % 8 : rnd()); ram[0x9F7D] = (uint8_t)(rnd() % 3 ? rnd() % 8 : rnd());
            put_word(0x965A, rnd());
            put_word(0xA11A, 0x40 + (rnd() % 4) * 4);
            int32_t px = get_sword(0xC800 + e), py = get_sword(0xC900 + e);
            for (uint32_t a = 0x40; a < 0x80; a += 4) {                         /* enemies */
                ram[0xC000 + a] = (uint8_t)(rnd() % 3 ? 0xC1 | (rnd() & 0x3E) : rnd());
                ram[0xD402 + a] = (uint8_t)(rnd() % 3 ? 0 : rnd() % 4);
                put_word(0xDA00 + a, rnd() % 4 ? rnd() % 0x400 : rnd() % 2 ? 0 : rnd());
                put_word(0xC800 + a, (uint32_t)(px + small(0x80))); put_word(0xC900 + a, (uint32_t)(py + small(0x60)));
                put_word(0xDD00 + a, rnd()); ram[0xCF00 + a] = (uint8_t)rnd(); ram[0xC002 + a] = (uint8_t)rnd();
                ram[0xCB00 + a] = (uint8_t)(rnd() % 24); ram[0xCB01 + a] = (uint8_t)(rnd() % 24);
                ram[0xCB03 + a] = (uint8_t)(rnd() % 32);
            }
            put_word(0xDD02 + e, 0x40 + (rnd() % 16) * 4);                      /* who hit the player */
            for (uint32_t s = 0xC0; s < 0xE0; s += 4) {                         /* the magic's entities */
                uint32_t type = (rnd() % 4) * 2;
                static const uint16_t sets[4] = {0x1400, 0x1406, 0x1402, 0x1408};
                ram[0xC000 + s] = (uint8_t)(rnd() % 3 ? 0x80 | (rnd() & 0x41) : rnd() & 0x41);
                put_word(0xD402 + s, type);
                ram[0xD401 + s] = (uint8_t)((rnd() % 3) * 2 | (rnd() % 2 ? 0x80 : 0));
                put_word(0xC300 + s, type == 4 && ram[0x9F04] == 2 ? 0x1404 : sets[type / 2]);
                ram[0xC302 + s] = (uint8_t)(type == 4 ? (rnd() % 8) * 2 : (rnd() % 2) * 2);
                ram[0xD800 + s] = (uint8_t)(type == 6 ? rnd() % 4 : rnd() % 3 ? rnd() % 6 : rnd());   /* the bolt: 3 steps */
                ram[0xD801 + s] = (uint8_t)(rnd() % 3 ? 1 + rnd() % 3 : rnd());
                ram[0xD802 + s] = (uint8_t)rnd(); ram[0xD803 + s] = (uint8_t)rnd();
                put_word(0xD302 + s, rnd() % 3 ? 0x40 + (rnd() % 16) * 4 : 0xFFFF);
                ram[0xC600 + s] = (uint8_t)(rnd() % 2 ? 0 : rnd());
                put_word(0xC800 + s, (uint32_t)(px + small(0x40))); put_word(0xC900 + s, (uint32_t)(py + small(0x40)));
                put_word(0xCA00 + s, rnd()); put_word(0xCA02 + s, rnd());
                ram[0xCB00 + s] = (uint8_t)(rnd() % 16); ram[0xCB01 + s] = (uint8_t)(rnd() % 16);
            }
        }
        if (!strcmp(name, "PlayerSwordTip")) {      /* a list of the sword's points, as the game passes */
            uint32_t sword = rnd() % 8, list = 0xF2FC + (uint32_t)(int32_t)(int16_t)bus_read16(0xF2FC + 2 * sword);
            list += (uint32_t)(int32_t)(int16_t)bus_read16(list + 2 * (rnd() % (bus_read8(0xF2F4 + sword) ? bus_read8(0xF2F4 + sword) : 1)));
            m68k_set_reg(M68K_REG_A0, rnd() % 3 ? list : 0xE9F2 + 2 * (rnd() % 4));
            put_word(0x9F2A, rnd() % 4 ? rnd() % 16 : 0xFFFF);                 /* any step of the trail script */
            if (rnd() % 2) ram[0x9F03] |= 0x20;                                  /* power on */
            uint32_t a0 = m68k_get_reg(NULL, M68K_REG_A0), e = m68k_get_reg(NULL, M68K_REG_A4);
            if (bus_read8(a0) != 0x80 && rnd() % 2) {                           /* a breakable block (2) at the tip */
                uint8_t dx = (uint8_t)bus_read8(a0);
                if (ram[0xC002 + e] & 8) dx = (uint8_t)-(uint8_t)(dx + 1);
                set_block_attr(0x9754, e, (uint32_t)(get_sword(0xC800 + e) + (int8_t)dx),
                               (uint32_t)(get_sword(0xC900 + e) + (int8_t)bus_read8(a0 + 1)), 2);
            }
        }
        if (!strcmp(name, "Hearts")) {              /* health up to the maximum, up to 15 hearts */
            uint32_t e = (uint32_t)(int32_t)(int16_t)(ram[0x9EEE] << 8 | ram[0x9EEF]);
            uint32_t max = (1 + rnd() % 15) * 0x100;
            put_word(0x9F00, rnd() % 8 ? max : rnd());
            put_word(0xDA00 + e, rnd() % 8 ? rnd() % (max + 1) : rnd());
        }
        if (!strncmp(name, "SpawnItem", 9)) {
            /* a kind of the table (two groups: 15 and 12 kinds) or a big item, the slots often all
             * busy with kinds of every priority */
            static const uint16_t big[] = {0x1040, 0x102A, 0x1000, 0x1012};
            uint16_t kind = rnd() % 8 == 0 ? big[rnd() % 4] : rnd() % 2 ? (uint16_t)(2 * (rnd() % 15)) : (uint16_t)(0x200 + 2 * (rnd() % 12));
            m68k_set_reg(M68K_REG_D0, (rnd() << 16) | kind);
            m68k_set_reg(M68K_REG_D1, rnd()); m68k_set_reg(M68K_REG_D2, rnd());
            uint32_t a4 = (rnd() % 64) * 4;
            m68k_set_reg(M68K_REG_A4, a4);
            ram[0xD100 + a4] = (uint8_t)rnd();                                 /* -$2F00 */
            int busy = rnd() % 4 != 0;
            for (uint32_t s = 0x80; s < 0xC0; s += 4) {
                ram[0xC000 + s] = (uint8_t)(busy || rnd() % 2 ? 0x80 | (rnd() & 0x40) : 0);
                put_word(0xD402 + s, rnd() % 2 ? 2 * (rnd() % 15) : 0x200 + 2 * (rnd() % 12));
            }
            put_word(0x9962, rnd() % 2 ? 0 : rnd() % 16);
            ram[0x959E] = (uint8_t)(rnd() % 3 ? rnd() % 4 : rnd());
            ram[0x959D] = (uint8_t)(rnd() % 3);
            put_word(0x9F00, rnd() % 2 ? 0xE00 + (rnd() % 3) * 0x100 : 0x200 + (rnd() % 12) * 0x100);   /* the heart container's limit */
        }
        if (strstr(name, "Freeze") || !strcmp(name, "FindFreeEffect") || !strncmp(name, "ClearEntity", 11)
            || !strcmp(name, "CheckGrounded")) {    /* every entity's state byte, often in use */
            for (uint32_t e = 0; e < 0x100; e += 4) ram[0xC000 + e] = (uint8_t)(rnd() % 3 ? 0x80 | rnd() : rnd());
            if (!strcmp(name, "FindFreeEffect") && rnd() % 3 == 0)
                for (uint32_t e = 0xE0; e < 0x100; e += 4) ram[0xC000 + e] |= 0x80;   /* all busy */
            m68k_set_reg(M68K_REG_A4, (rnd() % 64) * 4);
        }
        static const char *maths[] = {"Cosine", "Sine", "VelocityAt", "VelocityAtCoarse", "Direction16To",
            "Direction16", "DirectionTo", "Direction", "SetVelocityAt", "VelocityTo", "SetVelocityToCoarse",
            "SetVelocityAtCoarse", "VelocityToCoarse", "Distance"};
        for (int m = 0; m < 14; m++) if (!strcmp(name, maths[m])) {
            /* angles anywhere, often on a quarter or eighth; speeds of every size; two
             * entities apart by any offset, often level, straight above or at 45 degrees */
            static const uint16_t edges[] = {0, 0x4000, 0x8000, 0xC000, 0x2000, 0x3FFF, 0x4001, 0x7FFF, 0xFFFF, 0x0400};
            m68k_set_reg(M68K_REG_D3, (rnd() << 16) | (rnd() % 3 ? rnd() & 0xFFFF : edges[rnd() % 10]));
            m68k_set_reg(M68K_REG_D4, (rnd() << 16) | (rnd() % 3 ? rnd() % 0x800 : rnd() & 0xFFFF));
            uint32_t a4 = (rnd() % 64) * 4, a2 = (rnd() % 64) * 4;
            m68k_set_reg(M68K_REG_A4, a4); m68k_set_reg(M68K_REG_A2, a2);
            int32_t dx = (int32_t)(rnd() % 4 ? rnd() % 512 : rnd() % 8) * (rnd() % 2 ? 1 : -1);
            int32_t dy = rnd() % 4 == 0 ? dx * (rnd() % 2 ? 1 : -1) : rnd() % 5 == 0 ? 0 : (int32_t)(rnd() % 512) * (rnd() % 2 ? 1 : -1);
            if (rnd() % 8 == 0) dx = 0;
            if (rnd() % 16 == 0) { dx = (int32_t)(rnd() & 0xFFFF); dy = (int32_t)(rnd() & 0xFFFF); }   /* anything */
            uint32_t x = rnd() & 0xFFFF, y = rnd() & 0xFFFF;
            put_word(0xC800 + a4, x); put_word(0xC900 + a4, y);
            if (a2 != a4) { put_word(0xC800 + a2, x + (uint32_t)dx); put_word(0xC900 + a2, y + (uint32_t)dy); }
            m68k_set_reg(M68K_REG_D0, rnd() % 4 ? (rnd() << 16) | (uint32_t)(dx & 0xFFFF) : rnd());
            m68k_set_reg(M68K_REG_D1, rnd() % 4 ? (rnd() << 16) | (uint32_t)(dy & 0xFFFF) : rnd());
        }
        if (!strcmp(name, "MagicUsed") || !strcmp(name, "SlotEmptied") || !strcmp(name, "MagicMeter") || !strcmp(name, "HudRedraw")
            || !strcmp(name, "GoldDisplay")) {     /* the HUD bar's state */
            uint32_t k = rnd() % 13;
            if (!strcmp(name, "MagicUsed")) m68k_set_reg(M68K_REG_D0, (rnd() << 16) | k);
            if (!strcmp(name, "SlotEmptied")) {     /* a special action or an item kind ($28, $2B up) */
                static const uint8_t ks[] = {0, 3, 7, 12, 0x28, 0x2B, 0x2C, 0x2F, 0x29, 0x2A};
                k = ks[rnd() % 10];
                m68k_set_reg(M68K_REG_D0, (rnd() << 16) | (rnd() << 8 & 0xFF00) | k);
            }
            if (!strcmp(name, "MagicMeter")) m68k_set_reg(M68K_REG_D0, (rnd() << 16) | (rnd() % 4 ? rnd() % 0x2001 : rnd() & 0xFFFF));
            for (uint32_t i = 0; i < 13; i++) ram[0x95B8 + i] = (uint8_t)(rnd() % 3 ? 1 + rnd() % 3 : rnd());
            ram[0x9F10] = (uint8_t)(rnd() % 4 ? rnd() % 13 : 0xFF); ram[0x9F11] = (uint8_t)(rnd() % 4 ? rnd() % 13 : 0xFF);
            if (rnd() % 2) ram[0x9F10 + rnd() % 2] = (uint8_t)k;
            ram[0x95DC] = (uint8_t)(rnd() % 2 ? ram[0x9F10] : rnd() % 13); ram[0x95DD] = (uint8_t)(rnd() % 2 ? ram[0x9F11] : rnd() % 13);
            put_word(0x95F8, rnd() % 2 ? (rnd() % 0x21) << 8 : rnd());
            put_long(0x962C, rnd() % 3 ? rnd() % 1000000 : rnd() % 2 ? rnd() : rnd() % 10);
            if (!strcmp(name, "SlotEmptied")) {     /* often in a slot, as its special action */
                uint8_t as = (uint8_t)(k == 0x28 ? 12 : k >= 0x2B ? k - 0x23 : k);
                if (rnd() % 2) ram[0x9F10] = as;
                if (rnd() % 2) ram[0x9F11] = as;
            }
        }
        if (!strcmp(name, "EnemyNextState")) {      /* a monster with a pending state or none, any variant */
            uint32_t e = 0x40 + (rnd() % 16) * 4;
            m68k_set_reg(M68K_REG_A4, e);
            ram[0xDE00 + e] = (uint8_t)(rnd() % 2 ? 0xFF : rnd() % 9);          /* -$2200 */
            ram[0xDA03 + e] = (uint8_t)(rnd() % 4 ? rnd() % 24 : rnd());        /* -$25FD */
            ram[0xD401 + e] = (uint8_t)(rnd() % 9 | (rnd() % 2 ? 0x80 : 0));    /* -$2BFF */
        }
        if (!strcmp(name, "RunSpawnPoints")) {
            /* spawn points of the kinds that need no level data (0-5, 7, 9, 10, 13, 15), for
             * enemy kinds already in the room (their graphics are loaded), around the player */
            uint16_t kinds[32]; int nk = 0;                                 /* the room's list ($FFA122) */
            for (uint32_t i = 0; i < (uint32_t)(ram[0xA142] << 8 | ram[0xA143]) && nk < 8; i++)
                kinds[nk++] = (uint16_t)(ram[0xA122 + 4 * i] << 8 | ram[0xA123 + 4 * i]);
            uint32_t pl = (uint32_t)(int32_t)(int16_t)(ram[0x9EEE] << 8 | ram[0x9EEF]);
            int32_t px = get_sword(0xC800 + pl), py = get_sword(0xC900 + pl);
            static const uint8_t ks[] = {0, 1, 2, 3, 4, 5, 7, 9, 10, 13, 15};
            for (uint32_t q = 0; nk && q < 0x40; q += 4) {
                if (rnd() % 2) continue;                                    /* keep the room's own */
                uint8_t k = ks[rnd() % 11];
                ram[0xA15C + q] = (uint8_t)(0x80 | (rnd() % 2) | (rnd() % 4 ? 0 : 0x40));
                ram[0xA15D + q] = (uint8_t)(k | (rnd() % 5 ? 0 : 0x80));
                ram[0xA15E + q] = (uint8_t)rnd();
                ram[0xA15F + q] = (uint8_t)(rnd() & 0x7F);
                ram[0xA1AC + q] = (uint8_t)rnd(); ram[0xA1AD + q] = (uint8_t)rnd();
                put_word(0xA1AE + q, kinds[rnd() % (unsigned)nk]);
                {   /* on the screen, just off it, or far: by the camera ($9758, $975A) */
                    int32_t cx = get_sword(0x9758), cy = get_sword(0x975A);
                    int where = (int)(rnd() % 3);
                    int32_t x = where == 0 ? cx + (int32_t)(rnd() % 320) : where == 1 ? cx - 1 - (int32_t)(rnd() % 60) : px + small(0x200);
                    int32_t y = where == 0 ? cy + (int32_t)(rnd() % 224) : where == 1 ? cy + 225 + (int32_t)(rnd() % 40) : py + small(0x100);
                    put_word(0xA1FC + q, (uint32_t)x); put_word(0xA1FE + q, (uint32_t)y);
                }
                uint8_t max = (uint8_t)(rnd() % 4 ? 1 + rnd() % 5 : 0x80 | rnd());
                ram[0xA24C + q] = max;
                uint8_t made = (uint8_t)(max & 0x80 ? rnd() % 4 : rnd() % (max + 1u));
                ram[0xA24D + q] = made;
                ram[0xA24E + q] = (uint8_t)(rnd() % (made + 1u)); ram[0xA24F + q] = (uint8_t)(rnd() % 2 ? 0 : rnd() % (made + 1u));
                ram[0xA29C + q] = (uint8_t)(1 + rnd() % 4); ram[0xA29D + q] = (uint8_t)(rnd() % 3);
                ram[0xA29E + q] = (uint8_t)(rnd() % 4); ram[0xA29F + q] = (uint8_t)(rnd() % 16 - 8);
                if (k == 4) ram[0xA29D + q] = (uint8_t)rnd();                   /* its flag */
                if (k == 13) put_long(0xA2EC + q, 0x10000 + rnd() % 0x8000);    /* offsets: any bytes */
                else if (k == 15) { put_word(0xA2EC + q, (uint32_t)(px + small(0x80))); put_word(0xA2EE + q, (uint32_t)(py + small(0x80))); }
                else { put_word(0xA2EC + q, rnd() % 0x200); put_word(0xA2EE + q, (uint32_t)small(0x40)); }
            }
            for (uint32_t t = 0; t < 16; t += 4) { put_word(0xA14C + t, rnd()); put_word(0xA14E + t, rnd() % 3 ? rnd() % 0x100 : rnd()); }
            put_word(0x965A, rnd());
            if (rnd() % 8 == 0) ram[0x9659] ^= (uint8_t)(1 << (rnd() % 2 ? 0 : 4));
            if (rnd() % 4 == 0) ram[0x9668] = (uint8_t)(rnd() % 2 ? 0xF : rnd() % 16);
        }
        if (!strcmp(name, "RunItems")) {
            /* the item slots: kinds of the table at every step, begun or not, near the player,
             * timers near their ends; the attacker ($9F30) and its spark state */
            uint32_t p = (uint32_t)(int32_t)(int16_t)(ram[0x9EEE] << 8 | ram[0x9EEF]);
            int32_t px = get_sword(0xC800 + p), py = get_sword(0xC900 + p);
            static const uint8_t steps0[] = {0, 2, 4, 6}, steps2[] = {0, 2};
            for (uint32_t s = 0x80; s < 0xC0; s += 4) {
                uint16_t kind = rnd() % 2 ? (uint16_t)(2 * (rnd() % 15)) : (uint16_t)(0x200 + 2 * (rnd() % 12));
                ram[0xC000 + s] = (uint8_t)(rnd() % 4 ? 0x80 | (rnd() & 0x41) : rnd() & 0x41);
                put_word(0xD402 + s, kind);
                {   /* its animation set from the kinds' table, as the spawner gives it (a random
                     * one sends the animation interpreter round forever) */
                    uint32_t grp = 0x11094 + (uint32_t)(int32_t)(int16_t)bus_read16(0x11094 + (kind >> 8));
                    uint32_t rec = 0x11094 + (uint32_t)(int32_t)(int16_t)bus_read16(grp + (kind & 0xFF));
                    put_word(0xC300 + s, bus_read16(rec));
                    ram[0xC302 + s] = (uint8_t)(kind == 0x20E ? 8 * (rnd() % 2) : 2 * (rnd() % 2) * (kind < 0x200));
                    ram[0xC303 + s] = 10;
                }
                uint8_t st = kind < 0x200 ? steps0[rnd() % 4] : (kind == 0x204 || kind == 0x210) ? steps2[rnd() % 2] : 0;
                ram[0xD401 + s] = (uint8_t)(st | (rnd() % 2 ? 0x80 : 0));
                put_word(0xC800 + s, (uint32_t)(px + small(40))); put_word(0xC900 + s, (uint32_t)(py + small(40)));
                /* the saved place (-$2100), as its first frame leaves it: a broken block's
                 * remains write the map there, and a random place writes over other RAM */
                put_word(0xDF00 + s, (uint32_t)get_sword(0xC800 + s)); put_word(0xDF02 + s, (uint32_t)get_sword(0xC900 + s));
                if (kind == 0x212) {                                            /* on a block of the map */
                    uint32_t cols = (uint32_t)(ram[0x9754 + 0x44] << 8 | ram[0x9754 + 0x45]) + 1;
                    uint32_t rows = (uint32_t)(ram[0x9754 + 0x46] << 8 | ram[0x9754 + 0x47]) / 64 + 1;
                    put_word(0xDF00 + s, 0x1000 + (rnd() % cols) * 16 + rnd() % 16);
                    put_word(0xDF02 + s, 0x1000 + (rnd() % rows) * 16 + rnd() % 16);
                }
                put_word(0xCA00 + s, (uint32_t)small(0x400)); put_word(0xCA02 + s, (uint32_t)small(0x400));
                ram[0xD800 + s] = (uint8_t)(rnd() % 3 ? 1 + rnd() % 3 : rnd());
                ram[0xD801 + s] = (uint8_t)(rnd() % 3 ? rnd() % 3 : rnd());
                put_word(0xD802 + s, rnd() % 3 ? 1 + rnd() % 3 : rnd());
                ram[0xDA03 + s] = (uint8_t)rnd(); ram[0xDA02 + s] = (uint8_t)rnd();
                put_word(0xD302 + s, rnd() % 2 ? p : 0x40 + (rnd() % 16) * 4);
                ram[0xC600 + s] = (uint8_t)(rnd() % 2 ? 0 : rnd());
                ram[0xCB00 + s] = (uint8_t)(rnd() % 16); ram[0xCB01 + s] = (uint8_t)(rnd() % 16);
                ram[0xCB03 + s] = (uint8_t)(rnd() % 16);
                ram[0xCF00 + s] = (uint8_t)rnd(); ram[0xD100 + s] = (uint8_t)rnd();
                put_word(0xCC00 + s, (uint32_t)small(0x40)); put_word(0xCD00 + s, rnd() % 0x400);
                ram[0xDE00 + s] = (uint8_t)(rnd() % 2);
                if ((kind == 0x204 || kind == 0x210) && rnd() % 2) {     /* a bubble in a water current */
                    static uint8_t cur[16]; static int nc = -1;
                    if (nc < 0) {
                        nc = 0;
                        for (uint32_t a = 0; a < 0x80 && nc < 16; a++) {
                            Cpu t = {0}; t.a[4] = s; t.d[1] = a; TileTypeOf(&t);
                            if ((t.d[2] & 0xB00F) == 0xB000) cur[nc++] = (uint8_t)a;
                        }
                    }
                    if (nc) set_block_attr(0x9754, s, (uint32_t)get_sword(0xC800 + s), (uint32_t)get_sword(0xC900 + s), cur[rnd() % (unsigned)nc]);
                }
            }
            for (uint32_t a = 0x40; a < 0x80; a += 4) {                         /* enemies for the sparks */
                ram[0xC000 + a] = (uint8_t)(rnd() % 3 ? 0xC1 : rnd());
                ram[0xD402 + a] = (uint8_t)(rnd() % 3 ? 0 : rnd() % 4);
                ram[0xD401 + a] = (uint8_t)(rnd() % 4 * 2);
                put_word(0xDA00 + a, rnd() % 4 ? rnd() % 0x400 : 0);
                put_word(0xC800 + a, (uint32_t)(px + small(40))); put_word(0xC900 + a, (uint32_t)(py + small(40)));
            }
            put_word(0x9F30, rnd() % 2 ? p : 0x40 + (rnd() % 16) * 4);
            uint32_t at = (uint32_t)(int32_t)(int16_t)(ram[0x9F30] << 8 | ram[0x9F31]);
            ram[0xDA03 + at] = (uint8_t)(rnd() % 3 ? rnd() % 2 : rnd());
            ram[0xD800 + at] = (uint8_t)(rnd() % 2 ? 0 : rnd());
            ram[0x9F78] = (uint8_t)(rnd() % 2 ? 0 : rnd());
            put_word(0x965A, rnd());
            put_word(0x95F4, rnd() % 0x200);
        }
        if (!strcmp(name, "Currents")) {            /* in water with a current, near the speed limit */
            ram[0x966B] = (uint8_t)(rnd() | (rnd() % 4 ? 1 : 0));
            put_word(0x9EE2, rnd() % 4 ? 0xB000 | (rnd() & 0xFFF) : rnd());
            uint32_t e = m68k_get_reg(NULL, M68K_REG_A4);
            put_word(0xCA00 + e, (uint32_t)(rnd() % 2 ? 0x180 : -0x180) + small(8));
            put_word(0xCA02 + e, (uint32_t)(rnd() % 2 ? 0x180 : -0x180) + small(8));
        }
        if (!strcmp(name, "DecodeGfxBlock")) {     /* a real block, into the game's decode buffer */
            m68k_set_reg(M68K_REG_D0, (rnd() << 16) | (rnd() % 768));
            m68k_set_reg(M68K_REG_A1, 0x00FF2D00);
        }
        if (!strncmp(name, "UpdateAnim", 10) || !strcmp(name, "SetAnimNow")) {
            /* a valid animation for the chosen entity, its script started or part-way through */
            anim_walk();
            uint32_t e = m68k_get_reg(NULL, M68K_REG_A4);
            const uint8_t *t = anim_sga[rnd() % (unsigned)anim_n];
            ram[0xC300 + e] = t[0]; ram[0xC301 + e] = t[1]; ram[0xC302 + e] = t[2];
            m68k_set_reg(M68K_REG_D0, (m68k_get_reg(NULL, M68K_REG_D0) & ~0xFFu) | t[2]);
            uint32_t l2 = ANIM_DATA + (uint32_t)aw(t[0]);
            uint32_t l3 = ANIM_DATA + (uint32_t)(int16_t)bus_read16(l2 + t[1]);
            uint32_t script = ANIM_DATA + (uint32_t)(int16_t)bus_read16(l3 + t[2]);
            /* resume at a random item of the script, so commands after the first frame run too */
            uint32_t items[64]; int ni = 0;
            for (uint32_t p = script; ni < 64; ) {
                unsigned b = bus_read8(p);
                items[ni++] = p;
                if (b >= 0xF0) { unsigned k = b & 15; p += 1 + (k >= 10 && k <= 12 ? 2 : 0); if (k == 13 || k == 15) break; }
                else { p += 3; if (!b) break; }
            }
            uint32_t at = items[rnd() % (unsigned)ni];
            for (int k2 = 0; k2 < 4; k2++) { ram[0xC400 + e + k2] = (uint8_t)(at >> (24 - 8 * k2)); ram[0xC500 + e + k2] = (uint8_t)(script >> (24 - 8 * k2)); }
        }
        if (!strncmp(name, "Obj", 3) || !strncmp(name, "Ppl", 3) || !strncmp(name, "Hnd", 3) || !strncmp(name, "Act", 3)) {
            /* one of objects.c's or people.c's handlers: an enemy slot set up by the game's own
             * routine for one of the handler's kinds ($12684: animation set, stats), then its
             * fields at the values each step reads: on the first frame or later, near the player
             * or the screen's edge, timers near their ends, touched or not, counted at a spawn
             * point or not */
            static const struct { const char *n; uint16_t kinds[3]; } objs[] = {
                {"ObjLobbed", {0x208}}, {"ObjThrown", {0x20A, 0x20C, 0x22A}}, {"ObjStanding", {0x226}},
                {"ObjFlying", {0x214}}, {"ObjWalker", {0x21C}}, {"ObjCrawler", {0x212, 0x21E, 0x230}},
                {"ObjCrawlerStill", {0x232}}, {"ObjAimed", {0x22C}}, {"ObjBouncer", {0x22E}},
                {"ObjHopper", {0x802}}, {"ObjFloater", {0x804}}, {"ObjColumn", {0x806}}, {"ObjEdgeCrawler", {0x808}},
                {"PplScene", {0x400}}, {"PplStanding", {0x402}}, {"PplBreakable", {0x404}},
                {"PplTownsperson", {0x406, 0x416, 0x468}}, {"PplStandingStill", {0x412}}, {"PplWanderer", {0x438}},
                {"PplFlagged", {0x43A}}, {"PplPlacedBreakable", {0x46A}}, {"PplSpeaker", {0x600}},
                {"HndAreaMonster", {0x058}}, {"HndHopperOr", {0x800}}, {"HndGuardian", {0x060}},
                {"HndVanishing", {0x218}}, {"HndFlyer", {0x21A}}, {"HndThrownLater", {0x220}},
                {"HndCarried", {0x228}}, {"HndPacer", {0x80C}},
                {"HndDarter", {0x216}}, {"HndDiver", {0x222}}, {"HndSeeker", {0x224}}, {"HndGrower", {0x80A}},
            };
            uint16_t kind = 0;
            for (unsigned i = 0; i < sizeof objs / sizeof *objs; i++)
                if (!strcmp(name, objs[i].n)) { int nk = objs[i].kinds[2] ? 3 : objs[i].kinds[1] ? 2 : 1; kind = objs[i].kinds[rnd() % (unsigned)nk]; }
            int act_state = -1, act_num = 0;
            if (!strncmp(name, "Act", 3)) {         /* a monster kind and state whose action this is */
                uint32_t want = R(idx)->addr, pairs[64]; int np = 0;
                for (uint32_t kd = 0; kd < 0x74; kd += 2) {
                    uint32_t row = 0x13B70 + (uint32_t)(int32_t)(int16_t)bus_read16(0x13B70 + kd);
                    for (uint32_t st = 0; st < 9; st++) {
                        uint32_t an = bus_read8(row + st);
                        uint32_t lst = 0x13DA4 + (uint32_t)(int32_t)(int16_t)bus_read16(0x13DA4 + 2 * st);
                        uint32_t tg = (uint32_t)bus_read16(lst + 4 * an) << 16 | bus_read16(lst + 4 * an + 2);
                        if (tg == want && np < 64) pairs[np++] = kd << 16 | st << 8 | an;
                    }
                }
                if (np) { uint32_t pr = pairs[rnd() % (unsigned)np]; kind = (uint16_t)(pr >> 16); act_state = (int)(pr >> 8 & 0xFF); act_num = (int)(pr & 0xFF); }
                else { kind = (uint16_t)(2 * (rnd() % 58)); act_state = (int)(rnd() % 9); }   /* in the table, used by no kind */
            }
            if (kind == 0x416) {                    /* any of group 4's people (handler 11's kinds) */
                kind = (uint16_t)(0x406 + 2 * (rnd() % 49));
                if (kind == 0x412 || kind == 0x438 || kind == 0x43A) kind = 0x406;
            }
            uint32_t e = 0x40 + (rnd() % 16) * 4;
            m68k_set_reg(M68K_REG_A4, e);
            for (uint32_t row = 0xC000; row < 0xE000; row += 0x100)          /* every field, at random */
                for (uint32_t b = 0; b < 4; b++) ram[row + e + b] = (uint8_t)rnd();
            if (kind >> 8 != 6) {                   /* $126D4 has no entry for group 6 */
                Cpu t = {0}; t.a[2] = t.a[4] = e; t.d[1] = kind; t.a[7] = 0xFFFF4F00u;
                call_68k(&t, 0x12684);
            }
            uint32_t p = (uint32_t)(int32_t)(int16_t)(ram[0x9EEE] << 8 | ram[0x9EEF]);
            int32_t px = get_sword(0xC800 + p), py = get_sword(0xC900 + p);
            int32_t cx = get_sword(0x9758), cy = get_sword(0x975A);
            put_word(0xD402 + e, kind);
            ram[0xC000 + e] = (uint8_t)(rnd() % 4 ? 0x80 | (rnd() & 0x41) : rnd());
            ram[0xD401 + e] = (uint8_t)(rnd() % 2 ? 0x80 : 0);
            ram[0xC002 + e] = (uint8_t)rnd();                                 /* facing, and the rest ($12684 clears it) */
            {   int where = (int)(rnd() % 4);                               /* by the player, or the screen's edges */
                int32_t x = where < 2 ? px + small(48) : where == 2 ? cx + (rnd() % 2 ? -40 : 360) + small(24) : cx + (int32_t)(rnd() % 320);
                int32_t y = where < 2 ? py + small(32) : where == 3 ? cy + (rnd() % 2 ? -40 : 260) + small(24) : cy + (int32_t)(rnd() % 224);
                put_word(0xC800 + e, (uint32_t)x); put_word(0xC900 + e, (uint32_t)y);
                put_word(0xD200 + e, (uint32_t)(x + small(4)));                  /* the x before */
                put_word(0xDF02 + e, (uint32_t)(y + (rnd() % 3 ? small(0x40) : small(4))));   /* home */
                put_word(0xD302 + e, (uint32_t)((x >> 4) + (rnd() % 2 ? 0 : small(1))));       /* start column */
            }
            put_word(0xCA00 + e, (uint32_t)small(0x400)); put_word(0xCA02 + e, (uint32_t)small(0x400));
            put_word(0xCC00 + e, (uint32_t)small(0x40)); put_word(0xCC02 + e, (uint32_t)small(0x40));
            put_word(0xCD00 + e, rnd() % 0x400); put_word(0xCD02 + e, rnd() % 0x400);
            ram[0xD800 + e] = (uint8_t)(rnd() % 3 ? 1 + rnd() % 3 : rnd());   /* timers near their ends */
            ram[0xD801 + e] = (uint8_t)(rnd() % 3 ? rnd() % 3 : rnd());
            put_word(0xD802 + e, rnd() % 2 ? 0 : rnd() % 3 ? rnd() % 0x20 : rnd());
            ram[0xD503 + e] = (uint8_t)(rnd() % 2 ? 0 : rnd());               /* touched */
            ram[0xCF00 + e] = (uint8_t)(rnd() % 2 ? 0 : rnd());               /* collisions */
            ram[0xCE00 + e] = (uint8_t)(rnd() % 2 ? 0 : rnd() & 7);           /* NO_COUNT and the despawn test */
            ram[0xC600 + e] = (uint8_t)(rnd() % 2 ? 0 : rnd());               /* the animation busy */
            put_word(0xD500 + e, rnd() % 3 ? (rnd() % 16) * 4 : 0x8000 | rnd());   /* its spawn point */
            put_word(0xA11C, rnd() % 2 ? p : 0x40 + (rnd() % 16) * 4);
            ram[0xA10E] = (uint8_t)(rnd() % 4);
            if (rnd() % 8 == 0) ram[0x966B] ^= 0x20;
            if (!strncmp(name, "Act", 3)) {
                /* as EnemyDispatch leaves it: the state (begun or not), d0 = action * 4,
                 * d1 = state * 2, a0 = the action; the spawn flags; its partner an entity;
                 * the animation saved under a puff a real one (a random one runs the animation
                 * interpreter round for ever) */
                ram[0xD401 + e] = (uint8_t)(act_state | (rnd() % 2 ? 0x80 : 0));
                m68k_set_reg(M68K_REG_D0, (uint32_t)act_num * 4);
                m68k_set_reg(M68K_REG_D1, (rnd() << 16) | (uint32_t)act_state * 2);
                m68k_set_reg(M68K_REG_A0, R(idx)->addr);
                ram[0xDA03 + e] = (uint8_t)(rnd() % 4 ? rnd() % 24 : rnd());    /* the variant */
                ram[0xD502 + e] = (uint8_t)rnd();                                 /* spawn flags */
                put_word(0xD302 + e, rnd() % 2 ? p : 0x40 + (rnd() % 16) * 4);    /* partner */
                put_word(0xD802 + e, rnd() % 4 ? (uint32_t)(ram[0xC300 + e] << 8 | ram[0xC301 + e]) : (rnd() % 2 ? 0x1208 : 0x120A));
                ram[0xD801 + e] = (uint8_t)(rnd() % 2 ? rnd() : rnd() & 0x80);
                ram[0xD400 + e] = (uint8_t)rnd();                                 /* its animation's frame count */
                ram[0xDE00 + e] = (uint8_t)(rnd() % 2 ? 0xFF : rnd());            /* the pending state (-$2200) */
                if (rnd() % 2) ram[0x966B] ^= 1;                                  /* in water */
                ram[0xDE01 + e] = (uint8_t)rnd();                                 /* the magic hit (-$21FF) */
                put_word(0xDA00 + e, rnd() % 2 ? rnd() % 0x40 : rnd() % 0x800);   /* health: beaten by a hit, or not */
                if (rnd() % 4 == 0) ram[0xD800 + e] = 0;                          /* a timer already run out */
                put_word(0x965A, rnd());                                          /* the frame counter */
                if (!strcmp(name, "Act1454C") && rnd() % 2)                       /* the frame it appeared: young or old */
                    put_word(0xD302 + e, (uint32_t)((ram[0x965A] << 8 | ram[0x965B]) - (int32_t)(rnd() % 0x300)));
                /* $1454C's flag; for the others this byte is the saved animation set's, and a bad
                 * one sends the animation interpreter round for ever */
                if (!strcmp(name, "Act1454C") && rnd() % 4 == 0) ram[0xD802 + e] = (uint8_t)(rnd() % 2 ? 0xFF : 0);
                if (rnd() % 4 == 0) put_long(0xCA00 + e, 0);                      /* standing still */
                put_word(0xCE02 + e, rnd() % 4 ? 0x40 + rnd() % 0xC0 : rnd() % 0x40);   /* its range from home */
                /* its size, as its animation gives it: never 0 (the puffs about it divide by it,
                 * RandomSigned $4340, and a division by zero stops the translated code) */
                ram[0xCB02 + e] = (uint8_t)(4 + rnd() % 28); ram[0xCB03 + e] = (uint8_t)(4 + rnd() % 28);
                ram[0xCB00 + e] = (uint8_t)(4 + rnd() % 28); ram[0xCB01 + e] = (uint8_t)(4 + rnd() % 28);   /* its body (-$3500), the same */
                /* its hit records (-$2A00, -$2900): none, as between its attacks (pointers; a
                 * random odd one is an address error for the original and not for the C) */
                put_long(0xD600 + e, 0); put_long(0xD700 + e, 0);
                if (!strcmp(name, "Act15066") && rnd() % 2) {                     /* its last hop, on the ground */
                    ram[0xD800 + e] = 1; ram[0xD801 + e] = 1; ram[0xCF00 + e] = 2;
                }
                if (!strcmp(name, "Act151D6") && rnd() % 2) ram[0xD802 + e] = 0xFF;   /* attacking */
                if (!strcmp(name, "Act17548")) {                                  /* its kind ($16) in the room's list */
                    uint32_t n = (uint32_t)(ram[0xA142] << 8 | ram[0xA143]), i;
                    for (i = 0; i < n && i < 8; i++) if ((ram[0xA122 + 4 * i] << 8 | ram[0xA123 + 4 * i]) == 0x16) break;
                    if (i == n || i == 8) { i = n < 8 ? n : 7; put_word(0xA122 + 4 * i, 0x16); if (n < 8) put_word(0xA142, n + 1); }
                }
                if (!strcmp(name, "Act1774E") || !strcmp(name, "Act17822") || !strcmp(name, "Act178D0")) {   /* the last frame */
                    if (rnd() % 2) ram[0xD801 + e] = 1;
                    if (rnd() % 2) ram[0xD800 + e] = (uint8_t)(rnd() % 3);
                }
                if (!strcmp(name, "Act178D0")) {                                  /* not laid yet; the frame marked */
                    if (rnd() % 2) ram[0xD802 + e] = 0;
                    if (rnd() % 2) ram[0xD400 + e] = (uint8_t)(1 + rnd() % 255);
                }
                if (!strcmp(name, "Act178D0"))                                    /* its young (8, 6) in the room's list */
                    for (uint32_t k = 0; k < 2; k++) {
                        uint32_t kind = k ? 6 : 8, n = (uint32_t)(ram[0xA142] << 8 | ram[0xA143]), i;
                        for (i = 0; i < n && i < 8; i++) if ((ram[0xA122 + 4 * i] << 8 | ram[0xA123 + 4 * i]) == kind) break;
                        if (i == n || i == 8) { i = n < 8 ? n : 6 + k; put_word(0xA122 + 4 * i, kind); if (n < 8) put_word(0xA142, n + 1); }
                    }
                if (!strcmp(name, "Act1759A")) {                                  /* the target's sword out or not */
                    uint32_t t = (uint32_t)(ram[0xA11C] << 8 | ram[0xA11D]) & 0xFFFF;
                    if (t < 0x100) put_long(0xD600 + t, rnd() % 2 ? 0 : 0x10000 + rnd() % 0x60000);   /* boxes read from ROM: not the stack */
                }
                if (!strcmp(name, "Act17014")) {                                  /* its phases; a player high up */
                    ram[0xCE01 + e] = (uint8_t)(rnd() % 5);
                    if (rnd() % 4 == 0) put_word(0xC900 + p, 0x34 + rnd() % 8);
                }
                if (!strcmp(name, "Act1729A") || !strcmp(name, "Act17310") || !strcmp(name, "Act17362")) {
                    /* a link of the chain: ahead of it (GOAL) a link or none, behind it (MAKER) a
                     * link that ends the chain or none (a loop of links goes round for ever, in the
                     * original too), and kind $2E in the room's list for the head to grow it */
                    uint32_t a = 0x40 + (rnd() % 16) * 4, b = 0x40 + (rnd() % 16) * 4;
                    if (a == e) a = 0x40 + ((a - 0x40 + 4) & 0x3C);
                    if (b == e || b == a) b = 0x40 + ((e - 0x40 + 8) & 0x3C);
                    if (b == a || b == e) b = p;
                    put_word(0xD300 + e, rnd() % 2 ? 0xFF00 | rnd() % 256 : a);
                    put_word(0xD302 + e, rnd() % 2 ? 0xFF00 | rnd() % 256 : b);
                    put_word(0xD300 + b, e); put_word(0xD302 + b, 0xFF00);
                    put_word(0xD300 + a, 0xFF00); put_word(0xD302 + a, e);
                    ram[0xCE01 + e] = (uint8_t)(rnd() % 7);                       /* its number */
                    ram[0xDA02 + e] = (uint8_t)(rnd() % 3 ? 1 + rnd() % 2 : rnd());   /* the next link's wait */
                    ram[0xCF01 + e] = (uint8_t)(rnd() % 2 ? 0 : rnd());           /* -$30FF, -$30FE */
                    ram[0xCF02 + e] = (uint8_t)(rnd() % 2 ? 0 : rnd());
                    put_word(0x965A, rnd() % 2 ? rnd() & ~15u : rnd());          /* the head's 16-frame check */
                    uint32_t n = (uint32_t)(ram[0xA142] << 8 | ram[0xA143]), i;
                    for (i = 0; i < n && i < 8; i++) if ((ram[0xA122 + 4 * i] << 8 | ram[0xA123 + 4 * i]) == 0x2E) break;
                    if (i == n || i == 8) { i = n < 8 ? n : 7; put_word(0xA122 + 4 * i, 0x2E); if (n < 8) put_word(0xA142, n + 1); }
                }
                if (!strcmp(name, "Act15A86") || !strcmp(name, "Act15C9E") || !strcmp(name, "Act15DD2")
                    || !strcmp(name, "Act15E54") || !strcmp(name, "Act15EE6") || !strcmp(name, "Act15FB0")) {
                    /* Gragg & Glagg: its group record at $FFF800 (a flag byte, then its three
                     * companions), the byte it sets when gone at $FFF810, the fight's state; free
                     * room for $15A86's companions ($15AD2 and $15AE4 wait for ever without) and
                     * kind $800 in the room's list ($4910 likewise) */
                    put_word(0xD300 + e, 0xF800);
                    ram[0xF800] = (uint8_t)rnd();
                    for (uint32_t k3 = 1; k3 < 4; k3++) {
                        uint32_t m = 0x40 + (rnd() % 16) * 4;
                        if (m == e) m = p;
                        put_word(0xF800 + 2 * k3, m);
                    }
                    put_word(0xD302 + e, 0xF810);
                    ram[0xD801 + e] = (uint8_t)(rnd() % 4 || !strcmp(name, "Act15A86")      /* $15A86: its steps (0-8) */
                                                ? ((rnd() % 5) * 2 | (rnd() % 2 ? 0x80 : 0)) : rnd() % 2 ? 0 : 0xFF);
                    ram[0xA0E2] &= 0x7F;
                    for (int k3 = 0, freed = 0; k3 < 16 && freed < 3; k3++) {
                        uint32_t s = 0x40 + (uint32_t)k3 * 4;
                        if (s != e && rnd() % 2) { ram[0xC000 + s] &= 0x7F; freed++; }
                    }
                    {   uint32_t n = (uint32_t)(ram[0xA142] << 8 | ram[0xA143]), i;
                        for (i = 0; i < n && i < 8; i++) if ((ram[0xA122 + 4 * i] << 8 | ram[0xA123 + 4 * i]) == 0x800) break;
                        if (i == n || i == 8) {
                            i = n < 8 ? n : 7;
                            put_word(0xA122 + 4 * i, 0x800);
                            if (n < 8) put_word(0xA142, n + 1);
                        }
                    }
                    if (!strcmp(name, "Act15A86")) {                              /* its counters at their ends */
                        ram[0xD803 + e] = (uint8_t)(rnd() % 2 ? 1 : rnd() % 4);
                        ram[0xD802 + e] = (uint8_t)(rnd() % 4);
                    }
                    if (!strcmp(name, "Act15C9E"))                                /* its last puff */
                        ram[0xD801 + e] = (uint8_t)(rnd() % 2 ? 1 : 1 + rnd() % 16);
                    if (!strcmp(name, "Act15EE6") && rnd() % 4 == 0)              /* no room for its object */
                        for (uint32_t s = 0x40; s < 0x80; s += 4) ram[0xC000 + s] |= 0x80;
                    ram[0xA10F] = (uint8_t)rnd();
                    ram[0xA107] = (uint8_t)(rnd() % 2 ? 0 : rnd());
                    ram[0xA111] = (uint8_t)(rnd() % 2 ? 0 : 1 + rnd() % 3);
                    put_word(0xA112, rnd() % 2 ? e : 0x40 + (rnd() % 16) * 4);
                    put_word(0xA10C, rnd() % 2 ? e : 0x40 + (rnd() % 16) * 4);
                    ram[0xA106] = (uint8_t)(rnd() % 3);
                    ram[0xA114] = (uint8_t)(rnd() % 3);
                    put_word(0xA108, rnd() % 2 ? 0 : 0x40 + (rnd() % 16) * 4);
                    put_word(0xA10A, rnd() % 2 ? 0 : 0x40 + (rnd() % 16) * 4);
                    put_word(0xA102, rnd() % 2 ? 0 : e);
                    put_word(0xDF02 + e, (uint32_t)(get_sword(0xC900 + e) + small(0x40)));   /* home height */
                }
                {   int32_t x = get_sword(0xC800 + e);                            /* home x: at it, or near */
                    put_word(0xDF00 + e, (uint32_t)(rnd() % 4 == 0 ? x : x + small(0x40))); }
            }
            if (!strncmp(name, "Hnd", 3)) {
                uint8_t begun = (uint8_t)(rnd() % 2 ? 0x80 : 0);
                ram[0xDA03 + e] = (uint8_t)(rnd() % 4 ? rnd() % 24 : rnd());    /* the monsters' variant */
                if (!strcmp(name, "HndAreaMonster")) {      /* area 6 room 5: $16908's steps (0-16) */
                    if (rnd() % 2) put_word(0x9668, 0x605);
                    ram[0xD401 + e] = (uint8_t)((ram[0x9668] << 8 | ram[0x9669]) == 0x605 ? (rnd() % 9) * 2 | begun : rnd() % 9 | begun);
                }
                if (!strcmp(name, "HndGuardian")) {
                    /* step 0's first frame ($15ACE) takes three free entities and loops for ever
                     * without them, in the original as well: always past it */
                    ram[0xD401 + e] = (uint8_t)(rnd() % 9 | begun);
                    if (!(ram[0xD401 + e] & 0x7F)) ram[0xD401 + e] = 0x80;
                    ram[0xA10F] = (uint8_t)(rnd() % 4 ? 2 | (rnd() & 1) : rnd());
                    ram[0xA107] = (uint8_t)(rnd() % 2 ? 0 : rnd());
                    ram[0xA111] = (uint8_t)(rnd() % 4 ? 0 : rnd());
                    ram[0xA10E] = (uint8_t)(rnd() % 9);
                    ram[0xA110] = (uint8_t)(rnd() % 2 ? 0x2D + rnd() % 4 : rnd());
                    uint32_t t = rnd() % 2 ? p : 0x40 + (rnd() % 16) * 4;  /* the target, by the place or not */
                    if (t == e) t = p;
                    put_word(0xA11C, t);
                    if (rnd() % 2) {
                        put_word(0xC800 + t, (uint32_t)(0x1100 + small(0x24)));
                        put_word(0xC900 + t, (uint32_t)(0x1050 - ram[0xCB03 + t] + small(0x24)));
                    }
                    ram[0xD800 + e] = (uint8_t)(rnd() % 2 ? 0 : rnd() % 3);       /* blinking */
                    ram[0xD801 + e] = (uint8_t)((rnd() % 5) * 2 | (rnd() % 2 ? 0x80 : 0));   /* $15B44's steps (0-8) */
                    ram[0xDE01 + e] = (uint8_t)rnd();                             /* the magic hit (-$21FF) */
                    {   /* step 6 ($15EE6) looks up kind $800 in the room's list, and $4910 loops for ever
                         * when it is not there: give the room one */
                        uint32_t n = (uint32_t)(ram[0xA142] << 8 | ram[0xA143]), i;
                        for (i = 0; i < n && i < 8; i++) if ((ram[0xA122 + 4 * i] << 8 | ram[0xA123 + 4 * i]) == 0x800) break;
                        if (i == n || i == 8) {
                            i = n < 8 ? n : 7;
                            put_word(0xA122 + 4 * i, 0x800);
                            if (n < 8) put_word(0xA142, n + 1);
                        }
                    }
                    put_word(0xD300 + e, 0xF800);                                 /* its companions: a list at $FFF800 */
                    for (uint32_t k3 = 0; k3 < 4; k3++) put_word(0xF800 + 2 * k3, 0x40 + (rnd() % 16) * 4);
                }
                if (!strcmp(name, "HndThrownLater")) ram[0xD401 + e] = (uint8_t)(rnd() % 4);
                if (!strcmp(name, "HndPacer")) {
                    ram[0xD401 + e] = (uint8_t)(rnd() % 2 ? 0 : 2);
                    if (rnd() % 2) put_word(0xC800 + e, 0x1000 + rnd() % 0xA0);
                }
                if (!strcmp(name, "HndFlyer")) {
                    ram[0xD401 + e] = begun;
                    ram[0xD800 + e] = (uint8_t)(rnd() % 4 ? rnd() % 4 : rnd());
                    ram[0xD801 + e] = (uint8_t)(rnd() % 4 ? 1 + rnd() % 2 : rnd());
                }
                if (!strcmp(name, "HndCarried") || !strcmp(name, "HndVanishing") || !strcmp(name, "HndHopperOr"))
                    ram[0xD401 + e] = (uint8_t)(rnd() % 4 | begun);
                if (!strcmp(name, "HndCarried")) ram[0xC003 + e] = (uint8_t)rnd();   /* BLINK's bit 2 */
                {   /* batch 6: phases (-$31FF), its maker (-$2CFE) an entity, timers near their ends */
                    uint32_t mk = rnd() % 2 ? p : 0x40 + (rnd() % 16) * 4;
                    ram[0xCE01 + e] = (uint8_t)(rnd() % 3);
                    if (!strcmp(name, "HndDarter")) {
                        ram[0xD401 + e] = (uint8_t)(begun | (rnd() % 2));
                        if (rnd() % 2) ram[0x9F03] ^= 0x40;
                        if (rnd() % 2) ram[0xD800 + e] = 1;                       /* aimed this frame */
                    }
                    if (!strcmp(name, "HndDiver")) {
                        ram[0xD401 + e] = (uint8_t)(rnd() % 5);
                        put_word(0xD302 + e, mk);
                        if (rnd() % 2) put_word(0xC900 + p, (uint32_t)(0x1068 + small(8)));
                        put_word(0xDA00 + p, rnd() % 2 ? 0x2F0 + rnd() % 0x20 : rnd() % 0x1000);   /* the player's health */
                        ram[0x99ED] = (uint8_t)(rnd() % 4 ? rnd() : 0xFF);
                    }
                    if (!strcmp(name, "HndSeeker")) {
                        put_word(0xD302 + e, mk);
                        ram[0xD802 + e] = (uint8_t)((rnd() % 8) * 0x20);         /* the angle: eighths (a table of longs) */
                        ram[0xD803 + e] = (uint8_t)(rnd() % 2 ? 0 : 0xFF);
                        ram[0xDA02 + e] = (uint8_t)(rnd() % 3 ? rnd() % 3 : rnd());
                    }
                    if (!strcmp(name, "HndGrower")) {
                        ram[0xD401 + e] = (uint8_t)((rnd() % 5) * 2);
                        ram[0xD802 + e] = (uint8_t)(rnd() % 4 ? (rnd() % 5) * 2 : rnd());
                        ram[0xD803 + e] = (uint8_t)(rnd() % 3 ? 1 + rnd() % 2 : rnd());
                    }
                }
                put_word(0xCE02 + e, rnd() % 3 ? 1 + rnd() % 3 : rnd());          /* frames to live */
                if (rnd() % 2) ram[0x966B] ^= 0x20;
            }
            if (!strncmp(name, "Ppl", 3)) {
                /* the steps of their tables (0-8), begun or not; a broken thing's $FF */
                ram[0xD401 + e] = (uint8_t)((rnd() % 5) * 2 | (rnd() % 2 ? 0x80 : 0));
                if (strstr(name, "Breakable") && rnd() % 3 == 0) ram[0xD401 + e] = 0xFF;
                ram[0xD503 + e] = (uint8_t)(rnd() % 2 ? 0x80 | rnd() : rnd() & 0x7F);   /* the sword's touch */
                put_word(0x9962, rnd() % 3 ? 0 : 8);                            /* text running */
                {   /* talking: level with the player and within reach, on the ground, up alone */
                    uint8_t reach = (uint8_t)(rnd() % 2 ? 16 + rnd() % 32 : rnd());
                    ram[0xCB00 + e] = reach;
                    if (rnd() % 2) {
                        put_word(0xC800 + e, (uint32_t)(px + small(reach + 4)));
                        put_word(0xC900 + e, (uint32_t)(py + small(ram[0xCB03 + e] + ram[0xCB03 + p] + 4)));
                    }
                    if (rnd() % 2) ram[0x9F02] |= 0x40; else ram[0x9F02] = (uint8_t)rnd();
                    ram[0x9F0B] = (uint8_t)(rnd() % 2 ? 0 : rnd());
                    ram[0x9F0D] = (uint8_t)(rnd() % 2 ? 1 : rnd() % 16);
                    ram[0xCF00 + p] = (uint8_t)(rnd() % 2 ? 2 | (rnd() & 0xF5) : rnd());
                }
                {   /* a script of commands 0-4, and jumps that land on one, in scratch RAM. At home
                     * already, no walks home: walks home that are already there and jumps could go
                     * round for ever, in the original as well */
                    int at_home = rnd() % 4 == 0;
                    uint32_t s = 0x4E00, at[16], jumps[16]; int n = 0, nj = 0;
                    for (int i = 0; i < 12; i++) {
                        at[n++] = s;
                        int cmd = (int)(rnd() % 6);
                        if (cmd == 5 && n > 1) {
                            uint32_t to = at[rnd() % (unsigned)(n - 1)];
                            int16_t off = (int16_t)(to - (s + 3) + 2);
                            jumps[nj++] = s;
                            ram[s++] = 5; ram[s++] = (uint8_t)(off >> 8); ram[s++] = (uint8_t)off;
                            at[n - 1] = to;                                         /* never a jump's target */
                            continue;
                        }
                        if (cmd == 5) cmd = 4;
                        if (cmd == 2 && at_home) cmd = 0;
                        ram[s++] = (uint8_t)cmd;
                        if (cmd == 0) ram[s++] = (uint8_t)(rnd() % 4 ? 1 + rnd() % 3 : rnd());
                        if (cmd == 1) ram[s++] = (uint8_t)small(40);
                        if (cmd == 3) { ram[s++] = (uint8_t)rnd(); ram[s++] = (uint8_t)rnd(); }
                    }
                    ram[s++] = 0; ram[s++] = 1;                                     /* the end: a wait */
                    put_long(0xD900 + e, 0xFFFF0000u | (nj && rnd() % 4 == 0 ? jumps[rnd() % (unsigned)nj] : at[rnd() % (unsigned)n]));
                    ram[0xCE00 + e] = (uint8_t)(rnd() % 2 ? 0 : 1 + rnd() % 255);    /* it walks a script */
                    int32_t x = get_sword(0xC800 + e);
                    put_word(0xD300 + e, rnd() % 2 ? (uint32_t)(x + small(40)) : (uint32_t)((rnd() % 4) << 8 | rnd()));
                    if (rnd() % 4 == 0) put_word(0xD300 + e, (uint32_t)x);
                    if ((ram[0xD401 + e] & 0x7F) == 2 && rnd() % 2) ram[0xD300 + e] = 1;   /* a wait's last frame */
                    put_word(0xDF00 + e, (uint32_t)(at_home ? x : x + (rnd() % 2 ? 1 : -1) * (int32_t)(1 + rnd() % 40)));
                    if (rnd() % 2) put_word(0xD800 + e, rnd());                      /* else the timer near its end */
                }
                if (!strcmp(name, "PplScene")) {    /* the player about the scene's lines; the dimming; the attack */
                    put_word(0xC800 + p, 0x10D0 + rnd() % 0x90);
                    put_word(0x9734, rnd() % 3 ? rnd() % 0x52 : 0);
                    put_word(0x9F32, rnd() % 2 ? rnd() & 0x7FFF : 0x8000 | rnd());
                    uint32_t a = rnd() % 2 ? p : 0x40 + (rnd() % 16) * 4;
                    if (a == e) a = p;                                              /* another entity */
                    put_word(0x9F30, a);
                    ram[0xD401 + a] = (uint8_t)(rnd() % 2 ? 0x0C | (rnd() & 0x80) : rnd());
                }
                if (!strcmp(name, "PplWanderer")) {
                    if (rnd() % 2) put_word(0xC800 + e, 0x11A0 + rnd() % 0x70);
                    put_word(0x965A, rnd() % 2 ? rnd() & ~63u : rnd());
                }
                for (uint32_t a = 0x9992; a < 0x99A2; a++) ram[a] = (uint8_t)rnd();   /* the game flags */
                ram[0x9668] = (uint8_t)(rnd() % 4 ? rnd() % 16 : 0x0F);
                put_word(0xD802 + e, rnd());                                        /* its flag; the scene's message */
            }
        }
        uint32_t sp = m68k_get_reg(NULL, M68K_REG_SP) - 4;
        bus_write16(sp, 0x0000); bus_write16(sp + 2, 0x0200);   /* return to an address the check recognises */
        m68k_set_reg(M68K_REG_SP, sp);
        m68k_set_reg(M68K_REG_PC, R(idx)->addr);
        long budget = 0;                            /* run until the check closes; a hang is reported */
        do { m68k_execute(1000000); budget++; } while (shadow.open && budget < 500);
        if (shadow.open) { fprintf(stderr, "fuzz: %s did not return within 500M cycles (case %ld)\n", name, k + 1); count = k; break; }
    }
    fuzzing = 0; watch = 0; nframes = 0;                        /* no caller after a fuzz case: nothing to watch */
    md_restore(base, n); free(base);
    if (scenes) fprintf(stderr, "fuzz: %ld collision scenes, %ld on a slope, %ld on a solid block\n", scenes, on_slope, on_solid);
    fprintf(stderr, "fuzz %s: %ld cases run, %ld mismatches\n", name, count, stats[idx].bad - before);
    return stats[idx].bad != before;
}

void hybrid_report(void)
{
    if (mode == PORT_OFF) return;
    fprintf(stderr, "%s mode:\n", mode == PORT_SHADOW ? "shadow" : "replace");
    long total = 0, bad = 0;
    for (int i = 0; i < NR; i++) {
        fprintf(stderr, "  %-16s $%06X  calls %8ld", R(i)->name, (unsigned)R(i)->addr, stats[i].calls);
        if (mode == PORT_SHADOW) fprintf(stderr, "  mismatches %ld", stats[i].bad);
        if (mode == PORT_SHADOW && stats[i].dead) fprintf(stderr, "  (dead registers left different: %ld)", stats[i].dead);
        fprintf(stderr, "\n");
        total += stats[i].calls; bad += stats[i].bad;
    }
    for (int i = 0; i < nleaves; i++)
        fprintf(stderr, "  left generated code at %06X for 68000 code at %06X, %ld times\n",
                (unsigned)leaves[i].from, (unsigned)leaves[i].to, leaves[i].n);
    if (mode == PORT_SHADOW) fprintf(stderr, "  all: %ld calls checked, %ld mismatches\n", total, bad);
    if (visited) {                      /* one line per routine: name start end, then executed PCs */
        FILE *f = fopen(cov_path, "w");
        if (!f) { perror(cov_path); return; }
        for (int i = 0; i < NR; i++) {
            const PortRoutine *p = R(i);
            fprintf(f, "%s %06X %06X", p->name, (unsigned)p->addr, (unsigned)p->end);
            for (uint32_t a = p->addr; a < p->end; a++) if (visited[a]) fprintf(f, " %06X", (unsigned)a);
            fprintf(f, "\n");
        }
        fclose(f);
    }
}
