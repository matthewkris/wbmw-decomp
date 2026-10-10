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

/* memory for port/, through the bus so VDP and Z80 writes behave as on the machine */
uint8_t rd8(uint32_t a) { return (uint8_t)bus_read8(a); }
uint16_t rd16(uint32_t a) { return (uint16_t)bus_read16(a); }
void wr8(uint32_t a, uint8_t v) { bus_write8(a, v); }
void wr16(uint32_t a, uint16_t v) { bus_write16(a, v); }

static int mode;
static int break_idx = -1;            /* self-test: corrupt this routine's result (WBMW_PORT_BREAK=name) */
static int16_t *slot_of;              /* routine index for each even ROM address, -1 if none */
#define SLOTS 0x20000
static struct { long calls, bad; } stats[256];

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
static uint8_t *snap; static size_t snap_n;
static struct { int open, idx; uint32_t ret, sp; long call; } shadow;
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

static void check(void)
{
    const PortRoutine *p = &port_routines[shadow.idx];
    capture(got);                                   /* the original's result */
    Cpu *a = &got->cpu, *b = &want->cpu;
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
    md_irq_hold(0);
}

static void run_port(int idx)
{
    Cpu c; get_cpu(&c);
    port_routines[idx].fn(&c);
    if (idx == break_idx) c.ccr ^= CC_X;            /* one flag, the subtlest thing to get wrong */
    uint32_t ret = rd32(c.a[7]);                    /* the original's rts */
    c.a[7] += 4;
    set_cpu(&c);
    m68k_set_reg(M68K_REG_PC, ret);
}

int md_instr_hook(unsigned int pc)
{
    if (mode == PORT_OFF) return 0;
    if (shadow.open) {
        if (pc == shadow.ret && m68k_get_reg(NULL, M68K_REG_SP) == shadow.sp) check();
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
    md_irq_hold(1);                                 /* before the restore, so no interrupt is taken here */
    run_port(idx);
    capture(want);
    md_restore(snap, snap_n);
    m68k_set_reg(M68K_REG_PC, pc);
    shadow.open = 1;
    return 0;
}

void hybrid_init(int m)
{
    mode = m;
    if (mode == PORT_OFF) return;
    slot_of = malloc(SLOTS * sizeof *slot_of);
    for (int i = 0; i < SLOTS; i++) slot_of[i] = -1;
    for (int i = 0; i < port_routine_count; i++) slot_of[port_routines[i].addr] = (int16_t)i;
    const char *brk = getenv("WBMW_PORT_BREAK");
    for (int i = 0; brk && i < port_routine_count; i++)
        if (!strcmp(brk, port_routines[i].name)) { break_idx = i; fprintf(stderr, "self-test: corrupting %s\n", brk); }
    want = malloc(sizeof *want); got = malloc(sizeof *got);
}

void hybrid_report(void)
{
    if (mode == PORT_OFF) return;
    fprintf(stderr, "%s mode:\n", mode == PORT_SHADOW ? "shadow" : "replace");
    long total = 0, bad = 0;
    for (int i = 0; i < port_routine_count; i++) {
        fprintf(stderr, "  %-16s $%06X  calls %8ld", port_routines[i].name, (unsigned)port_routines[i].addr, stats[i].calls);
        if (mode == PORT_SHADOW) fprintf(stderr, "  mismatches %ld", stats[i].bad);
        fprintf(stderr, "\n");
        total += stats[i].calls; bad += stats[i].bad;
    }
    if (mode == PORT_SHADOW) fprintf(stderr, "  all: %ld calls checked, %ld mismatches\n", total, bad);
}
