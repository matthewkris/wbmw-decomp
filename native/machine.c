/* The native build's machine core, by hand: what the harness must leave to the 68000 (it sets
 * the status register or waits on the hardware), written for the native host (wbmw.c).
 *
 *   $200   reset: the hardware set up from the table at $28E, the work RAM cleared, then $4A06
 *   $4A06  restart: the VDP, the Z80's program, the pads, the tables, then the first scene ($50E2)
 *   $4BF8  the frame loop: the stack reset; each frame the frame wait ($3B8), the four system
 *          tasks ($4BBC), the sprites ($53AA) and the user tasks ($4C00)
 *   $3B8   the frame wait: the frame counters, the wait for the VBlank interrupt (the host draws
 *          the frame there and runs the interrupt), the frame's work ($5262)
 *   $4B5C  the VBlank interrupt; $50D4 a scene switch with the display off; $C262 the inn's sleep
 *
 * Every task pass runs on port/task.c's coroutines (TaskCallSystem and TaskCallUser, the
 * scheduler's two `jsr (a0)`), so the hand ports that wait for frames run as in the harness.
 *
 * Calls by address (rt_dispatch) go to the hand ports (port/registry.c) and these; what neither
 * has is left to the translated code (work/gen_native, its own dispatcher renamed gen_dispatch)
 * and counted, until there is none. */
#include <stdio.h>
#include <stdlib.h>
#include "../port/world.h"
#include "../port/m68k_rt.h"
#include "../port/task.h"

void rt_poll(Cpu *c, uint32_t at);                  /* the host: a frame, at a wait for VBlank */
void rt_set_sr(Cpu *c, uint16_t sr);
int gen_dispatch(Cpu *c, uint32_t target);          /* the translated code's (work/gen_native) */
void TaskCallSystem(Cpu *c);
void TaskCallUser(Cpu *c);

static Cpu *c;
static void call(uint32_t addr) { Cpu *self = c; call_68k(c, addr); c = self; }
static void push32(uint32_t v) { c->a[7] -= 4; wr32(c->a[7], v); }

#define RESTART     0x4BF8u
#define HANG        0x4CB4u

/* ---- the translated code still in use, counted (WBMW_GEN_STATS=1 prints it at exit) ---- */
#define GEN_MAX 512
static struct { uint32_t addr; unsigned long n; } gen_used[GEN_MAX];
static int gen_used_n;
static void gen_count(uint32_t t)
{
    int i = 0;
    while (i < gen_used_n && gen_used[i].addr != t) i++;
    if (i == gen_used_n && gen_used_n < GEN_MAX) { gen_used[i].addr = t; gen_used_n++; }
    if (i < gen_used_n) gen_used[i].n++;
}
static void gen_report(void)
{
    if (!getenv("WBMW_GEN_STATS")) return;
    fprintf(stderr, "translated code entered at %d addresses:", gen_used_n);
    for (int i = 0; i < gen_used_n; i++) fprintf(stderr, " %06X(%lu)", (unsigned)gen_used[i].addr, gen_used[i].n);
    fprintf(stderr, "\n");
}

/* ---- the hand ports by address ---- */
static const PortRoutine **by_addr;
static int by_addr_n;
static int cmp_addr(const void *a, const void *b)
{
    uint32_t x = (*(const PortRoutine *const *)a)->addr, y = (*(const PortRoutine *const *)b)->addr;
    return x < y ? -1 : x > y;
}
static const PortRoutine *hand(uint32_t a)
{
    if (!by_addr) {
        by_addr = malloc(sizeof *by_addr * (size_t)port_routine_count);
        for (int i = 0; i < port_routine_count; i++) by_addr[by_addr_n++] = &port_routines[i];
        qsort(by_addr, (size_t)by_addr_n, sizeof *by_addr, cmp_addr);
        atexit(gen_report);
    }
    int lo = 0, hi = by_addr_n - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (by_addr[m]->addr == a) return by_addr[m];
        if (by_addr[m]->addr < a) lo = m + 1; else hi = m - 1;
    }
    return NULL;
}

/* ---- $3B8: the frame wait ---- */
/* from pc: $3B8 (the start), $3D6, $3DC/$3E2 (waiting), $3E4 (the frame's work), $3EA-$3FC
 * (waiting out the VBlank, with $8A5D bit 6); a loaded state may stop anywhere in it */
