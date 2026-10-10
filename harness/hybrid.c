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
static int fuzzing;
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
    if (fuzzing) m68k_end_timeslice();              /* one fuzz case per execute */
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

/* coverage: which instructions the originals executed inside shadow checks */
static uint8_t *visited; static const char *cov_path;
void hybrid_coverage(const char *path) { cov_path = path; }

int md_instr_hook(unsigned int pc)
{
    if (mode == PORT_OFF) return 0;
    if (shadow.open) {
        if (visited && pc < SLOTS) visited[pc] = 1;
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
    if (visited) visited[pc] = 1;                   /* the entry instruction runs inside the check */
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
    if (cov_path && mode == PORT_SHADOW) visited = calloc(SLOTS, 1);
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

int hybrid_fuzz(const char *name, long count)
{
    int idx = -1;
    for (int i = 0; i < port_routine_count; i++) if (!strcmp(port_routines[i].name, name)) idx = i;
    if (idx < 0 || mode != PORT_SHADOW) { fprintf(stderr, "fuzz: %s is not a ported routine (or not in shadow mode)\n", name); return -1; }
    size_t n; const uint8_t *s = md_snapshot(&n);
    uint8_t *base = malloc(n); memcpy(base, s, n);
    long before = stats[idx].bad;
    fuzzing = 1;
    for (long k = 0; k < count; k++) {
        md_restore(base, n);
        for (int i = 0; i < 8; i++) m68k_set_reg(M68K_REG_D0 + i, rnd() << 16 ^ rnd());
        m68k_set_reg(M68K_REG_A4, (rnd() & 63) * 4);
        m68k_set_reg(M68K_REG_SR, (m68k_get_reg(NULL, M68K_REG_SR) & ~0x1Fu) | (rnd() & 0x1F));
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
        uint32_t sp = m68k_get_reg(NULL, M68K_REG_SP) - 4;
        bus_write16(sp, 0x0000); bus_write16(sp + 2, 0x0200);   /* return to an address the check recognises */
        m68k_set_reg(M68K_REG_SP, sp);
        m68k_set_reg(M68K_REG_PC, port_routines[idx].addr);
        long budget = 0;                            /* run until the check closes; a hang is reported */
        do { m68k_execute(1000000); budget++; } while (shadow.open && budget < 500);
        if (shadow.open) { fprintf(stderr, "fuzz: %s did not return within 500M cycles (case %ld)\n", name, k + 1); count = k; break; }
    }
    fuzzing = 0;
    md_restore(base, n); free(base);
    fprintf(stderr, "fuzz %s: %ld cases run, %ld mismatches\n", name, count, stats[idx].bad - before);
    return stats[idx].bad != before;
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
    if (visited) {                      /* one line per routine: name start end, then executed PCs */
        FILE *f = fopen(cov_path, "w");
        if (!f) { perror(cov_path); return; }
        for (int i = 0; i < port_routine_count; i++) {
            const PortRoutine *p = &port_routines[i];
            fprintf(f, "%s %06X %06X", p->name, (unsigned)p->addr, (unsigned)p->end);
            for (uint32_t a = p->addr; a < p->end; a++) if (visited[a]) fprintf(f, " %06X", (unsigned)a);
            fprintf(f, "\n");
        }
        fclose(f);
    }
}