static void frame_wait_from(uint32_t pc)
{
    if (pc < 0x3D6) {
        wr32(0xFFFF8002u, rd32(0xFFFF8002u) + 1);
        wr32(0xFFFF8A56u, rd32(0xFFFF8A56u) + 1);
        c->d[0] = set_b(c->d[0], rd8(0xFFFF8A5Du));
        c->d[1] = set_b(c->d[1], rd8(0xFFFF8A7Eu) ^ (c->d[0] & 0xFF));
        c->d[1] = set_b(c->d[1], c->d[1] & 0x40);
        if (!(c->d[1] & 0xFF)) {
            c->d[0] = set_b(c->d[0], c->d[0] & 0x40);
            if (!(c->d[0] & 0xFF)) { c->ccr = (uint16_t)((c->ccr & CC_X) | CC_Z); return; }   /* no wait */
        }
        pc = 0x3D6;
    }
    if (pc == 0x3D6) { wr8(0xFFFF8006u, rd8(0xFFFF8006u) & 0xFE); pc = 0x3DC; }
    if (pc <= 0x3E2) {
        while (!(rd8(0xFFFF8006u) & 1)) rt_poll(c, 0x3DC);          /* the host: a frame and its interrupt */
        pc = 0x3E4;
    }
    if (pc == 0x3E4) { call(0x5262); pc = 0x3EA; }
    if (pc <= 0x3FC) {
        while ((rd8(0xFFFF8A5Du) & 0x40) && (rd16(0xC00004) & 8)) { }
        c->d[0] = set_w(c->d[0], rd16(0xC00004) & 8);
    }
}
static void frame_wait(void) { frame_wait_from(0x3B8); }

/* $4B5C: the VBlank interrupt: the interrupts counted ($8A49), this frame's flag ($8006 bit 0) */
void machine_isr(Cpu *cpu)
{
    wr8(0xFFFF8A49u, (uint8_t)(rd8(0xFFFF8A49u) + 1));
    wr8(0xFFFF8006u, rd8(0xFFFF8006u) | 1);
    uint16_t sr = rd16(cpu->a[7]); cpu->a[7] += 6;   /* rte: the frame the host pushed */
    rt_set_sr(cpu, sr);
}

/* ---- the task slots ---- */
/* $4BBC: the four system slots ($8048, $80 apart) whose status has bit 7, each a pass; 0 if
 * the pass left for the restart (c->ret says where) */
static int system_tasks(void)
{
    wr8(0xFFFF8A48u, 0x80);
    c->a[5] = 0xFFFF8048u;
    do {
        if (rd8(c->a[5]) & 0x80) {
            wr16(0xFFFF8A4Cu, (uint16_t)c->a[5]);
            c->a[0] = rd32(c->a[5] + 0xC);
            c->ret = 0;
            TaskCallSystem(c);                       /* jsr (a0) on the slot's coroutine */
            if (c->ret != 0x4BD8) return 0;
            c->a[5] = (uint32_t)(int32_t)(int16_t)rd16(0xFFFF8A4Cu);
        }
        c->a[5] += 0x80;
        wr8(0xFFFF8A48u, (uint8_t)(rd8(0xFFFF8A48u) + 1));
    } while (rd8(0xFFFF8A48u) < 0x84);
    return 1;
}
/* $4C6E: the next user slot ($8248 on, $8A4A its number), a pass if it runs */
static int user_task(void)
{
    wr8(0xFFFF8A49u, 0);
    if (rd8(c->a[5]) & 0x80) {
        wr16(0xFFFF8A4Cu, (uint16_t)c->a[5]);
        c->a[0] = rd32(c->a[5] + 0xC);
        wr8(0xFFFF8A48u, rd8(0xFFFF8A4Au));
        c->ret = 0;
        TaskCallUser(c);
        if (c->ret != 0x4C8A) return 0;
        c->a[5] = (uint32_t)(int32_t)(int16_t)rd16(0xFFFF8A4Cu);
    }
    c->a[5] += 0x80;
    wr8(0xFFFF8A4Au, (uint8_t)(rd8(0xFFFF8A4Au) + 1));
    if (rd8(0xFFFF8A4Au) >= 0x10) { wr8(0xFFFF8A4Au, 0); c->a[5] = 0xFFFF8248u; }
    return 1;
}
/* $4C00: sixteen user passes; with $8A5D bit 6, only while the frame has time (no VBlank yet,
 * the raster above line $D0), from where the last frame stopped */
static int user_tasks(void)
{
    if ((rd8(0xFFFF8A5Du) & 0x40) && rd8(0xFFFF8A49u) < 0x10) {
        c->a[5] = 0xFFFF8248u + ((uint32_t)rd8(0xFFFF8A4Au) << 7);
        c->a[5] = (uint32_t)(int32_t)(int16_t)c->a[5];
        wr8(0xFFFF8A4Bu, 0x10);
        for (;;) {
            if (rd16(0xC00004) & 8) return 1;
            uint16_t v, w = rd16(0xC00008) & 0xFF00;
            do { v = w; w = rd16(0xC00008) & 0xFF00; } while (v != w);
            if (v >= 0xD000) return 1;
            if (!user_task()) return 0;
            wr8(0xFFFF8A4Bu, (uint8_t)(rd8(0xFFFF8A4Bu) - 1));
            if (!rd8(0xFFFF8A4Bu)) return 1;
        }
    }
    wr8(0xFFFF8A4Bu, 0x10);
    c->a[5] = 0xFFFF8248u;
    do {
        if (!user_task()) return 0;
        wr8(0xFFFF8A4Bu, (uint8_t)(rd8(0xFFFF8A4Bu) - 1));
    } while (rd8(0xFFFF8A4Bu));
    return 1;
}

/* ---- the frame loop ---- */
/* $4BF8 on, until the game hangs ($4CB4); from_wait: a loaded state, stopped at `pc` in the
 * frame wait. Returns the address it stopped at. */
static uint32_t frame_loop(int from_wait, uint32_t pc)
{
    for (;;) {
        if (!from_wait) {
            c->a[7] = 0xFFFF0C00u;                   /* $4BF8 */
            push32(0x4BF6);                          /* $4BF2: bsr $3b8 */
            frame_wait();
            c->a[7] += 4;
        } else {
            from_wait = 0;
            frame_wait_from(pc);
            c->a[7] += 4;                            /* its rts to $4BF6 */
        }
        for (;;) {                                   /* $4BBC */
            if (!system_tasks()) break;
            push32(0x4BF0); call(0x53AA); c->a[7] += 4;
            push32(0x4BF2);
            int ok = user_tasks();
            c->a[7] += 4;
            if (!ok) break;
            push32(0x4BF6); frame_wait(); c->a[7] += 4;
        }
        uint32_t to = c->ret & 0xFFFFFF;            /* a pass left the loop */
        if (to == RESTART) continue;
        return to;
    }
}

/* $50E2 (port/vblank.c's, which runs on a task pass) for the restart, which is not on one: the
 * tasks reset, the first system slot given the scene a1 names (its name, its resume address,
 * d0), then the frame loop ($4BF8) */
static void scene_start(void)
{
    push32(c->a[1]); call(0x4B6A); c->a[1] = rd32(c->a[7]); c->a[7] += 4;
    c->a[0] = 0xFFFF8048u;
    wr32(c->a[0] + 4, rd32(c->a[1])); c->a[1] += 4;
    wr32(c->a[0] + 0xC, c->a[1]);
    wr16(c->a[0] + 0x40, (uint16_t)c->d[0]);
    wr16(c->a[0], 0x8000);
    c->ret = RESTART;
}

/* $4A06: the restart: the VDP idle, the hardware and the game's tables set up, the interrupts
 * on, the pads read twice, then the first scene ($4CC4, by $50E2, which ends at $4BF8) */
static void restart_game(void)
{
    do c->d[0] = set_w(c->d[0], 2 & rd16(0xC00004)); while (c->d[0] & 0xFFFF);
    rt_set_sr(c, 0x2700);
    push32(0x4A16); call(0x4A7A); c->a[7] += 4;
    wr32(0xC00004, 0xC0000000u);
    wr16(0xC00000, 0);
    call(0x306); call(0x4B02); call(0x5170); call(0x6BA0); call(0x5398); call(0x4B42); call(0x9F46);
    wr8(0xFFFF8A50u, 0); wr8(0xFFFF8A51u, 0); wr8(0xFFFF9855u, 0);
    c->d[0] = 1 & rd16(0xC00004);
    wr8(0xFFFF8A8Bu, (uint8_t)c->d[0]);
    wr8(0xFFFF8006u, 1);
    rt_set_sr(c, 0x2500);
    call(0x64E); call(0x51C6); call(0x51C6);
    c->a[1] = 0x4CC4;                               /* $4CB8: jmp $50e2 */
    scene_start();
}

/* $200: reset: unless warm (a pad port already set up), the hardware from the table at $28E:
 * the security code, the VDP's registers, a DMA fill of video memory, the Z80's little program,
 * the work RAM, colours and scroll cleared, the sound chip silenced; then $2FA, $4A06 */
static void reset_game(void)
{
    if (!rd32(0xA10008) && !rd16(0xA1000C)) {
        uint32_t t = 0x28E;
        c->d[5] = rd16(t); c->d[6] = rd16(t + 2); c->d[7] = rd16(t + 4); t += 6;
        for (int k = 0; k < 5; k++) { c->a[k] = rd32(t); t += 4; }
        if (rd8(c->a[1] - 0x10FF) & 0xF) wr32(c->a[1] + 0x2F00, 0x53454741u);   /* 'SEGA' */
        (void)rd16(c->a[4]);
        c->d[0] = 0; c->a[6] = 0;
        for (int k = 0; k < 24; k++) {               /* the VDP's registers */
            c->d[5] = set_b(c->d[5], rd8(t)); t++;
            wr16(c->a[4], (uint16_t)c->d[5]);
            c->d[5] = set_w(c->d[5], c->d[5] + c->d[7]);
        }
        wr32(c->a[4], rd32(t)); t += 4;              /* a DMA fill of video memory */
        wr16(c->a[3], 0);
        wr16(c->a[1], (uint16_t)c->d[7]);            /* the Z80's bus, out of reset */
        wr16(c->a[2], (uint16_t)c->d[7]);
        while (rd8(c->a[1]) & 1) { }
        for (int k = 0; k < 0x26; k++) { wr8(c->a[0] + (uint32_t)k, rd8(t)); t++; }   /* its program */
        c->a[0] += 0x26;
        wr16(c->a[2], 0); wr16(c->a[1], 0); wr16(c->a[2], (uint16_t)c->d[7]);
        for (int k = 0; k < 0x4000; k++) { c->a[6] -= 4; wr32(c->a[6], 0); }      /* the work RAM */
        wr32(c->a[4], rd32(t)); t += 4;
        wr32(c->a[4], rd32(t)); t += 4;
        for (int k = 0; k < 32; k++) wr32(c->a[3], 0);                              /* colours */
        wr32(c->a[4], rd32(t)); t += 4;
        for (int k = 0; k < 20; k++) wr32(c->a[3], 0);                              /* scroll */
        for (int k = 0; k < 4; k++) { wr8(c->a[3] + 0x11, rd8(t)); t++; }           /* the PSG silenced */
        wr16(c->a[2], 0);
        for (int k = 0; k < 8; k++) c->d[k] = 0;
        for (int k = 0; k < 7; k++) c->a[k] = 0;
        rt_set_sr(c, 0x2700);
    }
    (void)rd16(0xC00004);                            /* $2FA */
}

/* the host's entry: from reset, or from a state stopped in the frame wait at `pc` */
int machine_run(Cpu *cpu, uint32_t pc)
{
    c = cpu;
    uint32_t to;
    if (pc >= 0x3B8 && pc <= 0x3FE) to = frame_loop(1, pc);
    else {
        reset_game();
        restart_game();
        to = c->ret & 0xFFFFFF;
        to = to == RESTART ? frame_loop(0, 0) : to;
    }
    if (to == HANG) { fprintf(stderr, "wbmw: the game stopped (no free task slot, $4CB4)\n"); return 5; }
    fprintf(stderr, "wbmw: control went to $%06X, which has no C\n", (unsigned)to);
    return 4;
}

/* ---- the routines here that the game reaches by address ---- */
/* $50D4: to the scene a1 names, the palettes cleared and the display off first (port/vblank.c's
 * $50E2 for the rest) */
static void scene_switch_off(void)
{
    push32(c->a[1]);
    call(0x559E); call(0x62A);
    c->a[1] = rd32(c->a[7]); c->a[7] += 4;
    hand(0x50E2)->fn(c);
}
/* $C262: the inn's sleep: d0+1 VBlank interrupts waited for, then the game's room entry ($8EB4) */
static void inn_sleep(void)
{
    do {
        while (!(rd8(0xFFFF8006u) & 1)) rt_poll(c, 0xC262);
        wr8(0xFFFF8006u, rd8(0xFFFF8006u) & 0xFE);
        c->d[0] = set_w(c->d[0], c->d[0] - 1);
    } while ((c->d[0] & 0xFFFF) != 0xFFFF);
    wr8(0xFFFF9658u, 0);
    port_continue_at(c, 0x8EB4, 0);
}

int rt_dispatch(Cpu *cpu, uint32_t t)
{
    t &= 0xFFFFFF;
    if (t == RESTART || t == 0x4A06 || t == HANG) { cpu->ret = t; return 1; }   /* the frame loop's own */
    const PortRoutine *p = hand(t);
    if (p) { cpu->ret = 0; p->fn(cpu); rt_after_hand(cpu); return 1; }
    Cpu *outer = c;
    c = cpu;
    switch (t) {
    case 0x3B8: frame_wait(); c->ret = 0; break;
    case 0x50D4: cpu->ret = 0; scene_switch_off(); break;
    case 0xC262: cpu->ret = 0; inn_sleep(); break;
    default:
        c = outer;
        if (gen_dispatch(cpu, t)) { gen_count(t); return 1; }
        return 0;
    }
    c = outer;
    rt_after_hand(cpu);
    return 1;
}
